/*
 * adapter.core.LocationManagerAdapter's natives: OH's location service through its NDK (LocationKit,
 * liblocation_ndk.so), the service a HAP uses for the same thing (westlake ADR-0009 decision 1).
 *
 * nativeEnabled: OH_Location_IsLocatingEnabled, the device's location switch: 1 on, 0 off, or the
 * negative of OH's result code when the service could not be asked (-1 when the library is missing).
 *
 * nativeStart(requestId, intervalSeconds, powerScene): one OH request, started with
 * OH_Location_StartLocating; its fixes go to LocationManagerAdapter.onOhLocation(requestId, ...) on the
 * thread OH calls back on, attached for the call. Returns the request's handle, or the negative of OH's
 * result code (LOCATION_PERMISSION_DENIED 201 when the host application holds no location permission,
 * LOCATION_SWITCH_OFF 3301100, LOCATION_SERVICE_UNAVAILABLE 3301000).
 *
 * nativeStop(handle): OH_Location_StopLocating, then the configuration is destroyed. A late
 * callback carries only the Android request ID. Java has already removed it from its registry.
 *
 * The library is opened at run time, as libohnotification.so is for the notification switch: a board
 * without it answers -1 and Westlake keeps its compatibility floor (location off).
 */
#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <LocationKit/oh_location.h>
#include <LocationKit/oh_location_type.h>

typedef Location_ResultCode (*IsEnabledFn)(bool*);
typedef Location_ResultCode (*StartFn)(const Location_RequestConfig*);
typedef Location_ResultCode (*StopFn)(const Location_RequestConfig*);
typedef Location_RequestConfig* (*CreateFn)(void);
typedef void (*DestroyFn)(Location_RequestConfig*);
typedef void (*SetPowerFn)(Location_RequestConfig*, Location_PowerConsumptionScene);
typedef void (*SetIntervalFn)(Location_RequestConfig*, int);
typedef void (*SetCallbackFn)(Location_RequestConfig*, Location_InfoCallback, void*);
typedef Location_BasicInfo (*BasicInfoFn)(Location_Info*);

static struct {
    int resolved;
    IsEnabledFn isEnabled;
    StartFn start;
    StopFn stop;
    CreateFn create;
    DestroyFn destroy;
    SetPowerFn setPower;
    SetIntervalFn setInterval;
    SetCallbackFn setCallback;
    BasicInfoFn basicInfo;
} oh;

static JavaVM* g_vm;
static jclass g_adapter;
static jmethodID g_onLocation;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static int bind_location(void)
{
    pthread_mutex_lock(&g_lock);
    if (!oh.resolved) {
        oh.resolved = 1;
        void* lib = dlopen("liblocation_ndk.so", RTLD_NOW);
        if (lib == NULL) lib = dlopen("/system/lib64/ndk/liblocation_ndk.so", RTLD_NOW);
        if (lib != NULL) {
            oh.isEnabled = (IsEnabledFn) dlsym(lib, "OH_Location_IsLocatingEnabled");
            oh.start = (StartFn) dlsym(lib, "OH_Location_StartLocating");
            oh.stop = (StopFn) dlsym(lib, "OH_Location_StopLocating");
            oh.create = (CreateFn) dlsym(lib, "OH_Location_CreateRequestConfig");
            oh.destroy = (DestroyFn) dlsym(lib, "OH_Location_DestroyRequestConfig");
            oh.setPower = (SetPowerFn) dlsym(lib, "OH_LocationRequestConfig_SetPowerConsumptionScene");
            oh.setInterval = (SetIntervalFn) dlsym(lib, "OH_LocationRequestConfig_SetInterval");
            oh.setCallback = (SetCallbackFn) dlsym(lib, "OH_LocationRequestConfig_SetCallback");
            oh.basicInfo = (BasicInfoFn) dlsym(lib, "OH_LocationInfo_GetBasicInfo");
        }
        int complete = oh.isEnabled && oh.start && oh.stop && oh.create && oh.destroy && oh.setPower
                && oh.setInterval && oh.setCallback && oh.basicInfo;
        if (!complete) oh.isEnabled = NULL;
        fprintf(stderr, "[WL-LOCATION] OH location NDK %s\n", complete ? "bound" : "unavailable");
    }
    pthread_mutex_unlock(&g_lock);
    return oh.isEnabled != NULL;
}

