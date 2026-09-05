/*
 * oh_surface_texture_jni.cpp
 *
 * Android SurfaceTexture JNI backed by OHOS OH_NativeImage.
 *
 * Keep the Android framework object, lifecycle, callbacks, and error contract
 * intact. Adapt only at the graphics boundary: OH_NativeImage is OHOS's
 * SurfaceTexture equivalent and owns the consumer queue, producer endpoint,
 * external-GL-texture binding, fences, transform, and timestamp.
 */

#include <native_image/native_image.h>
#include <native_window/external_window.h>

#include <jni.h>

#include <atomic>
#include <cstdio>

namespace android {
namespace {

constexpr uint32_t kSurfaceTextureMagic = 0x4f485354;  // "OHST"
constexpr uint32_t kTextureExternalOes = 0x8d65;       // GL_TEXTURE_EXTERNAL_OES

struct OhSurfaceTextureState {
    uint32_t magic = kSurfaceTextureMagic;
    OH_NativeImage* image = nullptr;
    OHNativeWindow* nativeWindow = nullptr;
    JavaVM* vm = nullptr;
    jobject weakThis = nullptr;              // global java.lang.ref.WeakReference
    jclass surfaceTextureClass = nullptr;    // global android.graphics.SurfaceTexture
    std::atomic<bool> released{false};
    std::atomic<bool> finalized{false};
    std::atomic<uint32_t> frameAvailableCount{0};
    int32_t texName = 0;
    bool detached = false;

    void PostFrameAvailable()
    {
        if (released.load(std::memory_order_acquire) || vm == nullptr ||
            weakThis == nullptr || surfaceTextureClass == nullptr) {
            return;
        }

        JNIEnv* env = nullptr;
        bool detachThread = false;
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
            JavaVMAttachArgs args {
                JNI_VERSION_1_6, const_cast<char*>("OHSurfaceTexture"), nullptr
            };
            if (vm->AttachCurrentThreadAsDaemon(&env, &args) != JNI_OK) {
                return;
            }
            detachThread = true;
        }

        static jmethodID postEvent = nullptr;
        if (postEvent == nullptr) {
            postEvent = env->GetStaticMethodID(surfaceTextureClass, "postEventFromNative",
                                               "(Ljava/lang/ref/WeakReference;)V");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                postEvent = nullptr;
            }
        }
        if (postEvent != nullptr) {
            env->CallStaticVoidMethod(surfaceTextureClass, postEvent, weakThis);
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        }
        if (detachThread) {
            vm->DetachCurrentThread();
        }
    }
};

struct SurfaceTextureFields {
    jfieldID surfaceTexture = nullptr;
    jfieldID producer = nullptr;
};

SurfaceTextureFields gFields;

OhSurfaceTextureState* GetState(JNIEnv* env, jobject thiz)
{
    if (thiz == nullptr || gFields.surfaceTexture == nullptr) {
        return nullptr;
    }
    auto* state = reinterpret_cast<OhSurfaceTextureState*>(
        env->GetLongField(thiz, gFields.surfaceTexture));
    return state != nullptr && state->magic == kSurfaceTextureMagic ? state : nullptr;
}

void ThrowException(JNIEnv* env, const char* className, const char* message)
{
    jclass exceptionClass = env->FindClass(className);
    if (exceptionClass != nullptr) {
        env->ThrowNew(exceptionClass, message);
        env->DeleteLocalRef(exceptionClass);
    }
}

void OnFrameAvailable(void* context)
{
    auto* state = static_cast<OhSurfaceTextureState*>(context);
    if (state != nullptr && state->magic == kSurfaceTextureMagic) {
        const uint32_t count = state->frameAvailableCount.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (count <= 8 || (count % 120) == 0) {
            fprintf(stderr,
                    "[WESTLAKE-SURFACETEXTURE] frame-available state=%p image=%p count=%u\n",
                    state, state->image, count);
            fflush(stderr);
        }
        state->PostFrameAvailable();
    }
}

