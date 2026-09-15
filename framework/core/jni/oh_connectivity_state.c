/*
 * Source-owned OpenHarmony network-state bridge for the Android connectivity facade.
 *
 * This intentionally exposes observations, not policy: Java maps the OH default
 * network, capabilities, bearer, interface, and MTU into Android value objects.
 * A failed/unauthorized OH query is retained in the snapshot and never becomes a
 * fabricated connected network.
 */
#include <jni.h>
#include <network/netmanager/net_connection.h>

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <ifaddrs.h>
/* The OH SDK's generated linux/socket.h redefines libc sockaddr_storage. */
#define sockaddr_storage kernel_sockaddr_storage
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#undef sockaddr_storage
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* Distinct from the OH boolean 0/1 values and retained in the wire snapshot. */
#define HAS_DEFAULT_KERNEL_ROUTE 2
#define HAS_DEFAULT_OH_UNCACHED 3
/* Both the OH enumeration and the local route probe confirmed disconnection. */
#define HAS_DEFAULT_CONFIRMED_ABSENT 4
#define PROXY_EXCLUSION_WIRE_CAPACITY \
    (NETCONN_MAX_EXCLUSION_SIZE * NETCONN_MAX_STR_LEN + 1)
#define SNAPSHOT_WIRE_CAPACITY (PROXY_EXCLUSION_WIRE_CAPACITY + 16384)

static int same_address(const struct sockaddr *left, const struct sockaddr *right)
{
    if (left == NULL || right == NULL || left->sa_family != right->sa_family) {
        return 0;
    }
    if (left->sa_family == AF_INET) {
        return memcmp(&((const struct sockaddr_in *)left)->sin_addr,
                &((const struct sockaddr_in *)right)->sin_addr,
                sizeof(struct in_addr)) == 0;
    }
    if (left->sa_family == AF_INET6) {
        return memcmp(&((const struct sockaddr_in6 *)left)->sin6_addr,
                &((const struct sockaddr_in6 *)right)->sin6_addr,
                sizeof(struct in6_addr)) == 0;
    }
    return 0;
}

static uint32_t bearer_for_interface(const char *iface)
{
    if (strncmp(iface, "wlan", 4) == 0 || strncmp(iface, "wifi", 4) == 0) {
        return UINT32_C(1) << NETCONN_BEARER_WIFI;
    }
    if (strncmp(iface, "rmnet", 5) == 0 || strncmp(iface, "ccmni", 5) == 0 ||
            strncmp(iface, "sipa", 4) == 0) {
        return UINT32_C(1) << NETCONN_BEARER_CELLULAR;
    }
    if (strncmp(iface, "eth", 3) == 0 || strncmp(iface, "en", 2) == 0) {
        return UINT32_C(1) << NETCONN_BEARER_ETHERNET;
    }
    if (strncmp(iface, "tun", 3) == 0 || strncmp(iface, "vpn", 3) == 0 ||
            strncmp(iface, "ppp", 3) == 0) {
        return UINT32_C(1) << NETCONN_BEARER_VPN;
    }
    return 0;
}

/*
 * Network is an Android value object and still needs a stable positive handle
 * when an OH libc does not expose interface indices to this application. Keep
 * that last-resort identity tied to the observed interface name; it is not
 * presented as an OH NetId and is only used by the kernel-route fallback.
 */
static uint32_t interface_facade_id(const char *iface)
{
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char *cursor = (const unsigned char *)iface; *cursor != '\0'; ++cursor) {
        hash = (hash ^ *cursor) * UINT32_C(16777619);
    }
    return hash % UINT32_C(65534) + UINT32_C(1);
}

static int prefix_from_netmask(const struct sockaddr *mask, int family)
{
    if (mask == NULL || mask->sa_family != family) {
        return -1;
    }
    const uint8_t *bytes = family == AF_INET
            ? (const uint8_t *)&((const struct sockaddr_in *)mask)->sin_addr
            : (const uint8_t *)&((const struct sockaddr_in6 *)mask)->sin6_addr;
    size_t length = family == AF_INET ? sizeof(struct in_addr) : sizeof(struct in6_addr);
    int prefix = 0;
    int zero_seen = 0;
    for (size_t i = 0; i < length; ++i) {
        for (int bit = 7; bit >= 0; --bit) {
            if ((bytes[i] & (UINT8_C(1) << bit)) != 0) {
                if (zero_seen) {
                    return -1;
                }
                ++prefix;
            } else {
                zero_seen = 1;
            }
        }
    }
    return prefix;
}

static void ipv4_proc_bytes(uint32_t value, uint8_t bytes[4])
{
    for (int i = 0; i < 4; ++i) {
        bytes[i] = (uint8_t)(value >> (i * 8));
    }
}

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int ipv6_proc_bytes(const char *value, uint8_t bytes[16])
{
    if (strlen(value) != 32) return -1;
    for (int i = 0; i < 16; ++i) {
        int high = hex_nibble(value[i * 2]);
        int low = hex_nibble(value[i * 2 + 1]);
        if (high < 0 || low < 0) return -1;
        bytes[i] = (uint8_t)((high << 4) | low);
    }
    return 0;
}

static int all_zero(const uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] != 0) return 0;
    }
    return 1;
}

static int self_network_info_permission(void)
{
    static int cached = -2;
    if (cached != -2) return cached;
    void *library = dlopen("libability_access_control.so", RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) return cached = -1;
    bool (*check)(const char *) = (bool (*)(const char *))dlsym(
            library, "OH_AT_CheckSelfPermission");
    cached = check == NULL ? -1
            : (check("ohos.permission.GET_NETWORK_INFO") ? 1 : 0);
    dlclose(library);
    return cached;
}

