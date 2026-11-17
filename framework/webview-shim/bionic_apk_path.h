#ifndef WESTLAKE_BIONIC_APK_PATH_H
#define WESTLAKE_BIONIC_APK_PATH_H

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

/* ABI routing only: these names are not package, signer or authorization proof. */
static int westlake_apk_filename(const char *path)
{
    const char *apk = strstr(path, ".apk");
    while (apk != NULL) {
        if (apk[4] == '\0' || apk[4] == '!' || apk[4] == '/') return 1;
        apk = strstr(apk + 4, ".apk");
    }
    return 0;
}

/* RemoteApkCode retains verified read-only APK descriptors for the client lifetime.
 * The native loader may preserve /proc/self/fd/N as dli_fname, although /proc/maps
 * names the underlying APK. Resolve that descriptor label before routing bionic
 * ABI calls. Do not cache by descriptor number: closed descriptors can be reused.
 */
static int in_apk(const char *path)
{
    static const char prefix[] = "/proc/self/fd/";
    if (path == NULL) return 0;
    if (strncmp(path, prefix, sizeof(prefix) - 1) != 0) return westlake_apk_filename(path);
    const char *end = path + sizeof(prefix) - 1;
    if (*end < '0' || *end > '9') return 0;
    unsigned fd = 0;
    while (*end >= '0' && *end <= '9') {
        unsigned digit = (unsigned)(*end - '0');
        if (fd > ((unsigned)INT_MAX - digit) / 10) return 0;
        fd = fd * 10 + digit;
        end++;
    }
    if (*end != '\0' && !(end[0] == '!' && end[1] == '/')) return 0;
    char descriptor[sizeof(prefix) + 10];
    size_t length = (size_t)(end - path);
    if (length >= sizeof(descriptor)) return 0;
    memcpy(descriptor, path, length);
    descriptor[length] = '\0';
    char resolved[PATH_MAX];
    int saved_errno = errno;
    ssize_t n = readlink(descriptor, resolved, sizeof(resolved) - 1);
    errno = saved_errno;
    if (n <= 0 || (size_t)n >= sizeof(resolved) - 1) return 0;
    resolved[n] = '\0';
    return westlake_apk_filename(resolved);
}

#endif
