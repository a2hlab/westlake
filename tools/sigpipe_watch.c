/* Signal-only board diagnostic. Never reads I/O buffers or changes signal masks.
 * Attaches to a verified process's threads, reports SIGPIPE and detaches
 * with the same signal. A timeout also detaches. No PTRACE_O_EXITKILL is used.
 * Build with the OH SDK clang for aarch64-linux-ohos; --self-test uses a real
 * closed pipe and verifies that the tracee still dies from SIGPIPE.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <elf.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__aarch64__)
#include <asm/ptrace.h>
#elif defined(__x86_64__)
#include <sys/user.h>
#else
#error unsupported register ABI
#endif

static volatile sig_atomic_t interrupted;
static void stop(int sig) { (void)sig; interrupted = 1; }
static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
static int verified(pid_t pid, uid_t uid, const char *exe) {
    char path[128], line[256];
    struct stat actual, expected;
    snprintf(path, sizeof(path), "/proc/%d/exe", pid);
    if (stat(path, &actual) || stat(exe, &expected)
            || actual.st_dev != expected.st_dev || actual.st_ino != expected.st_ino) return 0;
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    unsigned real = ~0u, effective = ~0u;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "Uid: %u %u", &real, &effective) == 2) break;
    fclose(f);
    return real == uid && effective == uid;
}
static pid_t watchdog(pid_t pid) {
    char path[128], name[64];
    snprintf(path, sizeof(path), "/proc/%d/task", pid);
    DIR *d = opendir(path);
    if (!d) return 0;
    pid_t found = 0;
    struct dirent *entry;
    while ((entry = readdir(d))) {
        int tid = atoi(entry->d_name);
        if (tid <= 0) continue;
        snprintf(path, sizeof(path), "/proc/%d/task/%d/comm", pid, tid);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (fgets(name, sizeof(name), f) && !strcmp(name, "OS_DfxWatchdog\n")) {
            if (found) { fclose(f); found = 0; break; }
            found = tid;
        }
        fclose(f);
    }
    closedir(d);
    return found;
}
/* Only report addresses in executable mappings, not arbitrary stack words. */
static int location(pid_t pid, const char *label, uintptr_t address) {
    char path[128], line[1024], permissions[8], file[768];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    unsigned long begin, end, offset;
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        file[0] = 0;
        int fields = sscanf(line, "%lx-%lx %7s %lx %*s %*s %767[^\n]",
                &begin, &end, permissions, &offset, file);
        if (fields < 4 || !strchr(permissions, 'x') || address < begin || address >= end) continue;
        const char *name = strrchr(file, '/');
        name = name ? name + 1 : "anonymous";
        if (strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-!") != strlen(name))
            name = "mapped-code";
        printf("%s module=%s offset=0x%lx address=0x%lx\n", label, name,
                (unsigned long)address - begin + offset, (unsigned long)address);
        found = 1;
        break;
    }
    fclose(f);
    return found;
}
static void report(pid_t pid, pid_t tid) {
    siginfo_t info;
    if (!ptrace(PTRACE_GETSIGINFO, tid, 0, &info))
        printf("SIGPIPE tid=%d code=%d sender_pid=%d sender_uid=%u\n",
                tid, info.si_code, info.si_pid, info.si_uid);
#if defined(__aarch64__)
    struct user_pt_regs regs;
#else
    struct user_regs_struct regs;
#endif
    struct iovec io = { &regs, sizeof(regs) };
    if (ptrace(PTRACE_GETREGSET, tid, (void *)(uintptr_t)NT_PRSTATUS, &io) || io.iov_len != sizeof(regs)) {
        printf("registers_unavailable errno=%d\n", errno);
        return;
    }
#if defined(__aarch64__)
    location(pid, "pc", regs.pc);
    location(pid, "lr", regs.regs[30]);
    uintptr_t frame = regs.regs[29];
#else
    location(pid, "pc", regs.rip);
    uintptr_t frame = regs.rbp;
#endif
    for (int i = 0; i < 24 && frame && !(frame & 15); i++) {
        errno = 0;
        uintptr_t next = (uintptr_t)ptrace(PTRACE_PEEKDATA, tid, (void *)frame, 0);
        if (errno) break;
        uintptr_t ret = (uintptr_t)ptrace(PTRACE_PEEKDATA, tid, (void *)(frame + sizeof(uintptr_t)), 0);
        if (errno || !location(pid, "frame", ret)) break;
        if (next <= frame || next - frame > 1024 * 1024) break;
        frame = next;
    }
}
static int delivery(int status) {
    return (unsigned)status >> 16 == PTRACE_EVENT_STOP ? 0 : WSTOPSIG(status);
}
static int detach(pid_t tid, int stopped, int sig) {
    if (!stopped) {
        if (ptrace(PTRACE_INTERRUPT, tid, 0, 0)) {
            /* A fatal signal can remove the other tracees during cleanup. */
            if (errno == ESRCH) return 1;
            printf("interrupt_error errno=%d\n", errno); return 0;
        }
        int status;
        double deadline = now() + 2;
        while (now() < deadline) {
            pid_t got = waitpid(tid, &status, __WALL | WNOHANG);
            if (got == tid) {
                if (!WIFSTOPPED(status)) return 1;
                stopped = 1;
                sig = delivery(status);
                break;
            }
            if (got < 0 && errno != EINTR) return errno == ECHILD;
            usleep(10000);
        }
    }
    if (!stopped) { puts("detach_unconfirmed=true"); return 0; }
    if (ptrace(PTRACE_DETACH, tid, 0, (void *)(uintptr_t)sig)) {
        printf("detach_error errno=%d\n", errno); return 0;
    }
    return 1;
}
static int watch(pid_t pid, uid_t uid, const char *exe, int seconds, int trigger) {
    pid_t tid = 0;
    double deadline = now() + seconds;
    while (!interrupted && now() < deadline && verified(pid, uid, exe)) {
        tid = watchdog(pid);
        if (tid) break;
        usleep(50000);
    }
    if (!tid || !verified(pid, uid, exe)) { puts("watchdog_unavailable"); return 2; }
    if (ptrace(PTRACE_SEIZE, tid, 0, 0)) { printf("attach_error errno=%d\n", errno); return 3; }
    printf("attached pid=%d tid=%d\n", pid, tid);
    if (trigger >= 0) { if (write(trigger, "x", 1) != 1) interrupted = 1; close(trigger); }
    while (!interrupted && now() < deadline) {
        int status;
        pid_t got = waitpid(tid, &status, __WALL | WNOHANG);
        if (got < 0 && errno != EINTR) break;
        if (got == tid) {
            if (!WIFSTOPPED(status)) { puts("tracee_exited"); return 2; }
            int sig = delivery(status);
            if (sig == SIGPIPE) {
                report(pid, tid);
                if (!detach(tid, 1, SIGPIPE)) return 4;
                puts("signal_preserved=true");
                return 0;
            }
            if (ptrace(PTRACE_CONT, tid, 0, (void *)(uintptr_t)sig)) break;
        }
        usleep(10000);
    }
    if (!detach(tid, 0, 0)) return 4;
    puts("no_sigpipe_observed");
    return 2;
}
/* Polling avoids automatically attaching to unrelated child processes. A thread
 * that starts and fails between scans can be missed; a negative result is not a
 * crash-free claim. PTRACE_SEIZE does not stop threads just to attach to them. */