/* RTM_GETROUTE is a kernel observation, not a Network Kit service grant. */
static int kernel_netlink_routes(NetConn_ConnectionProperties *props,
        unsigned int selected_index, int *phase)
{
    *phase = 0;
    if (selected_index == 0) return -ENODEV;
    *phase = 1;
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) return -errno;
    // A normal HAP can create this socket, but explicit netlink bind is
    // SELinux-denied on the board. The kernel assigns a port on first send.
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    *phase = 2;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        int error = errno;
        close(fd);
        return -error;
    }
    struct {
        struct nlmsghdr header;
        struct rtmsg message;
    } request;
    memset(&request, 0, sizeof(request));
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(request.message));
    request.header.nlmsg_type = RTM_GETROUTE;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.header.nlmsg_seq = 1;
    request.message.rtm_family = AF_UNSPEC;
    struct sockaddr_nl kernel;
    memset(&kernel, 0, sizeof(kernel));
    kernel.nl_family = AF_NETLINK;
    *phase = 3;
    if (sendto(fd, &request, request.header.nlmsg_len, 0,
            (const struct sockaddr *)&kernel, sizeof(kernel)) < 0) {
        int error = errno;
        close(fd);
        return -error;
    }
    *phase = 4;
    NetConn_Route *observed = calloc(NETCONN_MAX_ROUTE_SIZE, sizeof(*observed));
    char *buffer = calloc(1, 65536);
    if (observed == NULL || buffer == NULL) {
        free(buffer);
        free(observed);
        close(fd);
        return -ENOMEM;
    }
    int count = 0;
    int result = -ETIMEDOUT;
    for (int round = 0; round < 64; ++round) {
        *phase = 5;
        struct sockaddr_nl sender;
        memset(&sender, 0, sizeof(sender));
        struct iovec vector = {.iov_base = buffer, .iov_len = 65536};
        struct msghdr reply;
        memset(&reply, 0, sizeof(reply));
        reply.msg_name = &sender;
        reply.msg_namelen = sizeof(sender);
        reply.msg_iov = &vector;
        reply.msg_iovlen = 1;
        ssize_t received = recvmsg(fd, &reply, 0);
        if (received < 0) {
            result = -errno;
            break;
        }
        if (received == 0 || (reply.msg_flags & MSG_TRUNC) != 0 || sender.nl_pid != 0) {
            result = -EBADMSG;
            break;
        }
        unsigned int remaining = (unsigned int)received;
        for (struct nlmsghdr *header = (struct nlmsghdr *)buffer;
                remaining >= sizeof(*header) &&
                header->nlmsg_len >= sizeof(*header) &&
                NLMSG_ALIGN(header->nlmsg_len) <= remaining;
                header = NLMSG_NEXT(header, remaining)) {
            if (header->nlmsg_seq != request.header.nlmsg_seq) continue;
            if (header->nlmsg_type == NLMSG_DONE) {
                result = count;
                goto finished;
            }
            if (header->nlmsg_type == NLMSG_ERROR) {
                if (NLMSG_PAYLOAD(header, 0) < sizeof(struct nlmsgerr)) {
                    result = -EBADMSG;
                } else {
                    const struct nlmsgerr *error = (const struct nlmsgerr *)NLMSG_DATA(header);
                    result = error->error == 0 ? count : error->error;
                }
                goto finished;
            }
            if (header->nlmsg_type != RTM_NEWROUTE ||
                    NLMSG_PAYLOAD(header, 0) < sizeof(struct rtmsg)) continue;
            struct rtmsg *route = (struct rtmsg *)NLMSG_DATA(header);
            int family = route->rtm_family;
            if ((family != AF_INET && family != AF_INET6) ||
                    route->rtm_table != RT_TABLE_MAIN || route->rtm_type != RTN_UNICAST ||
                    route->rtm_dst_len > (family == AF_INET ? 32 : 128)) continue;
            size_t address_size = family == AF_INET ? 4 : 16;
            uint8_t destination[16] = {0}, gateway[16] = {0};
            unsigned int output_index = 0;
            int has_gateway = 0, malformed = 0;
            int attributes_left = RTM_PAYLOAD(header);
            for (struct rtattr *attribute = RTM_RTA(route);
                    RTA_OK(attribute, attributes_left);
                    attribute = RTA_NEXT(attribute, attributes_left)) {
                if (attribute->rta_type == RTA_OIF) {
                    if (RTA_PAYLOAD(attribute) < sizeof(output_index)) malformed = 1;
                    else memcpy(&output_index, RTA_DATA(attribute), sizeof(output_index));
                } else if (attribute->rta_type == RTA_DST) {
                    if (RTA_PAYLOAD(attribute) < address_size) malformed = 1;
                    else memcpy(destination, RTA_DATA(attribute), address_size);
                } else if (attribute->rta_type == RTA_GATEWAY) {
                    if (RTA_PAYLOAD(attribute) < address_size) malformed = 1;
                    else {
                        memcpy(gateway, RTA_DATA(attribute), address_size);
                        has_gateway = !all_zero(gateway, address_size);
                    }
                }
            }
            if (malformed || output_index != selected_index ||
                    (family == AF_INET6 && destination[0] == 0xff)) continue;
            if (count >= NETCONN_MAX_ROUTE_SIZE) {
                result = -E2BIG;
                goto finished;
            }
            NetConn_Route *entry = &observed[count];
            entry->destination.family = (uint8_t)family;
            entry->destination.prefixlen = route->rtm_dst_len;
            if (inet_ntop(family, destination, entry->destination.address,
                    sizeof(entry->destination.address)) == NULL) continue;
            if (has_gateway) {
                entry->gateway.family = (uint8_t)family;
                if (inet_ntop(family, gateway, entry->gateway.address,
                        sizeof(entry->gateway.address)) == NULL) continue;
                entry->hasGateway = 1;
            }
            entry->isDefaultRoute = route->rtm_dst_len == 0 &&
                    all_zero(destination, address_size);
            strncpy(entry->iface, props->ifaceName, sizeof(entry->iface) - 1);
            ++count;
        }
        if (remaining != 0) {
            result = -EBADMSG;
            break;
        }
    }
