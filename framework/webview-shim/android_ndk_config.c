/*
 * libandroid's AConfiguration, sensor and input entry points, for callers in every namespace.
 *
 * The runtime's libandroid.so (default namespace) has neither, and the WebView shim's copy (the
 * Android namespace) has seven sensor calls and no AConfiguration. SDL2 imports
 * AConfiguration_new/fromAssetManager/getLanguage/getCountry/delete and eleven sensor calls, so
 * libSDL2.so could not relocate in either namespace; Go's mobile runtime (aaaaxy) reads density and
 * orientation. This library is preloaded, so its definitions answer every caller.
 *
 * AConfiguration is filled from the application's own Configuration, the one its Resources use.
 * AOSP's AConfiguration_fromAssetManager reads the AssetManager's configuration, which Resources set
 * from that same Configuration.
 *
 * Sensors answer as a board without them: no sensor is listed or found, a queue can be created,
 * and enabling one fails for want of a sensor. These are the WebView shim libandroid's answers,
 * which these definitions replace for its callers too. Input answers as a platform whose input
 * queue never reaches an app (below).
 */
#include <dlfcn.h>
#include <errno.h>
#include <jni.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* ---- AConfiguration ------------------------------------------------------------------------- */

typedef struct AConfiguration {
    int32_t mcc, mnc;
    char language[2], country[2];
    int32_t orientation, touchscreen, density, keyboard, navigation, keysHidden, navHidden;
    int32_t sdkVersion, screenSize, screenLong, screenRound, uiModeType, uiModeNight;
    int32_t screenWidthDp, screenHeightDp, smallestScreenWidthDp, layoutDirection, grammaticalGender;
} AConfiguration;

/* AConfiguration_diff bits (configuration.h). */
enum {
    DIFF_MCC = 0x0001, DIFF_MNC = 0x0002, DIFF_LOCALE = 0x0004, DIFF_TOUCHSCREEN = 0x0008,
    DIFF_KEYBOARD = 0x0010, DIFF_KEYBOARD_HIDDEN = 0x0020, DIFF_NAVIGATION = 0x0040,
    DIFF_ORIENTATION = 0x0080, DIFF_DENSITY = 0x0100, DIFF_SCREEN_SIZE = 0x0200, DIFF_VERSION = 0x0400,
    DIFF_SCREEN_LAYOUT = 0x0800, DIFF_UI_MODE = 0x1000, DIFF_SMALLEST_SCREEN_SIZE = 0x2000,
    DIFF_LAYOUTDIR = 0x4000, DIFF_SCREEN_ROUND = 0x8000, DIFF_GRAMMATICAL_GENDER = 0x20000,
};

static JavaVM *wl_java_vm(void)
{
    static JavaVM *vm;
    if (vm == NULL) {
        typedef jint (*GetVms)(JavaVM **, jsize, jsize *);
        GetVms get = (GetVms) dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
        jsize count = 0;
        if (get == NULL || get(&vm, 1, &count) != JNI_OK || count < 1) vm = NULL;
    }
    return vm;
}

static bool wl_clear(JNIEnv *env)
{
    if (!(*env)->ExceptionCheck(env)) return false;
    (*env)->ExceptionClear(env);
    return true;
}

static jint wl_int_field(JNIEnv *env, jobject object, jclass cls, const char *name)
{
    jfieldID field = (*env)->GetFieldID(env, cls, name, "I");
    if (field == NULL) {
        wl_clear(env);
        return 0;
    }
    return (*env)->GetIntField(env, object, field);
}

