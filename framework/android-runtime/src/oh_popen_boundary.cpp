// Android popen lifecycle at the OH libc boundary. Android returns a stream
// after fork succeeds, even when the child cannot exec its shell (status 127).
// OH musl's posix_spawn implementation instead reports exec errors immediately.
// Keep the ordinary app identity/policy and report the actual child exit status.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
struct ProcessStream { FILE* stream; pid_t pid; ProcessStream* next; };
pthread_mutex_t streams_lock = PTHREAD_MUTEX_INITIALIZER;
ProcessStream* streams = nullptr;
int (*libc_fclose)(FILE*);
int (*libc_pclose)(FILE*);
pthread_once_t initialized = PTHREAD_ONCE_INIT;
bool trace;
extern "C" char** environ;
void resolve() {
    libc_fclose = reinterpret_cast<int(*)(FILE*)>(dlsym(RTLD_NEXT,"fclose"));
    libc_pclose = reinterpret_cast<int(*)(FILE*)>(dlsym(RTLD_NEXT,"pclose"));
    const char* requested=getenv("WESTLAKE_POPEN_TRACE");
    trace=requested && !strcmp(requested,"1");
    pthread_atfork([]{pthread_mutex_lock(&streams_lock);},
                   []{pthread_mutex_unlock(&streams_lock);},
                   []{pthread_mutex_unlock(&streams_lock);});
}
__attribute__((constructor)) void initialize() { pthread_once(&initialized,resolve); }
ProcessStream* remove(FILE* stream) {
    pthread_mutex_lock(&streams_lock);
    ProcessStream** link=&streams;
    while (*link && (*link)->stream!=stream) link=&(*link)->next;
    ProcessStream* node=*link;
    if (node) *link=node->next;
    pthread_mutex_unlock(&streams_lock);
    return node;
}
int finish(ProcessStream* node) {
    // Close/flush the pipe before waiting, including the write and r+ cases.
    libc_fclose(node->stream);
    int status; pid_t result;
    do { result=waitpid(node->pid,&status,0); } while (result<0 && errno==EINTR);
    const int saved=errno;
    if (trace) fprintf(stderr,"[OH_POPEN] reaped pid=%d status=%d errno=%d\n",node->pid,result<0?-1:status,result<0?saved:0);
    free(node);
    errno=saved;
    return result<0 ? -1 : status;
}
}

extern "C" FILE* popen(const char* command,const char* mode) {
    if (!command || !mode) { errno=EINVAL; return nullptr; }
    initialize();
    if (!libc_fclose || !libc_pclose) { errno=ENOSYS; return nullptr; }
    const bool bidirectional=strchr(mode,'+')!=nullptr;
    const bool reading=bidirectional || strchr(mode,'r')!=nullptr;
    const char* stream_mode=bidirectional?"r+":(reading?"r":"w");
    int fds[2];
    if ((bidirectional?socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,fds):pipe2(fds,O_CLOEXEC))) return nullptr;
    const int parent=reading?0:1, child=1-parent, desired=reading?STDOUT_FILENO:STDIN_FILENO;
    if (fds[child]==desired) {
        int replacement=fcntl(fds[child],F_DUPFD_CLOEXEC,3);
        if (replacement<0) { int e=errno;close(fds[0]);close(fds[1]);errno=e;return nullptr; }
        close(fds[child]);fds[child]=replacement;
    }
    auto* node=static_cast<ProcessStream*>(malloc(sizeof(ProcessStream)));
    FILE* stream=node?fdopen(fds[parent],stream_mode):nullptr;
    if (!stream) { int e=errno;free(node);close(fds[0]);close(fds[1]);errno=e;return nullptr; }
    char* args[]={const_cast<char*>("sh"),const_cast<char*>("-c"),const_cast<char*>("--"),const_cast<char*>(command),nullptr};
    // As in bionic, the vfork child performs only descriptor operations and exec.
    // Do not call application atfork callbacks or modify process credentials.
    pid_t pid=vfork();
    if (pid==0) {
        close(fds[parent]);
        if (dup2(fds[child],desired)<0) _exit(127);
        close(fds[child]);
        if (bidirectional && dup2(STDOUT_FILENO,STDIN_FILENO)<0) _exit(127);
        execve("/system/bin/sh",args,environ);
        _exit(127);
    }
    if (pid<0) { int e=errno;libc_fclose(stream);close(fds[child]);free(node);errno=e;return nullptr; }
    close(fds[child]);
    node->stream=stream;node->pid=pid;
    pthread_mutex_lock(&streams_lock);node->next=streams;streams=node;pthread_mutex_unlock(&streams_lock);
    if (trace) {
        Dl_info caller{};dladdr(__builtin_return_address(0),&caller);
        fprintf(stderr,"[OH_POPEN] opened pid=%d fd=%d caller=%s\n",pid,fileno(stream),caller.dli_fname?caller.dli_fname:"?");
    }
    return stream;
}
extern "C" int pclose(FILE* stream) {
    initialize();if (!libc_pclose) { errno=ENOSYS;return -1; }
    ProcessStream* node=remove(stream);
    return node?finish(node):libc_pclose(stream);
}
extern "C" int fclose(FILE* stream) {
    initialize();if (!libc_fclose) { errno=ENOSYS;return EOF; }
    ProcessStream* node=remove(stream);
    return node?finish(node):libc_fclose(stream);
}
