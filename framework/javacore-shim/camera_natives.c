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
    void *lib;
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
    g.lib = lib;
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

/* ---- stage 2: an open camera, its capture session and outputs (adapter.core.OhCameraDevice) ---------------- */

typedef struct Camera_Input Camera_Input;
typedef struct Camera_CaptureSession Camera_CaptureSession;
typedef struct Camera_PreviewOutput Camera_PreviewOutput;
typedef struct Camera_PhotoOutput Camera_PhotoOutput;

#define WL_MAX_OUTPUTS 8

typedef struct {
    Camera_Device *cameras;          /* OH's list, kept while the device is open: dev points into it */
    uint32_t count;
    Camera_Device *dev;
    Camera_OutputCapability *capability;
    Camera_Input *input;
    Camera_CaptureSession *session;
    Camera_PreviewOutput *previews[WL_MAX_OUTPUTS];
    int previewCount;
    Camera_PhotoOutput *photo;
    int started;
} wl_camera;

static void *sym(const char *name)
{
    void *f = g.lib != NULL ? dlsym(g.lib, name) : NULL;
    if (f == NULL) fprintf(stderr, "[WL-CAMERA] libohcamera.so lacks %s\n", name);
    return f;
}

#define CALL(type, name, ...) ({ type f_ = (type) sym(name); f_ != NULL ? f_(__VA_ARGS__) : CAMERA_OPERATION_NOT_ALLOWED; })

typedef Camera_ErrorCode (*CreateInputFn)(Camera_Manager *, const Camera_Device *, Camera_Input **);
typedef Camera_ErrorCode (*InputFn)(Camera_Input *);
typedef Camera_ErrorCode (*CreateSessionFn)(Camera_Manager *, Camera_CaptureSession **);
typedef Camera_ErrorCode (*SessionFn)(Camera_CaptureSession *);
typedef Camera_ErrorCode (*SessionModeFn)(Camera_CaptureSession *, Camera_SceneMode);
typedef Camera_ErrorCode (*SessionInputFn)(Camera_CaptureSession *, Camera_Input *);
typedef Camera_ErrorCode (*SessionPreviewFn)(Camera_CaptureSession *, Camera_PreviewOutput *);
typedef Camera_ErrorCode (*SessionPhotoFn)(Camera_CaptureSession *, Camera_PhotoOutput *);
typedef Camera_ErrorCode (*CreatePreviewFn)(Camera_Manager *, const Camera_Profile *, const char *, Camera_PreviewOutput **);
typedef Camera_ErrorCode (*PreviewFn)(Camera_PreviewOutput *);
typedef Camera_ErrorCode (*CreatePhotoFn)(Camera_Manager *, const Camera_Profile *, const char *, Camera_PhotoOutput **);
typedef Camera_ErrorCode (*PhotoFn)(Camera_PhotoOutput *);

/* The window behind a Java Surface: the runtime's ANativeWindow (*anw, which the caller then holds), unwrapped
 * to the OH window it stands for (the bridge's oh_anw_get_oh), as videocodec_natives.cpp does for MediaCodec. */
static void *ohWindowOf(JNIEnv *env, jobject surface, void **anw)
{
    typedef void *(*FromSurface)(JNIEnv *, jobject);
    typedef void *(*Unwrap)(void *);
    FromSurface fromSurface = (FromSurface) dlsym(RTLD_DEFAULT, "ANativeWindow_fromSurface");
    Unwrap unwrap = (Unwrap) dlsym(RTLD_DEFAULT, "oh_anw_get_oh");
    if (unwrap == NULL) {
        void *bridge = dlopen("liboh_adapter_bridge.so", RTLD_NOW | RTLD_NOLOAD);
        if (bridge != NULL) unwrap = (Unwrap) dlsym(bridge, "oh_anw_get_oh");
    }
    *anw = fromSurface != NULL && surface != NULL ? fromSurface(env, surface) : NULL;
    return *anw != NULL && unwrap != NULL ? unwrap(*anw) : NULL;
}

