/* Android shared-memory API backed by Linux memfd on OpenHarmony. */
#include <cutils/ashmem.h>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

int ashmem_create_region(const char* name, size_t size) {
    int fd = syscall(SYS_memfd_create, name ? name : "android-shared-memory",
                     MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) != 0) {
        int error = errno;
        close(fd);
        errno = error;
        return -1;
    }
    return fd;
}
int ashmem_valid(int fd) {
    return fcntl(fd, F_GET_SEALS) >= 0;
}
int ashmem_get_size_region(int fd) {
    if (!ashmem_valid(fd)) { errno = ENOTTY; return -1; }
    struct stat info;
    if (fstat(fd, &info) != 0) return -1;
    if (info.st_size > INT_MAX) { errno = EOVERFLOW; return -1; }
    return info.st_size;
}
int ashmem_set_prot_region(int fd, int prot) {
    int seals = fcntl(fd, F_GET_SEALS);
    if (seals < 0) return -1;
    if (prot & PROT_WRITE) {
        if (seals & (F_SEAL_WRITE | F_SEAL_FUTURE_WRITE)) { errno = EINVAL; return -1; }
        return 0;
    }
    return fcntl(fd, F_ADD_SEALS, F_SEAL_FUTURE_WRITE);
}
/* memfd pages are not discardable ashmem pages, so pin/unpin cannot purge them. */
int ashmem_pin_region(int fd, size_t, size_t) {
    if (!ashmem_valid(fd)) return -1;
    return 0;  // ASHMEM_NOT_PURGED
}
int ashmem_unpin_region(int fd, size_t, size_t) {
    if (!ashmem_valid(fd)) return -1;
    return 0;  // ASHMEM_IS_UNPINNED
}
