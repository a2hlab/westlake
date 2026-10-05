/*
 * android.view.Surface.nativeLockCanvas / nativeUnlockCanvasAndPost -- wrapped, not replaced.
 *
 * liboh_android_runtime.so (which cannot be rebuilt here) registers a lockCanvas that hands back
 * the surface with no buffer behind the Canvas, and an unlockCanvasAndPost that does nothing: a
 * SurfaceView drawn through SurfaceHolder.lockCanvas showed nothing, a transparent hole where
 * nounours's bear belongs. AOSP's version locks the Surface (Surface::lock), points the Java Canvas
 * at the locked buffer (graphics::Canvas::setBuffer, the ACanvas API), and on unlock detaches the
 * Canvas and posts the buffer. This does the same through the NDK pair: ANativeWindow_lock and
 * ANativeWindow_unlockAndPost, which the preloaded bionic shim answers for the adapter's windows,
 * and libhwui's ACanvas_getNativeHandleFromJava / ACanvas_setBuffer / ACanvas_clipRect.
 *
 * The registered implementations are taken from ART's method record, as wrap_system_properties
 * does, and wrapped only if they lie in liboh_android_runtime.so. A surface the pair does not
 * answer (no adapter window, or a shim without the pair) gets the runtime's own versions.
 */
#include <dlfcn.h>
#include <jni.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    int32_t width, height, stride, format;
    void* bits;
    uint32_t reserved[6];
} WlWindowBuffer;                              /* ANativeWindow_Buffer */
typedef struct { int32_t left, top, right, bottom; } WlRect;   /* ARect */

typedef jlong (*LockCanvasFn)(JNIEnv*, jclass, jlong, jobject, jobject);
typedef void (*UnlockCanvasFn)(JNIEnv*, jclass, jlong, jobject);
typedef int (*WindowLockFn)(void*, WlWindowBuffer*, WlRect*);
typedef int (*WindowUnlockFn)(void*);
typedef void* (*CanvasFromJavaFn)(JNIEnv*, jobject);
typedef bool (*CanvasSetBufferFn)(void*, const WlWindowBuffer*, int32_t);
typedef void (*CanvasClipFn)(void*, const WlRect*, bool);

static LockCanvasFn g_lock_canvas;
static UnlockCanvasFn g_unlock_canvas;
static WindowLockFn g_window_lock;
static WindowUnlockFn g_window_unlock;
static CanvasFromJavaFn g_canvas;
static CanvasSetBufferFn g_set_buffer;
static CanvasClipFn g_clip;
static jfieldID g_left, g_top, g_right, g_bottom;

static jlong wl_lockCanvas(JNIEnv* env, jclass clazz, jlong nativeObject, jobject canvas, jobject dirtyRect)
{
    WlRect dirty = { 0, 0, 0, 0 };
    WlWindowBuffer buffer;
    if (dirtyRect != NULL) {
        dirty.left = (*env)->GetIntField(env, dirtyRect, g_left);
        dirty.top = (*env)->GetIntField(env, dirtyRect, g_top);
        dirty.right = (*env)->GetIntField(env, dirtyRect, g_right);
        dirty.bottom = (*env)->GetIntField(env, dirtyRect, g_bottom);
    }
    if (nativeObject != 0 && canvas != NULL && g_window_lock((void*) (intptr_t) nativeObject, &buffer, &dirty) == 0) {
        void* target = g_canvas(env, canvas);
        if (target != NULL && g_set_buffer(target, &buffer, 0 /* ADATASPACE_UNKNOWN */)) {
            /* The lock reports the whole buffer dirty: its earlier contents are not kept. */
            g_clip(target, &dirty, false);
            if (dirtyRect != NULL) {
                (*env)->SetIntField(env, dirtyRect, g_left, dirty.left);
                (*env)->SetIntField(env, dirtyRect, g_top, dirty.top);
                (*env)->SetIntField(env, dirtyRect, g_right, dirty.right);
                (*env)->SetIntField(env, dirtyRect, g_bottom, dirty.bottom);
            }
            /* The locked object Surface.unlockCanvasAndPost hands back, as the runtime's version
             * returns it: its nativeRelease does nothing. */
            return nativeObject;
        }
        if (target != NULL) g_set_buffer(target, NULL, 0);
        g_window_unlock((void*) (intptr_t) nativeObject);
    }
    return g_lock_canvas(env, clazz, nativeObject, canvas, dirtyRect);
}

