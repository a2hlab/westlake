#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <unistd.h>

#include <jni.h>

#define WL_ALOOPER_MAX_FDS 64

typedef int (*ALooper_callbackFunc)(int fd, int events, void *data);

typedef struct ALooperFd {
    int fd;
    int ident;
    int events;
    ALooper_callbackFunc callback;
    void *data;
} ALooperFd;

typedef struct ALooper {
    int references;
    void *backend;
    int wake_fd;
    ALooperFd fds[WL_ALOOPER_MAX_FDS];
} ALooper;

static __thread ALooper *g_thread_looper;

/* Stable Bionic prop_info handles backed by the OHOS system-parameter API. */
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

/*
 * Keep the most recent successful CPU lock thread-local so an opt-in boundary
 * trace can distinguish "the decoder posted black pixels" from "non-black
 * pixels were lost by the OH TextureView consumer".  Android's lock/post API
 * is paired on one producer thread; clearing this record on every failed lock
 * also prevents a buggy caller from making a failed lock look like a repeated
 * post of the previous buffer.
 */
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

/*
 * Android's software video decoders are allowed to select the legacy YV12
 * FourCC (Y + V + U planar 4:2:0).  OH NativeWindow expects its own compact
 * GraphicPixelFormat enum, so passing the Android FourCC through makes the
 * producer permanently fail RequestBuffer with SURFACE_ERROR_ERROR.  Remember
 * the Android request per native window; YV12 is allocated as an OH RGBX buffer
 * and converted at unlock/post, keeping both sides of the boundary on their
 * documented formats.
 */
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
        if (__atomic_load_n(&g_requested_formats[i].window, __ATOMIC_ACQUIRE) == window) {
            __atomic_store_n(&g_requested_formats[i].format, format, __ATOMIC_RELEASE);
            return;
        }
    }
    const unsigned int slot = __atomic_fetch_add(
            &g_requested_format_next, 1, __ATOMIC_RELAXED) % WL_FORMAT_WINDOW_SLOTS;
    __atomic_store_n(&g_requested_formats[slot].format, format, __ATOMIC_RELAXED);
    __atomic_store_n(&g_requested_formats[slot].window, window, __ATOMIC_RELEASE);
}

static int32_t wl_requested_format(void *window)
{
    for (size_t i = 0; i < WL_FORMAT_WINDOW_SLOTS; ++i) {
        if (__atomic_load_n(&g_requested_formats[i].window, __ATOMIC_ACQUIRE) == window) {
            return __atomic_load_n(&g_requested_formats[i].format, __ATOMIC_ACQUIRE);
        }
    }
    return 0;
}

/*
 * WESTLAKE boundary diagnostic.  Keep this opt-in because libandroid.so is
 * shared by WebView, HWUI, and app native code.  The short-video player is a
 * direct client of these Android NDK entry points; logging here establishes
 * whether decoded frames actually cross into the OH NativeWindow queue.
 */
static int wl_trace_native_window(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *value = getenv("WESTLAKE_TRACE_ANDROID_NATIVE_WINDOW");
        cached = value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
    }
    return cached;
}

static unsigned int wl_native_window_count(unsigned int *counter)
{
    return __atomic_add_fetch(counter, 1, __ATOMIC_RELAXED);
}

typedef struct WlANativeWindowBuffer {
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    void *bits;
    uint32_t reserved[6];
} WlANativeWindowBuffer;

/*
 * WESTLAKE §772: compact OH NativeWindow boundary for Android-native clients.
 *
 * The Android player namespace cannot reopen the full adapter bridge because
 * that DSO pulls in framework-only OH dependencies.  More importantly, the
 * bridge never exported ANativeWindow_lock/unlockAndPost, which libttmplayer
 * imports.  Keep the Android NDK ABI here and terminate it at the small OH
 * libsurface boundary instead.
 */
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

extern int32_t NativeObjectReference(void *object);
extern int32_t NativeObjectUnreference(void *object);
extern int32_t NativeWindowHandleOpt(WlOHNativeWindow *window, int code, ...);
extern int32_t NativeWindowRequestBuffer(WlOHNativeWindow *window,
                                         WlOHNativeWindowBuffer **buffer,
                                         int32_t *fence_fd);
extern int32_t NativeWindowFlushBuffer(WlOHNativeWindow *window,
                                       WlOHNativeWindowBuffer *buffer,
                                       int32_t fence_fd, WlOHRegion region);
extern int32_t NativeWindowCancelBuffer(WlOHNativeWindow *window,
                                        WlOHNativeWindowBuffer *buffer);
extern int32_t NativeWindowLockBuffer(WlOHNativeWindow *window,
                                      WlOHRegion region,
                                      WlOHNativeWindowBuffer **buffer);
extern int32_t NativeWindowUnlockAndFlushBuffer(WlOHNativeWindow *window);
extern WlOHBufferHandle *GetBufferHandleFromNative(WlOHNativeWindowBuffer *buffer);
extern WlOHNativeBuffer *OH_NativeBufferFromNativeWindowBuffer(
        WlOHNativeWindowBuffer *buffer);
