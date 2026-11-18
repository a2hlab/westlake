/* Board integration check against the installed libhicollie. The controlled
 * child queues its own SIGPIPE before starting the real native watchdog.
 * No application process, account, network or application data is accessed.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void callback(void *unused) {
    (void)unused;
    sigset_t mask;
    if (pthread_sigmask(SIG_SETMASK, NULL, &mask)) _exit(2);
    int pipe = sigismember(&mask, SIGPIPE), quit = sigismember(&mask, SIGQUIT);
    int usr1 = sigismember(&mask, SIGUSR1), usr2 = sigismember(&mask, SIGUSR2);
    int sampler = sigismember(&mask, 35);
    printf("watchdog_callback=true pipe_blocked=%d quit_blocked=%d usr1_blocked=%d usr2_blocked=%d sampler_blocked=%d\n",
            pipe, quit, usr1, usr2, sampler);
    _exit(pipe == 1 && quit == 1 && usr1 == 1 && usr2 == 0 && sampler == 1 ? 0 : 3);
}

static int child(void) {
    signal(SIGPIPE, SIG_DFL);
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPIPE); sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGUSR1); sigaddset(&mask, SIGUSR2); sigaddset(&mask, 35);
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL)) return 4;
    void *library = dlopen("/system/lib64/chipset-sdk-sp/libhicollie.z.so", RTLD_NOW | RTLD_GLOBAL);
    if (!library) { puts("hicollie_unavailable=true"); return 4; }
    int (*start)(const char *, unsigned, void (*)(void *), void *, unsigned) =
            (int (*)(const char *, unsigned, void (*)(void *), void *, unsigned))dlsym(library, "SetTimerRust");
    if (!start || kill(getpid(), SIGPIPE)) return 4;
    sigset_t pending;
    if (sigpending(&pending) || sigismember(&pending, SIGPIPE) != 1) return 4;
    puts("pending_sigpipe=true");
    if (start("WestlakeSignalProbe", 1, callback, NULL, 0) < 0) return 4;
    sleep(5);
    puts("watchdog_callback_missing=true");
    return 4;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc == 2 && !strcmp(argv[1], "--child")) return child();
    if (argc != 4 || (strcmp(argv[2], "android") && strcmp(argv[2], "native"))
            || (strcmp(argv[3], "preserved") && strcmp(argv[3], "sigpipe"))) {
        fprintf(stderr, "usage: %s SHIM_PATH android|native preserved|sigpipe\n", argv[0]);
        return 4;
    }
    pid_t pid = fork();
    if (pid < 0) return 4;
    if (!pid) {
        setenv("LD_PRELOAD", argv[1], 1);
        if (!strcmp(argv[2], "android")) setenv("ASX_APK_PATH", "/signal-mask-probe.apk", 1);
        else unsetenv("ASX_APK_PATH");
        execl(argv[0], argv[0], "--child", NULL);
        _exit(4);
    }
    int status;
    if (waitpid(pid, &status, 0) != pid) return 4;
    int expected = !strcmp(argv[3], "preserved")
            ? WIFEXITED(status) && WEXITSTATUS(status) == 0
            : WIFSIGNALED(status) && WTERMSIG(status) == SIGPIPE;
    printf("child_signal=%d child_exit=%d expected_result=%s\n",
            WIFSIGNALED(status) ? WTERMSIG(status) : 0,
            WIFEXITED(status) ? WEXITSTATUS(status) : -1, expected ? "PASS" : "FAIL");
    return expected ? 0 : 1;
}
