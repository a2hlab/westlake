#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <fcntl.h>
#include <locale.h>
#include <math.h>
#include <netdb.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <errno.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

static int caller_is_webview(void *return_address, const char **caller_path)
{
    Dl_info caller = {0};
    if (dladdr(return_address, &caller) == 0 || caller.dli_fname == NULL ||
        strstr(caller.dli_fname, "libwebviewchromium.so") == NULL) {
        return 0;
    }
    if (caller_path != NULL) {
        *caller_path = caller.dli_fname;
    }
    return 1;
}

/* Code built against bionic: the WebView DSO and the app's own packaged libraries. */
/*
 * An Android-built (bionic ABI) caller: the WebView, a library the app packages
 * (/data/local/tmp/asx/lib/), or one the app wrote and loads from its own storage
 * (/data/data/<pkg>/..., including the code_cache/wl-exec copies this shim makes of
 * them). WhatsApp's superpack libraries run from those copies; its Breakpad setup
 * passed a 32-byte bionic old-action to OH's sigaction untranslated, musl wrote 152
 * bytes into it, and the clobbered stack crashed the app four seconds in.
 */
static int caller_is_android_dso(void *return_address, const char **caller_path)
{
    Dl_info caller = {0};
    if (dladdr(return_address, &caller) == 0 || caller.dli_fname == NULL) {
        return 0;
    }
    if (strstr(caller.dli_fname, "libwebviewchromium.so") == NULL &&
        strstr(caller.dli_fname, "/data/local/tmp/asx/lib/") == NULL &&
        strncmp(caller.dli_fname, "/data/data/", 11) != 0 &&
        strncmp(caller.dli_fname, "/data/user/", 11) != 0) {
        return 0;
    }
    if (caller_path != NULL) {
        *caller_path = caller.dli_fname;
    }
    return 1;
}

static int caller_uses_bionic_signal_abi(void *return_address,
                                         const char **caller_path)
{
    return caller_is_android_dso(return_address, caller_path);
}

/*
 * Bionic exposes getprogname(), while the OH musl libc on the target does
 * not.  Newer Chromium 109 link outputs retain that import even though the
 * value is only used for process identity and diagnostics.  The WebView ELF
 * import is redirected to this equal-length name (getprogname ->
 * wl_progname), keeping the compatibility behavior local to the Android
 * boundary instead of adding a process-wide libc interposer.
 */
static pthread_once_t westlake_program_name_once = PTHREAD_ONCE_INIT;
static char westlake_program_name[256] = "unknown";

static void westlake_read_program_name(void)
{
    char value[sizeof(westlake_program_name)];
    ssize_t length = -1;
    int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        length = read(fd, value, sizeof(value) - 1);
        close(fd);
    }
    if (length <= 0) {
        length = readlink("/proc/self/exe", value, sizeof(value) - 1);
    }
    if (length <= 0) return;
    value[length] = '\0';
    char *base = strrchr(value, '/');
    base = base != NULL ? base + 1 : value;
    if (*base != '\0') {
        snprintf(westlake_program_name, sizeof(westlake_program_name), "%s",
                 base);
    }
}

const char *wl_progname(void)
{
    pthread_once(&westlake_program_name_once, westlake_read_program_name);
    return westlake_program_name;
}

/*
 * Android's public arm64 signal ABI is not compatible with OH musl:
 *
 *   bionic: flags@0, callback@8, 64-bit mask@16, restorer@24 (32 bytes)
 *   OH musl: callback@0, 128-byte mask@8, flags@136, restorer@144 (152 bytes)
 *   bionic sigset_t: 8 bytes
 *   OH musl sigset_t: 128 bytes
 *
 * Letting Android-built Chromium call OH sigaction directly makes musl treat
 * the bionic flags word as a callback pointer and overrun a bionic old-action
 * buffer on return.  Letting it call OH sigfillset/sigemptyset directly is
 * equally destructive: musl writes 128 bytes into Chromium's 8-byte set,
 * corrupting the stack and eventually executing an action flags word such as
 * 0x18000004 as a callback.
 *
 * WebView ELF imports are redirected to equal-length wl_* names at deployment
 * time, so these converters are scoped to the Android WebView boundary.
 * Native OH and ART signal handling is untouched.
 */
struct westlake_webview_bionic_sigaction {
    int32_t flags;
    uint32_t flags_padding;
    union {
        void (*handler)(int);
        void (*sigaction)(int, siginfo_t *, void *);
    } callback;
    uint64_t mask;
    void (*restorer)(void);
};

_Static_assert(sizeof(struct westlake_webview_bionic_sigaction) == 32,
               "unexpected bionic arm64 sigaction size");
_Static_assert(offsetof(struct westlake_webview_bionic_sigaction, callback) == 8,
               "unexpected bionic arm64 sigaction callback offset");
_Static_assert(offsetof(struct westlake_webview_bionic_sigaction, mask) == 16,
               "unexpected bionic arm64 sigaction mask offset");
_Static_assert(sizeof(struct sigaction) == 152,
               "unexpected OH arm64 sigaction size");
_Static_assert(offsetof(struct sigaction, sa_mask) == 8,
               "unexpected OH arm64 sigaction mask offset");
_Static_assert(offsetof(struct sigaction, sa_flags) == 136,
               "unexpected OH arm64 sigaction flags offset");
_Static_assert(sizeof(sigset_t) == 128,
               "unexpected OH arm64 sigset_t size");

typedef uint64_t westlake_webview_bionic_sigset_t;

static int westlake_bionic_signal_number_is_valid(int signal_number)
{
    return signal_number > 0 && signal_number <= 64;
}

int wl_emptyset(westlake_webview_bionic_sigset_t *set)
{
    if (set == NULL) {
        errno = EINVAL;
        return -1;
    }
    *set = 0;
    return 0;
}

int wl_fillset(westlake_webview_bionic_sigset_t *set)
{
    if (set == NULL) {
        errno = EINVAL;
        return -1;
    }
    *set = UINT64_MAX;
    return 0;
}

int wl_addset(westlake_webview_bionic_sigset_t *set, int signal_number)
{
    if (set == NULL ||
        !westlake_bionic_signal_number_is_valid(signal_number)) {
        errno = EINVAL;
        return -1;
    }
    *set |= UINT64_C(1) << (signal_number - 1);
    return 0;
}

int wl_ismember(const westlake_webview_bionic_sigset_t *set,
                int signal_number)
{
    if (set == NULL ||
        !westlake_bionic_signal_number_is_valid(signal_number)) {
        errno = EINVAL;
        return -1;
    }
    return ((*set & (UINT64_C(1) << (signal_number - 1))) != 0) ? 1 : 0;
}

int wl_pthread_mask(int how,
                    const westlake_webview_bionic_sigset_t *bionic_set,
                    westlake_webview_bionic_sigset_t *bionic_old_set)
{
    typedef int (*PthreadSigmaskFn)(int, const sigset_t *, sigset_t *);
    static PthreadSigmaskFn real_pthread_sigmask;
    if (real_pthread_sigmask == NULL) {
        real_pthread_sigmask =
                (PthreadSigmaskFn)dlsym(RTLD_NEXT, "pthread_sigmask");
    }
    if (real_pthread_sigmask == NULL) {
        return ENOSYS;
    }

    sigset_t oh_set;
    sigset_t oh_old_set;
    const sigset_t *oh_set_pointer = NULL;
    sigset_t *oh_old_set_pointer = NULL;
    if (bionic_set != NULL) {
        memset(&oh_set, 0, sizeof(oh_set));
        memcpy(&oh_set, bionic_set, sizeof(*bionic_set));
        oh_set_pointer = &oh_set;
    }
    if (bionic_old_set != NULL) {
        memset(&oh_old_set, 0, sizeof(oh_old_set));
        oh_old_set_pointer = &oh_old_set;
    }

    int result = real_pthread_sigmask(how, oh_set_pointer,
                                      oh_old_set_pointer);
    if (result == 0 && bionic_old_set != NULL) {
        memcpy(bionic_old_set, &oh_old_set, sizeof(*bionic_old_set));
    }
    return result;
}

int sigemptyset(sigset_t *set)
{
    if (caller_uses_bionic_signal_abi(__builtin_return_address(0), NULL)) {
        return wl_emptyset((westlake_webview_bionic_sigset_t *)set);
    }
    typedef int (*SigemptysetFn)(sigset_t *);
    static SigemptysetFn real_sigemptyset;
    if (real_sigemptyset == NULL) {
        real_sigemptyset = (SigemptysetFn)dlsym(RTLD_NEXT, "sigemptyset");
    }
    if (real_sigemptyset == NULL) {
        errno = ENOSYS;
        return -1;
    }
    return real_sigemptyset(set);
}

int sigfillset(sigset_t *set)
{
    if (caller_uses_bionic_signal_abi(__builtin_return_address(0), NULL)) {
        return wl_fillset((westlake_webview_bionic_sigset_t *)set);
    }
    typedef int (*SigfillsetFn)(sigset_t *);
    static SigfillsetFn real_sigfillset;
    if (real_sigfillset == NULL) {
        real_sigfillset = (SigfillsetFn)dlsym(RTLD_NEXT, "sigfillset");
    }
    if (real_sigfillset == NULL) {
        errno = ENOSYS;
        return -1;
    }
    return real_sigfillset(set);
}

int sigaddset(sigset_t *set, int signal_number)
{
    if (caller_uses_bionic_signal_abi(__builtin_return_address(0), NULL)) {
        return wl_addset((westlake_webview_bionic_sigset_t *)set,
                         signal_number);
    }
    typedef int (*SigaddsetFn)(sigset_t *, int);
    static SigaddsetFn real_sigaddset;
    if (real_sigaddset == NULL) {
        real_sigaddset = (SigaddsetFn)dlsym(RTLD_NEXT, "sigaddset");
    }
    if (real_sigaddset == NULL) {
        errno = ENOSYS;
        return -1;
    }
    return real_sigaddset(set, signal_number);
}

int sigdelset(sigset_t *set, int signal_number)
{
    if (caller_uses_bionic_signal_abi(__builtin_return_address(0), NULL)) {
        if (set == NULL ||
            !westlake_bionic_signal_number_is_valid(signal_number)) {
            errno = EINVAL;
            return -1;
        }
        *(westlake_webview_bionic_sigset_t *)set &=
                ~(UINT64_C(1) << (signal_number - 1));
        return 0;
    }
    typedef int (*SigdelsetFn)(sigset_t *, int);
    static SigdelsetFn real_sigdelset;
    if (real_sigdelset == NULL) {
        real_sigdelset = (SigdelsetFn)dlsym(RTLD_NEXT, "sigdelset");
    }
    if (real_sigdelset == NULL) {
        errno = ENOSYS;
        return -1;
    }
    return real_sigdelset(set, signal_number);
}

int sigismember(const sigset_t *set, int signal_number)
{
    if (caller_uses_bionic_signal_abi(__builtin_return_address(0), NULL)) {
        return wl_ismember(
                (const westlake_webview_bionic_sigset_t *)set,
                signal_number);
    }
    typedef int (*SigismemberFn)(const sigset_t *, int);
    static SigismemberFn real_sigismember;
    if (real_sigismember == NULL) {
        real_sigismember =
                (SigismemberFn)dlsym(RTLD_NEXT, "sigismember");
    }
    if (real_sigismember == NULL) {
        errno = ENOSYS;
        return -1;
    }
    return real_sigismember(set, signal_number);
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *old_set)
{
    if (caller_uses_bionic_signal_abi(__builtin_return_address(0), NULL)) {
        return wl_pthread_mask(
                how, (const westlake_webview_bionic_sigset_t *)set,
                (westlake_webview_bionic_sigset_t *)old_set);
    }
    typedef int (*PthreadSigmaskFn)(int, const sigset_t *, sigset_t *);
    static PthreadSigmaskFn real_pthread_sigmask;
    if (real_pthread_sigmask == NULL) {
        real_pthread_sigmask =
                (PthreadSigmaskFn)dlsym(RTLD_NEXT, "pthread_sigmask");
    }
    if (real_pthread_sigmask == NULL) {
        return ENOSYS;
    }
    return real_pthread_sigmask(how, set, old_set);
}

/*
 * sigprocmask takes the same 8-byte bionic set as pthread_sigmask. Untranslated, musl copies a
 * 128-byte old mask into it. Instagram's libraries import it; it reports failure through errno,
 * where pthread_sigmask returns the error.
 */
int sigprocmask(int how, const sigset_t *set, sigset_t *old_set)
{
    if (caller_uses_bionic_signal_abi(__builtin_return_address(0), NULL)) {
        int error = wl_pthread_mask(how, (const westlake_webview_bionic_sigset_t *)set,
                                    (westlake_webview_bionic_sigset_t *)old_set);
        if (error == 0) return 0;
        errno = error;
        return -1;
    }
    typedef int (*SigprocmaskFn)(int, const sigset_t *, sigset_t *);
    static SigprocmaskFn real_sigprocmask;
    if (real_sigprocmask == NULL) {
        real_sigprocmask = (SigprocmaskFn)dlsym(RTLD_NEXT, "sigprocmask");
    }
    if (real_sigprocmask == NULL) {
        errno = ENOSYS;
        return -1;
    }
    return real_sigprocmask(how, set, old_set);
}

int wl_sigact(int signal_number,
              const struct westlake_webview_bionic_sigaction *bionic_action,
              struct westlake_webview_bionic_sigaction *bionic_old_action)
{
    typedef int (*SigactionFn)(int, const struct sigaction *,
                               struct sigaction *);
    static SigactionFn real_sigaction;
    if (real_sigaction == NULL) {
        real_sigaction = (SigactionFn)dlsym(RTLD_NEXT, "sigaction");
    }
    if (real_sigaction == NULL) {
        errno = ENOSYS;
        return -1;
    }

    struct sigaction oh_action;
    struct sigaction oh_old_action;
    const struct sigaction *oh_action_pointer = NULL;
    struct sigaction *oh_old_action_pointer = NULL;
    if (bionic_action != NULL) {
        memset(&oh_action, 0, sizeof(oh_action));
        oh_action.sa_flags = bionic_action->flags;
        memcpy(&oh_action.sa_mask, &bionic_action->mask,
               sizeof(bionic_action->mask));
        if ((bionic_action->flags & SA_SIGINFO) != 0) {
            oh_action.sa_sigaction = bionic_action->callback.sigaction;
        } else {
            oh_action.sa_handler = bionic_action->callback.handler;
        }
        oh_action.sa_restorer = bionic_action->restorer;
        oh_action_pointer = &oh_action;
    }
    if (bionic_old_action != NULL) {
        memset(&oh_old_action, 0, sizeof(oh_old_action));
        oh_old_action_pointer = &oh_old_action;
    }

    int result = real_sigaction(signal_number, oh_action_pointer,
                                oh_old_action_pointer);
    int saved_errno = errno;
    if (result == 0 && bionic_old_action != NULL) {
        memset(bionic_old_action, 0, sizeof(*bionic_old_action));
        bionic_old_action->flags = oh_old_action.sa_flags;
        memcpy(&bionic_old_action->mask, &oh_old_action.sa_mask,
               sizeof(bionic_old_action->mask));
        if ((oh_old_action.sa_flags & SA_SIGINFO) != 0) {
            bionic_old_action->callback.sigaction = oh_old_action.sa_sigaction;
        } else {
            bionic_old_action->callback.handler = oh_old_action.sa_handler;
        }
        bionic_old_action->restorer = oh_old_action.sa_restorer;
    }

    static unsigned int call_count;
    unsigned int call = __atomic_add_fetch(&call_count, 1, __ATOMIC_RELAXED);
    if (call <= 64) {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-SIGNAL] sigaction #%u signal=%d "
                "flags=%#x callback=%p rc=%d errno=%d\n",
                call, signal_number,
                bionic_action != NULL ? bionic_action->flags : 0,
                bionic_action != NULL
                        ? (void *)bionic_action->callback.handler
                        : NULL,
                result, saved_errno);
    }
    errno = saved_errno;
    return result;
}

/*
 * Chromium normally reaches sigaction through its ELF import, which the
 * deployment patch renames to wl_sigact. Some signal-management paths resolve
 * the public name dynamically, though, bypassing that static import rewrite.
 * Passing their bionic struct directly to OH musl stores the flags word in the
 * signal-chain handler slot; the next signal then branches to values such as
 * 0x18000004. Interpose the public name as well, but translate only a direct
 * Android WebView caller. Every OH-native caller is forwarded unchanged.
 */
int sigaction(int signal_number, const struct sigaction *action,
              struct sigaction *old_action)
{
    if (caller_uses_bionic_signal_abi(__builtin_return_address(0), NULL)) {
        return wl_sigact(
                signal_number,
                (const struct westlake_webview_bionic_sigaction *)action,
                (struct westlake_webview_bionic_sigaction *)old_action);
    }

    typedef int (*SigactionFn)(int, const struct sigaction *,
                               struct sigaction *);
    static SigactionFn real_sigaction;
    if (real_sigaction == NULL) {
        real_sigaction = (SigactionFn)dlsym(RTLD_NEXT, "sigaction");
    }
    if (real_sigaction == NULL) {
        errno = ENOSYS;
        return -1;
    }
    return real_sigaction(signal_number, action, old_action);
}

/*
 * Chromium is Android-built, so its struct addrinfo uses Bionic's pointer
 * order: ai_canonname then ai_addr. OH musl reverses those two members while
 * retaining the same total size. The default OH namespace resolves ordinary
 * getaddrinfo imports to libc before an LD_PRELOAD interposer, and moving the
 * complete WebView DSO into the Android-native namespace breaks its JNI/load
 * group. Keep WebView in its proven namespace and expose uniquely named entry
 * points for an equal-length ELF import rewrite at deployment time.
 */
