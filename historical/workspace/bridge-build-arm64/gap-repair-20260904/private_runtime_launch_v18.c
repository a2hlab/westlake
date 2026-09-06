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

/* Map installer-staged, app-readable runtime files only in this process tree. */
int main(int argc, char **argv) {
    if (argc < 3 || getuid() != 0) return 2;
    char source[PATH_MAX];
    const char prefix[] = "/data/app/el2/100/base/org.westlake.imehost/files/android-runtime-gap-";
    struct stat owner, before, after, mapped;
    if (!realpath(argv[1], source) || strcmp(source, argv[1]) ||
        strncmp(source, prefix, sizeof(prefix)-1) ||
        lstat(source, &owner) || !S_ISDIR(owner.st_mode) || owner.st_uid != 20010054) return 2;
    if (stat("/proc/self/ns/mnt", &before) || unshare(CLONE_NEWNS) ||
        stat("/proc/self/ns/mnt", &after) || before.st_ino == after.st_ino ||
        mount(NULL, "/", NULL, MS_REC|MS_PRIVATE, NULL)) {
        perror("private runtime namespace"); return 1;
    }
    char temporary[PATH_MAX], system_etc[PATH_MAX];
    if (snprintf(temporary,sizeof(temporary),"%s-tmp",source) >= (int)sizeof(temporary) ||
        snprintf(system_etc,sizeof(system_etc),"%s-etc",source) >= (int)sizeof(system_etc)) return 2;
    struct stat tmp_owner, etc_owner;
    if (lstat(temporary,&tmp_owner) || !S_ISDIR(tmp_owner.st_mode) || tmp_owner.st_uid != owner.st_uid ||
        lstat(system_etc,&etc_owner) || !S_ISDIR(etc_owner.st_mode) || etc_owner.st_uid != owner.st_uid) return 2;
    // The /data/local/tmp parent itself is inaccessible to normal apps. Map a
    // private app-owned parent as well as the runtime below it.
    if (mount(temporary,"/data/local/tmp",NULL,MS_BIND,NULL) ||
        mount(system_etc,"/system/etc",NULL,MS_BIND,NULL)) {
        perror("private runtime parents"); return 1;
    }
    const char *asx = "/data/local/tmp/asx";
    if (mount(source, asx, NULL, MS_BIND, NULL) || stat(asx, &mapped) ||
        mapped.st_ino != owner.st_ino || mapped.st_dev != owner.st_dev) {
        perror("private runtime mount"); return 1;
    }
    // Real font files retain the platform font label; avoid an app-data symlink.
    if (mount("/system/fonts", "/data/local/tmp/asx/android-root/fonts", NULL, MS_BIND, NULL) ||
        mount("/data/local/tmp/asx/android-root", "/system/android", NULL, MS_BIND|MS_REC, NULL)) {
        perror("private Android resources"); return 1;
    }
    fprintf(stderr, "[OH_RUNTIME_FILES] namespace %lu -> %lu source=%s inode=%lu\n",
            before.st_ino, after.st_ino, source, owner.st_ino);
    execvp(argv[2], argv+2);
    perror("exec"); return 1;
}