static void wl_unlockCanvasAndPost(JNIEnv* env, jclass clazz, jlong nativeObject, jobject canvas)
{
    /* Detach the Canvas from the buffer, as AOSP does whether or not one was attached. */
    void* target = canvas != NULL ? g_canvas(env, canvas) : NULL;
    if (target != NULL) g_set_buffer(target, NULL, 0);
    if (nativeObject != 0 && g_window_unlock((void*) (intptr_t) nativeObject) == 0) return;
    g_unlock_canvas(env, clazz, nativeObject, canvas);
}

/* The registered native of a static method, if liboh_android_runtime.so's (see the file comment). */
static void* runtime_native(JNIEnv* env, jclass c, const char* name, const char* signature)
{
    jmethodID m = (*env)->GetStaticMethodID(env, c, name, signature);
    void* entry;
    Dl_info info;
    if (m == NULL) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    entry = *(void* const*) ((const char*) m + 16);   /* ArtMethod::ptr_sized_fields_.data_ */
    if (entry == NULL || dladdr(entry, &info) == 0 || info.dli_fname == NULL
            || strstr(info.dli_fname, "liboh_android_runtime") == NULL) {
        return NULL;
    }
    return entry;
}

/* Returns 1 when both are wrapped. */
int wl_register_surface_canvas(JNIEnv* env)
{
    static const JNINativeMethod wrapped[] = {
        { "nativeLockCanvas", "(JLandroid/graphics/Canvas;Landroid/graphics/Rect;)J", (void*) wl_lockCanvas },
        { "nativeUnlockCanvasAndPost", "(JLandroid/graphics/Canvas;)V", (void*) wl_unlockCanvasAndPost },
    };
    jclass surface, rect;
    int ok = 0;
    g_window_lock = (WindowLockFn) dlsym(RTLD_DEFAULT, "ANativeWindow_lock");
    g_window_unlock = (WindowUnlockFn) dlsym(RTLD_DEFAULT, "ANativeWindow_unlockAndPost");
    g_canvas = (CanvasFromJavaFn) dlsym(RTLD_DEFAULT, "ACanvas_getNativeHandleFromJava");
    g_set_buffer = (CanvasSetBufferFn) dlsym(RTLD_DEFAULT, "ACanvas_setBuffer");
    g_clip = (CanvasClipFn) dlsym(RTLD_DEFAULT, "ACanvas_clipRect");
    if (g_window_lock == NULL || g_window_unlock == NULL || g_canvas == NULL || g_set_buffer == NULL
            || g_clip == NULL) {
        return 0;
    }
    rect = (*env)->FindClass(env, "android/graphics/Rect");
    if (rect == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    g_left = (*env)->GetFieldID(env, rect, "left", "I");
    g_top = (*env)->GetFieldID(env, rect, "top", "I");
    g_right = (*env)->GetFieldID(env, rect, "right", "I");
    g_bottom = (*env)->GetFieldID(env, rect, "bottom", "I");
    (*env)->DeleteLocalRef(env, rect);
    if (g_left == NULL || g_top == NULL || g_right == NULL || g_bottom == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    surface = (*env)->FindClass(env, "android/view/Surface");
    if (surface == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    g_lock_canvas = (LockCanvasFn) runtime_native(env, surface, wrapped[0].name, wrapped[0].signature);
    g_unlock_canvas = (UnlockCanvasFn) runtime_native(env, surface, wrapped[1].name, wrapped[1].signature);
    if (g_lock_canvas != NULL && g_unlock_canvas != NULL) {
        ok = (*env)->RegisterNatives(env, surface, wrapped, 2) == JNI_OK;
        if (!ok) (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, surface);
    return ok;
}