struct westlake_webview_bionic_addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    char *ai_canonname;
    struct sockaddr *ai_addr;
    struct westlake_webview_bionic_addrinfo *ai_next;
};

_Static_assert(sizeof(struct westlake_webview_bionic_addrinfo) ==
                       sizeof(struct addrinfo),
               "Bionic and OH addrinfo sizes must match");
_Static_assert(offsetof(struct westlake_webview_bionic_addrinfo,
                        ai_canonname) == offsetof(struct addrinfo, ai_addr),
               "Bionic canonname must occupy OH addr slot");
_Static_assert(offsetof(struct westlake_webview_bionic_addrinfo, ai_addr) ==
                       offsetof(struct addrinfo, ai_canonname),
               "Bionic addr must occupy OH canonname slot");

static void westlake_webview_free_addrinfo(
        struct westlake_webview_bionic_addrinfo *entry)
{
    while (entry != NULL) {
        struct westlake_webview_bionic_addrinfo *next = entry->ai_next;
        free(entry->ai_canonname);
        free(entry->ai_addr);
        free(entry);
        entry = next;
    }
}

/*
 * Resolve the OH resolver from the already-loaded host libc itself.  A plain
 * getaddrinfo() call from this preload can otherwise be preempted by another
 * Android boundary interposer.  That is particularly harmful here: the
 * generic native-network shim decides whether to translate addrinfo from its
 * immediate return address, which is this shim rather than Chromium, and a
 * transient negative result is then cached by Chromium's host resolver.
 *
 * The result returned by this function is always the OH/musl layout and is
 * copied into Bionic layout below before it crosses back into WebView.
 */
typedef int (*westlake_webview_getaddrinfo_fn)(
        const char *, const char *, const struct addrinfo *,
        struct addrinfo **);
typedef void (*westlake_webview_freeaddrinfo_fn)(struct addrinfo *);

static pthread_once_t westlake_webview_libc_once = PTHREAD_ONCE_INIT;
static void *westlake_webview_libc_handle;
static westlake_webview_getaddrinfo_fn westlake_webview_host_getaddrinfo;
static westlake_webview_freeaddrinfo_fn westlake_webview_host_freeaddrinfo;
static const char *westlake_webview_resolver_path = "(unresolved)";

static void westlake_webview_open_host_libc(void)
{
    westlake_webview_libc_handle =
            dlopen("libc.so", RTLD_NOW | RTLD_NOLOAD);
    if (westlake_webview_libc_handle == NULL) {
        westlake_webview_libc_handle =
                dlopen("/system/lib64/libc.so", RTLD_NOW | RTLD_NOLOAD);
    }
    /* OH's libc.so is a symlink to the dynamic loader.  Keep every spelling
     * NOLOAD-only: opening a different spelling normally can map a second
     * loader instance instead of returning the process's libc provider. */
    if (westlake_webview_libc_handle == NULL) {
        westlake_webview_libc_handle = dlopen(
                "/system/lib/ld-musl-aarch64.so.1",
                RTLD_NOW | RTLD_NOLOAD);
    }
    if (westlake_webview_libc_handle != NULL) {
        westlake_webview_host_getaddrinfo =
                (westlake_webview_getaddrinfo_fn)dlsym(
                        westlake_webview_libc_handle, "getaddrinfo");
        westlake_webview_host_freeaddrinfo =
                (westlake_webview_freeaddrinfo_fn)dlsym(
                        westlake_webview_libc_handle, "freeaddrinfo");
    }

    /* This DSO also exports a dlopen boundary wrapper, so an internal dlopen
     * can have no RTLD_NEXT provider when a test or provider loads the shim
     * after libc.  RTLD_DEFAULT is safe for these uniquely renamed WebView
     * entries: this DSO does not export ordinary getaddrinfo/freeaddrinfo.
     * It resolves either OH libc directly or the generic native-network
     * boundary.  The latter selects ABI translation by immediate caller and
     * therefore forwards this shim's calls in OH layout, exactly as required
     * before westlake_webview_copy_addrinfo converts the result to Bionic. */
    if (westlake_webview_host_getaddrinfo == NULL) {
        westlake_webview_host_getaddrinfo =
                (westlake_webview_getaddrinfo_fn)dlsym(
                        RTLD_DEFAULT, "getaddrinfo");
    }
    if (westlake_webview_host_freeaddrinfo == NULL) {
        westlake_webview_host_freeaddrinfo =
                (westlake_webview_freeaddrinfo_fn)dlsym(
                        RTLD_DEFAULT, "freeaddrinfo");
    }

    /* Keep a final fallback for unusual loader namespaces. */
    if (westlake_webview_host_getaddrinfo == NULL) {
        westlake_webview_host_getaddrinfo =
                (westlake_webview_getaddrinfo_fn)dlsym(
                        RTLD_NEXT, "getaddrinfo");
    }
    if (westlake_webview_host_freeaddrinfo == NULL) {
        westlake_webview_host_freeaddrinfo =
                (westlake_webview_freeaddrinfo_fn)dlsym(
                        RTLD_NEXT, "freeaddrinfo");
    }

    if (westlake_webview_host_getaddrinfo != NULL) {
        Dl_info info = {0};
        if (dladdr((void *)westlake_webview_host_getaddrinfo, &info) != 0 &&
            info.dli_fname != NULL) {
            westlake_webview_resolver_path = info.dli_fname;
        }
    }
}

static int westlake_webview_resolve_host(
        const char *node, const char *service, const struct addrinfo *hints,
        struct addrinfo **result)
{
    pthread_once(&westlake_webview_libc_once,
                 westlake_webview_open_host_libc);
    if (westlake_webview_host_getaddrinfo == NULL) return EAI_SYSTEM;
    return westlake_webview_host_getaddrinfo(node, service, hints, result);
}

static void westlake_webview_release_host_addrinfo(struct addrinfo *result)
{
    pthread_once(&westlake_webview_libc_once,
                 westlake_webview_open_host_libc);
    if (westlake_webview_host_freeaddrinfo != NULL) {
        westlake_webview_host_freeaddrinfo(result);
    }
}

static struct westlake_webview_bionic_addrinfo *
westlake_webview_copy_addrinfo(const struct addrinfo *source)
{
    struct westlake_webview_bionic_addrinfo *head = NULL;
    struct westlake_webview_bionic_addrinfo **tail = &head;
    for (; source != NULL; source = source->ai_next) {
        struct westlake_webview_bionic_addrinfo *entry =
                calloc(1, sizeof(*entry));
        if (entry == NULL) goto fail;
        entry->ai_flags = source->ai_flags;
        entry->ai_family = source->ai_family;
        entry->ai_socktype = source->ai_socktype;
        entry->ai_protocol = source->ai_protocol;
        entry->ai_addrlen = source->ai_addrlen;
        if (source->ai_canonname != NULL) {
            entry->ai_canonname = strdup(source->ai_canonname);
            if (entry->ai_canonname == NULL) {
                free(entry);
                goto fail;
            }
        }
        if (source->ai_addr != NULL && source->ai_addrlen != 0) {
            entry->ai_addr = malloc(source->ai_addrlen);
            if (entry->ai_addr == NULL) {
                free(entry->ai_canonname);
                free(entry);
                goto fail;
            }
            memcpy(entry->ai_addr, source->ai_addr, source->ai_addrlen);
        }
        *tail = entry;
        tail = &entry->ai_next;
    }
    return head;

fail:
    westlake_webview_free_addrinfo(head);
    return NULL;
}

/* Both exported names deliberately have the same byte length as the imports
 * they replace: getaddrinfo -> wl_getai_oh and freeaddrinfo -> wl_freeai_oh. */
int wl_getai_oh(const char *node, const char *service,
                const struct addrinfo *bionic_hints,
                struct addrinfo **bionic_result)
{
    struct addrinfo oh_hints;
    const struct addrinfo *real_hints = NULL;
    if (bionic_hints != NULL) {
        memset(&oh_hints, 0, sizeof(oh_hints));
        oh_hints.ai_flags = bionic_hints->ai_flags;
        oh_hints.ai_family = bionic_hints->ai_family;
        oh_hints.ai_socktype = bionic_hints->ai_socktype;
        oh_hints.ai_protocol = bionic_hints->ai_protocol;
        real_hints = &oh_hints;
    }

    struct addrinfo *oh_result = NULL;
    /*
     * Chromium can issue its first host lookup while the OH resolver service
     * is still settling after a network/reconnect notification.  OH musl has
     * returned both EAI_AGAIN and EAI_NONAME for that transient window, even
     * though the same hostname resolves immediately afterwards through the
     * same getaddrinfo entry point.  Chromium caches the first negative result
     * long enough to leave a newly opened WebView permanently blank.
     *
     * Retry only name-resolution failures, keep the delay bounded, and retain
     * the original result for every other error.  This remains a generic
     * Android-to-OH boundary repair: no hostnames or applications are singled
     * out, and a genuinely absent name costs at most 300 ms.
     */
    int rc = 0;
    unsigned int attempts = 0;
    do {
        ++attempts;
        oh_result = NULL;
        rc = westlake_webview_resolve_host(
                node, service, real_hints, &oh_result);
        if (rc != EAI_AGAIN && rc != EAI_NONAME) break;
        if (attempts >= 3) break;
        usleep(attempts * 100 * 1000);
    } while (1);
    static unsigned int lookup_count;
    unsigned int lookup = __atomic_add_fetch(&lookup_count, 1,
                                              __ATOMIC_RELAXED);
    if (lookup <= 256) {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-NET] dns #%u host=%s service=%s "
                "family=%d socktype=%d attempts=%u rc=%d resolver=%s\n",
                lookup, node != NULL ? node : "(null)",
                service != NULL ? service : "(null)",
                real_hints != NULL ? real_hints->ai_family : 0,
                real_hints != NULL ? real_hints->ai_socktype : 0,
                attempts, rc, westlake_webview_resolver_path);
    }
    if (rc != 0) return rc;

    struct westlake_webview_bionic_addrinfo *translated =
            westlake_webview_copy_addrinfo(oh_result);
    westlake_webview_release_host_addrinfo(oh_result);
    if (translated == NULL && oh_result != NULL) {
        if (bionic_result != NULL) *bionic_result = NULL;
        return EAI_MEMORY;
    }
    if (bionic_result == NULL) {
        westlake_webview_free_addrinfo(translated);
        return EAI_FAIL;
    }
    *bionic_result = (struct addrinfo *)translated;

    return 0;
}

void wl_freeai_oh(struct addrinfo *result)
{
    westlake_webview_free_addrinfo(
            (struct westlake_webview_bionic_addrinfo *)result);
}

static pthread_once_t westlake_webview_dns_once = PTHREAD_ONCE_INIT;
static struct in_addr westlake_webview_ipv4_dns;
static int westlake_webview_has_ipv4_dns;

static int westlake_webview_equivalent_ipv4_dns(
        const struct in6_addr *ipv6, struct in_addr *ipv4)
{
    static const unsigned char google_primary_v6[16] = {
        0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x88,
    };
    static const unsigned char google_secondary_v6[16] = {
        0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x44,
    };

    if (memcmp(ipv6->s6_addr, google_primary_v6,
               sizeof(google_primary_v6)) == 0) {
        return inet_pton(AF_INET, "8.8.8.8", ipv4) == 1;
    }
    if (memcmp(ipv6->s6_addr, google_secondary_v6,
               sizeof(google_secondary_v6)) == 0) {
        return inet_pton(AF_INET, "8.8.4.4", ipv4) == 1;
    }
    return 0;
}

static void westlake_webview_read_ipv4_dns(void)
{
    FILE *resolver = fopen("/etc/resolv.conf", "r");
    if (resolver == NULL) return;

    char line[256];
    while (fgets(line, sizeof(line), resolver) != NULL) {
        char address[INET_ADDRSTRLEN];
        if (sscanf(line, " nameserver %15s", address) == 1 &&
            inet_pton(AF_INET, address, &westlake_webview_ipv4_dns) == 1) {
            westlake_webview_has_ipv4_dns = 1;
            break;
        }
    }
    fclose(resolver);
}

/* Equal-length import target for connect -> wl_conn.  sockaddr has the same
 * layout at this boundary. Chromium may select an IPv6 DNS server from the
 * Android network facade even when the OH board has only an IPv4 default
 * route. Retry only that immediate ENETUNREACH case through an IPv4-mapped
 * address. Prefer the OH host's configured IPv4 resolver: an equivalent
 * public-vendor address may be syntactically reachable but filtered by the
 * current network, while /etc/resolv.conf is the resolver that OH libc has
 * already selected for that network. Use the vendor equivalent only when the
 * host has no IPv4 resolver. All ordinary destinations and successful IPv6
 * connections remain untouched. */
int wl_conn(int fd, const struct sockaddr *address, socklen_t address_length)
{
    typedef int (*ConnectFn)(int, const struct sockaddr *, socklen_t);
    static ConnectFn real_connect;
    if (real_connect == NULL) {
        real_connect = (ConnectFn)dlsym(RTLD_NEXT, "connect");
    }
    if (real_connect == NULL) {
        errno = ENOSYS;
        return -1;
    }

    char host[INET6_ADDRSTRLEN] = "?";
    unsigned int port = 0;
    int family = address != NULL ? address->sa_family : 0;
    if (address != NULL && family == AF_INET &&
        address_length >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)address;
        inet_ntop(AF_INET, &in->sin_addr, host, sizeof(host));
        port = ntohs(in->sin_port);
    } else if (address != NULL && family == AF_INET6 &&
               address_length >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *in6 =
                (const struct sockaddr_in6 *)address;
        inet_ntop(AF_INET6, &in6->sin6_addr, host, sizeof(host));
        port = ntohs(in6->sin6_port);
    }

    int result = real_connect(fd, address, address_length);
    int saved_errno = errno;
    int used_dns_fallback = 0;
    char fallback_host[INET_ADDRSTRLEN] = "-";
    if (result == -1 && saved_errno == ENETUNREACH && family == AF_INET6 &&
        port == 53 && address_length >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *requested =
                (const struct sockaddr_in6 *)address;
        struct in_addr fallback_dns;
        pthread_once(&westlake_webview_dns_once,
                     westlake_webview_read_ipv4_dns);
        int has_fallback = westlake_webview_has_ipv4_dns;
        if (has_fallback) {
            fallback_dns = westlake_webview_ipv4_dns;
        } else {
            has_fallback = westlake_webview_equivalent_ipv4_dns(
                    &requested->sin6_addr, &fallback_dns);
        }
        if (has_fallback) {
            struct sockaddr_in6 mapped;
            memset(&mapped, 0, sizeof(mapped));
            mapped.sin6_family = AF_INET6;
            mapped.sin6_port = requested->sin6_port;
            mapped.sin6_addr.s6_addr[10] = 0xff;
            mapped.sin6_addr.s6_addr[11] = 0xff;
            memcpy(&mapped.sin6_addr.s6_addr[12],
                   &fallback_dns, sizeof(fallback_dns));
            inet_ntop(AF_INET, &fallback_dns,
                      fallback_host, sizeof(fallback_host));
            result = real_connect(fd, (const struct sockaddr *)&mapped,
                                  sizeof(mapped));
            saved_errno = errno;
            used_dns_fallback = 1;
        }
    }
    static unsigned int connect_count;
    unsigned int call = __atomic_add_fetch(&connect_count, 1,
                                            __ATOMIC_RELAXED);
    if (call <= 512) {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-NET] connect #%u fd=%d family=%d "
                "address=%s port=%u rc=%d errno=%d dns_v4_fallback=%s\n",
                call, fd, family, host, port, result,
                result == 0 ? 0 : saved_errno,
                used_dns_fallback ? fallback_host : "-");
    }
    errno = saved_errno;
    return result;
}

/*
 * WESTLAKE §734: select the working OHOS GLES entry-point library for the
 * Android-built WebView.
 *
 * Chromium asks Android's loader for libGLESv2.so.  On this board that name
 * resolves to /system/lib64/ndk/libGLESv2.so.  The NDK facade requires an
 * Android EGL thread hook table which OHOS's platform EGL does not install:
 * even with a valid current context, glGetString returns NULL and
 * glGetIntegerv leaves GL_MAX_VERTEX_ATTRIBS untouched.  The platform GLESv3
 * library exports the GLES2 ABI as well and reports the live Mali context
 * correctly through the same EGL display/context.
 *
 * Keep the translation at the Android/OH boundary and scope it to direct
 * libwebviewchromium callers.  OH-native code and every other library request
 * retain the system loader's normal behavior.
 */

/*
 * Libraries refused rather than loaded.
 *
 * Some app libraries are self-decrypting and inspect the host before unpacking.
 * When they judge the environment not to be stock Android they finish DT_INIT
 * without producing plaintext, and the first .init_array constructor then
 * executes ciphertext: SIGILL, raised inside dlopen, which no Java caller can
 * catch. The process dies.
 *
 * Returning NULL instead turns that uncatchable crash into the ordinary
 * UnsatisfiedLinkError such an SDK already handles, because a library missing
 * from the device is a case it must support. Measured on Android 11 with
 * libakamaibmp.so forced to fail: com.mcdonalds.app catches it, continues, and
 * reaches HomeDashboardActivity with no exception logged.
 *
 * Add an entry only with evidence for both halves: that the library traps on
 * this runtime, and that the app tolerates its absence. Toutiao's
 * libmetasec_ml.so was checked and loads cleanly here, so it is not listed.
 */
static const char *const g_refused_libraries[] = {
    "libakamaibmp.so", /* Akamai Bot Manager (CyberFend); telemetry only */
};

