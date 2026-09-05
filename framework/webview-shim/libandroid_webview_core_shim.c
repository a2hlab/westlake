/*
 * Minimal Android NDK surface used by the pinned AOSP WebView provider.
 *
 * The functions here forward to liboh_adapter_bridge.so, which owns the
 * Android-to-OHOS object adaptation. Keep ASurfaceControl out of this DSO
 * until its complete transaction API is implemented: Chromium treats a
 * partially visible ASurfaceControl table as usable and enters an invalid
 * hardware-compositor path.
 */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int (*ALooper_callbackFunc)(int fd, int events, void *data);

typedef struct ALooper {
    int references;
    void *backend;
} ALooper;

static __thread ALooper *g_thread_looper;

/*
 * Bionic system-property boundary used by Android NDK libraries.  OHOS does
 * not export Bionic's prop_info ABI, so keep stable opaque records here and
 * populate them from the native SystemReadParam service.
 */
#define WL_PROP_NAME_MAX 128
#define WL_PROP_VALUE_MAX 256
#define WL_PROP_CACHE_SIZE 64

struct prop_info {
    char name[WL_PROP_NAME_MAX];
    char value[WL_PROP_VALUE_MAX];
};

static struct prop_info g_property_cache[WL_PROP_CACHE_SIZE];
static size_t g_property_count;
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
    if (name == NULL || value == NULL || value_size == 0) return 0;
    value[0] = '\0';
    if (read_param != NULL) {
        uint32_t length = (uint32_t)value_size;
        if (read_param(name, value, &length) == 0 && value[0] != '\0') {
            value[value_size - 1] = '\0';
            return (int)strlen(value);
        }
    }
    /* Keep native NDK property reads consistent with Android's Java bridge. */
    if (strcmp(name, "ro.build.version.sdk") == 0) {
        snprintf(value, value_size, "%s", "34");
        return 2;
    }
    value[0] = '\0';
    return 0;
}

const struct prop_info *__system_property_find(const char *name)
{
    char value[WL_PROP_VALUE_MAX];
    const int value_length = read_oh_property(name, value, sizeof(value));
    fprintf(stderr, "[WESTLAKE-BIONIC-PROP] find name=%s len=%d\n",
            name != NULL ? name : "<null>", value_length);
    fflush(stderr);
    if (value_length == 0) return NULL;

    pthread_mutex_lock(&g_property_mutex);
    struct prop_info *entry = NULL;
    for (size_t i = 0; i < g_property_count; ++i) {
        if (strcmp(g_property_cache[i].name, name) == 0) {
            entry = &g_property_cache[i];
            break;
        }
    }
    if (entry == NULL && g_property_count < WL_PROP_CACHE_SIZE) {
        entry = &g_property_cache[g_property_count++];
        snprintf(entry->name, sizeof(entry->name), "%s", name);
    }
    if (entry != NULL) {
        snprintf(entry->value, sizeof(entry->value), "%s", value);
    }
    pthread_mutex_unlock(&g_property_mutex);
    return entry;
}

int __system_property_read(const struct prop_info *property,
                           char *name, char *value)
{
    if (property == NULL) return -1;
    if (name != NULL) snprintf(name, WL_PROP_NAME_MAX, "%s", property->name);
    if (value != NULL) snprintf(value, WL_PROP_VALUE_MAX, "%s", property->value);
    return (int)strlen(property->value);
}

/* Bionic's fortified three-argument openat rejects flags that require mode. */
int __openat_2(int dir_fd, const char *path, int flags)
{
    int needs_mode = (flags & O_CREAT) != 0;
#ifdef O_TMPFILE
    needs_mode = needs_mode || ((flags & O_TMPFILE) == O_TMPFILE);
#endif
    fprintf(stderr,
            "[WESTLAKE-BIONIC-OPENAT2] dirfd=%d path=%s flags=%#x needs_mode=%d\n",
            dir_fd, path != NULL ? path : "<null>", flags, needs_mode);
    fflush(stderr);
    if (needs_mode) abort();
    const int result = openat(dir_fd, path, flags);
    fprintf(stderr, "[WESTLAKE-BIONIC-OPENAT2] result=%d errno=%d\n",
            result, result < 0 ? errno : 0);
    fflush(stderr);
    return result;
}

