/*
 * Android IncrementalManager JNI at the OpenHarmony filesystem boundary.
 *
 * The source host stages complete APK files on a regular OH filesystem and
 * does not expose Android Incremental FS. Report that capability accurately
 * while preserving Android's null-signature result for ordinary files.
 */
#include "AndroidRuntime.h"

#include <jni.h>

namespace android {
namespace {

jboolean nativeIsEnabled(JNIEnv*, jclass) { return JNI_FALSE; }
jboolean nativeIsV2Available(JNIEnv*, jclass) { return JNI_FALSE; }
jboolean nativeIsIncrementalPath(JNIEnv*, jclass, jstring) { return JNI_FALSE; }
jboolean nativeIsIncrementalFd(JNIEnv*, jclass, jint) { return JNI_FALSE; }
jbyteArray nativeUnsafeGetFileSignature(JNIEnv*, jclass, jstring) { return nullptr; }

const JNINativeMethod kMethods[] = {
    {"nativeIsEnabled", "()Z", reinterpret_cast<void*>(nativeIsEnabled)},
    {"nativeIsV2Available", "()Z", reinterpret_cast<void*>(nativeIsV2Available)},
    {"nativeIsIncrementalPath", "(Ljava/lang/String;)Z",
     reinterpret_cast<void*>(nativeIsIncrementalPath)},
    {"nativeIsIncrementalFd", "(I)Z", reinterpret_cast<void*>(nativeIsIncrementalFd)},
    {"nativeUnsafeGetFileSignature", "(Ljava/lang/String;)[B",
     reinterpret_cast<void*>(nativeUnsafeGetFileSignature)},
};

}  // namespace

int register_android_os_incremental_IncrementalManager(JNIEnv* env) {
    return AndroidRuntime::registerNativeMethods(
            env, "android/os/incremental/IncrementalManager", kMethods,
            sizeof(kMethods) / sizeof(kMethods[0]));
}

}  // namespace android
