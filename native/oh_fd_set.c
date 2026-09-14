// Android's fortified fd_set ABI over the host's fd_set operations. Keep the
// descriptor and object-size checks from bionic's __check_fd_set contract.
#include <stdlib.h>
#include <stdio.h>
#include <sys/select.h>

static void check_set(int fd, size_t size) {
    if (fd < 0 || fd >= FD_SETSIZE || size < sizeof(fd_set)) {
        fprintf(stderr, "[WESTLAKE-BIONIC-ABI] invalid fd_set: fd=%d size=%zu\n", fd, size);
        abort();
    }
}

void __FD_SET_chk(int fd, fd_set* set, size_t size) {
    check_set(fd, size);
    FD_SET(fd, set);
}

void __FD_CLR_chk(int fd, fd_set* set, size_t size) {
    check_set(fd, size);
    FD_CLR(fd, set);
}

int __FD_ISSET_chk(int fd, const fd_set* set, size_t size) {
    check_set(fd, size);
    return FD_ISSET(fd, set);
}