static void on_location(Location_Info* location, void* user_data)
{
    jint id = (jint) (intptr_t) user_data;
    if (id == 0 || location == NULL || g_vm == NULL || g_onLocation == NULL) return;
    Location_BasicInfo info = oh.basicInfo(location);
    JNIEnv* env = NULL;
    int attached = 0;
    if ((*g_vm)->GetEnv(g_vm, (void**) &env, JNI_VERSION_1_6) != JNI_OK) {
        JavaVMAttachArgs args = {JNI_VERSION_1_6, "OH-location", NULL};
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, &args) != JNI_OK) return;
        attached = 1;
    }
    (*env)->CallStaticVoidMethod(env, g_adapter, g_onLocation, (jint) id, info.latitude, info.longitude,
            info.altitude, info.accuracy, info.speed, info.direction, (jlong) info.timeForFix,
            (jlong) info.timeSinceBoot, info.altitudeAccuracy, info.speedAccuracy, info.directionAccuracy,
            (jint) info.locationSourceType);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}

static jint native_enabled(JNIEnv* env, jclass cls)
{
    (void) env;
    (void) cls;
    if (!bind_location()) return -1;
    bool enabled = false;
    Location_ResultCode rc = oh.isEnabled(&enabled);
    if (rc != LOCATION_SUCCESS) return -(jint) rc;
    return enabled ? 1 : 0;
}

static jlong native_start(JNIEnv* env, jclass cls, jint id, jint interval_seconds, jint power_scene)
{
    (void) env;
    (void) cls;
    if (!bind_location()) return -1;
    Location_RequestConfig* config = oh.create();
    if (config == NULL) return -1;
    oh.setPower(config, (Location_PowerConsumptionScene) power_scene);
    oh.setInterval(config, interval_seconds > 0 ? interval_seconds : 1);
    oh.setCallback(config, on_location, (void*) (intptr_t) id);
    Location_ResultCode rc = oh.start(config);
    if (rc != LOCATION_SUCCESS) {
        oh.destroy(config);
        return -(jlong) rc;
    }
    return (jlong) (intptr_t) config;
}

static jint native_stop(JNIEnv* env, jclass cls, jlong handle)
{
    (void) env;
    (void) cls;
    if (handle <= 0 || !bind_location()) return -1;
    Location_RequestConfig* config = (Location_RequestConfig*) (intptr_t) handle;
    Location_ResultCode rc = oh.stop(config);
    oh.destroy(config);
    return (jint) rc;
}

int wl_register_location(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "adapter/core/LocationManagerAdapter");
    if (cls == NULL) {
        (*env)->ExceptionClear(env);
        return -1;
    }
    JNINativeMethod methods[] = {
        {"nativeEnabled", "()I", (void*) native_enabled},
        {"nativeStart", "(III)J", (void*) native_start},
        {"nativeStop", "(J)I", (void*) native_stop},
    };
    int rc = (*env)->RegisterNatives(env, cls, methods, 3);
    if (rc != 0) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, cls);
        return -1;
    }
    g_onLocation = (*env)->GetStaticMethodID(env, cls, "onOhLocation", "(IDDDDDDJJDDDI)V");
    if (g_onLocation == NULL) (*env)->ExceptionClear(env);
    g_adapter = (jclass) (*env)->NewGlobalRef(env, cls);
    (*env)->GetJavaVM(env, &g_vm);
    (*env)->DeleteLocalRef(env, cls);
    return g_onLocation != NULL ? 1 : -1;
}
