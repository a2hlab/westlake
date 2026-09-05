// Isolated capability probe; built against the matching AOSP-15 class headers.
// Do not load unless the launcher pins the verified original HWUI image.
#include "pipeline/skia/SkiaRecordingCanvas.h"
#include "pipeline/skia/GLFunctorDrawable.h"
#include <jni.h>
#include <cstddef>
#include <cstdio>

namespace android::uirenderer::skiapipeline {
void SkiaRecordingCanvas::drawWebViewFunctor(int functor) {
    static_assert(offsetof(SkiaRecordingCanvas, mRecorder) == 64, "Pinned recorder ABI changed");
    static_assert(offsetof(SkiaRecordingCanvas, mDisplayList) == 3328, "Pinned display-list ABI changed");
    static_assert(offsetof(SkiaDisplayList, mChildFunctors) == 128, "Pinned functor deque ABI changed");
    static_assert(sizeof(GLFunctorDrawable) == 40, "Pinned GL drawable ABI changed");
    FunctorDrawable* drawable = mDisplayList->allocateDrawable<GLFunctorDrawable>(functor, asSkCanvas());
    mDisplayList->mChildFunctors.push_back(drawable);
    mRecorder.drawWebView(drawable);
}
}
static void draw(jlong canvas, jint functor) {
    auto* object = reinterpret_cast<android::uirenderer::skiapipeline::SkiaRecordingCanvas*>(canvas);
    object->android::uirenderer::skiapipeline::SkiaRecordingCanvas::drawWebViewFunctor(functor);
}
extern "C" int westlake_install_webview_functor(JNIEnv* env) {
    jclass canvas = env->FindClass("android/graphics/RecordingCanvas");
    if (!canvas) return 0;
    JNINativeMethod method{const_cast<char*>("nDrawWebViewFunctor"), const_cast<char*>("(JI)V"), reinterpret_cast<void*>(draw)};
    const bool ok = env->RegisterNatives(canvas, &method, 1) == JNI_OK;
    env->DeleteLocalRef(canvas);
    fprintf(stderr, "[OH_HWUI_FUNCTOR] hardware recording installed=%d\n", ok);
    return ok;
}
