/*
 * Android 15's seccomp policy for app processes (arm64 primary, arm secondary), as bionic builds it:
 * tools/genseccomp.py --name-modifier app over SYSCALLS.TXT, SECCOMP_ALLOWLIST_APP.TXT,
 * SECCOMP_ALLOWLIST_COMMON.TXT, SECCOMP_BLOCKLIST_APP.TXT, SECCOMP_BLOCKLIST_COMMON.TXT and
 * SECCOMP_PRIORITY.TXT of platform/bionic android15-qpr2-release, with the syscall numbers of that
 * branch's kernel/uapi headers. Both arrays are byte for byte the arm64_app_filter and arm_app_filter
 * of an Android 15 device's libandroid_runtime.so (checked against a BP1A.250505.005 build). Only the
 * array declarations changed for C; the instructions and comments are the generator's output.
 * Regenerate rather than edit.
 */
#ifndef WESTLAKE_ANDROID_APP_SECCOMP_POLICY_H
#define WESTLAKE_ANDROID_APP_SECCOMP_POLICY_H

#include <linux/filter.h>
#include <linux/seccomp.h>

static const struct sock_filter arm64_app_filter[] = {
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 0, 0, 66),
BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 98, 64, 0), //futex
BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 29, 63, 0), //ioctl
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 163, 31, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 101, 15, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 43, 7, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 30, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 19, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 18, 58, 57), //io_setup|io_destroy|io_submit|io_cancel|io_getevents|setxattr|lsetxattr|fsetxattr|getxattr|lgetxattr|fgetxattr|listxattr|llistxattr|flistxattr|removexattr|lremovexattr|fremovexattr|getcwd
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 29, 57, 56), //eventfd2|epoll_create1|epoll_ctl|epoll_pwait|dup|dup3|fcntl|inotify_init1|inotify_add_watch|inotify_rm_watch
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 41, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 39, 55, 54), //ioprio_set|ioprio_get|flock|mknodat|mkdirat|unlinkat|symlinkat|linkat|renameat
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 42, 54, 53), //pivot_root
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 59, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 52, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 51, 51, 50), //statfs|fstatfs|truncate|ftruncate|fallocate|faccessat|chdir|fchdir
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 58, 50, 49), //fchmod|fchmodat|fchownat|fchown|openat|close
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 90, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 89, 48, 47), //pipe2|quotactl|getdents64|lseek|read|write|readv|writev|pread64|pwrite64|preadv|pwritev|sendfile|pselect6|ppoll|signalfd4|vmsplice|splice|tee|readlinkat|newfstatat|fstat|sync|fsync|fdatasync|sync_file_range|timerfd_create|timerfd_settime|timerfd_gettime|utimensat
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 98, 47, 46), //capget|capset|personality|exit|exit_group|waitid|set_tid_address|unshare
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 147, 7, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 113, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 107, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 104, 43, 42), //nanosleep|getitimer|setitimer
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 112, 42, 41), //timer_create|timer_gettime|timer_getoverrun|timer_settime|timer_delete
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 117, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 116, 40, 39), //clock_gettime|clock_getres|clock_nanosleep
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 142, 39, 38), //ptrace|sched_setparam|sched_setscheduler|sched_getscheduler|sched_getparam|sched_setaffinity|sched_getaffinity|sched_yield|sched_get_priority_max|sched_get_priority_min|sched_rr_get_interval|restart_syscall|kill|tkill|tgkill|sigaltstack|rt_sigsuspend|rt_sigaction|rt_sigprocmask|rt_sigpending|rt_sigtimedwait|rt_sigqueueinfo|rt_sigreturn|setpriority|getpriority
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 153, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 150, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 149, 36, 35), //setresuid|getresuid
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 151, 35, 34), //getresgid
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 160, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 159, 33, 32), //times|setpgid|getpgid|getsid|setsid|getgroups
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 161, 32, 31), //uname
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 267, 15, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 220, 7, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 198, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 172, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 170, 27, 26), //getrlimit|setrlimit|getrusage|umask|prctl|getcpu|gettimeofday
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 180, 26, 25), //getpid|getppid|getuid|geteuid|getgid|getegid|gettid|sysinfo
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 203, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 202, 24, 23), //socket|socketpair|bind|listen
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 217, 23, 22), //connect|getsockname|getpeername|sendto|recvfrom|setsockopt|getsockopt|shutdown|sendmsg|recvmsg|readahead|brk|munmap|mremap
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 240, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 226, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 224, 20, 19), //clone|execve|mmap|fadvise64
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 234, 19, 18), //mprotect|msync|mlock|munlock|mlockall|munlockall|mincore|madvise
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 260, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 244, 17, 16), //rt_tgsigqueueinfo|perf_event_open|accept4|recvmmsg
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 262, 16, 15), //wait4|prlimit64
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 434, 7, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 291, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 274, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 272, 12, 11), //syncfs|setns|sendmmsg|process_vm_readv|process_vm_writev
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 288, 11, 10), //sched_setattr|sched_getattr|renameat2|seccomp|getrandom|memfd_create|bpf|execveat|userfaultfd|membarrier|mlock2|copy_file_range|preadv2|pwritev2
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 424, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 292, 9, 8), //statx
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 425, 8, 7), //pidfd_send_signal
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 440, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 438, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 437, 5, 4), //pidfd_open|clone3|close_range
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 439, 4, 3), //pidfd_getfd
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 462, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 442, 2, 1), //process_madvise|epoll_pwait2
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 463, 1, 0), //mseal
BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
};

