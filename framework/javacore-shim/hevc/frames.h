#pragma once

#include <cstddef>
#include <native_window/external_window.h>

struct AVFrame;

// Copy an 8-bit YUV420P picture into a two-plane NV12 buffer.
bool wl_hevc_copy_nv12(const AVFrame* frame, uint8_t* data, size_t capacity, int stride);
int wl_hevc_render_frame(OHNativeWindow* window, const AVFrame* frame, int64_t presentationNs);