static int westlake_library_is_refused(const char *basename)
{
    if (basename == NULL) {
        return 0;
    }
    for (size_t i = 0; i < sizeof(g_refused_libraries) / sizeof(g_refused_libraries[0]); ++i) {
        if (strcmp(basename, g_refused_libraries[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int wl_needs_android_relocs(const char *path);
int wl_write_loadable_copy(const char *path, const char *target, int retag);
void wl_copy_needed_siblings(const char *path, const char *dir, int depth);
void *wl_dlopen_android_relocs(void *(*real_dlopen)(const char *, int), const char *path, const char *target,
                               int flags);
void wl_relocs_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* 1 for a path in the app's own data directory, outside code_cache (see westlake_load_from_code_cache). */
static int westlake_in_app_storage(const char *path)
{
    static const char *const roots[] = {"/data/data/", "/data/user/0/", "/data/user_de/0/"};
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        if (strncmp(path, roots[i], strlen(roots[i])) == 0) return strstr(path, "/code_cache/") == NULL;
    }
    return 0;
}

/*
 * An exec-allowed directory for a loadable copy of path: <app data>/code_cache/wl-exec for a
 * library in app storage (the one app-data directory OH lets an app map executable), else a
 * wl-exec directory beside the library (the runtime root, where apps' own libraries are staged,
 * is exec-allowed and writable). Created if missing.
 */
static int westlake_exec_copy_dir(const char *path, char *dir, size_t size)
{
    static const char *const roots[] = {"/data/data/", "/data/user/0/", "/data/user_de/0/"};
    const char *base = strrchr(path, '/');
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        size_t n = strlen(roots[i]);
        const char *slash = strncmp(path, roots[i], n) == 0 ? strchr(path + n, '/') : NULL;
        if (slash != NULL) {
            char parent[600];
            snprintf(parent, sizeof(parent), "%.*s/code_cache", (int)(slash - path), path);
            mkdir(parent, 0700);
            snprintf(dir, size, "%s/wl-exec", parent);
            mkdir(dir, 0700);
            return access(dir, W_OK) == 0 ? 0 : -1;
        }
    }
    if (base == NULL || base == path) return -1;
    snprintf(dir, size, "%.*s/wl-exec", (int)(base - path), path);
    mkdir(dir, 0700);
    return access(dir, W_OK) == 0 ? 0 : -1;
}

/*
 * A library with DT_ANDROID_RELR that the launcher did not stage (one the app wrote at run time) is
 * loaded from a copy with the tags renumbered (android_relocs.c). NULL, with the reason logged,
 * when that fails.
 */
static void *westlake_load_android_relocs(void *(*real_dlopen)(const char *, int), const char *path, int flags)
{
    char dir[512], target[768];
    const char *base = strrchr(path, '/');
    if (westlake_exec_copy_dir(path, dir, sizeof(dir)) != 0) {
        wl_relocs_log("[WESTLAKE-RELOCS] no writable code_cache for %s\n", path);
        return NULL;
    }
    snprintf(target, sizeof(target), "%s%s", dir, base);
    return wl_dlopen_android_relocs(real_dlopen, path, target, flags);
}

/*
 * Libraries the app writes into its own storage and loads by path (Meta's SoLoader: superpack
 * unpacks them into lib-compressed/ or files/, then System.load()s each, dependencies first).
 *
 * bionic resolves a DT_NEEDED name against libraries already loaded, whatever path loaded them.
 * OH's musl gives a library loaded by explicit path no short name (dynlink.c: "Add a shortname only
 * if name arg was not an explicit pathname"), so a later DT_NEEDED of that name is searched for on
 * the library path instead: WhatsApp's libfbjni.so bound OH's libc++ (no std::__ndk1) although the
 * app had just loaded its own libc++_shared.so, and Facebook's libstorer-jni.so could not find the
 * libxplat_... library loaded before it. OH also refuses to map app data executable outside
 * code_cache, and a copy may need its DT_ANDROID_RELR renumbered.
 *
 * So such a library, with the DT_NEEDED siblings beside it, is copied into
 * <app data>/code_cache/wl-exec and opened by bare name in a namespace of its own whose library
 * path starts there: musl finds it and its siblings by name and records their short names. The
 * namespace inherits everything the Android namespace ART's loader made for the APK's NDK libraries
 * holds (their libc++_shared.so), and from the default namespace only the C runtime. NULL when the
 * namespace cannot be made, so the other paths below take over.
 */
static void *westlake_load_app_library(const char *path, int flags)
{
    typedef void (*DlnsInitFn)(Dl_namespace *, const char *);
    typedef int (*DlnsCreate2Fn)(Dl_namespace *, const char *, int);
    typedef int (*DlnsGetFn)(const char *, Dl_namespace *);
    typedef int (*DlnsInheritFn)(Dl_namespace *, Dl_namespace *, const char *);
    typedef void *(*DlopenNsFn)(Dl_namespace *, const char *, int);
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static Dl_namespace ns;
    static int state; /* 0 not tried, 1 ready, -1 unavailable */
    static DlopenNsFn open_fn;
    char dir[512], target[768];
    const char *base = strrchr(path, '/');
    if (base == NULL || westlake_exec_copy_dir(path, dir, sizeof(dir)) != 0) return NULL;
    /*
     * When ART's loader made an Android namespace for the APK's NDK libraries, the library joins
     * them there: symbols resolve against the namespace's own libraries (libc++_shared.so, this
     * shim), which a second namespace inheriting them did not see (Messenger's libcore.so: "Error
     * relocating ... std::__ndk1::basic_string ... __system_property_read_callback"). Its copy goes
     * into the staged APK library directory, first on that namespace's search path, never over a
     * library the APK ships under the same name.
     */
    {
        static DlnsGetFn android_get;
        static DlopenNsFn android_open;
        const char *apk_lib_dir = getenv("ASX_NATIVE_LIB_DIR");
        Dl_namespace android;
        memset(&android, 0, sizeof(android));
        if (android_get == NULL) android_get = (DlnsGetFn) dlsym(RTLD_DEFAULT, "dlns_get");
        if (android_open == NULL) android_open = (DlopenNsFn) dlsym(RTLD_DEFAULT, "dlopen_ns");
        if (apk_lib_dir != NULL && android_get != NULL && android_open != NULL &&
            android_get("westlake_android_app", &android) == 0) {
            snprintf(target, sizeof(target), "%s%s", apk_lib_dir, base);
            if (wl_write_loadable_copy(path, target, wl_needs_android_relocs(path)) == 0) {
                wl_copy_needed_siblings(path, apk_lib_dir, 0);
                void *handle = android_open(&android, base + 1, flags);
                const char *error = handle == NULL ? dlerror() : NULL;
                wl_relocs_log("[WESTLAKE-LOADER] %s: %s by name in the Android namespace from %s%s%s\n", path,
                              handle != NULL ? "loaded" : "failed", apk_lib_dir, error != NULL ? ": " : "",
                              error != NULL ? error : "");
                if (handle != NULL) return handle;
            }
        }
    }
    pthread_mutex_lock(&lock);
    if (state == 0) {
        DlnsInitFn init_fn = (DlnsInitFn) dlsym(RTLD_DEFAULT, "dlns_init");
        DlnsCreate2Fn create_fn = (DlnsCreate2Fn) dlsym(RTLD_DEFAULT, "dlns_create2");
        DlnsGetFn get_fn = (DlnsGetFn) dlsym(RTLD_DEFAULT, "dlns_get");
        DlnsInheritFn inherit_fn = (DlnsInheritFn) dlsym(RTLD_DEFAULT, "dlns_inherit");
        open_fn = (DlopenNsFn) dlsym(RTLD_DEFAULT, "dlopen_ns");
        const char *android_path = getenv("WESTLAKE_ANDROID_NATIVE_SEARCH_PATH");
        const char *app_lib_dir = getenv("ASX_NATIVE_LIB_DIR");
        char search[4096];
        snprintf(search, sizeof(search), "%s:%s", dir,
                 android_path != NULL && android_path[0] != '\0' ? android_path
                 : app_lib_dir != NULL ? app_lib_dir : "/system/lib64");
        Dl_namespace parent, android;
        memset(&parent, 0, sizeof(parent));
        memset(&android, 0, sizeof(android));
        state = -1;
        if (init_fn != NULL && create_fn != NULL && get_fn != NULL && inherit_fn != NULL && open_fn != NULL &&
            get_fn(NULL, &parent) == 0) {
            init_fn(&ns, "westlake_app_libs");
            if (create_fn(&ns, search, 0) == 0) {
                /* The Android namespace, when the launch made one, first and whole. */
                if (get_fn("westlake_android_app", &android) == 0) inherit_fn(&ns, &android, NULL);
                if (inherit_fn(&ns, &parent, "libc.so:libdl.so:libm.so:libz.so:liblog.so") == 0) state = 1;
                /* This library, global there, as ART's loader does for its namespace: its bionic and
                 * NDK symbols (ASharedMemory_*, __assert, ...) must resolve for what loads here. */
                Dl_info self;
                if (state == 1 && dladdr((void *) &westlake_load_app_library, &self) != 0 && self.dli_fname != NULL &&
                    open_fn(&ns, self.dli_fname, RTLD_NOW | RTLD_GLOBAL) == NULL) {
                    state = -1;
                }
            }
        }
        wl_relocs_log("[WESTLAKE-LOADER] app library namespace %s (search %s)\n",
                      state == 1 ? "ready" : "unavailable", search);
    }
    int ready = state == 1;
    pthread_mutex_unlock(&lock);
    if (!ready) return NULL;
    snprintf(target, sizeof(target), "%s%s", dir, base);
    if (wl_write_loadable_copy(path, target, wl_needs_android_relocs(path)) != 0) {
        wl_relocs_log("[WESTLAKE-LOADER] could not copy %s to %s\n", path, target);
        return NULL;
    }
    wl_copy_needed_siblings(path, dir, 0);
    void *handle = open_fn(&ns, base + 1, flags);
    const char *error = handle == NULL ? dlerror() : NULL;
    wl_relocs_log("[WESTLAKE-LOADER] %s: %s by name from %s%s%s\n", path, handle != NULL ? "loaded" : "failed", dir,
                  error != NULL ? ": " : "", error != NULL ? error : "");
    return handle;
}

/*
 * A library an app copied into its own storage, loaded from there. Android maps such files
 * executable; OH's policy refuses every app-data directory but code_cache ("failed to map library
 * ... errno=13", measured by the harness's runtime-answers probe: files, cache and no_backup fail,
 * code_cache loads). Meta's SoLoader with superpack unpacks libraries into files/ and loads them there
 * (WhatsApp: files/decompressed/libs.spo/, every library of it). When such a load fails, the same bytes are
 * copied into <app data>/code_cache/wl-exec/ and loaded from there; a copy that already matches in
 * size and modification time is reused.
 */
static void *westlake_load_from_code_cache(void *(*real_dlopen)(const char *, int), const char *path, int flags)
{
    static const char *const roots[] = {"/data/data/", "/data/user/0/", "/data/user_de/0/"};
    const char *rest = NULL;
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]) && rest == NULL; ++i) {
        size_t n = strlen(roots[i]);
        if (strncmp(path, roots[i], n) == 0) rest = path + n;
    }
    if (rest == NULL || strstr(path, "/code_cache/") != NULL) return NULL;
    const char *slash = strchr(rest, '/');
    const char *base = strrchr(path, '/');
    if (slash == NULL || base == NULL) return NULL;
    char dir[512], target[768];
    snprintf(dir, sizeof(dir), "%.*s/code_cache/wl-exec", (int)(slash - path), path);
    struct stat src, dst;
    if (stat(path, &src) != 0) return NULL;
    char parent[512];
    snprintf(parent, sizeof(parent), "%.*s/code_cache", (int)(slash - path), path);
    mkdir(parent, 0700);
    mkdir(dir, 0700);
    snprintf(target, sizeof(target), "%s%s", dir, base);
    if (stat(target, &dst) != 0 || dst.st_size != src.st_size || dst.st_mtime < src.st_mtime) {
        char tmp[800];
        snprintf(tmp, sizeof(tmp), "%s.tmp%d", target, (int)getpid());
        int in = open(path, O_RDONLY | O_CLOEXEC);
        int out = in >= 0 ? open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0700) : -1;
        char buf[65536];
        ssize_t n = 0;
        while (in >= 0 && out >= 0 && (n = read(in, buf, sizeof(buf))) > 0) {
            if (write(out, buf, (size_t)n) != n) { n = -1; break; }
        }
        if (in >= 0) close(in);
        if (out >= 0) close(out);
        if (n != 0 || out < 0 || rename(tmp, target) != 0) {
            unlink(tmp);
            return NULL;
        }
    }
    void *handle = real_dlopen(target, flags);
    if (handle != NULL) {
        fprintf(stderr, "[WESTLAKE-LOADER] %s refused from app storage; loaded its copy in %s\n", path, dir);
    }
    return handle;
}

/*
 * bionic's linker entry behind dlopen, which libdl.so exports: open on behalf of caller_addr's
 * namespace. Facebook's libbreakpad.so looks it up by dlsym(dlopen("libdl.so"), "__loader_dlopen")
 * and gave up ("Could not find __loader_dlopen from libdl.so"). Namespaces are not modelled here;
 * the shim's dlopen decides.
 */
void *__loader_dlopen(const char *filename, int flags, const void *caller_addr)
{
    (void) caller_addr;
    return dlopen(filename, flags);
}

/*
 * A library inside an APK (<apk>!/lib/<abi>/<name>). OH's loader maps a stored zip member itself, so
 * such a load gets the APK's own bytes, without what the launcher fixes when it stages the same
 * library under lib/<abi>/: Android's RELR tags, null init array entries. Meta's SoLoader loads
 * straight from the split APK that way, and Threads died when libc++_shared.so's constructor jumped
 * to its link-time address. The staged copy is the same library, as Android would load it from an
 * extracted install. Returns it (in staged), or NULL to load the path as asked.
 */
static const char *westlake_staged_apk_member(const char *filename, char *staged, size_t size)
{
    const char *member = filename != NULL ? strstr(filename, ".apk!/lib/") : NULL;
    if (member == NULL) return NULL;
    const char *abi = member + strlen(".apk!/lib/");
    const char *slash = strchr(abi, '/');
    if (slash == NULL || slash[1] == '\0' || strchr(slash + 1, '/') != NULL) return NULL;
    if (snprintf(staged, size, "/data/local/tmp/asx/lib/%.*s/%s", (int) (slash - abi), abi, slash + 1) >= (int) size ||
        access(staged, R_OK) != 0) {
        return NULL;
    }
    fprintf(stderr, "[WESTLAKE-LOADER] %s: loading the staged copy %s\n", filename, staged);
    return staged;
}

/*
 * OH's dlopen into a namespace. ART's loader opens the libraries the launcher routes to the Android
 * namespace through it (looked up with dlsym(RTLD_DEFAULT), which finds this one), not through
 * dlopen: Threads' libc++_shared.so went that way, so the redirect above is applied here too.
 */
void *dlopen_ns(Dl_namespace *ns, const char *file, int mode)
{
    typedef void *(*DlopenNsFn)(Dl_namespace *, const char *, int);
    static DlopenNsFn real_dlopen_ns;
    if (real_dlopen_ns == NULL) real_dlopen_ns = (DlopenNsFn) dlsym(RTLD_NEXT, "dlopen_ns");
    if (real_dlopen_ns == NULL) {
        errno = ENOSYS;
        return NULL;
    }
    char staged[512];
    const char *redirected = westlake_staged_apk_member(file, staged, sizeof(staged));
    return real_dlopen_ns(ns, redirected != NULL ? redirected : file, mode);
}

