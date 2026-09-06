// Minimal non-syscall ptrace launcher for locating fatal native signals in
// appspawn-x descendants.  Unlike exit_hook.c this does not interpose libc or
// alter the tracee's ABI.  It follows fork/clone, records signal-delivery PCs,
// and otherwise resumes each task unchanged.

#include <elf.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PTRACE_O_TRACEFORK
#define PTRACE_O_TRACEFORK 0x00000002
#define PTRACE_O_TRACEVFORK 0x00000004
#define PTRACE_O_TRACECLONE 0x00000008
#define PTRACE_O_TRACEEXIT 0x00000040
#endif

#ifndef PTRACE_O_TRACESYSGOOD
#define PTRACE_O_TRACESYSGOOD 0x00000001
#endif

#ifndef PTRACE_GET_SYSCALL_INFO
#define PTRACE_GET_SYSCALL_INFO 0x420e
#endif

struct wl_ptrace_syscall_info {
    uint8_t op;
    uint8_t pad[3];
    uint32_t arch;
    uint64_t instruction_pointer;
    uint64_t stack_pointer;
    union {
        struct {
            uint64_t nr;
            uint64_t args[6];
        } entry;
        struct {
            int64_t rval;
            uint8_t is_error;
        } exit;
    } u;
};

#ifndef PTRACE_SYSCALL_INFO_ENTRY
#define PTRACE_SYSCALL_INFO_ENTRY 1
#define PTRACE_SYSCALL_INFO_EXIT 2
#endif

enum {
    kNrMunmap = 215,
    kNrMremap = 216,
    kNrMmap = 222,
};

struct regs_a64 {
    unsigned long long x[31];
    unsigned long long sp;
    unsigned long long pc;
    unsigned long long pstate;
};

static int getregs(pid_t pid, struct regs_a64 *regs)
{
    struct iovec io = { regs, sizeof(*regs) };
    return ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRSTATUS, &io);
}

static int map_for(pid_t pid, unsigned long long address, char *out,
                   size_t out_size, unsigned long long *file_offset)
{
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    FILE *maps = fopen(maps_path, "r");
    if (maps == NULL) return 0;

    char line[512];
    while (fgets(line, sizeof(line), maps) != NULL) {
        unsigned long long lo = 0, hi = 0, offset = 0;
        char perms[8] = {0};
        char path[320] = {0};
        int fields = sscanf(line, "%llx-%llx %7s %llx %*s %*s %319[^\n]",
                            &lo, &hi, perms, &offset, path);
        if (fields >= 4 && address >= lo && address < hi) {
            snprintf(out, out_size, "%s", fields >= 5 ? path : "(anonymous)");
            *file_offset = offset + address - lo;
            fclose(maps);
            return 1;
        }
    }
    fclose(maps);
    return 0;
}

// Keep enough full-register samples to catch a late JIT fault.  The old
// one-sample-per-process rule was usually consumed by an earlier recoverable
// signal and omitted x19-x28 at the actual compiled-code failure.
static unsigned int detailed_signal_count = 0;
static int trace_map_syscalls = 0;
static void print_register_site(pid_t pid, int sig, const char *phase);

static long trace_options(void)
{
    long options = PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
                   PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXIT;
    if (trace_map_syscalls) options |= PTRACE_O_TRACESYSGOOD;
    return options;
}

static int resume_task(pid_t pid)
{
    return ptrace(trace_map_syscalls ? PTRACE_SYSCALL : PTRACE_CONT,
                  pid, 0, 0);
}

static int attach_task(pid_t tid, long options)
{
    if (ptrace(PTRACE_ATTACH, tid, 0, 0) != 0) {
        fprintf(stderr, "[WESTLAKE-PTRACE] attach tid=%d failed errno=%d\n",
                tid, errno);
        return -1;
    }
    int status = 0;
    if (waitpid(tid, &status, __WALL) != tid || !WIFSTOPPED(status)) {
        fprintf(stderr, "[WESTLAKE-PTRACE] attach wait tid=%d failed errno=%d status=%#x\n",
                tid, errno, status);
        return -1;
    }
    if (ptrace(PTRACE_SETOPTIONS, tid, 0, (void *)options) != 0) {
        fprintf(stderr, "[WESTLAKE-PTRACE] options tid=%d failed errno=%d\n",
                tid, errno);
        return -1;
    }
    print_register_site(tid, SIGSTOP, "attach-stop");
    if (resume_task(tid) != 0) {
        fprintf(stderr, "[WESTLAKE-PTRACE] resume tid=%d failed errno=%d\n",
                tid, errno);
        return -1;
    }
    return 0;
}

