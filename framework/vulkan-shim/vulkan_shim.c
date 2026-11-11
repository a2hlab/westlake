/*
 * libvulkan.so for Android apps on OpenHarmony: OH's Vulkan loader plus VK_KHR_android_surface.
 *
 * An Android app creates its window surface with vkCreateAndroidSurfaceKHR. OH's loader has no
 * such extension; its surface extension is VK_OHOS_surface (vkCreateSurfaceOHOS on an
 * OHNativeWindow). Flutter's Impeller creates its instance without checking for the extension
 * and calls the function it looked up, so every Flutter app died on a null call on its raster
 * thread before its first frame.
 *
 * This library sits first on the app's library path under the loader's name. Every public
 * vk* entry point forwards to OH's loader (vulkan_trampolines.S, generated from its export
 * list). Four are implemented here:
 *   vkEnumerateInstanceExtensionProperties  also lists VK_KHR_android_surface when OH has VK_OHOS_surface
 *   vkCreateInstance                        enables VK_OHOS_surface in place of VK_KHR_android_surface
 *   vkGetInstanceProcAddr                   returns these, and vkCreateAndroidSurfaceKHR
 *   vkGetDeviceProcAddr                     OH's, logging the names it has no function for
 * vkCreateAndroidSurfaceKHR takes the OHNativeWindow behind the app's ANativeWindow (Westlake's
 * wrapper, or the OH window itself) and calls vkCreateSurfaceOHOS.
 */
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_ohos.h>

#define REAL_LOADER "/system/lib64/platformsdk/libvulkan.so"
#define ANDROID_SURFACE "VK_KHR_android_surface"
#define OHOS_SURFACE "VK_OHOS_surface"
#define STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO 1000008000

typedef struct {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    void* window; /* ANativeWindow* */
} AndroidSurfaceCreateInfo;

/* Filled by vulkan_trampolines.S's table, in export-list order. */
extern void* wl_vk_table[] __attribute__((visibility("hidden")));
extern const char* const wl_vk_names[] __attribute__((visibility("hidden")));
extern const unsigned wl_vk_count __attribute__((visibility("hidden")));

static void* real;
static PFN_vkGetInstanceProcAddr real_gipa;
static PFN_vkCreateInstance real_create_instance;
static PFN_vkEnumerateInstanceExtensionProperties real_enumerate;
static PFN_vkGetDeviceProcAddr real_gdpa;
static PFN_vkDestroyInstance real_destroy_instance;
static PFN_vkCreateDevice real_create_device;
static PFN_vkDestroyDevice real_destroy_device;

/*
 * DIAGNOSTIC, not for merging. Flutter 3.27/3.29 creates a throwaway Vulkan context to test for
 * support, then its real one; on this board Mali closes its /dev/mali0 descriptor inside the second
 * vkCreateInstance and its "mali-cmar-backe" thread aborts (base_jd_event_term).
 *   WESTLAKE_VK_TRACE=1     log instance and device creation and destruction: thread, time, and the
 *                           process's /dev/mali descriptors and mali threads before and after
 *   WESTLAKE_VK_KEEP=1      vkDestroyDevice and vkDestroyInstance return without destroying
 *   WESTLAKE_VK_SETTLE_MS=N vkDestroyDevice and vkDestroyInstance wait N ms after destroying
 *   WESTLAKE_VK_MIN_AGE_MS=N vkDestroyDevice waits until the device is N ms old before destroying it
 */
static int wl_vk_trace, wl_vk_keep, wl_vk_settle_ms, wl_vk_min_age_ms;

/* Creation times of the devices alive, for WESTLAKE_VK_MIN_AGE_MS. */
#define WL_VK_DEVICES 16
static struct { const void* device; long created; } wl_vk_devices[WL_VK_DEVICES];
static pthread_mutex_t wl_vk_devices_lock = PTHREAD_MUTEX_INITIALIZER;