void SurfaceTexture_nativeInit(JNIEnv* env, jobject thiz, jboolean isDetached,
                               jint texName, jboolean singleBufferMode, jobject weakThiz)
{
    auto* state = new OhSurfaceTextureState();
    state->texName = texName;
    state->detached = isDetached == JNI_TRUE;

    // A detached Android SurfaceTexture starts with texture name 0 and is attached later.
    // SurfaceImage accepts that exact state; AttachContext supplies the real texture name.
    state->image = OH_NativeImage_CreateWithSingleBufferMode(
        static_cast<uint32_t>(texName), kTextureExternalOes,
        singleBufferMode == JNI_TRUE);
    if (state->image == nullptr) {
        delete state;
        ThrowException(env, "android/view/Surface$OutOfResourcesException",
                       "OH_NativeImage creation failed");
        return;
    }

    state->nativeWindow = OH_NativeImage_AcquireNativeWindow(state->image);
    if (state->nativeWindow == nullptr) {
        OH_NativeImage_Destroy(&state->image);
        delete state;
        ThrowException(env, "android/view/Surface$OutOfResourcesException",
                       "OH_NativeImage producer window creation failed");
        return;
    }

    // Android BufferQueue starts with a valid minimal size; the real producer updates it.
    const int32_t sizeResult = OH_ConsumerSurface_SetDefaultSize(state->image, 1, 1);
    const int32_t producerSizeResult = OH_NativeWindow_NativeWindowHandleOpt(
        state->nativeWindow, SET_BUFFER_GEOMETRY, 1, 1);
    if (sizeResult != 0 || producerSizeResult != 0) {
        fprintf(stderr,
                "[WESTLAKE-SURFACETEXTURE] initial size failed consumer=%d producer=%d\n",
                sizeResult, producerSizeResult);
    }

    env->GetJavaVM(&state->vm);
    state->weakThis = env->NewGlobalRef(weakThiz);
    jclass localClass = env->GetObjectClass(thiz);
    state->surfaceTextureClass = static_cast<jclass>(env->NewGlobalRef(localClass));
    env->DeleteLocalRef(localClass);

    OH_OnFrameAvailableListener listener { state, OnFrameAvailable };
    const int32_t listenerResult =
        OH_NativeImage_SetOnFrameAvailableListener(state->image, listener);
    if (listenerResult != 0) {
        fprintf(stderr, "[WESTLAKE-SURFACETEXTURE] listener failed ret=%d\n",
                listenerResult);
    }

    env->SetLongField(thiz, gFields.surfaceTexture, reinterpret_cast<jlong>(state));
    env->SetLongField(thiz, gFields.producer,
                      reinterpret_cast<jlong>(state->nativeWindow));
    fprintf(stderr,
            "[WESTLAKE-SURFACETEXTURE] native-image init state=%p image=%p window=%p "
            "detached=%d tex=%d single=%d sizeRet=%d producerSizeRet=%d listenerRet=%d\n",
            state, state->image, state->nativeWindow, isDetached == JNI_TRUE ? 1 : 0,
            static_cast<int>(texName), singleBufferMode == JNI_TRUE ? 1 : 0,
            sizeResult, producerSizeResult, listenerResult);
    fflush(stderr);
}

void SurfaceTexture_nativeFinalize(JNIEnv* env, jobject thiz)
{
    OhSurfaceTextureState* state = GetState(env, thiz);
    if (state == nullptr) {
        return;
    }
    state->released.store(true, std::memory_order_release);
    state->finalized.store(true, std::memory_order_release);
    if (state->image != nullptr) {
        (void)OH_NativeImage_UnsetOnFrameAvailableListener(state->image);
    }
    if (state->weakThis != nullptr) {
        env->DeleteGlobalRef(state->weakThis);
        state->weakThis = nullptr;
    }
    if (state->surfaceTextureClass != nullptr) {
        env->DeleteGlobalRef(state->surfaceTextureClass);
        state->surfaceTextureClass = nullptr;
    }
    env->SetLongField(thiz, gFields.surfaceTexture, 0);
    env->SetLongField(thiz, gFields.producer, 0);

    // This port's android.view.Surface stores a non-owning OHNativeWindow pointer.
    // Keep NativeImage pinned until Surface gains matching native ref/unref ownership;
    // otherwise a Surface that outlives its SurfaceTexture would dangle.
    fprintf(stderr,
            "[WESTLAKE-SURFACETEXTURE] finalize state=%p image=%p window=%p (pinned)\n",
            state, state->image, state->nativeWindow);
    fflush(stderr);
}

void SurfaceTexture_nativeSetDefaultBufferSize(JNIEnv* env, jobject thiz,
                                               jint width, jint height)
{
    OhSurfaceTextureState* state = GetState(env, thiz);
    if (state == nullptr || state->image == nullptr || width <= 0 || height <= 0) {
        return;
    }
    const int32_t consumerResult =
        OH_ConsumerSurface_SetDefaultSize(state->image, width, height);
    // OH keeps the consumer's default dimensions and the producer native
    // window's requested buffer geometry as distinct state. Android's
    // SurfaceTexture contract exposes one default size to both producer and
    // consumer. If only the consumer is updated, eglCreateWindowSurface()
    // succeeds but the producer reports 0x0 and GL rendering fails before the
    // first frame. Mirror the size at the OH boundary so EGL/ANativeWindow and
    // the NativeImage consumer agree.
    const int32_t producerResult = OH_NativeWindow_NativeWindowHandleOpt(
        state->nativeWindow, SET_BUFFER_GEOMETRY,
        static_cast<int32_t>(width), static_cast<int32_t>(height));
    fprintf(stderr,
            "[WESTLAKE-SURFACETEXTURE] set size %dx%d consumer=%d producer=%d\n",
            static_cast<int>(width), static_cast<int>(height),
            consumerResult, producerResult);
    fflush(stderr);
}