static int attach_process_threads(pid_t root, long options)
{
    char task_path[64];
    snprintf(task_path, sizeof(task_path), "/proc/%d/task", root);
    DIR *tasks = opendir(task_path);
    if (tasks == NULL) {
        fprintf(stderr, "[WESTLAKE-PTRACE] open %s failed errno=%d\n",
                task_path, errno);
        return -1;
    }

    unsigned int attached = 0;
    struct dirent *entry;
    while ((entry = readdir(tasks)) != NULL) {
        char *end = NULL;
        long value = strtol(entry->d_name, &end, 10);
        if (entry->d_name[0] == '\0' || end == NULL || *end != '\0' ||
            value <= 0 || value > INT32_MAX) {
            continue;
        }
        if (attach_task((pid_t)value, options) == 0) attached++;
    }
    closedir(tasks);
    fprintf(stderr,
            "[WESTLAKE-PTRACE] attached root=%d threads=%u map-syscalls=%d\n",
            root, attached, trace_map_syscalls);
    fflush(stderr);
    return attached != 0 ? 0 : -1;
}

struct map_syscall_state {
    pid_t pid;
    uint64_t nr;
    uint64_t args[6];
    uint64_t lr;
};

static struct map_syscall_state map_states[65536];

static void record_map_syscall(pid_t pid)
{
    struct wl_ptrace_syscall_info info;
    memset(&info, 0, sizeof(info));
    long got = ptrace(PTRACE_GET_SYSCALL_INFO, pid, sizeof(info), &info);
    if (got < 0) return;

    struct map_syscall_state *state = &map_states[(unsigned int)pid & 0xffffu];
    if (info.op == PTRACE_SYSCALL_INFO_ENTRY) {
        if (info.u.entry.nr != kNrMmap && info.u.entry.nr != kNrMunmap &&
            info.u.entry.nr != kNrMremap) {
            state->pid = -1;
            return;
        }
        struct regs_a64 regs;
        state->pid = pid;
        state->nr = info.u.entry.nr;
        memcpy(state->args, info.u.entry.args, sizeof(state->args));
        state->lr = getregs(pid, &regs) == 0 ? regs.x[30] : 0;
        return;
    }
    if (info.op != PTRACE_SYSCALL_INFO_EXIT || state->pid != pid) return;

    uint64_t result = (uint64_t)info.u.exit.rval;
    int low_result = !info.u.exit.is_error && result < (1ull << 32);
    int low_target = state->args[0] < (1ull << 32);
    if ((state->nr == kNrMmap && low_result) ||
        ((state->nr == kNrMunmap || state->nr == kNrMremap) && low_target)) {
        char lr_map[384] = {0};
        unsigned long long lr_off = 0;
        int lr_ok = map_for(pid, state->lr, lr_map, sizeof(lr_map), &lr_off);
        fprintf(stderr,
                "[WESTLAKE-PTRACE-MAP] pid=%d nr=%llu a0=%#llx len=%#llx "
                "a2=%#llx a3=%#llx result=%#llx error=%u lr=%#llx %s+%#llx\n",
                pid, (unsigned long long)state->nr,
                (unsigned long long)state->args[0],
                (unsigned long long)state->args[1],
                (unsigned long long)state->args[2],
                (unsigned long long)state->args[3],
                (unsigned long long)result, (unsigned int)info.u.exit.is_error,
                (unsigned long long)state->lr, lr_ok ? lr_map : "?", lr_off);
        fflush(stderr);
    }
    state->pid = -1;
}

