/* Headless-only provider. No hardware, IPC or device permissions are used. */
#include <assert.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <jni.h>
#include <LocationKit/oh_location.h>

#ifdef LOCATION_REQUEST_STANDALONE

static int outstanding;
static inline __attribute__((unused)) void *tracked_calloc(size_t count, size_t size)
{
    void *ptr = calloc(count, size);
    if (ptr) ++outstanding;
    return ptr;
}

static inline __attribute__((unused)) void tracked_free(void *ptr)
{
    if (ptr) --outstanding;
    free(ptr);
}

#define calloc tracked_calloc
#define free tracked_free
#ifndef LOCATION_BRIDGE_SOURCE
#define LOCATION_BRIDGE_SOURCE "../../../framework/javacore-shim/location_natives.c"
#endif
#include LOCATION_BRIDGE_SOURCE
#undef calloc
#undef free
#endif

static int created, destroyed, started, stopped;
static bool reject_start;
static bool location_enabled = true;
static Location_InfoCallback callback;
static void *callback_data;
static char sample;

static void emit_sample(void)
{
    callback((Location_Info *)&sample, callback_data);
}

Location_ResultCode OH_Location_IsLocatingEnabled(bool *value)
{
    *value = location_enabled;
    return LOCATION_SUCCESS;
}

Location_RequestConfig *OH_Location_CreateRequestConfig(void)
{
    ++created;
    return (Location_RequestConfig *)malloc(1);
}

void OH_Location_DestroyRequestConfig(Location_RequestConfig *config)
{
    ++destroyed;
    free(config);
}

void OH_LocationRequestConfig_SetCallback(Location_RequestConfig *config, Location_InfoCallback fn, void *data)
{
    assert(config && fn);
    callback = fn;
    callback_data = data;
}

void OH_LocationRequestConfig_SetPowerConsumptionScene(Location_RequestConfig *config, Location_PowerConsumptionScene scene)
{
    assert(config && scene == 0x0602);
}

void OH_LocationRequestConfig_SetInterval(Location_RequestConfig *config, int interval)
{
    assert(config && interval == 1);
}

Location_ResultCode OH_Location_StartLocating(const Location_RequestConfig *config)
{
    assert(config);
    ++started;
    if (reject_start) return LOCATION_SERVICE_UNAVAILABLE;
    if (started == 1) emit_sample();
    return LOCATION_SUCCESS;
}

Location_ResultCode OH_Location_StopLocating(const Location_RequestConfig *config)
{
    assert(config);
    ++stopped;
    return LOCATION_SUCCESS;
}

Location_BasicInfo OH_LocationInfo_GetBasicInfo(Location_Info *location)
{
    assert(location == (Location_Info *)&sample);
    return (Location_BasicInfo){.timeForFix = 1, .timeSinceBoot = 1, .locationSourceType = LOCATION_SOURCE_TYPE_NETWORK};
}

#ifndef LOCATION_REQUEST_STANDALONE
JNIEXPORT jboolean JNICALL Java_LocationRequestLifecycleProbe_isFixtureBound(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    void *lib = dlopen("liblocation_ndk.so", RTLD_NOW);
    if (!lib) return JNI_FALSE;
    void *fn = dlsym(lib, "OH_Location_StartLocating");
    dlclose(lib);
    return fn == (void *)OH_Location_StartLocating ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_LocationRequestLifecycleProbe_emit(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    emit_sample();
}

JNIEXPORT jint JNICALL Java_LocationRequestLifecycleProbe_stopCount(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return stopped;
}

JNIEXPORT void JNICALL Java_LocationRequestLifecycleProbe_switchOff(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    location_enabled = false;
}
#else
static void install_provider(void)
{
    oh.isEnabled = OH_Location_IsLocatingEnabled;
    oh.create = OH_Location_CreateRequestConfig;
    oh.destroy = OH_Location_DestroyRequestConfig;
    oh.setPower = OH_LocationRequestConfig_SetPowerConsumptionScene;
    oh.setInterval = OH_LocationRequestConfig_SetInterval;
    oh.setCallback = OH_LocationRequestConfig_SetCallback;
    oh.start = OH_Location_StartLocating;
    oh.stop = OH_Location_StopLocating;
    oh.basicInfo = OH_LocationInfo_GetBasicInfo;
    oh.resolved = 1;
}

int main(void)
{
    install_provider();
    const int iterations = 4096;
    for (int id = 1; id <= iterations; ++id) {
        jlong handle = native_start(NULL, NULL, id, 1, 0x0602);
        assert(handle > 0);
        assert(native_stop(NULL, NULL, handle) == LOCATION_SUCCESS);
        emit_sample();
    }
    reject_start = true;
    assert(native_start(NULL, NULL, iterations + 1, 1, 0x0602) == -LOCATION_SERVICE_UNAVAILABLE);
    printf("created=%d destroyed=%d stopped=%d retained=%d\n", created, destroyed, stopped, outstanding);
    return created == iterations + 1 && destroyed == created && stopped == iterations && outstanding == 0 ? 0 : 1;
}
#endif