extern int32_t OH_NativeBuffer_Map(WlOHNativeBuffer *buffer, void **vir_addr);
extern int32_t OH_NativeBuffer_MapWaitFence(WlOHNativeBuffer *buffer,
                                             int32_t fence_fd,
                                             void **vir_addr);

enum {
    WL_OH_SET_BUFFER_GEOMETRY = 0,
    WL_OH_GET_BUFFER_GEOMETRY = 1,
    WL_OH_GET_FORMAT = 2,
    WL_OH_SET_FORMAT = 3,
    WL_OH_GET_USAGE = 4,
    WL_OH_SET_USAGE = 5,
};

/* Android HAL formats used by ANativeWindow, translated to OH graphic formats. */
static int32_t wl_android_to_oh_format(int32_t format)
{
    switch (format) {
        case 1: return 12; /* AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM -> RGBA_8888 */
        case 2: return 11; /* R8G8B8X8_UNORM -> RGBX_8888 */
        case 3: return 13; /* R8G8B8_UNORM -> RGB_888 */
        case 4: return 3;  /* R5G6B5_UNORM -> RGB_565 */
        case WL_ANDROID_FORMAT_YV12:
            /* See wl_convert_yv12_to_rgbx(): OH TextureLayer consumes RGBX. */
            return WL_OH_FORMAT_RGBX_8888;
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

/* Android HAL_PIXEL_FORMAT_YV12 is Y, then V, then U, with 16-byte strides. */
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
    const size_t tight_rgbx_row_bytes = (size_t)locked->width * 4;
    if (rgbx_row_bytes < tight_rgbx_row_bytes &&
            rgbx_row_bytes >= (size_t)locked->width) {
        rgbx_row_bytes *= 4;
    }
    if (rgbx_row_bytes < tight_rgbx_row_bytes) return;

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
            dst[(size_t)x * 4 + 0] = wl_clamp_u8(
                    (298 * positive_c + 409 * e + 128) >> 8);
            dst[(size_t)x * 4 + 1] = wl_clamp_u8(
                    (298 * positive_c - 100 * d - 208 * e + 128) >> 8);
            dst[(size_t)x * 4 + 2] = wl_clamp_u8(
                    (298 * positive_c + 516 * d + 128) >> 8);
            dst[(size_t)x * 4 + 3] = 255;
        }
    }
}

static void *bridge_symbol(const char *name)
{
    static void *bridge;
    static void *asset_bridge;
    if (bridge == NULL) {
        bridge = dlopen("liboh_adapter_bridge.so", RTLD_NOW | RTLD_GLOBAL);
        if (bridge == NULL) {
            fprintf(stderr, "[WESTLAKE-WEBVIEW-NDK] bridge load failed: %s\n", dlerror());
            return NULL;
        }
    }

    void *symbol = dlsym(bridge, name);
    /*
     * Older validated bridge builds predate the Android NDK AssetManager C
     * boundary.  Loading a newly relinked four-megabyte framework bridge just
     * for that narrow API also perturbs unrelated ART/GC state on this port.
     * Resolve only wl_AAsset* through a small companion which reuses the
     * already-loaded bridge's Java AssetManager and AssetManager2 objects.
     * Keep this lazy: the companion has framework C++ relocations and must not
     * be pulled into appspawn before the runtime bridge is fully initialized.
     */
    if (symbol == NULL && strncmp(name, "wl_AAsset", 9) == 0) {
        if (asset_bridge == NULL) {
            asset_bridge = dlopen("libwestlake_asset_bridge.so",
                                  RTLD_NOW | RTLD_GLOBAL);
            if (asset_bridge == NULL) {
                fprintf(stderr,
                        "[WESTLAKE-WEBVIEW-NDK] asset bridge load failed: %s\n",
                        dlerror());
                return NULL;
            }
        }
        symbol = dlsym(asset_bridge, name);
    }
    if (symbol == NULL) {
        fprintf(stderr, "[WESTLAKE-WEBVIEW-NDK] bridge symbol %s missing: %s\n",
                name, dlerror());
    }
    return symbol;
}

/*
 * Android NDK asset API.
 *
 * AssetManager2 and Asset ownership stay in liboh_adapter_bridge.so; this
 * libandroid.so boundary only forwards the stable C ABI.  This mirrors the
 * split used by ALooper above and lets ordinary app-native libraries consume
 * the same assets as android.content.res.AssetManager without importing the
 * framework's C++ implementation into their linker namespace.
 */
typedef struct AAssetManager AAssetManager;
typedef struct AAssetDir AAssetDir;
typedef struct AAsset AAsset;

AAssetManager *AAssetManager_fromJava(JNIEnv *env, jobject asset_manager)
{
    typedef void *(*Fn)(JNIEnv *, jobject);
    static Fn fn;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAssetManager_fromJava");
    return fn != NULL ? (AAssetManager *)fn(env, asset_manager) : NULL;
}

AAsset *AAssetManager_open(AAssetManager *manager, const char *filename, int mode)
{
    typedef void *(*Fn)(void *, const char *, int);
    static Fn fn;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAssetManager_open");
    return fn != NULL ? (AAsset *)fn(manager, filename, mode) : NULL;
}

