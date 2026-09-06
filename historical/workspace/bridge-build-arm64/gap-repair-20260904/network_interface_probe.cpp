#include "oh_network_interfaces.h"
#include <cstdio>
#include <cstdlib>
#include <grp.h>
#include <sys/prctl.h>
#include <unistd.h>
extern "C" int SetSelfTokenID(unsigned long long);
extern "C" int setcon(const char*);
int main(int argc, char** argv) {
    if (argc == 3) {
        unsigned uid = std::strtoul(argv[1], nullptr, 0);
        if (SetSelfTokenID(std::strtoull(argv[2], nullptr, 0))) return 2;
        for (int cap=0; prctl(PR_CAPBSET_READ,cap,0,0,0)>=0; cap++)
            if (prctl(PR_CAPBSET_DROP,cap,0,0,0)) return 3;
        gid_t groups[]={1097,3003,3004,3099};
        if (setgroups(4,groups) || setresgid(uid,uid,uid) || setresuid(uid,uid,uid) ||
            prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0) || setcon("u:r:normal_hap:s0")) return 4;
    } else if (argc != 1) return 2;
    errno=0; unsigned raw=::if_nametoindex("lo");
    std::printf("uid=%u raw_lo_index=%u raw_errno=%d\n",getuid(),raw,errno);
    ifaddrs* list=nullptr;
    if (getifaddrs(&list)) return 5;
    unsigned count=0, failed=0;
    for (const ifaddrs* it=list; it; it=it->ifa_next) {
        if (!it->ifa_name || !it->ifa_addr || it->ifa_addr->sa_family!=AF_PACKET) continue;
        const auto* packet=reinterpret_cast<const sockaddr_ll*>(it->ifa_addr);
        if (packet->sll_ifindex<=0) continue;
        char name[IF_NAMESIZE]={};
        unsigned index=oh_network::interfaceIndex(it->ifa_name);
        const char* reverse=oh_network::interfaceName(index,name);
        count++;
        if (index!=static_cast<unsigned>(packet->sll_ifindex) || !reverse || std::strcmp(reverse,it->ifa_name)) {
            failed++; std::printf("MISMATCH %s kernel=%d lookup=%u reverse=%s\n",it->ifa_name,packet->sll_ifindex,index,reverse?reverse:"NULL");
        }
    }
    freeifaddrs(list);
    char name[IF_NAMESIZE]={};
    errno=0; unsigned missing=oh_network::interfaceIndex("westlake-noif");int name_error=errno;
    errno=0; const char* missing_name=oh_network::interfaceName(~0u,name);int index_error=errno;
    if (missing || missing_name || (name_error!=ENXIO && name_error!=ENODEV) || index_error!=ENXIO) failed++;
    std::printf("kernel_roundtrips=%u failures=%u missing_name_errno=%d missing_index_errno=%d\n",count,failed,name_error,index_error);
    return count && !failed ? 0 : 6;
}