static long wl_vk_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void wl_vk_gpu_state(char* buf, size_t size) {
    size_t used = (size_t)snprintf(buf, size, "fds=");
    DIR* d = opendir("/proc/self/fd");
    struct dirent* e;
    while (d && (e = readdir(d)) != NULL && used + 16 < size) {
        char link[64], target[64];
        snprintf(link, sizeof link, "/proc/self/fd/%s", e->d_name);
        ssize_t n = readlink(link, target, sizeof target - 1);
        if (n > 0) {
            target[n] = 0;
            if (!strncmp(target, "/dev/mali", 9)) used += (size_t)snprintf(buf + used, size - used, "%s,", e->d_name);
        }
    }
    if (d) closedir(d);
    if (used + 16 < size) used += (size_t)snprintf(buf + used, size - used, " threads=");
    d = opendir("/proc/self/task");
    while (d && (e = readdir(d)) != NULL && used + 40 < size) {
        if (e->d_name[0] == '.') continue;
        char path[64], comm[32] = {0};
        snprintf(path, sizeof path, "/proc/self/task/%s/comm", e->d_name);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        ssize_t n = read(fd, comm, sizeof comm - 1);
        close(fd);
        if (n > 0 && comm[n - 1] == '\n') comm[n - 1] = 0;
        if (!strncmp(comm, "mali", 4)) used += (size_t)snprintf(buf + used, size - used, "%s:%s,", e->d_name, comm);
    }
    if (d) closedir(d);
}

static void wl_vk_log(const char* what, const void* handle, const char* phase, int result) {
    if (!wl_vk_trace) return;
    char state[768];
    wl_vk_gpu_state(state, sizeof state);
    fprintf(stderr, "[WL-VULKAN] %s %s %p tid=%ld t=%ld result=%d %s\n", what, phase, handle,
            (long)syscall(SYS_gettid), wl_vk_ms(), result, state);
}

static VKAPI_ATTR void VKAPI_CALL wl_vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* alloc) {
    wl_vk_log("vkDestroyInstance", instance, "enter", 0);
    if (!wl_vk_keep && real_destroy_instance) real_destroy_instance(instance, alloc);
    if (wl_vk_settle_ms > 0) usleep((useconds_t)wl_vk_settle_ms * 1000);
    wl_vk_log("vkDestroyInstance", instance, wl_vk_keep ? "kept" : "done", 0);
}

static VKAPI_ATTR VkResult VKAPI_CALL wl_vkCreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info,
                                                        const VkAllocationCallbacks* alloc, VkDevice* device) {
    wl_vk_log("vkCreateDevice", physical, "enter", 0);
    VkResult result = real_create_device ? real_create_device(physical, info, alloc, device)
                                         : VK_ERROR_INITIALIZATION_FAILED;
    wl_vk_log("vkCreateDevice", result == VK_SUCCESS ? (const void*)*device : NULL, "done", result);
    if (result == VK_SUCCESS) {
        pthread_mutex_lock(&wl_vk_devices_lock);
        for (int i = 0; i < WL_VK_DEVICES; i++) {
            if (wl_vk_devices[i].device == NULL) {
                wl_vk_devices[i].device = *device;
                wl_vk_devices[i].created = wl_vk_ms();
                break;
            }
        }
        pthread_mutex_unlock(&wl_vk_devices_lock);
    }
    return result;
}

static VKAPI_ATTR void VKAPI_CALL wl_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
    wl_vk_log("vkDestroyDevice", device, "enter", 0);
    long created = 0;
    pthread_mutex_lock(&wl_vk_devices_lock);
    for (int i = 0; i < WL_VK_DEVICES; i++) {
        if (wl_vk_devices[i].device == (const void*)device) {
            created = wl_vk_devices[i].created;
            wl_vk_devices[i].device = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&wl_vk_devices_lock);
    if (wl_vk_min_age_ms > 0 && created > 0) {
        long wait = created + wl_vk_min_age_ms - wl_vk_ms();
        if (wait > 0) {
            usleep((useconds_t)wait * 1000);
            wl_vk_log("vkDestroyDevice", device, "aged", (int)wait);
        }
    }
    if (!wl_vk_keep && real_destroy_device) real_destroy_device(device, alloc);
    if (wl_vk_settle_ms > 0) usleep((useconds_t)wl_vk_settle_ms * 1000);
    wl_vk_log("vkDestroyDevice", device, wl_vk_keep ? "kept" : "done", 0);
}

static int wl_vk_wrapping(void) { return wl_vk_trace || wl_vk_keep || wl_vk_settle_ms > 0 || wl_vk_min_age_ms > 0; }