AAssetDir *AAssetManager_openDir(AAssetManager *manager, const char *directory_name)
{
    typedef void *(*Fn)(void *, const char *);
    static Fn fn;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAssetManager_openDir");
    return fn != NULL ? (AAssetDir *)fn(manager, directory_name) : NULL;
}

const char *AAssetDir_getNextFileName(AAssetDir *directory)
{
    typedef const char *(*Fn)(void *);
    static Fn fn;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAssetDir_getNextFileName");
    return fn != NULL ? fn(directory) : NULL;
}

void AAssetDir_rewind(AAssetDir *directory)
{
    typedef void (*Fn)(void *);
    static Fn fn;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAssetDir_rewind");
    if (fn != NULL) fn(directory);
}

void AAssetDir_close(AAssetDir *directory)
{
    typedef void (*Fn)(void *);
    static Fn fn;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAssetDir_close");
    if (fn != NULL) fn(directory);
}

int AAsset_read(AAsset *asset, void *buffer, size_t count)
{
    typedef int (*Fn)(void *, void *, size_t);
    static Fn fn;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAsset_read");
    return fn != NULL ? fn(asset, buffer, count) : -1;
}

int64_t AAsset_seek64(AAsset *asset, int64_t offset, int whence)
{
    typedef int64_t (*Fn)(void *, int64_t, int);
    static Fn fn;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAsset_seek64");
    return fn != NULL ? fn(asset, offset, whence) : (int64_t)-1;
}

off_t AAsset_seek(AAsset *asset, off_t offset, int whence)
{
    return (off_t)AAsset_seek64(asset, (int64_t)offset, whence);
}

void AAsset_close(AAsset *asset)
{
    typedef void (*Fn)(void *);
    static Fn fn;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAsset_close");
    if (fn != NULL) fn(asset);
}

const void *AAsset_getBuffer(AAsset *asset)
{
    typedef const void *(*Fn)(void *);
    static Fn fn;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAsset_getBuffer");
    return fn != NULL ? fn(asset) : NULL;
}

int64_t AAsset_getLength64(AAsset *asset)
{
    typedef int64_t (*Fn)(void *);
    static Fn fn;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAsset_getLength64");
    return fn != NULL ? fn(asset) : 0;
}

off_t AAsset_getLength(AAsset *asset)
{
    return (off_t)AAsset_getLength64(asset);
}

int64_t AAsset_getRemainingLength64(AAsset *asset)
{
    typedef int64_t (*Fn)(void *);
    static Fn fn;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAsset_getRemainingLength64");
    return fn != NULL ? fn(asset) : 0;
}

off_t AAsset_getRemainingLength(AAsset *asset)
{
    return (off_t)AAsset_getRemainingLength64(asset);
}

int AAsset_openFileDescriptor64(AAsset *asset, int64_t *out_start,
                                int64_t *out_length)
{
    typedef int (*Fn)(void *, int64_t *, int64_t *);
    static Fn fn;
    int64_t start = 0;
    int64_t length = 0;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAsset_openFileDescriptor64");
    const int fd = fn != NULL ? fn(asset, &start, &length) : -1;
    if (fd >= 0) {
        if (out_start != NULL) *out_start = start;
        if (out_length != NULL) *out_length = length;
    }
    return fd;
}

int AAsset_openFileDescriptor(AAsset *asset, off_t *out_start, off_t *out_length)
{
    int64_t start = 0;
    int64_t length = 0;
    const int fd = AAsset_openFileDescriptor64(asset, &start, &length);
    if (fd >= 0) {
        if (out_start != NULL) *out_start = (off_t)start;
        if (out_length != NULL) *out_length = (off_t)length;
    }
    return fd;
}

int AAsset_isAllocated(AAsset *asset)
{
    typedef int (*Fn)(void *);
    static Fn fn;
    if (fn == NULL) fn = (Fn)bridge_symbol("wl_AAsset_isAllocated");
    return fn != NULL ? fn(asset) : 0;
}

ALooper *ALooper_prepare(int opts)
{
    if (g_thread_looper == NULL) {
        g_thread_looper = calloc(1, sizeof(*g_thread_looper));
        if (g_thread_looper == NULL) {
            return NULL;
        }
        g_thread_looper->references = 1;
        g_thread_looper->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        for (int i = 0; i < WL_ALOOPER_MAX_FDS; ++i) {
            g_thread_looper->fds[i].fd = -1;
        }
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
        typedef int (*Fn)(void *, int, int, int, ALooper_callbackFunc, void *);
        Fn fn = (Fn)bridge_symbol("wl_ALooper_addFd");
        if (fn != NULL) {
            result = fn(looper->backend, fd, ident, events, callback, data);
        }
    } else if (looper != NULL && fd >= 0 && (ident >= 0 || callback != NULL)) {
        int slot = -1;
        for (int i = 0; i < WL_ALOOPER_MAX_FDS; ++i) {
            if (looper->fds[i].fd == fd) {
                slot = i;
                break;
            }
            if (slot < 0 && looper->fds[i].fd < 0) {
                slot = i;
            }
        }
        if (slot >= 0) {
            looper->fds[slot].fd = fd;
            looper->fds[slot].ident = ident;
            looper->fds[slot].events = events;
            looper->fds[slot].callback = callback;
            looper->fds[slot].data = data;
            result = 1;
        }
    }
    fprintf(stderr,
            "[WESTLAKE-WEBVIEW-NDK] ALooper_addFd looper=%p backend=%p fd=%d"
            " ident=%d events=%d callback=%p data=%p result=%d\n",
            (void *)looper, looper != NULL ? looper->backend : NULL, fd, ident, events,
            (void *)callback, data, result);
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
    } else if (looper != NULL && fd >= 0) {
        result = 0;
        for (int i = 0; i < WL_ALOOPER_MAX_FDS; ++i) {
            if (looper->fds[i].fd == fd) {
                looper->fds[i].fd = -1;
                result = 1;
                break;
            }
        }
    }
    fprintf(stderr,
            "[WESTLAKE-WEBVIEW-NDK] ALooper_removeFd looper=%p backend=%p fd=%d result=%d\n",
            (void *)looper, looper != NULL ? looper->backend : NULL, fd, result);
    fflush(stderr);
    return result;
}

