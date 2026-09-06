#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/ashmem.h>
#include <linux/capability.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include <unistd.h>
extern int SetSelfTokenID(uint64_t);
extern int setcon(const char *);

static void mapping(const char *name, int fd) {
    if (fd < 0) { printf("%s create errno=%d\n", name, errno); return; }
    struct stat st = {0}; fstat(fd, &st);
    char label[256] = {0}; fgetxattr(fd, "security.selinux", label, sizeof(label)-1);
    printf("%s mode=%o uid=%u label=%s\n", name, st.st_mode, st.st_uid, label);
    void *rw = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    int rwerr = rw == MAP_FAILED ? errno : 0;
    void *rx = mmap(NULL, 4096, PROT_READ|PROT_EXEC, MAP_SHARED, fd, 0);
    printf("%s shared_rw=%d shared_rx=%d\n", name, rwerr, rx == MAP_FAILED ? errno : 0);
    if (rw != MAP_FAILED) munmap(rw, 4096);
    if (rx != MAP_FAILED) munmap(rx, 4096);
    close(fd);
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 3) {
        unsigned uid = strtoul(argv[1], NULL, 0);
        if (SetSelfTokenID(strtoull(argv[2], NULL, 0))) return 2;
        for (int cap=0; prctl(PR_CAPBSET_READ,cap,0,0,0)>=0; cap++)
            if (prctl(PR_CAPBSET_DROP,cap,0,0,0)) return 3;
        gid_t groups[] = {1097,3003,3004,3099};
        if (setgroups(4,groups) || setresgid(uid,uid,uid) || setresuid(uid,uid,uid)) return 4;
        if (prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0) || setcon("u:r:normal_hap:s0")) return 5;
    }
    char context[256]={0};int attr=open("/proc/self/attr/current",O_RDONLY);
    if(attr>=0){read(attr,context,sizeof(context)-1);close(attr);}
    printf("uid=%u context=%s\n",getuid(),context);
    const char *paths[]={"/system/etc/fonts.xml", "/data/local/tmp/asx/fonts.xml", "/system/fonts/HarmonyOS_Sans.ttf", "/system/fonts/Roboto-Regular.ttf", "/system/android/framework/framework-res.apk", "/data/local/tmp/asx/framework-res.apk", "/data/local/tmp/asx/android-root/etc/fonts.xml"};
    for (size_t i=0; i<sizeof(paths)/sizeof(paths[0]); i++) {
        int file=open(paths[i],O_RDONLY); int opened=file < 0 ? errno : 0;
        char first=0; int readable=file < 0 || read(file,&first,1) < 0 ? errno : 0;
        printf("file=%s open_errno=%d read_errno=%d\n",paths[i],opened,readable);
        if(file>=0) close(file);
    }
    int fd=syscall(SYS_memfd_create,"westlake-exec-probe",0);
    if(fd>=0)ftruncate(fd,4096);
    mapping("memfd",fd);
    fd=open("/dev/ashmem",O_RDWR);
    if(fd>=0 && ioctl(fd,ASHMEM_SET_SIZE,4096)){printf("ashmem size errno=%d\n",errno);close(fd);fd=-1;}
    mapping("ashmem",fd);
    void *memory=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(memory==MAP_FAILED){printf("anonymous_rw errno=%d\n",errno);return 6;}
    // AArch64: mov w0,#42; ret. Execute, update under RWX, and execute again.
    uint32_t *code = memory;
    code[0] = 0x52800540; code[1] = 0xd65f03c0;
    __builtin___clear_cache((char *)code, (char *)(code + 2));
    int ret=mprotect(memory,4096,PROT_READ|PROT_EXEC);
    printf("anonymous_rw_to_rx=%d\n",ret ? errno : 0);
    if (ret) return 7;
    int first = ((int (*)(void))memory)();
    ret=mprotect(memory,4096,PROT_READ|PROT_WRITE|PROT_EXEC);
    printf("anonymous_rx_to_rwx=%d first_result=%d\n",ret ? errno : 0,first);
    if (ret || first != 42) return 8;
    code[0] = 0x52800560; // mov w0,#43
    __builtin___clear_cache((char *)code, (char *)(code + 2));
    ret=mprotect(memory,4096,PROT_READ|PROT_EXEC);
    printf("anonymous_rwx_to_rx=%d\n",ret ? errno : 0);
    if (ret) return 9;
    int second = ((int (*)(void))memory)();
    printf("updated_code_result=%d\n",second);
    if (second != 43) return 10;
    munmap(memory,4096);
    return 0;
}
