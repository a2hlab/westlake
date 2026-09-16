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

void ALooper_wake(ALooper *looper)
{
    if (looper == NULL || looper->backend == NULL) {
        return;
    }
    typedef void (*Fn)(void *);
    Fn fn = (Fn)bridge_symbol("wl_ALooper_wake");
    if (fn != NULL) {
        fn(looper->backend);
    }
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

/*
 * Android CPU rendering clients import the lock/post half of the NDK native
 * window API from libandroid.so.  Terminate that ABI at OH's small
 * libsurface boundary.  Resolve the OH functions lazily so this compact DSO
 * stays usable by WebView before a native window is needed and does not gain
 * framework-only DT_NEEDED entries.
 */
typedef struct WlANativeWindowBuffer {
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    void *bits;
    uint32_t reserved[6];
} WlANativeWindowBuffer;

typedef struct WlOHNativeWindow WlOHNativeWindow;
typedef struct WlOHNativeWindowBuffer WlOHNativeWindowBuffer;
typedef struct WlOHNativeBuffer WlOHNativeBuffer;

typedef struct WlOHRegion {
    struct WlOHRect {
        int32_t x;
        int32_t y;
        uint32_t w;
        uint32_t h;
    } *rects;
    int32_t rect_number;
} WlOHRegion;

typedef struct WlOHBufferHandle {
    int32_t fd;
    int32_t width;
    int32_t stride;
    int32_t height;
    int32_t size;
    int32_t format;
    uint64_t usage;
    void *vir_addr;
    int32_t key;
    uint64_t phy_addr;
    uint32_t reserve_fds;
    uint32_t reserve_ints;
    int32_t reserve[];
} WlOHBufferHandle;

typedef struct WlAndroidRect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} WlAndroidRect;

typedef struct WlNativeWindowApi {
    int32_t (*handle_opt)(WlOHNativeWindow *, int, ...);
    int32_t (*request_buffer)(WlOHNativeWindow *,
                              WlOHNativeWindowBuffer **, int32_t *);
    int32_t (*flush_buffer)(WlOHNativeWindow *, WlOHNativeWindowBuffer *,
                            int32_t, WlOHRegion);
    int32_t (*cancel_buffer)(WlOHNativeWindow *, WlOHNativeWindowBuffer *);
    WlOHBufferHandle *(*get_buffer_handle)(WlOHNativeWindowBuffer *);
    WlOHNativeBuffer *(*native_buffer_from_window_buffer)(
            WlOHNativeWindowBuffer *);
    int32_t (*map)(WlOHNativeBuffer *, void **);
    int32_t (*map_wait_fence)(WlOHNativeBuffer *, int32_t, void **);
    void *library;
    int ready;
} WlNativeWindowApi;

static WlNativeWindowApi g_native_window_api;
static pthread_once_t g_native_window_api_once = PTHREAD_ONCE_INIT;

