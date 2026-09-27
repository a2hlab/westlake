#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static int owned_directory(const char* path, uid_t uid) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == uid;
}

// Android's system trust store lives at /system/etc/security/cacerts, a path Dart's TLS reads
// verbatim; OH has /system/etc/security without it, read-only. When the runtime carries one
// (<root>/etc/security/cacerts), cover /system/etc/security with <root>/etc/security holding that
// directory plus a bind of every OH entry, so OH's own files stay where they were.
static int overlay_security(const char* root) {
    char security[PATH_MAX], cacerts[PATH_MAX], original[PATH_MAX], from[PATH_MAX], to[PATH_MAX];
    struct stat st;
    if (snprintf(security, sizeof(security), "%s/etc/security", root) >= (int) sizeof(security) ||
        snprintf(cacerts, sizeof(cacerts), "%s/cacerts", security) >= (int) sizeof(cacerts) ||
        snprintf(original, sizeof(original), "%s/security-oh", root) >= (int) sizeof(original)) return -1;
    if (lstat(cacerts, &st) || !S_ISDIR(st.st_mode)) return 0;
    if ((mkdir(original, 0755) && errno != EEXIST) ||
        mount("/system/etc/security", original, NULL, MS_BIND, NULL)) return -1;
    DIR* dir = opendir(original);
    if (!dir) return -1;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..") || !strcmp(name, "cacerts")) continue;
        if (snprintf(from, sizeof(from), "%s/%s", original, name) >= (int) sizeof(from) ||
            snprintf(to, sizeof(to), "%s/%s", security, name) >= (int) sizeof(to) || stat(from, &st)) {
            closedir(dir);
            return -1;
        }
        if (S_ISDIR(st.st_mode) ? (mkdir(to, 0755) && errno != EEXIST)
                                : ((close(open(to, O_WRONLY | O_CREAT, 0644)) && errno != EEXIST))) {
            closedir(dir);
            return -1;
        }
        if (mount(from, to, NULL, MS_BIND, NULL)) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    return mount(security, "/system/etc/security", NULL, MS_BIND | MS_REC, NULL) ? -1 : 1;
}

// Mount only this candidate's runtime and empty application data in a private
// namespace. Appspawn performs the normal OH token and DAC transition later.
int main(int argc, char** argv) {
    if (argc < 4 || getuid() != 0) return 2;
    char* end = NULL;
    unsigned long uid = strtoul(argv[2], &end, 10);
    char root[PATH_MAX], temporary[PATH_MAX], parent[PATH_MAX], icu_data[PATH_MAX];
    const char* prefix = "/data/app/el2/100/base/";
    if (!end || *end || uid < 20000000 || uid > 2147483647 ||
        !realpath(argv[1], root) || strcmp(root, argv[1]) ||
        strncmp(root, prefix, strlen(prefix)) ||
        !strstr(root, "/files/a2hlab-source-") || !owned_directory(root, uid) ||
        snprintf(temporary, sizeof(temporary), "%s/private-tmp", root) >= sizeof(temporary) ||
        snprintf(parent, sizeof(parent), "%s/app-data", root) >= sizeof(parent) ||
        snprintf(icu_data, sizeof(icu_data), "%s/i18n/etc/icu", root) >= sizeof(icu_data) ||
        !owned_directory(temporary, uid) || !owned_directory(parent, uid) ||
        !owned_directory(icu_data, uid)) return 2;
    struct stat before, after;
    if (stat("/proc/self/ns/mnt", &before) || unshare(CLONE_NEWNS) ||
        stat("/proc/self/ns/mnt", &after) || before.st_ino == after.st_ino ||
        mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) ||
        mount(temporary, "/data/local/tmp", NULL, MS_BIND, NULL) ||
        mount(root, "/data/local/tmp/asx", NULL, MS_BIND, NULL) ||
        mount(parent, "/data/data", NULL, MS_BIND, NULL) ||
        mount(icu_data, "/system/usr/icu", NULL, MS_BIND, NULL)) {
        perror("source application namespace"); return 1;
    }
    int security = overlay_security(root);
    if (security < 0) {
        perror("source application trust store"); return 1;
    }
    fprintf(stderr, "SOURCE_APP_NAMESPACE uid=%lu mount=%lu->%lu runtime=%s icu=%s cacerts=%s\n",
            uid, before.st_ino, after.st_ino, root, icu_data, security ? "mounted" : "absent");
    execvp(argv[3], argv + 3);
    perror("source app exec");
    return 1;
}