void SurfaceTexture_nativeUpdateTexImage(JNIEnv* env, jobject thiz)
{
    OhSurfaceTextureState* state = GetState(env, thiz);
    if (state == nullptr || state->image == nullptr ||
        state->released.load(std::memory_order_acquire)) {
        ThrowException(env, "java/lang/IllegalStateException", "SurfaceTexture is released");
        return;
    }
    const int32_t result = OH_NativeImage_UpdateSurfaceImage(state->image);
    if (result != 0) {
        fprintf(stderr, "[WESTLAKE-SURFACETEXTURE] updateTexImage failed ret=%d\n", result);
        fflush(stderr);
        ThrowException(env, "java/lang/IllegalStateException",
                       "Unable to update texture contents");
    }
}

void SurfaceTexture_nativeReleaseTexImage(JNIEnv* env, jobject thiz)
{
    OhSurfaceTextureState* state = GetState(env, thiz);
    if (state == nullptr || state->image == nullptr ||
        state->released.load(std::memory_order_acquire)) {
        ThrowException(env, "java/lang/IllegalStateException", "SurfaceTexture is released");
        return;
    }
    const int32_t result = OH_NativeImage_ReleaseTextImage(state->image);
    if (result != 0) {
        fprintf(stderr, "[WESTLAKE-SURFACETEXTURE] releaseTexImage failed ret=%d\n", result);
        fflush(stderr);
        ThrowException(env, "java/lang/RuntimeException",
                       "Unable to release texture contents");
    }
}

jint SurfaceTexture_nativeDetachFromGLContext(JNIEnv* env, jobject thiz)
{
    OhSurfaceTextureState* state = GetState(env, thiz);
    if (state == nullptr || state->image == nullptr) {
        return -1;
    }
    const int32_t result = OH_NativeImage_DetachContext(state->image);
    if (result == 0) {
        state->detached = true;
    }
    return result;
}

jint SurfaceTexture_nativeAttachToGLContext(JNIEnv* env, jobject thiz, jint texName)
{
    OhSurfaceTextureState* state = GetState(env, thiz);
    if (state == nullptr || state->image == nullptr) {
        return -1;
    }
    const int32_t result =
        OH_NativeImage_AttachContext(state->image, static_cast<uint32_t>(texName));
    if (result == 0) {
        state->texName = texName;
        state->detached = false;
    }
    return result;
}

void SurfaceTexture_nativeGetTransformMatrix(JNIEnv* env, jobject thiz,
                                             jfloatArray matrix)
{
    OhSurfaceTextureState* state = GetState(env, thiz);
    if (state == nullptr || state->image == nullptr || matrix == nullptr ||
        env->GetArrayLength(matrix) < 16) {
        return;
    }
    jfloat values[16] = {};
    const int32_t result = OH_NativeImage_GetTransformMatrix(state->image, values);
    if (result == 0) {
        env->SetFloatArrayRegion(matrix, 0, 16, values);
    }
}

jlong SurfaceTexture_nativeGetTimestamp(JNIEnv* env, jobject thiz)
{
    OhSurfaceTextureState* state = GetState(env, thiz);
    return state != nullptr && state->image != nullptr
        ? static_cast<jlong>(OH_NativeImage_GetTimestamp(state->image)) : 0;
}

jint SurfaceTexture_nativeGetDataSpace(JNIEnv*, jobject)
{
    // OH_NativeImage exposes color space, not Android's HAL dataspace enum.
    return 0;  // HAL_DATASPACE_UNKNOWN
}

