/*
 * android.app.NativeActivity and android.view.InputQueue: natives the staged runtime declares and
 * never registers (AOSP: frameworks/base/core/jni/android_app_NativeActivity.cpp and
 * android_view_InputQueue.cpp, in libandroid_runtime). Without them a NativeActivity app stopped in
 * NativeActivity.onCreate with "No implementation found for long loadNativeCode(...)" (eFootball,
 * Identity V, Eggy Party, Clip Studio Paint).
 *
 * loadNativeCode opens the app's library through the runtime's own loader (libart's
 * OpenNativeLibrary, which System.loadLibrary uses, so the library lands in the namespace it would
 * have), looks up its entry point (ANativeActivity_onCreate unless the manifest names another) and
 * calls it with an ANativeActivity laid out as <android/native_activity.h> declares it, as AOSP's
 * NativeCode does. The lifecycle, window and input-queue natives call the callbacks the app filled
 * in. The window is the Surface's, from ANativeWindow_fromSurface of the libraries the app's own
 * library uses.
 *
 * Work for the main thread goes through a pipe the main thread's looper watches (ALooper_addFd), as
 * AOSP's mainWorkRead pipe does: ANativeActivity_finish and the window and soft-input requests an
 * app makes from its own threads (the bionic shim forwards them here), and the finish of input events.
 *
 * Input: no native input queue exists yet, so each event the window's InputQueue sends is finished
 * unhandled from the main thread, and the window's input pipeline goes on to its views.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COUNT(t) ((int) (sizeof(t) / sizeof((t)[0])))

static void nalog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void nalog(const char* fmt, ...)
{
    char line[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int) sizeof(line) - 2) n = (int) sizeof(line) - 2;
    line[n++] = '\n';
    (void) write(2, line, (size_t) n);
}

/* <android/native_activity.h> and <android/rect.h>, as the NDK declares them. */
typedef struct ANativeActivityCallbacks ANativeActivityCallbacks;
typedef struct ANativeActivity {
    ANativeActivityCallbacks* callbacks;
    JavaVM* vm;
    JNIEnv* env;
    jobject clazz;
    const char* internalDataPath;
    const char* externalDataPath;
    int32_t sdkVersion;
    void* instance;
    void* assetManager;
    const char* obbPath;
} ANativeActivity;

typedef struct { int32_t left, top, right, bottom; } WlRect;

struct ANativeActivityCallbacks {
    void (*onStart)(ANativeActivity*);
    void (*onResume)(ANativeActivity*);
    void* (*onSaveInstanceState)(ANativeActivity*, size_t*);
    void (*onPause)(ANativeActivity*);
    void (*onStop)(ANativeActivity*);
    void (*onDestroy)(ANativeActivity*);
    void (*onWindowFocusChanged)(ANativeActivity*, int);
    void (*onNativeWindowCreated)(ANativeActivity*, void*);
    void (*onNativeWindowResized)(ANativeActivity*, void*);
    void (*onNativeWindowRedrawNeeded)(ANativeActivity*, void*);
    void (*onNativeWindowDestroyed)(ANativeActivity*, void*);
    void (*onInputQueueCreated)(ANativeActivity*, void*);
    void (*onInputQueueDestroyed)(ANativeActivity*, void*);
    void (*onContentRectChanged)(ANativeActivity*, const WlRect*);
    void (*onConfigurationChanged)(ANativeActivity*);
    void (*onLowMemory)(ANativeActivity*);
};

typedef void (*CreateFn)(ANativeActivity*, void*, size_t);
typedef void* (*WindowFromSurfaceFn)(JNIEnv*, jobject);
typedef void (*WindowReleaseFn)(void*);
typedef int32_t (*WindowSizeFn)(void*);
typedef void* (*AssetManagerFromJavaFn)(JNIEnv*, jobject);
typedef void* (*OpenNativeLibraryFn)(JNIEnv*, int, const char*, jobject, const char*, jstring, bool*, char**);
typedef int (*LooperCallbackFn)(int, int, void*);
typedef void* (*LooperForThreadFn)(void);
typedef int (*LooperAddFdFn)(void*, int, int, int, LooperCallbackFn, void*);
typedef void (*CommandHookFn)(void*, int32_t, int32_t, int32_t);
typedef void (*SetCommandHookFn)(CommandHookFn);

