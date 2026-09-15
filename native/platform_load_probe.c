#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

int main(int argc, char** argv) {
    const char* owner = getenv("WESTLAKE_NATIVE_PROBE_UID");
    if (owner) {
        char* end = NULL;
        errno = 0;
        unsigned long uid = strtoul(owner, &end, 10);
        if (errno || end == owner || *end || uid == 0 || uid > UINT_MAX
                || setgid((gid_t)uid) || setuid((uid_t)uid)) {
            perror("SOURCE_NATIVE_PROBE_UID_FAILED");
            return 1;
        }
    }
    fprintf(stderr, "SOURCE_NATIVE_PROBE_UID %u\n", (unsigned)getuid());
    for (int i = 1; i < argc; ++i) {
        fprintf(stderr, "SOURCE_NATIVE_LOADING %s\n", argv[i]);
        void* handle = dlopen(argv[i], RTLD_NOW | RTLD_GLOBAL);
        if (!handle) {
            fprintf(stderr, "SOURCE_NATIVE_LOAD_FAILED %s: %s\n", argv[i], dlerror());
            return 1;
        }
        fprintf(stderr, "SOURCE_NATIVE_LOADED %s\n", argv[i]);
    }
    puts("SOURCE_NATIVE_LOAD_PASS");
    return 0;
}
