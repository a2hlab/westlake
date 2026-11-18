#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace westlake {

// Explicit diagnostic opt-in only. Raw third-party messages can contain secrets.
// The launcher must provision a private directory owned by the app UID. Never
// fall back to stderr or HiLog if this sink is absent, full or inaccessible.
inline bool capturePrivateAndroidLog(const char* directory, int priority,
                                     const char* tag, const char* message) {
    if (!directory || directory[0] != '/' || !message) return false;
    int dir = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dir < 0) return false;
    struct stat metadata{};
    bool valid = fstat(dir, &metadata) == 0 && S_ISDIR(metadata.st_mode)
            && metadata.st_uid == geteuid() && (metadata.st_mode & 07777) == 0700;
    int file = valid ? openat(dir, "android.log",
            O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600) : -1;
    close(dir);
    if (file < 0) return false;
    // An independent open file description per call makes flock cover threads
    // and forked processes. Drop contended diagnostics instead of stalling apps.
    valid = fstat(file, &metadata) == 0 && S_ISREG(metadata.st_mode)
            && metadata.st_uid == geteuid() && metadata.st_nlink == 1
            && (metadata.st_mode & 07777) == 0600
            && flock(file, LOCK_EX | LOCK_NB) == 0;
    if (!valid) { close(file); return false; }
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    char record[4096];
    int length = snprintf(record, sizeof(record), "%lld.%03ld pid=%d priority=%d %.*s: %.*s\n",
            static_cast<long long>(now.tv_sec), now.tv_nsec / 1000000,
            static_cast<int>(getpid()), priority, 128, tag ? tag : "AndroidLog", 3800, message);
    size_t bytes = length > 0 ? std::min(static_cast<size_t>(length), sizeof(record) - 1) : 0;
    constexpr off_t limit = 8 * 1024 * 1024;
    valid = bytes > 0 && fstat(file, &metadata) == 0 && metadata.st_size >= 0
            && metadata.st_size <= limit - static_cast<off_t>(bytes);
    bool written = false;
    if (valid) {
        // One bounded append while locked. A partial write is a dropped record;
        // no retry or fallback can leak the message to a public log destination.
        ssize_t result;
        do { result = write(file, record, bytes); } while (result < 0 && errno == EINTR);
        written = result == static_cast<ssize_t>(bytes);
    }
    flock(file, LOCK_UN);
    close(file);
    return written;
}

}  // namespace westlake
