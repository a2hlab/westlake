// Public Android buffers own OH NativeBuffer references; report Android format,
// usage and pixel stride, translating the OH descriptor at this boundary.
#include "oh_buffer_format.h"
#include <android/rect.h>
#include <nativebase/nativebase.h>
#include <sync/sync.h>
#include <cerrno>
#include <climits>
#include <cstring>

extern "C" void* oh_anw_buffer_get_oh(ANativeWindowBuffer*);

extern "C" int AHardwareBuffer_allocate(const AHardwareBuffer_Desc* d, AHardwareBuffer** out) {
    if (!out) return -EINVAL;
    *out = nullptr;
    if (!d || !d->width || !d->height || d->width > INT_MAX || d->height > INT_MAX ||
        d->layers != 1 || d->rfu0 || d->rfu1) return -EINVAL;
    const uint64_t supported = AHARDWAREBUFFER_USAGE_CPU_READ_MASK |
            AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
            AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
    int format = westlakeHostBufferFormat(d->format);
    if (format < 0 || (d->usage & ~supported)) return -ENOTSUP;
    OH_NativeBuffer_Config config{};
    config.width = d->width; config.height = d->height; config.format = format;
    config.usage = westlakeHostBufferUsage(d->usage);
    auto* buffer = OH_NativeBuffer_Alloc(&config);
    if (!buffer) return -ENOMEM;
    *out = reinterpret_cast<AHardwareBuffer*>(buffer);
    return 0;
}
extern "C" void AHardwareBuffer_acquire(AHardwareBuffer* b) {
    if (b) OH_NativeBuffer_Reference(reinterpret_cast<OH_NativeBuffer*>(b));
}
extern "C" void AHardwareBuffer_release(AHardwareBuffer* b) {
    if (b) OH_NativeBuffer_Unreference(reinterpret_cast<OH_NativeBuffer*>(b));
}
extern "C" void AHardwareBuffer_describe(const AHardwareBuffer* b, AHardwareBuffer_Desc* d) {
    if (!b || !d) return;
    OH_NativeBuffer_Config config{};
    OH_NativeBuffer_GetConfig(reinterpret_cast<OH_NativeBuffer*>(const_cast<AHardwareBuffer*>(b)), &config);
    *d = {};
    d->width = config.width; d->height = config.height; d->layers = 1;
    d->format = westlakeAndroidBufferFormat(config.format);
    d->usage = westlakeAndroidBufferUsage(static_cast<uint32_t>(config.usage));
    int pixelBytes = westlakeBufferPixelBytes(d->format);
    d->stride = pixelBytes ? config.stride / pixelBytes : 0;
}
extern "C" int AHardwareBuffer_lock(AHardwareBuffer* b, uint64_t usage, int32_t fence,
                                      const ARect* rect, void** out) {
    if (!b || !out || !usage || (usage & ~(AHARDWAREBUFFER_USAGE_CPU_READ_MASK |
                                         AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK))) return -EINVAL;
    *out = nullptr;
    AHardwareBuffer_Desc d{}; AHardwareBuffer_describe(b, &d);
    if (rect && (rect->left < 0 || rect->top < 0 || rect->right < rect->left ||
                 rect->bottom < rect->top || rect->right > d.width || rect->bottom > d.height))
        return -EINVAL;
    if (fence >= 0 && sync_wait(fence, -1)) return -errno;
    return OH_NativeBuffer_Map(reinterpret_cast<OH_NativeBuffer*>(b), out) ? -EIO : 0;
}
extern "C" int AHardwareBuffer_unlock(AHardwareBuffer* b, int32_t* fence) {
    if (fence) *fence = -1; // OH unmap completes the CPU access synchronously.
    if (!b) return -EINVAL;
    return OH_NativeBuffer_Unmap(reinterpret_cast<OH_NativeBuffer*>(b)) ? -EIO : 0;
}
extern "C" AHardwareBuffer* ANativeWindowBuffer_getHardwareBuffer(ANativeWindowBuffer* b) {
    auto* windowBuffer = static_cast<OHNativeWindowBuffer*>(oh_anw_buffer_get_oh(b));
    OH_NativeBuffer* buffer = nullptr;
    if (!windowBuffer || OH_NativeBuffer_FromNativeWindowBuffer(windowBuffer, &buffer)) return nullptr;
    return reinterpret_cast<AHardwareBuffer*>(buffer); // borrowed; window owns it
}
extern "C" int westlake_window_buffer_describe(void* windowBuffer, ANativeWindowBuffer* out) {
    OH_NativeBuffer* buffer = nullptr;
    if (OH_NativeBuffer_FromNativeWindowBuffer(static_cast<OHNativeWindowBuffer*>(windowBuffer), &buffer))
        return -EINVAL;
    AHardwareBuffer_Desc d{}; AHardwareBuffer_describe(reinterpret_cast<AHardwareBuffer*>(buffer), &d);
    if (!d.format) return -ENOTSUP;
    out->width = d.width; out->height = d.height; out->stride = d.stride;
    out->format = d.format; out->usage = d.usage; out->usage_deprecated = d.usage;
    out->layerCount = d.layers;
    // OH EGL receives the OH handle through oh_anw_get_oh. An OH BufferHandle
    // is not an Android native_handle_t and must never be cast into this field.
    out->handle = nullptr;
    return 0;
}
extern "C" int westlake_android_buffer_format(int format) { return westlakeAndroidBufferFormat(format); }
extern "C" int westlake_host_buffer_format(int format) { return westlakeHostBufferFormat(format); }
extern "C" uint64_t westlake_host_buffer_usage(uint64_t usage) { return westlakeHostBufferUsage(usage); }
