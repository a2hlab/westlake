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
 * list). Six are implemented here:
 *   vkEnumerateInstanceExtensionProperties  also lists VK_KHR_android_surface when OH has VK_OHOS_surface
 *   vkCreateInstance                        enables VK_OHOS_surface in place of VK_KHR_android_surface
 *   vkGetInstanceProcAddr                   returns these, and vkCreateAndroidSurfaceKHR
 *   vkGetDeviceProcAddr                     OH's, logging the names it has no function for
 *   vkDestroyDevice, vkDestroyInstance      keep the device (and its instance): destroying one aborts the
 *                                           board's Mali driver (see wl_vkDestroyDevice)
 * vkCreateAndroidSurfaceKHR takes the OHNativeWindow behind the app's ANativeWindow (Westlake's
 * wrapper, or the OH window itself) and calls vkCreateSurfaceOHOS.
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static PFN_vkDestroyDevice real_destroy_device;
static PFN_vkDestroyInstance real_destroy_instance;

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
    real_destroy_device = (PFN_vkDestroyDevice)dlsym(real, "vkDestroyDevice");
    real_destroy_instance = (PFN_vkDestroyInstance)dlsym(real, "vkDestroyInstance");
}

/*
 * Devices kept rather than destroyed. On this board's Mali driver, destroying a VkDevice aborts the
 * process: the driver's mali-cmar-backe thread, started by vkCreateDevice, fails on the context's
 * termination (mali_base_kbase.c:361, base_jd_event_term) inside vkDestroyDevice. Flutter 3.27 and
 * 3.29 create a throwaway Vulkan context to test for support and destroy its device ~10 ms later,
 * and every such app died there before its first frame. With the device kept, Flutter goes on to its
 * real context and the apps draw (EPD Green$, Sushiro, imsgchat).
 *
 * A kept device costs its driver threads and memory for the life of the process; apps destroy a
 * device rarely (a probe, a renderer torn down), so the first 8 are kept and any after that is
 * destroyed as asked. An instance whose devices may be kept is kept too: an instance may not be
 * destroyed while a device made from it lives. Each keep is logged once.
 */
#define WL_VK_KEPT_MAX 8
static int wl_vk_kept_devices;
static pthread_mutex_t wl_vk_kept_lock = PTHREAD_MUTEX_INITIALIZER;

static VKAPI_ATTR void VKAPI_CALL wl_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
    if (device == VK_NULL_HANDLE) return;
    pthread_mutex_lock(&wl_vk_kept_lock);
    int keep = wl_vk_kept_devices < WL_VK_KEPT_MAX;
    if (keep) wl_vk_kept_devices++;
    int kept = wl_vk_kept_devices;
    pthread_mutex_unlock(&wl_vk_kept_lock);
    if (keep) {
        fprintf(stderr, "[WL-VULKAN] vkDestroyDevice(%p): kept (%d of %d), as destroying a device aborts this "
                "board's Mali driver\n", (void*)device, kept, WL_VK_KEPT_MAX);
        return;
    }
    if (real_destroy_device) real_destroy_device(device, alloc);
}

static VKAPI_ATTR void VKAPI_CALL wl_vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* alloc) {
    if (instance == VK_NULL_HANDLE) return;
    pthread_mutex_lock(&wl_vk_kept_lock);
    int devices_kept = wl_vk_kept_devices;
    pthread_mutex_unlock(&wl_vk_kept_lock);
    if (devices_kept > 0) {
        fprintf(stderr, "[WL-VULKAN] vkDestroyInstance(%p): kept with the devices kept\n", (void*)instance);
        return;
    }
    if (real_destroy_instance) real_destroy_instance(instance, alloc);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
    wl_vkDestroyDevice(device, alloc);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* alloc) {
    wl_vkDestroyInstance(instance, alloc);
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
    if (!android) return real_create_instance(info, alloc, instance);
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
    if (!strcmp(name, "vkDestroyDevice")) return instance ? (PFN_vkVoidFunction)wl_vkDestroyDevice : NULL;
    if (!strcmp(name, "vkDestroyInstance")) return instance ? (PFN_vkVoidFunction)wl_vkDestroyInstance : NULL;
    PFN_vkVoidFunction function = real_gipa(instance, name);
    if (!function) wl_vk_missing("vkGetInstanceProcAddr", name);
    return function;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name) {
    if (!real_gdpa || !name) return NULL;
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (!strcmp(name, "vkDestroyDevice")) return (PFN_vkVoidFunction)wl_vkDestroyDevice;
    PFN_vkVoidFunction function = real_gdpa(device, name);
    if (!function) wl_vk_missing("vkGetDeviceProcAddr", name);
    return function;
}
