// Java HardwareBuffer retains the Android lifecycle and JNI signatures. Only
// allocation and the opaque Parcel payload cross into the OH buffer provider.
#include "oh61_buffer_handle.h"
#include <android/hardware_buffer.h>
#include <android/hardware_buffer_jni.h>
#include <android/graphics/canvas.h>
#include <android_os_Parcel.h>
#include <binder/Parcel.h>
#include <native_buffer/native_buffer.h>
#include <native_buffer_inner.h>
#include <surface_buffer.h>
#include <buffer_handle_utils.h>
#include <nativehelper/JNIHelp.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>
#include <cstdio>

namespace {
jclass bufferClass;
jclass graphicClass;
jfieldID graphicObject;
jmethodID graphicConstructor;
jfieldID nativeObject;
jmethodID constructor;
AHardwareBuffer* buffer(jlong value) { return reinterpret_cast<AHardwareBuffer*>(value); }
void destroy(jlong value) { AHardwareBuffer_release(buffer(value)); }
void throwState(JNIEnv* env, const char* text) { jniThrowException(env, "java/lang/IllegalStateException", text); }

jlong create(JNIEnv*, jclass, jint width, jint height, jint format, jint layers, jlong usage) {
    AHardwareBuffer_Desc desc{};
    desc.width = width; desc.height = height; desc.layers = layers; desc.format = format; desc.usage = usage;
    AHardwareBuffer* out = nullptr;
    return AHardwareBuffer_allocate(&desc, &out) ? 0 : reinterpret_cast<jlong>(out);
}
jlong finalizer(JNIEnv*, jclass) { return reinterpret_cast<jlong>(&destroy); }
jboolean supported(JNIEnv* env, jclass clazz, jint width, jint height, jint format, jint layers, jlong usage) {
    jlong result = create(env, clazz, width, height, format, layers, usage);
    if (result) destroy(result);
    return result ? JNI_TRUE : JNI_FALSE;
}
AHardwareBuffer_Desc describe(jlong value) {
    AHardwareBuffer_Desc desc{}; if (value) AHardwareBuffer_describe(buffer(value), &desc); return desc;
}
jint width(JNIEnv*, jclass, jlong value) { return describe(value).width; }
jint height(JNIEnv*, jclass, jlong value) { return describe(value).height; }
jint format(JNIEnv*, jclass, jlong value) { return describe(value).format; }
jint layers(JNIEnv*, jclass, jlong value) { return describe(value).layers; }
jlong usage(JNIEnv*, jclass, jlong value) { return describe(value).usage; }
// These two Java methods are @CriticalNative: no JNIEnv or jclass arguments.
jlong size(jlong value) {
    auto* h = value ? OH_NativeBuffer_GetBufferHandle(reinterpret_cast<OH_NativeBuffer*>(value)) : nullptr;
    return h && h->size > 0 ? h->size : 0;
}
jlong id(jlong value) {
    auto* h = value ? OH_NativeBuffer_GetBufferHandle(reinterpret_cast<OH_NativeBuffer*>(value)) : nullptr;
    // Linux identifies the backing shared allocation, including across dup/Parcel imports.
    struct stat st{};
    if (!h || h->fd < 0 || fstat(h->fd, &st)) return 0;
    return st.st_ino;
}
jlong fromGraphicBuffer(JNIEnv* env, jclass, jobject object) {
    if (!object) return 0;
    if (!graphicClass || !env->IsInstanceOf(object, graphicClass)) return 0;
    jlong value = env->GetLongField(object, graphicObject);
    if (!value) { throwState(env, "GraphicBuffer is closed"); return 0; }
    AHardwareBuffer_acquire(buffer(value));
    return value;
}

// Android treats a HardwareBuffer's parcel bytes as a platform-private payload.
// Transfer each OH descriptor field and duplicate FD through real libbinder;
// process-local addresses are never serialized. Versioned to reject other ABIs.
constexpr int32_t kParcelMagic = 0x4f484231; // OHB1
void write(JNIEnv* env, jclass, jlong value, jobject dest) {
    auto* p = android::parcelForJavaObject(env, dest);
    auto* h = value ? OH_NativeBuffer_GetBufferHandle(reinterpret_cast<OH_NativeBuffer*>(value)) : nullptr;
    if (!p || !h || h->reserveFds > 1024 || h->reserveInts > 1024) {
        throwState(env, "Invalid HardwareBuffer parcel"); return;
    }
    android::status_t rc = 0;
    auto integer = [&](int32_t n) { if (!rc) rc = p->writeInt32(n); };
    integer(kParcelMagic); integer(h->width); integer(h->height); integer(h->stride);
    integer(h->size); integer(h->format);
    if (!rc) rc = p->writeUint64(h->usage);
    integer(h->reserveFds); integer(h->reserveInts);
    auto fd = [&](int descriptor) {
        integer(descriptor >= 0);
        if (!rc && descriptor >= 0) rc = p->writeDupFileDescriptor(descriptor);
    };
    fd(h->fd);
    for (uint32_t i = 0; i < h->reserveFds; ++i) fd(h->reserve[i]);
    for (uint32_t i = 0; i < h->reserveInts; ++i) integer(h->reserve[h->reserveFds + i]);
    fprintf(stderr, "[SOURCE-HARDWARE-BUFFER] parcel written status=%d fds=%u ints=%u\n", rc, h->reserveFds, h->reserveInts);
    if (rc) {
        fprintf(stderr, "[SOURCE-HARDWARE-BUFFER] parcel write status=%d fd=%d reserveFds=%u reserveInts=%u dataSize=%zu position=%zu\n",
                rc, h->fd, h->reserveFds, h->reserveInts, p->dataSize(), p->dataPosition());
        throwState(env, "Unable to write HardwareBuffer parcel");
    }
}
jlong read(JNIEnv* env, jclass, jobject source) {
    auto* p = android::parcelForJavaObject(env, source);
    if (!p) return 0;
    int32_t magic, w, h, stride, bytes, fmt, fds, ints;
    uint64_t use;
    if (p->readInt32(&magic) || magic != kParcelMagic || p->readInt32(&w) ||
        p->readInt32(&h) || p->readInt32(&stride) || p->readInt32(&bytes) ||
        p->readInt32(&fmt) || p->readUint64(&use) || p->readInt32(&fds) || p->readInt32(&ints) ||
        w <= 0 || h <= 0 || stride <= 0 || bytes <= 0 || fds < 0 || ints < 0 ||
        fds > 1024 || ints > 1024) return 0;
    // The carrier's parameter names are reversed. The OH 6 implementation takes
    // reserveFds first, as its initialization loop and on-device ABI confirm.
    std::unique_ptr<BufferHandle, decltype(&FreeBufferHandle)> handle(AllocateBufferHandle(fds, ints), FreeBufferHandle);
    if (!handle) return 0;
    fprintf(stderr, "[SOURCE-HARDWARE-BUFFER] read handle fds=%u ints=%u\n", handle->reserveFds, handle->reserveInts);
    handle->width = w; handle->height = h; handle->stride = stride; handle->size = bytes;
    handle->format = fmt; handle->usage = use;
    handle->virAddr = nullptr; handle->phyAddr = 0;
    auto fd = [&](int32_t* output) {
        int32_t present;
        if (p->readInt32(&present) || (present != 0 && present != 1)) return false;
        *output = -1;
        if (!present) return true;
        int borrowed = p->readFileDescriptor();
        if (borrowed < 0) return false;
        *output = fcntl(borrowed, F_DUPFD_CLOEXEC, 0);
        return *output >= 0;
    };
    if (!fd(&handle->fd)) return 0;
    for (int i = 0; i < fds; ++i) if (!fd(&handle->reserve[i])) return 0;
    for (int i = 0; i < ints; ++i) if (p->readInt32(&handle->reserve[fds + i])) return 0;
    auto host = OHOS::SurfaceBuffer::Create();
    if (!host) return 0;
    fprintf(stderr, "[SOURCE-HARDWARE-BUFFER] read surface created\n");
    host->SetBufferHandle(handle.release()); // adopts descriptor and registers with the OH allocator
    fprintf(stderr, "[SOURCE-HARDWARE-BUFFER] read handle set\n");
    if (!host->GetBufferHandle()) return 0;
    auto* native = host->SurfaceBufferToNativeBuffer();
    fprintf(stderr, "[SOURCE-HARDWARE-BUFFER] read native converted\n");
    if (OH_NativeBuffer_Reference(native)) return 0;
    return reinterpret_cast<jlong>(native);
}
jlong createGraphic(JNIEnv* env, jclass clazz, jint w, jint h, jint format, jint usage) {
    return create(env, clazz, w, h, format, 1, static_cast<uint32_t>(usage));
}
void destroyGraphic(JNIEnv*, jclass, jlong value) { destroy(value); }
jobject graphicFromHardware(JNIEnv* env, jclass, jobject hardware) {
    AHardwareBuffer* native = AHardwareBuffer_fromHardwareBuffer(env, hardware);
    if (!native) return nullptr;
    AHardwareBuffer_Desc d{}; AHardwareBuffer_describe(native, &d);
    AHardwareBuffer_acquire(native);
    jobject result = env->NewObject(graphicClass, graphicConstructor, static_cast<jint>(d.width),
            static_cast<jint>(d.height), static_cast<jint>(d.format), static_cast<jint>(d.usage),
            reinterpret_cast<jlong>(native));
    if (!result) AHardwareBuffer_release(native);
    return result;
}
jboolean lockCanvas(JNIEnv* env, jclass, jlong value, jobject canvasObject, jobject dirty) {
    if (!value || !canvasObject) return JNI_FALSE;
    auto d = describe(value);
    ARect rect{0, 0, static_cast<int32_t>(d.width), static_cast<int32_t>(d.height)};
    if (dirty) {
        jclass cls = env->GetObjectClass(dirty);
        const char* fields[] = {"left", "top", "right", "bottom"};
        int32_t* values[] = {&rect.left, &rect.top, &rect.right, &rect.bottom};
        for (unsigned i=0; i<4; ++i) {
            jfieldID id = env->GetFieldID(cls, fields[i], "I");
            if (!id) { env->DeleteLocalRef(cls); return JNI_FALSE; }
            *values[i] = env->GetIntField(dirty, id);
        }
        env->DeleteLocalRef(cls);
    }
    void* address = nullptr;
    if (AHardwareBuffer_lock(buffer(value), AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
            AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, &rect, &address)) return JNI_FALSE;
    ANativeWindow_Buffer pixels{};
    pixels.width=d.width; pixels.height=d.height; pixels.stride=d.stride;
    pixels.format=d.format; pixels.bits=address;
    android::graphics::Canvas canvas(env, canvasObject);
    if (!canvas.setBuffer(&pixels, ADATASPACE_UNKNOWN)) {
        AHardwareBuffer_unlock(buffer(value), nullptr); return JNI_FALSE;
    }
    canvas.clipRect(rect);
    return JNI_TRUE;
}
jboolean unlockCanvas(JNIEnv* env, jclass, jlong value, jobject canvasObject) {
    if (!value || !canvasObject) return JNI_FALSE;
    android::graphics::Canvas canvas(env, canvasObject);
    canvas.setBuffer(nullptr, ADATASPACE_UNKNOWN);
    return AHardwareBuffer_unlock(buffer(value), nullptr) ? JNI_FALSE : JNI_TRUE;
}
const JNINativeMethod graphicMethods[] = {
    {"nCreateGraphicBuffer", "(IIII)J", reinterpret_cast<void*>(createGraphic)},
    {"nDestroyGraphicBuffer", "(J)V", reinterpret_cast<void*>(destroyGraphic)},
    {"nWriteGraphicBufferToParcel", "(JLandroid/os/Parcel;)V", reinterpret_cast<void*>(write)},
    {"nReadGraphicBufferFromParcel", "(Landroid/os/Parcel;)J", reinterpret_cast<void*>(read)},
    {"nLockCanvas", "(JLandroid/graphics/Canvas;Landroid/graphics/Rect;)Z", reinterpret_cast<void*>(lockCanvas)},
    {"nUnlockCanvasAndPost", "(JLandroid/graphics/Canvas;)Z", reinterpret_cast<void*>(unlockCanvas)},
    {"nCreateFromHardwareBuffer", "(Landroid/hardware/HardwareBuffer;)Landroid/graphics/GraphicBuffer;",
        reinterpret_cast<void*>(graphicFromHardware)},
};
const JNINativeMethod methods[] = {
    {"nCreateHardwareBuffer", "(IIIIJ)J", reinterpret_cast<void*>(create)},
    {"nCreateFromGraphicBuffer", "(Landroid/graphics/GraphicBuffer;)J", reinterpret_cast<void*>(fromGraphicBuffer)},
    {"nGetNativeFinalizer", "()J", reinterpret_cast<void*>(finalizer)},
    {"nWriteHardwareBufferToParcel", "(JLandroid/os/Parcel;)V", reinterpret_cast<void*>(write)},
    {"nReadHardwareBufferFromParcel", "(Landroid/os/Parcel;)J", reinterpret_cast<void*>(read)},
    {"nIsSupported", "(IIIIJ)Z", reinterpret_cast<void*>(supported)},
    {"nGetWidth", "(J)I", reinterpret_cast<void*>(width)},
    {"nGetHeight", "(J)I", reinterpret_cast<void*>(height)},
    {"nGetFormat", "(J)I", reinterpret_cast<void*>(format)},
    {"nGetLayers", "(J)I", reinterpret_cast<void*>(layers)},
    {"nGetUsage", "(J)J", reinterpret_cast<void*>(usage)},
    {"nEstimateSize", "(J)J", reinterpret_cast<void*>(size)},
    {"nGetId", "(J)J", reinterpret_cast<void*>(id)},
};
} // namespace

