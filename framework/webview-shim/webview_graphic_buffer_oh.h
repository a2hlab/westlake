/*
 * Minimal GraphicBuffer contract used by the WebView platform adapter.
 *
 * The upstream declaration embeds android::GraphicBuffer and therefore pulls
 * generated HIDL graphics headers into every consumer.  The OpenHarmony
 * boundary implementation is deliberately CPU-backed and only implements the
 * six callbacks exposed through AwDrawGLFunctionTable, so keep that private
 * Android implementation detail out of this source build.
 */
#ifndef ANDROID_GRAPHIC_BUFFER_IMPL_H
#define ANDROID_GRAPHIC_BUFFER_IMPL_H

#include <stdint.h>

#include "draw_gl.h"

namespace android {

class GraphicBufferImpl {
 public:
  static long Create(int width, int height);
  static void Release(long buffer_id);
  static int MapStatic(long buffer_id, AwMapMode mode, void** address);
  static int UnmapStatic(long buffer_id);
  static void* GetNativeBufferStatic(long buffer_id);
  static uint32_t GetStrideStatic(long buffer_id);
};

}  // namespace android

#endif  // ANDROID_GRAPHIC_BUFFER_IMPL_H
