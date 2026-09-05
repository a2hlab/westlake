/* OHOS counterpart of libcore's asynchronous close monitor. No Java heap
 * pointers are retained while blocking. SIGRTMAX-2 matches the upstream
 * non-Bionic convention; never overwrite another component's handler.
 */
#pragma once
#include <atomic>
#include <cerrno>
#include <csignal>
#include <mutex>
#include <pthread.h>
#include <poll.h>
#include <time.h>

namespace oh_network {
class CloseMonitor {
    static inline std::mutex mutex;
    static inline CloseMonitor* first = nullptr;
    CloseMonitor* next;
    int fd;
    pthread_t thread = pthread_self();
    std::atomic<bool> closed{false};
    static void interrupt(int) {}
public:
    static bool initialize() {
        struct sigaction previous{}, action{};
        const int signal = SIGRTMAX - 2;
        if (sigaction(signal, nullptr, &previous)) return false;
        if (previous.sa_handler != SIG_DFL && previous.sa_handler != interrupt) {
            errno = EBUSY;
            return false;
        }
        sigemptyset(&action.sa_mask);
        action.sa_handler = interrupt; // Deliberately no SA_RESTART.
        return sigaction(signal, &action, nullptr) == 0;
    }
    explicit CloseMonitor(int descriptor) : fd(descriptor) {
        std::lock_guard<std::mutex> lock(mutex);
        next = first;
        first = this;
    }
    ~CloseMonitor() {
        std::lock_guard<std::mutex> lock(mutex);
        CloseMonitor** link = &first;
        while (*link != this) link = &(*link)->next;
        *link = next;
    }
    CloseMonitor(const CloseMonitor&) = delete;
    CloseMonitor& operator=(const CloseMonitor&) = delete;
    bool signalled() const { return closed.load(std::memory_order_acquire); }
    // Block delivery while checking the close flag, then atomically restore
    // the caller's mask inside ppoll. A close between the check and wait is
    // now pending, not a signal consumed just before a blocking poll starts.
    static int pollInterruptibly(pollfd* descriptors, nfds_t count, int timeout) {
        sigset_t block, previous;
        sigemptyset(&block); sigaddset(&block, SIGRTMAX - 2);
        const int maskError = pthread_sigmask(SIG_BLOCK, &block, &previous);
        if (maskError) { errno = maskError; return -1; }
        bool wasClosed = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (CloseMonitor* item = first; item; item = item->next)
                if (pthread_equal(item->thread, pthread_self()) && item->signalled()) wasClosed = true;
        }
        timespec duration{timeout < 0 ? 0 : timeout / 1000,
                          timeout < 0 ? 0 : (timeout % 1000) * 1000000L};
        int result = -1;
        if (!wasClosed) result = ppoll(descriptors, count, timeout < 0 ? nullptr : &duration, &previous);
        const int saved = wasClosed ? EBADF : errno;
        pthread_sigmask(SIG_SETMASK, &previous, nullptr);
        errno = saved;
        return result;
    }
    static void signal(int descriptor) {
        std::lock_guard<std::mutex> lock(mutex);
        for (CloseMonitor* item = first; item; item = item->next) {
            if (item->fd == descriptor) {
                item->closed.store(true, std::memory_order_release);
                pthread_kill(item->thread, SIGRTMAX - 2);
            }
        }
    }
};
}