typedef struct WlNativeCode {
    ANativeActivity activity;   /* first: an ANativeActivity* is its WlNativeCode*, as in AOSP */
    ANativeActivityCallbacks callbacks;
    void* library;
    void* window;
    jobject surface;            /* global reference to the Surface the window came from */
    jlong surface_object;       /* that Surface's mNativeObject when the window was taken */
    int32_t last_width, last_height;
    WindowFromSurfaceFn window_from_surface;
    WindowReleaseFn window_release;
    WindowSizeFn window_width, window_height;
    char* internal_path;
    char* external_path;
    char* obb_path;
} WlNativeCode;

typedef struct WlInputQueue {
    jobject weak;               /* global reference to the WeakReference<InputQueue> */
    int64_t next_id;
} WlInputQueue;

/* ---- Main-thread work ------------------------------------------------------------------------ */

enum { WORK_FINISH_EVENT = 1, WORK_ACTIVITY_COMMAND = 2 };
/* AOSP's command numbers (android_app_NativeActivity.cpp). */
enum { CMD_FINISH = 1, CMD_SET_WINDOW_FORMAT, CMD_SET_WINDOW_FLAGS, CMD_SHOW_SOFT_INPUT, CMD_HIDE_SOFT_INPUT };

typedef struct {
    int32_t kind;
    int32_t command;
    int64_t a;
    int64_t b;
    void* target;
} WlMainWork;

static JavaVM* g_vm;
static int g_work_write = -1;
static jmethodID g_finish, g_set_window_flags, g_set_window_format, g_show_ime, g_hide_ime;
static jmethodID g_finish_input_event, g_weak_get;
static jfieldID g_surface_native_object;
static char* g_dl_error;

/* The objects work may name: one posted for an activity or queue that is gone by then is dropped. */
#define WL_LIVE_MAX 64
static pthread_mutex_t g_live_lock = PTHREAD_MUTEX_INITIALIZER;
static void* g_live[WL_LIVE_MAX];

