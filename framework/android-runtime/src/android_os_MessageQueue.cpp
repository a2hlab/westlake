// ============================================================================
// android_os_MessageQueue.cpp
//
// JNI for android.os.MessageQueue — Looper core. ActivityThread.main() calls
// Looper.prepareMainLooper → new MessageQueue(true) → nativeInit() at line 79.
// Without this registered, UnsatisfiedLinkError kills the child.
//
// Implementation: real Linux epoll + eventfd, matching AOSP system/core's
// libutils Looper semantics. This is a functional MessageQueue, not a stub —
// Hello World needs pollOnce/wake to actually deliver UI events after the
// main looper is created.
//
// AOSP NativeMessageQueue wraps android::Looper (from libutils) with sp<> +
// NativeMessageQueue::raiseException. We skip the sp/libutils dependency
// and implement directly with POSIX primitives. Behavior matches what
// MessageQueue.java expects:
//   - nativeInit: allocate native handle, returns long pointer
//   - nativePollOnce(ptr, timeoutMs): block up to timeoutMs waiting on
//       wake fd or registered fds; timeoutMs==-1 = infinite; 0 = no-wait
//   - nativeWake(ptr): signal wake fd so pollOnce returns immediately
//   - nativeIsPolling(ptr): true if pollOnce is currently blocking
//   - nativeDestroy(ptr): close fds + delete handle
//   - nativeSetFileDescriptorEvents: register/unregister Java fd monitoring
//       (still unused by the current framework build)
//
// WESTLAKE §708: Android's NDK ALooper and Java MessageQueue are two APIs over
// the SAME per-thread android::Looper.  Chromium depends on that contract: it
// calls ALooper_prepare()/ALooper_addFd() on the UI thread, then expects
// MessageQueue.nativePollOnce() to dispatch those callbacks.  The old
// libandroid shim returned success while discarding every fd, permanently
// starving WebView navigation.  This implementation exposes a narrow C ABI to
// libandroid.so and registers its callback fds in this MessageQueue's epoll set.
// ============================================================================

#include "AndroidRuntime.h"

#include <jni.h>
#include <atomic>
#include <new>          // std::nothrow
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <time.h>

