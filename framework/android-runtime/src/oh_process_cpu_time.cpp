// Android process CPU time at the OH libc boundary. Same clock and millisecond
// truncation as AOSP android_util_Process.cpp; this excludes sleep/wall time.
#include <jni.h>
#include <time.h>
#ifdef OH_PROCESS_CPU_OVERLAY
#include <dlfcn.h>
#include <cstdio>
#endif
namespace android {
namespace {
jlong elapsedCpuTime(JNIEnv*, jclass) {
    timespec ts{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) return 0;
    return static_cast<jlong>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}
}
int register_android_os_ProcessCpuTime(JNIEnv* env) {
    jclass type = env->FindClass("android/os/Process");
    if (!type) return -1;
    const JNINativeMethod method = {
        "getElapsedCpuTime", "()J", reinterpret_cast<void*>(elapsedCpuTime)};
    const int rc = env->RegisterNatives(type, &method, 1);
    env->DeleteLocalRef(type);
    return rc == JNI_OK ? 0 : -1;
}
#ifdef OH_PROCESS_CPU_OVERLAY
// Opt-in overlay preserves the pinned runtime's existing registrations.
int register_android_os_Process(JNIEnv* env) {
    using Register = int (*)(JNIEnv*);
    auto original = reinterpret_cast<Register>(dlsym(RTLD_NEXT,
        "_ZN7android27register_android_os_ProcessEP7_JNIEnv"));
    if (!original || original(env) != 0) return -1;
    int rc = register_android_os_ProcessCpuTime(env);
    std::fprintf(stderr, "[OH_PROCESS] getElapsedCpuTime registration rc=%d\n", rc);
    return rc;
}
#endif
}