static void *bridge_symbol(const char *name)
{
    static void *bridge;
    if (bridge == NULL) {
        bridge = dlopen("liboh_adapter_bridge.so", RTLD_NOW | RTLD_GLOBAL);
        if (bridge == NULL) {
            fprintf(stderr, "[WESTLAKE-WEBVIEW-NDK] bridge load failed: %s\n",
                    dlerror());
            return NULL;
        }
    }

    void *symbol = dlsym(bridge, name);
    if (symbol == NULL) {
        fprintf(stderr, "[WESTLAKE-WEBVIEW-NDK] bridge symbol %s missing: %s\n",
                name, dlerror());
    }
    return symbol;
}

ALooper *ALooper_prepare(int opts)
{
    if (g_thread_looper == NULL) {
        g_thread_looper = (ALooper *)calloc(1, sizeof(*g_thread_looper));
        if (g_thread_looper == NULL) {
            return NULL;
        }
        g_thread_looper->references = 1;
    }
    if (g_thread_looper->backend == NULL) {
        typedef void *(*Fn)(int);
        Fn fn = (Fn)bridge_symbol("wl_ALooper_prepare");
        if (fn != NULL) {
            g_thread_looper->backend = fn(opts);
        }
    }
    fprintf(stderr,
            "[WESTLAKE-WEBVIEW-NDK] ALooper_prepare opts=%d looper=%p backend=%p\n",
            opts, (void *)g_thread_looper, g_thread_looper->backend);
    fflush(stderr);
    return g_thread_looper;
}

ALooper *ALooper_forThread(void)
{
    return g_thread_looper;
}

int ALooper_pollOnce(int timeout_millis, int *out_fd, int *out_events,
                     void **out_data)
{
    /*
     * This compact libandroid is used on sensorless OH boards.  Java's
     * MessageQueue owns and dispatches every registered backend fd, so there
     * is no second fd set to poll here.  Preserve the NDK timeout contract
     * without spinning if optional native code runs its own sensor loop.
     */
    if (out_fd != NULL) *out_fd = -1;
    if (out_events != NULL) *out_events = 0;
    if (out_data != NULL) *out_data = NULL;
    const int wait_millis = timeout_millis < 0 ? 1000 : timeout_millis;
    int rc;
    do {
        rc = poll(NULL, 0, wait_millis);
    } while (rc < 0 && errno == EINTR);
    return rc < 0 ? -4 : -3; /* ALOOPER_POLL_ERROR / _TIMEOUT */
}

void ALooper_acquire(ALooper *looper)
{
    if (looper != NULL) {
        __atomic_add_fetch(&looper->references, 1, __ATOMIC_RELAXED);
    }
}

void ALooper_release(ALooper *looper)
{
    if (looper != NULL && looper->references > 1) {
        __atomic_sub_fetch(&looper->references, 1, __ATOMIC_RELAXED);
    }
}

int ALooper_addFd(ALooper *looper, int fd, int ident, int events,
                  ALooper_callbackFunc callback, void *data)
{
    int result = -1;
    if (looper != NULL && looper->backend != NULL) {
        typedef int (*Fn)(void *, int, int, int,
                          ALooper_callbackFunc, void *);
        Fn fn = (Fn)bridge_symbol("wl_ALooper_addFd");
        if (fn != NULL) {
            result = fn(looper->backend, fd, ident, events, callback, data);
        }
    }
    fprintf(stderr,
            "[WESTLAKE-WEBVIEW-NDK] ALooper_addFd looper=%p backend=%p fd=%d"
            " ident=%d events=%d callback=%p data=%p result=%d\n",
            (void *)looper, looper != NULL ? looper->backend : NULL,
            fd, ident, events, (void *)callback, data, result);
    fflush(stderr);
    return result;
}