finished:
    *phase = 7;
    if (result >= 0) {
        memcpy(props->routeList, observed, (size_t)count * sizeof(*observed));
        props->routeListSize = count;
    }
    free(buffer);
    free(observed);
    close(fd);
    return result;
}

/* Only enumerate kernel-installed routes for the observed default interface. */
static void kernel_ipv4_routes(NetConn_ConnectionProperties *props)
{
    static int permission_denied;
    if (permission_denied) return;
    FILE *routes = fopen("/proc/net/route", "r");
    if (routes == NULL) {
        if (errno == EACCES || errno == EPERM) permission_denied = 1;
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), routes) != NULL &&
            props->routeListSize < NETCONN_MAX_ROUTE_SIZE) {
        char iface[IFNAMSIZ] = {0};
        unsigned int destination = 0, gateway = 0, flags = 0, mask = 0;
        if (sscanf(line, " %15s %8x %8x %x %*u %*u %*u %8x",
                iface, &destination, &gateway, &flags, &mask) != 5 ||
                strcmp(iface, props->ifaceName) != 0 ||
                (flags & RTF_UP) == 0 || (flags & RTF_LOCAL) != 0) {
            continue;
        }
        uint8_t destination_bytes[4], gateway_bytes[4], mask_bytes[4];
        ipv4_proc_bytes(destination, destination_bytes);
        ipv4_proc_bytes(gateway, gateway_bytes);
        ipv4_proc_bytes(mask, mask_bytes);
        struct sockaddr_in netmask;
        memset(&netmask, 0, sizeof(netmask));
        netmask.sin_family = AF_INET;
        memcpy(&netmask.sin_addr, mask_bytes, sizeof(mask_bytes));
        int prefix = prefix_from_netmask((const struct sockaddr *)&netmask, AF_INET);
        if (prefix < 0 || ((flags & RTF_GATEWAY) != 0 && gateway == 0)) continue;
        NetConn_Route *route = &props->routeList[props->routeListSize];
        memset(route, 0, sizeof(*route));
        route->destination.family = AF_INET;
        route->destination.prefixlen = (uint8_t)prefix;
        if (inet_ntop(AF_INET, destination_bytes, route->destination.address,
                sizeof(route->destination.address)) == NULL) continue;
        if ((flags & RTF_GATEWAY) != 0) {
            route->gateway.family = AF_INET;
            if (inet_ntop(AF_INET, gateway_bytes, route->gateway.address,
                    sizeof(route->gateway.address)) == NULL) continue;
            route->hasGateway = 1;
        }
        route->isDefaultRoute = destination == 0 && prefix == 0;
        strncpy(route->iface, props->ifaceName, sizeof(route->iface) - 1);
        ++props->routeListSize;
    }
    if (ferror(routes) && (errno == EACCES || errno == EPERM)) permission_denied = 1;
    fclose(routes);
}

static void kernel_ipv6_routes(NetConn_ConnectionProperties *props)
{
    static int permission_denied;
    if (permission_denied) return;
    FILE *routes = fopen("/proc/net/ipv6_route", "r");
    if (routes == NULL) {
        if (errno == EACCES || errno == EPERM) permission_denied = 1;
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), routes) != NULL &&
            props->routeListSize < NETCONN_MAX_ROUTE_SIZE) {
        char destination_hex[33] = {0}, source_hex[33] = {0};
        char gateway_hex[33] = {0}, iface[IFNAMSIZ] = {0};
        unsigned int prefix = 0, source_prefix = 0, metric = 0;
        unsigned int refcnt = 0, use = 0, flags = 0;
        if (sscanf(line, " %32s %2x %32s %2x %32s %8x %8x %8x %8x %15s",
                destination_hex, &prefix, source_hex, &source_prefix,
                gateway_hex, &metric, &refcnt, &use, &flags, iface) != 10 ||
                strcmp(iface, props->ifaceName) != 0 || prefix > 128 ||
                (flags & RTF_UP) == 0 || (flags & RTF_LOCAL) != 0) {
            continue;
        }
        (void)source_prefix;
        (void)metric;
        (void)refcnt;
        (void)use;
        uint8_t destination_bytes[16], gateway_bytes[16];
        if (ipv6_proc_bytes(destination_hex, destination_bytes) != 0 ||
                ipv6_proc_bytes(source_hex, gateway_bytes) != 0 ||
                ipv6_proc_bytes(gateway_hex, gateway_bytes) != 0 ||
                destination_bytes[0] == 0xff) {
            continue; // Multicast membership is not an Android IP route.
        }
        NetConn_Route *route = &props->routeList[props->routeListSize];
        memset(route, 0, sizeof(*route));
        route->destination.family = AF_INET6;
        route->destination.prefixlen = (uint8_t)prefix;
        if (inet_ntop(AF_INET6, destination_bytes, route->destination.address,
                sizeof(route->destination.address)) == NULL) continue;
        if (!all_zero(gateway_bytes, sizeof(gateway_bytes))) {
            route->gateway.family = AF_INET6;
            if (inet_ntop(AF_INET6, gateway_bytes, route->gateway.address,
                    sizeof(route->gateway.address)) == NULL) continue;
            route->hasGateway = 1;
        }
        route->isDefaultRoute = all_zero(destination_bytes, sizeof(destination_bytes)) &&
                prefix == 0;
        strncpy(route->iface, props->ifaceName, sizeof(route->iface) - 1);
        ++props->routeListSize;
    }
    if (ferror(routes) && (errno == EACCES || errno == EPERM)) permission_denied = 1;
    fclose(routes);
}