static void wl_init_native_window_api(void)
{
    /* Loading the bridge first makes its already validated OH dependencies
     * available in both the ordinary and isolated Android DSO namespaces. */
    (void)bridge_symbol("ANativeWindow_fromSurface");
    void *library = dlopen("libsurface.z.so", RTLD_NOW | RTLD_GLOBAL);
    if (library == NULL) {
        fprintf(stderr, "[WESTLAKE-NATIVEWINDOW] libsurface load failed: %s\n",
                dlerror());
        return;
    }
    g_native_window_api.handle_opt =
            (int32_t (*)(WlOHNativeWindow *, int, ...))
                    dlsym(library, "NativeWindowHandleOpt");
    g_native_window_api.request_buffer =
            (int32_t (*)(WlOHNativeWindow *, WlOHNativeWindowBuffer **,
                         int32_t *))dlsym(library, "NativeWindowRequestBuffer");
    g_native_window_api.flush_buffer =
            (int32_t (*)(WlOHNativeWindow *, WlOHNativeWindowBuffer *,
                         int32_t, WlOHRegion))
                    dlsym(library, "NativeWindowFlushBuffer");
    g_native_window_api.cancel_buffer =
            (int32_t (*)(WlOHNativeWindow *, WlOHNativeWindowBuffer *))
                    dlsym(library, "NativeWindowCancelBuffer");
    g_native_window_api.get_buffer_handle =
            (WlOHBufferHandle *(*)(WlOHNativeWindowBuffer *))
                    dlsym(library, "GetBufferHandleFromNative");
    g_native_window_api.native_buffer_from_window_buffer =
            (WlOHNativeBuffer *(*)(WlOHNativeWindowBuffer *))
                    dlsym(library, "OH_NativeBufferFromNativeWindowBuffer");
    g_native_window_api.map =
            (int32_t (*)(WlOHNativeBuffer *, void **))
                    dlsym(library, "OH_NativeBuffer_Map");
    g_native_window_api.map_wait_fence =
            (int32_t (*)(WlOHNativeBuffer *, int32_t, void **))
                    dlsym(library, "OH_NativeBuffer_MapWaitFence");
    g_native_window_api.library = library;
    g_native_window_api.ready =
            g_native_window_api.handle_opt != NULL &&
            g_native_window_api.request_buffer != NULL &&
            g_native_window_api.flush_buffer != NULL &&
            g_native_window_api.cancel_buffer != NULL &&
            g_native_window_api.get_buffer_handle != NULL &&
            g_native_window_api.native_buffer_from_window_buffer != NULL &&
            g_native_window_api.map != NULL &&
            g_native_window_api.map_wait_fence != NULL;
    if (!g_native_window_api.ready) {
        fprintf(stderr,
                "[WESTLAKE-NATIVEWINDOW] incomplete libsurface CPU boundary\n");
    }
}

static WlNativeWindowApi *wl_native_window_api(void)
{
    pthread_once(&g_native_window_api_once, wl_init_native_window_api);
    return g_native_window_api.ready ? &g_native_window_api : NULL;
}

typedef struct WlLockedPixels {
    void *window;
    void *oh_buffer;
    void *bits;
    void *mapped_bits;
    void *scratch_bits;
    size_t bits_size;
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    int32_t mapped_stride;
    int32_t mapped_format;
    int32_t dirty_x;
    int32_t dirty_y;
    uint32_t dirty_w;
    uint32_t dirty_h;
    int has_dirty;
    int valid;
} WlLockedPixels;

static __thread WlLockedPixels g_locked_pixels;

#define WL_ANDROID_FORMAT_YV12 ((int32_t)0x32315659)
#define WL_OH_FORMAT_RGBX_8888 11
#define WL_FORMAT_WINDOW_SLOTS 64

typedef struct WlRequestedFormat {
    void *window;
    int32_t format;
} WlRequestedFormat;

static WlRequestedFormat g_requested_formats[WL_FORMAT_WINDOW_SLOTS];
static unsigned int g_requested_format_next;

static void wl_remember_requested_format(void *window, int32_t format)
{
    if (window == NULL || format <= 0) return;
    for (size_t i = 0; i < WL_FORMAT_WINDOW_SLOTS; ++i) {
        if (__atomic_load_n(&g_requested_formats[i].window,
                            __ATOMIC_ACQUIRE) == window) {
            __atomic_store_n(&g_requested_formats[i].format, format,
                             __ATOMIC_RELEASE);
            return;
        }
    }
    const unsigned int slot = __atomic_fetch_add(
            &g_requested_format_next, 1, __ATOMIC_RELAXED) %
            WL_FORMAT_WINDOW_SLOTS;
    __atomic_store_n(&g_requested_formats[slot].format, format,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_requested_formats[slot].window, window,
                     __ATOMIC_RELEASE);
}

static int32_t wl_requested_format(void *window)
{
    for (size_t i = 0; i < WL_FORMAT_WINDOW_SLOTS; ++i) {
        if (__atomic_load_n(&g_requested_formats[i].window,
                            __ATOMIC_ACQUIRE) == window) {
            return __atomic_load_n(&g_requested_formats[i].format,
                                   __ATOMIC_ACQUIRE);
        }
    }
    return 0;
}

