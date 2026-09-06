#ifndef WESTLAKE_OH_NETWORK_INTERFACES_H
#define WESTLAKE_OH_NETWORK_INTERFACES_H
#include <cerrno>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <sys/socket.h>

namespace oh_network {
// OH normal-app policy can deny libc's SIOCGIFINDEX/SIOCGIFNAME ioctls while
// allowing the route-netlink enumeration used by getifaddrs. Use the kernel's
// AF_PACKET interface index in that case; never invent an index or address.
inline unsigned interfaceIndex(const char* name) {
    const unsigned direct = ::if_nametoindex(name);
    if (direct || (errno != EACCES && errno != EPERM)) return direct;
    ifaddrs* interfaces = nullptr;
    if (::getifaddrs(&interfaces) != 0) return 0;
    unsigned result = 0;
    for (const ifaddrs* it = interfaces; it; it = it->ifa_next) {
        if (!it->ifa_name || !it->ifa_addr || it->ifa_addr->sa_family != AF_PACKET ||
            std::strcmp(it->ifa_name, name) != 0) continue;
        const int index = reinterpret_cast<const sockaddr_ll*>(it->ifa_addr)->sll_ifindex;
        if (index > 0) result = static_cast<unsigned>(index);
        break;
    }
    ::freeifaddrs(interfaces);
    if (!result) errno = ENXIO;
    return result;
}

inline char* interfaceName(unsigned index, char name[IF_NAMESIZE]) {
    char* direct = ::if_indextoname(index, name);
    if (direct || (errno != EACCES && errno != EPERM)) return direct;
    ifaddrs* interfaces = nullptr;
    if (::getifaddrs(&interfaces) != 0) return nullptr;
    bool found = false;
    for (const ifaddrs* it = interfaces; it; it = it->ifa_next) {
        if (!it->ifa_name || !it->ifa_addr || it->ifa_addr->sa_family != AF_PACKET) continue;
        const int kernel_index = reinterpret_cast<const sockaddr_ll*>(it->ifa_addr)->sll_ifindex;
        if (kernel_index <= 0 || static_cast<unsigned>(kernel_index) != index) continue;
        const size_t size = std::strlen(it->ifa_name);
        if (size < IF_NAMESIZE) { std::memcpy(name, it->ifa_name, size + 1); found = true; }
        break;
    }
    ::freeifaddrs(interfaces);
    if (!found) errno = ENXIO;
    return found ? name : nullptr;
}
}  // namespace oh_network
#endif
