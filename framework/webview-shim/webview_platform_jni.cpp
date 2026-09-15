/*
 * Source entry point for AOSP WebView platform support on OpenHarmony.
 *
 * The functional registration tables remain the upstream frameworks/base
 * implementations.  This small boundary-owned entry point avoids depending
 * on Android's fatal logging macros during JNI startup and leaves an explicit
 * device-evidence marker after all three tables have registered.
 */
#include <jni.h>

#include <stdio.h>

namespace android {
void RegisterDrawFunctor(JNIEnv* env);
void RegisterDrawGLFunctor(JNIEnv* env);
void RegisterGraphicsUtils(JNIEnv* env);
}  // namespace android

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    fprintf(stderr, "[SOURCE-WEBVIEW-PLATFORM] JNI_OnLoad enter vm=%p\n",
            static_cast<void*>(vm));
    JNIEnv* env = nullptr;
    if (vm == nullptr || vm->AttachCurrentThread(&env, nullptr) != JNI_OK ||
        env == nullptr) {
        fprintf(stderr, "[SOURCE-WEBVIEW-PLATFORM] AttachCurrentThread failed\n");
        return JNI_ERR;
    }
    android::RegisterDrawFunctor(env);
    android::RegisterDrawGLFunctor(env);
    android::RegisterGraphicsUtils(env);
    if (env->ExceptionCheck()) {
        fprintf(stderr, "[SOURCE-WEBVIEW-PLATFORM] registration exception\n");
        return JNI_ERR;
    }
    fprintf(stderr,
            "[SOURCE-WEBVIEW-PLATFORM] registered draw_fn draw_gl graphics_utils\n");
    fflush(stderr);
    return JNI_VERSION_1_4;
}