void ALooper_wake(ALooper *looper)
{
    if (looper == NULL) {
        return;
    }
    if (looper->backend != NULL) {
        typedef void (*Fn)(void *);
        Fn fn = (Fn)bridge_symbol("wl_ALooper_wake");
        if (fn != NULL) {
            fn(looper->backend);
        }
    }
    if (looper->wake_fd >= 0) {
        uint64_t value = 1;
        (void)write(looper->wake_fd, &value, sizeof(value));
    }
}

static short wl_to_poll_events(int events)
{
    short result = 0;
    if ((events & 1) != 0) result |= POLLIN;
    if ((events & 2) != 0) result |= POLLOUT;
    return result;
}

static int wl_from_poll_events(short events)
{
    int result = 0;
    if ((events & POLLIN) != 0) result |= 1;
    if ((events & POLLOUT) != 0) result |= 2;
    if ((events & POLLERR) != 0) result |= 4;
    if ((events & POLLHUP) != 0) result |= 8;
    if ((events & POLLNVAL) != 0) result |= 16;
    return result;
}

int ALooper_pollOnce(int timeout_millis, int *out_fd, int *out_events,
                     void **out_data)
{
    ALooper *looper = g_thread_looper;
    if (looper == NULL) {
        return -4; /* ALOOPER_POLL_ERROR */
    }

    struct pollfd poll_fds[WL_ALOOPER_MAX_FDS + 1];
    int slots[WL_ALOOPER_MAX_FDS + 1];
    int count = 0;
    if (looper->wake_fd >= 0) {
        poll_fds[count] = (struct pollfd){looper->wake_fd, POLLIN, 0};
        slots[count++] = -1;
    }
    for (int i = 0; i < WL_ALOOPER_MAX_FDS; ++i) {
        if (looper->fds[i].fd >= 0) {
            poll_fds[count] = (struct pollfd){
                looper->fds[i].fd, wl_to_poll_events(looper->fds[i].events), 0};
            slots[count++] = i;
        }
    }

    int rc;
    do {
        rc = poll(poll_fds, (nfds_t)count, timeout_millis);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) return -4;  /* ALOOPER_POLL_ERROR */
    if (rc == 0) return -3; /* ALOOPER_POLL_TIMEOUT */

    for (int i = 0; i < count; ++i) {
        if (poll_fds[i].revents == 0) continue;
        if (slots[i] < 0) {
            uint64_t value;
            (void)read(looper->wake_fd, &value, sizeof(value));
            return -1; /* ALOOPER_POLL_WAKE */
        }
        ALooperFd *registration = &looper->fds[slots[i]];
        const int events = wl_from_poll_events(poll_fds[i].revents);
        if (registration->callback != NULL) {
            const int keep = registration->callback(
                    registration->fd, events, registration->data);
            if (keep == 0) registration->fd = -1;
            return -2; /* ALOOPER_POLL_CALLBACK */
        }
        if (out_fd != NULL) *out_fd = registration->fd;
        if (out_events != NULL) *out_events = events;
        if (out_data != NULL) *out_data = registration->data;
        return registration->ident;
    }
    return -4;
}

/* Android NDK sensor boundary for sensorless OH boards; see the core shim. */
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
    /*
     * Surface.mNativeObject is an AdapterAnw at this boundary, not always a
     * raw OHNativeWindow.  Calling NativeObjectReference on the wrapper makes
     * OH reject it and turns a valid window into NULL, leaving HWUI with an
     * empty BufferQueue.  The framework bridge owns that representation and
     * already implements the wrapper-aware conversion/reference contract.
     */
    typedef void *(*Fn)(void *, void *);
    Fn fn = (Fn)bridge_symbol("ANativeWindow_fromSurface");
    void *window = fn != NULL ? fn(env, surface) : NULL;
    if (wl_trace_native_window()) {
        fprintf(stderr,
                "[WESTLAKE-NATIVEWINDOW] fromSurface env=%p surface=%p window=%p\n",
                env, surface, window);
        fflush(stderr);
    }
    return window;
}

