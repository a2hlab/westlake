// ============================================================================
// android_view_VelocityTracker.cpp
//
// The JNI half of android.view.VelocityTracker, built into
// liboh_android_runtime.so.
//
// Why it exists
// -------------
// The adapter ships android.view.VelocityTracker in framework.jar but no
// implementation for its seven native methods, so the very first touch that
// reaches a scrolling container dies:
//
//   java.lang.UnsatisfiedLinkError: No implementation found for
//       long android.view.VelocityTracker.nativeInitialize(int)
//     at android.view.VelocityTracker.obtain(VelocityTracker.java:230)
//     at android.widget.HorizontalScrollView.initOrResetVelocityTracker(:540)
//     at android.widget.HorizontalScrollView.onInterceptTouchEvent(:641)
//     at android.view.ViewGroup.dispatchTouchEvent(ViewGroup.java:2654)
//
// ViewRootImpl's input stages swallow that error, which is why touch looked
// like it was being delivered and then silently doing nothing. Every scrollable
// container is affected -- on the reproduction target the channel row is a
// HorizontalScrollView and the feed is a RecyclerView, and both call
// VelocityTracker.obtain().
//
// Why it lives here rather than in a side-loaded .so
// --------------------------------------------------
// The frontline shipped this as libwlveltrack.so, pushed next to the app's own
// libraries and pulled in from ActivityManagerRouting via Runtime.nativeLoad()
// so that its JNI_OnLoad could RegisterNatives. That detour existed only
// because VelocityTracker is a boot-classpath class: ART resolves a BCP class's
// natives against libraries registered under the *boot* loader, and this
// adapter's libnativeloader carries a hardcoded three-entry allowlist
// (libopenjdk.so / libicu_jni.so / libjavacore.so) -- anything else is refused
// with "system library is absent from the adapter manifest".
//
// Registering from inside liboh_android_runtime removes the problem instead of
// working around it. startReg() runs with the runtime's own JNIEnv before any
// app code, and explicit RegisterNatives overrides lookup entirely, so no app
// side deployment, no nativeLoad(), and no per-install path list is involved.
//
// Scope
// -----
// Bookkeeping only. It hands out a real per-tracker handle so that
// obtain/recycle/finalize pair up correctly, and reports zero velocity. That is
// enough for taps, drags and scrolls, which follow the touch position; only
// *flings* need a real velocity, and they simply come out as "no fling".
// Computing a true velocity means reading the MotionEvent back through JNI on
// the UI thread inside touch dispatch, which is a far larger blast radius than
// anything here needs yet. When that becomes necessary, this file is the place:
// nativeAddMovement already receives the MotionEvent.
//
// Ported from toutiao-repro/native/veltrack/wl_veltrack.c, validated on
// DAYU200 / OpenHarmony 6.1.0.31.
// ============================================================================

#include <jni.h>

#include <cstdint>
#include <cstdlib>

namespace {

// MotionEvent.AXIS_X / MotionEvent.AXIS_Y.
constexpr jint kAxisX = 0;
constexpr jint kAxisY = 1;

// Stamped into every handle so a stale or forged jlong is rejected rather than
// dereferenced. VelocityTracker.recycle() can be called more than once by
// careless app code, and a double free here would take down the UI thread.
constexpr uint32_t kTrackerMagic = 0x574C5654u;  // 'WLVT'

struct Tracker {
    uint32_t magic;
    jint strategy;
};

Tracker* fromHandle(jlong handle) {
    Tracker* tracker = reinterpret_cast<Tracker*>(static_cast<intptr_t>(handle));
    if (tracker == nullptr || tracker->magic != kTrackerMagic) {
        return nullptr;
    }
    return tracker;
}

jlong nativeInitialize(JNIEnv* /*env*/, jclass /*clazz*/, jint strategy) {
    Tracker* tracker = static_cast<Tracker*>(std::malloc(sizeof(Tracker)));
    if (tracker == nullptr) {
        // Java checks the handle only on use, not on obtain(); a zero handle
        // turns every later call into a no-op, which is the safe degradation.
        return 0;
    }
    tracker->magic = kTrackerMagic;
    tracker->strategy = strategy;
    return static_cast<jlong>(reinterpret_cast<intptr_t>(tracker));
}

void nativeDispose(JNIEnv* /*env*/, jclass /*clazz*/, jlong handle) {
    Tracker* tracker = fromHandle(handle);
    if (tracker == nullptr) {
        return;
    }
    tracker->magic = 0;
    std::free(tracker);
}

void nativeClear(JNIEnv* /*env*/, jclass /*clazz*/, jlong /*handle*/) {
    // No samples are retained, so there is nothing to clear.
}

void nativeAddMovement(JNIEnv* /*env*/, jclass /*clazz*/, jlong /*handle*/,
                       jobject /*event*/) {
    // Samples are deliberately not retained; see the scope note above.
}

void nativeComputeCurrentVelocity(JNIEnv* /*env*/, jclass /*clazz*/, jlong /*handle*/,
                                  jint /*units*/, jfloat /*maxVelocity*/) {
    // Nothing to compute while every velocity is reported as zero.
}

jfloat nativeGetVelocity(JNIEnv* /*env*/, jclass /*clazz*/, jlong /*handle*/,
                         jint /*axis*/, jint /*pointerId*/) {
    return 0.0f;
}

jboolean nativeIsAxisSupported(JNIEnv* /*env*/, jclass /*clazz*/, jint axis) {
    return (axis == kAxisX || axis == kAxisY) ? JNI_TRUE : JNI_FALSE;
}

const JNINativeMethod kMethods[] = {
    { "nativeInitialize",             "(I)J",
      reinterpret_cast<void*>(nativeInitialize) },
    { "nativeDispose",                "(J)V",
      reinterpret_cast<void*>(nativeDispose) },
    { "nativeClear",                  "(J)V",
      reinterpret_cast<void*>(nativeClear) },
    { "nativeAddMovement",            "(JLandroid/view/MotionEvent;)V",
      reinterpret_cast<void*>(nativeAddMovement) },
    { "nativeComputeCurrentVelocity", "(JIF)V",
      reinterpret_cast<void*>(nativeComputeCurrentVelocity) },
    { "nativeGetVelocity",            "(JII)F",
      reinterpret_cast<void*>(nativeGetVelocity) },
    { "nativeIsAxisSupported",        "(I)Z",
      reinterpret_cast<void*>(nativeIsAxisSupported) },
};

}  // namespace

int register_android_view_VelocityTracker(JNIEnv* env) {
    jclass clazz = env->FindClass("android/view/VelocityTracker");
    if (clazz == nullptr) {
        env->ExceptionClear();
        return -1;
    }
    jint result = env->RegisterNatives(clazz, kMethods,
                                       static_cast<jint>(sizeof(kMethods) / sizeof(kMethods[0])));
    if (result != JNI_OK) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(clazz);
    return result == JNI_OK ? 0 : -1;
}