namespace android {

namespace {

constexpr uint64_t kNativeMqMagic = 0x574c4d514c4f4f50ULL;  // "WLMQLOOP"
constexpr int kMaxNdkLooperFds = 64;
constexpr int kMaxLiveNativeMqs = 4096;

using WlLooperCallback = int (*)(int fd, int events, void* data);
using WlPostedCallback = void (*)(void* data);

struct NdkFdRegistration {
    int fd;
    int ident;
    int events;
    WlLooperCallback callback;
    void* data;
};

struct NativeMQ {
    uint64_t magic;
    int epfd;
    int wakefd;
    std::atomic<bool> polling{false};
    // libandroid's ALooper wrapper keeps its initial native reference for the
    // lifetime of the thread.  Remember when that API has exposed this queue
    // so MessageQueue.nativeDestroy() cannot leave the wrapper with a dangling
    // backend pointer.  The retained queue is process-bounded and mirrors the
    // lifetime of the wrapper's thread-local ALooper.
    bool ndkOwned;
    // Each Java MessageQueue owns one native wrapper.  The only legal reuse is
    // an NDK-first ALooper backend being adopted by its first Java queue.
    bool javaOwned;
    pthread_mutex_t fdLock;
    NdkFdRegistration ndkFds[kMaxNdkLooperFds];
};

struct PostedCallback {
    NativeMQ* mq;
    int fd;
    WlPostedCallback callback;
    void* data;
};

thread_local NativeMQ* gThreadNativeMq = nullptr;
pthread_mutex_t gLiveNativeMqsLock = PTHREAD_MUTEX_INITIALIZER;
NativeMQ* gLiveNativeMqs[kMaxLiveNativeMqs] = {};
std::atomic<uint64_t> gNdkDispatchCount{0};
std::atomic<int> gNdkTraceRemaining{0};

static bool IsLiveNativeMq(NativeMQ* mq) {
    if (mq == nullptr) return false;
    bool live = false;
    pthread_mutex_lock(&gLiveNativeMqsLock);
    for (int i = 0; i < kMaxLiveNativeMqs; ++i) {
        if (gLiveNativeMqs[i] == mq) {
            live = true;
            break;
        }
    }
    pthread_mutex_unlock(&gLiveNativeMqsLock);
    return live;
}

static bool RegisterLiveNativeMq(NativeMQ* mq) {
    bool registered = false;
    pthread_mutex_lock(&gLiveNativeMqsLock);
    for (int i = 0; i < kMaxLiveNativeMqs; ++i) {
        if (gLiveNativeMqs[i] == nullptr) {
            gLiveNativeMqs[i] = mq;
            registered = true;
            break;
        }
    }
    pthread_mutex_unlock(&gLiveNativeMqsLock);
    return registered;
}

static bool AcquireJavaOwnership(NativeMQ* mq) {
    if (mq == nullptr) return false;
    bool acquired = false;
    pthread_mutex_lock(&gLiveNativeMqsLock);
    for (int i = 0; i < kMaxLiveNativeMqs; ++i) {
        if (gLiveNativeMqs[i] == mq) {
            if (mq->ndkOwned && !mq->javaOwned) {
                mq->javaOwned = true;
                acquired = true;
            }
            break;
        }
    }
    pthread_mutex_unlock(&gLiveNativeMqsLock);
    return acquired;
}

enum class JavaDestroyAction { kStale, kDuplicate, kRetainForNdk, kDelete };

static JavaDestroyAction ReleaseJavaOwnership(NativeMQ* mq) {
    JavaDestroyAction action = JavaDestroyAction::kStale;
    pthread_mutex_lock(&gLiveNativeMqsLock);
    for (int i = 0; i < kMaxLiveNativeMqs; ++i) {
        if (gLiveNativeMqs[i] != mq) continue;
        if (!mq->javaOwned) {
            action = JavaDestroyAction::kDuplicate;
        } else {
            mq->javaOwned = false;
            if (mq->ndkOwned) {
                action = JavaDestroyAction::kRetainForNdk;
            } else {
                gLiveNativeMqs[i] = nullptr;
                action = JavaDestroyAction::kDelete;
            }
        }
        break;
    }
    pthread_mutex_unlock(&gLiveNativeMqsLock);
    return action;
}

static bool MarkNdkOwned(NativeMQ* mq) {
    bool marked = false;
    pthread_mutex_lock(&gLiveNativeMqsLock);
    for (int i = 0; i < kMaxLiveNativeMqs; ++i) {
        if (gLiveNativeMqs[i] == mq) {
            mq->ndkOwned = true;
            marked = true;
            break;
        }
    }
    pthread_mutex_unlock(&gLiveNativeMqsLock);
    return marked;
}

// Diagnostic for the standalone runtime's main Java queue. AOSP represents a
// synchronization barrier as a head Message with a null target. If that head
// remains old while nativePollOnce keeps running, ordinary Handler work (such
// as Fresco's UI executor result) cannot pass it. Sample only the process main
// thread and rate-limit output; this is observation only and never mutates the
// Java queue.
static void TraceStaleMainQueueBarrier(JNIEnv* env, jobject queue, jint timeoutMs) {
    if (env == nullptr || queue == nullptr ||
        static_cast<pid_t>(syscall(SYS_gettid)) != getpid()) {
        return;
    }
    struct timespec nowTs = {};
    if (clock_gettime(CLOCK_MONOTONIC, &nowTs) != 0) return;
    const int64_t nowMs = static_cast<int64_t>(nowTs.tv_sec) * 1000LL +
                          static_cast<int64_t>(nowTs.tv_nsec) / 1000000LL;
    static std::atomic<int64_t> lastSampleMs{0};
    int64_t prior = lastSampleMs.load(std::memory_order_relaxed);
    if (nowMs - prior < 250 ||
        !lastSampleMs.compare_exchange_strong(prior, nowMs,
                                               std::memory_order_relaxed)) {
        return;
    }

    jclass queueClass = env->GetObjectClass(queue);
    jfieldID messagesField = queueClass == nullptr ? nullptr :
        env->GetFieldID(queueClass, "mMessages", "Landroid/os/Message;");
    if (env->ExceptionCheck()) { env->ExceptionClear(); messagesField = nullptr; }
    jobject head = messagesField == nullptr ? nullptr :
        env->GetObjectField(queue, messagesField);
    if (env->ExceptionCheck()) { env->ExceptionClear(); head = nullptr; }
    if (head != nullptr) {
        jclass messageClass = env->GetObjectClass(head);
        jfieldID targetField = messageClass == nullptr ? nullptr :
            env->GetFieldID(messageClass, "target", "Landroid/os/Handler;");
        jfieldID whenField = messageClass == nullptr ? nullptr :
            env->GetFieldID(messageClass, "when", "J");
        jfieldID tokenField = messageClass == nullptr ? nullptr :
            env->GetFieldID(messageClass, "arg1", "I");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            targetField = nullptr;
            whenField = nullptr;
            tokenField = nullptr;
        }
        jobject target = targetField == nullptr ? nullptr :
            env->GetObjectField(head, targetField);
        if (env->ExceptionCheck()) { env->ExceptionClear(); target = nullptr; }
        if (targetField != nullptr && target == nullptr) {
            const jlong when = whenField == nullptr ? 0 :
                env->GetLongField(head, whenField);
            const jint token = tokenField == nullptr ? -1 :
                env->GetIntField(head, tokenField);
            if (env->ExceptionCheck()) env->ExceptionClear();
            const int64_t ageMs = when > 0 ? nowMs - static_cast<int64_t>(when) : -1;
            if (ageMs >= 200) {
                fprintf(stderr,
                        "[WESTLAKE-MQ-BARRIER-DIAG] queue=%p head=%p token=%d"
                        " when=%lld now=%lld age=%lld timeout=%d\n",
                        static_cast<void*>(queue), static_cast<void*>(head), token,
                        static_cast<long long>(when), static_cast<long long>(nowMs),
                        static_cast<long long>(ageMs), timeoutMs);
                fflush(stderr);
            }
        }
        if (target != nullptr) env->DeleteLocalRef(target);
        if (messageClass != nullptr) env->DeleteLocalRef(messageClass);
        env->DeleteLocalRef(head);
    }
    if (queueClass != nullptr) env->DeleteLocalRef(queueClass);
}

static uint32_t ToEpollEvents(int events) {
    uint32_t result = EPOLLERR | EPOLLHUP;
    if ((events & 1) != 0) result |= EPOLLIN;   // ALOOPER_EVENT_INPUT
    if ((events & 2) != 0) result |= EPOLLOUT;  // ALOOPER_EVENT_OUTPUT
    return result;
}

static int FromEpollEvents(uint32_t events) {
    int result = 0;
    if ((events & EPOLLIN) != 0) result |= 1;
    if ((events & EPOLLOUT) != 0) result |= 2;
    if ((events & EPOLLERR) != 0) result |= 4;
    if ((events & EPOLLHUP) != 0) result |= 8;
    return result;
}

static void WakeNativeMq(NativeMQ* mq) {
    if (mq == nullptr || mq->wakefd < 0) return;
    uint64_t value = 1;
    (void)write(mq->wakefd, &value, sizeof(value));
}

static bool CopyNdkRegistration(NativeMQ* mq, int fd, NdkFdRegistration* out) {
    if (mq == nullptr || out == nullptr) return false;
    bool found = false;
    pthread_mutex_lock(&mq->fdLock);
    for (int i = 0; i < kMaxNdkLooperFds; ++i) {
        if (mq->ndkFds[i].fd == fd) {
            *out = mq->ndkFds[i];
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&mq->fdLock);
    return found;
}

static int AddNdkLooperFd(NativeMQ* mq, int fd, int ident, int events,
                          WlLooperCallback callback, void* data) {
    if (mq == nullptr || mq->magic != kNativeMqMagic || fd < 0 || callback == nullptr) {
        return -1;
    }

    int slot = -1;
    bool replacing = false;
    pthread_mutex_lock(&mq->fdLock);
    for (int i = 0; i < kMaxNdkLooperFds; ++i) {
        if (mq->ndkFds[i].fd == fd) {
            slot = i;
            replacing = true;
            break;
        }
        if (slot < 0 && mq->ndkFds[i].fd < 0) slot = i;
    }
    if (slot >= 0) {
        mq->ndkFds[slot] = {fd, ident, events, callback, data};
    }
    pthread_mutex_unlock(&mq->fdLock);
    if (slot < 0) return -1;

    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = ToEpollEvents(events);
    event.data.fd = fd;
    int operation = replacing ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (epoll_ctl(mq->epfd, operation, fd, &event) < 0) {
        // A stale userspace entry can survive an externally closed/reused fd.
        // Retry as ADD/MOD once before reporting failure.
        operation = replacing ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
        if (epoll_ctl(mq->epfd, operation, fd, &event) < 0) {
            pthread_mutex_lock(&mq->fdLock);
            if (mq->ndkFds[slot].fd == fd) mq->ndkFds[slot].fd = -1;
            pthread_mutex_unlock(&mq->fdLock);
            return -1;
        }
    }
    WakeNativeMq(mq);
    fprintf(stderr,
            "[WESTLAKE-ALOOPER-708] add mq=%p fd=%d ident=%d events=0x%x callback=%p data=%p\n",
            static_cast<void*>(mq), fd, ident, events,
            reinterpret_cast<void*>(callback), data);
    fflush(stderr);
    return 1;
}

static int RemoveNdkLooperFd(NativeMQ* mq, int fd) {
    if (mq == nullptr || mq->magic != kNativeMqMagic || fd < 0) return -1;
    bool found = false;
    pthread_mutex_lock(&mq->fdLock);
    for (int i = 0; i < kMaxNdkLooperFds; ++i) {
        if (mq->ndkFds[i].fd == fd) {
            mq->ndkFds[i].fd = -1;
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&mq->fdLock);
    if (found) {
        (void)epoll_ctl(mq->epfd, EPOLL_CTL_DEL, fd, nullptr);
        WakeNativeMq(mq);
    }
    return found ? 1 : 0;
}

// Execute a one-shot callback from NativeMQ::pollOnce itself.  Android's real
// InputEventReceiver is fd-driven and therefore runs even while a synchronous
// Choreographer barrier is installed in the Java MessageQueue.  Boundary code
// which receives an OH event on a worker thread needs the same property: a
// Handler.post() is not equivalent and can be stranded indefinitely.
static int RunPostedCallback(int fd, int /*events*/, void* opaque) {
    PostedCallback* posted = static_cast<PostedCallback*>(opaque);
    if (posted == nullptr) return 0;

    uint64_t value;
    while (read(fd, &value, sizeof(value)) == sizeof(value)) {}

    // Remove before invoking user code so re-entrant posts cannot observe a
    // stale registration.  Return 1 because the registration is already gone.
    (void)RemoveNdkLooperFd(posted->mq, fd);
    close(fd);
    WlPostedCallback callback = posted->callback;
    void* data = posted->data;
    delete posted;
    if (callback != nullptr) callback(data);
    return 1;
}

static NativeMQ* CreateNativeMq() {
    NativeMQ* mq = new (std::nothrow) NativeMQ();
    if (!mq) {
        return nullptr;
    }
    mq->magic = kNativeMqMagic;
    mq->epfd = -1;
    mq->wakefd = -1;
    mq->ndkOwned = false;
    mq->javaOwned = false;
    pthread_mutex_init(&mq->fdLock, nullptr);
    for (int i = 0; i < kMaxNdkLooperFds; ++i) mq->ndkFds[i].fd = -1;
    mq->wakefd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (mq->wakefd < 0) {
        pthread_mutex_destroy(&mq->fdLock);
        delete mq;
        return 0;
    }
    mq->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (mq->epfd < 0) {
        close(mq->wakefd);
        pthread_mutex_destroy(&mq->fdLock);
        delete mq;
        return nullptr;
    }
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = mq->wakefd;
    if (epoll_ctl(mq->epfd, EPOLL_CTL_ADD, mq->wakefd, &ev) < 0) {
        close(mq->wakefd);
        close(mq->epfd);
        pthread_mutex_destroy(&mq->fdLock);
        delete mq;
        return nullptr;
    }
    if (!RegisterLiveNativeMq(mq)) {
        close(mq->wakefd);
        close(mq->epfd);
        pthread_mutex_destroy(&mq->fdLock);
        delete mq;
        return nullptr;
    }
    gThreadNativeMq = mq;
    return mq;
}

jlong MQ_nativeInit(JNIEnv* env, jclass /*clazz*/) {
    (void)env;
    // Android's Java MessageQueue and NDK ALooper are two front ends for the
    // same per-thread android::Looper.  Lynx constructs them in NDK-first
    // order: ALooper_prepare(), ALooper_addFd(timerfd), then Looper.prepare().
    // Reusing an NDK-created queue here is therefore required; allocating a
    // second queue strands the timerfd in an epoll set Java never polls.
    NativeMQ* mq = gThreadNativeMq;
    if (AcquireJavaOwnership(mq)) {
        fprintf(stderr,
                "[WESTLAKE-ALOOPER-829] MessageQueue adopt NDK thread mq=%p\n",
                static_cast<void*>(mq));
        fflush(stderr);
        return reinterpret_cast<jlong>(mq);
    }
    // A Java-owned queue must never be handed to a second Java MessageQueue.
    // AOSP allocates a distinct NativeMessageQueue wrapper in that case, even
    // though the wrappers may share the underlying per-thread Looper.
    mq = CreateNativeMq();
    if (mq == nullptr) return 0;
    mq->javaOwned = true;
    return reinterpret_cast<jlong>(mq);
}

void MQ_nativeDestroy(JNIEnv* /*env*/, jclass /*clazz*/, jlong ptr) {
    NativeMQ* mq = reinterpret_cast<NativeMQ*>(ptr);
    if (!mq) return;
    const JavaDestroyAction action = ReleaseJavaOwnership(mq);
    if (action == JavaDestroyAction::kStale ||
        action == JavaDestroyAction::kDuplicate) {
        fprintf(stderr,
                "[WESTLAKE-ALOOPER-OWNERSHIP] ignore %s nativeDestroy mq=%p\n",
                action == JavaDestroyAction::kStale ? "stale" : "duplicate",
                static_cast<void*>(mq));
        fflush(stderr);
        return;
    }
    if (action == JavaDestroyAction::kRetainForNdk) {
        // libandroid's thread-local ALooper still names this backend.  Keep it
        // valid rather than manufacturing a use-after-free during thread
        // teardown; the wrapper itself has process/thread lifetime today.
        fprintf(stderr,
                "[WESTLAKE-ALOOPER-829] MessageQueue retain NDK-owned mq=%p\n",
                static_cast<void*>(mq));
        fflush(stderr);
        return;
    }
    if (gThreadNativeMq == mq) gThreadNativeMq = nullptr;
    mq->magic = 0;
    if (mq->epfd >= 0)  close(mq->epfd);
    if (mq->wakefd >= 0) close(mq->wakefd);
    pthread_mutex_destroy(&mq->fdLock);
    delete mq;
}

void MQ_nativePollOnce(JNIEnv* env, jobject obj,
                        jlong ptr, jint timeoutMs) {
    NativeMQ* mq = reinterpret_cast<NativeMQ*>(ptr);
    if (!mq) return;

    TraceStaleMainQueueBarrier(env, obj, timeoutMs);

    mq->polling.store(true, std::memory_order_release);

    struct epoll_event events[16];
    int timeout = (timeoutMs < 0) ? -1 : timeoutMs;
    int n;
    do {
        n = epoll_wait(mq->epfd, events, 16, timeout);
    } while (n < 0 && errno == EINTR);

    for (int i = 0; i < n; ++i) {
        if (events[i].data.fd == mq->wakefd) {
            // Drain wake counter so next wait blocks correctly until next wake.
            uint64_t tmp;
            (void) read(mq->wakefd, &tmp, sizeof(tmp));
        }
        else {
            NdkFdRegistration registration;
            if (CopyNdkRegistration(mq, events[i].data.fd, &registration) &&
                registration.callback != nullptr) {
                const int callbackEvents = FromEpollEvents(events[i].events);
                const uint64_t dispatchNo =
                    gNdkDispatchCount.fetch_add(1, std::memory_order_relaxed) + 1;
                const bool pendingBefore = env->ExceptionCheck();
                const int keep = registration.callback(
                    registration.fd, callbackEvents, registration.data);
                const bool pendingAfter = env->ExceptionCheck();
                const bool traceDispatch =
                    gNdkTraceRemaining.load(std::memory_order_relaxed) > 0;
                if (traceDispatch) {
                    gNdkTraceRemaining.fetch_sub(1, std::memory_order_relaxed);
                }
                // WESTLAKE §712 diagnostic. Chromium 113's UI eventfd callback
                // intentionally does not drain its eventfd when this JNIEnv has
                // a pending exception. Preserve the exception exactly as-is and
                // expose only its state at the OH/AOSP looper boundary.
                if (dispatchNo <= 40 || traceDispatch || pendingBefore || pendingAfter ||
                    (dispatchNo % 1000) == 0) {
                    fprintf(stderr,
                            "[WESTLAKE-ALOOPER-712] dispatch=%llu mq=%p fd=%d"
                            " events=0x%x callback=%p pendingBefore=%d keep=%d"
                            " pendingAfter=%d\n",
                            static_cast<unsigned long long>(dispatchNo),
                            static_cast<void*>(mq), registration.fd, callbackEvents,
                            reinterpret_cast<void*>(registration.callback),
                            pendingBefore ? 1 : 0, keep, pendingAfter ? 1 : 0);
                    fflush(stderr);
                }
                if (keep == 0) (void)RemoveNdkLooperFd(mq, registration.fd);
            }
        }
    }

    mq->polling.store(false, std::memory_order_release);
}

void MQ_nativeWake(JNIEnv* /*env*/, jclass /*clazz*/, jlong ptr) {
    NativeMQ* mq = reinterpret_cast<NativeMQ*>(ptr);
    WakeNativeMq(mq);
}

jboolean MQ_nativeIsPolling(JNIEnv* /*env*/, jclass /*clazz*/, jlong ptr) {
    NativeMQ* mq = reinterpret_cast<NativeMQ*>(ptr);
    if (!mq) return JNI_FALSE;
    return mq->polling.load(std::memory_order_acquire) ? JNI_TRUE : JNI_FALSE;
}

void MQ_nativeSetFileDescriptorEvents(JNIEnv* /*env*/, jclass /*clazz*/,
                                       jlong /*ptr*/, jint /*fd*/, jint /*events*/) {
    // Stub no-op. Hello World does not install custom fd watchers on the main
    // MessageQueue. If a later stage adds watchers (e.g. InputChannel, Binder
    // driver reader), extend to epoll_ctl ADD/MOD/DEL and track watcher→fd
    // map. Until then, silently ignore.
}

const JNINativeMethod kMessageQueueMethods[] = {
    { "nativeInit",                    "()J",   reinterpret_cast<void*>(MQ_nativeInit) },
    { "nativeDestroy",                 "(J)V",  reinterpret_cast<void*>(MQ_nativeDestroy) },
    { "nativePollOnce",                "(JI)V", reinterpret_cast<void*>(MQ_nativePollOnce) },
    { "nativeWake",                    "(J)V",  reinterpret_cast<void*>(MQ_nativeWake) },
    { "nativeIsPolling",               "(J)Z",  reinterpret_cast<void*>(MQ_nativeIsPolling) },
    { "nativeSetFileDescriptorEvents", "(JII)V",reinterpret_cast<void*>(MQ_nativeSetFileDescriptorEvents) },
};

}  // namespace

// Narrow ABI consumed by the OH libandroid.so shim.  Keeping this in the
// adapter bridge makes the OH boundary explicit while retaining AOSP's single
// per-thread looper semantics above it.
extern "C" __attribute__((visibility("default"))) void* wl_ALooper_prepare(int opts) {
    NativeMQ* mq = gThreadNativeMq;
    bool created = false;
    if (!IsLiveNativeMq(mq)) {
        mq = CreateNativeMq();
        created = mq != nullptr;
    }
    if (mq != nullptr) MarkNdkOwned(mq);
    fprintf(stderr,
            "[WESTLAKE-ALOOPER-829] prepare opts=%d mq=%p created=%d\n",
            opts, static_cast<void*>(mq), created ? 1 : 0);
    fflush(stderr);
    return mq;
}

extern "C" __attribute__((visibility("default"))) int wl_ALooper_addFd(
        void* looper, int fd, int ident, int events,
        WlLooperCallback callback, void* data) {
    return AddNdkLooperFd(static_cast<NativeMQ*>(looper), fd, ident, events, callback, data);
}

extern "C" __attribute__((visibility("default"))) int wl_ALooper_removeFd(
        void* looper, int fd) {
    return RemoveNdkLooperFd(static_cast<NativeMQ*>(looper), fd);
}

extern "C" __attribute__((visibility("default"))) void wl_ALooper_wake(void* looper) {
    NativeMQ* mq = static_cast<NativeMQ*>(looper);
    if (mq != nullptr && mq->magic == kNativeMqMagic) {
        WakeNativeMq(mq);
    }
}

// Post native work to an exact Java MessageQueue.  The pointer is the mPtr of
// Looper.getMainLooper().getQueue(), so this does not guess which of several
// HandlerThread loopers is the UI looper.  Delivery is an epoll callback, just
// like AOSP InputEventReceiver, and consequently bypasses Java sync barriers.
extern "C" __attribute__((visibility("default"))) int wl_MessageQueue_post(
        void* looper, WlPostedCallback callback, void* data) {
    NativeMQ* mq = static_cast<NativeMQ*>(looper);
    if (mq == nullptr || mq->magic != kNativeMqMagic || callback == nullptr) {
        return -1;
    }

    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) return -1;
    PostedCallback* posted = new (std::nothrow) PostedCallback{mq, fd, callback, data};
    if (posted == nullptr) {
        close(fd);
        return -1;
    }
    if (AddNdkLooperFd(mq, fd, 0, 1 /* ALOOPER_EVENT_INPUT */,
                       RunPostedCallback, posted) < 0) {
        delete posted;
        close(fd);
        return -1;
    }

    uint64_t value = 1;
    if (write(fd, &value, sizeof(value)) != sizeof(value)) {
        (void)RemoveNdkLooperFd(mq, fd);
        delete posted;
        close(fd);
        return -1;
    }
    // Registering a descriptor from another thread while epoll_wait() is
    // blocked is not, by itself, a portable wake-up mechanism.  The newly
    // armed eventfd can therefore remain ready but undispatched until some
    // unrelated event reaches this queue.  Wake the queue through its
    // existing wakefd after the registration and payload are both visible,
    // matching Looper::addFd()/wake() cross-thread semantics.
    WakeNativeMq(mq);
    return 0;
}

extern "C" __attribute__((visibility("default"))) void wl_ALooper_traceNext(
        int callbackCount) {
    gNdkTraceRemaining.store(callbackCount > 0 ? callbackCount : 0,
                             std::memory_order_relaxed);
    fprintf(stderr,
            "[WESTLAKE-ALOOPER-715] armed callbacks=%d threadMq=%p dispatchNow=%llu\n",
            callbackCount, static_cast<void*>(gThreadNativeMq),
            static_cast<unsigned long long>(
                gNdkDispatchCount.load(std::memory_order_relaxed)));
    fflush(stderr);
}

int register_android_os_MessageQueue(JNIEnv* env) {
    jclass clazz = env->FindClass("android/os/MessageQueue");
    if (!clazz) {
        return -1;
    }
    jint rc = env->RegisterNatives(clazz, kMessageQueueMethods,
                                    sizeof(kMessageQueueMethods) / sizeof(kMessageQueueMethods[0]));
    env->DeleteLocalRef(clazz);
    return rc == JNI_OK ? 0 : -1;
}

}  // namespace android