static void print_stack_words(pid_t pid, unsigned long long sp)
{
    fprintf(stderr, "[WESTLAKE-PTRACE] stack pid=%d sp=%#llx", pid, sp);
    for (unsigned int i = 0; i < 16; ++i) {
        errno = 0;
        unsigned long word = (unsigned long)ptrace(
                PTRACE_PEEKDATA, pid, (void *)(unsigned long)(sp + i * 8u), 0);
        if (errno != 0) {
            fprintf(stderr, " +%#x=?", i * 8u);
        } else {
            fprintf(stderr, " +%#x=%#lx", i * 8u, word);
        }
    }
    fputc('\n', stderr);
}

static void print_code_words(pid_t pid, unsigned long long pc)
{
    // Capture the generated instructions while the JIT memfd is still mapped.
    // A post-mortem maps file cannot recover anonymous jit-cache bytes.
    unsigned long long start = pc >= 32u ? (pc - 32u) & ~7ull : 0;
    fprintf(stderr, "[WESTLAKE-PTRACE] code pid=%d pc=%#llx", pid, pc);
    for (unsigned int i = 0; i < 9; ++i) {
        unsigned long long address = start + i * 8u;
        errno = 0;
        unsigned long word = (unsigned long)ptrace(
                PTRACE_PEEKDATA, pid, (void *)(unsigned long)address, 0);
        if (errno != 0) {
            fprintf(stderr, " @%#llx=?", address);
        } else {
            fprintf(stderr, " @%#llx=%#lx", address, word);
        }
    }
    fputc('\n', stderr);
}

