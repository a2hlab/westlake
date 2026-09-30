// The OH window behind a Java Surface, for every EGL binding that creates a window surface
// (EGL10's EGLImpl and android.opengl.EGL14), and the one behind a SurfaceTexture.
//
// Resolved the way the runtime's hwui resolves it: ANativeWindow_fromSurface, then the adapter's
// AdapterAnw unwrapped to the OHNativeWindow OH's EGL accepts. Both are looked up at run time, so
// this library has no link-time dependency on the runtime's internals.
#include <jni.h>
#include <dlfcn.h>
#include <stdio.h>

struct ANativeWindow;

// The runtime's bridge library (liboh_adapter_bridge.so) is not in the global scope, so a
// RTLD_DEFAULT lookup misses its symbols; ask that library directly.
static void* bridge_symbol(const char* name) {
    void* fn = dlsym(RTLD_DEFAULT, name);
    if (fn == nullptr) {
        void* bridge = dlopen("liboh_adapter_bridge.so", RTLD_NOW | RTLD_NOLOAD);
        if (bridge != nullptr) fn = dlsym(bridge, name);
    }
    return fn;
}

extern "C" void* wl_window_from_surface(JNIEnv* env, jobject surface) {
    typedef ANativeWindow* (*FromSurface)(JNIEnv*, jobject);
    typedef void* (*Unwrap)(ANativeWindow*);
    static FromSurface from_surface =
            reinterpret_cast<FromSurface>(dlsym(RTLD_DEFAULT, "ANativeWindow_fromSurface"));
    // The unwrapper lives in the bridge; missing it, the adapter's wrapper reached OH's EGL, which
    // rejected it ("eglCreateWindowSurface error").
    static Unwrap unwrap = reinterpret_cast<Unwrap>(bridge_symbol("oh_anw_get_oh"));
    if (from_surface == nullptr) {
        fprintf(stderr, "[WL-OPENGL] ANativeWindow_fromSurface not found\n");
        return nullptr;
    }
    ANativeWindow* window = from_surface(env, surface);
    if (window == nullptr) return nullptr;
    void* oh = unwrap != nullptr ? unwrap(window) : nullptr;
    fprintf(stderr, "[WL-OPENGL] window surface: anw=%p oh=%p unwrap=%s\n",
            static_cast<void*>(window), oh, unwrap != nullptr ? "found" : "MISSING");
    return oh != nullptr ? oh : static_cast<void*>(window);
}

// A SurfaceTexture is backed by an OH_NativeImage; its producer OHNativeWindow is what OH's EGL
// renders into (AOSP: new Surface(SurfaceTexture_getProducer())). The bridge's SurfaceTexture JNI
// owns that window for the SurfaceTexture's lifetime.
extern "C" void* wl_window_from_surface_texture(JNIEnv* env, jobject surfaceTexture) {
    typedef jlong (*GetWindow)(JNIEnv*, jobject);
    static GetWindow get_window =
            reinterpret_cast<GetWindow>(bridge_symbol("oh_surface_texture_get_native_window"));
    if (get_window == nullptr) {
        fprintf(stderr, "[WL-OPENGL] oh_surface_texture_get_native_window not found\n");
        return nullptr;
    }
    void* window = reinterpret_cast<void*>(get_window(env, surfaceTexture));
    fprintf(stderr, "[WL-OPENGL] SurfaceTexture window surface: oh=%p\n", window);
    return window;
}