static void releaseWindow(void *anw)
{
    typedef void (*Release)(void *);
    Release release = anw != NULL ? (Release) dlsym(RTLD_DEFAULT, "ANativeWindow_release") : NULL;
    if (release != NULL) release(anw);
}

/* OH's surface id of the window behind a Java Surface, as OH's outputs take it ("<decimal id>"). */
static int surface_id(JNIEnv *env, jobject surface, char *out, size_t size)
{
    typedef int32_t (*GetSurfaceIdFn)(void *, uint64_t *);
    void *anw = NULL;
    void *window = ohWindowOf(env, surface, &anw);
    GetSurfaceIdFn getId = (GetSurfaceIdFn) dlsym(RTLD_DEFAULT, "OH_NativeWindow_GetSurfaceId");
    if (getId == NULL) {
        void *nw = dlopen("libnative_window.so", RTLD_NOW);
        if (nw != NULL) getId = (GetSurfaceIdFn) dlsym(nw, "OH_NativeWindow_GetSurfaceId");
    }
    uint64_t id = 0;
    int ok = window != NULL && getId != NULL && getId(window, &id) == 0 && id != 0;
    if (ok) snprintf(out, size, "%llu", (unsigned long long) id);
    releaseWindow(anw);
    return ok;
}

static void release_outputs(wl_camera *c)
{
    if (c->session != NULL) {
        if (c->started) CALL(SessionFn, "OH_CaptureSession_Stop", c->session);
        CALL(SessionFn, "OH_CaptureSession_Release", c->session);
        c->session = NULL;
    }
    c->started = 0;
    for (int i = 0; i < c->previewCount; i++) CALL(PreviewFn, "OH_PreviewOutput_Release", c->previews[i]);
    c->previewCount = 0;
    if (c->photo != NULL) CALL(PhotoFn, "OH_PhotoOutput_Release", c->photo);
    c->photo = NULL;
}

/* The OH profile for an output: the exact size in the list (OH's YUV first for previews), else none. */
static const Camera_Profile *profile_for(Camera_Profile **list, uint32_t size, uint32_t w, uint32_t h, int prefer)
{
    const Camera_Profile *any = NULL;
    for (uint32_t i = 0; i < size; i++) {
        if (list[i] == NULL || list[i]->size.width != w || list[i]->size.height != h) continue;
        if ((int) list[i]->format == prefer) return list[i];
        if (any == NULL) any = list[i];
    }
    return any;
}

static jlong CameraDevice_open(JNIEnv *env, jclass clazz, jstring ohId)
{
    (void) clazz;
    if (!load() || ohId == NULL) return 0;
    wl_camera *c = calloc(1, sizeof(*c));
    if (c == NULL) return 0;
    const char *id = (*env)->GetStringUTFChars(env, ohId, NULL);
    Camera_ErrorCode rc = g.getCameras(g.manager, &c->cameras, &c->count);
    for (uint32_t i = 0; rc == CAMERA_OK && id != NULL && i < c->count; i++) {
        if (c->cameras[i].cameraId != NULL && strcmp(c->cameras[i].cameraId, id) == 0) c->dev = &c->cameras[i];
    }
    if (id != NULL) (*env)->ReleaseStringUTFChars(env, ohId, id);
    if (rc == CAMERA_OK && c->dev == NULL) rc = CAMERA_INVALID_ARGUMENT;
    if (rc == CAMERA_OK) rc = g.getCapability(g.manager, c->dev, &c->capability);
    if (rc == CAMERA_OK) rc = CALL(CreateInputFn, "OH_CameraManager_CreateCameraInput", g.manager, c->dev, &c->input);
    if (rc == CAMERA_OK) rc = CALL(InputFn, "OH_CameraInput_Open", c->input);
    if (rc != CAMERA_OK) {
        fprintf(stderr, "[WL-CAMERA] opening the camera failed: OH %d\n", (int) rc);
        if (c->input != NULL) CALL(InputFn, "OH_CameraInput_Release", c->input);
        if (c->capability != NULL && g.deleteCapability != NULL) g.deleteCapability(g.manager, c->capability);
        if (c->cameras != NULL && g.deleteCameras != NULL) g.deleteCameras(g.manager, c->cameras, c->count);
        free(c);
        return -(jlong) rc;   /* OH's error, negated: a handle is never negative */
    }
    return (jlong) (intptr_t) c;
}

