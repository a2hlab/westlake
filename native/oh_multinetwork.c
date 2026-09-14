/* Android multinetwork boundary for the OpenHarmony host.
 *
 * frameworks-base/native/android/net.c implements the android_* multinetwork
 * NDK entry points on top of netd's client library (setNetworkForProcess,
 * resNetworkQuery, tagSocket, ...). OpenHarmony has no netd, no fwmark daemon
 * and no dnsproxyd socket, so that translation unit cannot be built here; its
 * traffic-accounting half already lives in oh_socket_tagging.c and this file
 * supplies the remaining half.
 *
 * What OpenHarmony does provide is a single default network for the process,
 * managed by the platform itself. So the process/socket binding calls have
 * nothing to route between: they record the request and report success, and
 * the recorded value round-trips exactly as it does on Android. The resolver
 * calls have no OpenHarmony analogue at all, so they report the NDK's
 * documented negative-POSIX failure rather than pretending to issue a query.
 * Ordinary name resolution is unaffected — it goes through getaddrinfo, not
 * through this API.
 */
#include <android/multinetwork.h>
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/socket.h>

/* Handle encoding, identical to net.c. This MUST stay in sync with the
 * android.net.Network#getNetworkHandle() implementation we build from source. */
static const uint32_t kHandleMagic = 0xcafed00d;
static const uint32_t kHandleMagicSize = 32;

/* netd's "no network selected"; the handle spelling of it is NETWORK_UNSPECIFIED. */
#define WESTLAKE_NETID_UNSET 0u

static int netid_from_handle(net_handle_t handle, unsigned *netid) {
    static const uint32_t k32BitMask = 0xffffffff;
    if (handle != NETWORK_UNSPECIFIED && (handle & k32BitMask) != kHandleMagic) return 0;
    if (netid != NULL) *netid = (unsigned)((handle >> (CHAR_BIT * sizeof(k32BitMask))) & k32BitMask);
    return 1;
}

static net_handle_t handle_from_netid(unsigned netid) {
    if (netid == WESTLAKE_NETID_UNSET) return NETWORK_UNSPECIFIED;
    return (((net_handle_t)netid) << kHandleMagicSize) | kHandleMagic;
}

/* Process-wide on Android too (netd's client keeps one word per process), and
 * read from arbitrary threads, so hold them the way netd's client does. */
static _Atomic unsigned g_process_network = WESTLAKE_NETID_UNSET;
static _Atomic unsigned g_process_dns = WESTLAKE_NETID_UNSET;

int android_setprocnetwork(net_handle_t network) {
    unsigned netid;
    if (!netid_from_handle(network, &netid)) {
        errno = EINVAL;
        return -1;
    }
    /* Nothing to switch between: the process already runs on the platform's
     * only network, which is the one this handle can have come from. */
    atomic_store(&g_process_network, netid);
    return 0;
}

int android_getprocnetwork(net_handle_t *network) {
    if (network == NULL) {
        errno = EINVAL;
        return -1;
    }
    *network = handle_from_netid(atomic_load(&g_process_network));
    return 0;
}

int android_setprocdns(net_handle_t network) {
    unsigned netid;
    if (!netid_from_handle(network, &netid)) {
        errno = EINVAL;
        return -1;
    }
    atomic_store(&g_process_dns, netid);
    return 0;
}

int android_getprocdns(net_handle_t *network) {
    if (network == NULL) {
        errno = EINVAL;
        return -1;
    }
    unsigned netid = atomic_load(&g_process_dns);
    if (netid == WESTLAKE_NETID_UNSET) {
        /* Matches netd's getNetworkForDns() when nothing has been bound. */
        errno = ENONET;
        return -1;
    }
    *network = handle_from_netid(netid);
    return 0;
}

int android_setsocknetwork(net_handle_t network, int fd) {
    unsigned netid;
    if (!netid_from_handle(network, &netid)) {
        errno = EINVAL;
        return -1;
    }
    /* netd validates the descriptor before marking it; keep that failure mode
     * real rather than accepting descriptors it would have rejected. */
    int domain = 0;
    socklen_t length = sizeof(domain);
    if (getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &domain, &length) < 0) return -1;
    return 0;
}

/* The resolver API is netd's asynchronous DNS proxy. These return a negative
 * POSIX code directly (not -1/errno) per the NDK contract. */

int android_res_nquery(net_handle_t network, const char *dname, int ns_class, int ns_type,
                       uint32_t flags) {
    (void)network;
    (void)dname;
    (void)ns_class;
    (void)ns_type;
    (void)flags;
    return -EOPNOTSUPP;
}

int android_res_nsend(net_handle_t network, const uint8_t *msg, size_t msglen, uint32_t flags) {
    (void)network;
    (void)msg;
    (void)msglen;
    (void)flags;
    return -EOPNOTSUPP;
}

int android_res_nresult(int fd, int *rcode, uint8_t *answer, size_t anslen) {
    (void)rcode;
    (void)answer;
    (void)anslen;
    /* No descriptor was ever handed out by the two calls above, so any fd
     * arriving here did not come from this resolver. Do not close it. */
    (void)fd;
    return -EBADF;
}

void android_res_cancel(int nsend_fd) {
    /* Same reasoning: nothing to cancel, and the descriptor is not ours to close. */
    (void)nsend_fd;
}
