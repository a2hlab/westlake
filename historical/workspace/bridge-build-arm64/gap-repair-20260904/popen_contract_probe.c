#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <unistd.h>

static int errors;
#define CHECK(c) do {if(!(c)){printf("FAIL line=%d errno=%d\n",__LINE__,errno);errors++;}}while(0)
int main(int argc,char** argv) {
    int denied=argc>1&&!strcmp(argv[1],"--deny-exec");
    if(denied) {
        struct sock_filter filter[]={BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,nr)),BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_execve,0,1),BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EACCES),BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW)};
        struct sock_fprog program={sizeof(filter)/sizeof(filter[0]),filter};
        if(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)||prctl(PR_SET_SECCOMP,SECCOMP_MODE_FILTER,&program)) {perror("seccomp");return 2;}
    }
    FILE* f=popen("printf PIPE_OK; exit 7","r");CHECK(f!=NULL);
    if(f){char b[32]={0};char* got=fgets(b,sizeof(b),f);CHECK(fileno(f)>=0);CHECK(fcntl(fileno(f),F_GETFD)&FD_CLOEXEC);int status=pclose(f);printf("read denied=%d got=%d text=%s status=%d\n",denied,got!=NULL,b,status);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==(denied?127:7));CHECK(denied?!got:!strcmp(b,"PIPE_OK"));}
    f=popen("exit 9","r");CHECK(f!=NULL);if(f){int status=fclose(f);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==(denied?127:9));printf("fclose status=%d\n",status);}
    if(!denied){
        f=popen("cat","r+");CHECK(f!=NULL);if(f){fputs("BIDIRECTIONAL\n",f);fflush(f);shutdown(fileno(f),SHUT_WR);char b[64]={0};CHECK(fgets(b,sizeof(b),f)!=NULL);CHECK(!strcmp(b,"BIDIRECTIONAL\n"));CHECK(pclose(f)==0);}
        f=popen("cat >/dev/null; exit 3","w");CHECK(f!=NULL);if(f){fputs("WRITE",f);int status=pclose(f);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==3);}
        f=popen("kill -KILL $$","r");CHECK(f!=NULL);if(f){int status=pclose(f);CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==SIGKILL);}
    }
    printf("POPEN_CONTRACT denied=%d failures=%d\n",denied,errors);return errors?1:0;
}
