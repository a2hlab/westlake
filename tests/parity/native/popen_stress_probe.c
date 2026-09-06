#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
static atomic_int failed;
static void* worker(void* unused) {
    (void)unused;
    for(int i=0;i<16;i++) {
        FILE* f=popen("exit 7","r");
        if(!f){atomic_fetch_add(&failed,1);continue;}
        int status=i%2?pclose(f):fclose(f);
        if(!WIFEXITED(status)||WEXITSTATUS(status)!=7)atomic_fetch_add(&failed,1);
    }
    return NULL;
}
static int descriptors(void) {
    DIR* d=opendir("/proc/self/fd");if(!d)return -1;
    int count=0;struct dirent* item;
    while((item=readdir(d)))if(item->d_name[0]!='.')count++;
    closedir(d);return count;
}
int main(void) {
    int before=descriptors();pthread_t threads[4];
    for(int i=0;i<4;i++)if(pthread_create(&threads[i],NULL,worker,NULL))return 2;
    for(int i=0;i<12;i++) {
        pid_t child=fork();if(child<0)return 3;
        if(child==0) { FILE* f=tmpfile();if(!f)_exit(4);_exit(fclose(f)?5:0); }
        int status;while(waitpid(child,&status,0)<0&&errno==EINTR){}
        if(status!=0)atomic_fetch_add(&failed,1);
    }
    for(int i=0;i<4;i++)pthread_join(threads[i],NULL);
    int after=descriptors(),status;errno=0;pid_t child=waitpid(-1,&status,WNOHANG);
    if(child!=-1||errno!=ECHILD||before!=after)atomic_fetch_add(&failed,1);
    printf("POPEN_STRESS operations=64 concurrent_forks=12 fds_before=%d fds_after=%d failures=%d\n",before,after,atomic_load(&failed));
    return atomic_load(&failed)?1:0;
}
