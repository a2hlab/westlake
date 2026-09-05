/*
 * android.view.TextureView JNI at the OHOS native-window boundary.
 *
 * TextureView's hardware path is owned by TextureLayer/HWUI.  These methods
 * only keep the producer native window used by the optional software-canvas
 * API.  Preserve Android's Java object and store a referenced OH producer
 * endpoint in mNativeWindow.
 */

#include "external_window.h"

#include <jni.h>

#include <cstdio>

extern "C" jlong oh_surface_texture_get_native_window(JNIEnv* env, jobject surfaceTexture);

namespace android {
namespace {

jfieldID gTextureViewNativeWindow = nullptr;

void TextureView_nDestroyNativeWindow(JNIEnv* env, jobject textureView)
{
    if (gTextureViewNativeWindow == nullptr) {
        return;
    }
    auto* window = reinterpret_cast<OHNativeWindow*>(
        env->GetLongField(textureView, gTextureViewNativeWindow));
    if (window != nullptr) {
        OH_NativeWindow_NativeObjectUnreference(window);
        env->SetLongField(textureView, gTextureViewNativeWindow, 0);
    }
}

void TextureView_nCreateNativeWindow(JNIEnv* env, jobject textureView,
                                     jobject surfaceTexture)
{
    TextureView_nDestroyNativeWindow(env, textureView);
    const jlong handle = oh_surface_texture_get_native_window(env, surfaceTexture);
    auto* window = reinterpret_cast<OHNativeWindow*>(handle);
    if (window != nullptr) {
        OH_NativeWindow_NativeObjectReference(window);
        env->SetLongField(textureView, gTextureViewNativeWindow, handle);
    }
    fprintf(stderr, "[WESTLAKE-TEXTUREVIEW] create surfaceTexture=%p window=%p\n",
            surfaceTexture, window);
    fflush(stderr);
}

jboolean TextureView_nLockCanvas(JNIEnv*, jclass, jlong nativeWindow,
                                 jobject, jobject)
{
    // Software Canvas requires binding an OH buffer to android.graphics.Canvas.
    // Report unavailable truthfully; TextureView's normal HWUI path is independent.
    return nativeWindow != 0 ? JNI_FALSE : JNI_FALSE;
}

void TextureView_nUnlockCanvasAndPost(JNIEnv*, jclass, jlong, jobject)
{
}

const JNINativeMethod kTextureViewMethods[] = {
    {"nCreateNativeWindow", "(Landroid/graphics/SurfaceTexture;)V",
     reinterpret_cast<void*>(TextureView_nCreateNativeWindow)},
    {"nDestroyNativeWindow", "()V",
     reinterpret_cast<void*>(TextureView_nDestroyNativeWindow)},
    {"nLockCanvas", "(JLandroid/graphics/Canvas;Landroid/graphics/Rect;)Z",
     reinterpret_cast<void*>(TextureView_nLockCanvas)},
    {"nUnlockCanvasAndPost", "(JLandroid/graphics/Canvas;)V",
     reinterpret_cast<void*>(TextureView_nUnlockCanvasAndPost)},
};

}  // namespace

int register_android_view_TextureView_oh(JNIEnv* env)
{
    jclass clazz = env->FindClass("android/view/TextureView");
    if (clazz == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return -1;
    }
    gTextureViewNativeWindow = env->GetFieldID(clazz, "mNativeWindow", "J");
    if (env->ExceptionCheck() || gTextureViewNativeWindow == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(clazz);
        return -1;
    }

    const int result = env->RegisterNatives(
        clazz, kTextureViewMethods,
        static_cast<jint>(sizeof(kTextureViewMethods) / sizeof(kTextureViewMethods[0])));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(clazz);
    fprintf(stderr, "[WESTLAKE-TEXTUREVIEW] register rc=%d\n", result);
    fflush(stderr);
    return result;
}

}  // namespace android