/*
 * Recover observable process routing state when an OH service transaction is
 * permission/version-gated. Connecting a datagram socket performs only a local
 * route lookup; no packet is sent. The destination comes from OH's configured
 * resolver rather than a bridge constant.
 */
static int kernel_route_snapshot(NetConn_NetHandle *handle,
        NetConn_NetCapabilities *caps, NetConn_ConnectionProperties *props)
{
    FILE *resolver = fopen("/etc/resolv.conf", "r");
    if (resolver == NULL) {
        return errno != 0 ? -errno : -1;
    }
    char keyword[64];
    char address[INET6_ADDRSTRLEN];
    char selected_resolver[INET6_ADDRSTRLEN] = {0};
    char line[512];
    int family = AF_UNSPEC;
    while (fgets(line, sizeof(line), resolver) != NULL) {
        if (sscanf(line, " %63s %45s", keyword, address) != 2 ||
                strcmp(keyword, "nameserver") != 0) {
            continue;
        }
        struct in_addr v4;
        struct in6_addr v6;
        int parsed_family = inet_pton(AF_INET, address, &v4) == 1 ? AF_INET
                : (inet_pton(AF_INET6, address, &v6) == 1 ? AF_INET6 : AF_UNSPEC);
        if (parsed_family == AF_UNSPEC) {
            continue;
        }
        if (family == AF_UNSPEC) {
            family = parsed_family;
            strncpy(selected_resolver, address, sizeof(selected_resolver) - 1);
        }
        if (props->dnsListSize < NETCONN_MAX_ADDR_SIZE) {
            NetConn_NetAddr *dns = &props->dnsList[props->dnsListSize++];
            dns->family = (uint8_t)parsed_family;
            strncpy(dns->address, address, sizeof(dns->address) - 1);
        }
    }
    fclose(resolver);
    if (family == AF_UNSPEC) {
        return -ENOENT;
    }

    int fd = socket(family, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -errno;
    }
    struct sockaddr_storage destination;
    memset(&destination, 0, sizeof(destination));
    socklen_t destination_length;
    if (family == AF_INET) {
        struct sockaddr_in *v4 = (struct sockaddr_in *)&destination;
        v4->sin_family = AF_INET;
        v4->sin_port = htons(53);
        (void)inet_pton(AF_INET, selected_resolver, &v4->sin_addr);
        destination_length = sizeof(*v4);
    } else {
        struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)&destination;
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(53);
        (void)inet_pton(AF_INET6, selected_resolver, &v6->sin6_addr);
        destination_length = sizeof(*v6);
    }
    if (connect(fd, (struct sockaddr *)&destination, destination_length) != 0) {
        int saved = errno;
        close(fd);
        return saved != 0 ? -saved : -1;
    }
    struct sockaddr_storage local;
    socklen_t local_length = sizeof(local);
    memset(&local, 0, sizeof(local));
    if (getsockname(fd, (struct sockaddr *)&local, &local_length) != 0) {
        int saved = errno;
        close(fd);
        return saved != 0 ? -saved : -1;
    }
    uint32_t mark = 0;
    socklen_t mark_length = sizeof(mark);
    if (getsockopt(fd, SOL_SOCKET, SO_MARK, &mark, &mark_length) != 0) {
        mark = 0;
    }
    close(fd);

    struct ifaddrs *interfaces = NULL;
    if (getifaddrs(&interfaces) != 0) {
        return errno != 0 ? -errno : -1;
    }
    const char *selected = NULL;
    for (struct ifaddrs *item = interfaces; item != NULL; item = item->ifa_next) {
        if (item->ifa_addr != NULL && (item->ifa_flags & IFF_UP) != 0 &&
                (item->ifa_flags & IFF_LOOPBACK) == 0 &&
                same_address(item->ifa_addr, (struct sockaddr *)&local)) {
            selected = item->ifa_name;
            break;
        }
    }
    if (selected == NULL) {
        freeifaddrs(interfaces);
        return -ENODEV;
    }
    strncpy(props->ifaceName, selected, sizeof(props->ifaceName) - 1);
    uint32_t index = if_nametoindex(selected);
    uint32_t net_id = mark & UINT32_C(0xffff);
    handle->netId = (int32_t)(net_id != 0 ? net_id : index);
    uint32_t bearer = bearer_for_interface(selected);
    caps->bearerTypesSize = 0;
    for (int32_t value = 0; value <= NETCONN_BEARER_VPN; ++value) {
        if ((bearer & (UINT32_C(1) << value)) != 0) {
            caps->bearerTypes[caps->bearerTypesSize++] = (NetConn_NetBearerType)value;
        }
    }
    caps->netCaps[0] = NETCONN_NET_CAPABILITY_INTERNET;
    caps->netCapsSize = 1;
    if ((bearer & (UINT32_C(1) << NETCONN_BEARER_VPN)) == 0) {
        caps->netCaps[caps->netCapsSize++] = NETCONN_NET_CAPABILITY_NOT_VPN;
    }
    // Route lookup selects the interface; route enumeration below reports only
    // installed destinations and gateways, not an invented internet default.
    for (struct ifaddrs *item = interfaces; item != NULL; item = item->ifa_next) {
        if (item->ifa_addr != NULL && strcmp(item->ifa_name, selected) == 0 &&
                (item->ifa_addr->sa_family == AF_INET || item->ifa_addr->sa_family == AF_INET6)
                && props->netAddrListSize < NETCONN_MAX_ADDR_SIZE) {
            int address_family = item->ifa_addr->sa_family;
            int prefix = prefix_from_netmask(item->ifa_netmask, address_family);
            if (prefix < 0) {
                continue;
            }
            const void *raw = address_family == AF_INET
                    ? (const void *)&((const struct sockaddr_in *)item->ifa_addr)->sin_addr
                    : (const void *)&((const struct sockaddr_in6 *)item->ifa_addr)->sin6_addr;
            NetConn_NetAddr *link = &props->netAddrList[props->netAddrListSize];
            if (inet_ntop(address_family, raw, link->address, sizeof(link->address)) != NULL) {
                link->family = (uint8_t)address_family;
                link->prefixlen = (uint8_t)prefix;
                ++props->netAddrListSize;
            }
        }
    }
    freeifaddrs(interfaces);
    int control = socket(AF_INET, SOCK_DGRAM, 0);
    if (control >= 0) {
        struct ifreq index_request;
        memset(&index_request, 0, sizeof(index_request));
        strncpy(index_request.ifr_name, props->ifaceName,
                sizeof(index_request.ifr_name) - 1);
        if (index == 0 && ioctl(control, SIOCGIFINDEX, &index_request) == 0 &&
                index_request.ifr_ifindex > 0) {
            index = (uint32_t)index_request.ifr_ifindex;
        }

        struct ifreq mtu_request;
        memset(&mtu_request, 0, sizeof(mtu_request));
        strncpy(mtu_request.ifr_name, props->ifaceName, sizeof(mtu_request.ifr_name) - 1);
        if (ioctl(control, SIOCGIFMTU, &mtu_request) == 0 && mtu_request.ifr_mtu > 0 &&
                mtu_request.ifr_mtu <= UINT16_MAX) {
            props->mtu = (uint16_t)mtu_request.ifr_mtu;
        }
        close(control);
    }
    if (handle->netId <= 0) {
        handle->netId = (int32_t)(index != 0 ? index : interface_facade_id(props->ifaceName));
    }
    int netlink_phase = 0;
    int netlink_routes = kernel_netlink_routes(props, index, &netlink_phase);
    if (netlink_routes <= 0) {
        kernel_ipv4_routes(props);
        kernel_ipv6_routes(props);
    }
    static char last_route_log[128];
    char route_log[128];
    int route_log_length = snprintf(route_log, sizeof(route_log),
            "iface=%s index=%u netlink=%d phase=%d routes=%d uid=%u", props->ifaceName,
            index, netlink_routes, netlink_phase, props->routeListSize,
            (unsigned)getuid());
    if (route_log_length > 0 && (size_t)route_log_length < sizeof(route_log) &&
            strcmp(last_route_log, route_log) != 0) {
        strncpy(last_route_log, route_log, sizeof(last_route_log) - 1);
        fprintf(stderr, "[OhConnectivityState] kernel route enumeration %s\n", route_log);
    }
    return handle->netId > 0 ? 0 : -ENODEV;
}