void ANativeWindow_acquire(void *window)
{
    typedef void (*Fn)(void *);
    Fn fn = (Fn)bridge_symbol("ANativeWindow_acquire");
    if (fn != NULL) fn(window);
    if (wl_trace_native_window()) {
        fprintf(stderr, "[WESTLAKE-NATIVEWINDOW] acquire window=%p delegated=%d\n",
                window, fn != NULL);
        fflush(stderr);
    }
}

void ANativeWindow_release(void *window)
{
    typedef void (*Fn)(void *);
    Fn fn = (Fn)bridge_symbol("ANativeWindow_release");
    if (fn != NULL) fn(window);
    if (wl_trace_native_window()) {
        fprintf(stderr, "[WESTLAKE-NATIVEWINDOW] release window=%p delegated=%d\n",
                window, fn != NULL);
        fflush(stderr);
    }
}

int32_t ANativeWindow_getFormat(void *window)
{
    int32_t format = 0;
    if (window == NULL || NativeWindowHandleOpt(
            (WlOHNativeWindow *)window, WL_OH_GET_FORMAT, &format) != 0) {
        return 0;
    }
    return wl_oh_to_android_format(format);
}

int32_t ANativeWindow_getWidth(void *window)
{
    int32_t width = 0;
    int32_t height = 0;
    if (window == NULL || NativeWindowHandleOpt(
            (WlOHNativeWindow *)window, WL_OH_GET_BUFFER_GEOMETRY,
            &height, &width) != 0) {
        return 0;
    }
    return width;
}

int32_t ANativeWindow_getHeight(void *window)
{
    int32_t width = 0;
    int32_t height = 0;
    if (window == NULL || NativeWindowHandleOpt(
            (WlOHNativeWindow *)window, WL_OH_GET_BUFFER_GEOMETRY,
            &height, &width) != 0) {
        return 0;
    }
    return height;
}

int32_t ANativeWindow_setBuffersGeometry(void *window, int32_t width,
                                         int32_t height, int32_t format)
{
    int32_t result = -1;
    if (window != NULL) {
        wl_remember_requested_format(window, format);
        result = NativeWindowHandleOpt((WlOHNativeWindow *)window,
                                       WL_OH_SET_BUFFER_GEOMETRY,
                                       width, height);
        if (result == 0 && format > 0) {
            result = NativeWindowHandleOpt((WlOHNativeWindow *)window,
                                           WL_OH_SET_FORMAT,
                                           wl_android_to_oh_format(format));
        }
    }
    if (wl_trace_native_window()) {
        fprintf(stderr,
                "[WESTLAKE-NATIVEWINDOW] setGeometry window=%p size=%dx%d format=%d result=%d\n",
                window, width, height, format, result);
        fflush(stderr);
    }
    return result;
}

int32_t ANativeWindow_lock(void *window, void *out_buffer, void *dirty_bounds)
{
    static unsigned int calls;
    WlOHNativeWindowBuffer *oh_buffer = NULL;
    int32_t result = -1;

    free(g_locked_pixels.scratch_bits);
    memset(&g_locked_pixels, 0, sizeof(g_locked_pixels));
    if (window != NULL && out_buffer != NULL) {
        /*
         * Do not use NativeWindowLockBuffer here.  On the deployed OH 6.1
         * producer behind SurfaceTexture it returns SURFACE_ERROR_ERROR for
         * every decoded frame, even though ordinary RequestBuffer succeeds.
         * Android's NDK lock contract maps cleanly onto the lower-level OH
         * request/map/flush sequence and keeps the compatibility boundary
         * independent of that optional paired helper.
         */
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
        result = NativeWindowRequestBuffer((WlOHNativeWindow *)window,
                                           &oh_buffer, &fence_fd);
        if (result == 0 && oh_buffer != NULL) {
            WlOHNativeBuffer *native_buffer =
                    OH_NativeBufferFromNativeWindowBuffer(oh_buffer);
            WlOHBufferHandle *handle = GetBufferHandleFromNative(oh_buffer);
            void *mapped_addr = NULL;
            if (native_buffer != NULL) {
                result = OH_NativeBuffer_MapWaitFence(native_buffer, fence_fd,
                                                       &mapped_addr);
                if (result != 0) {
                    result = OH_NativeBuffer_Map(native_buffer, &mapped_addr);
                }
            } else {
                result = -1;
            }
            if (fence_fd >= 0) {
                close(fence_fd);
                fence_fd = -1;
            }
            if (result == 0 && mapped_addr == NULL && handle != NULL) {
                mapped_addr = handle->vir_addr;
            }
            if (handle == NULL || mapped_addr == NULL) {
                result = -1;
            }
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
                    const size_t y_stride = wl_align_size(
                            (size_t)handle->width, 16);
                    const size_t chroma_stride = wl_align_size(y_stride / 2, 16);
                    const size_t chroma_height =
                            ((size_t)handle->height + 1) / 2;
                    android_bits_size = y_stride * (size_t)handle->height +
                            2 * chroma_stride * chroma_height;
                    android_bits = calloc(1, android_bits_size);
                    stride = (int32_t)y_stride;
                    if (android_bits == NULL) result = -1;
                } else {
                    const int32_t bytes_per_pixel =
                            wl_android_bytes_per_pixel(android_format);
                    stride = handle->stride;
                    if (bytes_per_pixel > 1 &&
                            stride >= handle->width * bytes_per_pixel &&
                            stride % bytes_per_pixel == 0) {
                        stride /= bytes_per_pixel;
                    }
                    android_bits_size = (size_t)stride *
                            (size_t)handle->height * (size_t)bytes_per_pixel;
                }
                if (result != 0) {
                    free(android_format == WL_ANDROID_FORMAT_YV12
                            ? android_bits : NULL);
                    (void)NativeWindowCancelBuffer((WlOHNativeWindow *)window,
                                                   oh_buffer);
                    oh_buffer = NULL;
                } else {
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
                        g_locked_pixels.dirty_w =
                                android_rect->right > android_rect->left
                                ? (uint32_t)(android_rect->right - android_rect->left) : 0;
                        g_locked_pixels.dirty_h =
                                android_rect->bottom > android_rect->top
                                ? (uint32_t)(android_rect->bottom - android_rect->top) : 0;
                        g_locked_pixels.has_dirty = 1;
                    }
                    g_locked_pixels.valid = 1;
                }
            } else {
                (void)NativeWindowCancelBuffer((WlOHNativeWindow *)window,
                                               oh_buffer);
            }
        }
        if (fence_fd >= 0) {
            close(fence_fd);
        }
    }
    if (wl_trace_native_window() || result != 0) {
        const unsigned int count = wl_native_window_count(&calls);
        if (count <= 12 || count % 60 == 0 || result != 0) {
            const WlANativeWindowBuffer *buffer =
                    (const WlANativeWindowBuffer *)out_buffer;
            fprintf(stderr,
                    "[WESTLAKE-NATIVEWINDOW] lock #%u window=%p result=%d"
                    " size=%dx%d stride=%d format=%d bits=%p dirty=%p\n",
                    count, window, result,
                    result == 0 && buffer != NULL ? buffer->width : 0,
                    result == 0 && buffer != NULL ? buffer->height : 0,
                    result == 0 && buffer != NULL ? buffer->stride : 0,
                    result == 0 && buffer != NULL ? buffer->format : 0,
                    result == 0 && buffer != NULL ? buffer->bits : NULL,
                    dirty_bounds);
            fflush(stderr);
        }
    }
    return result;
}