void *dlopen(const char *filename, int flags)
{
    void *caller = __builtin_return_address(0);
    typedef void *(*DlopenFn)(const char *, int);
    static DlopenFn real_dlopen;
    if (real_dlopen == NULL) {
        real_dlopen = (DlopenFn)dlsym(RTLD_NEXT, "dlopen");
    }
    if (real_dlopen == NULL) {
        errno = ENOSYS;
        return NULL;
    }

    const char *actual_filename = filename;
    const char *basename = filename != NULL ? strrchr(filename, '/') : NULL;
    basename = basename != NULL ? basename + 1 : filename;
    if (basename != NULL && westlake_library_is_refused(basename)) {
        static int refused_logged;
        if (!__atomic_exchange_n(&refused_logged, 1, __ATOMIC_RELAXED)) {
            fprintf(stderr,
                    "[WESTLAKE-LOADER] refusing self-trapping library: %s\n",
                    filename);
        }
        return NULL;
    }
    char staged[512];
    if (westlake_staged_apk_member(filename, staged, sizeof(staged)) != NULL) {
        actual_filename = staged;
        basename = strrchr(staged, '/') + 1;
    }
    /*
     * OpenSL ES through dlopen. Oboe (Rhythm Engine) does not link libOpenSLES: it dlopens it and
     * looks up slCreateEngine and the SL_IID_* by handle, which reaches OH's library directly and
     * skips the Android-ABI adapter this shim exports -- OH then refused Oboe's engine interface
     * (SL_RESULT_FEATURE_UNSUPPORTED) and the app died. An app's dlopen of libOpenSLES.so is
     * answered with this shim's own handle. The shim depends on OH's libOpenSLES, and a lookup by
     * handle searches a library before its dependencies, so the adapter's slCreateEngine and
     * Android's own IDs are found here and every other SL_IID_* in OH's library behind it. The
     * adapter's own dlopen, from inside this shim, still gets OH's library.
     */
    /*
     * libdl.so, asked for by app code: this library's own handle, so a lookup by it finds bionic's
     * __loader_dlopen here (above) and, through this library's dependencies, everything musl's
     * libdl exports. OH's own libraries keep getting OH's libdl.
     */
    if (basename != NULL && strcmp(basename, "libdl.so") == 0) {
        Dl_info self, caller_info;
        if (dladdr((void *) &dlopen, &self) != 0 && self.dli_fname != NULL &&
            dladdr(caller, &caller_info) != 0 && caller_info.dli_fname != NULL &&
            caller_info.dli_fbase != self.dli_fbase &&
            strncmp(caller_info.dli_fname, "/system/", 8) != 0 &&
            strncmp(caller_info.dli_fname, "/vendor/", 8) != 0) {
            void *handle = real_dlopen(self.dli_fname, flags | RTLD_NOLOAD);
            if (handle != NULL) {
                return handle;
            }
        }
    }
    if (basename != NULL && strcmp(basename, "libOpenSLES.so") == 0) {
        Dl_info self, caller_info;
        if (dladdr((void *) &dlopen, &self) != 0 && self.dli_fname != NULL &&
            !(dladdr(caller, &caller_info) != 0 &&
              caller_info.dli_fbase == self.dli_fbase)) {
            void *handle = real_dlopen(self.dli_fname, flags | RTLD_NOLOAD);
            if (handle != NULL) {
                static int logged;
                if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) {
                    fprintf(stderr, "[WESTLAKE-SLES] dlopen %s -> the Android OpenSL ES adapter\n",
                            filename);
                }
                return handle;
            }
        }
    }
    /*
     * WebView's own libraries come from the WebView library directory, which the search path
     * lists last.  Every name in it also exists in the runtime directory, which comes first, so a
     * bare soname from a WebView caller resolves to the runtime's copy.  For libandroid.so that
     * means the whole NDK SurfaceControl surface goes missing -- the runtime's exports none of
     * its 27 entry points and the WebView one exports all of them -- and the same holds for
     * libjnigraphics.so, which the boundaries probe found in the identical position before
     * anything had tripped over it.
     *
     * So resolve any bare soname against that directory for WebView's callers, rather than
     * naming libraries one at a time: the directory holds exactly the libraries WebView is meant
     * to use, and a fix per library is a fix per outage.  Only bare names are redirected; a
     * caller that passes a path has already chosen.
     *
     * This must run BEFORE the .z.so probe below.  That probe opens the plain name first and
     * returns the handle when it succeeds, and for every name in the WebView directory the plain
     * name does succeed -- out of the runtime directory, which the search path lists first.  With
     * the probe in front, this block was unreachable for exactly the libraries it exists to fix:
     * NewPipe and AnkiDroid both lost all fourteen ASurfaceTransaction entry points Chromium
     * looks up and died on SIGTRAP in CrBrowserMain, with "library resolved" never once logged.
     */
    static char webview_library[512];
    if (basename != NULL && basename == filename &&
        caller_is_webview(caller, NULL)) {
        const char *dir = getenv("ASX_WEBVIEW_LIB_DIR");
        if (dir != NULL && dir[0] != '\0') {
            snprintf(webview_library, sizeof(webview_library), "%s/%s", dir, basename);
            if (access(webview_library, R_OK) == 0) {
                actual_filename = webview_library;
                fprintf(stderr,
                        "[WESTLAKE-WEBVIEW-BIONIC] library resolved %s -> %s for the WebView "
                        "caller\n",
                        filename, actual_filename);
            }
        }
    }
    /*
     * OH names several NDK libraries with a .z.so suffix that Android spells plainly, and an app
     * asking for the Android name gets "No such file or directory" even though the library is
     * there. LocalSend's Flutter engine looks up ANativeWindow_acquire and the AHardwareBuffer
     * family in libnativewindow.so; /vendor/lib64/libnativewindow.z.so exports all of them.
     *
     * Tried only after the plain name fails to open, so a board that does ship the Android
     * spelling keeps winning, and applied to any caller because the naming is the platform's, not
     * WebView's.  Opens actual_filename, so a WebView redirect decided above is what gets tried;
     * the .z.so spelling is only ever derived from a bare name the caller passed.
     */
    static char suffixed[512];
    if (basename != NULL && basename == filename) {
        size_t length = strlen(filename);
        if (length > 3 && strcmp(filename + length - 3, ".so") == 0 &&
            (length < 5 || strcmp(filename + length - 5, ".z.so") != 0)) {
            void *plain = real_dlopen(actual_filename, flags);
            if (plain != NULL) {
                return plain;
            }
            snprintf(suffixed, sizeof(suffixed), "%.*s.z.so", (int) (length - 3), filename);
            void *handle = real_dlopen(suffixed, flags);
            if (handle != NULL) {
                static int logged;
                if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) {
                    fprintf(stderr,
                            "[WESTLAKE-WEBVIEW-BIONIC] OH library naming: %s -> %s\n",
                            filename, suffixed);
                }
                return handle;
            }
        }
    }
    if (basename != NULL && strcmp(basename, "libGLESv2.so") == 0 &&
        caller_is_webview(caller, NULL)) {
        actual_filename = "/system/lib64/platformsdk/libGLESv3.so";
        static int logged;
        if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) {
            fprintf(stderr,
                    "[WESTLAKE-WEBVIEW-BIONIC] GLES library translated "
                    "%s -> %s for libwebviewchromium.so\n",
                    filename, actual_filename);
        }
    }
    if (actual_filename != NULL && actual_filename[0] == '/' && !(flags & RTLD_NOLOAD) &&
        westlake_in_app_storage(actual_filename)) {
        void *namespaced = westlake_load_app_library(actual_filename, flags);
        if (namespaced != NULL) {
            return namespaced;
        }
    }
    if (actual_filename != NULL && actual_filename[0] == '/' && !(flags & RTLD_NOLOAD) &&
        wl_needs_android_relocs(actual_filename)) {
        /* Never the original: musl would leave its RELR pointers unrelocated (SIGSEGV). */
        return westlake_load_android_relocs(real_dlopen, actual_filename, flags);
    }
    void *handle = real_dlopen(actual_filename, flags);
    if (handle == NULL && actual_filename != NULL && actual_filename[0] == '/') {
        void *copy = westlake_load_from_code_cache(real_dlopen, actual_filename, flags);
        if (copy != NULL) {
            return copy;
        }
    }
    return handle;
}

/*
 * bionic's extended dlopen. PPSSPP's libppsspp_jni.so imports android_dlopen_ext@LIBC, OH's musl
 * loader has no such symbol, and the whole library failed to relocate -- the emulator reported
 * "LoadLibrary failed" before it could draw anything.
 *
 * The extinfo fields are mostly hints this loader cannot act on and the caller does not depend on:
 * a reserved address (with or without HINT), RELRO sharing, and the linker namespace all describe
 * where or how bionic should load, not what. Those are ignored and the library is opened normally,
 * through the dlopen above so OH's .z.so naming still applies. A library passed by file descriptor
 * is opened via /proc/self/fd. Loading at an offset inside that file (an uncompressed .so inside an
 * APK) is the one request this cannot meet, and it fails with that reason rather than opening the
 * wrong bytes.
 */
/* SL_IID_ANDROIDSIMPLEBUFFERQUEUE and the buffer queue behind it: opensles_android_adapter.c. */

typedef struct {
    uint64_t flags;
    void *reserved_addr;
    size_t reserved_size;
    int relro_fd;
    int library_fd;
    int64_t library_fd_offset;
    void *library_namespace;
} westlake_android_dlextinfo;

#define WESTLAKE_DLEXT_USE_LIBRARY_FD        0x10
#define WESTLAKE_DLEXT_USE_LIBRARY_FD_OFFSET 0x20

static __thread char westlake_dlext_error[160];

void *android_dlopen_ext(const char *filename, int flags, const westlake_android_dlextinfo *extinfo)
{
    if (extinfo != NULL && (extinfo->flags & WESTLAKE_DLEXT_USE_LIBRARY_FD)) {
        if ((extinfo->flags & WESTLAKE_DLEXT_USE_LIBRARY_FD_OFFSET) && extinfo->library_fd_offset != 0) {
            snprintf(westlake_dlext_error, sizeof(westlake_dlext_error),
                     "android_dlopen_ext: loading %s at offset %lld in fd %d is not supported",
                     filename != NULL ? filename : "(fd)", (long long) extinfo->library_fd_offset,
                     extinfo->library_fd);
            fprintf(stderr, "[WESTLAKE-BIONIC] %s\n", westlake_dlext_error);
            errno = ENOSYS;
            return NULL;
        }
        char path[64];
        snprintf(path, sizeof(path), "/proc/self/fd/%d", extinfo->library_fd);
        return dlopen(path, flags);
    }
    return dlopen(filename, flags);
}


/*
 * WESTLAKE §680: translate Android/bionic sysconf selectors at the libc boundary.
 *
 * Bionic and OH musl number sysconf selectors independently: bionic's _SC_PAGESIZE is 39, and
 * musl's 39 is _SC_BC_STRING_MAX, so an Android-built caller asking for the page size gets 1000.
 * Chromium's allocator traps on that; McDonald's Realm rounds its mmap offsets with it and the
 * kernel rejects the unaligned offset (EINVAL) when the home dashboard opens its database. The CPU
 * count, physical-memory and clock-tick selectors differ the same way.
 *
 * This shim is process-wide through LD_PRELOAD, and OH-native libraries legitimately use the
 * musl numbering, so only direct callers built against bionic are translated: the WebView DSO and
 * the app's own libraries. Everything else goes to musl unchanged.
 */
/*
 * Bionic sysconf selector -> OH musl selector, matched by name (generated from the NDK r25
 * sysroot's bits/sysconf.h and the OH 6.1 SDK's musl unistd.h). -2: a cache-geometry query
 * musl lacks, which bionic itself answers 0 when unknown; -1: no musl counterpart.
 */
static const short westlake_bionic_sysconf[] = {
    [0x0000] = 0,  /* _SC_ARG_MAX */
    [0x0001] = 36,  /* _SC_BC_BASE_MAX */
    [0x0002] = 37,  /* _SC_BC_DIM_MAX */
    [0x0003] = 38,  /* _SC_BC_SCALE_MAX */
    [0x0004] = 39,  /* _SC_BC_STRING_MAX */
    [0x0005] = 1,  /* _SC_CHILD_MAX */
    [0x0006] = 2,  /* _SC_CLK_TCK */
    [0x0007] = 40,  /* _SC_COLL_WEIGHTS_MAX */
    [0x0008] = 42,  /* _SC_EXPR_NEST_MAX */
    [0x0009] = 43,  /* _SC_LINE_MAX */
    [0x000a] = 3,  /* _SC_NGROUPS_MAX */
    [0x000b] = 4,  /* _SC_OPEN_MAX */
    [0x000c] = 88,  /* _SC_PASS_MAX */
    [0x000d] = 47,  /* _SC_2_C_BIND */
    [0x000e] = 48,  /* _SC_2_C_DEV */
    [0x000f] = -1,  /* _SC_2_C_VERSION */
    [0x0010] = 95,  /* _SC_2_CHAR_TERM */
    [0x0011] = 49,  /* _SC_2_FORT_DEV */
    [0x0012] = 50,  /* _SC_2_FORT_RUN */
    [0x0013] = 52,  /* _SC_2_LOCALEDEF */
    [0x0014] = 51,  /* _SC_2_SW_DEV */
    [0x0015] = 97,  /* _SC_2_UPE */
    [0x0016] = 46,  /* _SC_2_VERSION */
    [0x0017] = 7,  /* _SC_JOB_CONTROL */
    [0x0018] = 8,  /* _SC_SAVED_IDS */
    [0x0019] = 29,  /* _SC_VERSION */
    [0x001a] = 44,  /* _SC_RE_DUP_MAX */
    [0x001b] = 5,  /* _SC_STREAM_MAX */
    [0x001c] = 6,  /* _SC_TZNAME_MAX */
    [0x001d] = 92,  /* _SC_XOPEN_CRYPT */
    [0x001e] = 93,  /* _SC_XOPEN_ENH_I18N */
    [0x001f] = 94,  /* _SC_XOPEN_SHM */
    [0x0020] = 89,  /* _SC_XOPEN_VERSION */
    [0x0021] = 90,  /* _SC_XOPEN_XCU_VERSION */
    [0x0022] = 130,  /* _SC_XOPEN_REALTIME */
    [0x0023] = 131,  /* _SC_XOPEN_REALTIME_THREADS */
    [0x0024] = 129,  /* _SC_XOPEN_LEGACY */
    [0x0025] = 87,  /* _SC_ATEXIT_MAX */
    [0x0026] = 60,  /* _SC_IOV_MAX */
    [0x0027] = 30,  /* _SC_PAGESIZE */
    [0x0028] = 30,  /* _SC_PAGE_SIZE */
    [0x0029] = 91,  /* _SC_XOPEN_UNIX */
    [0x002a] = 125,  /* _SC_XBS5_ILP32_OFF32 */
    [0x002b] = 126,  /* _SC_XBS5_ILP32_OFFBIG */
    [0x002c] = 127,  /* _SC_XBS5_LP64_OFF64 */
    [0x002d] = 128,  /* _SC_XBS5_LPBIG_OFFBIG */
    [0x002e] = 23,  /* _SC_AIO_LISTIO_MAX */
    [0x002f] = 24,  /* _SC_AIO_MAX */
    [0x0030] = 25,  /* _SC_AIO_PRIO_DELTA_MAX */
    [0x0031] = 26,  /* _SC_DELAYTIMER_MAX */
    [0x0032] = 27,  /* _SC_MQ_OPEN_MAX */
    [0x0033] = 28,  /* _SC_MQ_PRIO_MAX */
    [0x0034] = 31,  /* _SC_RTSIG_MAX */
    [0x0035] = 32,  /* _SC_SEM_NSEMS_MAX */
    [0x0036] = 33,  /* _SC_SEM_VALUE_MAX */
    [0x0037] = 34,  /* _SC_SIGQUEUE_MAX */
    [0x0038] = 35,  /* _SC_TIMER_MAX */
    [0x0039] = 12,  /* _SC_ASYNCHRONOUS_IO */
    [0x003a] = 15,  /* _SC_FSYNC */
    [0x003b] = 16,  /* _SC_MAPPED_FILES */
    [0x003c] = 17,  /* _SC_MEMLOCK */
    [0x003d] = 18,  /* _SC_MEMLOCK_RANGE */
    [0x003e] = 19,  /* _SC_MEMORY_PROTECTION */
    [0x003f] = 20,  /* _SC_MESSAGE_PASSING */
    [0x0040] = 13,  /* _SC_PRIORITIZED_IO */
    [0x0041] = 10,  /* _SC_PRIORITY_SCHEDULING */
    [0x0042] = 9,  /* _SC_REALTIME_SIGNALS */
    [0x0043] = 21,  /* _SC_SEMAPHORES */
    [0x0044] = 22,  /* _SC_SHARED_MEMORY_OBJECTS */
    [0x0045] = 14,  /* _SC_SYNCHRONIZED_IO */
    [0x0046] = 11,  /* _SC_TIMERS */
    [0x0047] = 69,  /* _SC_GETGR_R_SIZE_MAX */
    [0x0048] = 70,  /* _SC_GETPW_R_SIZE_MAX */
    [0x0049] = 71,  /* _SC_LOGIN_NAME_MAX */
    [0x004a] = 73,  /* _SC_THREAD_DESTRUCTOR_ITERATIONS */
    [0x004b] = 74,  /* _SC_THREAD_KEYS_MAX */
    [0x004c] = 75,  /* _SC_THREAD_STACK_MIN */
    [0x004d] = 76,  /* _SC_THREAD_THREADS_MAX */
    [0x004e] = 72,  /* _SC_TTY_NAME_MAX */
    [0x004f] = 67,  /* _SC_THREADS */
    [0x0050] = 77,  /* _SC_THREAD_ATTR_STACKADDR */
    [0x0051] = 78,  /* _SC_THREAD_ATTR_STACKSIZE */
    [0x0052] = 79,  /* _SC_THREAD_PRIORITY_SCHEDULING */
    [0x0053] = 80,  /* _SC_THREAD_PRIO_INHERIT */
    [0x0054] = 81,  /* _SC_THREAD_PRIO_PROTECT */
    [0x0055] = 68,  /* _SC_THREAD_SAFE_FUNCTIONS */
    [0x0056] = -1,
    [0x0057] = -1,
    [0x0058] = -1,
    [0x0059] = -1,
    [0x005a] = -1,
    [0x005b] = -1,
    [0x005c] = -1,
    [0x005d] = -1,
    [0x005e] = -1,
    [0x005f] = -1,
    [0x0060] = 83,  /* _SC_NPROCESSORS_CONF */
    [0x0061] = 84,  /* _SC_NPROCESSORS_ONLN */
    [0x0062] = 85,  /* _SC_PHYS_PAGES */
    [0x0063] = 86,  /* _SC_AVPHYS_PAGES */
    [0x0064] = 149,  /* _SC_MONOTONIC_CLOCK */
    [0x0065] = 168,  /* _SC_2_PBS */
    [0x0066] = 169,  /* _SC_2_PBS_ACCOUNTING */
    [0x0067] = 175,  /* _SC_2_PBS_CHECKPOINT */
    [0x0068] = 170,  /* _SC_2_PBS_LOCATE */
    [0x0069] = 171,  /* _SC_2_PBS_MESSAGE */
    [0x006a] = 172,  /* _SC_2_PBS_TRACK */
    [0x006b] = 132,  /* _SC_ADVISORY_INFO */
    [0x006c] = 133,  /* _SC_BARRIERS */
    [0x006d] = 137,  /* _SC_CLOCK_SELECTION */
    [0x006e] = 138,  /* _SC_CPUTIME */
    [0x006f] = 180,  /* _SC_HOST_NAME_MAX */
    [0x0070] = 235,  /* _SC_IPV6 */
    [0x0071] = 236,  /* _SC_RAW_SOCKETS */
    [0x0072] = 153,  /* _SC_READER_WRITER_LOCKS */
    [0x0073] = 155,  /* _SC_REGEXP */
    [0x0074] = 157,  /* _SC_SHELL */
    [0x0075] = 159,  /* _SC_SPAWN */
    [0x0076] = 154,  /* _SC_SPIN_LOCKS */
    [0x0077] = 160,  /* _SC_SPORADIC_SERVER */
    [0x0078] = 241,  /* _SC_SS_REPL_MAX */
    [0x0079] = 173,  /* _SC_SYMLOOP_MAX */
    [0x007a] = 139,  /* _SC_THREAD_CPUTIME */
    [0x007b] = 82,  /* _SC_THREAD_PROCESS_SHARED */
    [0x007c] = 247,  /* _SC_THREAD_ROBUST_PRIO_INHERIT */
    [0x007d] = 248,  /* _SC_THREAD_ROBUST_PRIO_PROTECT */
    [0x007e] = 161,  /* _SC_THREAD_SPORADIC_SERVER */
    [0x007f] = 164,  /* _SC_TIMEOUTS */
    [0x0080] = 181,  /* _SC_TRACE */
    [0x0081] = 182,  /* _SC_TRACE_EVENT_FILTER */
    [0x0082] = 242,  /* _SC_TRACE_EVENT_NAME_MAX */
    [0x0083] = 183,  /* _SC_TRACE_INHERIT */
    [0x0084] = 184,  /* _SC_TRACE_LOG */
    [0x0085] = 243,  /* _SC_TRACE_NAME_MAX */
    [0x0086] = 244,  /* _SC_TRACE_SYS_MAX */
    [0x0087] = 245,  /* _SC_TRACE_USER_EVENT_MAX */
    [0x0088] = 165,  /* _SC_TYPED_MEMORY_OBJECTS */
    [0x0089] = 237,  /* _SC_V7_ILP32_OFF32 */
    [0x008a] = 238,  /* _SC_V7_ILP32_OFFBIG */
    [0x008b] = 239,  /* _SC_V7_LP64_OFF64 */
    [0x008c] = 240,  /* _SC_V7_LPBIG_OFFBIG */
    [0x008d] = 246,  /* _SC_XOPEN_STREAMS */
    [0x008e] = -1,  /* _SC_XOPEN_UUCP */
    [0x008f] = -2,  /* _SC_LEVEL1_ICACHE_SIZE */
    [0x0090] = -2,  /* _SC_LEVEL1_ICACHE_ASSOC */
    [0x0091] = -2,  /* _SC_LEVEL1_ICACHE_LINESIZE */
    [0x0092] = -2,  /* _SC_LEVEL1_DCACHE_SIZE */
    [0x0093] = -2,  /* _SC_LEVEL1_DCACHE_ASSOC */
    [0x0094] = -2,  /* _SC_LEVEL1_DCACHE_LINESIZE */
    [0x0095] = -2,  /* _SC_LEVEL2_CACHE_SIZE */
    [0x0096] = -2,  /* _SC_LEVEL2_CACHE_ASSOC */
    [0x0097] = -2,  /* _SC_LEVEL2_CACHE_LINESIZE */
    [0x0098] = -2,  /* _SC_LEVEL3_CACHE_SIZE */
    [0x0099] = -2,  /* _SC_LEVEL3_CACHE_ASSOC */
    [0x009a] = -2,  /* _SC_LEVEL3_CACHE_LINESIZE */
    [0x009b] = -2,  /* _SC_LEVEL4_CACHE_SIZE */
    [0x009c] = -2,  /* _SC_LEVEL4_CACHE_ASSOC */
    [0x009d] = -2,  /* _SC_LEVEL4_CACHE_LINESIZE */
};