static uint32_t capability_mask(const NetConn_NetCapabilities *caps)
{
    uint32_t mask = 0;
    int32_t size = caps->netCapsSize;
    if (size < 0 || size > NETCONN_MAX_CAP_SIZE) {
        return 0;
    }
    for (int32_t i = 0; i < size; ++i) {
        int32_t value = (int32_t)caps->netCaps[i];
        if (value >= 0 && value < 32) {
            mask |= UINT32_C(1) << value;
        }
    }
    return mask;
}

static uint32_t bearer_mask(const NetConn_NetCapabilities *caps)
{
    uint32_t mask = 0;
    int32_t size = caps->bearerTypesSize;
    if (size < 0 || size > NETCONN_MAX_BEARER_TYPE_SIZE) {
        return 0;
    }
    for (int32_t i = 0; i < size; ++i) {
        int32_t value = (int32_t)caps->bearerTypes[i];
        if (value >= 0 && value < 32) {
            mask |= UINT32_C(1) << value;
        }
    }
    return mask;
}

static int numeric_address_wire(char *output, size_t capacity,
        const NetConn_NetAddr *addresses, int32_t count, int with_prefix)
{
    if (count < 0 || count > NETCONN_MAX_ADDR_SIZE || capacity == 0) {
        return -1;
    }
    size_t used = 0;
    int emitted = 0;
    output[0] = '\0';
    for (int32_t i = 0; i < count; ++i) {
        const NetConn_NetAddr *entry = &addresses[i];
        int family = entry->family;
        size_t length = strnlen(entry->address, sizeof(entry->address));
        if ((family != AF_INET && family != AF_INET6) || length == sizeof(entry->address)) {
            continue;
        }
        struct in6_addr binary;
        char numeric[INET6_ADDRSTRLEN];
        if (inet_pton(family, entry->address, &binary) != 1 ||
                inet_ntop(family, &binary, numeric, sizeof(numeric)) == NULL) {
            continue;
        }
        if (with_prefix && entry->prefixlen > (family == AF_INET ? 32 : 128)) {
            continue;
        }
        int added = with_prefix
                ? snprintf(output + used, capacity - used, "%s%s/%u",
                        emitted == 0 ? "" : ";", numeric, (unsigned)entry->prefixlen)
                : snprintf(output + used, capacity - used, "%s%s",
                        emitted == 0 ? "" : ";", numeric);
        if (added < 0 || (size_t)added >= capacity - used) {
            return -1;
        }
        used += (size_t)added;
        ++emitted;
    }
    return emitted;
}

