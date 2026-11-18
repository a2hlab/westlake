/* Isolate the production mask adapter from the full Android ABI preload, which
 * requires the Android launch environment. Both variants use the board's real
 * watchdog; compile with -DWESTLAKE_PROBE_PROTECT=0 for the unmodified control.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#if WESTLAKE_PROBE_PROTECT
#include "../framework/webview-shim/hicollie_signal_mask.h"
#endif

int pthread_sigmask(int how, const sigset_t *set, sigset_t *old_set) {
    typedef int (*Mask)(int, const sigset_t *, sigset_t *);
    static Mask real;
    if (!real) real = (Mask)dlsym(RTLD_NEXT, "pthread_sigmask");
    if (!real) return ENOSYS;
#if WESTLAKE_PROBE_PROTECT
    sigset_t adapted;
    set = westlake_hicollie_signal_mask(how, set, &adapted, __builtin_return_address(0));
#endif
    return real(how, set, old_set);
}