/*
 * The session for these outputs: kinds[i] 0 = a preview-like stream (OH preview output: the app's SurfaceView,
 * SurfaceTexture or YUV ImageReader), 1 = JPEG (OH photo output). OH codes are returned (0 = CAMERA_OK); the
 * outputs are started by nativeStart.
 */
static jint CameraDevice_configure(JNIEnv *env, jclass clazz, jlong handle, jobjectArray surfaces, jintArray kinds,
                                   jintArray widths, jintArray heights)
{
    (void) clazz;
    wl_camera *c = (wl_camera *) (intptr_t) handle;
    if (c == NULL) return CAMERA_INVALID_ARGUMENT;
    release_outputs(c);
    jsize n = surfaces ? (*env)->GetArrayLength(env, surfaces) : 0;
    if (n > WL_MAX_OUTPUTS) n = WL_MAX_OUTPUTS;
    jint *kind = n ? (*env)->GetIntArrayElements(env, kinds, NULL) : NULL;
    jint *w = n ? (*env)->GetIntArrayElements(env, widths, NULL) : NULL;
    jint *h = n ? (*env)->GetIntArrayElements(env, heights, NULL) : NULL;
    Camera_ErrorCode rc = CALL(CreateSessionFn, "OH_CameraManager_CreateCaptureSession", g.manager, &c->session);
    if (rc == CAMERA_OK) CALL(SessionModeFn, "OH_CaptureSession_SetSessionMode", c->session, NORMAL_PHOTO);
    if (rc == CAMERA_OK) rc = CALL(SessionFn, "OH_CaptureSession_BeginConfig", c->session);
    if (rc == CAMERA_OK) rc = CALL(SessionInputFn, "OH_CaptureSession_AddInput", c->session, c->input);
    for (jsize i = 0; rc == CAMERA_OK && i < n; i++) {
        jobject surface = (*env)->GetObjectArrayElement(env, surfaces, i);
        char id[32];
        if (surface == NULL || !surface_id(env, surface, id, sizeof(id))) {
            fprintf(stderr, "[WL-CAMERA] output %d: no OH surface behind the app's Surface\n", (int) i);
            rc = CAMERA_INVALID_ARGUMENT;
        } else if (kind[i] == 1) {
            const Camera_Profile *p = profile_for(c->capability->photoProfiles, c->capability->photoProfilesSize,
                                                  (uint32_t) w[i], (uint32_t) h[i], 2000 /* JPEG */);
            if (p == NULL) rc = CAMERA_INVALID_ARGUMENT;
            if (rc == CAMERA_OK && c->photo == NULL)
                rc = CALL(CreatePhotoFn, "OH_CameraManager_CreatePhotoOutput", g.manager, p, id, &c->photo);
            if (rc == CAMERA_OK) rc = CALL(SessionPhotoFn, "OH_CaptureSession_AddPhotoOutput", c->session, c->photo);
        } else {
            const Camera_Profile *p = profile_for(c->capability->previewProfiles, c->capability->previewProfilesSize,
                                                  (uint32_t) w[i], (uint32_t) h[i], 1003 /* YUV_420_SP */);
            Camera_PreviewOutput *out = NULL;
            if (p == NULL) rc = CAMERA_INVALID_ARGUMENT;
            if (rc == CAMERA_OK) rc = CALL(CreatePreviewFn, "OH_CameraManager_CreatePreviewOutput", g.manager, p, id, &out);
            if (rc == CAMERA_OK) {
                c->previews[c->previewCount++] = out;
                rc = CALL(SessionPreviewFn, "OH_CaptureSession_AddPreviewOutput", c->session, out);
            }
        }
        fprintf(stderr, "[WL-CAMERA] output %d: %s %dx%d -> %d\n", (int) i, kind[i] == 1 ? "jpeg" : "preview",
                (int) w[i], (int) h[i], (int) rc);
        if (surface != NULL) (*env)->DeleteLocalRef(env, surface);
    }
    if (rc == CAMERA_OK) rc = CALL(SessionFn, "OH_CaptureSession_CommitConfig", c->session);
    if (kind) (*env)->ReleaseIntArrayElements(env, kinds, kind, JNI_ABORT);
    if (w) (*env)->ReleaseIntArrayElements(env, widths, w, JNI_ABORT);
    if (h) (*env)->ReleaseIntArrayElements(env, heights, h, JNI_ABORT);
    if (rc != CAMERA_OK) release_outputs(c);
    return (jint) rc;
}

