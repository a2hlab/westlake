/*
 * Android's seccomp filter for app processes, in Westlake's app process.
 *
 * Every Android app process runs under the filter the zygote installs while specializing the child
 * (SetUpSeccompFilter -> bionic's set_app_seccomp_filter): /proc/self/status reads "Seccomp: 2", and a
 * system call outside Android's app policy raises SIGSYS (SECCOMP_RET_TRAP) instead of running.
 * Westlake's app process had no filter at all ("Seccomp: 0").
 *
 * With WESTLAKE_ANDROID_SECCOMP=1, libwl_missing_natives' JNI_OnLoad installs the program bionic's
 * _set_seccomp_filter(APP) builds: the architecture check, Android 15's arm64 app policy, and the arm
 * policy behind the AArch32 jump, from android_app_seccomp_policy.h. The library loads ahead of
 * handleBindApplication, so the filter is in place before any of the app's own code runs.
 *
 * Android installs the filter in the single-threaded child right after fork, so every later thread
 * inherits it. Westlake's process already has threads at this point, so SECCOMP_FILTER_FLAG_TSYNC
 * puts the one filter on all of them. Without CAP_SYS_ADMIN the kernel requires no_new_privs, which
 * appspawn-x's children already have; nothing here sets it.
 *
 * This is Android's own policy, not a stand-in: a no-op filter would only change what /proc reports.
 */
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "android_app_seccomp_policy.h"

#define WL_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#define WL_ANDROID_APP_FILTER_LENGTH (4 + 1 + WL_COUNT(arm64_app_filter) + 1 + 1 + WL_COUNT(arm_app_filter) + 1)

size_t wl_build_android_app_filter(struct sock_filter *out);
int wl_install_android_app_seccomp(void);

/* The status lines a reader of /proc/self/status sees, for the log. */
static void wl_seccomp_status(char *out, size_t size)
{
    char line[256];
    size_t used = 0;
    FILE *status = fopen("/proc/self/status", "re");
    out[0] = '\0';
    if (status == NULL) return;
    while (fgets(line, sizeof(line), status) != NULL) {
        if (strncmp(line, "Seccomp", 7) != 0 && strncmp(line, "NoNewPrivs", 10) != 0) continue;
        line[strcspn(line, "\n")] = '\0';
        int n = snprintf(out + used, size - used, "%s%s", used > 0 ? ", " : "", line);
        if (n < 0 || (size_t) n >= size - used) break;
        used += (size_t) n;
    }
    fclose(status);
}

/*
 * The program bionic's _set_seccomp_filter(APP) builds, into out (room for
 * WL_ANDROID_APP_FILTER_LENGTH instructions). Returns its length, or 0 if the AArch32 jump does not fit.
 */
size_t wl_build_android_app_filter(struct sock_filter *out)
{
    size_t n = 0;

    /* ValidateArchitectureAndJumpIfNeeded: AArch64 falls through, AArch32 jumps to its own policy. */
    out[n++] = (struct sock_filter) BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch));
    out[n++] = (struct sock_filter) BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 2, 0);
    out[n++] = (struct sock_filter) BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_ARM, 1, 0);
    out[n++] = (struct sock_filter) BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP);
    size_t secondary_jump = n - 2;

    out[n++] = (struct sock_filter) BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
    memcpy(&out[n], arm64_app_filter, sizeof(arm64_app_filter));
    n += WL_COUNT(arm64_app_filter);
    out[n++] = (struct sock_filter) BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP);

    /* SetValidateArchitectureJumpTarget: the AArch32 check lands on the next instruction. */
    size_t jump = n - secondary_jump - 1;
    if (jump > 255) return 0;
    out[secondary_jump] = (struct sock_filter) BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_ARM, (unsigned char) jump, 0);

    out[n++] = (struct sock_filter) BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
    memcpy(&out[n], arm_app_filter, sizeof(arm_app_filter));
    n += WL_COUNT(arm_app_filter);
    out[n++] = (struct sock_filter) BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP);
    return n;
}

int wl_install_android_app_seccomp(void)
{
    struct sock_filter program[WL_ANDROID_APP_FILTER_LENGTH];
    size_t n = wl_build_android_app_filter(program);
    if (n == 0) {
        fprintf(stderr, "[WL-SECCOMP] Android app filter not installed: the AArch32 jump does not fit\n");
        return -1;
    }
    struct sock_fprog prog = { (unsigned short) n, program };
    long rc = syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &prog);
    char status[160];
    wl_seccomp_status(status, sizeof(status));
    if (rc != 0) {
        /* A positive answer is the thread that could not take the filter (TSYNC). */
        if (rc > 0) {
            fprintf(stderr, "[WL-SECCOMP] Android app filter not installed: thread %ld cannot take it (%s)\n", rc, status);
        } else {
            fprintf(stderr, "[WL-SECCOMP] Android app filter not installed: %s (%s)\n", strerror(errno), status);
        }
        return -1;
    }
    fprintf(stderr, "[WL-SECCOMP] Android 15 app filter installed on every thread: %zu instructions (%s)\n", n, status);
    return 0;
}
