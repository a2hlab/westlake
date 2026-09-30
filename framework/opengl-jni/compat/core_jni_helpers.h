// OH port: RegisterMethodsOrDie from AOSP's core_jni_helpers.h, as android/opengl/util.cpp uses it
// (here it reports failure instead of aborting; opengl_jni_onload.cpp logs the result).
#pragma once
#include <jni.h>
#include <memory>
#include <nativehelper/JNIPlatformHelp.h>  // jniGetNioBufferFields, as the AOSP header provides

namespace android {
static inline int RegisterMethodsOrDie(JNIEnv* env, const char* className,
                                       const JNINativeMethod* methods, int numMethods) {
    jclass clazz = env->FindClass(className);
    if (clazz == nullptr) {
        env->ExceptionClear();
        return -1;
    }
    int rc = env->RegisterNatives(clazz, methods, numMethods);
    env->DeleteLocalRef(clazz);
    return rc;
}
}  // namespace android