static jint CameraDevice_start(JNIEnv *env, jclass clazz, jlong handle)
{
    (void) env; (void) clazz;
    wl_camera *c = (wl_camera *) (intptr_t) handle;
    if (c == NULL || c->session == NULL) return CAMERA_INVALID_ARGUMENT;
    if (c->started) return CAMERA_OK;
    Camera_ErrorCode rc = CALL(SessionFn, "OH_CaptureSession_Start", c->session);
    c->started = rc == CAMERA_OK;
    return (jint) rc;
}

static jint CameraDevice_stop(JNIEnv *env, jclass clazz, jlong handle)
{
    (void) env; (void) clazz;
    wl_camera *c = (wl_camera *) (intptr_t) handle;
    if (c == NULL || c->session == NULL || !c->started) return CAMERA_OK;
    c->started = 0;
    return (jint) CALL(SessionFn, "OH_CaptureSession_Stop", c->session);
}

static jint CameraDevice_capture(JNIEnv *env, jclass clazz, jlong handle)
{
    (void) env; (void) clazz;
    wl_camera *c = (wl_camera *) (intptr_t) handle;
    if (c == NULL || c->photo == NULL) return CAMERA_INVALID_ARGUMENT;
    if (!c->started) {
        Camera_ErrorCode rc = CALL(SessionFn, "OH_CaptureSession_Start", c->session);
        if (rc != CAMERA_OK) return (jint) rc;
        c->started = 1;
    }
    return (jint) CALL(PhotoFn, "OH_PhotoOutput_Capture", c->photo);
}

static void CameraDevice_close(JNIEnv *env, jclass clazz, jlong handle)
{
    (void) env; (void) clazz;
    wl_camera *c = (wl_camera *) (intptr_t) handle;
    if (c == NULL) return;
    release_outputs(c);
    if (c->input != NULL) {
        CALL(InputFn, "OH_CameraInput_Close", c->input);
        CALL(InputFn, "OH_CameraInput_Release", c->input);
    }
    if (c->capability != NULL && g.deleteCapability != NULL) g.deleteCapability(g.manager, c->capability);
    if (c->cameras != NULL && g.deleteCameras != NULL) g.deleteCameras(g.manager, c->cameras, c->count);
    free(c);
}

static const JNINativeMethod kCameraDeviceMethods[] = {
    {"nativeOpen", "(Ljava/lang/String;)J", (void *) CameraDevice_open},
    {"nativeConfigure", "(J[Landroid/view/Surface;[I[I[I)I", (void *) CameraDevice_configure},
    {"nativeStart", "(J)I", (void *) CameraDevice_start},
    {"nativeStop", "(J)I", (void *) CameraDevice_stop},
    {"nativeCapture", "(J)I", (void *) CameraDevice_capture},
    {"nativeClose", "(J)V", (void *) CameraDevice_close},
};

/* ---- android.hardware.camera2.utils.SurfaceUtils: what an OutputConfiguration asks of an app's Surface ------- */

typedef int32_t (*WindowIntFn)(void *);

static int32_t window_query(JNIEnv *env, jobject surface, const char *name)
{
    void *anw = NULL;
    typedef void *(*FromSurface)(JNIEnv *, jobject);
    FromSurface fromSurface = (FromSurface) dlsym(RTLD_DEFAULT, "ANativeWindow_fromSurface");
    WindowIntFn query = (WindowIntFn) dlsym(RTLD_DEFAULT, name);
    anw = fromSurface != NULL && surface != NULL ? fromSurface(env, surface) : NULL;
    int32_t value = anw != NULL && query != NULL ? query(anw) : -1;
    releaseWindow(anw);
    return value;
}