namespace android {
int register_android_hardware_HardwareBuffer(JNIEnv* env) {
    jclass local = env->FindClass("android/hardware/HardwareBuffer");
    if (!local) return -1;
    if (env->RegisterNatives(local, methods, std::size(methods))) return -1;
    bufferClass = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    if (!bufferClass) return -1;
    nativeObject = env->GetFieldID(bufferClass, "mNativeObject", "J");
    constructor = env->GetMethodID(bufferClass, "<init>", "(J)V");
    if (!nativeObject || !constructor) return -1;
    local = env->FindClass("android/graphics/GraphicBuffer");
    if (!local) return -1;
    if (env->RegisterNatives(local, graphicMethods, std::size(graphicMethods))) return -1;
    graphicClass = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    if (!graphicClass) return -1;
    graphicObject = env->GetFieldID(graphicClass, "mNativeObject", "J");
    graphicConstructor = env->GetMethodID(graphicClass, "<init>", "(IIIIJ)V");
    return graphicObject && graphicConstructor ? 0 : -1;
}
}
extern "C" AHardwareBuffer* AHardwareBuffer_fromHardwareBuffer(JNIEnv* env, jobject object) {
    if (!object || !bufferClass || !env->IsInstanceOf(object, bufferClass)) return nullptr;
    return buffer(env->GetLongField(object, nativeObject));
}
extern "C" jobject AHardwareBuffer_toHardwareBuffer(JNIEnv* env, AHardwareBuffer* value) {
    if (!value || !bufferClass) return nullptr;
    AHardwareBuffer_acquire(value);
    jobject object = env->NewObject(bufferClass, constructor, reinterpret_cast<jlong>(value));
    if (!object) AHardwareBuffer_release(value);
    return object;
}
