#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
static unsigned checks;
static void check(int v){if(!v){fprintf(stderr,"NETWORK_APK_CALLER_FAIL %u\n",checks+1);exit(1);}checks++;}
typedef int (*Resolver)(const char *,const char *,const struct addrinfo *,struct addrinfo **);
typedef void (*Release)(struct addrinfo *);
typedef int (*Probe)(Resolver,Release,int);
static void probe(const char *name,Resolver resolve,Release release,int bionic) {
 void *h=dlopen(name,RTLD_NOW|RTLD_LOCAL);check(h!=NULL);Probe call=(Probe)dlsym(h,"resolve_from_library");check(call!=NULL);check(call(resolve,release,bionic)==0);check(dlclose(h)==0);
}
int main(int argc,char **argv){
 if(argc!=4)return 2;
 unsetenv("ASX_NATIVE_LIB_DIR");unsetenv("WESTLAKE_ANDROID_NATIVE_NET_TARGETS");
 void *h=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);check(h!=NULL);Resolver resolve=(Resolver)dlsym(h,"getaddrinfo");Release release=(Release)dlsym(h,"freeaddrinfo");check(resolve&&release);
 /* This executable and a regular native library must keep the host ABI. */
 struct addrinfo hints={0},*r=NULL;hints.ai_family=AF_INET;check(resolve("127.0.0.1",NULL,&hints,&r)==0);check(r&&r->ai_addr&&r->ai_addr->sa_family==AF_INET);release(r);
 probe(argv[2],resolve,release,0);
 int fd=open(argv[3],O_RDONLY);check(fd>=0);char label[128];snprintf(label,sizeof label,"/proc/self/fd/%d",fd);
 probe(label,resolve,release,1);check(close(fd)==0);
 /* Reuse the descriptor number for a native DSO; no stale APK cache is allowed. */
 int native=open(argv[2],O_RDONLY);check(native>=0);if(native!=fd){check(dup2(native,fd)==fd);close(native);}probe(label,resolve,release,0);close(fd);
 setenv("WESTLAKE_ANDROID_NATIVE_NET_TARGETS","native.so",1);probe(argv[2],resolve,release,1);unsetenv("WESTLAKE_ANDROID_NATIVE_NET_TARGETS");
 check(dlclose(h)==0);printf("NETWORK_APK_CALLER_PASS %u\n",checks);return 0;
}