/*
 * Bionic pathconf selector -> OH musl selector, matched by name (NDK r25 unistd.h and the
 * OH 6.1 SDK's musl unistd.h). -1: no musl counterpart. Every value is shifted: bionic's
 * _PC_NAME_MAX is 4, musl's is 3, so an untranslated call answers a different limit.
 */
static const short westlake_bionic_pathconf[] = {
    [0x00] = 13,  /* _PC_FILESIZEBITS */
    [0x01] = 0,  /* _PC_LINK_MAX */
    [0x02] = 1,  /* _PC_MAX_CANON */
    [0x03] = 2,  /* _PC_MAX_INPUT */
    [0x04] = 3,  /* _PC_NAME_MAX */
    [0x05] = 4,  /* _PC_PATH_MAX */
    [0x06] = 5,  /* _PC_PIPE_BUF */
    [0x07] = 20,  /* _PC_2_SYMLINKS */
    [0x08] = 18,  /* _PC_ALLOC_SIZE_MIN */
    [0x09] = 14,  /* _PC_REC_INCR_XFER_SIZE */
    [0x0a] = 15,  /* _PC_REC_MAX_XFER_SIZE */
    [0x0b] = 16,  /* _PC_REC_MIN_XFER_SIZE */
    [0x0c] = 17,  /* _PC_REC_XFER_ALIGN */
    [0x0d] = 19,  /* _PC_SYMLINK_MAX */
    [0x0e] = 6,  /* _PC_CHOWN_RESTRICTED */
    [0x0f] = 7,  /* _PC_NO_TRUNC */
    [0x10] = 8,  /* _PC_VDISABLE */
    [0x11] = 10,  /* _PC_ASYNC_IO */
    [0x12] = 11,  /* _PC_PRIO_IO */
    [0x13] = 9,  /* _PC_SYNC_IO */
};

/*
 * pathconf/fpathconf carry the same kind of constant as sysconf and are numbered just as
 * independently: two of McDonald's packaged libraries ask for a limit by bionic's number and OH
 * musl answers a different one. Same caller rule as sysconf.
 */
static long westlake_translate_pathconf(int name, const char* caller_path, int* handled)
{
    *handled = 0;
    if (caller_path == NULL) return -1;
    if (name < 0 || (size_t)name >= sizeof(westlake_bionic_pathconf) / sizeof(westlake_bionic_pathconf[0])) {
        *handled = 1;
        errno = EINVAL;
        return -1;
    }
    const int native_name = westlake_bionic_pathconf[name];
    if (native_name < 0) {
        *handled = 1;
        errno = EINVAL;
        return -1;
    }
    static unsigned int logged;
    const unsigned int bit = 1U << (name % 32);
    if ((__atomic_fetch_or(&logged, bit, __ATOMIC_RELAXED) & bit) == 0) {
        fprintf(stderr, "[WESTLAKE-WEBVIEW-BIONIC] pathconf selector=%d translated bionic->musl "
                "selector=%d caller=%s\n", name, native_name, caller_path);
        fflush(stderr);
    }
    return native_name;
}

long pathconf(const char* path, int name)
{
    typedef long (*PathconfFn)(const char*, int);
    static PathconfFn real_pathconf;
    if (real_pathconf == NULL) real_pathconf = (PathconfFn)dlsym(RTLD_NEXT, "pathconf");
    if (real_pathconf == NULL) { errno = ENOSYS; return -1; }
    const char* caller_path = NULL;
    if (!caller_is_android_dso(__builtin_return_address(0), &caller_path)) {
        return real_pathconf(path, name);
    }
    int handled = 0;
    const long native_name = westlake_translate_pathconf(name, caller_path, &handled);
    if (handled) return -1;
    return real_pathconf(path, (int)native_name);
}

long fpathconf(int fd, int name)
{
    typedef long (*FpathconfFn)(int, int);
    static FpathconfFn real_fpathconf;
    if (real_fpathconf == NULL) real_fpathconf = (FpathconfFn)dlsym(RTLD_NEXT, "fpathconf");
    if (real_fpathconf == NULL) { errno = ENOSYS; return -1; }
    const char* caller_path = NULL;
    if (!caller_is_android_dso(__builtin_return_address(0), &caller_path)) {
        return real_fpathconf(fd, name);
    }
    int handled = 0;
    const long native_name = westlake_translate_pathconf(name, caller_path, &handled);
    if (handled) return -1;
    return real_fpathconf(fd, (int)native_name);
}

/*
 * Symbols an Android-built library imports that neither musl nor the staged runtime defines, and
 * whose absence stops the library loading at all. libflutter.so is the case that found these: it
 * imports the software-rendering pair and a FORTIFY open, so dlopen fails before Flutter ever
 * chooses its GL path.
 *
 * __openat_2 is bionic's _FORTIFY_SOURCE wrapper and has openat's semantics exactly.
 *
 * ANativeWindow_lock/unlockAndPost belong to the CPU-rendering path: lock hands the caller a
 * mapped buffer to draw into. Westlake's ANativeWindow adapter has the OH plumbing for that
 * (request buffer, buffer handle, virAddr) but does not implement the pair, so these report the
 * path as unavailable rather than pretending to render. A caller that only links them gets a
 * loadable library; a caller that uses them gets a clear failure instead of a wrong picture.
 */
long __openat_2(int dirfd, const char* path, int flags)
{
    typedef int (*OpenatFn)(int, const char*, int, ...);
    static OpenatFn real_openat;
    if (real_openat == NULL) real_openat = (OpenatFn)dlsym(RTLD_NEXT, "openat");
    if (real_openat == NULL) { errno = ENOSYS; return -1; }
    return real_openat(dirfd, path, flags);
}

int ANativeWindow_lock(void* window, void* outBuffer, void* inOutDirtyBounds)
{
    (void)window; (void)outBuffer; (void)inOutDirtyBounds;
    static int logged;
    if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) {
        fprintf(stderr, "[WESTLAKE-ANW] ANativeWindow_lock: CPU rendering path not implemented; "
                "callers with a GPU path are unaffected\n");
        fflush(stderr);
    }
    errno = ENODEV;
    return -ENODEV;
}

int ANativeWindow_unlockAndPost(void* window)
{
    (void)window;
    errno = ENODEV;
    return -ENODEV;
}

long sysconf(int name)
{
    typedef long (*SysconfFn)(int);
    static SysconfFn real_sysconf;
    if (real_sysconf == NULL) {
        real_sysconf = (SysconfFn)dlsym(RTLD_NEXT, "sysconf");
    }
    if (real_sysconf == NULL) {
        errno = ENOSYS;
        return -1;
    }

    const char *caller_path = NULL;
    if (!caller_is_android_dso(__builtin_return_address(0), &caller_path)) {
        return real_sysconf(name);
    }
    const int known = name >= 0 &&
            (size_t)name < sizeof(westlake_bionic_sysconf) / sizeof(westlake_bionic_sysconf[0]);
    const int native_name = known ? westlake_bionic_sysconf[name] : -1;
    long result;
    if (native_name >= 0) {
        result = real_sysconf(native_name);
    } else if (native_name == -2) {
        result = 0;
    } else {
        errno = EINVAL;
        result = -1;
    }
    static unsigned char logged[256 / 8];
    if (known && name < 256) {
        unsigned char bit = (unsigned char)(1U << (name % 8));
        if ((__atomic_fetch_or(&logged[name / 8], bit, __ATOMIC_RELAXED) & bit) == 0) {
            fprintf(stderr,
                    "[WESTLAKE-WEBVIEW-BIONIC] sysconf selector=%d translated bionic->musl "
                    "selector=%d result=%ld caller=%s\n",
                    name, native_name, result, caller_path);
        }
    }
    return result;
}

/*
 * WESTLAKE §682: present the Android filesystem root to Android WebView while
 * preserving appspawn-x's OH-specific ART root.
 *
 * appspawn-x must use ANDROID_ROOT=/system/android for its staged ART/runtime
 * files. Chromium's Android Skia backend, however, computes its font directory
 * as getenv("ANDROID_ROOT") + "/fonts". On this OH board the Android-standard
 * font/config view is the staged runtime directory itself: it holds a fonts/
 * directory beside an etc/fonts.xml that is the complete adapter
 * configuration. The system partition contains an older,
 * read-only bootstrap fonts.xml without script-fallback families, so returning
 * /system would make Chromium render Latin while dropping every CJK glyph.
 *
 * Translate only direct calls from libwebviewchromium.so. All framework, ART,
 * and OH-native callers continue to see the real process environment.
 */
/*
 * The Android root Skia should see: the runtime directory itself.
 *
 * It already has the layout Skia expects -- etc/fonts.xml beside a fonts/ directory -- because
 * that is how the launcher stages it. An earlier layout put those under an android-root/
 * subdirectory and this shim named it outright; the launcher stopped staging it and the constant
 * stayed, so every font configuration Skia tried failed to open, it ended up with no font
 * families at all, and drawing an article's first text dereferenced null.
 *
 * Derived from WESTLAKE_RUNTIME_ROOT rather than named, and returned only when the font
 * configuration is really there, so a layout that moves again degrades to the real environment
 * instead of silently pointing at nothing.
 */
static const char *westlake_android_root(void)
{
    static char root[256];
    static int resolved;
    if (!resolved) {
        typedef char *(*GetenvFn)(const char *);
        GetenvFn real_getenv = (GetenvFn)dlsym(RTLD_NEXT, "getenv");
        const char *configured = real_getenv != NULL ? real_getenv("WESTLAKE_RUNTIME_ROOT") : NULL;
        if (configured == NULL || configured[0] == '\0') {
            configured = "/data/local/tmp/asx";
        }
        char candidate[256];
        snprintf(candidate, sizeof(candidate), "%s/etc/fonts.xml", configured);
        if (access(candidate, R_OK) == 0) {
            snprintf(root, sizeof(root), "%s", configured);
        } else {
            root[0] = '\0';
        }
        resolved = 1;
    }
    return root[0] != '\0' ? root : NULL;
}

char *getenv(const char *name)
{
    typedef char *(*GetenvFn)(const char *);
    static GetenvFn real_getenv;
    if (real_getenv == NULL) {
        real_getenv = (GetenvFn)dlsym(RTLD_NEXT, "getenv");
    }

    char *value = real_getenv != NULL ? real_getenv(name) : NULL;
    if (name != NULL && strcmp(name, "ANDROID_ROOT") == 0 &&
        caller_is_webview(__builtin_return_address(0), NULL)) {
        const char *android_root = westlake_android_root();
        if (android_root != NULL) {
            static int logged;
            if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) {
                fprintf(stderr,
                        "[WESTLAKE-WEBVIEW-BIONIC] ANDROID_ROOT translated "
                        "%s -> %s for libwebviewchromium.so\n",
                        value != NULL ? value : "<unset>", android_root);
            }
            return (char *) android_root;
        }
    }
    return value;
}

/*
 * WESTLAKE §732: Skia's Android font parser uses a compile-time absolute
 * /system/etc/fonts.xml path even though it uses ANDROID_ROOT for the font-file
 * directory. The board's system partition is read-only and contains the early
 * bootstrap config, which has no script fallback families. Redirect only
 * Chromium's read of that one Android config file to the complete staged
 * adapter config; all other fopen callers and paths retain their native
 * behavior.
 */
FILE *fopen(const char *path, const char *mode)
{
    typedef FILE *(*FopenFn)(const char *, const char *);
    static FopenFn real_fopen;
    if (real_fopen == NULL) {
        real_fopen = (FopenFn)dlsym(RTLD_NEXT, "fopen");
    }
    if (real_fopen == NULL) {
        errno = ENOSYS;
        return NULL;
    }

    const int from_webview = caller_is_webview(__builtin_return_address(0), NULL);
    const char *actual_path = path;
    if (path != NULL && strcmp(path, "/system/etc/fonts.xml") == 0 &&
        from_webview) {
        const char *android_root = westlake_android_root();
        static char font_config[288];
        if (android_root != NULL) {
            snprintf(font_config, sizeof(font_config), "%s/etc/fonts.xml", android_root);
            actual_path = font_config;
            static int logged;
            if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) {
                fprintf(stderr,
                        "[WESTLAKE-WEBVIEW-BIONIC] font config translated %s -> %s\n",
                        path, actual_path);
            }
        }
    }
    return real_fopen(actual_path, mode);
}

int __register_atfork(void (*prepare)(void), void (*parent)(void),
                      void (*child)(void), void *dso)
{
    (void)dso;
    fprintf(stderr, "[WESTLAKE-WEBVIEW-BIONIC] __register_atfork\n");
    return pthread_atfork(prepare, parent, child);
}

/* Diagnostic companion for the Android-sized setjmp boundary.  The assembly
 * calls this before saving the caller context, so a codec probe can establish
 * whether Chromium actually reaches the redirected import. */
void wl_sjp_note(void *environment, void *caller)
{
    if (access("/data/local/tmp/asx/trace_webview_setjmp", F_OK) != 0) {
        return;
    }
    static unsigned int calls;
    unsigned int call = __atomic_add_fetch(&calls, 1, __ATOMIC_RELAXED);
    if (call <= 256) {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-SJP] call=%u env=%p caller=%p\n",
                call, environment, caller);
    }
}

void wl_ljmp_note(void *environment, int value, void *caller)
{
    if (access("/data/local/tmp/asx/trace_webview_setjmp", F_OK) != 0) {
        return;
    }
    static unsigned int calls;
    unsigned int call = __atomic_add_fetch(&calls, 1, __ATOMIC_RELAXED);
    if (call <= 256) {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-LJMP] call=%u env=%p value=%d caller=%p\n",
                call, environment, value, caller);
    }
}

int *__errno(void)
{
    return __errno_location();
}

char *__gnu_strerror_r(int error_number, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return (char *)strerror(error_number);
    }
    if (strerror_r(error_number, buffer, buffer_size) != 0) {
        snprintf(buffer, buffer_size, "Unknown error %d", error_number);
    }
    return buffer;
}

