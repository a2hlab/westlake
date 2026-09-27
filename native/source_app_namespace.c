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

// Android files the runtime carries at their Android paths, merged over OH's: <root>/etc over
// /system/etc (fonts.xml and font_fallback.xml, security/cacerts -- Dart's TLS reads that fixed path)
// and <root>/fonts over /system/fonts (Android's fonts beside OH's). Skia, in Flutter, reads the font
// list from /system/etc/fonts.xml; OH's is a symlink an app may not follow, so it saw no system
// fonts and drew no text. Each OH entry the runtime does not replace is bound into the runtime's
// copy -- a symlink as the file it names -- and the merged directory then covers OH's.
static int merge(const char* replacement, const char* original) {
    char from[PATH_MAX], to[PATH_MAX];
    struct stat st, mine;
    DIR* dir = opendir(original);
    if (!dir) return -1;
    struct dirent* entry;
    int rc = 0;
    while (rc == 0 && (entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (snprintf(from, sizeof(from), "%s/%s", original, name) >= (int) sizeof(from) ||
            snprintf(to, sizeof(to), "%s/%s", replacement, name) >= (int) sizeof(to)) {
            rc = -1;
            break;
        }
        if (stat(from, &st)) continue;  /* dangling or unreadable: nothing to show */
        if (lstat(to, &mine) == 0) {
            /* The runtime replaces it; a directory both have is merged one level down. */
            if (S_ISDIR(mine.st_mode) && S_ISDIR(st.st_mode)) rc = merge(to, from);
            continue;
        }
        if (S_ISDIR(st.st_mode) ? mkdir(to, 0755) : close(open(to, O_WRONLY | O_CREAT, 0644))) {
            rc = -1;
            break;
        }
        if (mount(from, to, NULL, MS_BIND, NULL)) rc = -1;
    }
    closedir(dir);
    return rc;
}

static int overlay(const char* root, const char* mine, const char* system, const char* saved) {
    char replacement[PATH_MAX], original[PATH_MAX];
    struct stat st;
    if (snprintf(replacement, sizeof(replacement), "%s/%s", root, mine) >= (int) sizeof(replacement) ||
        snprintf(original, sizeof(original), "%s/%s", root, saved) >= (int) sizeof(original)) return -1;
    if (lstat(replacement, &st) || !S_ISDIR(st.st_mode)) return 0;
    if ((mkdir(original, 0755) && errno != EEXIST) || mount(system, original, NULL, MS_BIND, NULL) ||
        merge(replacement, original) ||
        mount(replacement, system, NULL, MS_BIND | MS_REC, NULL)) return -1;
    return 1;
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
    int etc = overlay(root, "etc", "/system/etc", "system-etc-oh");
    int fonts = etc < 0 ? -1 : overlay(root, "fonts", "/system/fonts", "system-fonts-oh");
    if (etc < 0 || fonts < 0) {
        perror("source application system files"); return 1;
    }
    fprintf(stderr, "SOURCE_APP_NAMESPACE uid=%lu mount=%lu->%lu runtime=%s icu=%s etc=%s fonts=%s\n",
            uid, before.st_ino, after.st_ino, root, icu_data, etc ? "merged" : "absent", fonts ? "merged" : "absent");
    execvp(argv[3], argv + 3);
    perror("source app exec");
    return 1;
}