static int numeric_route_wire(char *output, size_t capacity,
        const NetConn_ConnectionProperties *props)
{
    int32_t count = props->routeListSize;
    if (count < 0 || count > NETCONN_MAX_ROUTE_SIZE || capacity == 0) return -1;
    size_t used = 0;
    int emitted = 0;
    output[0] = '\0';
    for (int32_t i = 0; i < count; ++i) {
        const NetConn_Route *route = &props->routeList[i];
        int family = route->destination.family;
        if ((family != AF_INET && family != AF_INET6) ||
                (route->iface[0] != '\0' &&
                 strncmp(route->iface, props->ifaceName, sizeof(route->iface)) != 0) ||
                route->destination.prefixlen > (family == AF_INET ? 32 : 128)) {
            continue;
        }
        struct in6_addr binary;
        char destination[INET6_ADDRSTRLEN], gateway[INET6_ADDRSTRLEN] = {0};
        if (strnlen(route->destination.address,
                sizeof(route->destination.address)) == sizeof(route->destination.address) ||
                inet_pton(family, route->destination.address, &binary) != 1 ||
                inet_ntop(family, &binary, destination, sizeof(destination)) == NULL) {
            continue;
        }
        if (route->hasGateway != 0) {
            if (route->gateway.family != family ||
                    strnlen(route->gateway.address,
                            sizeof(route->gateway.address)) == sizeof(route->gateway.address) ||
                    inet_pton(family, route->gateway.address, &binary) != 1 ||
                    inet_ntop(family, &binary, gateway, sizeof(gateway)) == NULL) {
                continue;
            }
        }
        int added = snprintf(output + used, capacity - used, "%s%s/%u%s%s",
                emitted == 0 ? "" : ";", destination,
                (unsigned)route->destination.prefixlen,
                gateway[0] == '\0' ? "" : "@", gateway);
        if (added < 0 || (size_t)added >= capacity - used) return -1;
        used += (size_t)added;
        ++emitted;
    }
    return emitted;
}

/* Keep OH proxy strings unambiguous in the versioned Java wire format. */
static int valid_proxy_token(const char *value, size_t capacity)
{
    size_t length = strnlen(value, capacity);
    if (length == 0 || length == capacity) return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char character = (unsigned char)value[i];
        if (character <= ' ' || character == 0x7f || character == '|' ||
                character == ';' || character == ',') return 0;
    }
    return 1;
}

static int proxy_exclusion_wire(char *output, size_t capacity,
        const NetConn_HttpProxy *proxy)
{
    int32_t count = proxy->exclusionListSize;
    if (count < 0 || count > NETCONN_MAX_EXCLUSION_SIZE || capacity == 0) return -1;
    size_t used = 0;
    output[0] = '\0';
    for (int32_t i = 0; i < count; ++i) {
        const char *entry = proxy->exclusionList[i];
        if (!valid_proxy_token(entry, sizeof(proxy->exclusionList[i]))) return -1;
        int added = snprintf(output + used, capacity - used, "%s%s",
                i == 0 ? "" : ";", entry);
        if (added < 0 || (size_t)added >= capacity - used) return -1;
        used += (size_t)added;
    }
    return count;
}

static int same_numeric_netaddr(const NetConn_NetAddr *left,
        const NetConn_NetAddr *right, int with_prefix)
{
    if (left->family != right->family ||
            (left->family != AF_INET && left->family != AF_INET6) ||
            (with_prefix && left->prefixlen != right->prefixlen)) return 0;
    struct in6_addr left_binary, right_binary;
    if (strnlen(left->address, sizeof(left->address)) == sizeof(left->address) ||
            strnlen(right->address, sizeof(right->address)) == sizeof(right->address) ||
            inet_pton(left->family, left->address, &left_binary) != 1 ||
            inet_pton(right->family, right->address, &right_binary) != 1) return 0;
    return memcmp(&left_binary, &right_binary,
            left->family == AF_INET ? sizeof(struct in_addr) : sizeof(struct in6_addr)) == 0;
}

static void merge_observed_addresses(NetConn_NetAddr *destination, int32_t *destination_count,
        const NetConn_NetAddr *observed, int32_t observed_count, int with_prefix)
{
    if (*destination_count < 0 || *destination_count > NETCONN_MAX_ADDR_SIZE ||
            observed_count < 0 || observed_count > NETCONN_MAX_ADDR_SIZE) return;
    for (int32_t i = 0; i < observed_count && *destination_count < NETCONN_MAX_ADDR_SIZE; ++i) {
        int duplicate = 0;
        for (int32_t j = 0; j < *destination_count; ++j) {
            if (same_numeric_netaddr(&destination[j], &observed[i], with_prefix)) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate) destination[(*destination_count)++] = observed[i];
    }
}

static int same_numeric_route(const NetConn_Route *left, const NetConn_Route *right)
{
    if (!same_numeric_netaddr(&left->destination, &right->destination, 1) ||
            (left->hasGateway != 0) != (right->hasGateway != 0) ||
            (left->hasGateway != 0 &&
             !same_numeric_netaddr(&left->gateway, &right->gateway, 0))) return 0;
    return left->iface[0] == '\0' || right->iface[0] == '\0' ||
            strncmp(left->iface, right->iface, sizeof(left->iface)) == 0;
}

static void merge_observed_routes(NetConn_ConnectionProperties *destination,
        const NetConn_ConnectionProperties *observed)
{
    if (destination->routeListSize < 0 ||
            destination->routeListSize > NETCONN_MAX_ROUTE_SIZE ||
            observed->routeListSize < 0 ||
            observed->routeListSize > NETCONN_MAX_ROUTE_SIZE) return;
    for (int32_t i = 0; i < observed->routeListSize &&
            destination->routeListSize < NETCONN_MAX_ROUTE_SIZE; ++i) {
        int duplicate = 0;
        for (int32_t j = 0; j < destination->routeListSize; ++j) {
            if (same_numeric_route(&destination->routeList[j], &observed->routeList[i])) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate) destination->routeList[destination->routeListSize++] =
                observed->routeList[i];
    }
}

/* The inspected OH service source caches permission results for its default-net
 * calls, but checks the same permission without that cache for
 * GetAllNets/properties/capabilities. The deployed service may differ.
 * Promote only a real OH handle whose reported interface matches the kernel's
 * currently routed interface; supplement incomplete OH link lists with observed
 * kernel addresses, resolvers, and installed routes. */