int ALooper_removeFd(ALooper *looper, int fd)
{
    int result = -1;
    if (looper != NULL && looper->backend != NULL) {
        typedef int (*Fn)(void *, int);
        Fn fn = (Fn)bridge_symbol("wl_ALooper_removeFd");
        if (fn != NULL) {
            result = fn(looper->backend, fd);
        }
    }
    fprintf(stderr,
            "[WESTLAKE-WEBVIEW-NDK] ALooper_removeFd looper=%p backend=%p"
            " fd=%d result=%d\n",
            (void *)looper, looper != NULL ? looper->backend : NULL,
            fd, result);
    fflush(stderr);
    return result;
}

/*
 * Android NDK sensor boundary for boards with no physical Android sensors.
 * A non-null manager is a valid service handle; an absent default sensor is
 * the documented way to report an unsupported sensor type.  Keep the queue
 * object valid as well so clients that create it before checking the sensor
 * can tear it down normally.
 */
typedef struct ASensorManager { int unused; } ASensorManager;
typedef struct ASensorEventQueue { int unused; } ASensorEventQueue;
typedef struct ASensor { int unused; } ASensor;
typedef struct ASensorEvent { uint8_t opaque[104]; } ASensorEvent;

static ASensorManager g_sensor_manager;
static ASensorEventQueue g_sensor_queue;

ASensorManager *ASensorManager_getInstance(void)
{
    return &g_sensor_manager;
}

const ASensor *ASensorManager_getDefaultSensor(ASensorManager *manager,
                                                int sensor_type)
{
    (void)manager;
    (void)sensor_type;
    return NULL;
}

ASensorEventQueue *ASensorManager_createEventQueue(
        ASensorManager *manager, ALooper *looper, int ident,
        ALooper_callbackFunc callback, void *data)
{
    (void)looper;
    (void)ident;
    (void)callback;
    (void)data;
    return manager != NULL ? &g_sensor_queue : NULL;
}

int ASensorManager_destroyEventQueue(ASensorManager *manager,
                                     ASensorEventQueue *queue)
{
    return manager != NULL && queue != NULL ? 0 : -EINVAL;
}

int ASensorEventQueue_enableSensor(ASensorEventQueue *queue,
                                   const ASensor *sensor)
{
    return queue != NULL && sensor != NULL ? 0 : -EINVAL;
}

int ASensorEventQueue_disableSensor(ASensorEventQueue *queue,
                                    const ASensor *sensor)
{
    return queue != NULL && sensor != NULL ? 0 : -EINVAL;
}

int ASensorEventQueue_getEvents(ASensorEventQueue *queue,
                                ASensorEvent *events, size_t count)
{
    (void)events;
    (void)count;
    return queue != NULL ? 0 : -EINVAL;
}

void *ANativeWindow_fromSurface(void *env, void *surface)
{
    typedef void *(*Fn)(void *, void *);
    Fn fn = (Fn)bridge_symbol("ANativeWindow_fromSurface");
    return fn != NULL ? fn(env, surface) : NULL;
}

void ANativeWindow_acquire(void *window)
{
    typedef void (*Fn)(void *);
    Fn fn = (Fn)bridge_symbol("ANativeWindow_acquire");
    if (fn != NULL) {
        fn(window);
    }
}

void ANativeWindow_release(void *window)
{
    typedef void (*Fn)(void *);
    Fn fn = (Fn)bridge_symbol("ANativeWindow_release");
    if (fn != NULL) {
        fn(window);
    }
}

int32_t ANativeWindow_getFormat(void *window)
{
    typedef int32_t (*Fn)(void *);
    Fn fn = (Fn)bridge_symbol("ANativeWindow_getFormat");
    return fn != NULL ? fn(window) : 0;
}
