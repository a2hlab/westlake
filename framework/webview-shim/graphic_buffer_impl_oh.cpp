#include "webview_graphic_buffer_oh.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

namespace android {
namespace {

struct OhGraphicBuffer {
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  void* pixels;
};

OhGraphicBuffer* from_id(long buffer_id) {
  return reinterpret_cast<OhGraphicBuffer*>(buffer_id);
}

}  // namespace

long GraphicBufferImpl::Create(int width, int height) {
  if (width <= 0 || height <= 0) return 0;
  OhGraphicBuffer* buffer = static_cast<OhGraphicBuffer*>(calloc(1, sizeof(*buffer)));
  if (buffer == nullptr) return 0;
  buffer->width = static_cast<uint32_t>(width);
  buffer->height = static_cast<uint32_t>(height);
  buffer->stride = static_cast<uint32_t>(width) * 4;
  if (buffer->stride / 4 != static_cast<uint32_t>(width) ||
      buffer->height > SIZE_MAX / buffer->stride) {
    free(buffer);
    return 0;
  }
  buffer->pixels = calloc(buffer->height, buffer->stride);
  if (buffer->pixels == nullptr) {
    free(buffer);
    return 0;
  }
  fprintf(stderr,
          "[WESTLAKE-WEBVIEW-PLAT] CPU GraphicBuffer create %dx%d stride=%u buffer=%p\n",
          width, height, buffer->stride, static_cast<void*>(buffer));
  return reinterpret_cast<long>(buffer);
}

void GraphicBufferImpl::Release(long buffer_id) {
  OhGraphicBuffer* buffer = from_id(buffer_id);
  if (buffer == nullptr) return;
  free(buffer->pixels);
  free(buffer);
}

int GraphicBufferImpl::MapStatic(long buffer_id, AwMapMode mode, void** address) {
  (void)mode;
  OhGraphicBuffer* buffer = from_id(buffer_id);
  if (buffer == nullptr || address == nullptr) return -1;
  *address = buffer->pixels;
  return 0;
}

int GraphicBufferImpl::UnmapStatic(long buffer_id) {
  return from_id(buffer_id) != nullptr ? 0 : -1;
}

void* GraphicBufferImpl::GetNativeBufferStatic(long buffer_id) {
  (void)buffer_id;
  return nullptr;
}

uint32_t GraphicBufferImpl::GetStrideStatic(long buffer_id) {
  OhGraphicBuffer* buffer = from_id(buffer_id);
  return buffer != nullptr ? buffer->stride : 0;
}

}  // namespace android