int32_t ANativeWindow_unlockAndPost(void *window)
{
    static unsigned int calls;
    const unsigned int count = wl_native_window_count(&calls);
    uint64_t sample_hash = UINT64_C(1469598103934665603);
    uint32_t sample_nonzero = 0;
    uint32_t sample_count = 0;
    uint32_t first_word = 0;
    uint32_t middle_word = 0;
    uint32_t last_word = 0;
    if (g_locked_pixels.valid && g_locked_pixels.window == window &&
            g_locked_pixels.bits != NULL) {
        const size_t byte_count = g_locked_pixels.bits_size;
        const uint8_t *bytes = (const uint8_t *)g_locked_pixels.bits;
        if (byte_count >= sizeof(uint32_t)) {
            memcpy(&first_word, bytes, sizeof(first_word));
            memcpy(&middle_word, bytes + byte_count / 2, sizeof(middle_word));
            memcpy(&last_word, bytes + byte_count - sizeof(last_word),
                   sizeof(last_word));
            const size_t wanted = 2048;
            const size_t step = byte_count > wanted ? byte_count / wanted : 1;
            for (size_t offset = 0; offset < byte_count; offset += step) {
                const uint8_t value = bytes[offset];
                sample_hash ^= value;
                sample_hash *= UINT64_C(1099511628211);
                sample_nonzero += value != 0;
                ++sample_count;
            }
        }
    }
    int32_t result = -1;
    if (window != NULL && g_locked_pixels.valid &&
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
        result = NativeWindowFlushBuffer((WlOHNativeWindow *)window,
                                         (WlOHNativeWindowBuffer *)
                                                 g_locked_pixels.oh_buffer,
                                         -1, region);
    }
    if (wl_trace_native_window() || result != 0) {
        if (count <= 12 || count % 60 == 0 || result != 0) {
            fprintf(stderr,
                    "[WESTLAKE-NATIVEWINDOW] unlockAndPost #%u window=%p result=%d"
                    " locked=%d lockedWindow=%p ohBuffer=%p"
                    " samples=%u nonzero=%u hash=%016llx"
                    " words=%08x/%08x/%08x\n",
                    count, window, result, g_locked_pixels.valid,
                    g_locked_pixels.window, g_locked_pixels.oh_buffer,
                    sample_count, sample_nonzero,
                    (unsigned long long)sample_hash,
                    first_word, middle_word, last_word);
            fflush(stderr);
        }
    }
    free(g_locked_pixels.scratch_bits);
    memset(&g_locked_pixels, 0, sizeof(g_locked_pixels));
    return result;
}

/*
 * HWUI's ASurfaceControlFunctions intentionally dlopen("libandroid.so") and
 * dlsym every NDK SurfaceControl entry point from that specific handle.  On
 * Android these symbols live in libandroid.so.  Westlake's implementations
 * live in liboh_adapter_bridge.so because they terminate at OH RenderService,
 * so expose the Android ABI here and forward each call to that boundary.
 */