/* The Surface's pixel format; a window that does not say is a consumer of implementation-defined buffers
 * (HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED), as a SurfaceView or SurfaceTexture is. */
static jint SurfaceUtils_detectType(JNIEnv *env, jclass clazz, jobject surface)
{
    (void) clazz;
    int32_t format = window_query(env, surface, "ANativeWindow_getFormat");
    return format > 0 ? format : 0x22;
}

static jint SurfaceUtils_detectDataspace(JNIEnv *env, jclass clazz, jobject surface)
{
    (void) env; (void) clazz; (void) surface;
    return 0;   /* HAL_DATASPACE_UNKNOWN */
}

static jlong SurfaceUtils_detectUsageFlags(JNIEnv *env, jclass clazz, jobject surface)
{
    (void) env; (void) clazz; (void) surface;
    return 0x100;   /* GRALLOC_USAGE_HW_TEXTURE: a preview consumer */
}

static jint SurfaceUtils_detectDimens(JNIEnv *env, jclass clazz, jobject surface, jintArray dimens)
{
    (void) clazz;
    jint d[2] = {window_query(env, surface, "ANativeWindow_getWidth"), window_query(env, surface, "ANativeWindow_getHeight")};
    if (d[0] <= 0 || d[1] <= 0) return -19;   /* NO_INIT */
    (*env)->SetIntArrayRegion(env, dimens, 0, 2, d);
    return 0;
}

static jlong SurfaceUtils_getSurfaceId(JNIEnv *env, jclass clazz, jobject surface)
{
    (void) clazz;
    char id[32];
    return surface_id(env, surface, id, sizeof(id)) ? (jlong) strtoull(id, NULL, 10) : 0;
}

static const JNINativeMethod kSurfaceUtilsMethods[] = {
    {"nativeDetectSurfaceType", "(Landroid/view/Surface;)I", (void *) SurfaceUtils_detectType},
    {"nativeDetectSurfaceDataspace", "(Landroid/view/Surface;)I", (void *) SurfaceUtils_detectDataspace},
    {"nativeDetectSurfaceUsageFlags", "(Landroid/view/Surface;)J", (void *) SurfaceUtils_detectUsageFlags},
    {"nativeDetectSurfaceDimens", "(Landroid/view/Surface;[I)I", (void *) SurfaceUtils_detectDimens},
    {"nativeGetSurfaceId", "(Landroid/view/Surface;)J", (void *) SurfaceUtils_getSurfaceId},
};

/* ---- android.hardware.Camera (Camera1): its natives forwarded to adapter.core.OhCamera1 ----------------------- */

static jclass gCamera1;   /* global ref to adapter.core.OhCamera1 */

static jmethodID camera1_method(JNIEnv *env, const char *name, const char *signature)
{
    if (gCamera1 == NULL) return NULL;
    jmethodID m = (*env)->GetStaticMethodID(env, gCamera1, name, signature);
    if (m == NULL) {
        (*env)->ExceptionClear(env);
        fprintf(stderr, "[WL-CAMERA] OhCamera1.%s%s missing\n", name, signature);
    }
    return m;
}

#define CAMERA1_VOID(name, sig, ...) do { jmethodID m_ = camera1_method(env, name, sig); \
    if (m_ != NULL) (*env)->CallStaticVoidMethod(env, gCamera1, m_, __VA_ARGS__); } while (0)

static jint Camera1_count(JNIEnv *env, jclass clazz, jint deviceId, jint policy)
{
    (void) clazz; (void) deviceId; (void) policy;
    jmethodID m = camera1_method(env, "getNumberOfCameras", "()I");
    return m != NULL ? (*env)->CallStaticIntMethod(env, gCamera1, m) : 0;
}

static void Camera1_info(JNIEnv *env, jclass clazz, jint id, jint rotation, jint slow, jint policy, jobject info)
{
    (void) clazz; (void) rotation; (void) slow; (void) policy;
    CAMERA1_VOID("getCameraInfo", "(ILandroid/hardware/Camera$CameraInfo;)V", id, info);
}

