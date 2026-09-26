// The OH window behind a Java Surface, for every EGL binding that creates a window surface
// (EGL10's EGLImpl and android.opengl.EGL14).
//
// Resolved the way the runtime's hwui resolves it: ANativeWindow_fromSurface, then the adapter's
// AdapterAnw unwrapped to the OHNativeWindow OH's EGL accepts. Both are looked up at run time, so
// this library has no link-time dependency on the runtime's internals.
#include <jni.h>
#include <dlfcn.h>
#include <stdio.h>

struct ANativeWindow;

extern "C" void* wl_window_from_surface(JNIEnv* env, jobject surface) {
    typedef ANativeWindow* (*FromSurface)(JNIEnv*, jobject);
    typedef void* (*Unwrap)(ANativeWindow*);
    static FromSurface from_surface =
            reinterpret_cast<FromSurface>(dlsym(RTLD_DEFAULT, "ANativeWindow_fromSurface"));
    // The unwrapper lives in liboh_adapter_bridge.so, which is not in the global scope: a
    // RTLD_DEFAULT lookup misses it, and the adapter's wrapper then reached OH's EGL, which
    // rejected it ("eglCreateWindowSurface error"). Ask that library directly.
    static Unwrap unwrap = [] {
        void* fn = dlsym(RTLD_DEFAULT, "oh_anw_get_oh");
        if (fn == nullptr) {
            void* bridge = dlopen("liboh_adapter_bridge.so", RTLD_NOW | RTLD_NOLOAD);
            if (bridge != nullptr) fn = dlsym(bridge, "oh_anw_get_oh");
        }
        return reinterpret_cast<Unwrap>(fn);
    }();
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