static void print_register_site(pid_t pid, int sig, const char *phase)
{
    if (sig != SIGABRT) return;
    struct regs_a64 regs;
    if (getregs(pid, &regs) != 0) {
        fprintf(stderr, "[WESTLAKE-PTRACE] %s pid=%d sig=%d getregs errno=%d\n",
                phase, pid, sig, errno);
        return;
    }
    char pc_map[384] = {0}, lr_map[384] = {0};
    unsigned long long pc_off = 0, lr_off = 0;
    int pc_ok = map_for(pid, regs.pc, pc_map, sizeof(pc_map), &pc_off);
    int lr_ok = map_for(pid, regs.x[30], lr_map, sizeof(lr_map), &lr_off);
    siginfo_t info;
    memset(&info, 0, sizeof(info));
    int info_ok = ptrace(PTRACE_GETSIGINFO, pid, 0, &info) == 0;
    fprintf(stderr,
            "[WESTLAKE-PTRACE] %s pid=%d sig=%d code=%d addr=%p "
            "pc=%#llx %s+%#llx lr=%#llx %s+%#llx sp=%#llx fp=%#llx "
            "x0=%#llx x1=%#llx x2=%#llx x3=%#llx x4=%#llx x5=%#llx x6=%#llx x7=%#llx x8=%#llx\n",
            phase, pid, sig,
            info_ok ? info.si_code : 0,
            info_ok ? info.si_addr : NULL,
            regs.pc, pc_ok ? pc_map : "?", pc_off,
            regs.x[30], lr_ok ? lr_map : "?", lr_off, regs.sp, regs.x[29],
            regs.x[0], regs.x[1], regs.x[2], regs.x[3], regs.x[4],
            regs.x[5], regs.x[6], regs.x[7], regs.x[8]);
    if (detailed_signal_count < 64u) {
        detailed_signal_count++;
        fprintf(stderr,
                "[WESTLAKE-PTRACE] callee-saved pid=%d "
                "x9=%#llx x10=%#llx x11=%#llx x12=%#llx x13=%#llx x14=%#llx "
                "x15=%#llx x16=%#llx x17=%#llx x18=%#llx x19=%#llx x20=%#llx "
                "x21=%#llx x22=%#llx x23=%#llx x24=%#llx x25=%#llx x26=%#llx "
                "x27=%#llx x28=%#llx\n",
                pid, regs.x[9], regs.x[10], regs.x[11], regs.x[12], regs.x[13],
                regs.x[14], regs.x[15], regs.x[16], regs.x[17], regs.x[18],
                regs.x[19], regs.x[20], regs.x[21], regs.x[22], regs.x[23],
                regs.x[24], regs.x[25], regs.x[26], regs.x[27], regs.x[28]);
        print_code_words(pid, regs.pc);
        print_stack_words(pid, regs.sp);
        unsigned long long fp = regs.x[29];
        for (unsigned int depth = 0; depth < 48 && (fp & 15) == 0 && fp >= regs.sp && fp - regs.sp < 8*1024*1024; ++depth) {
            errno = 0;
            unsigned long next = ptrace(PTRACE_PEEKDATA, pid, (void *)(unsigned long)fp, 0);
            if (errno) break;
            unsigned long ra = ptrace(PTRACE_PEEKDATA, pid, (void *)(unsigned long)(fp+8), 0);
            if (errno) break;
            char frame_map[384] = {0}; unsigned long long frame_offset = 0;
            int found = map_for(pid, ra, frame_map, sizeof(frame_map), &frame_offset);
            fprintf(stderr, "[ABRT-FRAME] tid=%d depth=%u ra=%#lx %s+%#llx\n", pid, depth, ra, found?frame_map:"?", frame_offset);
            if (next <= fp) break;
            fp = next;
        }

    }
    fflush(stderr);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s command [args...] | --attach pid\n", argv[0]);
        return 2;
    }

    int status = 0;
    trace_map_syscalls = getenv("WL_PTRACE_MAP_SYSCALLS") != NULL;
    long options = trace_options();
    pid_t root;
    if (strcmp(argv[1], "--attach") == 0) {
        if (argc != 3) return 2;
        char *end = NULL;
        long value = strtol(argv[2], &end, 10);
        if (end == NULL || *end != '\0' || value <= 0 || value > INT32_MAX) return 2;
        root = (pid_t)value;
        if (attach_process_threads(root, options) != 0) return 3;
    } else {
        root = fork();
        if (root == 0) {
            if (ptrace(PTRACE_TRACEME, 0, 0, 0) != 0) _exit(126);
            raise(SIGSTOP);
            execvp(argv[1], &argv[1]);
            _exit(127);
        }
        if (waitpid(root, &status, 0) != root) return 3;
        ptrace(PTRACE_SETOPTIONS, root, 0, (void *)options);
        resume_task(root);
        fprintf(stderr, "[WESTLAKE-PTRACE] tracing root=%d map-syscalls=%d\n",
                root, trace_map_syscalls);
        fflush(stderr);
    }

    for (;;) {
        pid_t pid = waitpid(-1, &status, __WALL);
        if (pid < 0) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) break;
            return 4;
        }
        if (WIFEXITED(status) || WIFSIGNALED(status)) continue;
        if (!WIFSTOPPED(status)) continue;

        int sig = WSTOPSIG(status);
        unsigned int event = (unsigned int)status >> 16;
        if (trace_map_syscalls && sig == (SIGTRAP | 0x80)) {
            record_map_syscall(pid);
            ptrace(PTRACE_SYSCALL, pid, 0, 0);
            continue;
        }
        if (event == PTRACE_EVENT_FORK || event == PTRACE_EVENT_VFORK ||
            event == PTRACE_EVENT_CLONE) {
            unsigned long new_pid = 0;
            ptrace(PTRACE_GETEVENTMSG, pid, 0, &new_pid);
            resume_task(pid);
            continue;
        }
        if (event == PTRACE_EVENT_EXIT) {
            unsigned long exit_status = 0;
            ptrace(PTRACE_GETEVENTMSG, pid, 0, &exit_status);
            if (exit_status != 0) {
                print_register_site(pid, (int)(exit_status & 0x7f), "exit-event");
                fprintf(stderr,
                        "[WESTLAKE-PTRACE] exit-event pid=%d waitstatus=%#lx\n",
                        pid, exit_status);
                fflush(stderr);
            }
            resume_task(pid);
            continue;
        }

        if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGABRT) {
            print_register_site(pid, sig, "signal-delivery");
        }
        int deliver = (sig == SIGTRAP || sig == SIGSTOP) ? 0 : sig;
        ptrace(trace_map_syscalls ? PTRACE_SYSCALL : PTRACE_CONT,
               pid, 0, (void *)(long)deliver);
    }
    return 0;
}