void SurfaceTexture_nativeRelease(JNIEnv* env, jobject thiz)
{
    OhSurfaceTextureState* state = GetState(env, thiz);
    if (state == nullptr || state->image == nullptr ||
        state->released.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    (void)OH_NativeImage_UnsetOnFrameAvailableListener(state->image);
    const int32_t result = OH_NativeImage_Release(state->image);
    fprintf(stderr, "[WESTLAKE-SURFACETEXTURE] release state=%p ret=%d\n", state, result);
    fflush(stderr);
}

jboolean SurfaceTexture_nativeIsReleased(JNIEnv* env, jobject thiz)
{
    OhSurfaceTextureState* state = GetState(env, thiz);
    return state == nullptr || state->released.load(std::memory_order_acquire)
        ? JNI_TRUE : JNI_FALSE;
}

const JNINativeMethod kSurfaceTextureMethods[] = {
    {"nativeInit", "(ZIZLjava/lang/ref/WeakReference;)V",
     reinterpret_cast<void*>(SurfaceTexture_nativeInit)},
    {"nativeFinalize", "()V", reinterpret_cast<void*>(SurfaceTexture_nativeFinalize)},
    {"nativeSetDefaultBufferSize", "(II)V",
     reinterpret_cast<void*>(SurfaceTexture_nativeSetDefaultBufferSize)},
    {"nativeUpdateTexImage", "()V", reinterpret_cast<void*>(SurfaceTexture_nativeUpdateTexImage)},
    {"nativeReleaseTexImage", "()V",
     reinterpret_cast<void*>(SurfaceTexture_nativeReleaseTexImage)},
    {"nativeDetachFromGLContext", "()I",
     reinterpret_cast<void*>(SurfaceTexture_nativeDetachFromGLContext)},
    {"nativeAttachToGLContext", "(I)I",
     reinterpret_cast<void*>(SurfaceTexture_nativeAttachToGLContext)},
    {"nativeGetTransformMatrix", "([F)V",
     reinterpret_cast<void*>(SurfaceTexture_nativeGetTransformMatrix)},
    {"nativeGetTimestamp", "()J", reinterpret_cast<void*>(SurfaceTexture_nativeGetTimestamp)},
    {"nativeGetDataSpace", "()I", reinterpret_cast<void*>(SurfaceTexture_nativeGetDataSpace)},
    {"nativeRelease", "()V", reinterpret_cast<void*>(SurfaceTexture_nativeRelease)},
    {"nativeIsReleased", "()Z", reinterpret_cast<void*>(SurfaceTexture_nativeIsReleased)},
};

}  // namespace

int register_android_graphics_SurfaceTexture_oh(JNIEnv* env)
{
    jclass clazz = env->FindClass("android/graphics/SurfaceTexture");
    if (clazz == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return -1;
    }
    gFields.surfaceTexture = env->GetFieldID(clazz, "mSurfaceTexture", "J");
    gFields.producer = env->GetFieldID(clazz, "mProducer", "J");
    if (env->ExceptionCheck() || gFields.surfaceTexture == nullptr ||
        gFields.producer == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(clazz);
        return -1;
    }
    int registered = 0;
    for (const auto& method : kSurfaceTextureMethods) {
        if (env->RegisterNatives(clazz, &method, 1) == JNI_OK) {
            ++registered;
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }
    env->DeleteLocalRef(clazz);
    fprintf(stderr, "[WESTLAKE-SURFACETEXTURE] registered %d/%zu JNI methods\n",
            registered, sizeof(kSurfaceTextureMethods) / sizeof(kSurfaceTextureMethods[0]));
    fflush(stderr);
    return registered == static_cast<int>(sizeof(kSurfaceTextureMethods) /
                                           sizeof(kSurfaceTextureMethods[0])) ? 0 : -1;
}

}  // namespace android

// android.view.Surface's compat JNI asks the SurfaceTexture boundary for the
// OH producer endpoint. It must never interpret Android private native types.
extern "C" jlong oh_surface_texture_get_native_window(JNIEnv* env, jobject surfaceTexture)
{
    android::OhSurfaceTextureState* state = android::GetState(env, surfaceTexture);
    return state != nullptr && !state->released.load(std::memory_order_acquire)
        ? reinterpret_cast<jlong>(state->nativeWindow) : 0;
}

// TextureLayer's OH consumer adapter keeps the Android SurfaceTexture object and
// DeferredLayerUpdater lifecycle intact.  It only needs an opaque, pinned handle
// to this boundary state so RenderThread can acquire the corresponding
// OH_NativeImage buffer.  OhSurfaceTextureState is deliberately never exposed to
// the HWUI-facing translation unit.
extern "C" void* oh_surface_texture_get_consumer_handle(JNIEnv* env, jobject surfaceTexture)
{
    android::OhSurfaceTextureState* state = android::GetState(env, surfaceTexture);
    return state != nullptr && !state->released.load(std::memory_order_acquire)
        ? static_cast<void*>(state) : nullptr;
}

extern "C" OH_NativeImage* oh_surface_texture_get_native_image(void* consumerHandle)
{
    auto* state = static_cast<android::OhSurfaceTextureState*>(consumerHandle);
    return state != nullptr && state->magic == android::kSurfaceTextureMagic &&
            !state->released.load(std::memory_order_acquire)
        ? state->image : nullptr;
}
