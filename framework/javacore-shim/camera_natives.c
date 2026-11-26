/*
 * camera_natives.c (libwl_missing_natives.so)
 *
 * OH's cameras, as OH's camera NDK (libohcamera.so, API 12+) describes them, for Westlake's camera service
 * (adapter.core.OhCameraService, the "media.camera" an app's CameraManager asks). The library is opened at run
 * time, as location_natives.c opens OH's location NDK: an image without it lists no camera.
 *
 * nativeCameraList() answers one line per camera, fields separated by tabs:
 *   <OH camera id> <position: 1 back, 2 front, 0 unspecified> <sensor orientation, degrees>
 *   <preview profiles "format:width:height,..."> <photo profiles, same form>
 * and an empty string when OH lists none or the NDK is missing. OH formats are Camera_Format values
 * (CAMERA_FORMAT_YUV_420_SP 1003, CAMERA_FORMAT_JPEG 2000, CAMERA_FORMAT_RGBA_8888 3).
 */

#include <dlfcn.h>
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ohcamera/camera.h>
#include <ohcamera/camera_device.h>
/* not camera_manager.h: through photo_output.h it includes a C++-only header (rawfile/raw_file.h); the calls go
 * through dlsym with the types below. */

typedef Camera_ErrorCode (*GetManagerFn)(Camera_Manager **);
typedef Camera_ErrorCode (*GetCamerasFn)(Camera_Manager *, Camera_Device **, uint32_t *);
typedef Camera_ErrorCode (*DeleteCamerasFn)(Camera_Manager *, Camera_Device *, uint32_t);
typedef Camera_ErrorCode (*GetCapabilityFn)(Camera_Manager *, const Camera_Device *, Camera_OutputCapability **);
typedef Camera_ErrorCode (*DeleteCapabilityFn)(Camera_Manager *, Camera_OutputCapability *);
typedef Camera_ErrorCode (*GetOrientationFn)(Camera_Device *, uint32_t *);

static struct {
    int loaded;
    GetManagerFn getManager;
    GetCamerasFn getCameras;
    DeleteCamerasFn deleteCameras;
    GetCapabilityFn getCapability;
    DeleteCapabilityFn deleteCapability;
    GetOrientationFn getOrientation;
    Camera_Manager *manager;
} g;

static int load(void)
{
    if (g.loaded) return g.manager != NULL;
    g.loaded = 1;
    void *lib = dlopen("libohcamera.so", RTLD_NOW);
    if (lib == NULL) lib = dlopen("/system/lib64/ndk/libohcamera.so", RTLD_NOW);
    if (lib == NULL) {
        fprintf(stderr, "[WL-CAMERA] libohcamera.so unavailable: %s\n", dlerror());
        return 0;
    }
    g.getManager = (GetManagerFn) dlsym(lib, "OH_Camera_GetCameraManager");
    g.getCameras = (GetCamerasFn) dlsym(lib, "OH_CameraManager_GetSupportedCameras");
    g.deleteCameras = (DeleteCamerasFn) dlsym(lib, "OH_CameraManager_DeleteSupportedCameras");
    g.getCapability = (GetCapabilityFn) dlsym(lib, "OH_CameraManager_GetSupportedCameraOutputCapability");
    g.deleteCapability = (DeleteCapabilityFn) dlsym(lib, "OH_CameraManager_DeleteSupportedCameraOutputCapability");
    g.getOrientation = (GetOrientationFn) dlsym(lib, "OH_CameraDevice_GetCameraOrientation");
    if (g.getManager == NULL || g.getCameras == NULL || g.getCapability == NULL) {
        fprintf(stderr, "[WL-CAMERA] libohcamera.so lacks the camera manager calls\n");
        return 0;
    }
    Camera_ErrorCode rc = g.getManager(&g.manager);
    if (rc != CAMERA_OK || g.manager == NULL) {
        fprintf(stderr, "[WL-CAMERA] OH_Camera_GetCameraManager failed: %d\n", (int) rc);
        g.manager = NULL;
        return 0;
    }
    return 1;
}

typedef struct {
    char *text;
    size_t used, capacity;
} buf;

static void put(buf *b, const char *format, ...) __attribute__((format(printf, 2, 3)));

#include <stdarg.h>
static void put(buf *b, const char *format, ...)
{
    for (;;) {
        va_list args;
        va_start(args, format);
        int n = vsnprintf(b->text ? b->text + b->used : NULL, b->text ? b->capacity - b->used : 0, format, args);
        va_end(args);
        if (n < 0) return;
        if (b->text != NULL && b->used + (size_t) n < b->capacity) {
            b->used += (size_t) n;
            return;
        }
        size_t grown = (b->capacity + (size_t) n + 1) * 2;
        char *t = realloc(b->text, grown);
        if (t == NULL) return;
        b->text = t;
        b->capacity = grown;
    }
}

static void profiles(buf *b, Camera_Profile **list, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        if (list[i] == NULL) continue;
        put(b, "%s%d:%u:%u", i ? "," : "", (int) list[i]->format, list[i]->size.width, list[i]->size.height);
    }
}

static jstring Camera_list(JNIEnv *env, jclass clazz)
{
    (void) clazz;
    buf b = {0};
    put(&b, "%s", "");
    if (load()) {
        Camera_Device *cameras = NULL;
        uint32_t count = 0;
        Camera_ErrorCode rc = g.getCameras(g.manager, &cameras, &count);
        if (rc == CAMERA_OK && cameras != NULL) {
            for (uint32_t i = 0; i < count; i++) {
                Camera_Device *c = &cameras[i];
                uint32_t orientation = 0;
                if (g.getOrientation != NULL) g.getOrientation(c, &orientation);
                put(&b, "%s\t%d\t%u\t", c->cameraId ? c->cameraId : "", (int) c->cameraPosition, orientation);
                Camera_OutputCapability *capability = NULL;
                if (g.getCapability(g.manager, c, &capability) == CAMERA_OK && capability != NULL) {
                    profiles(&b, capability->previewProfiles, capability->previewProfilesSize);
                    put(&b, "\t");
                    profiles(&b, capability->photoProfiles, capability->photoProfilesSize);
                    if (g.deleteCapability != NULL) g.deleteCapability(g.manager, capability);
                } else {
                    put(&b, "\t");
                }
                put(&b, "\n");
            }
            if (g.deleteCameras != NULL) g.deleteCameras(g.manager, cameras, count);
        } else {
            fprintf(stderr, "[WL-CAMERA] OH_CameraManager_GetSupportedCameras: %d, %u cameras\n", (int) rc, count);
        }
    }
    jstring out = (*env)->NewStringUTF(env, b.text ? b.text : "");
    free(b.text);
    return out;
}

static const JNINativeMethod kCameraMethods[] = {
    {"nativeCameraList", "()Ljava/lang/String;", (void *) Camera_list},
};

int wl_register_oh_cameras(JNIEnv *env)
{
    jclass clazz = (*env)->FindClass(env, "adapter/core/OhCameraService");
    if (clazz == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    int bound = (*env)->RegisterNatives(env, clazz, kCameraMethods, 1) == JNI_OK;
    if (!bound) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, clazz);
    return bound;
}
