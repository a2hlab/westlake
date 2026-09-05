/* Minimal stand-in for AOSP frameworks/base/core/jni/android_runtime/AndroidRuntime.h.
 *
 * SQLiteConnection.cpp calls AndroidRuntime::getJNIEnv() from four sqlite
 * callbacks -- the trace hook, the profile hook, the progress handler and the
 * custom-function trampoline.  Those run on whichever thread is driving sqlite,
 * so the JNIEnv cannot be captured once and reused; it has to be fetched per
 * call.  The implementation in wl_compat.cpp does that from the JavaVM cached at
 * JNI_OnLoad, and returns null on a thread that is not attached, which is what
 * every caller already null-checks for.
 */
#ifndef WL_ANDROID_RUNTIME_H
#define WL_ANDROID_RUNTIME_H

#include <jni.h>

namespace android {

class AndroidRuntime {
public:
    /** JNIEnv for the calling thread, or null if it is not attached. */
    static JNIEnv* getJNIEnv();

    /** The process JavaVM, cached by JNI_OnLoad. */
    static JavaVM* getJavaVM();

    /** Called once from JNI_OnLoad. */
    static void setJavaVM(JavaVM* vm);
};

}  // namespace android

#endif  // WL_ANDROID_RUNTIME_H
