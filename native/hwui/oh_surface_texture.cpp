// Android's SurfaceTexture facade backed by the OH consumer queue. No HWUI
// instruction patching or private Android SurfaceTexture layout is involved.
#include "oh61_buffer_handle.h"
#include <android/hardware_buffer.h>
#include <android/data_space.h>
#include <android/surface_texture.h>
#include <android/surface_texture_jni.h>
#include <surfacetexture/surface_texture_platform.h>
#include <native_image/native_image.h>
#include <native_buffer/native_buffer.h>
#include <native_window/external_window.h>
#include <surface_buffer.h>
#include <sync/sync.h>
#include <GLES2/gl2.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

extern "C" void* oh_surface_texture_get_consumer_handle(JNIEnv*, jobject);
extern "C" OH_NativeImage* oh_surface_texture_get_native_image(void*);

struct ASurfaceTexture {
    JavaVM* vm = nullptr;
    jobject javaTexture = nullptr; // pins the JNI state until this facade is released
    void* consumer = nullptr;
    OH_NativeImage* image = nullptr;
    OHNativeWindowBuffer* current = nullptr;
    std::mutex mutex;
    bool owned = false;
    int nextSlot = 0;
    std::unordered_map<uint32_t, int> slots;
    android::ASurfaceTexture_createReleaseFence createFence = nullptr;
    void* fenceHandle = nullptr;
};