#define WEBVIEW_PROP_NAME_MAX 128
#define WEBVIEW_PROP_VALUE_MAX 256
#define WEBVIEW_PROP_CACHE_SIZE 64

struct prop_info {
    char name[WEBVIEW_PROP_NAME_MAX];
    char value[WEBVIEW_PROP_VALUE_MAX];
    uint32_t serial;
};

static struct prop_info g_property_cache[WEBVIEW_PROP_CACHE_SIZE];
static size_t g_property_count;
static uint32_t g_property_serial;
static pthread_mutex_t g_property_mutex = PTHREAD_MUTEX_INITIALIZER;

static int read_oh_property(const char *name, char *value, size_t value_size)
{
    typedef int (*ReadParamFn)(const char *, char *, uint32_t *);
    static ReadParamFn read_param;
    static int resolved;
    if (!resolved) {
        read_param = (ReadParamFn)dlsym(RTLD_DEFAULT, "SystemReadParam");
        resolved = 1;
    }
    value[0] = '\0';
    if (read_param == NULL) {
        return 0;
    }
    uint32_t length = (uint32_t)value_size;
    if (read_param(name, value, &length) != 0) {
        value[0] = '\0';
        return 0;
    }
    value[value_size - 1] = '\0';
    return (int)strlen(value);
}

/*
 * Android-shaped property names OH's parameter store does not hold.
 *
 * Every name an Android caller asks for is absent from OH's store, so each read came back empty:
 * Chromium's sys_info_android.cc logged "Can't parse dalvik.vm.heapsize" and the four build
 * properties it reads for its own version gating were empty too. Worse than empty, they
 * disagreed with the process: android.os.SystemProperties answers these from a table in
 * framework/android-runtime/src/android_os_SystemProperties.cpp, so Java said SDK 34 while
 * native said nothing at all, and a caller that gates on one and branches on the other sees a
 * device that cannot exist.
 *
 * The values below are that table's, and must track it. Only names absent from OH's store fall
 * through to here, so a board that really does answer one wins.
 *
 * ro.arch and dalvik.vm.heapsize are not in that table because nothing in Java asks for them.
 * The heap size is a hint Chromium clamps into a sane range rather than a number it trusts; it is
 * reported as a plain Android default instead of a measurement, because this runtime's ART heap
 * ceiling is not the app's to know.
 */
static const struct {
    const char *name;
    const char *value;
} g_android_properties[] = {
    { "ro.build.version.sdk",      "34" },
    { "ro.build.version.release",  "14" },
    { "ro.build.version.codename", "REL" },
    { "ro.build.id",               "oh-adapter" },
    { "ro.arch",                   "arm64" },
    { "dalvik.vm.heapsize",        "512m" },
};

int __system_property_get(const char *name, char *value)
{
    if (name == NULL || value == NULL) {
        return 0;
    }
    int length = read_oh_property(name, value, WEBVIEW_PROP_VALUE_MAX);
    if (length == 0) {
        for (size_t i = 0; i < sizeof(g_android_properties) / sizeof(g_android_properties[0]); ++i) {
            if (strcmp(name, g_android_properties[i].name) == 0) {
                snprintf(value, WEBVIEW_PROP_VALUE_MAX, "%s", g_android_properties[i].value);
                length = (int)strlen(value);
                break;
            }
        }
    }
    fprintf(stderr, "[WESTLAKE-WEBVIEW-BIONIC] property %s=%s\n", name, value);
    return length;
}

const struct prop_info *__system_property_find(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    char value[WEBVIEW_PROP_VALUE_MAX];
    if (read_oh_property(name, value, sizeof(value)) == 0) {
        return NULL;
    }

    pthread_mutex_lock(&g_property_mutex);
    struct prop_info *entry = NULL;
    for (size_t i = 0; i < g_property_count; ++i) {
        if (strcmp(g_property_cache[i].name, name) == 0) {
            entry = &g_property_cache[i];
            break;
        }
    }
    if (entry == NULL && g_property_count < WEBVIEW_PROP_CACHE_SIZE) {
        entry = &g_property_cache[g_property_count++];
        snprintf(entry->name, sizeof(entry->name), "%s", name);
    }
    if (entry != NULL) {
        snprintf(entry->value, sizeof(entry->value), "%s", value);
        entry->serial = ++g_property_serial;
    }
    pthread_mutex_unlock(&g_property_mutex);
    return entry;
}

void __system_property_read_callback(
        const struct prop_info *property,
        void (*callback)(void *, const char *, const char *, uint32_t),
        void *cookie)
{
    if (property != NULL && callback != NULL) {
        callback(cookie, property->name, property->value, property->serial);
    }
}

int __open_2(const char *path, int flags)
{
    if ((flags & O_CREAT) != 0) {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-BIONIC] __open_2 rejected O_CREAT path=%s\n",
                path != NULL ? path : "<null>");
        abort();
    }
    return open(path, flags);
}

struct cmsghdr *__cmsg_nxthdr(struct msghdr *message, struct cmsghdr *control)
{
    struct cmsghdr *next = (struct cmsghdr *)((char *)control +
            CMSG_ALIGN(control->cmsg_len));
    size_t length = (size_t)((char *)(next + 1) - (char *)message->msg_control);
    return length > message->msg_controllen ? NULL : next;
}

void __FD_CLR_chk(int fd, fd_set *set, size_t set_size)
{
    if (fd < 0 || (size_t)fd >= set_size * 8) {
        abort();
    }
    FD_CLR(fd, set);
}

int __FD_ISSET_chk(int fd, const fd_set *set, size_t set_size)
{
    if (fd < 0 || (size_t)fd >= set_size * 8) {
        abort();
    }
    return FD_ISSET(fd, set);
}

void __FD_SET_chk(int fd, fd_set *set, size_t set_size)
{
    if (fd < 0 || (size_t)fd >= set_size * 8) {
        abort();
    }
    FD_SET(fd, set);
}

ssize_t __pread64_chk(int fd, void *buffer, size_t count, int64_t offset,
                      size_t buffer_size)
{
    if (count > buffer_size) {
        abort();
    }
    return pread(fd, buffer, count, (off_t)offset);
}

ssize_t __pwrite64_chk(int fd, const void *buffer, size_t count, int64_t offset,
                       size_t buffer_size)
{
    if (count > buffer_size) {
        abort();
    }
    return pwrite(fd, buffer, count, (off_t)offset);
}

long long strtoll_l(const char *text, char **end, int base, locale_t locale)
{
    (void)locale;
    return strtoll(text, end, base);
}

unsigned long long strtoull_l(const char *text, char **end, int base, locale_t locale)
{
    (void)locale;
    return strtoull(text, end, base);
}

void android_fdsan_exchange_owner_tag(int fd, uint64_t expected_tag, uint64_t new_tag)
{
    (void)fd;
    (void)expected_tag;
    (void)new_tag;
}

void android_set_abort_message(const char *message)
{
    fprintf(stderr, "[WESTLAKE-WEBVIEW-BIONIC] abort message: %s\n",
            message != NULL ? message : "<null>");
}

/*
 * WESTLAKE §709: do not export bionic/jemalloc's optional sdallocx API.
 *
 * Chromium weak-links sdallocx and deliberately falls back to its own
 * deallocator when the process allocator does not provide it.  OH uses musl,
 * not bionic's jemalloc.  The former shim advertised sdallocx and forwarded it
 * to musl free(), causing Chromium-owned allocations to crash in musl get_meta
 * as soon as §708 made WebView's native looper callbacks live.  Absence is the
 * correct ABI answer at this boundary.
 */

static char g_bionic_ctype[257];
const char *_ctype_ = g_bionic_ctype;

__attribute__((constructor)) static void initialize_bionic_ctype(void)
{
    enum {
        CTYPE_UPPER = 0x01,
        CTYPE_LOWER = 0x02,
        CTYPE_DIGIT = 0x04,
        CTYPE_SPACE = 0x08,
        CTYPE_PUNCT = 0x10,
        CTYPE_CONTROL = 0x20,
        CTYPE_HEX = 0x40,
        CTYPE_BLANK = 0x80,
    };
    for (int value = 0; value <= 255; ++value) {
        unsigned char flags = 0;
        if (value < 32 || value == 127) flags |= CTYPE_CONTROL;
        if (value >= 9 && value <= 13) flags |= CTYPE_SPACE;
        if (value == 32) flags |= CTYPE_SPACE | CTYPE_BLANK;
        if (value >= '0' && value <= '9') flags |= CTYPE_DIGIT;
        if (value >= 'A' && value <= 'Z') flags |= CTYPE_UPPER;
        if (value >= 'a' && value <= 'z') flags |= CTYPE_LOWER;
        if ((value >= 'A' && value <= 'F') ||
            (value >= 'a' && value <= 'f')) flags |= CTYPE_HEX;
        if ((value >= 33 && value <= 47) ||
            (value >= 58 && value <= 64) ||
            (value >= 91 && value <= 96) ||
            (value >= 123 && value <= 126)) flags |= CTYPE_PUNCT;
        g_bionic_ctype[value + 1] = (char)flags;
    }
}

char *__strncpy_chk2(char *dst, const char *src, size_t count,
                     size_t dst_size, size_t src_size)
{
    if (count > dst_size) {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-BIONIC] __strncpy_chk2 write overflow count=%zu dst=%zu\n",
                count, dst_size);
        abort();
    }

    char *result = dst;
    const char *source_start = src;
    while (count != 0) {
        if ((size_t)(src - source_start) >= src_size) {
            fprintf(stderr,
                    "[WESTLAKE-WEBVIEW-BIONIC] __strncpy_chk2 read overflow src=%zu\n",
                    src_size);
            abort();
        }
        *dst = *src;
        ++dst;
        ++src;
        --count;
        if (dst[-1] == '\0') {
            memset(dst, 0, count);
            break;
        }
    }
    return result;
}

/*
 * ---------------------------------------------------------------------------
 * Residue measured on the OH 6.1 board while loading McDonald's 26.31.1
 * (extractNativeLibs=false, ten prebuilt bionic-linked libraries). With this
 * shim preloaded exactly as the launcher does, the complete set of unresolved
 * bionic-private names across all ten libraries was thirteen symbols:
 *
 *   __sF (6 libs)  __system_property_foreach/_find_nth (2)  __pthread_cleanup_push/_pop (2/1)
 *   isnan/isinf (1)  __get_h_errno (1)  four ASensor* and ALooper_pollAll (1 each; libandroid)
 *
 * Everything below implements the libc-side ones. The libandroid ones belong
 * in libandroid_webview_shim.c. None of these are Android APIs the app called;
 * they are what bionic's headers compile ordinary C into.
 * ---------------------------------------------------------------------------
 */

/*
 * __sF: old bionic exposed stdin/stdout/stderr as three inline FILE objects.
 * Android code may pass &__sF[n] to stdio, but may not inspect FILE's bytes.
 * Translate those three sentinel addresses at each stdio entry point and pass
 * every ordinary FILE* through untouched. Same contract as
 * appspawn-x/bionic_compat/src/bionic_stdio_compat.c, which is not part of
 * this preload set.
 */
typedef struct WebviewBionicLegacyFile {
    unsigned char opaque[152];
} WebviewBionicLegacyFile;

WebviewBionicLegacyFile __sF[3];

static FILE *webview_translate_file(FILE *stream)
{
    if ((void *)stream == (void *)&__sF[0]) return stdin;
    if ((void *)stream == (void *)&__sF[1]) return stdout;
    if ((void *)stream == (void *)&__sF[2]) return stderr;
    return stream;
}

#define WEBVIEW_STDIO_FORWARD(ret, name, params, args) \
    ret name params \
    { \
        static ret (*real) params; \
        if (real == NULL) { \
            real = (ret (*) params)dlsym(RTLD_NEXT, #name); \
        } \
        return real args; \
    }

WEBVIEW_STDIO_FORWARD(int, vfprintf, (FILE *stream, const char *format, va_list ap),
                      (webview_translate_file(stream), format, ap))
WEBVIEW_STDIO_FORWARD(size_t, fread, (void *ptr, size_t size, size_t count, FILE *stream),
                      (ptr, size, count, webview_translate_file(stream)))
WEBVIEW_STDIO_FORWARD(size_t, fwrite, (const void *ptr, size_t size, size_t count, FILE *stream),
                      (ptr, size, count, webview_translate_file(stream)))
WEBVIEW_STDIO_FORWARD(int, fflush, (FILE *stream), (stream == NULL ? NULL : webview_translate_file(stream)))
WEBVIEW_STDIO_FORWARD(int, fputc, (int c, FILE *stream), (c, webview_translate_file(stream)))
WEBVIEW_STDIO_FORWARD(int, fputs, (const char *str, FILE *stream), (str, webview_translate_file(stream)))
WEBVIEW_STDIO_FORWARD(int, fseek, (FILE *stream, long offset, int whence),
                      (webview_translate_file(stream), offset, whence))
WEBVIEW_STDIO_FORWARD(long, ftell, (FILE *stream), (webview_translate_file(stream)))
WEBVIEW_STDIO_FORWARD(int, feof, (FILE *stream), (webview_translate_file(stream)))
WEBVIEW_STDIO_FORWARD(int, ferror, (FILE *stream), (webview_translate_file(stream)))
WEBVIEW_STDIO_FORWARD(int, fileno, (FILE *stream), (webview_translate_file(stream)))
WEBVIEW_STDIO_FORWARD(int, fclose, (FILE *stream), (webview_translate_file(stream)))
WEBVIEW_STDIO_FORWARD(int, setvbuf, (FILE *stream, char *buf, int mode, size_t size),
                      (webview_translate_file(stream), buf, mode, size))

int fprintf(FILE *stream, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int written = vfprintf(stream, format, ap);
    va_end(ap);
    return written;
}

/*
 * pthread_cleanup_push/pop: bionic's headers expand these into calls that
 * link a caller-owned record onto a per-thread list; musl keeps its own
 * layout. bionic has no pthread_cancel, so only explicit pop runs handlers.
 */
typedef struct WebviewBionicCleanup {
    struct WebviewBionicCleanup *prev;
    void (*routine)(void *);
    void *arg;
} WebviewBionicCleanup;

static __thread WebviewBionicCleanup *g_cleanup_head;

void __pthread_cleanup_push(WebviewBionicCleanup *c, void (*routine)(void *), void *arg)
{
    c->routine = routine;
    c->arg = arg;
    c->prev = g_cleanup_head;
    g_cleanup_head = c;
}

void __pthread_cleanup_pop(WebviewBionicCleanup *c, int execute)
{
    g_cleanup_head = c->prev;
    if (execute) {
        c->routine(c->arg);
    }
}

/*
 * Deprecated property enumeration. bionic keeps both for old NDK code;
 * both are expressed over the cache this shim already maintains, so a
 * property is only enumerable once something has looked it up.
 */
int __system_property_foreach(void (*callback)(const struct prop_info *, void *), void *cookie)
{
    if (callback == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_property_mutex);
    size_t count = g_property_count;
    pthread_mutex_unlock(&g_property_mutex);
    for (size_t i = 0; i < count; ++i) {
        callback(&g_property_cache[i], cookie);
    }
    return 0;
}

const struct prop_info *__system_property_find_nth(unsigned n)
{
    pthread_mutex_lock(&g_property_mutex);
    const struct prop_info *entry = n < g_property_count ? &g_property_cache[n] : NULL;
    pthread_mutex_unlock(&g_property_mutex);
    return entry;
}

/*
 * bionic exports isnan/isinf as real functions; musl's math.h provides only
 * macros, which is exactly why a bionic-built library cannot resolve them
 * here. Undefine the macros so the functions can be declared.
 */
#undef isnan
#undef isinf
int isnan(double value)
{
    return __builtin_isnan(value);
}

int isinf(double value)
{
    return __builtin_isinf(value);
}

/* bionic's h_errno accessor; musl's h_errno is a plain thread-local. */
int *__get_h_errno(void)
{
    return &h_errno;
}

/*
 * libandroid's NDK trace API (android/trace.h). In the Android namespace libandroid.so is the
 * WebView shim's, which has none of it, so a library that imports ATrace_* fails to relocate:
 * React Native's libworklets.so (Reanimated) did, and Nori, Rush Hour and Marlin aborted on the
 * JniException. This preloaded shim supplies them as Android behaves with tracing off:
 * ATrace_isEnabled() is false and the section calls do nothing.
 */
bool ATrace_isEnabled(void) { return false; }
void ATrace_beginSection(const char *section_name) { (void)section_name; }
void ATrace_endSection(void) {}
void ATrace_beginAsyncSection(const char *section_name, int32_t cookie) { (void)section_name; (void)cookie; }
void ATrace_endAsyncSection(const char *section_name, int32_t cookie) { (void)section_name; (void)cookie; }
void ATrace_setCounter(const char *counter_name, int64_t counter_value) { (void)counter_name; (void)counter_value; }

/*
 * ---------------------------------------------------------------------------
 * More bionic-private names app libraries import that OH's musl lacks, counted by the harness's
 * ndk:libc-abi rows across 330 maps: __libc_init (23 apps), __system_property_read (12),
 * getprogname (8), funopen (6), __assert (4), __sched_cpufree (4), sys_signame (2), isnanf (1).
 * A library importing one fails to relocate; many apps catch that and go on without it, then die
 * at the first call into the library: Transit's libtransitBgtfs.so (on __assert) died as
 * "No implementation found for TransitLib.refreshRoyaleStoreSubscriptionStatus", and Snapchat's
 * libclient.so on __assert directly. Each is bionic's behaviour.
 * ---------------------------------------------------------------------------
 */

/* bionic's assert() target: report and abort, as bionic does. */
void __assert(const char *file, int line, const char *failed_expression)
{
    fprintf(stderr, "%s:%d: assertion \"%s\" failed\n", file ? file : "?", line,
            failed_expression ? failed_expression : "?");
    abort();
}

/* The entry an executable's crt calls. A shared library links it only through a crt object it
 * never runs; reaching it in this process would be a bug, so it says so and aborts. */
void __libc_init(void *raw_args, void (*onexit)(void), int (*slingshot)(int, char **, char **),
                 const void *structors)
{
    (void)raw_args; (void)onexit; (void)slingshot; (void)structors;
    fprintf(stderr, "[WESTLAKE-BIONIC] __libc_init called inside an app process\n");
    abort();
}

/* Reads a property found with __system_property_find into name/value; returns the value's
 * length, as bionic does (PROP_VALUE_MAX-sized buffers). */
int __system_property_read(const struct prop_info *pi, char *name, char *value)
{
    if (pi == NULL) {
        return 0;
    }
    pthread_mutex_lock(&g_property_mutex);
    if (name != NULL) {
        snprintf(name, 32, "%s", pi->name);          /* bionic's PROP_NAME_MAX */
    }
    int length = 0;
    if (value != NULL) {
        snprintf(value, 92, "%s", pi->value);        /* bionic's PROP_VALUE_MAX */
        length = (int)strlen(value);
    }
    pthread_mutex_unlock(&g_property_mutex);
    return length;
}

/* The process's short name, as bionic sets it from argv[0]: the basename of the first word of
 * /proc/self/cmdline, read once. */
static char g_progname[256];

static void westlake_read_progname(void)
{
    int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    char buffer[sizeof(g_progname)];
    ssize_t n = fd >= 0 ? read(fd, buffer, sizeof(buffer) - 1) : -1;
    if (fd >= 0) {
        close(fd);
    }
    if (n <= 0) {
        return;
    }
    buffer[n] = '\0';
    const char *slash = strrchr(buffer, '/');
    snprintf(g_progname, sizeof(g_progname), "%s", slash != NULL ? slash + 1 : buffer);
}

const char *getprogname(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, westlake_read_progname);
    return g_progname;
}