static int32_t wl_android_to_oh_format(int32_t format)
{
    switch (format) {
        case 1: return 12;
        case 2: return 11;
        case 3: return 13;
        case 4: return 3;
        case WL_ANDROID_FORMAT_YV12: return WL_OH_FORMAT_RGBX_8888;
        default: return format;
    }
}

static int32_t wl_oh_to_android_format(int32_t format)
{
    switch (format) {
        case 12: return 1;
        case 11: return 2;
        case 13: return 3;
        case 3: return 4;
        default: return format;
    }
}

static int32_t wl_android_bytes_per_pixel(int32_t format)
{
    switch (format) {
        case 1:
        case 2: return 4;
        case 3: return 3;
        case 4: return 2;
        default: return 1;
    }
}

static size_t wl_align_size(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static uint8_t wl_clamp_u8(int32_t value)
{
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

static void wl_convert_yv12_to_rgbx(const WlLockedPixels *locked)
{
    if (locked == NULL || locked->format != WL_ANDROID_FORMAT_YV12 ||
            locked->bits == NULL || locked->mapped_bits == NULL ||
            locked->width <= 0 || locked->height <= 0) {
        return;
    }
    const size_t y_stride = (size_t)locked->stride;
    const size_t chroma_stride = wl_align_size(y_stride / 2, 16);
    const size_t y_size = y_stride * (size_t)locked->height;
    const size_t chroma_height = ((size_t)locked->height + 1) / 2;
    const uint8_t *y_plane = (const uint8_t *)locked->bits;
    const uint8_t *v_plane = y_plane + y_size;
    const uint8_t *u_plane = v_plane + chroma_stride * chroma_height;
    uint8_t *rgbx = (uint8_t *)locked->mapped_bits;
    size_t rgbx_row_bytes = (size_t)locked->mapped_stride;
    const size_t tight_row_bytes = (size_t)locked->width * 4;
    if (rgbx_row_bytes < tight_row_bytes &&
            rgbx_row_bytes >= (size_t)locked->width) {
        rgbx_row_bytes *= 4;
    }
    if (rgbx_row_bytes < tight_row_bytes) return;
    for (int32_t y = 0; y < locked->height; ++y) {
        uint8_t *dst = rgbx + (size_t)y * rgbx_row_bytes;
        const uint8_t *src_y = y_plane + (size_t)y * y_stride;
        const uint8_t *src_v = v_plane + (size_t)(y / 2) * chroma_stride;
        const uint8_t *src_u = u_plane + (size_t)(y / 2) * chroma_stride;
        for (int32_t x = 0; x < locked->width; ++x) {
            const int32_t c = (int32_t)src_y[x] - 16;
            const int32_t d = (int32_t)src_u[x / 2] - 128;
            const int32_t e = (int32_t)src_v[x / 2] - 128;
            const int32_t positive_c = c > 0 ? c : 0;
            dst[(size_t)x * 4] = wl_clamp_u8(
                    (298 * positive_c + 409 * e + 128) >> 8);
            dst[(size_t)x * 4 + 1] = wl_clamp_u8(
                    (298 * positive_c - 100 * d - 208 * e + 128) >> 8);
            dst[(size_t)x * 4 + 2] = wl_clamp_u8(
                    (298 * positive_c + 516 * d + 128) >> 8);
            dst[(size_t)x * 4 + 3] = 255;
        }
    }
}

enum {
    WL_OH_SET_BUFFER_GEOMETRY = 0,
    WL_OH_GET_BUFFER_GEOMETRY = 1,
    WL_OH_GET_FORMAT = 2,
    WL_OH_SET_FORMAT = 3,
};

int32_t ANativeWindow_getWidth(void *window)
{
    WlNativeWindowApi *api = wl_native_window_api();
    int32_t width = 0;
    int32_t height = 0;
    if (api == NULL || window == NULL || api->handle_opt(
            (WlOHNativeWindow *)window, WL_OH_GET_BUFFER_GEOMETRY,
            &height, &width) != 0) {
        return 0;
    }
    return width;
}

int32_t ANativeWindow_getHeight(void *window)
{
    WlNativeWindowApi *api = wl_native_window_api();
    int32_t width = 0;
    int32_t height = 0;
    if (api == NULL || window == NULL || api->handle_opt(
            (WlOHNativeWindow *)window, WL_OH_GET_BUFFER_GEOMETRY,
            &height, &width) != 0) {
        return 0;
    }
    return height;
}

int32_t ANativeWindow_setBuffersGeometry(void *window, int32_t width,
                                         int32_t height, int32_t format)
{
    WlNativeWindowApi *api = wl_native_window_api();
    if (api == NULL || window == NULL) return -EINVAL;
    wl_remember_requested_format(window, format);
    int32_t result = api->handle_opt((WlOHNativeWindow *)window,
                                     WL_OH_SET_BUFFER_GEOMETRY,
                                     width, height);
    if (result == 0 && format > 0) {
        result = api->handle_opt((WlOHNativeWindow *)window, WL_OH_SET_FORMAT,
                                 wl_android_to_oh_format(format));
    }
    return result;
}

int32_t ANativeWindow_lock(void *window, void *out_buffer, void *dirty_bounds)
{
    WlNativeWindowApi *api = wl_native_window_api();
    WlOHNativeWindowBuffer *oh_buffer = NULL;
    int32_t result = -EINVAL;
    free(g_locked_pixels.scratch_bits);
    memset(&g_locked_pixels, 0, sizeof(g_locked_pixels));
    if (api == NULL || window == NULL || out_buffer == NULL) return result;

    WlOHRegion region = {NULL, 0};
    struct WlOHRect rect;
    int32_t fence_fd = -1;
    if (dirty_bounds != NULL) {
        const WlAndroidRect *android_rect =
                (const WlAndroidRect *)dirty_bounds;
        rect.x = android_rect->left;
        rect.y = android_rect->top;
        rect.w = android_rect->right > android_rect->left
                ? (uint32_t)(android_rect->right - android_rect->left) : 0;
        rect.h = android_rect->bottom > android_rect->top
                ? (uint32_t)(android_rect->bottom - android_rect->top) : 0;
        region.rects = &rect;
        region.rect_number = 1;
    }
    result = api->request_buffer((WlOHNativeWindow *)window, &oh_buffer,
                                 &fence_fd);
    if (result == 0 && oh_buffer != NULL) {
        WlOHNativeBuffer *native_buffer =
                api->native_buffer_from_window_buffer(oh_buffer);
        WlOHBufferHandle *handle = api->get_buffer_handle(oh_buffer);
        void *mapped_addr = NULL;
        result = native_buffer != NULL
                ? api->map_wait_fence(native_buffer, fence_fd, &mapped_addr)
                : -EINVAL;
        if (result != 0 && native_buffer != NULL) {
            result = api->map(native_buffer, &mapped_addr);
        }
        if (fence_fd >= 0) {
            close(fence_fd);
            fence_fd = -1;
        }
        if (result == 0 && mapped_addr == NULL && handle != NULL) {
            mapped_addr = handle->vir_addr;
        }
        if (handle == NULL || mapped_addr == NULL) result = -EINVAL;
        if (result == 0) {
            WlANativeWindowBuffer *android_buffer =
                    (WlANativeWindowBuffer *)out_buffer;
            const int32_t requested_format = wl_requested_format(window);
            const int32_t android_format =
                    requested_format == WL_ANDROID_FORMAT_YV12
                    ? WL_ANDROID_FORMAT_YV12
                    : wl_oh_to_android_format(handle->format);
            int32_t stride;
            void *android_bits = mapped_addr;
            size_t android_bits_size;
            if (android_format == WL_ANDROID_FORMAT_YV12) {
                const size_t y_stride = wl_align_size((size_t)handle->width, 16);
                const size_t chroma_stride = wl_align_size(y_stride / 2, 16);
                const size_t chroma_height = ((size_t)handle->height + 1) / 2;
                android_bits_size = y_stride * (size_t)handle->height +
                        2 * chroma_stride * chroma_height;
                android_bits = calloc(1, android_bits_size);
                stride = (int32_t)y_stride;
                if (android_bits == NULL) result = -ENOMEM;
            } else {
                const int32_t bytes_per_pixel =
                        wl_android_bytes_per_pixel(android_format);
                stride = handle->stride;
                if (bytes_per_pixel > 1 &&
                        stride >= handle->width * bytes_per_pixel &&
                        stride % bytes_per_pixel == 0) {
                    stride /= bytes_per_pixel;
                }
                android_bits_size = (size_t)stride * (size_t)handle->height *
                        (size_t)bytes_per_pixel;
            }
            if (result == 0) {
                android_buffer->width = handle->width;
                android_buffer->height = handle->height;
                android_buffer->stride = stride;
                android_buffer->format = android_format;
                android_buffer->bits = android_bits;
                memset(android_buffer->reserved, 0,
                       sizeof(android_buffer->reserved));
                g_locked_pixels.window = window;
                g_locked_pixels.oh_buffer = oh_buffer;
                g_locked_pixels.bits = android_bits;
                g_locked_pixels.mapped_bits = mapped_addr;
                g_locked_pixels.scratch_bits =
                        android_format == WL_ANDROID_FORMAT_YV12
                        ? android_bits : NULL;
                g_locked_pixels.bits_size = android_bits_size;
                g_locked_pixels.width = handle->width;
                g_locked_pixels.height = handle->height;
                g_locked_pixels.stride = stride;
                g_locked_pixels.format = android_format;
                g_locked_pixels.mapped_stride = handle->stride;
                g_locked_pixels.mapped_format = handle->format;
                if (dirty_bounds != NULL) {
                    const WlAndroidRect *android_rect =
                            (const WlAndroidRect *)dirty_bounds;
                    g_locked_pixels.dirty_x = android_rect->left;
                    g_locked_pixels.dirty_y = android_rect->top;
                    g_locked_pixels.dirty_w = rect.w;
                    g_locked_pixels.dirty_h = rect.h;
                    g_locked_pixels.has_dirty = 1;
                }
                g_locked_pixels.valid = 1;
            }
        }
        if (result != 0) {
            free(g_locked_pixels.scratch_bits);
            memset(&g_locked_pixels, 0, sizeof(g_locked_pixels));
            (void)api->cancel_buffer((WlOHNativeWindow *)window, oh_buffer);
        }
    }
    if (fence_fd >= 0) close(fence_fd);
    return result;
}

int32_t ANativeWindow_unlockAndPost(void *window)
{
    WlNativeWindowApi *api = wl_native_window_api();
    int32_t result = -EINVAL;
    if (api != NULL && window != NULL && g_locked_pixels.valid &&
            g_locked_pixels.window == window &&
            g_locked_pixels.oh_buffer != NULL) {
        wl_convert_yv12_to_rgbx(&g_locked_pixels);
        WlOHRegion region = {NULL, 0};
        struct WlOHRect rect;
        if (g_locked_pixels.has_dirty) {
            rect.x = g_locked_pixels.dirty_x;
            rect.y = g_locked_pixels.dirty_y;
            rect.w = g_locked_pixels.dirty_w;
            rect.h = g_locked_pixels.dirty_h;
            region.rects = &rect;
            region.rect_number = 1;
        }
        result = api->flush_buffer((WlOHNativeWindow *)window,
                                   (WlOHNativeWindowBuffer *)
                                           g_locked_pixels.oh_buffer,
                                   -1, region);
    }
    free(g_locked_pixels.scratch_bits);
    memset(&g_locked_pixels, 0, sizeof(g_locked_pixels));
    return result;
}