static jint Camera1_setup(JNIEnv *env, jobject thiz, jobject cameraRef, jint id, jstring package, jint rotation,
                          jboolean slow, jint client, jint policy)
{
    (void) package; (void) rotation; (void) slow; (void) client; (void) policy;
    jmethodID m = camera1_method(env, "setup", "(Ljava/lang/Object;Ljava/lang/Object;I)I");
    return m != NULL ? (*env)->CallStaticIntMethod(env, gCamera1, m, thiz, cameraRef, id) : -13;
}

static void Camera1_release(JNIEnv *env, jobject thiz) { CAMERA1_VOID("release", "(Ljava/lang/Object;)V", thiz); }
static void Camera1_setPreviewSurface(JNIEnv *env, jobject thiz, jobject surface)
{
    CAMERA1_VOID("setPreviewSurface", "(Ljava/lang/Object;Landroid/view/Surface;)V", thiz, surface);
}
static void Camera1_setPreviewTexture(JNIEnv *env, jobject thiz, jobject texture)
{
    CAMERA1_VOID("setPreviewTexture", "(Ljava/lang/Object;Landroid/graphics/SurfaceTexture;)V", thiz, texture);
}
static void Camera1_startPreview(JNIEnv *env, jobject thiz) { CAMERA1_VOID("startPreview", "(Ljava/lang/Object;)V", thiz); }
static void Camera1_stopPreview(JNIEnv *env, jobject thiz) { CAMERA1_VOID("stopPreview", "(Ljava/lang/Object;)V", thiz); }
static jboolean Camera1_previewEnabled(JNIEnv *env, jobject thiz)
{
    jmethodID m = camera1_method(env, "previewEnabled", "(Ljava/lang/Object;)Z");
    return m != NULL ? (*env)->CallStaticBooleanMethod(env, gCamera1, m, thiz) : JNI_FALSE;
}
static void Camera1_autoFocus(JNIEnv *env, jobject thiz) { CAMERA1_VOID("autoFocus", "(Ljava/lang/Object;)V", thiz); }
static void Camera1_takePicture(JNIEnv *env, jobject thiz, jint msgType)
{
    CAMERA1_VOID("takePicture", "(Ljava/lang/Object;I)V", thiz, msgType);
}
static void Camera1_setParameters(JNIEnv *env, jobject thiz, jstring params)
{
    CAMERA1_VOID("setParameters", "(Ljava/lang/Object;Ljava/lang/String;)V", thiz, params);
}
static jstring Camera1_getParameters(JNIEnv *env, jobject thiz)
{
    jmethodID m = camera1_method(env, "getParameters", "(Ljava/lang/Object;)Ljava/lang/String;");
    return m != NULL ? (jstring) (*env)->CallStaticObjectMethod(env, gCamera1, m, thiz) : NULL;
}

/* What a camera without these features answers: no preview callbacks, zoom, face detection or audio restriction;
 * the lock, unlock and reconnect a single client never needs; the shutter sound can be disabled. */
static void Camera1_noop(JNIEnv *env, jobject thiz) { (void) env; (void) thiz; }
static void Camera1_noopI(JNIEnv *env, jobject thiz, jint v) { (void) env; (void) thiz; (void) v; }
static void Camera1_noopZZ(JNIEnv *env, jobject thiz, jboolean a, jboolean b) { (void) env; (void) thiz; (void) a; (void) b; }
static void Camera1_noopBufI(JNIEnv *env, jobject thiz, jbyteArray b, jint m) { (void) env; (void) thiz; (void) b; (void) m; }
static void Camera1_noopSurface(JNIEnv *env, jobject thiz, jobject s) { (void) env; (void) thiz; (void) s; }
static jboolean Camera1_shutterSound(JNIEnv *env, jobject thiz, jboolean enabled) { (void) env; (void) thiz; (void) enabled; return JNI_TRUE; }
static jint Camera1_audioRestriction(JNIEnv *env, jobject thiz) { (void) env; (void) thiz; return 0; }