/*
 * BSD's funopen over musl's fopencookie. bionic's fpos_t is a 64-bit offset; musl's is a union, so
 * the seek callback is typed with bionic's.
 *
 * The FILE is unbuffered and this cookie buffers reads itself. OH's musl refills a buffered FILE
 * by calling the cookie reader with the FILE's own buffer as the destination (__fill_buffer ->
 * readx == cookieread), and cookieread uses that same buffer for its last byte, so each refill
 * overwrote what it had just read: superpack's funopen over a Java InputStream saw byte 1023 where
 * byte 0 should be (runtime-answers probe: "131415..." for a stream of i % 251), failed its
 * archive magic, and Facebook, Instagram and Messenger stopped at "could not extract file from
 * archive". Unbuffered, musl reads straight into the caller's memory.
 */
typedef int64_t westlake_bionic_fpos_t;

enum { WESTLAKE_FUNOPEN_BUFFER = 4096 };

struct westlake_funopen_cookie {
    void *cookie;
    int (*readfn)(void *, char *, int);
    int (*writefn)(void *, const char *, int);
    westlake_bionic_fpos_t (*seekfn)(void *, westlake_bionic_fpos_t, int);
    int (*closefn)(void *);
    size_t pos, len; /* read-ahead: buffer[pos..len) is data the caller has not taken yet */
    char buffer[WESTLAKE_FUNOPEN_BUFFER];
};

static ssize_t westlake_funopen_read(void *c, char *buf, size_t size)
{
    struct westlake_funopen_cookie *f = c;
    if (f->readfn == NULL) {
        return -1;
    }
    if (f->pos == f->len && size >= sizeof(f->buffer)) {
        return f->readfn(f->cookie, buf, size > INT32_MAX ? INT32_MAX : (int)size);
    }
    if (f->pos == f->len) {
        int n = f->readfn(f->cookie, f->buffer, (int)sizeof(f->buffer));
        if (n <= 0) {
            return n;
        }
        f->pos = 0;
        f->len = (size_t)n;
    }
    size_t n = f->len - f->pos < size ? f->len - f->pos : size;
    memcpy(buf, f->buffer + f->pos, n);
    f->pos += n;
    return (ssize_t)n;
}

static ssize_t westlake_funopen_write(void *c, const char *buf, size_t size)
{
    struct westlake_funopen_cookie *f = c;
    f->pos = f->len = 0;
    return f->writefn ? f->writefn(f->cookie, buf, size > INT32_MAX ? INT32_MAX : (int)size) : -1;
}

static int westlake_funopen_seek(void *c, off_t *offset, int whence)
{
    struct westlake_funopen_cookie *f = c;
    if (f->seekfn == NULL) {
        errno = ESPIPE;
        return -1;
    }
    /* The underlying position is ahead of the caller's by the read-ahead not yet taken. */
    westlake_bionic_fpos_t target = (westlake_bionic_fpos_t)*offset;
    if (whence == SEEK_CUR) {
        target -= (westlake_bionic_fpos_t)(f->len - f->pos);
    }
    westlake_bionic_fpos_t result = f->seekfn(f->cookie, target, whence);
    if (result == -1) {
        return -1;
    }
    f->pos = f->len = 0;
    *offset = (off_t)result;
    return 0;
}

static int westlake_funopen_close(void *c)
{
    struct westlake_funopen_cookie *f = c;
    int result = f->closefn ? f->closefn(f->cookie) : 0;
    free(f);
    return result;
}

FILE *funopen(const void *cookie, int (*readfn)(void *, char *, int),
              int (*writefn)(void *, const char *, int),
              westlake_bionic_fpos_t (*seekfn)(void *, westlake_bionic_fpos_t, int),
              int (*closefn)(void *))
{
    if (readfn == NULL && writefn == NULL) {
        errno = EINVAL;
        return NULL;
    }
    struct westlake_funopen_cookie *f = calloc(1, sizeof(*f));
    if (f == NULL) {
        return NULL;
    }
    f->cookie = (void *)cookie;
    f->readfn = readfn;
    f->writefn = writefn;
    f->seekfn = seekfn;
    f->closefn = closefn;
    cookie_io_functions_t io = {westlake_funopen_read, westlake_funopen_write,
                                westlake_funopen_seek, westlake_funopen_close};
    FILE *file = fopencookie(f, readfn && writefn ? "r+" : readfn ? "r" : "w", io);
    if (file == NULL) {
        free(f);
        return NULL;
    }
    setvbuf(file, NULL, _IONBF, 0);
    return file;
}

/* bionic's CPU_FREE target; musl has __sched_cpualloc and __sched_cpucount but frees with free(). */
void __sched_cpufree(cpu_set_t *set)
{
    free(set);
}

/* Signal names without the SIG prefix, indexed by signal number, as bionic's sys_signame. */
const char *const sys_signame[65] = {
    [SIGHUP] = "HUP", [SIGINT] = "INT", [SIGQUIT] = "QUIT", [SIGILL] = "ILL", [SIGTRAP] = "TRAP",
    [SIGABRT] = "ABRT", [SIGBUS] = "BUS", [SIGFPE] = "FPE", [SIGKILL] = "KILL", [SIGUSR1] = "USR1",
    [SIGSEGV] = "SEGV", [SIGUSR2] = "USR2", [SIGPIPE] = "PIPE", [SIGALRM] = "ALRM", [SIGTERM] = "TERM",
    [SIGSTKFLT] = "STKFLT", [SIGCHLD] = "CHLD", [SIGCONT] = "CONT", [SIGSTOP] = "STOP",
    [SIGTSTP] = "TSTP", [SIGTTIN] = "TTIN", [SIGTTOU] = "TTOU", [SIGURG] = "URG", [SIGXCPU] = "XCPU",
    [SIGXFSZ] = "XFSZ", [SIGVTALRM] = "VTALRM", [SIGPROF] = "PROF", [SIGWINCH] = "WINCH",
    [SIGIO] = "IO", [SIGPWR] = "PWR", [SIGSYS] = "SYS",
};

int isnanf(float value)
{
    return __builtin_isnan(value);
}

/*
 * The NDK's ASharedMemory (libandroid.so, API 26), which neither OH nor the runtime's or the
 * WebView's libandroid.so provides: Instagram's and Messenger's libstartup.so failed to relocate on
 * ASharedMemory_create. Android backs it with ashmem; a sealable memfd gives the same fd-passing,
 * mmap-able region. setProt narrows nothing: ashmem enforces a narrowed protection on later
 * mappings, a memfd seal would fail while a writable mapping exists, and callers use it as a
 * safety net rather than for behaviour. ASharedMemory_dupFromJava is not provided (this library has
 * no JNI headers).
 */