typedef void (*WlSurfaceTransactionCallback)(void *context, void *stats);

typedef struct WlSurfaceTransactionState {
    void *transaction;
    void *complete_context;
    WlSurfaceTransactionCallback complete_callback;
    struct WlSurfaceTransactionState *next;
} WlSurfaceTransactionState;

static pthread_mutex_t g_surface_transaction_mutex = PTHREAD_MUTEX_INITIALIZER;
static WlSurfaceTransactionState *g_surface_transactions;

static WlSurfaceTransactionState *wl_surface_transaction_find_locked(
        void *transaction)
{
    WlSurfaceTransactionState *state = g_surface_transactions;
    while (state != NULL && state->transaction != transaction) {
        state = state->next;
    }
    return state;
}

static void wl_surface_transaction_track(void *transaction)
{
    if (transaction == NULL) return;
    WlSurfaceTransactionState *state =
            (WlSurfaceTransactionState *)calloc(1, sizeof(*state));
    if (state == NULL) return;
    state->transaction = transaction;
    pthread_mutex_lock(&g_surface_transaction_mutex);
    state->next = g_surface_transactions;
    g_surface_transactions = state;
    pthread_mutex_unlock(&g_surface_transaction_mutex);
}

static void wl_surface_transaction_untrack(void *transaction)
{
    pthread_mutex_lock(&g_surface_transaction_mutex);
    WlSurfaceTransactionState **link = &g_surface_transactions;
    while (*link != NULL && (*link)->transaction != transaction) {
        link = &(*link)->next;
    }
    WlSurfaceTransactionState *state = *link;
    if (state != NULL) *link = state->next;
    pthread_mutex_unlock(&g_surface_transaction_mutex);
    free(state);
}

void *ASurfaceControl_create(void *parent, const char *debug_name)
{
    typedef void *(*Fn)(void *, const char *);
    Fn fn = (Fn)bridge_symbol("ASurfaceControl_create");
    return fn != NULL ? fn(parent, debug_name) : NULL;
}

void *ASurfaceControl_createFromWindow(void *parent, const char *debug_name)
{
    /* The OH bridge creates an RSSurfaceNode child and treats parent as opaque. */
    return ASurfaceControl_create(parent, debug_name);
}

void ASurfaceControl_acquire(void *surface_control)
{
    typedef void (*Fn)(void *);
    Fn fn = (Fn)bridge_symbol("ASurfaceControl_acquire");
    if (fn != NULL) {
        fn(surface_control);
    }
}

void ASurfaceControl_release(void *surface_control)
{
    typedef void (*Fn)(void *);
    Fn fn = (Fn)bridge_symbol("ASurfaceControl_release");
    if (fn != NULL) {
        fn(surface_control);
    }
}

void ASurfaceControl_registerSurfaceStatsListener(void *surface_control,
                                                  int32_t id,
                                                  void *context,
                                                  void *listener)
{
    typedef void (*Fn)(void *, int32_t, void *, void *);
    Fn fn = (Fn)bridge_symbol("ASurfaceControl_registerSurfaceStatsListener");
    if (fn != NULL) {
        fn(surface_control, id, context, listener);
    }
}

void ASurfaceControl_unregisterSurfaceStatsListener(void *context, void *listener)
{
    typedef void (*Fn)(void *, void *);
    Fn fn = (Fn)bridge_symbol("ASurfaceControl_unregisterSurfaceStatsListener");
    if (fn != NULL) {
        fn(context, listener);
    }
}

int64_t ASurfaceControlStats_getAcquireTime(void *stats)
{
    typedef int64_t (*Fn)(void *);
    Fn fn = (Fn)bridge_symbol("ASurfaceControlStats_getAcquireTime");
    return fn != NULL ? fn(stats) : 0;
}

uint64_t ASurfaceControlStats_getFrameNumber(void *stats)
{
    typedef uint64_t (*Fn)(void *);
    Fn fn = (Fn)bridge_symbol("ASurfaceControlStats_getFrameNumber");
    return fn != NULL ? fn(stats) : 0;
}

void *ASurfaceTransaction_create(void)
{
    typedef void *(*Fn)(void);
    Fn fn = (Fn)bridge_symbol("ASurfaceTransaction_create");
    void *transaction = fn != NULL ? fn() : NULL;
    wl_surface_transaction_track(transaction);
    return transaction;
}

void ASurfaceTransaction_delete(void *transaction)
{
    typedef void (*Fn)(void *);
    Fn fn = (Fn)bridge_symbol("ASurfaceTransaction_delete");
    wl_surface_transaction_untrack(transaction);
    if (fn != NULL) {
        fn(transaction);
    }
}

