#include <cerrno>
#include <initializer_list>
#include <spawn.h>
#include <sys/wait.h>
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
    for (const char* command : {"printf PIPE_OK", "getprop ro.product.model", "/system/bin/param get const.product.model"}) {
        errno=0; FILE* file=popen(command,"r");int opened=errno;
        char text[256]={}; if(file) fgets(text,sizeof(text),file);
        int status=file?pclose(file):-1;
        std::printf("uid=%u command=%s opened=%d errno=%d status=%d text=%s\n",getuid(),command,file!=nullptr,opened,status,text);
    }
    for(const char* shell : {"/bin/sh","/system/bin/sh"}) {
        char* args[]={const_cast<char*>(shell),const_cast<char*>("-c"),const_cast<char*>("printf SPAWN_OK"),nullptr};
        pid_t child=0;int e=posix_spawn(&child,shell,nullptr,nullptr,args,environ);int status=-1;
        if(!e)waitpid(child,&status,0);
        std::printf("uid=%u shell=%s spawn_errno=%d status=%d\n",getuid(),shell,e,status);
    }
    return 0;
}