static const struct sock_filter arm_app_filter[] = {
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 0, 0, 142),
BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 240, 140, 0), //futex
BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 54, 139, 0), //ioctl
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 199, 69, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 91, 35, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 45, 17, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 26, 9, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 19, 5, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 10, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 8, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 7, 132, 131), //restart_syscall|exit|fork|read|write|open|close
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 9, 131, 130), //creat
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 13, 130, 129), //unlink|execve|chdir
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 24, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 21, 128, 127), //lseek|getpid
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 25, 127, 126), //getuid
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 36, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 33, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 27, 124, 123), //ptrace
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 34, 123, 122), //access
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 41, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 40, 121, 120), //sync|kill|rename|mkdir
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 44, 120, 119), //dup|pipe|times
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 66, 9, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 60, 5, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 57, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 55, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 46, 115, 114), //brk
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 56, 114, 113), //fcntl
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 58, 113, 112), //setpgid
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 63, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 61, 111, 110), //umask
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 65, 110, 109), //dup2|getppid
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 77, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 75, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 68, 107, 106), //setsid|sigaction
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 76, 106, 105), //setrlimit
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 85, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 79, 104, 103), //getrusage|gettimeofday
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 86, 103, 102), //readlink
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 131, 17, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 116, 9, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 104, 5, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 96, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 94, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 93, 97, 96), //munmap|truncate
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 95, 96, 95), //fchmod
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 98, 95, 94), //getpriority|setpriority
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 114, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 107, 93, 92), //setitimer|getitimer|stat
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 115, 92, 91), //wait4
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 122, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 118, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 117, 89, 88), //sysinfo
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 121, 88, 87), //fsync|sigreturn|clone
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 125, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 123, 86, 85), //uname
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 126, 85, 84), //mprotect
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 168, 7, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 140, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 136, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 134, 81, 80), //quotactl|getpgid|fchdir
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 137, 80, 79), //personality
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 150, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 149, 78, 77), //_llseek|getdents|_newselect|flock|msync|readv|writev|getsid|fdatasync
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 164, 77, 76), //mlock|munlock|mlockall|munlockall|sched_setparam|sched_getparam|sched_setscheduler|sched_getscheduler|sched_yield|sched_get_priority_max|sched_get_priority_min|sched_rr_get_interval|nanosleep|mremap
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 183, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 172, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 169, 74, 73), //poll
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 182, 73, 72), //prctl|rt_sigreturn|rt_sigaction|rt_sigprocmask|rt_sigpending|rt_sigtimedwait|rt_sigqueueinfo|rt_sigsuspend|pread64|pwrite64
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 190, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 188, 71, 70), //getcwd|capget|capset|sigaltstack|sendfile
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 198, 70, 69), //vfork|ugetrlimit|mmap2|truncate64|ftruncate64|stat64|lstat64|fstat64
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 327, 35, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 256, 17, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 219, 9, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 211, 5, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 207, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 205, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 203, 63, 62), //getuid32|getgid32|geteuid32|getegid32
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 206, 62, 61), //getgroups32
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 210, 61, 60), //fchown32|setresuid32|getresuid32
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 217, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 212, 59, 58), //getresgid32
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 218, 58, 57), //getdents64
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 241, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 224, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 222, 55, 54), //mincore|madvise|fcntl64
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 240, 54, 53), //gettid|readahead|setxattr|lsetxattr|fsetxattr|getxattr|lgetxattr|fgetxattr|listxattr|llistxattr|flistxattr|removexattr|lremovexattr|fremovexattr|tkill|sendfile64
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 250, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 249, 52, 51), //sched_setaffinity|sched_getaffinity|io_setup|io_destroy|io_getevents|io_submit|io_cancel|exit_group
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 254, 51, 50), //epoll_create|epoll_ctl|epoll_wait|remap_file_pages
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 290, 9, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 280, 5, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 270, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 263, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 262, 46, 45), //set_tid_address|timer_create|timer_settime|timer_gettime|timer_getoverrun|timer_delete
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 269, 45, 44), //clock_gettime|clock_getres|clock_nanosleep|statfs64|fstatfs64|tgkill
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 271, 44, 43), //arm_fadvise64_64
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 286, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 285, 42, 41), //waitid|socket|bind|connect|listen
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 289, 41, 40), //getsockname|getpeername|socketpair
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 316, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 292, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 291, 38, 37), //sendto
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 298, 37, 36), //recvfrom|shutdown|setsockopt|getsockopt|sendmsg|recvmsg
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 322, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 319, 35, 34), //inotify_init|inotify_add_watch|inotify_rm_watch
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 326, 34, 33), //openat|mkdirat|mknodat|fchownat
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 403, 17, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 369, 9, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 348, 5, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 345, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 340, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 338, 28, 27), //fstatat64|unlinkat|renameat|linkat|symlinkat|readlinkat|fchmodat|faccessat|pselect6|ppoll|unshare
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 344, 27, 26), //splice|sync_file_range2|tee|vmsplice
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 347, 26, 25), //getcpu|epoll_pwait
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 350, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 349, 24, 23), //utimensat
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 367, 23, 22), //timerfd_create|eventfd|fallocate|timerfd_settime|timerfd_gettime|signalfd4|eventfd2|epoll_create1|dup3|pipe2|inotify_init1|preadv|pwritev|rt_tgsigqueueinfo|perf_event_open|recvmmsg|accept4
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 380, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 373, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 370, 20, 19), //prlimit64
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 378, 19, 18), //syncfs|sendmmsg|setns|process_vm_readv|process_vm_writev
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 397, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 394, 17, 16), //sched_setattr|sched_getattr|renameat2|seccomp|getrandom|memfd_create|bpf|execveat|userfaultfd|membarrier|mlock2|copy_file_range|preadv2|pwritev2
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 398, 16, 15), //statx
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 438, 7, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 421, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 417, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 415, 12, 11), //clock_gettime64|clock_settime64|clock_adjtime64|clock_getres_time64|clock_nanosleep_time64|timer_gettime64|timer_settime64|timerfd_gettime64|timerfd_settime64|utimensat_time64|pselect6_time64|ppoll_time64
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 418, 11, 10), //recvmmsg_time64
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 434, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 425, 9, 8), //rt_sigtimedwait_time64|futex_time64|sched_rr_get_interval_time64|pidfd_send_signal
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 437, 8, 7), //pidfd_open|clone3|close_range
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 983042, 3, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 440, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 439, 5, 4), //pidfd_getfd
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 442, 4, 3), //process_madvise|epoll_pwait2
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 983045, 1, 0),
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 983043, 2, 1), //__ARM_NR_cacheflush
BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 983046, 1, 0), //__ARM_NR_set_tls
BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
};

#endif
