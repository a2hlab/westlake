#define _GNU_SOURCE
#include <errno.h>
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
    fprintf(stderr, "SOURCE_APP_NAMESPACE uid=%lu mount=%lu->%lu runtime=%s icu=%s\n",
            uid, before.st_ino, after.st_ino, root, icu_data);
    execvp(argv[3], argv + 3);
    perror("source app exec");
    return 1;
}
