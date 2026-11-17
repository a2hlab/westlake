#include <netdb.h>
#include <stddef.h>
#include <string.h>
#include <arpa/inet.h>
/* Invoke the real adapter from a dynamically loaded caller. The test chooses
 * the expected ABI without requiring Android or modifying libc on the host. */
struct bionic_addrinfo {
 int flags,family,socktype,protocol; socklen_t addrlen;
 char *canonname; struct sockaddr *addr; struct bionic_addrinfo *next;
};
int resolve_from_library(int (*resolve)(const char *,const char *,const struct addrinfo *,struct addrinfo **),
                        void (*release)(struct addrinfo *),int bionic) {
 struct addrinfo hints={0},*result=NULL;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
 hints.ai_flags=4; /* Bionic and host AI_NUMERICHOST. No DNS server is needed. */
 int rc=resolve("127.0.0.1","443",&hints,&result);if(rc||!result)return 1;
 const struct sockaddr *address=bionic?((struct bionic_addrinfo *)result)->addr:result->ai_addr;
 socklen_t length=bionic?((struct bionic_addrinfo *)result)->addrlen:result->ai_addrlen;
 int ok=address && length==sizeof(struct sockaddr_in) && address->sa_family==AF_INET;
 if(ok){const struct sockaddr_in *a=(const struct sockaddr_in *)address;ok=a->sin_port==htons(443) && a->sin_addr.s_addr==htonl(0x7f000001);}
 release(result);return ok?0:2;
}
