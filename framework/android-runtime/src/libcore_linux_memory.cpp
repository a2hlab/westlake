// libcore's Linux memory calls at the OH libc boundary. On this arm64 target
// size_t and off_t are 64 bits, so mmap has Android mmap64's width and semantics.
#include <jni.h>
#include <nativehelper/JNIHelp.h>
#include <nativehelper/JNIPlatformHelp.h>
#include <cerrno>
#include <cstdint>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

namespace android {
namespace {
static_assert(sizeof(off_t) == sizeof(jlong) && sizeof(size_t) == sizeof(jlong));
void* address(jlong value) { return reinterpret_cast<void*>(static_cast<uintptr_t>(value)); }
void check(JNIEnv* env, const char* call, int result) {
    if (result == -1) jniThrowErrnoException(env, call, errno);
}
jlong map(JNIEnv* env, jobject, jlong hint, jlong bytes, jint prot, jint flags, jobject descriptor, jlong offset) {
    int fd = jniGetFDFromFileDescriptor(env, descriptor);
    if (env->ExceptionCheck()) return -1;
    void* result = mmap(address(hint), bytes, prot, flags, fd, offset);
    if (result == MAP_FAILED) jniThrowErrnoException(env, "mmap", errno);
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(result));
}
void unmap(JNIEnv* env, jobject, jlong start, jlong bytes) {
    check(env, "munmap", TEMP_FAILURE_RETRY(munmap(address(start), bytes)));
}
void lock(JNIEnv* env, jobject, jlong start, jlong bytes) {
    check(env, "mlock", TEMP_FAILURE_RETRY(mlock(address(start), bytes)));
}
void unlock(JNIEnv* env, jobject, jlong start, jlong bytes) {
    check(env, "munlock", TEMP_FAILURE_RETRY(munlock(address(start), bytes)));
}
void sync(JNIEnv* env, jobject, jlong start, jlong bytes, jint flags) {
    check(env, "msync", TEMP_FAILURE_RETRY(msync(address(start), bytes, flags)));
}
const JNINativeMethod methods[] = {
    {"mmap", "(JJIILjava/io/FileDescriptor;J)J", reinterpret_cast<void*>(map)},
    {"munmap", "(JJ)V", reinterpret_cast<void*>(unmap)},
    {"mlock", "(JJ)V", reinterpret_cast<void*>(lock)},
    {"munlock", "(JJ)V", reinterpret_cast<void*>(unlock)},
    {"msync", "(JJI)V", reinterpret_cast<void*>(sync)},
};
}
int register_westlake_linux_memory(JNIEnv* env) {
    jclass linuxClass = env->FindClass("libcore/io/Linux");
    if (!linuxClass) return -1;
    int result = env->RegisterNatives(linuxClass, methods, sizeof(methods) / sizeof(methods[0]));
    env->DeleteLocalRef(linuxClass);
    return result;
}
}
