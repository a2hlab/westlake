// ============================================================================
// syncfence_natives.cpp (libwl_missing_natives.so)
//
// JNI bindings for android.hardware.SyncFence (API 33+): a sync file descriptor that a GPU, a display or a
// producer signals. Flutter's Android embedding and other apps that read HardwareBuffer frames use it, and
// without these natives the class's first use fails ("No implementation found for ...
// SyncFence.nGetDestructor"): 4 apps of the D600's HK round hkid1 and 4 of the phones' rounds.
//
// The natives keep AOSP's semantics (frameworks/base/core/jni/android_hardware_SyncFence.cpp over
// android::Fence), on the fd itself: Linux sync files are the same on OH.
// - nCreate takes the fd's ownership; the native object is reference-counted; nGetDestructor returns the
//   function NativeAllocationRegistry calls with the pointer, which drops one reference and closes the fd
//   with the last.
// - An invalid fence (fd < 0) counts as signalled, as Fence::wait returns OK for Fence::NO_FENCE.
// - nWait: a negative timeout waits forever; poll(2) on a sync file reports POLLIN once it signals.
// - nGetSignalTime: SIGNAL_TIME_INVALID (-1) for an invalid fence or an error, SIGNAL_TIME_PENDING
//   (Long.MAX_VALUE) while any of its fences is still active, else the latest signal time
//   (CLOCK_MONOTONIC ns), from the sync file's own info (SYNC_IOC_FILE_INFO).
// ============================================================================

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <jni.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

constexpr jlong kSignalTimeInvalid = -1;
constexpr jlong kSignalTimePending = INT64_MAX;

struct SyncFence {
    std::atomic<int32_t> refs{1};
    int fd = -1;
};

SyncFence* fromJlong(jlong pointer) {
    return reinterpret_cast<SyncFence*>(static_cast<uintptr_t>(pointer));
}

void release(SyncFence* fence) {
    if (fence != nullptr && fence->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        if (fence->fd >= 0) close(fence->fd);
        delete fence;
    }
}

// NativeAllocationRegistry's free function: void (*)(void*).
void destroyFence(void* pointer) {
    release(static_cast<SyncFence*>(pointer));
}

// The kernel's sync file info (include/uapi/linux/sync_file.h), declared here so the build does not depend on
// the sysroot shipping that header.
struct KernelSyncFenceInfo {
    char obj_name[32];
    char driver_name[32];
    int32_t status;      // 1 signalled, 0 active, < 0 error
    uint32_t flags;
    uint64_t timestamp_ns;
};
struct KernelSyncFileInfo {
    char name[32];
    int32_t status;
    uint32_t flags;
    uint32_t num_fences;
    uint32_t pad;
    uint64_t sync_fence_info;
};
constexpr unsigned long kSyncIocFileInfo = _IOWR('>', 4, KernelSyncFileInfo);

jlong signalTime(int fd) {
    if (fd < 0) return kSignalTimeInvalid;
    KernelSyncFileInfo info{};
    if (ioctl(fd, kSyncIocFileInfo, &info) != 0) return kSignalTimeInvalid;
    if (info.num_fences == 0) return info.status == 1 ? 0 : kSignalTimeInvalid;
    uint32_t count = info.num_fences;
    auto* fences = static_cast<KernelSyncFenceInfo*>(calloc(count, sizeof(KernelSyncFenceInfo)));
    if (fences == nullptr) return kSignalTimeInvalid;
    memset(&info, 0, sizeof(info));
    info.num_fences = count;
    info.sync_fence_info = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(fences));
    jlong result = kSignalTimeInvalid;
    if (ioctl(fd, kSyncIocFileInfo, &info) == 0) {
        result = 0;
        for (uint32_t i = 0; i < info.num_fences && i < count; ++i) {
            if (fences[i].status < 0) { result = kSignalTimeInvalid; break; }
            if (fences[i].status == 0) { result = kSignalTimePending; break; }
            if (static_cast<jlong>(fences[i].timestamp_ns) > result) result = static_cast<jlong>(fences[i].timestamp_ns);
        }
    }
    free(fences);
    return result;
}