static void live_set(void* object, bool live)
{
    int i;
    pthread_mutex_lock(&g_live_lock);
    for (i = 0; i < WL_LIVE_MAX; i++) {
        if (live ? g_live[i] == NULL : g_live[i] == object) {
            g_live[i] = live ? object : NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_live_lock);
}

static bool live_has(void* object)
{
    bool found = false;
    int i;
    pthread_mutex_lock(&g_live_lock);
    for (i = 0; i < WL_LIVE_MAX && !found; i++) found = object != NULL && g_live[i] == object;
    pthread_mutex_unlock(&g_live_lock);
    return found;
}

static void clear_exception(JNIEnv* env, const char* what)
{
    if (!(*env)->ExceptionCheck(env)) return;
    nalog("[WL-NATIVEACTIVITY] %s threw", what);
    (*env)->ExceptionDescribe(env);
    (*env)->ExceptionClear(env);
}

static int main_work(int fd, int events, void* data)
{
    static bool announced;
    WlMainWork work;
    JNIEnv* env = NULL;
    (void) events;
    (void) data;
    if (g_vm == NULL || (*g_vm)->GetEnv(g_vm, (void**) &env, JNI_VERSION_1_6) != JNI_OK || env == NULL) return 1;
    if (!announced) {
        announced = true;
        nalog("[WL-NATIVEACTIVITY] main-thread work runs from the main looper");
    }
    while (read(fd, &work, sizeof(work)) == (ssize_t) sizeof(work)) {
        if (!live_has(work.target)) continue;
        if (work.kind == WORK_FINISH_EVENT) {
            WlInputQueue* queue = work.target;
            jobject owner = (*env)->CallObjectMethod(env, queue->weak, g_weak_get);
            clear_exception(env, "WeakReference.get");
            if (owner != NULL) {
                (*env)->CallVoidMethod(env, owner, g_finish_input_event, (jlong) work.a, (jboolean) (work.b != 0));
                clear_exception(env, "InputQueue.finishInputEvent");
                (*env)->DeleteLocalRef(env, owner);
            }
        } else if (work.kind == WORK_ACTIVITY_COMMAND) {
            jobject activity = ((WlNativeCode*) work.target)->activity.clazz;
            switch (work.command) {
            case CMD_FINISH:
                (*env)->CallVoidMethod(env, activity, g_finish);
                break;
            case CMD_SET_WINDOW_FORMAT:
                (*env)->CallVoidMethod(env, activity, g_set_window_format, (jint) work.a);
                break;
            case CMD_SET_WINDOW_FLAGS:
                (*env)->CallVoidMethod(env, activity, g_set_window_flags, (jint) work.a, (jint) work.b);
                break;
            case CMD_SHOW_SOFT_INPUT:
                (*env)->CallVoidMethod(env, activity, g_show_ime, (jint) work.a);
                break;
            case CMD_HIDE_SOFT_INPUT:
                (*env)->CallVoidMethod(env, activity, g_hide_ime, (jint) work.a);
                break;
            default:
                break;
            }
            clear_exception(env, "NativeActivity command");
        }
    }
    return 1;
}

/* The pipe and its place on the main looper, made once, on the main thread (both callers run there). */
static bool main_work_ready(void)
{
    static bool tried, ready;
    int fds[2];
    if (tried) return ready;
    tried = true;
    LooperForThreadFn for_thread = (LooperForThreadFn) dlsym(RTLD_DEFAULT, "ALooper_forThread");
    LooperAddFdFn add_fd = (LooperAddFdFn) dlsym(RTLD_DEFAULT, "ALooper_addFd");
    void* looper = for_thread != NULL ? for_thread() : NULL;
    if (looper == NULL || add_fd == NULL || pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
        nalog("[WL-NATIVEACTIVITY] no main looper to post work to (looper %p, addFd %p)", looper, (void*) add_fd);
        return false;
    }
    if (add_fd(looper, fds[0], -2 /* ALOOPER_POLL_CALLBACK */, 1 /* ALOOPER_EVENT_INPUT */, main_work, NULL) != 1) {
        nalog("[WL-NATIVEACTIVITY] the main looper refused the work pipe");
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    g_work_write = fds[1];
    ready = true;
    return true;
}

static void post_work(int32_t kind, int32_t command, void* target, int64_t a, int64_t b)
{
    WlMainWork work = { kind, command, a, b, target };
    if (g_work_write < 0 || write(g_work_write, &work, sizeof(work)) != (ssize_t) sizeof(work)) {
        static bool logged;
        if (!logged) {
            logged = true;
            nalog("[WL-NATIVEACTIVITY] main-thread work dropped (errno %d)", g_work_write < 0 ? 0 : errno);
        }
    }
}

/* ANativeActivity_finish and its siblings, from any thread, through the bionic shim. */
static void activity_command(void* activity, int32_t command, int32_t a, int32_t b)
{
    post_work(WORK_ACTIVITY_COMMAND, command, activity, a, b);
}

/* ---- NativeActivity ---------------------------------------------------------------------------- */

static char* dup_string(JNIEnv* env, jstring value)
{
    const char* chars;
    char* copy;
    if (value == NULL) return NULL;
    chars = (*env)->GetStringUTFChars(env, value, NULL);
    copy = chars != NULL ? strdup(chars) : NULL;
    if (chars != NULL) (*env)->ReleaseStringUTFChars(env, value, chars);
    return copy;
}

/* A name from the libraries the app's library uses, else from any global library. */
static void* app_symbol(void* library, const char* name)
{
    void* symbol = library != NULL ? dlsym(library, name) : NULL;
    if (symbol == NULL) symbol = dlsym(RTLD_DEFAULT, name);
    if (symbol == NULL) nalog("[WL-NATIVEACTIVITY] %s not found", name);
    return symbol;
}

static void set_dl_error(const char* message)
{
    free(g_dl_error);
    g_dl_error = message != NULL ? strdup(message) : NULL;
}

static jlong loadNativeCode(JNIEnv* env, jobject thiz, jstring path, jstring function, jobject message_queue,
                            jstring internal_dir, jstring obb_dir, jstring external_dir, jint sdk_version,
                            jobject assets, jbyteArray saved_state, jobject class_loader, jstring library_path)
{
    (void) message_queue;
    char* library_name = dup_string(env, path);
    char* function_name = dup_string(env, function);
    OpenNativeLibraryFn open_library = (OpenNativeLibraryFn) dlsym(RTLD_DEFAULT, "OpenNativeLibrary");
    bool needs_bridge = false;
    char* error = NULL;
    void* library = NULL;
    CreateFn create;
    WlNativeCode* code;
    if (library_name == NULL || function_name == NULL) goto fail;
    library = open_library != NULL
            ? open_library(env, sdk_version, library_name, class_loader, NULL, library_path, &needs_bridge, &error)
            : dlopen(library_name, RTLD_NOW);
    if (library == NULL) {
        set_dl_error(error != NULL ? error : dlerror());
        nalog("[WL-NATIVEACTIVITY] %s did not load: %s", library_name, g_dl_error != NULL ? g_dl_error : "?");
        free(error);
        goto fail;
    }
    free(error);
    create = (CreateFn) dlsym(library, function_name);
    if (create == NULL) {
        set_dl_error(dlerror());
        nalog("[WL-NATIVEACTIVITY] %s has no %s", library_name, function_name);
        goto fail;
    }
    code = calloc(1, sizeof(*code));
    if (code == NULL) goto fail;
    if (!main_work_ready()) nalog("[WL-NATIVEACTIVITY] finish and window requests will not reach the activity");
    code->library = library;
    code->activity.callbacks = &code->callbacks;
    (*env)->GetJavaVM(env, &code->activity.vm);
    code->activity.env = env;
    code->activity.clazz = (*env)->NewGlobalRef(env, thiz);
    code->internal_path = dup_string(env, internal_dir);
    code->external_path = dup_string(env, external_dir);
    code->obb_path = dup_string(env, obb_dir);
    code->activity.internalDataPath = code->internal_path;
    code->activity.externalDataPath = code->external_path;
    code->activity.obbPath = code->obb_path;
    code->activity.sdkVersion = sdk_version;
    {
        AssetManagerFromJavaFn from_java = (AssetManagerFromJavaFn) app_symbol(library, "AAssetManager_fromJava");
        code->activity.assetManager = from_java != NULL && assets != NULL ? from_java(env, assets) : NULL;
    }
    code->window_from_surface = (WindowFromSurfaceFn) app_symbol(library, "ANativeWindow_fromSurface");
    code->window_release = (WindowReleaseFn) app_symbol(library, "ANativeWindow_release");
    code->window_width = (WindowSizeFn) app_symbol(library, "ANativeWindow_getWidth");
    code->window_height = (WindowSizeFn) app_symbol(library, "ANativeWindow_getHeight");
    live_set(code, true);
    {
        jbyte* state = NULL;
        jsize size = 0;
        void* copy = NULL;
        if (saved_state != NULL) {
            size = (*env)->GetArrayLength(env, saved_state);
            state = (*env)->GetByteArrayElements(env, saved_state, NULL);
            copy = state != NULL && size > 0 ? malloc((size_t) size) : NULL;
            if (copy != NULL) memcpy(copy, state, (size_t) size);
            if (state != NULL) (*env)->ReleaseByteArrayElements(env, saved_state, state, JNI_ABORT);
        }
        nalog("[WL-NATIVEACTIVITY] %s (opened by %s): %s(saved state %d bytes)", library_name,
              open_library != NULL ? "the runtime's loader" : "dlopen", function_name, copy != NULL ? (int) size : 0);
        create(&code->activity, copy, copy != NULL ? (size_t) size : 0);
        free(copy);
    }
    free(library_name);
    free(function_name);
    return (jlong) (intptr_t) code;
fail:
    free(library_name);
    free(function_name);
    return 0;
}

static jstring getDlError(JNIEnv* env, jobject thiz)
{
    (void) thiz;
    return (*env)->NewStringUTF(env, g_dl_error != NULL ? g_dl_error : "");
}

#define CODE(handle) ((WlNativeCode*) (intptr_t) (handle))

static void release_window(JNIEnv* env, WlNativeCode* code)
{
    if (code->window != NULL && code->window_release != NULL) code->window_release(code->window);
    code->window = NULL;
    if (code->surface != NULL) (*env)->DeleteGlobalRef(env, code->surface);
    code->surface = NULL;
    code->surface_object = 0;
}

/* Whether the Surface is the one the window came from, still over the same native surface. */
static bool same_surface(JNIEnv* env, WlNativeCode* code, jobject surface)
{
    return surface != NULL && code->window != NULL && code->surface != NULL &&
           (*env)->IsSameObject(env, surface, code->surface) &&
           (g_surface_native_object == NULL ||
            (*env)->GetLongField(env, surface, g_surface_native_object) == code->surface_object);
}

/* The Surface's window, kept while the Surface is the same; otherwise the old one is released. */
static void take_window(JNIEnv* env, WlNativeCode* code, jobject surface)
{
    if (same_surface(env, code, surface)) return;
    release_window(env, code);
    if (surface == NULL || code->window_from_surface == NULL) return;
    code->window = code->window_from_surface(env, surface);
    if (code->window == NULL) return;
    code->surface = (*env)->NewGlobalRef(env, surface);
    code->surface_object = g_surface_native_object != NULL ? (*env)->GetLongField(env, surface, g_surface_native_object) : 0;
}

static int32_t window_width(WlNativeCode* code)
{
    return code->window != NULL && code->window_width != NULL ? code->window_width(code->window) : 0;
}

static int32_t window_height(WlNativeCode* code)
{
    return code->window != NULL && code->window_height != NULL ? code->window_height(code->window) : 0;
}

static void unloadNativeCode(JNIEnv* env, jobject thiz, jlong handle)
{
    WlNativeCode* code = CODE(handle);
    (void) thiz;
    if (code == NULL) return;
    live_set(code, false);
    if (code->callbacks.onDestroy != NULL) code->callbacks.onDestroy(&code->activity);
    release_window(env, code);
    if (code->activity.clazz != NULL) (*env)->DeleteGlobalRef(env, code->activity.clazz);
    free(code->internal_path);
    free(code->external_path);
    free(code->obb_path);
    /* The library stays loaded, as AOSP leaves it (no dlclose). */
    free(code);
}

#define LIFECYCLE(name, callback)                                       \
    static void name(JNIEnv* env, jobject thiz, jlong handle)           \
    {                                                                   \
        WlNativeCode* code = CODE(handle);                              \
        (void) env;                                                     \
        (void) thiz;                                                    \
        if (code != NULL && code->callbacks.callback != NULL) code->callbacks.callback(&code->activity); \
    }

LIFECYCLE(onStartNative, onStart)
LIFECYCLE(onResumeNative, onResume)
LIFECYCLE(onPauseNative, onPause)
LIFECYCLE(onStopNative, onStop)
LIFECYCLE(onConfigurationChangedNative, onConfigurationChanged)
LIFECYCLE(onLowMemoryNative, onLowMemory)

static jbyteArray onSaveInstanceStateNative(JNIEnv* env, jobject thiz, jlong handle)
{
    WlNativeCode* code = CODE(handle);
    jbyteArray array = NULL;
    size_t size = 0;
    void* state;
    (void) thiz;
    if (code == NULL || code->callbacks.onSaveInstanceState == NULL) return NULL;
    state = code->callbacks.onSaveInstanceState(&code->activity, &size);
    if (state != NULL && size > 0) {
        array = (*env)->NewByteArray(env, (jsize) size);
        if (array != NULL) (*env)->SetByteArrayRegion(env, array, 0, (jsize) size, (const jbyte*) state);
    }
    free(state);
    return array;
}

static void onWindowFocusChangedNative(JNIEnv* env, jobject thiz, jlong handle, jboolean focused)
{
    WlNativeCode* code = CODE(handle);
    (void) env;
    (void) thiz;
    if (code != NULL && code->callbacks.onWindowFocusChanged != NULL) {
        code->callbacks.onWindowFocusChanged(&code->activity, focused ? 1 : 0);
    }
}

static void onSurfaceCreatedNative(JNIEnv* env, jobject thiz, jlong handle, jobject surface)
{
    WlNativeCode* code = CODE(handle);
    (void) thiz;
    if (code == NULL) return;
    take_window(env, code, surface);
    if (code->window == NULL) {
        nalog("[WL-NATIVEACTIVITY] no window from the activity's Surface");
        return;
    }
    code->last_width = window_width(code);
    code->last_height = window_height(code);
    nalog("[WL-NATIVEACTIVITY] window %p (%dx%d) created", code->window, code->last_width, code->last_height);
    if (code->callbacks.onNativeWindowCreated != NULL) code->callbacks.onNativeWindowCreated(&code->activity, code->window);
}

/* AOSP: the same window may have been resized; another is a destroy and a create, the destroy told
 * while the old window is still held. */
static void onSurfaceChangedNative(JNIEnv* env, jobject thiz, jlong handle, jobject surface, jint format, jint width,
                                   jint height)
{
    WlNativeCode* code = CODE(handle);
    (void) thiz;
    (void) format;
    (void) width;
    (void) height;
    if (code == NULL) return;
    if (same_surface(env, code, surface)) {
        int32_t new_width = window_width(code), new_height = window_height(code);
        if (new_width != code->last_width || new_height != code->last_height) {
            code->last_width = new_width;
            code->last_height = new_height;
            if (code->callbacks.onNativeWindowResized != NULL) {
                code->callbacks.onNativeWindowResized(&code->activity, code->window);
            }
        }
        return;
    }
    if (code->window != NULL && code->callbacks.onNativeWindowDestroyed != NULL) {
        code->callbacks.onNativeWindowDestroyed(&code->activity, code->window);
    }
    take_window(env, code, surface);
    if (code->window == NULL) return;
    code->last_width = window_width(code);
    code->last_height = window_height(code);
    if (code->callbacks.onNativeWindowCreated != NULL) code->callbacks.onNativeWindowCreated(&code->activity, code->window);
}

static void onSurfaceRedrawNeededNative(JNIEnv* env, jobject thiz, jlong handle, jobject surface)
{
    WlNativeCode* code = CODE(handle);
    (void) env;
    (void) thiz;
    (void) surface;
    if (code != NULL && code->window != NULL && code->callbacks.onNativeWindowRedrawNeeded != NULL) {
        code->callbacks.onNativeWindowRedrawNeeded(&code->activity, code->window);
    }
}

static void onSurfaceDestroyedNative(JNIEnv* env, jobject thiz, jlong handle)
{
    WlNativeCode* code = CODE(handle);
    (void) thiz;
    if (code == NULL) return;
    if (code->window != NULL && code->callbacks.onNativeWindowDestroyed != NULL) {
        code->callbacks.onNativeWindowDestroyed(&code->activity, code->window);
    }
    release_window(env, code);
}

static void onInputQueueCreatedNative(JNIEnv* env, jobject thiz, jlong handle, jlong queue)
{
    WlNativeCode* code = CODE(handle);
    (void) env;
    (void) thiz;
    if (code != NULL && code->callbacks.onInputQueueCreated != NULL) {
        code->callbacks.onInputQueueCreated(&code->activity, (void*) (intptr_t) queue);
    }
}

static void onInputQueueDestroyedNative(JNIEnv* env, jobject thiz, jlong handle, jlong queue)
{
    WlNativeCode* code = CODE(handle);
    (void) env;
    (void) thiz;
    if (code != NULL && code->callbacks.onInputQueueDestroyed != NULL) {
        code->callbacks.onInputQueueDestroyed(&code->activity, (void*) (intptr_t) queue);
    }
}

static void onContentRectChangedNative(JNIEnv* env, jobject thiz, jlong handle, jint x, jint y, jint w, jint h)
{
    WlNativeCode* code = CODE(handle);
    WlRect rect = { x, y, x + w, y + h };
    (void) env;
    (void) thiz;
    if (code != NULL && code->callbacks.onContentRectChanged != NULL) {
        code->callbacks.onContentRectChanged(&code->activity, &rect);
    }
}

static const JNINativeMethod kNativeActivity[] = {
    { "loadNativeCode",
      "(Ljava/lang/String;Ljava/lang/String;Landroid/os/MessageQueue;Ljava/lang/String;Ljava/lang/String;"
      "Ljava/lang/String;ILandroid/content/res/AssetManager;[BLjava/lang/ClassLoader;Ljava/lang/String;)J",
      (void*) loadNativeCode },
    { "getDlError", "()Ljava/lang/String;", (void*) getDlError },
    { "unloadNativeCode", "(J)V", (void*) unloadNativeCode },
    { "onStartNative", "(J)V", (void*) onStartNative },
    { "onResumeNative", "(J)V", (void*) onResumeNative },
    { "onSaveInstanceStateNative", "(J)[B", (void*) onSaveInstanceStateNative },
    { "onPauseNative", "(J)V", (void*) onPauseNative },
    { "onStopNative", "(J)V", (void*) onStopNative },
    { "onConfigurationChangedNative", "(J)V", (void*) onConfigurationChangedNative },
    { "onLowMemoryNative", "(J)V", (void*) onLowMemoryNative },
    { "onWindowFocusChangedNative", "(JZ)V", (void*) onWindowFocusChangedNative },
    { "onSurfaceCreatedNative", "(JLandroid/view/Surface;)V", (void*) onSurfaceCreatedNative },
    { "onSurfaceChangedNative", "(JLandroid/view/Surface;III)V", (void*) onSurfaceChangedNative },
    { "onSurfaceRedrawNeededNative", "(JLandroid/view/Surface;)V", (void*) onSurfaceRedrawNeededNative },
    { "onSurfaceDestroyedNative", "(J)V", (void*) onSurfaceDestroyedNative },
    { "onInputQueueCreatedNative", "(JJ)V", (void*) onInputQueueCreatedNative },
    { "onInputQueueDestroyedNative", "(JJ)V", (void*) onInputQueueDestroyedNative },
    { "onContentRectChangedNative", "(JIIII)V", (void*) onContentRectChangedNative },
};

/* ---- InputQueue -------------------------------------------------------------------------------- */

static jlong nativeInit(JNIEnv* env, jclass clazz, jobject weak_queue, jobject message_queue)
{
    WlInputQueue* queue = calloc(1, sizeof(*queue));
    (void) clazz;
    (void) message_queue;
    if (queue == NULL) return 0;
    queue->weak = (*env)->NewGlobalRef(env, weak_queue);
    queue->next_id = 1;
    if (!main_work_ready()) nalog("[WL-NATIVEACTIVITY] input events cannot be finished: no main looper");
    live_set(queue, true);
    return (jlong) (intptr_t) queue;
}

static void nativeDispose(JNIEnv* env, jclass clazz, jlong ptr)
{
    WlInputQueue* queue = (WlInputQueue*) (intptr_t) ptr;
    (void) clazz;
    if (queue == NULL) return;
    live_set(queue, false);
    if (queue->weak != NULL) (*env)->DeleteGlobalRef(env, queue->weak);
    free(queue);
}

/* The event's id, finished unhandled from the main thread once sendInputEvent has recorded it. */
static jlong send_event(WlInputQueue* queue)
{
    jlong id;
    if (queue == NULL) return 0;
    id = (jlong) queue->next_id++;
    post_work(WORK_FINISH_EVENT, 0, queue, id, 0);
    return id;
}

static jlong nativeSendKeyEvent(JNIEnv* env, jclass clazz, jlong ptr, jobject event, jboolean predispatch)
{
    (void) env;
    (void) clazz;
    (void) event;
    (void) predispatch;
    return send_event((WlInputQueue*) (intptr_t) ptr);
}

static jlong nativeSendMotionEvent(JNIEnv* env, jclass clazz, jlong ptr, jobject event)
{
    (void) env;
    (void) clazz;
    (void) event;
    return send_event((WlInputQueue*) (intptr_t) ptr);
}

static const JNINativeMethod kInputQueue[] = {
    { "nativeInit", "(Ljava/lang/ref/WeakReference;Landroid/os/MessageQueue;)J", (void*) nativeInit },
    { "nativeSendKeyEvent", "(JLandroid/view/KeyEvent;Z)J", (void*) nativeSendKeyEvent },
    { "nativeSendMotionEvent", "(JLandroid/view/MotionEvent;)J", (void*) nativeSendMotionEvent },
    { "nativeDispose", "(J)V", (void*) nativeDispose },
};

int wl_register_native_activity(JNIEnv* env)
{
    jclass activity, input_queue, weak, surface;
    int n = 0;
    (*env)->GetJavaVM(env, &g_vm);
    activity = (*env)->FindClass(env, "android/app/NativeActivity");
    if (activity != NULL) {
        g_finish = (*env)->GetMethodID(env, activity, "finish", "()V");
        g_set_window_flags = (*env)->GetMethodID(env, activity, "setWindowFlags", "(II)V");
        g_set_window_format = (*env)->GetMethodID(env, activity, "setWindowFormat", "(I)V");
        g_show_ime = (*env)->GetMethodID(env, activity, "showIme", "(I)V");
        g_hide_ime = (*env)->GetMethodID(env, activity, "hideIme", "(I)V");
    }
    if (activity != NULL && g_finish != NULL && g_set_window_flags != NULL && g_set_window_format != NULL &&
        g_show_ime != NULL && g_hide_ime != NULL &&
        (*env)->RegisterNatives(env, activity, kNativeActivity, COUNT(kNativeActivity)) == 0) {
        n += COUNT(kNativeActivity);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    input_queue = (*env)->FindClass(env, "android/view/InputQueue");
    weak = (*env)->FindClass(env, "java/lang/ref/Reference");
    if (input_queue != NULL && weak != NULL) {
        g_finish_input_event = (*env)->GetMethodID(env, input_queue, "finishInputEvent", "(JZ)V");
        g_weak_get = (*env)->GetMethodID(env, weak, "get", "()Ljava/lang/Object;");
    }
    if (g_finish_input_event != NULL && g_weak_get != NULL &&
        (*env)->RegisterNatives(env, input_queue, kInputQueue, COUNT(kInputQueue)) == 0) {
        n += COUNT(kInputQueue);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    surface = (*env)->FindClass(env, "android/view/Surface");
    g_surface_native_object = surface != NULL ? (*env)->GetFieldID(env, surface, "mNativeObject", "J") : NULL;
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    {
        SetCommandHookFn set_hook = (SetCommandHookFn) dlsym(RTLD_DEFAULT, "wl_native_activity_set_command_hook");
        if (set_hook != NULL) set_hook(activity_command);
    }
    return n;
}