__attribute__((constructor)) static void wl_vk_init(void) {
    real = dlopen(REAL_LOADER, RTLD_NOW | RTLD_LOCAL);
    if (!real) {
        fprintf(stderr, "[WL-VULKAN] cannot load %s: %s\n", REAL_LOADER, dlerror());
        return;
    }
    for (unsigned i = 0; i < wl_vk_count; i++) wl_vk_table[i] = dlsym(real, wl_vk_names[i]);
    real_gipa = (PFN_vkGetInstanceProcAddr)dlsym(real, "vkGetInstanceProcAddr");
    real_create_instance = (PFN_vkCreateInstance)dlsym(real, "vkCreateInstance");
    real_enumerate = (PFN_vkEnumerateInstanceExtensionProperties)dlsym(real, "vkEnumerateInstanceExtensionProperties");
    real_gdpa = (PFN_vkGetDeviceProcAddr)dlsym(real, "vkGetDeviceProcAddr");
    real_destroy_instance = (PFN_vkDestroyInstance)dlsym(real, "vkDestroyInstance");
    real_create_device = (PFN_vkCreateDevice)dlsym(real, "vkCreateDevice");
    real_destroy_device = (PFN_vkDestroyDevice)dlsym(real, "vkDestroyDevice");
    const char* value = getenv("WESTLAKE_VK_TRACE");
    wl_vk_trace = value && value[0] == '1';
    value = getenv("WESTLAKE_VK_KEEP");
    wl_vk_keep = value && value[0] == '1';
    value = getenv("WESTLAKE_VK_SETTLE_MS");
    wl_vk_settle_ms = value ? atoi(value) : 0;
    value = getenv("WESTLAKE_VK_MIN_AGE_MS");
    wl_vk_min_age_ms = value ? atoi(value) : 0;
    if (wl_vk_wrapping()) {
        fprintf(stderr, "[WL-VULKAN] diagnostic: trace=%d keep=%d settle=%d ms min-age=%d ms\n", wl_vk_trace, wl_vk_keep,
                wl_vk_settle_ms, wl_vk_min_age_ms);
    }
}

/*
 * A name OH's loader has no function for. The app gets null, as from Android's loader for a name it
 * lacks, and some call it unchecked: NBA 2K's render thread called through a null pointer right after
 * the Mali driver logged a run of "vk_icdGetInstanceProcAddr can not find <private>", the names
 * hidden. Each such name is logged once, the first 64 in all.
 */
#define WL_VK_MISSING_MAX 64
static char wl_vk_missing_names[WL_VK_MISSING_MAX][64];
static unsigned wl_vk_missing_count;
static pthread_mutex_t wl_vk_missing_lock = PTHREAD_MUTEX_INITIALIZER;

static void wl_vk_missing(const char* lookup, const char* name) {
    pthread_mutex_lock(&wl_vk_missing_lock);
    int seen = 0;
    for (unsigned i = 0; i < wl_vk_missing_count && !seen; i++) {
        seen = !strncmp(wl_vk_missing_names[i], name, sizeof wl_vk_missing_names[i] - 1);
    }
    if (!seen && wl_vk_missing_count < WL_VK_MISSING_MAX) {
        snprintf(wl_vk_missing_names[wl_vk_missing_count++], sizeof wl_vk_missing_names[0], "%s", name);
        fprintf(stderr, "[WL-VULKAN] %s(%s): no function\n", lookup, name);
    }
    pthread_mutex_unlock(&wl_vk_missing_lock);
}

static int real_has_ohos_surface(void) {
    uint32_t count = 0;
    if (!real_enumerate || real_enumerate(NULL, &count, NULL) != VK_SUCCESS || !count) return 0;
    VkExtensionProperties* props = calloc(count, sizeof *props);
    int found = 0;
    if (props && real_enumerate(NULL, &count, props) >= 0) {
        for (uint32_t i = 0; i < count; i++) found |= !strcmp(props[i].extensionName, OHOS_SURFACE);
    }
    free(props);
    return found;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
        const char* layer, uint32_t* count, VkExtensionProperties* props) {
    if (!real_enumerate) return VK_ERROR_INITIALIZATION_FAILED;
    if (layer || !real_has_ohos_surface()) return real_enumerate(layer, count, props);
    uint32_t total = 0;
    VkResult result = real_enumerate(NULL, &total, NULL);
    if (result != VK_SUCCESS) return result;
    if (!props) {
        *count = total + 1;
        return VK_SUCCESS;
    }
    uint32_t wanted = *count;
    uint32_t got = wanted < total ? wanted : total;
    result = real_enumerate(NULL, &got, props);
    if (result < 0) return result;
    if (wanted <= total) {
        *count = got;
        return VK_INCOMPLETE;
    }
    memset(&props[got], 0, sizeof props[got]);
    strcpy(props[got].extensionName, ANDROID_SURFACE);
    props[got].specVersion = 6;
    *count = got + 1;
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
        const VkInstanceCreateInfo* info, const VkAllocationCallbacks* alloc, VkInstance* instance) {
    if (!real_create_instance) return VK_ERROR_INITIALIZATION_FAILED;
    int android = 0, ohos = 0;
    for (uint32_t i = 0; i < info->enabledExtensionCount; i++) {
        android |= !strcmp(info->ppEnabledExtensionNames[i], ANDROID_SURFACE);
        ohos |= !strcmp(info->ppEnabledExtensionNames[i], OHOS_SURFACE);
    }
    if (wl_vk_trace) {
        fprintf(stderr, "[WL-VULKAN] vkCreateInstance api=0x%x extensions=%u layers=%u\n",
                info->pApplicationInfo ? info->pApplicationInfo->apiVersion : 0,
                info->enabledExtensionCount, info->enabledLayerCount);
    }
    wl_vk_log("vkCreateInstance", NULL, "enter", 0);
    if (!android) {
        VkResult plain = real_create_instance(info, alloc, instance);
        wl_vk_log("vkCreateInstance", plain == VK_SUCCESS ? (const void*)*instance : NULL, "done", plain);
        return plain;
    }
    const char** names = calloc(info->enabledExtensionCount, sizeof *names);
    if (!names) return VK_ERROR_OUT_OF_HOST_MEMORY;
    uint32_t n = 0;
    for (uint32_t i = 0; i < info->enabledExtensionCount; i++) {
        const char* name = info->ppEnabledExtensionNames[i];
        if (!strcmp(name, ANDROID_SURFACE)) {
            if (ohos) continue;
            name = OHOS_SURFACE;
        }
        names[n++] = name;
    }
    VkInstanceCreateInfo copy = *info;
    copy.enabledExtensionCount = n;
    copy.ppEnabledExtensionNames = names;
    VkResult result = real_create_instance(&copy, alloc, instance);
    free(names);
    wl_vk_log("vkCreateInstance", result == VK_SUCCESS ? (const void*)*instance : NULL, "done", result);
    return result;
}