static const JNINativeMethod kCamera1Methods[] = {
    {"_getNumberOfCameras", "(II)I", (void *) Camera1_count},
    {"_getCameraInfo", "(IIIILandroid/hardware/Camera$CameraInfo;)V", (void *) Camera1_info},
    {"native_setup", "(Ljava/lang/Object;ILjava/lang/String;IZII)I", (void *) Camera1_setup},
    {"native_release", "()V", (void *) Camera1_release},
    {"setPreviewSurface", "(Landroid/view/Surface;)V", (void *) Camera1_setPreviewSurface},
    {"setPreviewTexture", "(Landroid/graphics/SurfaceTexture;)V", (void *) Camera1_setPreviewTexture},
    {"startPreview", "()V", (void *) Camera1_startPreview},
    {"_stopPreview", "()V", (void *) Camera1_stopPreview},
    {"previewEnabled", "()Z", (void *) Camera1_previewEnabled},
    {"native_autoFocus", "()V", (void *) Camera1_autoFocus},
    {"native_cancelAutoFocus", "()V", (void *) Camera1_noop},
    {"native_takePicture", "(I)V", (void *) Camera1_takePicture},
    {"native_setParameters", "(Ljava/lang/String;)V", (void *) Camera1_setParameters},
    {"native_getParameters", "()Ljava/lang/String;", (void *) Camera1_getParameters},
    {"setHasPreviewCallback", "(ZZ)V", (void *) Camera1_noopZZ},
    {"_addCallbackBuffer", "([BI)V", (void *) Camera1_noopBufI},
    {"setPreviewCallbackSurface", "(Landroid/view/Surface;)V", (void *) Camera1_noopSurface},
    {"unlock", "()V", (void *) Camera1_noop},
    {"lock", "()V", (void *) Camera1_noop},
    {"reconnect", "()V", (void *) Camera1_noop},
    {"startSmoothZoom", "(I)V", (void *) Camera1_noopI},
    {"stopSmoothZoom", "()V", (void *) Camera1_noop},
    {"setDisplayOrientation", "(I)V", (void *) Camera1_noopI},
    {"_enableShutterSound", "(Z)Z", (void *) Camera1_shutterSound},
    {"enableFocusMoveCallback", "(I)V", (void *) Camera1_noopI},
    {"_startFaceDetection", "(I)V", (void *) Camera1_noopI},
    {"_stopFaceDetection", "()V", (void *) Camera1_noop},
    {"setAudioRestriction", "(I)V", (void *) Camera1_noopI},
    {"getAudioRestriction", "()I", (void *) Camera1_audioRestriction},
};

static int bind_all(JNIEnv *env, const char *name, const JNINativeMethod *methods, size_t n)
{
    jclass clazz = (*env)->FindClass(env, name);
    if (clazz == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    int bound = 0;
    for (size_t i = 0; i < n; i++) {
        if ((*env)->RegisterNatives(env, clazz, &methods[i], 1) == JNI_OK) bound++;
        else (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, clazz);
    return bound;
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
    int device = bind_all(env, "adapter/core/OhCameraDevice", kCameraDeviceMethods,
                          sizeof(kCameraDeviceMethods) / sizeof(kCameraDeviceMethods[0]));
    int utils = bind_all(env, "android/hardware/camera2/utils/SurfaceUtils", kSurfaceUtilsMethods,
                         sizeof(kSurfaceUtilsMethods) / sizeof(kSurfaceUtilsMethods[0]));
    jclass helper = (*env)->FindClass(env, "adapter/core/OhCamera1");
    int camera1 = 0;
    if (helper != NULL) {
        gCamera1 = (jclass) (*env)->NewGlobalRef(env, helper);
        (*env)->DeleteLocalRef(env, helper);
        camera1 = bind_all(env, "android/hardware/Camera", kCamera1Methods,
                           sizeof(kCamera1Methods) / sizeof(kCamera1Methods[0]));
    } else {
        (*env)->ExceptionClear(env);
    }
    fprintf(stderr, "[WL-NATIVES] camera: service %d/1 device %d/6 SurfaceUtils %d/5 Camera1 %d/%zu\n", bound, device,
            utils, camera1, sizeof(kCamera1Methods) / sizeof(kCamera1Methods[0]));
    return bound;
}
