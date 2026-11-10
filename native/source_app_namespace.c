#define _GNU_SOURCE
#include <ctype.h>
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

static int id_chars(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!isalnum((unsigned char) s[i]) && s[i] != '-' && s[i] != '_') return 0;
    }
    return 1;
}

// The app's own files at an Android install path, when the launcher asks for one
// (WESTLAKE_ANDROID_APP_DIR=/data/app/~~<id>==/<package>-<id>==, each id 22 URL-safe base64
// characters): <root>/android-app holds base.apk, the split APKs and lib/arm64, as Android's
// package manager leaves an install, and the app's ApplicationInfo names them there instead of
// under /data/local/tmp. OH keeps its own tree in /data/app, so in this namespace a new directory
// of the runtime, <root>/data-app, covers /data/app; each of OH's entries is bound back into it,
// with what is mounted below it (the runtime's real root is under el2), and the install directory
// goes beside them. The cover has the runtime's label, which the app's domain reads; a tmpfs would
// need a context= relabel, and the board refuses that for any file type. The cover is made here,
// by root, so nothing the app wrote is in it, and OH's tree and the install directory are reached
// through descriptors opened before the cover hides them.
static int install_fail(const char* step, const char* path) {
    fprintf(stderr, "source application install directory: %s%s%s: %s\n", step, path ? " " : "",
            path ? path : "", strerror(errno));
    return -1;
}

static int install_dir(const char* root, uid_t uid) {
    const char* target = getenv("WESTLAKE_ANDROID_APP_DIR");
    const char* prefix = "/data/app/~~";
    char source[PATH_MAX], cover[PATH_MAX], parent[PATH_MAX], from[PATH_MAX], to[PATH_MAX];
    char names[64][NAME_MAX + 1];
    size_t count = 0, length, leaf_length;
    if (target == NULL || target[0] == '\0') return 0;
    errno = EINVAL;
    length = strlen(target);
    if (strncmp(target, prefix, strlen(prefix)) || length >= sizeof(parent)) return install_fail("check", target);
    const char* id = target + strlen(prefix);
    if (!id_chars(id, 22) || strncmp(id + 22, "==/", 3)) return install_fail("check", target);
    const char* leaf = id + 25;
    leaf_length = strlen(leaf);
    if (leaf_length < 26 || leaf[leaf_length - 25] != '-' || !id_chars(leaf + leaf_length - 24, 22) ||
        strcmp(leaf + leaf_length - 2, "==")) return install_fail("check", target);
    for (const char* c = leaf; c < leaf + leaf_length - 25; c++) {
        if (!isalnum((unsigned char) *c) && *c != '.' && *c != '_') return install_fail("check", target);
    }
    if (snprintf(source, sizeof(source), "%s/android-app", root) >= (int) sizeof(source) ||
        !owned_directory(source, uid)) return install_fail("check", source);
    memcpy(parent, target, (size_t) (leaf - target - 1));
    parent[leaf - target - 1] = '\0';
    DIR* dir = opendir("/data/app");
    if (!dir) return install_fail("list", "/data/app");
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (count == sizeof(names) / sizeof(names[0])) {
            closedir(dir);
            errno = E2BIG;
            return install_fail("list", "/data/app");
        }
        snprintf(names[count++], sizeof(names[0]), "%s", entry->d_name);
    }
    closedir(dir);
    if (snprintf(cover, sizeof(cover), "%s/data-app", root) >= (int) sizeof(cover)) {
        errno = ENAMETOOLONG;
        return install_fail("name", root);
    }
    if (mkdir(cover, 0755)) return install_fail("make", cover);
    int rc = -1;
    int mine = -1, made = -1, oh = open("/data/app", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (oh < 0) {
        install_fail("open", "/data/app");
        goto done;
    }
    mine = open(source, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (mine < 0) {
        install_fail("open", source);
        goto done;
    }
    made = open(cover, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (made < 0) {
        install_fail("open", cover);
        goto done;
    }
    // As Android 15 leaves them: /data/app as OH has it (system, 0771: an app may pass through it
    // but not list it), the install's parent system-owned 0771 and the install directory 0775.
    struct stat system_app;
    if (fstat(oh, &system_app) || fchown(made, system_app.st_uid, system_app.st_gid) ||
        fchmod(made, system_app.st_mode & 07777)) {
        install_fail("own", cover);
        goto done;
    }
    snprintf(from, sizeof(from), "/proc/self/fd/%d", made);
    if (mount(from, "/data/app", NULL, MS_BIND, NULL)) {
        install_fail("cover", "/data/app");
        goto done;
    }
    for (size_t i = 0; i < count; i++) {
        struct stat st;
        if (snprintf(from, sizeof(from), "/proc/self/fd/%d/%s", oh, names[i]) >= (int) sizeof(from) ||
            snprintf(to, sizeof(to), "/data/app/%s", names[i]) >= (int) sizeof(to)) {
            errno = ENAMETOOLONG;
            install_fail("name", names[i]);
            goto done;
        }
        if (stat(from, &st)) continue;  /* dangling or unreadable: nothing to show */
        if (S_ISDIR(st.st_mode) ? mkdir(to, 0755) : close(open(to, O_WRONLY | O_CREAT, 0644))) {
            install_fail("make", to);
            goto done;
        }
        if (mount(from, to, NULL, MS_BIND | MS_REC, NULL)) {
            install_fail("bind back", to);
            goto done;
        }
    }
    snprintf(from, sizeof(from), "/proc/self/fd/%d", mine);
    if (mkdir(parent, 0771) || chown(parent, system_app.st_uid, system_app.st_gid) || chmod(parent, 0771)) {
        install_fail("make", parent);
        goto done;
    }
    if (mkdir(target, 0755)) {
        install_fail("make", target);
        goto done;
    }
    if (fchown(mine, system_app.st_uid, system_app.st_gid) || fchmod(mine, 0775)) {
        install_fail("own", source);
        goto done;
    }
    if (mount(from, target, NULL, MS_BIND, NULL)) {
        install_fail("bind", target);
        goto done;
    }
    rc = 1;
done:
    if (oh >= 0) close(oh);
    if (mine >= 0) close(mine);
    if (made >= 0) close(made);
    return rc;
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
    int install = install_dir(root, (uid_t) uid);
    if (install < 0) return 1;
    fprintf(stderr, "SOURCE_APP_NAMESPACE uid=%lu mount=%lu->%lu runtime=%s icu=%s etc=%s fonts=%s install=%s\n",
            uid, before.st_ino, after.st_ino, root, icu_data, etc ? "merged" : "absent", fonts ? "merged" : "absent",
            install ? getenv("WESTLAKE_ANDROID_APP_DIR") : "staged");
    execvp(argv[3], argv + 3);
    perror("source app exec");
    return 1;
}
