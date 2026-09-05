/* Copyright 2017 Google Inc.
 * BSD-style Skia LICENSE at third_party/skia/m133/LICENSE.
 * The two unmodified Android-framework helpers omitted from the OH Skia DSO.
 */
#include "include/android/SkAndroidFrameworkUtils.h"
#include "include/core/SkCanvas.h"
#include "include/utils/SkPaintFilterCanvas.h"
#include "src/core/SkDevice.h"

bool SkAndroidFrameworkUtils::clipWithStencil(SkCanvas* canvas) {
    return canvas->rootDevice()->android_utils_clipWithStencil();
}
SkCanvas* SkAndroidFrameworkUtils::getBaseWrappedCanvas(SkCanvas* canvas) {
    auto pfc = canvas->internal_private_asPaintFilterCanvas();
    auto result = canvas;
    while (pfc) {
        result = pfc->proxy();
        pfc = result->internal_private_asPaintFilterCanvas();
    }
    return result;
}
