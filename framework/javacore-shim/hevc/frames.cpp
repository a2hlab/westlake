#include "frames.h"

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <unistd.h>
#include <native_buffer/native_buffer.h>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace {
bool copyPlane(const AVFrame* frame, int plane, uint8_t* base, size_t capacity,
               uint64_t offset, uint32_t rowStride, uint32_t pixelStride)
{
    const int width = plane ? (frame->width + 1) / 2 : frame->width;
    const int height = plane ? (frame->height + 1) / 2 : frame->height;
    if (width <= 0 || height <= 0 || !rowStride || !pixelStride || !base || !frame->data[plane]) return false;
    uint64_t span = uint64_t(width - 1) * pixelStride + 1;
    if (offset >= capacity || span > rowStride || uint64_t(height - 1) * rowStride + span > capacity - offset)
        return false;
    for (int y = 0; y < height; ++y) {
        auto* dst = base + offset + uint64_t(y) * rowStride;
        const auto* src = frame->data[plane] + ptrdiff_t(y) * frame->linesize[plane];
        if (pixelStride == 1) std::memcpy(dst, src, width);
        else for (int x = 0; x < width; ++x) dst[size_t(x) * pixelStride] = src[x];
    }
    return true;
}

bool copyWindowBuffer(const AVFrame* frame, OHNativeWindowBuffer* windowBuffer)
{
    OH_NativeBuffer* buffer = nullptr;
    const BufferHandle* handle = OH_NativeWindow_GetBufferHandleFromNative(windowBuffer);
    if (!handle || handle->size <= 0 || handle->format != NATIVEBUFFER_PIXEL_FMT_YCBCR_420_SP ||
        OH_NativeBuffer_FromNativeWindowBuffer(windowBuffer, &buffer) != 0) return false;
    OH_NativeBuffer_Planes layout{};
    void* address = nullptr;
    int rc = OH_NativeBuffer_MapPlanes(buffer, &address, &layout);
    if (rc != 0 && OH_NativeBuffer_Map(buffer, &address) != 0) return false;
    auto* bytes = static_cast<uint8_t*>(address);
    bool ok = true;
    if (rc == 0 && layout.planeCount >= 2) {
        for (int i = 0; i < 3 && ok; ++i) {
            const bool sharedChroma = i && layout.planeCount == 2;
            const auto& plane = layout.planes[sharedChroma ? 1 : i];
            ok = copyPlane(frame, i, bytes, handle->size, plane.offset + (sharedChroma && i == 2),
                           plane.rowStride, sharedChroma ? 2 : plane.columnStride);
        }
    } else {
        ok = wl_hevc_copy_nv12(frame, bytes, handle->size, handle->stride);
    }
    return OH_NativeBuffer_Unmap(buffer) == 0 && ok;
}
}

bool wl_hevc_copy_nv12(const AVFrame* frame, uint8_t* data, size_t capacity, int stride)
{
    if (stride <= 0 || frame->format != AV_PIX_FMT_YUV420P) return false;
    const uint64_t chroma = uint64_t(stride) * frame->height;
    return copyPlane(frame, 0, data, capacity, 0, stride, 1) &&
           copyPlane(frame, 1, data, capacity, chroma, stride, 2) &&
           copyPlane(frame, 2, data, capacity, chroma + 1, stride, 2);
}

int wl_hevc_render_frame(OHNativeWindow* window, const AVFrame* frame, int64_t presentationNs)
{
    if (!window || frame->format != AV_PIX_FMT_YUV420P) return -1;
    uint64_t usage = 0;
    if (OH_NativeWindow_NativeWindowHandleOpt(window, GET_USAGE, &usage) ||
        OH_NativeWindow_NativeWindowHandleOpt(window, SET_USAGE, usage | NATIVEBUFFER_USAGE_CPU_WRITE) ||
        OH_NativeWindow_NativeWindowHandleOpt(window, SET_FORMAT, NATIVEBUFFER_PIXEL_FMT_YCBCR_420_SP) ||
        OH_NativeWindow_NativeWindowHandleOpt(window, SET_BUFFER_GEOMETRY, frame->width, frame->height)) return -1;
    if (presentationNs >= 0) {
        if (OH_NativeWindow_NativeWindowHandleOpt(window, SET_UI_TIMESTAMP, presentationNs) ||
            OH_NativeWindow_NativeWindowHandleOpt(window, SET_DESIRED_PRESENT_TIMESTAMP, presentationNs)) return -1;
    }
    OHNativeWindowBuffer* buffer = nullptr;
    int fence = -1;
    if (OH_NativeWindow_NativeWindowRequestBuffer(window, &buffer, &fence)) return -1;
    bool ready = true;
    if (fence >= 0) {
        pollfd wait{fence, POLLIN, 0};
        int events;
        do { events = poll(&wait, 1, 1000); } while (events < 0 && errno == EINTR);
        ready = events > 0 && (wait.revents & POLLIN);
        close(fence);
    }
    if (ready && copyWindowBuffer(frame, buffer) &&
        OH_NativeWindow_NativeWindowFlushBuffer(window, buffer, -1, Region{}) == 0) return 0;
    OH_NativeWindow_NativeWindowAbortBuffer(window, buffer);
    return -1;
}