static int uncached_oh_default(NetConn_NetHandle *handle, NetConn_NetCapabilities *caps,
        NetConn_ConnectionProperties *props, int32_t *caps_rc, int32_t *props_rc)
{
    NetConn_NetHandleList all_nets;
    memset(&all_nets, 0, sizeof(all_nets));
    int32_t list_rc = OH_NetConn_GetAllNets(&all_nets);
    int32_t count = list_rc == 0 && all_nets.netHandleListSize >= 0 &&
            all_nets.netHandleListSize <= NETCONN_MAX_NET_SIZE
            ? all_nets.netHandleListSize : 0;
    int promoted = 0, matches = 0, oh_addresses = 0, oh_dns = 0, oh_routes = 0;
    NetConn_ConnectionProperties *detail = count > 0 ? calloc(1, sizeof(*detail)) : NULL;
    NetConn_ConnectionProperties *candidate = count > 0 ? calloc(1, sizeof(*candidate)) : NULL;
    NetConn_NetHandle candidate_handle;
    NetConn_NetCapabilities candidate_caps;
    memset(&candidate_handle, 0, sizeof(candidate_handle));
    memset(&candidate_caps, 0, sizeof(candidate_caps));
    if (detail != NULL && candidate != NULL) {
        for (int32_t i = 0; i < count; ++i) {
            NetConn_NetHandle listed = all_nets.netHandles[i];
            if (listed.netId <= 0) continue;
            memset(detail, 0, sizeof(*detail));
            int32_t detail_rc = OH_NetConn_GetConnectionProperties(&listed, detail);
            if (detail_rc != 0 ||
                    strnlen(detail->ifaceName, sizeof(detail->ifaceName)) ==
                            sizeof(detail->ifaceName) ||
                    strcmp(detail->ifaceName, props->ifaceName) != 0 ||
                    detail->netAddrListSize < 0 ||
                    detail->netAddrListSize > NETCONN_MAX_ADDR_SIZE ||
                    detail->dnsListSize < 0 ||
                    detail->dnsListSize > NETCONN_MAX_ADDR_SIZE ||
                    detail->routeListSize < 0 ||
                    detail->routeListSize > NETCONN_MAX_ROUTE_SIZE) continue;
            NetConn_NetCapabilities listed_caps;
            memset(&listed_caps, 0, sizeof(listed_caps));
            int32_t listed_caps_rc = OH_NetConn_GetNetCapabilities(&listed, &listed_caps);
            if (listed_caps_rc != 0 || listed_caps.netCapsSize <= 0 ||
                    listed_caps.netCapsSize > NETCONN_MAX_CAP_SIZE ||
                    listed_caps.bearerTypesSize < 0 ||
                    listed_caps.bearerTypesSize > NETCONN_MAX_BEARER_TYPE_SIZE) continue;
            if (++matches > 1) break;
            oh_addresses = detail->netAddrListSize;
            oh_dns = detail->dnsListSize;
            oh_routes = detail->routeListSize;
            memcpy(candidate, detail, sizeof(*candidate));
            candidate_handle = listed;
            candidate_caps = listed_caps;
        }
        if (matches == 1) {
            if (candidate->mtu == 0) candidate->mtu = props->mtu;
            merge_observed_addresses(candidate->netAddrList, &candidate->netAddrListSize,
                    props->netAddrList, props->netAddrListSize, 1);
            merge_observed_addresses(candidate->dnsList, &candidate->dnsListSize,
                    props->dnsList, props->dnsListSize, 0);
            merge_observed_routes(candidate, props);
            memcpy(props, candidate, sizeof(*props));
            *handle = candidate_handle;
            *caps = candidate_caps;
            *caps_rc = 0;
            *props_rc = 0;
            promoted = 1;
        }
    }
    free(candidate);
    free(detail);
    static char last_log[512];
    char current_log[512];
    int log_length = snprintf(current_log, sizeof(current_log),
            "list_rc=%d count=%d matches=%d promoted=%d netId=%d iface=%s oh_counts=%d/%d/%d merged_counts=%d/%d/%d caps=0x%x uid=%u self_network_info=%d",
            list_rc, count, matches, promoted, handle->netId, props->ifaceName,
            oh_addresses, oh_dns, oh_routes, props->netAddrListSize,
            props->dnsListSize, props->routeListSize,
            promoted ? capability_mask(caps) : 0, (unsigned)getuid(),
            self_network_info_permission());
    if (log_length > 0 && (size_t)log_length < sizeof(current_log) &&
            strcmp(last_log, current_log) != 0) {
        strncpy(last_log, current_log, sizeof(last_log) - 1);
        fprintf(stderr, "[OhConnectivityState] OH uncached default reconciliation %s\n",
                current_log);
    }
    return promoted;
}