/* The first two letters of locale.getLanguage() or getCountry(); zeros when unset. */
static void wl_locale_code(JNIEnv *env, jobject locale, const char *getter, char out[2])
{
    out[0] = out[1] = 0;
    jclass cls = (*env)->GetObjectClass(env, locale);
    jmethodID method = (*env)->GetMethodID(env, cls, getter, "()Ljava/lang/String;");
    jstring value = method != NULL ? (jstring) (*env)->CallObjectMethod(env, locale, method) : NULL;
    if (wl_clear(env) || value == NULL) {
        (*env)->DeleteLocalRef(env, cls);
        return;
    }
    const char *chars = (*env)->GetStringUTFChars(env, value, NULL);
    if (chars != NULL && strlen(chars) >= 2) {
        out[0] = chars[0];
        out[1] = chars[1];
    }
    if (chars != NULL) (*env)->ReleaseStringUTFChars(env, value, chars);
    (*env)->DeleteLocalRef(env, value);
    (*env)->DeleteLocalRef(env, cls);
}

static jobject wl_call(JNIEnv *env, jobject object, const char *cls_name, const char *name, const char *sig)
{
    if (object == NULL) return NULL;
    jclass cls = (*env)->FindClass(env, cls_name);
    jmethodID method = cls != NULL ? (*env)->GetMethodID(env, cls, name, sig) : NULL;
    jobject result = method != NULL ? (*env)->CallObjectMethod(env, object, method) : NULL;
    if (wl_clear(env)) result = NULL;
    if (cls != NULL) (*env)->DeleteLocalRef(env, cls);
    return result;
}

/* As ResourcesImpl hands its Configuration to the AssetManager (ResTable_config fields). */
static void wl_read_configuration(JNIEnv *env, AConfiguration *out)
{
    jclass thread = (*env)->FindClass(env, "android/app/ActivityThread");
    jmethodID current = thread != NULL
            ? (*env)->GetStaticMethodID(env, thread, "currentApplication", "()Landroid/app/Application;") : NULL;
    jobject app = current != NULL ? (*env)->CallStaticObjectMethod(env, thread, current) : NULL;
    if (wl_clear(env)) app = NULL;
    jobject resources = wl_call(env, app, "android/content/Context", "getResources",
                                "()Landroid/content/res/Resources;");
    jobject config = wl_call(env, resources, "android/content/res/Resources", "getConfiguration",
                             "()Landroid/content/res/Configuration;");
    jclass cls = (*env)->FindClass(env, "android/content/res/Configuration");
    if (config != NULL && cls != NULL) {
        out->mcc = wl_int_field(env, config, cls, "mcc");
        out->mnc = wl_int_field(env, config, cls, "mnc");
        out->orientation = wl_int_field(env, config, cls, "orientation");
        out->touchscreen = wl_int_field(env, config, cls, "touchscreen");
        out->density = wl_int_field(env, config, cls, "densityDpi");
        out->keyboard = wl_int_field(env, config, cls, "keyboard");
        out->navigation = wl_int_field(env, config, cls, "navigation");
        int32_t keys = wl_int_field(env, config, cls, "keyboardHidden");
        /* KEYBOARDHIDDEN_NO with the hard keyboard hidden is KEYSHIDDEN_SOFT, as ResourcesImpl sets. */
        if (keys == 1 && wl_int_field(env, config, cls, "hardKeyboardHidden") == 2) keys = 3;
        out->keysHidden = keys;
        out->navHidden = wl_int_field(env, config, cls, "navigationHidden");
        int32_t layout = wl_int_field(env, config, cls, "screenLayout");
        out->screenSize = layout & 0x0f;
        out->screenLong = (layout & 0x30) >> 4;
        out->layoutDirection = (layout & 0xc0) >> 6;
        out->screenRound = (layout & 0x300) >> 8;
        int32_t ui = wl_int_field(env, config, cls, "uiMode");
        out->uiModeType = ui & 0x0f;
        out->uiModeNight = (ui & 0x30) >> 4;
        out->screenWidthDp = wl_int_field(env, config, cls, "screenWidthDp");
        out->screenHeightDp = wl_int_field(env, config, cls, "screenHeightDp");
        out->smallestScreenWidthDp = wl_int_field(env, config, cls, "smallestScreenWidthDp");
        jfieldID field = (*env)->GetFieldID(env, cls, "locale", "Ljava/util/Locale;");
        jobject locale = field != NULL ? (*env)->GetObjectField(env, config, field) : NULL;
        if (wl_clear(env)) locale = NULL;
        if (locale != NULL) {
            wl_locale_code(env, locale, "getLanguage", out->language);
            wl_locale_code(env, locale, "getCountry", out->country);
            (*env)->DeleteLocalRef(env, locale);
        }
    }
    jclass version = (*env)->FindClass(env, "android/os/Build$VERSION");
    jfieldID sdk = version != NULL ? (*env)->GetStaticFieldID(env, version, "SDK_INT", "I") : NULL;
    out->sdkVersion = sdk != NULL ? (*env)->GetStaticIntField(env, version, sdk) : 0;
    wl_clear(env);
    if (version != NULL) (*env)->DeleteLocalRef(env, version);
    if (cls != NULL) (*env)->DeleteLocalRef(env, cls);
    if (config != NULL) (*env)->DeleteLocalRef(env, config);
    if (resources != NULL) (*env)->DeleteLocalRef(env, resources);
    if (app != NULL) (*env)->DeleteLocalRef(env, app);
    if (thread != NULL) (*env)->DeleteLocalRef(env, thread);
}