int ASharedMemory_create(const char *name, size_t size)
{
    int fd = memfd_create(name != NULL && name[0] != '\0' ? name : "ashmem", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        return -1;
    }
    if (ftruncate(fd, (off_t)size) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

/* By seeking, not fstat: the app domain may not getattr a memfd (probe: fstat gave size 0). */
size_t ASharedMemory_getSize(int fd)
{
    off_t position = lseek(fd, 0, SEEK_CUR);
    off_t end = position < 0 ? -1 : lseek(fd, 0, SEEK_END);
    if (position >= 0) {
        lseek(fd, position, SEEK_SET);
    }
    return end > 0 ? (size_t)end : 0;
}

int ASharedMemory_setProt(int fd, int prot)
{
    if (fcntl(fd, F_GETFD) < 0 || (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

/*
 * The NDK's ALooper API. The runtime's libandroid.so implements it, but in the Android namespace a
 * library's libandroid.so is the WebView shim's, which does not: Facebook's libscrollmerged.so
 * failed to relocate on ALooper_pollAll. Each forwards to the runtime library's own entry point,
 * looked up through that library's handle so it can never resolve back to this one.
 */
typedef struct ALooper ALooper;
typedef int (*westlake_alooper_callback)(int fd, int events, void *data);

static void *westlake_runtime_android_symbol(const char *name)
{
    static void *library;
    if (library == NULL) {
        const char *root = getenv("WESTLAKE_RUNTIME_ROOT");
        char path[512];
        snprintf(path, sizeof(path), "%s/libandroid.so", root != NULL ? root : "");
        /* The default namespace's copy, which the framework (and its Looper) uses: a copy of this
         * library in another namespace must not load a second libandroid with its own Looper. */
        typedef int (*DlnsGetFn)(const char *, Dl_namespace *);
        typedef void *(*DlopenNsFn)(Dl_namespace *, const char *, int);
        DlnsGetFn get_fn = (DlnsGetFn) dlsym(RTLD_DEFAULT, "dlns_get");
        DlopenNsFn open_fn = (DlopenNsFn) dlsym(RTLD_DEFAULT, "dlopen_ns");
        Dl_namespace parent;
        memset(&parent, 0, sizeof(parent));
        if (get_fn != NULL && open_fn != NULL && get_fn("default", &parent) == 0) {
            library = open_fn(&parent, path, RTLD_NOW | RTLD_NOLOAD);
        }
        if (library == NULL) library = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
    }
    void *symbol = library != NULL ? dlsym(library, name) : NULL;
    if (symbol == NULL) wl_relocs_log("[WESTLAKE-BIONIC] runtime libandroid.so lacks %s\n", name);
    return symbol;
}

#define WESTLAKE_ALOOPER_FORWARD(ret, name, params, args)                             \
    ret name params                                                                   \
    {                                                                                 \
        static ret (*real) params;                                                    \
        if (real == NULL) real = (ret (*) params) westlake_runtime_android_symbol(#name); \
        return real args;                                                             \
    }

WESTLAKE_ALOOPER_FORWARD(ALooper *, ALooper_forThread, (void), ())
WESTLAKE_ALOOPER_FORWARD(ALooper *, ALooper_prepare, (int opts), (opts))
WESTLAKE_ALOOPER_FORWARD(void, ALooper_acquire, (ALooper *looper), (looper))
WESTLAKE_ALOOPER_FORWARD(void, ALooper_release, (ALooper *looper), (looper))
WESTLAKE_ALOOPER_FORWARD(int, ALooper_pollOnce, (int timeout, int *fd, int *events, void **data),
                         (timeout, fd, events, data))
WESTLAKE_ALOOPER_FORWARD(int, ALooper_pollAll, (int timeout, int *fd, int *events, void **data),
                         (timeout, fd, events, data))
WESTLAKE_ALOOPER_FORWARD(void, ALooper_wake, (ALooper *looper), (looper))
WESTLAKE_ALOOPER_FORWARD(int, ALooper_addFd,
                         (ALooper *looper, int fd, int ident, int events, westlake_alooper_callback callback,
                          void *data),
                         (looper, fd, ident, events, callback, data))
WESTLAKE_ALOOPER_FORWARD(int, ALooper_removeFd, (ALooper *looper, int fd), (looper, fd))

/*
 * AHardwareBuffer and the multinetwork DNS calls, for the same reason: the WebView shim's
 * libandroid.so has neither, so Instagram's libscrollmerged.so failed to relocate on
 * AHardwareBuffer_unlock. The runtime's libandroid.so imports AHardwareBuffer from libhwui.so;
 * a lookup through its handle searches its dependencies and finds libhwui's definition.
 */
typedef struct AHardwareBuffer AHardwareBuffer;
typedef struct AHardwareBuffer_Desc AHardwareBuffer_Desc;
typedef struct ARect ARect;

WESTLAKE_ALOOPER_FORWARD(int, AHardwareBuffer_allocate, (const AHardwareBuffer_Desc *desc, AHardwareBuffer **out),
                         (desc, out))
WESTLAKE_ALOOPER_FORWARD(void, AHardwareBuffer_acquire, (AHardwareBuffer *buffer), (buffer))
WESTLAKE_ALOOPER_FORWARD(void, AHardwareBuffer_release, (AHardwareBuffer *buffer), (buffer))
WESTLAKE_ALOOPER_FORWARD(void, AHardwareBuffer_describe, (const AHardwareBuffer *buffer, AHardwareBuffer_Desc *out),
                         (buffer, out))
WESTLAKE_ALOOPER_FORWARD(int, AHardwareBuffer_lock,
                         (AHardwareBuffer *buffer, uint64_t usage, int32_t fence, const ARect *rect, void **address),
                         (buffer, usage, fence, rect, address))
WESTLAKE_ALOOPER_FORWARD(int, AHardwareBuffer_unlock, (AHardwareBuffer *buffer, int32_t *fence), (buffer, fence))
WESTLAKE_ALOOPER_FORWARD(AHardwareBuffer *, AHardwareBuffer_fromHardwareBuffer, (void *env, void *object),
                         (env, object))
WESTLAKE_ALOOPER_FORWARD(void *, AHardwareBuffer_toHardwareBuffer, (void *env, AHardwareBuffer *buffer),
                         (env, buffer))
WESTLAKE_ALOOPER_FORWARD(int, android_res_nquery,
                         (uint64_t network, const char *name, int ns_class, int ns_type, uint32_t flags),
                         (network, name, ns_class, ns_type, flags))
WESTLAKE_ALOOPER_FORWARD(int, android_res_nresult, (int fd, int *rcode, uint8_t *answer, size_t length),
                         (fd, rcode, answer, length))

/* AChoreographer, for the same reason (Duolingo's packaged libraries import it); the runtime's
 * definitions live in liboh_hwui_shim.so, which libandroid.so's dependencies reach. */
typedef struct AChoreographer AChoreographer;
typedef void (*westlake_frame_callback)(long time, void *data);
typedef void (*westlake_frame_callback64)(int64_t time, void *data);
typedef void (*westlake_refresh_rate_callback)(int64_t period, void *data);

WESTLAKE_ALOOPER_FORWARD(AChoreographer *, AChoreographer_getInstance, (void), ())
WESTLAKE_ALOOPER_FORWARD(void, AChoreographer_postFrameCallback,
                         (AChoreographer *c, westlake_frame_callback callback, void *data), (c, callback, data))
WESTLAKE_ALOOPER_FORWARD(void, AChoreographer_postFrameCallbackDelayed,
                         (AChoreographer *c, westlake_frame_callback callback, void *data, long delay),
                         (c, callback, data, delay))
WESTLAKE_ALOOPER_FORWARD(void, AChoreographer_postFrameCallback64,
                         (AChoreographer *c, westlake_frame_callback64 callback, void *data), (c, callback, data))
WESTLAKE_ALOOPER_FORWARD(void, AChoreographer_postFrameCallbackDelayed64,
                         (AChoreographer *c, westlake_frame_callback64 callback, void *data, uint32_t delay),
                         (c, callback, data, delay))
WESTLAKE_ALOOPER_FORWARD(void, AChoreographer_registerRefreshRateCallback,
                         (AChoreographer *c, westlake_refresh_rate_callback callback, void *data), (c, callback, data))
WESTLAKE_ALOOPER_FORWARD(void, AChoreographer_unregisterRefreshRateCallback,
                         (AChoreographer *c, westlake_refresh_rate_callback callback, void *data), (c, callback, data))

/*
 * Importing an AHardwareBuffer into EGL. Westlake's AHardwareBuffer is an OH_NativeBuffer, but
 * hwui's TextureView path (AutoBackendTextureRelease -> MakeBackendTexture) imports it the Android
 * way: eglGetNativeClientBufferANDROID, then eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID). OH's
 * EGL takes an OHNativeWindowBuffer with EGL_NATIVE_BUFFER_OHOS, so the import failed and hwui
 * aborted with "Invalid GrBackendTexture" (Telegram, a video camera app). libhwui calls both
 * through its PLT, so these preloaded definitions win: the client buffer becomes an OH window
 * buffer over the same native buffer, and an image of it is created with the OH target. The window
 * buffer lives as long as the image.
 */
typedef void *WlEglDisplay;
typedef void *WlEglContext;
typedef void *WlEglImage;
typedef void *WlEglClientBuffer;
typedef int32_t WlEglInt;
#define WL_EGL_NATIVE_BUFFER_ANDROID 0x3140
#define WL_EGL_NATIVE_BUFFER_OHOS 0x34E1

/*
 * The EGL implementations behind these wrappers. More than one serves the process: OH's libEGL.so,
 * and the Mali driver, which the runtime's libandroid.so and libhwui.so link directly and which
 * hwui's EGL calls bind to. Each takes only its own display handles: hwui's display given to
 * libEGL.so fails with EGL_BAD_DISPLAY (Telegram's TextureView import), and an eglInitialize sent
 * to whichever one RTLD_NEXT happened to find failed outright (libGDX: "requires OpenGL ES 2.0";
 * Godot: "eglInitialize failed"). A call goes to the implementation whose current display it
 * names; failing that, to each in turn while the answer is EGL_BAD_DISPLAY.
 */
#define WL_EGL_BAD_DISPLAY 0x3008
#define WL_EGL_IMPLEMENTATIONS 3
typedef struct {
    void *library;
    WlEglDisplay (*current_display)(void);
    WlEglInt (*error)(void);
    WlEglImage (*create_image)(WlEglDisplay, WlEglContext, unsigned, WlEglClientBuffer, const WlEglInt *);
    unsigned (*destroy_image)(WlEglDisplay, WlEglImage);
    int (*dup_fence)(WlEglDisplay, void *);
} WlEglImplementation;
static WlEglImplementation wl_egl_implementations[WL_EGL_IMPLEMENTATIONS];
static int wl_egl_implementation_count;
static pthread_once_t wl_egl_implementations_once = PTHREAD_ONCE_INIT;

static void *wl_egl_entry(void *library, const char *name)
{
    void *symbol = dlsym(library, name);
    if (symbol == NULL) {
        /* An extension the library implements without exporting the name (the Mali driver's
         * eglDupNativeFenceFDANDROID): its own eglGetProcAddress. */
        void *(*proc)(const char *) = (void *(*)(const char *)) dlsym(library, "eglGetProcAddress");
        if (proc != NULL) symbol = proc(name);
    }
    Dl_info self, found;
    if (symbol != NULL && dladdr((void *) &wl_egl_entry, &self) != 0 && dladdr(symbol, &found) != 0 &&
        found.dli_fbase == self.dli_fbase) {
        return NULL; /* never this library's own wrapper */
    }
    return symbol;
}

static void wl_egl_add_implementation(void *library)
{
    if (library == NULL || wl_egl_implementation_count == WL_EGL_IMPLEMENTATIONS) return;
    void *current = wl_egl_entry(library, "eglGetCurrentDisplay");
    if (current == NULL) return;
    for (int i = 0; i < wl_egl_implementation_count; i++) {
        if ((void *) wl_egl_implementations[i].current_display == current) return; /* the same one */
    }
    WlEglImplementation *e = &wl_egl_implementations[wl_egl_implementation_count++];
    e->library = library;
    e->current_display = (WlEglDisplay (*)(void)) current;
    e->error = (WlEglInt (*)(void)) wl_egl_entry(library, "eglGetError");
    e->create_image = (WlEglImage (*)(WlEglDisplay, WlEglContext, unsigned, WlEglClientBuffer, const WlEglInt *))
            wl_egl_entry(library, "eglCreateImageKHR");
    e->destroy_image = (unsigned (*)(WlEglDisplay, WlEglImage)) wl_egl_entry(library, "eglDestroyImageKHR");
    e->dup_fence = (int (*)(WlEglDisplay, void *)) wl_egl_entry(library, "eglDupNativeFenceFDANDROID");
    Dl_info info;
    wl_relocs_log("[WESTLAKE-EGL] implementation %d: %s\n", wl_egl_implementation_count - 1,
                  dladdr(current, &info) != 0 && info.dli_fname != NULL ? info.dli_fname : "?");
}

static void wl_egl_find_implementations(void)
{
    /* First what the process binds to: eglGetCurrentDisplay, which this library does not define, in
     * the global scope. Then the driver the runtime links, then OH's libEGL.so. */
    Dl_info info;
    void *first = dlsym(RTLD_DEFAULT, "eglGetCurrentDisplay");
    if (first != NULL && dladdr(first, &info) != 0 && info.dli_fname != NULL) {
        wl_egl_add_implementation(dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD));
    }
    wl_egl_add_implementation(dlopen("libGLES_mali.z.so", RTLD_NOW | RTLD_NOLOAD));
    void *board = dlopen("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
    wl_egl_add_implementation(board != NULL ? board : dlopen("libEGL.so", RTLD_NOW));
}

/* The implementations to try for a call on this display, in order; returns how many. */
static int wl_egl_order(WlEglDisplay display, int order[WL_EGL_IMPLEMENTATIONS])
{
    pthread_once(&wl_egl_implementations_once, wl_egl_find_implementations);
    for (int i = 0; display != NULL && i < wl_egl_implementation_count; i++) {
        if (wl_egl_implementations[i].current_display() == display) {
            order[0] = i;
            return 1;
        }
    }
    for (int i = 0; i < wl_egl_implementation_count; i++) order[i] = i;
    return wl_egl_implementation_count;
}

static WlEglInt wl_egl_error(int which)
{
    return wl_egl_implementations[which].error != NULL ? wl_egl_implementations[which].error() : 0;
}

static struct {
    void *window_buffer;
    WlEglImage image;
    int implementation;
} wl_egl_imports[256];
static pthread_mutex_t wl_egl_imports_lock = PTHREAD_MUTEX_INITIALIZER;
static int wl_egl_imports_created, wl_egl_imports_destroyed;

static void *wl_native_window_symbol(const char *name)
{
    static void *library;
    if (library == NULL) library = dlopen("libnative_window.so", RTLD_NOW);
    return library != NULL ? dlsym(library, name) : NULL;
}

static void wl_destroy_window_buffer(void *window_buffer)
{
    static void (*destroy)(void *);
    if (destroy == NULL) destroy = (void (*)(void *)) wl_native_window_symbol("OH_NativeWindow_DestroyNativeWindowBuffer");
    if (destroy != NULL && window_buffer != NULL) destroy(window_buffer);
}

WlEglClientBuffer eglGetNativeClientBufferANDROID(const void *buffer)
{
    static void *(*create)(void *);
    if (create == NULL) {
        create = (void *(*)(void *)) wl_native_window_symbol("OH_NativeWindow_CreateNativeWindowBufferFromNativeBuffer");
    }
    void *window_buffer = create != NULL && buffer != NULL ? create((void *) buffer) : NULL;
    if (window_buffer == NULL) {
        wl_relocs_log("[WESTLAKE-EGL] no OH window buffer for AHardwareBuffer %p\n", buffer);
        return NULL;
    }
    pthread_mutex_lock(&wl_egl_imports_lock);
    size_t slot = 0;
    while (slot < sizeof(wl_egl_imports) / sizeof(wl_egl_imports[0]) && wl_egl_imports[slot].window_buffer != NULL) slot++;
    int full = slot == sizeof(wl_egl_imports) / sizeof(wl_egl_imports[0]);
    if (!full) {
        wl_egl_imports[slot].window_buffer = window_buffer;
        wl_egl_imports[slot].image = NULL;
    }
    pthread_mutex_unlock(&wl_egl_imports_lock);
    if (full) wl_relocs_log("[WESTLAKE-EGL] import table full: %d created, %d destroyed\n",
                            wl_egl_imports_created, wl_egl_imports_destroyed);
    return window_buffer;
}

WlEglImage eglCreateImageKHR(WlEglDisplay display, WlEglContext context, unsigned target, WlEglClientBuffer buffer,
                             const WlEglInt *attribs)
{
    size_t slot = sizeof(wl_egl_imports) / sizeof(wl_egl_imports[0]);
    if (target == WL_EGL_NATIVE_BUFFER_ANDROID) {
        pthread_mutex_lock(&wl_egl_imports_lock);
        for (size_t i = 0; i < sizeof(wl_egl_imports) / sizeof(wl_egl_imports[0]); i++) {
            if (wl_egl_imports[i].window_buffer == buffer && wl_egl_imports[i].image == NULL) {
                slot = i;
                break;
            }
        }
        pthread_mutex_unlock(&wl_egl_imports_lock);
    }
    int ours = slot < sizeof(wl_egl_imports) / sizeof(wl_egl_imports[0]);
    int order[WL_EGL_IMPLEMENTATIONS], count = wl_egl_order(display, order), used = -1;
    WlEglImage image = NULL;
    WlEglInt code = 0;
    for (int k = 0; k < count && image == NULL; k++) {
        WlEglImplementation *e = &wl_egl_implementations[order[k]];
        if (e->create_image == NULL) continue;
        used = order[k];
        image = e->create_image(display, context, ours ? WL_EGL_NATIVE_BUFFER_OHOS : target, buffer, attribs);
        /* Ask for the error only when another implementation remains: the caller reads it. */
        if (image == NULL && k + 1 < count && (code = wl_egl_error(used)) != WL_EGL_BAD_DISPLAY) break;
    }
    if (!ours) return image;
    pthread_mutex_lock(&wl_egl_imports_lock);
    if (image != NULL) {
        wl_egl_imports[slot].image = image;
        wl_egl_imports[slot].implementation = used;
        wl_egl_imports_created++;
    } else {
        wl_egl_imports[slot].window_buffer = NULL;
    }
    pthread_mutex_unlock(&wl_egl_imports_lock);
    if (image == NULL) {
        if (code == 0 && used >= 0) code = wl_egl_error(used);
        wl_relocs_log("[WESTLAKE-EGL] OH image import failed: 0x%x\n", (unsigned) code);
        wl_destroy_window_buffer(buffer);
    } else {
        static int logged;
        if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) {
            wl_relocs_log("[WESTLAKE-EGL] AHardwareBuffer imported as an OH native buffer image (implementation %d)\n",
                          used);
        }
    }
    return image;
}

unsigned eglDestroyImageKHR(WlEglDisplay display, WlEglImage image)
{
    void *window_buffer = NULL;
    int made_by = -1;
    pthread_mutex_lock(&wl_egl_imports_lock);
    for (size_t i = 0; image != NULL && i < sizeof(wl_egl_imports) / sizeof(wl_egl_imports[0]); i++) {
        if (wl_egl_imports[i].image == image) {
            window_buffer = wl_egl_imports[i].window_buffer;
            made_by = wl_egl_imports[i].implementation;
            wl_egl_imports[i].window_buffer = NULL;
            wl_egl_imports[i].image = NULL;
            wl_egl_imports_destroyed++;
            break;
        }
    }
    pthread_mutex_unlock(&wl_egl_imports_lock);
    int order[WL_EGL_IMPLEMENTATIONS], count = wl_egl_order(display, order);
    if (made_by >= 0) {
        order[0] = made_by;
        count = 1;
    }
    unsigned result = 0;
    for (int k = 0; k < count && !result; k++) {
        WlEglImplementation *e = &wl_egl_implementations[order[k]];
        if (e->destroy_image == NULL) continue;
        result = e->destroy_image(display, image);
        if (!result && k + 1 < count && wl_egl_error(order[k]) != WL_EGL_BAD_DISPLAY) break;
    }
    wl_destroy_window_buffer(window_buffer);
    return result;
}

/*
 * EGL_KHR_partial_update. The board's EGL advertises it but has rejected the damage region with
 * EGL_BAD_ACCESS (Telegram, after 120 frames), and the deployed libhwui still treats any failure as
 * fatal: the hwui source carries the remedy as §320, the binary predates it. The region is only a
 * hint of which part of the frame changes, so it is not passed on: each frame is presented whole.
 */
unsigned eglSetDamageRegionKHR(WlEglDisplay display, void *surface, WlEglInt *rects, WlEglInt count)
{
    (void) rects;
    (void) count;
    static int logged;
    if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) {
        wl_relocs_log("[WESTLAKE-EGLDAMAGE] damage region for %p/%p not passed on: frames presented whole\n",
                      display, surface);
    }
    return 1;
}

/*
 * eglDupNativeFenceFDANDROID. The Mali driver hwui binds its EGL calls to implements it but does not
 * export the name, so hwui's call went to OH's libEGL.so with the driver's display: "invalid display
 * pointer" on every TextureView frame. hwui then had no release fence, so the SurfaceTexture kept its
 * old buffer frame after frame, and Telegram's next swaps failed with EGL_BAD_ALLOC. The call goes to
 * the implementation that owns the display, through its eglGetProcAddress where it has no export.
 */
int eglDupNativeFenceFDANDROID(WlEglDisplay display, void *sync)
{
    int order[WL_EGL_IMPLEMENTATIONS], count = wl_egl_order(display, order);
    for (int k = 0; k < count; k++) {
        WlEglImplementation *e = &wl_egl_implementations[order[k]];
        if (e->dup_fence == NULL) continue;
        int fd = e->dup_fence(display, sync);
        if (fd >= 0) return fd;
        if (k + 1 < count && wl_egl_error(order[k]) != WL_EGL_BAD_DISPLAY) break;
    }
    static int logged;
    if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) {
        wl_relocs_log("[WESTLAKE-EGL] no native fence for sync %p on display %p\n", sync, display);
    }
    return -1; /* EGL_NO_NATIVE_FENCE_FD_ANDROID */
}

/*
 * eglTerminate. hwui renders the whole process through the default display and keeps it
 * initialized for the life of the process; Android's EGL loader counts initializations, so an app's
 * own eglTerminate (GLSurfaceView, a renderer through EGL14) never tears that display down there.
 * OH's terminates it at once, under hwui, whose next frame fails with EGL_NOT_INITIALIZED and aborts
 * (X, and Telegram's GL thread). The display therefore stays initialized; what the app created on
 * it, it destroys itself, as it does on Android.
 */
unsigned eglTerminate(WlEglDisplay display)
{
    static int logged;
    if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) {
        wl_relocs_log("[WESTLAKE-EGL] eglTerminate(%p): display left initialized for the process\n", display);
    }
    return 1;
}

/*
 * setcon. Westlake's app process starts in the launcher's domain (su, from the device shell) and
 * moves to the app's (normal_hap) through OH's HapContext::HapDomainSetcontext, which calls setcon.
 * A datagram socket connected before that keeps the launcher's label, and the app domain may
 * neither write to it nor reconnect it: hilog's, inherited from the launcher. Under SELinux
 * enforcing every app log line was dropped, silently (dontaudit): Telegram's process got 24 lines
 * into hilog in a run, 2,980 with this. Each connected AF_UNIX datagram socket is noted while
 * still in the old domain, and after a
 * successful setcon replaced, on the same descriptor, by one created and connected in the new one.
 * The switch is made while the process is single-threaded, so the swap races with nothing.
 */
#include <sys/un.h>

int setcon(const char *context)
{
    static int (*real)(const char *);
    if (real == NULL) {
        void *library = dlopen("libselinux.z.so", RTLD_NOW | RTLD_NOLOAD);
        if (library == NULL) library = dlopen("libselinux.z.so", RTLD_NOW);
        void *symbol = library != NULL ? dlsym(library, "setcon") : NULL;
        Dl_info self, found;
        if (symbol != NULL && dladdr((void *) &setcon, &self) != 0 && dladdr(symbol, &found) != 0 &&
            found.dli_fbase == self.dli_fbase) {
            symbol = NULL; /* never this library's own wrapper */
        }
        real = (int (*)(const char *)) symbol;
    }
    if (real == NULL) {
        errno = ENOSYS;
        return -1;
    }
    enum { WL_MAX_RENEW = 16 };
    struct {
        int fd;
        struct sockaddr_un peer;
        socklen_t length;
    } connected[WL_MAX_RENEW];
    int count = 0;
    for (int fd = 0; fd < 1024 && count < WL_MAX_RENEW; fd++) {
        int type = 0;
        socklen_t size = sizeof(type);
        if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &size) != 0 || type != SOCK_DGRAM) continue;
        struct sockaddr_un peer;
        socklen_t length = sizeof(peer);
        memset(&peer, 0, sizeof(peer));
        if (getpeername(fd, (struct sockaddr *) &peer, &length) != 0 || peer.sun_family != AF_UNIX ||
            length <= (socklen_t) offsetof(struct sockaddr_un, sun_path) || peer.sun_path[0] == '\0') {
            continue; /* not connected to a filesystem socket: unnamed, abstract or another family */
        }
        connected[count].fd = fd;
        connected[count].peer = peer;
        connected[count].length = length;
        count++;
    }
    int rc = real(context);
    if (rc != 0) return rc;
    int renewed = 0;
    for (int i = 0; i < count; i++) {
        int fd = connected[i].fd;
        int descriptor_flags = fcntl(fd, F_GETFD);
        int status_flags = fcntl(fd, F_GETFL);
        int fresh = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (fresh < 0) continue;
        if (connect(fresh, (struct sockaddr *) &connected[i].peer, connected[i].length) != 0 ||
            (status_flags >= 0 && fcntl(fresh, F_SETFL, status_flags) != 0) || dup2(fresh, fd) < 0) {
            close(fresh);
            continue;
        }
        if (descriptor_flags >= 0) fcntl(fd, F_SETFD, descriptor_flags);
        close(fresh);
        renewed++;
    }
    wl_relocs_log("[WESTLAKE-SELINUX] setcon %s: %d of %d connected datagram socket(s) re-created in the new domain\n",
                  context != NULL ? context : "(null)", renewed, count);
    return rc;
}