static VKAPI_ATTR VkResult VKAPI_CALL wl_vkCreateAndroidSurfaceKHR(
        VkInstance instance, const AndroidSurfaceCreateInfo* info,
        const VkAllocationCallbacks* alloc, VkSurfaceKHR* surface) {
    if (!info || info->sType != STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO || !info->window) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    PFN_vkCreateSurfaceOHOS create = (PFN_vkCreateSurfaceOHOS)real_gipa(instance, "vkCreateSurfaceOHOS");
    if (!create) {
        fprintf(stderr, "[WL-VULKAN] instance has no vkCreateSurfaceOHOS\n");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    /* Westlake hands apps either its AOSP-ABI wrapper around an OH window or the OH window
     * itself; oh_anw_get_oh recognises the wrapper and returns null otherwise. */
    typedef OHNativeWindow* (*GetOh)(void*);
    GetOh get_oh = (GetOh)dlsym(RTLD_DEFAULT, "oh_anw_get_oh");
    OHNativeWindow* window = get_oh ? get_oh(info->window) : NULL;
    if (!window) window = (OHNativeWindow*)info->window;
    VkSurfaceCreateInfoOHOS ohos = {VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS, NULL, 0, window};
    VkResult result = create(instance, &ohos, alloc, surface);
    fprintf(stderr, "[WL-VULKAN] vkCreateAndroidSurfaceKHR window=%p oh=%p -> %d\n",
            info->window, (void*)window, result);
    return result;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!real_gipa || !name) return NULL;
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (!strcmp(name, "vkCreateInstance")) return (PFN_vkVoidFunction)vkCreateInstance;
    if (!strcmp(name, "vkEnumerateInstanceExtensionProperties")) {
        return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
    }
    if (!strcmp(name, "vkCreateAndroidSurfaceKHR")) {
        return instance ? (PFN_vkVoidFunction)wl_vkCreateAndroidSurfaceKHR : NULL;
    }
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (wl_vk_wrapping() && instance) {
        if (!strcmp(name, "vkDestroyInstance")) return (PFN_vkVoidFunction)wl_vkDestroyInstance;
        if (!strcmp(name, "vkCreateDevice")) return (PFN_vkVoidFunction)wl_vkCreateDevice;
        if (!strcmp(name, "vkDestroyDevice")) return (PFN_vkVoidFunction)wl_vkDestroyDevice;
    }
    PFN_vkVoidFunction function = real_gipa(instance, name);
    if (!function) wl_vk_missing("vkGetInstanceProcAddr", name);
    return function;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name) {
    if (!real_gdpa || !name) return NULL;
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (wl_vk_wrapping() && !strcmp(name, "vkDestroyDevice")) return (PFN_vkVoidFunction)wl_vkDestroyDevice;
    PFN_vkVoidFunction function = real_gdpa(device, name);
    if (!function) wl_vk_missing("vkGetDeviceProcAddr", name);
    return function;
}