AConfiguration *AConfiguration_new(void)
{
    return calloc(1, sizeof(AConfiguration));
}

void AConfiguration_delete(AConfiguration *config)
{
    free(config);
}

void AConfiguration_fromAssetManager(AConfiguration *out, void *am)
{
    (void) am;
    if (out == NULL) return;
    JavaVM *vm = wl_java_vm();
    JNIEnv *env = NULL;
    bool attached = false;
    if (vm == NULL) return;
    if ((*vm)->GetEnv(vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return;
        attached = true;
    }
    memset(out, 0, sizeof(*out));
    wl_read_configuration(env, out);
    if (attached) (*vm)->DetachCurrentThread(vm);
}

void AConfiguration_copy(AConfiguration *dest, AConfiguration *src)
{
    if (dest != NULL && src != NULL) *dest = *src;
}

#define WL_CONFIG_FIELD(Name, field)                                                    \
    int32_t AConfiguration_get##Name(AConfiguration *config) { return config->field; }  \
    void AConfiguration_set##Name(AConfiguration *config, int32_t value) { config->field = value; }

WL_CONFIG_FIELD(Mcc, mcc)
WL_CONFIG_FIELD(Mnc, mnc)
WL_CONFIG_FIELD(Orientation, orientation)
WL_CONFIG_FIELD(Touchscreen, touchscreen)
WL_CONFIG_FIELD(Density, density)
WL_CONFIG_FIELD(Keyboard, keyboard)
WL_CONFIG_FIELD(Navigation, navigation)
WL_CONFIG_FIELD(KeysHidden, keysHidden)
WL_CONFIG_FIELD(NavHidden, navHidden)
WL_CONFIG_FIELD(SdkVersion, sdkVersion)
WL_CONFIG_FIELD(ScreenSize, screenSize)
WL_CONFIG_FIELD(ScreenLong, screenLong)
WL_CONFIG_FIELD(ScreenRound, screenRound)
WL_CONFIG_FIELD(UiModeType, uiModeType)
WL_CONFIG_FIELD(UiModeNight, uiModeNight)
WL_CONFIG_FIELD(ScreenWidthDp, screenWidthDp)
WL_CONFIG_FIELD(ScreenHeightDp, screenHeightDp)
WL_CONFIG_FIELD(SmallestScreenWidthDp, smallestScreenWidthDp)
WL_CONFIG_FIELD(LayoutDirection, layoutDirection)
WL_CONFIG_FIELD(GrammaticalGender, grammaticalGender)

/* Two characters, not terminated; zeros when unset. */
void AConfiguration_getLanguage(AConfiguration *config, char *out)
{
    out[0] = config->language[0];
    out[1] = config->language[1];
}

void AConfiguration_getCountry(AConfiguration *config, char *out)
{
    out[0] = config->country[0];
    out[1] = config->country[1];
}

void AConfiguration_setLanguage(AConfiguration *config, const char *language)
{
    config->language[0] = language != NULL && language[0] ? language[0] : 0;
    config->language[1] = language != NULL && language[0] ? language[1] : 0;
}

void AConfiguration_setCountry(AConfiguration *config, const char *country)
{
    config->country[0] = country != NULL && country[0] ? country[0] : 0;
    config->country[1] = country != NULL && country[0] ? country[1] : 0;
}

int32_t AConfiguration_diff(AConfiguration *a, AConfiguration *b)
{
    int32_t diff = 0;
    if (a->mcc != b->mcc) diff |= DIFF_MCC;
    if (a->mnc != b->mnc) diff |= DIFF_MNC;
    if (memcmp(a->language, b->language, 2) != 0 || memcmp(a->country, b->country, 2) != 0) diff |= DIFF_LOCALE;
    if (a->orientation != b->orientation) diff |= DIFF_ORIENTATION;
    if (a->touchscreen != b->touchscreen) diff |= DIFF_TOUCHSCREEN;
    if (a->density != b->density) diff |= DIFF_DENSITY;
    if (a->keyboard != b->keyboard) diff |= DIFF_KEYBOARD;
    if (a->navigation != b->navigation) diff |= DIFF_NAVIGATION;
    if (a->keysHidden != b->keysHidden || a->navHidden != b->navHidden) diff |= DIFF_KEYBOARD_HIDDEN;
    if (a->sdkVersion != b->sdkVersion) diff |= DIFF_VERSION;
    if (a->screenSize != b->screenSize || a->screenWidthDp != b->screenWidthDp ||
        a->screenHeightDp != b->screenHeightDp) diff |= DIFF_SCREEN_SIZE;
    if (a->screenLong != b->screenLong) diff |= DIFF_SCREEN_LAYOUT;
    if (a->screenRound != b->screenRound) diff |= DIFF_SCREEN_ROUND;
    if (a->uiModeType != b->uiModeType || a->uiModeNight != b->uiModeNight) diff |= DIFF_UI_MODE;
    if (a->smallestScreenWidthDp != b->smallestScreenWidthDp) diff |= DIFF_SMALLEST_SCREEN_SIZE;
    if (a->layoutDirection != b->layoutDirection) diff |= DIFF_LAYOUTDIR;
    if (a->grammaticalGender != b->grammaticalGender) diff |= DIFF_GRAMMATICAL_GENDER;
    return diff;
}

/* Whether base satisfies every field requested sets (a zero field asks for nothing). */
int32_t AConfiguration_match(AConfiguration *base, AConfiguration *requested)
{
#define WL_WANTS(field) (requested->field != 0 && requested->field != base->field)
    if (WL_WANTS(mcc) || WL_WANTS(mnc) || WL_WANTS(orientation) || WL_WANTS(touchscreen) ||
        WL_WANTS(keyboard) || WL_WANTS(navigation) || WL_WANTS(keysHidden) || WL_WANTS(navHidden) ||
        WL_WANTS(screenLong) || WL_WANTS(screenRound) || WL_WANTS(uiModeType) || WL_WANTS(uiModeNight) ||
        WL_WANTS(layoutDirection) || WL_WANTS(grammaticalGender)) return 0;
#undef WL_WANTS
    if (requested->language[0] && memcmp(requested->language, base->language, 2) != 0) return 0;
    if (requested->country[0] && memcmp(requested->country, base->country, 2) != 0) return 0;
    if (requested->sdkVersion && base->sdkVersion > requested->sdkVersion) return 0;
    if (requested->screenSize && base->screenSize > requested->screenSize) return 0;
    if (requested->screenWidthDp && base->screenWidthDp > requested->screenWidthDp) return 0;
    if (requested->screenHeightDp && base->screenHeightDp > requested->screenHeightDp) return 0;
    if (requested->smallestScreenWidthDp && base->smallestScreenWidthDp > requested->smallestScreenWidthDp) return 0;
    return 1;
}

/* No ordering between candidate configurations: neither is better. */
int32_t AConfiguration_isBetterThan(AConfiguration *base, AConfiguration *test, AConfiguration *requested)
{
    (void) base;
    (void) test;
    (void) requested;
    return 0;
}

/* ---- Sensors: a board without them ---------------------------------------------------------- */

typedef struct ASensorManager { int unused; } ASensorManager;
typedef struct ASensorEventQueue { int unused; } ASensorEventQueue;
typedef struct ASensor ASensor;
typedef ASensor const *ASensorRef;
typedef ASensorRef const *ASensorList;

static ASensorManager wl_sensor_manager;
static ASensorEventQueue wl_sensor_queue;
static ASensorRef const wl_no_sensors[1] = { NULL };

ASensorManager *ASensorManager_getInstance(void)
{
    return &wl_sensor_manager;
}

ASensorManager *ASensorManager_getInstanceForPackage(const char *package)
{
    (void) package;
    return &wl_sensor_manager;
}

int ASensorManager_getSensorList(ASensorManager *manager, ASensorList *list)
{
    if (manager == NULL || list == NULL) return -EINVAL;
    *list = wl_no_sensors;
    return 0;
}

ASensor const *ASensorManager_getDefaultSensor(ASensorManager *manager, int type)
{
    (void) manager;
    (void) type;
    return NULL;
}

ASensor const *ASensorManager_getDefaultSensorEx(ASensorManager *manager, int type, bool wakeUp)
{
    (void) manager;
    (void) type;
    (void) wakeUp;
    return NULL;
}

ASensorEventQueue *ASensorManager_createEventQueue(ASensorManager *manager, void *looper, int ident,
                                                   void *callback, void *data)
{
    (void) looper;
    (void) ident;
    (void) callback;
    (void) data;
    return manager != NULL ? &wl_sensor_queue : NULL;
}

int ASensorManager_destroyEventQueue(ASensorManager *manager, ASensorEventQueue *queue)
{
    return manager != NULL && queue != NULL ? 0 : -EINVAL;
}

int ASensorEventQueue_registerSensor(ASensorEventQueue *queue, ASensor const *sensor, int32_t period, int64_t latency)
{
    (void) period;
    (void) latency;
    return queue != NULL && sensor != NULL ? 0 : -EINVAL;
}

int ASensorEventQueue_enableSensor(ASensorEventQueue *queue, ASensor const *sensor)
{
    return queue != NULL && sensor != NULL ? 0 : -EINVAL;
}

int ASensorEventQueue_disableSensor(ASensorEventQueue *queue, ASensor const *sensor)
{
    return queue != NULL && sensor != NULL ? 0 : -EINVAL;
}

int ASensorEventQueue_setEventRate(ASensorEventQueue *queue, ASensor const *sensor, int32_t usec)
{
    (void) usec;
    return queue != NULL && sensor != NULL ? 0 : -EINVAL;
}

int ASensorEventQueue_hasEvents(ASensorEventQueue *queue)
{
    return queue != NULL ? 0 : -EINVAL;
}

ssize_t ASensorEventQueue_getEvents(ASensorEventQueue *queue, void *events, size_t count)
{
    (void) events;
    (void) count;
    return queue != NULL ? 0 : -EINVAL;
}

/* No sensor exists to ask about; these answer for a null one. */
const char *ASensor_getName(ASensor const *sensor) { (void) sensor; return NULL; }
const char *ASensor_getVendor(ASensor const *sensor) { (void) sensor; return NULL; }
const char *ASensor_getStringType(ASensor const *sensor) { (void) sensor; return NULL; }
int ASensor_getType(ASensor const *sensor) { (void) sensor; return -1; /* ASENSOR_TYPE_INVALID */ }
float ASensor_getResolution(ASensor const *sensor) { (void) sensor; return 0.0f; }
int ASensor_getMinDelay(ASensor const *sensor) { (void) sensor; return 0; }
int ASensor_getFifoMaxEventCount(ASensor const *sensor) { (void) sensor; return 0; }
int ASensor_getFifoReservedEventCount(ASensor const *sensor) { (void) sensor; return 0; }
int ASensor_getReportingMode(ASensor const *sensor) { (void) sensor; return -1; }
bool ASensor_isWakeUpSensor(ASensor const *sensor) { (void) sensor; return false; }
int ASensor_getHandle(ASensor const *sensor) { (void) sensor; return -1; }

/* ---- Input: no input queue reaches an app --------------------------------------------------- */

/*
 * AOSP hands an AInputQueue only to a NativeActivity (onInputQueueCreated), and its events come from
 * that queue or from AKeyEvent_fromJava/AMotionEvent_fromJava. The runtime has no NativeActivity
 * natives, and the fromJava calls are not defined here, so no queue or event can reach an app: the
 * queue calls answer as an empty queue, and the event getters, which have no event to read, zero.
 * They exist for libraries that link the NDK's input API without using it: Go's mobile runtime links
 * its NativeActivity glue into every library (aaaaxy's libgojni.so, which loads it from an ordinary
 * activity), and could not relocate without them.
 */
typedef struct AInputEvent AInputEvent;
typedef struct AInputQueue AInputQueue;
typedef int (*ALooper_callbackFunc)(int fd, int events, void *data);

void AInputQueue_attachLooper(AInputQueue *queue, void *looper, int ident, ALooper_callbackFunc callback, void *data)
{
    (void) queue;
    (void) looper;
    (void) ident;
    (void) callback;
    (void) data;
}

void AInputQueue_detachLooper(AInputQueue *queue) { (void) queue; }
int32_t AInputQueue_hasEvents(AInputQueue *queue) { return queue != NULL ? 0 : -EINVAL; }

int32_t AInputQueue_getEvent(AInputQueue *queue, AInputEvent **outEvent)
{
    (void) queue;
    if (outEvent != NULL) *outEvent = NULL;
    return -EAGAIN; /* InputQueue::getEvent's WOULD_BLOCK */
}

int32_t AInputQueue_preDispatchEvent(AInputQueue *queue, AInputEvent *event)
{
    (void) queue;
    (void) event;
    return 0;
}

void AInputQueue_finishEvent(AInputQueue *queue, AInputEvent *event, int handled)
{
    (void) queue;
    (void) event;
    (void) handled;
}

#define WL_EVENT_GETTER(type, name) \
    type name(const AInputEvent *event) { (void) event; return 0; }
#define WL_POINTER_GETTER(type, name) \
    type name(const AInputEvent *event, size_t pointer) { (void) event; (void) pointer; return 0; }
#define WL_HISTORICAL_GETTER(type, name) \
    type name(const AInputEvent *event, size_t pointer, size_t history) \
    { (void) event; (void) pointer; (void) history; return 0; }

WL_EVENT_GETTER(int32_t, AInputEvent_getType)
WL_EVENT_GETTER(int32_t, AInputEvent_getDeviceId)
WL_EVENT_GETTER(int32_t, AInputEvent_getSource)
void AInputEvent_release(const AInputEvent *event) { (void) event; }

WL_EVENT_GETTER(int32_t, AKeyEvent_getAction)
WL_EVENT_GETTER(int32_t, AKeyEvent_getFlags)
WL_EVENT_GETTER(int32_t, AKeyEvent_getKeyCode)
WL_EVENT_GETTER(int32_t, AKeyEvent_getScanCode)
WL_EVENT_GETTER(int32_t, AKeyEvent_getMetaState)
WL_EVENT_GETTER(int32_t, AKeyEvent_getRepeatCount)
WL_EVENT_GETTER(int64_t, AKeyEvent_getDownTime)
WL_EVENT_GETTER(int64_t, AKeyEvent_getEventTime)

WL_EVENT_GETTER(int32_t, AMotionEvent_getAction)
WL_EVENT_GETTER(int32_t, AMotionEvent_getActionButton)
WL_EVENT_GETTER(int32_t, AMotionEvent_getButtonState)
WL_EVENT_GETTER(int32_t, AMotionEvent_getClassification)
WL_EVENT_GETTER(int32_t, AMotionEvent_getEdgeFlags)
WL_EVENT_GETTER(int32_t, AMotionEvent_getFlags)
WL_EVENT_GETTER(int32_t, AMotionEvent_getMetaState)
WL_EVENT_GETTER(int64_t, AMotionEvent_getDownTime)
WL_EVENT_GETTER(int64_t, AMotionEvent_getEventTime)
WL_EVENT_GETTER(float, AMotionEvent_getXOffset)
WL_EVENT_GETTER(float, AMotionEvent_getYOffset)
WL_EVENT_GETTER(float, AMotionEvent_getXPrecision)
WL_EVENT_GETTER(float, AMotionEvent_getYPrecision)
WL_EVENT_GETTER(size_t, AMotionEvent_getPointerCount)
WL_EVENT_GETTER(size_t, AMotionEvent_getHistorySize)
WL_POINTER_GETTER(int32_t, AMotionEvent_getPointerId)
WL_POINTER_GETTER(int32_t, AMotionEvent_getToolType)
WL_POINTER_GETTER(float, AMotionEvent_getRawX)
WL_POINTER_GETTER(float, AMotionEvent_getRawY)
WL_POINTER_GETTER(float, AMotionEvent_getX)
WL_POINTER_GETTER(float, AMotionEvent_getY)
WL_POINTER_GETTER(float, AMotionEvent_getPressure)
WL_POINTER_GETTER(float, AMotionEvent_getSize)
WL_POINTER_GETTER(float, AMotionEvent_getTouchMajor)
WL_POINTER_GETTER(float, AMotionEvent_getTouchMinor)
WL_POINTER_GETTER(float, AMotionEvent_getToolMajor)
WL_POINTER_GETTER(float, AMotionEvent_getToolMinor)
WL_POINTER_GETTER(float, AMotionEvent_getOrientation)
WL_POINTER_GETTER(int64_t, AMotionEvent_getHistoricalEventTime) /* (event, history) */
WL_HISTORICAL_GETTER(float, AMotionEvent_getHistoricalRawX)
WL_HISTORICAL_GETTER(float, AMotionEvent_getHistoricalRawY)
WL_HISTORICAL_GETTER(float, AMotionEvent_getHistoricalX)
WL_HISTORICAL_GETTER(float, AMotionEvent_getHistoricalY)
WL_HISTORICAL_GETTER(float, AMotionEvent_getHistoricalPressure)
WL_HISTORICAL_GETTER(float, AMotionEvent_getHistoricalSize)
WL_HISTORICAL_GETTER(float, AMotionEvent_getHistoricalTouchMajor)
WL_HISTORICAL_GETTER(float, AMotionEvent_getHistoricalTouchMinor)
WL_HISTORICAL_GETTER(float, AMotionEvent_getHistoricalToolMajor)
WL_HISTORICAL_GETTER(float, AMotionEvent_getHistoricalToolMinor)
WL_HISTORICAL_GETTER(float, AMotionEvent_getHistoricalOrientation)

float AMotionEvent_getAxisValue(const AInputEvent *event, int32_t axis, size_t pointer)
{
    (void) event;
    (void) axis;
    (void) pointer;
    return 0;
}

float AMotionEvent_getHistoricalAxisValue(const AInputEvent *event, int32_t axis, size_t pointer, size_t history)
{
    (void) event;
    (void) axis;
    (void) pointer;
    (void) history;
    return 0;
}