jlong SyncFence_getDestructor(JNIEnv*, jclass) {
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(&destroyFence));
}

jlong SyncFence_create(JNIEnv*, jclass, jint fd) {
    auto* fence = new SyncFence();
    fence->fd = fd;
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(fence));
}

jboolean SyncFence_isValid(JNIEnv*, jclass, jlong pointer) {
    SyncFence* fence = fromJlong(pointer);
    return fence != nullptr && fence->fd >= 0 ? JNI_TRUE : JNI_FALSE;
}

jint SyncFence_getFd(JNIEnv*, jclass, jlong pointer) {
    SyncFence* fence = fromJlong(pointer);
    return fence != nullptr ? fence->fd : -1;
}

jboolean SyncFence_wait(JNIEnv*, jclass, jlong pointer, jlong timeoutNanos) {
    SyncFence* fence = fromJlong(pointer);
    if (fence == nullptr || fence->fd < 0) return JNI_TRUE;   // no fence: nothing to wait for
    int timeoutMs = timeoutNanos < 0 ? -1
        : static_cast<int>(timeoutNanos / 1000000 > INT32_MAX ? INT32_MAX : (timeoutNanos + 999999) / 1000000);
    struct pollfd pfd{fence->fd, POLLIN, 0};
    for (;;) {
        int ready = poll(&pfd, 1, timeoutMs);
        if (ready > 0) return (pfd.revents & POLLIN) != 0 && (pfd.revents & (POLLERR | POLLNVAL)) == 0 ? JNI_TRUE
                                                                                                       : JNI_FALSE;
        if (ready == 0) return JNI_FALSE;                      // timed out
        if (errno != EINTR) return JNI_FALSE;
    }
}

jlong SyncFence_getSignalTime(JNIEnv*, jclass, jlong pointer) {
    SyncFence* fence = fromJlong(pointer);
    return fence != nullptr ? signalTime(fence->fd) : kSignalTimeInvalid;
}

void SyncFence_incRef(JNIEnv*, jclass, jlong pointer) {
    SyncFence* fence = fromJlong(pointer);
    if (fence != nullptr) fence->refs.fetch_add(1, std::memory_order_relaxed);
}

const JNINativeMethod kSyncFenceMethods[] = {
    {"nGetDestructor", "()J", reinterpret_cast<void*>(SyncFence_getDestructor)},
    {"nCreate", "(I)J", reinterpret_cast<void*>(SyncFence_create)},
    {"nIsValid", "(J)Z", reinterpret_cast<void*>(SyncFence_isValid)},
    {"nGetFd", "(J)I", reinterpret_cast<void*>(SyncFence_getFd)},
    {"nWait", "(JJ)Z", reinterpret_cast<void*>(SyncFence_wait)},
    {"nGetSignalTime", "(J)J", reinterpret_cast<void*>(SyncFence_getSignalTime)},
    {"nIncRef", "(J)V", reinterpret_cast<void*>(SyncFence_incRef)},
};

}  // namespace

// Binds each native separately (as missing_natives.c's bind does): a framework that does not declare one
// native leaves the others bound. A framework without the class (before API 33) binds none.
extern "C" int wl_register_syncfence(JNIEnv* env) {
    jclass clazz = env->FindClass("android/hardware/SyncFence");
    if (clazz == nullptr) {
        env->ExceptionClear();
        return 0;
    }
    int bound = 0;
    for (const JNINativeMethod& method : kSyncFenceMethods) {
        if (env->RegisterNatives(clazz, &method, 1) == JNI_OK) {
            ++bound;
        } else {
            env->ExceptionClear();
        }
    }
    env->DeleteLocalRef(clazz);
    fprintf(stderr, "[WL-NATIVES] SyncFence bound %d/%zu\n", bound,
            sizeof(kSyncFenceMethods) / sizeof(kSyncFenceMethods[0]));
    fflush(stderr);
    return bound;
}
