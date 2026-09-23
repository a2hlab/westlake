#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <fcntl.h>
#include <locale.h>
#include <math.h>
#include <netdb.h>
#include <pthread.h>
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
static int caller_is_android_dso(void *return_address, const char **caller_path)
{
    Dl_info caller = {0};
    if (dladdr(return_address, &caller) == 0 || caller.dli_fname == NULL) {
        return 0;
    }
    if (strstr(caller.dli_fname, "libwebviewchromium.so") == NULL &&
        strstr(caller.dli_fname, "/data/local/tmp/asx/lib/") == NULL) {
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

void *dlopen(const char *filename, int flags)
{
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
     */
    static char webview_library[512];
    if (basename != NULL && basename == filename &&
        caller_is_webview(__builtin_return_address(0), NULL)) {
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
    if (basename != NULL && strcmp(basename, "libGLESv2.so") == 0 &&
        caller_is_webview(__builtin_return_address(0), NULL)) {
        actual_filename = "/system/lib64/platformsdk/libGLESv3.so";
        static int logged;
        if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) {
            fprintf(stderr,
                    "[WESTLAKE-WEBVIEW-BIONIC] GLES library translated "
                    "%s -> %s for libwebviewchromium.so\n",
                    filename, actual_filename);
        }
    }
    return real_dlopen(actual_filename, flags);
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