namespace {
bool releaseCurrent(ASurfaceTexture* st) {
    if (!st->current) return true;
    int fd = -1;
    EGLSyncKHR sync = EGL_NO_SYNC_KHR;
    EGLDisplay display = EGL_NO_DISPLAY;
    if (st->createFence && st->createFence(true, &sync, &display, &fd, st->fenceHandle)) {
        if (fd >= 0) close(fd);
        return false; // keep ownership until a release fence can be established
    }
    if (sync != EGL_NO_SYNC_KHR) {
        EGLint result = eglClientWaitSyncKHR(display, sync, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,
                                             EGL_FOREVER_KHR);
        eglDestroySyncKHR(display, sync);
        if (result != EGL_CONDITION_SATISFIED_KHR) {
            if (fd >= 0) close(fd);
            return false;
        }
    } else if (fd < 0 && eglGetCurrentContext() != EGL_NO_CONTEXT) {
        // Driver has neither fence extension. Complete the GL reads before reuse.
        glFinish();
    }
    int rc = OH_NativeImage_ReleaseNativeWindowBuffer(st->image, st->current, fd);
    if (rc) {
        fprintf(stderr, "[WESTLAKE-SURFACETEXTURE] release buffer failed: %d\n", rc);
        return false;
    }
    st->current = nullptr;
    return true;
}

android_dataspace dataSpace(OH_NativeBuffer* buffer) {
    OH_NativeBuffer_ColorSpace color = OH_COLORSPACE_NONE;
    if (OH_NativeBuffer_GetColorSpace(buffer, &color)) return HAL_DATASPACE_UNKNOWN;
    int standard, transfer, range = HAL_DATASPACE_RANGE_FULL;
    switch (color) {
        case OH_COLORSPACE_SRGB_LIMIT: range = HAL_DATASPACE_RANGE_LIMITED; [[fallthrough]];
        case OH_COLORSPACE_SRGB_FULL: case OH_COLORSPACE_DISPLAY_SRGB:
            standard = HAL_DATASPACE_STANDARD_BT709; transfer = HAL_DATASPACE_TRANSFER_SRGB; break;
        case OH_COLORSPACE_P3_LIMIT: range = HAL_DATASPACE_RANGE_LIMITED; [[fallthrough]];
        case OH_COLORSPACE_P3_FULL: case OH_COLORSPACE_DISPLAY_P3_SRGB:
            standard = HAL_DATASPACE_STANDARD_DCI_P3; transfer = HAL_DATASPACE_TRANSFER_SRGB; break;
        case OH_COLORSPACE_BT709_LIMIT: range = HAL_DATASPACE_RANGE_LIMITED; [[fallthrough]];
        case OH_COLORSPACE_BT709_FULL:
            standard = HAL_DATASPACE_STANDARD_BT709; transfer = HAL_DATASPACE_TRANSFER_SMPTE_170M; break;
        case OH_COLORSPACE_BT2020_HLG_LIMIT: range = HAL_DATASPACE_RANGE_LIMITED; [[fallthrough]];
        case OH_COLORSPACE_BT2020_HLG_FULL: case OH_COLORSPACE_DISPLAY_BT2020_HLG:
            standard = HAL_DATASPACE_STANDARD_BT2020; transfer = HAL_DATASPACE_TRANSFER_HLG; break;
        case OH_COLORSPACE_BT2020_PQ_LIMIT: range = HAL_DATASPACE_RANGE_LIMITED; [[fallthrough]];
        case OH_COLORSPACE_BT2020_PQ_FULL: case OH_COLORSPACE_DISPLAY_BT2020_PQ:
            standard = HAL_DATASPACE_STANDARD_BT2020; transfer = HAL_DATASPACE_TRANSFER_ST2084; break;
        case OH_COLORSPACE_LINEAR_SRGB:
            standard = HAL_DATASPACE_STANDARD_BT709; transfer = HAL_DATASPACE_TRANSFER_LINEAR; break;
        case OH_COLORSPACE_LINEAR_P3:
            standard = HAL_DATASPACE_STANDARD_DCI_P3; transfer = HAL_DATASPACE_TRANSFER_LINEAR; break;
        case OH_COLORSPACE_LINEAR_BT2020:
            standard = HAL_DATASPACE_STANDARD_BT2020; transfer = HAL_DATASPACE_TRANSFER_LINEAR; break;
        default: return HAL_DATASPACE_UNKNOWN;
    }
    return static_cast<android_dataspace>(standard | transfer | range);
}

void metadata(OH_NativeBuffer* buffer, AHdrMetadataType* type,
              android_cta861_3_metadata* cta, android_smpte2086_metadata* smpte) {
    *type = static_cast<AHdrMetadataType>(0);
    *cta = {}; *smpte = {};
    int32_t size = 0;
    uint8_t* bytes = nullptr;
    int rc = OH_NativeBuffer_GetMetadataValue(buffer, OH_HDR_STATIC_METADATA, &size, &bytes);
    // OH native_buffer.cpp returns a new uint8_t[] allocation for metadata.
    std::unique_ptr<uint8_t[]> allocation(bytes);
    if (rc || !bytes || size != sizeof(OH_NativeBuffer_StaticMetadata)) return;
    OH_NativeBuffer_StaticMetadata host;
    memcpy(&host, bytes, sizeof(host));
    cta->maxContentLightLevel = host.cta861.maxContentLightLevel;
    cta->maxFrameAverageLightLevel = host.cta861.maxFrameAverageLightLevel;
    smpte->displayPrimaryRed = {host.smpte2086.displayPrimaryRed.x, host.smpte2086.displayPrimaryRed.y};
    smpte->displayPrimaryGreen = {host.smpte2086.displayPrimaryGreen.x, host.smpte2086.displayPrimaryGreen.y};
    smpte->displayPrimaryBlue = {host.smpte2086.displayPrimaryBlue.x, host.smpte2086.displayPrimaryBlue.y};
    smpte->whitePoint = {host.smpte2086.whitePoint.x, host.smpte2086.whitePoint.y};
    smpte->maxLuminance = host.smpte2086.maxLuminance;
    smpte->minLuminance = host.smpte2086.minLuminance;
    *type = static_cast<AHdrMetadataType>(HDR10_CTA861_3 | HDR10_SMPTE2086);
}
} // namespace

extern "C" ASurfaceTexture* ASurfaceTexture_fromSurfaceTexture(JNIEnv* env, jobject texture) {
    void* consumer = oh_surface_texture_get_consumer_handle(env, texture);
    OH_NativeImage* image = oh_surface_texture_get_native_image(consumer);
    if (!image) return nullptr;
    auto st = std::make_unique<ASurfaceTexture>();
    env->GetJavaVM(&st->vm);
    st->javaTexture = env->NewGlobalRef(texture);
    if (!st->javaTexture) return nullptr;
    st->consumer = consumer; st->image = image;
    return st.release();
}

