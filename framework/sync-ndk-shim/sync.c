/*
 * libsync.so: the NDK's sync-file API (android/sync.h, API 26) over the kernel's sync_file ioctls,
 * as Android's libsync implements it. Firefox's libxul.so names libsync.so in DT_NEEDED and failed
 * to load without it ("library libsync.so ... needed by libxul.so").
 */
#include <errno.h>
#include <linux/sync_file.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#define EXPORT __attribute__((visibility("default")))

/* Waits for the fence to signal: 0 when it has, -1 with ETIME on timeout (Android's answer). */
EXPORT int sync_wait(int fd, int timeout)
{
    struct pollfd fds = {.fd = fd, .events = POLLIN};
    if (fd < 0) {
        errno = EINVAL;
        return -1;
    }
    for (;;) {
        int ret = poll(&fds, 1, timeout);
        if (ret > 0) {
            if (fds.revents & (POLLERR | POLLNVAL)) {
                errno = EINVAL;
                return -1;
            }
            return 0;
        }
        if (ret == 0) {
            errno = ETIME;
            return -1;
        }
        if (errno != EINTR && errno != EAGAIN) return -1;
    }
}

EXPORT int32_t sync_merge(const char *name, int32_t fd1, int32_t fd2)
{
    struct sync_merge_data data;
    memset(&data, 0, sizeof(data));
    data.fd2 = fd2;
    strncpy(data.name, name, sizeof(data.name) - 1);
    int ret;
    do {
        ret = ioctl(fd1, SYNC_IOC_MERGE, &data);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
    return ret < 0 ? ret : (int32_t) data.fence;
}

/* The file's info followed by its fences' (the layout android/sync.h's sync_get_fence_info reads). */
EXPORT struct sync_file_info *sync_file_info(int32_t fd)
{
    struct sync_file_info probe;
    memset(&probe, 0, sizeof(probe));
    if (ioctl(fd, SYNC_IOC_FILE_INFO, &probe) < 0) return NULL;
    size_t count = probe.num_fences;
    struct sync_file_info *info = calloc(1, sizeof(*info) + count * sizeof(struct sync_fence_info));
    if (info == NULL) return NULL;
    info->num_fences = (uint32_t) count;
    info->sync_fence_info = (uint64_t) (uintptr_t) (info + 1);
    if (ioctl(fd, SYNC_IOC_FILE_INFO, info) < 0) {
        free(info);
        return NULL;
    }
    return info;
}

EXPORT void sync_file_info_free(struct sync_file_info *info)
{
    free(info);
}
