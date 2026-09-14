/* Android traffic accounting boundary for the OpenHarmony host.
 *
 * Android's tagging API writes netd-owned BPF accounting maps. OpenHarmony's
 * public socket API does not expose that accounting operation. Preserve the
 * NDK's negative-error contract instead of reporting a tag which was not set.
 * TrafficStats treats this error as diagnostic; ordinary socket I/O is intact.
 */
#include <android/multinetwork.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>

static int socket_accounting_error(int fd) {
    int domain = 0;
    socklen_t length = sizeof(domain);
    if (getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &domain, &length) < 0) return -errno;
    if (domain != AF_INET && domain != AF_INET6) return -EAFNOSUPPORT;
    int protocol = 0;
    length = sizeof(protocol);
    if (getsockopt(fd, SOL_SOCKET, SO_PROTOCOL, &protocol, &length) < 0) return -errno;
    if (protocol != IPPROTO_TCP && protocol != IPPROTO_UDP) return -EPROTONOSUPPORT;
    return -EOPNOTSUPP;
}

int android_tag_socket_with_uid(int fd, uint32_t tag, uid_t uid) {
    (void)tag;
    (void)uid;
    return socket_accounting_error(fd);
}

int android_tag_socket(int fd, uint32_t tag) {
    return android_tag_socket_with_uid(fd, tag, (uid_t)-1);
}

int android_untag_socket(int fd) {
    return socket_accounting_error(fd);
}
