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

/* Development launcher boundary. No global mounts or original-data writes. */
int main(int argc, char **argv) {
    if (argc < 5 || getuid() != 0) {
        fprintf(stderr, "usage (root): private-data-launch OWNER_UID SOURCE TARGET COMMAND [ARGS...]\n");
        return 2;
    }
    char *end;
    unsigned long uid = strtoul(argv[1], &end, 10);
    if (*end || uid < 20000000 || uid > 0xfffffffful) return 2;
    char source[PATH_MAX], target[PATH_MAX];
    if (!realpath(argv[2], source) || !realpath(argv[3], target)) { perror("realpath"); return 1; }
    const char source_prefix[] = "/data/app/el2/100/base/org.westlake.imehost/files/";
    if (strcmp(source, argv[2]) || strcmp(target, argv[3]) ||
        strncmp(source, source_prefix, sizeof(source_prefix) - 1) ||
        strncmp(target, "/data/data/", 11) || !target[11] || strchr(target + 11, '/')) return 2;
    struct stat source_stat, namespace_before, namespace_after;
    if (lstat(source, &source_stat) || !S_ISDIR(source_stat.st_mode) || source_stat.st_uid != uid) return 2;
    if (stat("/proc/self/ns/mnt", &namespace_before) || unshare(CLONE_NEWNS) ||
        stat("/proc/self/ns/mnt", &namespace_after) || namespace_before.st_ino == namespace_after.st_ino) {
        perror("private mount namespace"); return 1;
    }
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) || mount(source, target, NULL, MS_BIND, NULL)) {
        perror("private data mount"); return 1;
    }
    struct stat mapped;
    if (stat(target, &mapped) || mapped.st_ino != source_stat.st_ino || mapped.st_dev != source_stat.st_dev) return 1;
    fprintf(stderr, "[OH_HOST_DATA] private namespace %lu -> %lu; mapped %s to isolated copy\n",
        namespace_before.st_ino, namespace_after.st_ino, target);
    execvp(argv[4], argv + 4);
    perror("exec"); return 1;
}
