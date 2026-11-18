#ifndef WESTLAKE_HICOLLIE_SIGNAL_MASK_H
#define WESTLAKE_HICOLLIE_SIGNAL_MASK_H
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const sigset_t *westlake_hicollie_signal_mask(int how, const sigset_t *set,
        sigset_t *adapted, void *return_address)
{
    /* WatchdogInner::Start in OH's libhicollie queries its inherited mask and
     * unblocks everything except its sampler signal. ART deliberately blocks
     * SIGPIPE, SIGQUIT and SIGUSR1 before creating app threads. Removing those
     * blocks delivered a pending SIGPIPE on Wattpad's OS_DfxWatchdog thread and
     * killed the app during startup. Keep the inherited ART blocks for this
     * native watchdog only; Android callers and other OH threads keep their
     * requested semantics. This neither ignores signals process-wide nor
     * disables watchdog tasks. A signal already unblocked stays unblocked.
     */
    const char *apk = getenv("ASX_APK_PATH");
    if (how == SIG_UNBLOCK && set != NULL && apk != NULL && *apk != '\0') {
        int saved_errno = errno;
        Dl_info caller = {0};
        char thread_name[16] = {0};
        if (dladdr(return_address, &caller) != 0 && caller.dli_fname != NULL) {
            const char *base = strrchr(caller.dli_fname, '/');
            base = base != NULL ? base + 1 : caller.dli_fname;
            if (strcmp(base, "libhicollie.z.so") == 0 &&
                pthread_getname_np(pthread_self(), thread_name, sizeof(thread_name)) == 0 &&
                strcmp(thread_name, "OS_DfxWatchdog") == 0) {
                *adapted = *set;
                sigdelset(adapted, SIGPIPE);
                sigdelset(adapted, SIGQUIT);
                sigdelset(adapted, SIGUSR1);
                set = adapted;
                fprintf(stderr, "[WESTLAKE-HICOLLIE-SIGNALS] retained_art_mask=true\n");
            }
        }
        errno = saved_errno;
    }
    return set;
}
#endif
