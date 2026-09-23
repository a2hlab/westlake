// ============================================================================
// android_view_VelocityTracker.cpp
//
// JNI bindings for android.view.VelocityTracker.
//
// Why this exists at runtime-registration time rather than lazily:
// oh_input_bridge.cpp already carries an identical stub set, but it registers
// them from the touch-injection paths (wl_run_click_from_main_mq,
// dispatchDragViaViewRoot) -- that is, only once the harness injects input.
// Apps reach VelocityTracker.obtain() much earlier than that. McDonald's
// (com.mcdonalds.app) calls it from BottomSheetBehavior.onTouchEvent during
// CoordinatorLayout.onAttachedToWindow, i.e. while the very first view
// hierarchy is being attached and long before any touch exists, and the
// resulting UnsatisfiedLinkError aborts child init outright. Registering here
// puts the methods in place during runtime startup, for every app.
//
// Scope: these return neutral values -- nativeInitialize hands back a non-zero
// opaque handle so VelocityTracker.<init> accepts it, and velocity always
// reads back 0. Dispatch, click detection and scrolling all work; fling
// velocity does not, so momentum-settle gestures degrade to position-only
// settling. Replacing this with the real computation means vendoring
// frameworks-native libs/input/VelocityTracker.cpp (present in imports/) and
// AOSP's core/jni/android_view_VelocityTracker.cpp on top of the existing
// android_input_Input_aosp.cpp -- deliberately deferred, not unreachable.
//
// Signatures verified against imports/frameworks-base VelocityTracker.java
// (the class this boot image actually supplies), lines 200-206.
// ============================================================================

#include "AndroidRuntime.h"

#include <jni.h>

namespace android {

namespace {

jlong VT_nativeInitialize(JNIEnv*, jclass, jint /*strategy*/) {
    // Non-zero: VelocityTracker keeps this as mPtr and NativeAllocationRegistry
    // registers it. Never dereferenced by this implementation.
    return static_cast<jlong>(1);
}

void    VT_nativeDispose(JNIEnv*, jclass, jlong /*ptr*/) {}
void    VT_nativeClear(JNIEnv*, jclass, jlong /*ptr*/) {}
void    VT_nativeAddMovement(JNIEnv*, jclass, jlong /*ptr*/, jobject /*event*/) {}
void    VT_nativeComputeCurrentVelocity(JNIEnv*, jclass, jlong /*ptr*/, jint /*units*/,
                                        jfloat /*maxVelocity*/) {}
jfloat  VT_nativeGetVelocity(JNIEnv*, jclass, jlong /*ptr*/, jint /*axis*/, jint /*id*/) {
    return 0.0f;
}
jboolean VT_nativeIsAxisSupported(JNIEnv*, jclass, jint /*axis*/) { return JNI_FALSE; }

const JNINativeMethod kVelocityTrackerMethods[] = {
    { "nativeInitialize",             "(I)J",  reinterpret_cast<void*>(VT_nativeInitialize) },
    { "nativeDispose",                "(J)V",  reinterpret_cast<void*>(VT_nativeDispose) },
    { "nativeClear",                  "(J)V",  reinterpret_cast<void*>(VT_nativeClear) },
    { "nativeAddMovement",            "(JLandroid/view/MotionEvent;)V",
      reinterpret_cast<void*>(VT_nativeAddMovement) },
    { "nativeComputeCurrentVelocity", "(JIF)V",
      reinterpret_cast<void*>(VT_nativeComputeCurrentVelocity) },
    { "nativeGetVelocity",            "(JII)F", reinterpret_cast<void*>(VT_nativeGetVelocity) },
    { "nativeIsAxisSupported",        "(I)Z",  reinterpret_cast<void*>(VT_nativeIsAxisSupported) },
};

}  // namespace

int register_android_view_VelocityTracker(JNIEnv* env) {
    jclass clazz = env->FindClass("android/view/VelocityTracker");
    if (!clazz) {
        return -1;
    }
    jint rc = env->RegisterNatives(clazz, kVelocityTrackerMethods,
                                   sizeof(kVelocityTrackerMethods)
                                       / sizeof(kVelocityTrackerMethods[0]));
    env->DeleteLocalRef(clazz);
    return rc == JNI_OK ? 0 : -1;
}

}  // namespace android