extern "C" void ASurfaceTexture_release(ASurfaceTexture* st) {
    if (!st) return;
    android::ASurfaceTexture_releaseConsumerOwnership(st);
    // If the driver could not release a buffer safely, retain the owner. Never
    // make a buffer writable while HWUI may still be reading it.
    if (st->current) return;
    JNIEnv* env = nullptr;
    bool detach = st->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK;
    if (detach && st->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    env->DeleteGlobalRef(st->javaTexture);
    if (detach) st->vm->DetachCurrentThread();
    delete st;
}

namespace android {
unsigned int ASurfaceTexture_getCurrentTextureTarget(ASurfaceTexture*) { return 0x8d65; }

void ASurfaceTexture_takeConsumerOwnership(ASurfaceTexture* st) {
    std::lock_guard<std::mutex> lock(st->mutex);
    // The OH NativeImage consumer API does not bind an external GL texture;
    // acquire/release operate on its buffer queue on this render thread.
    st->owned = oh_surface_texture_get_native_image(st->consumer) != nullptr;
}

void ASurfaceTexture_releaseConsumerOwnership(ASurfaceTexture* st) {
    std::lock_guard<std::mutex> lock(st->mutex);
    if (releaseCurrent(st)) {
        st->owned = false; st->createFence = nullptr; st->fenceHandle = nullptr;
    }
}

AHardwareBuffer* ASurfaceTexture_dequeueBuffer(
        ASurfaceTexture* st, int* slot, android_dataspace* dataspace,
        AHdrMetadataType* hdrType, android_cta861_3_metadata* cta,
        android_smpte2086_metadata* smpte, float* matrix, uint32_t* transform,
        bool* newContent, ASurfaceTexture_createReleaseFence createFence,
        ASurfaceTexture_fenceWait fenceWait, void* handle, ARect* crop) {
    std::lock_guard<std::mutex> lock(st->mutex);
    *newContent = false;
    if (!st->owned || !oh_surface_texture_get_native_image(st->consumer)) return nullptr;
    st->createFence = createFence; st->fenceHandle = handle;
    OHNativeWindowBuffer* next = nullptr;
    int fence = -1;
    int rc = OH_NativeImage_AcquireLatestNativeWindowBuffer(st->image, &next, &fence);
    if (rc == 0) {
        // Wait without transferring the input descriptor to the HWUI callback.
        int wait = fence < 0 ? 0 : (fenceWait ? fenceWait(fence, handle) : sync_wait(fence, -1));
        if (wait || !releaseCurrent(st)) {
            OH_NativeImage_ReleaseNativeWindowBuffer(st->image, next, fence);
            return nullptr;
        }
        if (fence >= 0) close(fence);
        st->current = next;
        *newContent = true;
    } else if (rc != 40601000) { // documented NATIVE_ERROR_NO_BUFFER
        fprintf(stderr, "[WESTLAKE-SURFACETEXTURE] acquire latest failed: %d\n", rc);
        return nullptr;
    }
    if (!st->current) return nullptr;
    OH_NativeBuffer* buffer = nullptr;
    if (OH_NativeBuffer_FromNativeWindowBuffer(st->current, &buffer) || !buffer) return nullptr;
    auto* surface = OHOS::SurfaceBuffer::NativeBufferToSurfaceBuffer(buffer);
    auto [it, added] = st->slots.try_emplace(surface->GetSeqNum(), st->nextSlot);
    if (added) ++st->nextSlot;
    *slot = it->second;
    *dataspace = dataSpace(buffer);
    metadata(buffer, hdrType, cta, smpte);
    static constexpr uint32_t transforms[] = {0, 4, 3, 7, 1, 2, 5, 6};
    auto hostTransform = static_cast<unsigned>(surface->GetSurfaceBufferTransform());
    if (hostTransform >= std::size(transforms)) return nullptr;
    *transform = transforms[hostTransform];
    OHOS::Rect rect;
    if (surface->GetCropMetadata(rect) && rect.w > 0 && rect.h > 0)
        *crop = {rect.x, rect.y, rect.x + rect.w, rect.y + rect.h};
    else
        *crop = {0, 0, surface->GetWidth(), surface->GetHeight()};
    // HWUI uses crop and transform separately; preserve the host matrix too.
    if (OH_NativeImage_GetBufferMatrix(st->image, matrix)) return nullptr;
    if (OH_NativeBuffer_Reference(buffer)) return nullptr;
    return reinterpret_cast<AHardwareBuffer*>(buffer);
}
} // namespace android

extern "C" ADataSpace AHardwareBuffer_getDataSpace(const AHardwareBuffer* buffer) {
    if (!buffer) return ADATASPACE_UNKNOWN;
    return static_cast<ADataSpace>(dataSpace(
            reinterpret_cast<OH_NativeBuffer*>(const_cast<AHardwareBuffer*>(buffer))));
}