int64_t ASurfaceTransaction_apply(void *transaction)
{
    typedef void (*Fn)(void *);
    Fn fn = (Fn)bridge_symbol("ASurfaceTransaction_apply");
    if (fn != NULL) {
        fn(transaction);
    }

    /*
     * OH uses an implicit transaction which the framework call above flushes,
     * but it exposes no Android ASurfaceTransactionStats object. Chromium
     * explicitly accepts a null stats acknowledgement. Dispatching it here
     * preserves Android's completion contract instead of leaving compositor
     * work waiting forever.
     */
    void *context = NULL;
    WlSurfaceTransactionCallback callback = NULL;
    pthread_mutex_lock(&g_surface_transaction_mutex);
    WlSurfaceTransactionState *state =
            wl_surface_transaction_find_locked(transaction);
    if (state != NULL) {
        context = state->complete_context;
        callback = state->complete_callback;
        state->complete_context = NULL;
        state->complete_callback = NULL;
    }
    pthread_mutex_unlock(&g_surface_transaction_mutex);
    if (callback != NULL) callback(context, NULL);
    return 0;
}

void ASurfaceTransaction_setOnComplete(
        void *transaction, void *context,
        WlSurfaceTransactionCallback callback)
{
    pthread_mutex_lock(&g_surface_transaction_mutex);
    WlSurfaceTransactionState *state =
            wl_surface_transaction_find_locked(transaction);
    if (state != NULL) {
        state->complete_context = context;
        state->complete_callback = callback;
    }
    pthread_mutex_unlock(&g_surface_transaction_mutex);
}

void ASurfaceTransaction_reparent(void *transaction,
                                  void *surface_control,
                                  void *new_parent)
{
    typedef void (*Fn)(void *, void *, void *);
    Fn fn = (Fn)bridge_symbol("ASurfaceTransaction_reparent");
    if (fn != NULL) {
        fn(transaction, surface_control, new_parent);
    }
}

void ASurfaceTransaction_setVisibility(void *transaction,
                                       void *surface_control,
                                       int8_t visibility)
{
    typedef void (*Fn)(void *, void *, int8_t);
    Fn fn = (Fn)bridge_symbol("ASurfaceTransaction_setVisibility");
    if (fn != NULL) {
        fn(transaction, surface_control, visibility);
    }
}

void ASurfaceTransaction_setZOrder(void *transaction,
                                   void *surface_control,
                                   int32_t z_order)
{
    typedef void (*Fn)(void *, void *, int32_t);
    Fn fn = (Fn)bridge_symbol("ASurfaceTransaction_setZOrder");
    if (fn != NULL) {
        fn(transaction, surface_control, z_order);
    }
}

/*
 * Chromium requires this complete Android Q SurfaceControl symbol table before
 * enabling the feature. OH's normal WebView path renders into the parent
 * NativeWindow; these remaining entries describe optional overlay metadata.
 * Preserve ownership and fence contracts while treating metadata with no OH
 * public equivalent as a no-op. A partial table is not ABI-compatible and
 * Chromium deliberately terminates when Android API 29+ advertises one.
 */
void ASurfaceTransaction_setBuffer(void *transaction, void *surface_control,
                                   void *hardware_buffer, int32_t fence_fd)
{
    (void)transaction;
    (void)surface_control;
    (void)hardware_buffer;
    if (fence_fd >= 0) close(fence_fd);
}

void ASurfaceTransaction_setGeometry(void *transaction, void *surface_control,
                                     const void *source_rect,
                                     const void *destination_rect,
                                     int32_t transform)
{
    (void)transaction;
    (void)surface_control;
    (void)source_rect;
    (void)destination_rect;
    (void)transform;
}

void ASurfaceTransaction_setBufferTransparency(
        void *transaction, void *surface_control, int8_t transparency)
{
    (void)transaction;
    (void)surface_control;
    (void)transparency;
}

void ASurfaceTransaction_setDamageRegion(void *transaction,
                                         void *surface_control,
                                         const void *rectangles,
                                         uint32_t rectangle_count)
{
    (void)transaction;
    (void)surface_control;
    (void)rectangles;
    (void)rectangle_count;
}

void ASurfaceTransaction_setBufferDataSpace(void *transaction,
                                            void *surface_control,
                                            uint64_t data_space)
{
    (void)transaction;
    (void)surface_control;
    (void)data_space;
}

void ASurfaceTransaction_setHdrMetadata_cta861_3(
        void *transaction, void *surface_control, const void *metadata)
{
    (void)transaction;
    (void)surface_control;
    (void)metadata;
}

void ASurfaceTransaction_setHdrMetadata_smpte2086(
        void *transaction, void *surface_control, const void *metadata)
{
    (void)transaction;
    (void)surface_control;
    (void)metadata;
}

int ASurfaceTransactionStats_getPresentFenceFd(void *stats)
{
    (void)stats;
    return -1;
}

int64_t ASurfaceTransactionStats_getLatchTime(void *stats)
{
    (void)stats;
    return 0;
}

void ASurfaceTransactionStats_getASurfaceControls(void *stats,
                                                  void ***surface_controls,
                                                  size_t *size)
{
    (void)stats;
    if (surface_controls != NULL) *surface_controls = NULL;
    if (size != NULL) *size = 0;
}

void ASurfaceTransactionStats_releaseASurfaceControls(void **surface_controls)
{
    (void)surface_controls;
}

int ASurfaceTransactionStats_getPreviousReleaseFenceFd(
        void *stats, void *surface_control)
{
    (void)stats;
    (void)surface_control;
    return -1;
}