JNIEXPORT jstring JNICALL
Java_adapter_core_OhConnectivityState_nativeSnapshot(JNIEnv *env, jclass klass)
{
    (void)klass;
    int32_t has_default = 0;
    int32_t metered = 1;
    int32_t has_rc = OH_NetConn_HasDefaultNet(&has_default);
    int32_t default_rc = -1;
    int32_t metered_rc = -1;
    int32_t caps_rc = -1;
    int32_t props_rc = -1;
    NetConn_NetHandle handle;
    NetConn_NetCapabilities caps;
    /* This OH value includes a large proxy exclusion array. Keeping it on a
     * JNI caller's stack can exhaust a small appspawn/preload thread stack. */
    NetConn_ConnectionProperties *props = calloc(1, sizeof(*props));
    if (props == NULL) {
        return NULL;
    }
    memset(&handle, 0, sizeof(handle));
    memset(&caps, 0, sizeof(caps));

    /*
     * Query independently. Some deployed OH service/client combinations do not
     * implement the newer HasDefaultNet transaction but do implement the older
     * GetDefaultNet operation. A successful handle is authoritative on its own;
     * retaining every raw status keeps this compatibility path observable.
     */
    default_rc = OH_NetConn_GetDefaultNet(&handle);
    metered_rc = OH_NetConn_IsDefaultNetMetered(&metered);
    if (default_rc == 0 && handle.netId > 0) {
        caps_rc = OH_NetConn_GetNetCapabilities(&handle, &caps);
        props_rc = OH_NetConn_GetConnectionProperties(&handle, props);
    } else {
        int route_rc = kernel_route_snapshot(&handle, &caps, props);
        if (route_rc == 0) {
            has_default = HAS_DEFAULT_KERNEL_ROUTE;
            if (uncached_oh_default(&handle, &caps, props, &caps_rc, &props_rc)) {
                has_default = HAS_DEFAULT_OH_UNCACHED;
            }
        } else if (route_rc == -ENETUNREACH || route_rc == -EHOSTUNREACH ||
                route_rc == -ENODEV) {
            NetConn_NetHandleList all_nets;
            memset(&all_nets, 0, sizeof(all_nets));
            int32_t list_rc = OH_NetConn_GetAllNets(&all_nets);
            if (list_rc == 0 && all_nets.netHandleListSize == 0) {
                has_default = HAS_DEFAULT_CONFIRMED_ABSENT;
            }
        }
    }
    int32_t proxy_rc = OH_NetConn_GetDefaultHttpProxy(&props->httpProxy);
    static char last_proxy_log[128];
    char proxy_log[128];
    int proxy_log_length = snprintf(proxy_log, sizeof(proxy_log),
            "rc=%d configured=%d port=%u self_network_info=%d uid=%u",
            proxy_rc, props->httpProxy.host[0] != '\0',
            (unsigned)props->httpProxy.port, self_network_info_permission(),
            (unsigned)getuid());
    if (proxy_log_length > 0 && (size_t)proxy_log_length < sizeof(proxy_log) &&
            strcmp(last_proxy_log, proxy_log) != 0) {
        strncpy(last_proxy_log, proxy_log, sizeof(last_proxy_log) - 1);
        fprintf(stderr, "[OhConnectivityState] OH proxy/permission probe %s\n", proxy_log);
    }

    char iface[NETCONN_MAX_STR_LEN];
    memset(iface, 0, sizeof(iface));
    if (props_rc == 0 || has_default == HAS_DEFAULT_KERNEL_ROUTE) {
        size_t limit = strnlen(props->ifaceName, sizeof(props->ifaceName));
        if (limit >= sizeof(iface)) {
            limit = sizeof(iface) - 1;
        }
        memcpy(iface, props->ifaceName, limit);
        for (size_t i = 0; i < limit; ++i) {
            if (iface[i] == '|') {
                iface[i] = '?';
            }
        }
    }

    int usable_fallback = has_default == HAS_DEFAULT_KERNEL_ROUTE;
    uint32_t net_caps = (caps_rc == 0 || usable_fallback) ? capability_mask(&caps) : 0;
    uint32_t bearers = (caps_rc == 0 || usable_fallback) ? bearer_mask(&caps) : 0;
    char link_addresses[2048];
    char dns_servers[2048];
    int link_count = numeric_address_wire(link_addresses, sizeof(link_addresses),
            props->netAddrList, props->netAddrListSize, 1);
    int dns_count = numeric_address_wire(dns_servers, sizeof(dns_servers),
            props->dnsList, props->dnsListSize, 0);
    char *route_wire = calloc(1, 8192);
    char *proxy_exclusions = calloc(1, PROXY_EXCLUSION_WIRE_CAPACITY);
    char *snapshot = calloc(1, SNAPSHOT_WIRE_CAPACITY);
    if (link_count < 0 || dns_count < 0 || route_wire == NULL ||
            proxy_exclusions == NULL || snapshot == NULL) {
        free(route_wire);
        free(proxy_exclusions);
        free(snapshot);
        free(props);
        return NULL;
    }
    int route_count = numeric_route_wire(route_wire, 8192, props);
    if (route_count < 0) {
        free(route_wire);
        free(proxy_exclusions);
        free(snapshot);
        free(props);
        return NULL;
    }
    int proxy_wire_rc = proxy_rc;
    const char *proxy_host = "";
    unsigned proxy_port = 0;
    if (proxy_rc == 0 && props->httpProxy.host[0] != '\0') {
        if (props->httpProxy.port == 0 ||
                !valid_proxy_token(props->httpProxy.host, sizeof(props->httpProxy.host)) ||
                proxy_exclusion_wire(proxy_exclusions, PROXY_EXCLUSION_WIRE_CAPACITY,
                        &props->httpProxy) < 0) {
            proxy_wire_rc = -EINVAL;
            fprintf(stderr, "[OhConnectivityState] ignored invalid OH proxy configuration\n");
        } else {
            proxy_host = props->httpProxy.host;
            proxy_port = props->httpProxy.port;
        }
    }
    int length = snprintf(snapshot, SNAPSHOT_WIRE_CAPACITY,
            "3|%d|%d|%d|%d|%d|%d|%d|%u|%u|%u|%u|%d|%s|%u|%d|%d|%d|%s|%s|%s|%d|%s|%u|%s",
            has_rc, has_default, default_rc, handle.netId,
            metered_rc, metered, caps_rc, net_caps, bearers,
            caps.linkUpBandwidthKbps, caps.linkDownBandwidthKbps,
            props_rc, iface, (unsigned)props->mtu,
            link_count, dns_count, route_count,
            link_addresses, dns_servers, route_wire,
            proxy_wire_rc, proxy_host, proxy_port, proxy_exclusions);
    free(route_wire);
    free(proxy_exclusions);
    free(props);
    if (length < 0 || length >= SNAPSHOT_WIRE_CAPACITY) {
        free(snapshot);
        return NULL;
    }
    jstring result = (*env)->NewStringUTF(env, snapshot);
    free(snapshot);
    return result;
}
