#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "SOURCE_LEGACY_CPP_FAIL line=%d %s\n", __LINE__, #condition); \
    exit(1); } } while (0)

static int (*acquire_guard)(uint64_t*);
static void (*release_guard)(uint64_t*);
static uint64_t shared_guard;
static unsigned initialized;
static pthread_barrier_t ready;

static void* initialize_once(void* unused) {
    (void)unused;
    int result = pthread_barrier_wait(&ready);
    CHECK(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
    if (acquire_guard(&shared_guard)) {
        ++initialized;
        release_guard(&shared_guard);
    }
    return NULL;
}

int main(void) {
    void* library = dlopen("libstdc++.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) fprintf(stderr, "%s\n", dlerror());
    CHECK(library);
    acquire_guard = (int (*)(uint64_t*))dlsym(library, "__cxa_guard_acquire");
    release_guard = (void (*)(uint64_t*))dlsym(library, "__cxa_guard_release");
    void (*abort_guard)(uint64_t*) = (void (*)(uint64_t*))dlsym(library, "__cxa_guard_abort");
    void* (*allocate)(size_t) = (void* (*)(size_t))dlsym(library, "_Znwm");
    void (*deallocate)(void*) = (void (*)(void*))dlsym(library, "_ZdlPv");
    int (*format)(char*, size_t, const char*, ...) =
            (int (*)(char*, size_t, const char*, ...))dlsym(library, "async_safe_format_buffer");
    CHECK(acquire_guard && release_guard && abort_guard && allocate && deallocate && format);
    Dl_info provider;
    CHECK(dladdr((void*)acquire_guard, &provider) && provider.dli_fname);
    const char* base = strrchr(provider.dli_fname, '/');
    CHECK(strcmp(base ? base + 1 : provider.dli_fname, "libstdc++.so") == 0);
    fprintf(stderr, "SOURCE_LEGACY_CPP_PROVIDER %s\n", provider.dli_fname);

    uint64_t retry_guard = 0;
    CHECK(acquire_guard(&retry_guard) == 1);
    abort_guard(&retry_guard);
    CHECK(acquire_guard(&retry_guard) == 1);
    release_guard(&retry_guard);
    CHECK(acquire_guard(&retry_guard) == 0 && (retry_guard & 255) == 1);

    pthread_t workers[8];
    CHECK(pthread_barrier_init(&ready, NULL, 8) == 0);
    for (unsigned i = 0; i < 8; ++i) CHECK(pthread_create(&workers[i], NULL, initialize_once, NULL) == 0);
    for (unsigned i = 0; i < 8; ++i) CHECK(pthread_join(workers[i], NULL) == 0);
    CHECK(pthread_barrier_destroy(&ready) == 0);
    CHECK(initialized == 1 && (shared_guard & 255) == 1);

    unsigned char* bytes = allocate(37);
    CHECK(bytes);
    memset(bytes, 0x6d, 37);
    for (unsigned i = 0; i < 37; ++i) CHECK(bytes[i] == 0x6d);
    deallocate(bytes);
    char formatted[64];
    CHECK(format(formatted, sizeof(formatted), "%s:%d:%zu", "source", -7, (size_t)37) == 12);
    CHECK(strcmp(formatted, "source:-7:37") == 0);
    CHECK(dlclose(library) == 0);
    puts("SOURCE_LEGACY_CPP_PASS threads=8 initializations=1 abort_retry=1 allocation=37 formatting=1");
    return 0;
}