static int watch_all(pid_t pid, uid_t uid, const char *exe, int seconds, int trigger) {
    pid_t traced[2048] = {0};
    double deadline = now() + seconds, next_scan = 0;
    int result = 2;
    while (!interrupted && now() < deadline) {
        if (now() >= next_scan) {
            if (!verified(pid, uid, exe)) { puts("tracee_unavailable"); break; }
            char path[128];
            snprintf(path, sizeof(path), "/proc/%d/task", pid);
            DIR *d = opendir(path);
            if (!d) break;
            struct dirent *entry;
            int failed = 0;
            while ((entry = readdir(d))) {
                int tid = atoi(entry->d_name), seen = 0, slot = -1;
                if (tid <= 0) continue;
                for (int i = 0; i < 2048; i++) {
                    if (traced[i] == tid) { seen = 1; break; }
                    if (!traced[i] && slot < 0) slot = i;
                }
                if (seen) continue;
                if (slot < 0) { puts("thread_limit=true"); failed = 1; break; }
                if (ptrace(PTRACE_SEIZE, tid, 0, 0)) {
                    if (errno == ESRCH) continue;
                    printf("attach_error tid=%d errno=%d\n", tid, errno);
                    failed = 1; break;
                }
                traced[slot] = tid;
                printf("attached pid=%d tid=%d\n", pid, tid);
            }
            closedir(d);
            if (failed) { result = 3; break; }
            if (trigger >= 0) {
                if (write(trigger, "x", 1) != 1) interrupted = 1;
                close(trigger); trigger = -1;
            }
            next_scan = now() + .01;
        }
        int status;
        pid_t got;
        while ((got = waitpid(-1, &status, __WALL | WNOHANG)) > 0) {
            int slot = -1;
            for (int i = 0; i < 2048; i++) if (traced[i] == got) { slot = i; break; }
            if (slot < 0) continue;
            if (!WIFSTOPPED(status)) { traced[slot] = 0; continue; }
            int sig = delivery(status);
            if (sig == SIGPIPE) {
                char path[128], name[64] = {0};
                snprintf(path, sizeof(path), "/proc/%d/task/%d/comm", pid, got);
                FILE *f = fopen(path, "r");
                if (f) { if (!fgets(name, sizeof(name), f)) name[0] = 0; fclose(f); }
                name[strcspn(name, "\n")] = 0;
                if (strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._- ") == strlen(name))
                    printf("signal_thread=%s\n", name);
                report(pid, got);
                result = detach(got, 1, sig) ? 0 : 4;
                traced[slot] = 0;
                if (!result) puts("signal_preserved=true");
                goto cleanup;
            }
            if (ptrace(PTRACE_CONT, got, 0, (void *)(uintptr_t)sig)) {
                printf("continue_error errno=%d\n", errno); result = 4; goto cleanup;
            }
        }
        usleep(5000);
    }
    if (result == 2) puts("no_sigpipe_observed");
cleanup:
    for (int i = 0; i < 2048; i++) if (traced[i]) detach(traced[i], 0, 0);
    if (trigger >= 0) close(trigger);
    return result;
}
static int self_test(const char *exe, int all) {
    int ready[2];
    if (pipe(ready)) return 4;
    pid_t pid = fork();
    if (pid < 0) return 4;
    if (!pid) {
        close(ready[1]);
        prctl(PR_SET_NAME, "OS_DfxWatchdog");
        signal(SIGPIPE, SIG_DFL);
        sigset_t mask;
        sigemptyset(&mask); sigaddset(&mask, SIGPIPE); sigprocmask(SIG_UNBLOCK, &mask, 0);
        char byte;
        if (read(ready[0], &byte, 1) != 1) _exit(90);
        int broken[2];
        if (pipe(broken)) _exit(91);
        close(broken[0]);
        ssize_t count = write(broken[1], "x", 1);
        _exit(count < 0 ? 92 : 93);
    }
    close(ready[0]);
    int result = (all ? watch_all : watch)(pid, getuid(), exe, 5, ready[1]);
    if (result) kill(pid, SIGKILL); /* Only this tool's controlled self-test child. */
    int status;
    if (waitpid(pid, &status, 0) != pid || !WIFSIGNALED(status) || WTERMSIG(status) != SIGPIPE) result = 4;
    printf("self_test=%s\n", result ? "FAIL" : "PASS");
    return result;
}
static int number(const char *text, unsigned long maximum, unsigned long *out) {
    if (!*text || strspn(text, "0123456789") != strlen(text)) return 0;
    errno = 0;
    char *end;
    *out = strtoul(text, &end, 10);
    return !errno && !*end && *out <= maximum;
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGINT, stop); signal(SIGTERM, stop);
    if (argc == 2 && !strcmp(argv[1], "--self-test")) return self_test(argv[0], 0);
    if (argc == 2 && !strcmp(argv[1], "--self-test-all")) return self_test(argv[0], 1);
    int all = argc == 6 && !strcmp(argv[5], "--all-threads");
    unsigned long pid, uid, seconds;
    if ((argc != 5 && !all) || !number(argv[1], INT_MAX, &pid) || pid <= 1
            || !number(argv[2], UINT_MAX, &uid) || !number(argv[4], 300, &seconds) || !seconds) {
        fprintf(stderr, "usage: %s PID UID EXECUTABLE TIMEOUT_SECONDS [--all-threads] | --self-test[-all]\n", argv[0]);
        return 4;
    }
    return (all ? watch_all : watch)((pid_t)pid, (uid_t)uid, argv[3], (int)seconds, -1);
}
