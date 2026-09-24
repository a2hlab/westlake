// Compatibility for AOSP's generated GL JNI sources, which only use this one entry point.
// Registration goes through libnativehelper, which the runtime already ships.
#pragma once
#include <jni.h>
#include <nativehelper/JNIHelp.h>
namespace android {
struct AndroidRuntime {
    static int registerNativeMethods(JNIEnv* env, const char* className,
                                     const JNINativeMethod* methods, int count) {
        return jniRegisterNativeMethods(env, className, methods, count);
    }
};
}  // namespace android
