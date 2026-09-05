/*
 * Diagnostic EGL boundary for Android media-player DSOs on OpenHarmony.
 *
 * The player creates its own EGL window surface in an isolated linker
 * namespace.  Log the exact display/config/window/context handles, queried
 * dimensions, make-current result and the first swap failures.  eglGetError()
 * is destructive, so retain every consumed error for the guest's next call.
 */
#include <EGL/egl.h>
#include <dlfcn.h>
#include <native_window/external_window.h>
#include <pthread.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void *g_real_egl;
static __thread EGLint g_pending_error = EGL_SUCCESS;
static unsigned long g_swap_count;
static unsigned long g_query_count;
static unsigned long g_presentation_count;

static void trace_line(const char *fmt, ...);

/*
 * EGL_ANDROID_presentation_time is not exported by the OH EGL client, but
 * Android players obtain it through eglGetProcAddress and use it to timestamp
 * each buffer before eglSwapBuffers.  Retain the native-window association
 * made at eglCreateWindowSurface and translate the timestamp onto OH's window
 * boundary.  A small fixed table is sufficient because the association count
 * is bounded by live EGL window surfaces and avoids ownership changes.
 */
struct surface_window_binding {
    EGLSurface surface;
    OHNativeWindow *window;
};

static pthread_mutex_t g_surface_lock = PTHREAD_MUTEX_INITIALIZER;
static struct surface_window_binding g_surface_windows[32];

static void remember_surface_window(EGLSurface surface, EGLNativeWindowType window)
{
    if (surface == EGL_NO_SURFACE || window == (EGLNativeWindowType)0) return;
    pthread_mutex_lock(&g_surface_lock);
    size_t free_slot = sizeof(g_surface_windows) / sizeof(g_surface_windows[0]);
    for (size_t i = 0; i < sizeof(g_surface_windows) / sizeof(g_surface_windows[0]); ++i) {
        if (g_surface_windows[i].surface == surface) {
            free_slot = i;
            break;
        }
        if (free_slot == sizeof(g_surface_windows) / sizeof(g_surface_windows[0]) &&
            g_surface_windows[i].surface == EGL_NO_SURFACE) {
            free_slot = i;
        }
    }
    if (free_slot < sizeof(g_surface_windows) / sizeof(g_surface_windows[0])) {
        g_surface_windows[free_slot].surface = surface;
        g_surface_windows[free_slot].window = (OHNativeWindow *)window;
    }
    pthread_mutex_unlock(&g_surface_lock);
}

static OHNativeWindow *find_surface_window(EGLSurface surface)
{
    OHNativeWindow *window = NULL;
    pthread_mutex_lock(&g_surface_lock);
    for (size_t i = 0; i < sizeof(g_surface_windows) / sizeof(g_surface_windows[0]); ++i) {
        if (g_surface_windows[i].surface == surface) {
            window = g_surface_windows[i].window;
            break;
        }
    }
    pthread_mutex_unlock(&g_surface_lock);
    return window;
}

static void forget_surface_window(EGLSurface surface)
{
    pthread_mutex_lock(&g_surface_lock);
    for (size_t i = 0; i < sizeof(g_surface_windows) / sizeof(g_surface_windows[0]); ++i) {
        if (g_surface_windows[i].surface == surface) {
            g_surface_windows[i].surface = EGL_NO_SURFACE;
            g_surface_windows[i].window = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_surface_lock);
}

/*
 * Keep the implementation private. libttmplayer.so exports a global OBJECT
 * named eglPresentationTimeANDROID for its cached function-pointer slot. If
 * this function has the public extension name, normal ELF preemption resolves
 * our address expression to that non-executable object and the player's first
 * indirect call faults at libttmplayer.so+0x47b380.
 */
static EGLBoolean westlake_egl_presentation_time(EGLDisplay display,
                                                  EGLSurface surface,
                                                  int64_t time)
{
    (void)display;
    OHNativeWindow *window = find_surface_window(surface);
    if (window == NULL) {
        return EGL_FALSE;
    }
    const int32_t result = OH_NativeWindow_NativeWindowHandleOpt(
        window, SET_UI_TIMESTAMP, (uint64_t)time);
    unsigned long count = __atomic_add_fetch(
        &g_presentation_count, 1, __ATOMIC_RELAXED);
    if (count <= 8 || result != 0 || count % 300 == 0) {
        trace_line("[WESTLAKE-TTEGL] eglPresentationTimeANDROID #%lu "
                   "surface=%p window=%p time=%lld result=%d\n",
                   count, (void *)surface, (void *)window,
                   (long long)time, result);
    }
    return result == 0 ? EGL_TRUE : EGL_FALSE;
}

static void trace_line(const char *fmt, ...)
{
    char line[768];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    size_t length = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
    (void)write(STDERR_FILENO, line, length);
}

static void *real_symbol(const char *name)
{
    if (g_real_egl == NULL) {
        g_real_egl = dlopen("/system/lib64/platformsdk/libEGL.so",
                           RTLD_NOW | RTLD_LOCAL);
        trace_line("[WESTLAKE-TTEGL] real=%p error=%s\n", g_real_egl,
                   g_real_egl == NULL ? dlerror() : "none");
    }
    void *symbol = g_real_egl == NULL ? NULL : dlsym(g_real_egl, name);
    if (symbol == NULL) {
        trace_line("[WESTLAKE-TTEGL] missing %s error=%s\n", name, dlerror());
    }
    return symbol;
}

static EGLint take_real_error(void)
{
    static EGLint (*real_eglGetError)(void);
    if (real_eglGetError == NULL) {
        real_eglGetError = (EGLint (*)(void))real_symbol("eglGetError");
    }
    return real_eglGetError == NULL ? EGL_SUCCESS : real_eglGetError();
}

static EGLint collect_error(void)
{
    EGLint error = take_real_error();
    if (error != EGL_SUCCESS && g_pending_error == EGL_SUCCESS) {
        g_pending_error = error;
    }
    return error;
}

EGLint eglGetError(void)
{
    if (g_pending_error != EGL_SUCCESS) {
        EGLint error = g_pending_error;
        g_pending_error = EGL_SUCCESS;
        return error;
    }
    return take_real_error();
}

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id)
{
    static EGLDisplay (*real)(EGLNativeDisplayType);
    if (real == NULL) real = (EGLDisplay (*)(EGLNativeDisplayType))real_symbol("eglGetDisplay");
    EGLDisplay result = real == NULL ? EGL_NO_DISPLAY : real(display_id);
    EGLint error = collect_error();
    trace_line("[WESTLAKE-TTEGL] eglGetDisplay native=%p result=%p error=0x%x\n",
               (void *)display_id, (void *)result, error);
    return result;
}

EGLBoolean eglInitialize(EGLDisplay display, EGLint *major, EGLint *minor)
{
    static EGLBoolean (*real)(EGLDisplay, EGLint *, EGLint *);
    if (real == NULL) real = (EGLBoolean (*)(EGLDisplay, EGLint *, EGLint *))real_symbol("eglInitialize");
    EGLBoolean result = real == NULL ? EGL_FALSE : real(display, major, minor);
    EGLint error = collect_error();
    trace_line("[WESTLAKE-TTEGL] eglInitialize dpy=%p result=%u version=%d.%d error=0x%x\n",
               (void *)display, result, major ? *major : -1, minor ? *minor : -1, error);
    return result;
}

EGLBoolean eglChooseConfig(EGLDisplay display, const EGLint *attribs,
                           EGLConfig *configs, EGLint config_size,
                           EGLint *num_config)
{
    static EGLBoolean (*real)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
    if (real == NULL) real = (EGLBoolean (*)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *))real_symbol("eglChooseConfig");
    EGLBoolean result = real == NULL ? EGL_FALSE : real(display, attribs, configs, config_size, num_config);
    EGLint error = collect_error();
    trace_line("[WESTLAKE-TTEGL] eglChooseConfig dpy=%p result=%u count=%d first=%p error=0x%x\n",
               (void *)display, result, num_config ? *num_config : -1,
               configs && config_size > 0 ? (void *)configs[0] : NULL, error);
    return result;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay display, EGLConfig config,
                              EGLint attribute, EGLint *value)
{
    static EGLBoolean (*real)(EGLDisplay, EGLConfig, EGLint, EGLint *);
    if (real == NULL) real = (EGLBoolean (*)(EGLDisplay, EGLConfig, EGLint, EGLint *))real_symbol("eglGetConfigAttrib");
    EGLBoolean result = real == NULL ? EGL_FALSE : real(display, config, attribute, value);
    EGLint error = collect_error();
    trace_line("[WESTLAKE-TTEGL] eglGetConfigAttrib attr=0x%x result=%u value=%d error=0x%x\n",
               attribute, result, value ? *value : -1, error);
    return result;
}

EGLContext eglCreateContext(EGLDisplay display, EGLConfig config,
                            EGLContext share_context, const EGLint *attribs)
{
    static EGLContext (*real)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
    if (real == NULL) real = (EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint *))real_symbol("eglCreateContext");
    EGLContext result = real == NULL ? EGL_NO_CONTEXT : real(display, config, share_context, attribs);
    EGLint error = collect_error();
    trace_line("[WESTLAKE-TTEGL] eglCreateContext dpy=%p config=%p share=%p result=%p error=0x%x\n",
               (void *)display, (void *)config, (void *)share_context, (void *)result, error);
    return result;
}

EGLSurface eglCreateWindowSurface(EGLDisplay display, EGLConfig config,
                                  EGLNativeWindowType window,
                                  const EGLint *attribs)
{
    static EGLSurface (*real)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint *);
    if (real == NULL) real = (EGLSurface (*)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint *))real_symbol("eglCreateWindowSurface");
    EGLSurface result = real == NULL ? EGL_NO_SURFACE : real(display, config, window, attribs);
    EGLint error = collect_error();
    if (result != EGL_NO_SURFACE) {
        remember_surface_window(result, window);
    }
    trace_line("[WESTLAKE-TTEGL] eglCreateWindowSurface dpy=%p config=%p window=%p result=%p error=0x%x\n",
               (void *)display, (void *)config, (void *)window, (void *)result, error);
    return result;
}

EGLBoolean eglMakeCurrent(EGLDisplay display, EGLSurface draw,
                          EGLSurface read, EGLContext context)
{
    static EGLBoolean (*real)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    if (real == NULL) real = (EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext))real_symbol("eglMakeCurrent");
    EGLBoolean result = real == NULL ? EGL_FALSE : real(display, draw, read, context);
    EGLint error = collect_error();
    trace_line("[WESTLAKE-TTEGL] eglMakeCurrent dpy=%p draw=%p read=%p ctx=%p result=%u error=0x%x\n",
               (void *)display, (void *)draw, (void *)read, (void *)context, result, error);
    return result;
}

EGLBoolean eglQuerySurface(EGLDisplay display, EGLSurface surface,
                           EGLint attribute, EGLint *value)
{
    static EGLBoolean (*real)(EGLDisplay, EGLSurface, EGLint, EGLint *);
    if (real == NULL) real = (EGLBoolean (*)(EGLDisplay, EGLSurface, EGLint, EGLint *))real_symbol("eglQuerySurface");
    EGLBoolean result = real == NULL ? EGL_FALSE : real(display, surface, attribute, value);
    EGLint error = collect_error();
    unsigned long count = __atomic_add_fetch(&g_query_count, 1, __ATOMIC_RELAXED);
    if (count <= 20 || result == EGL_FALSE || error != EGL_SUCCESS || count % 600 == 0) {
        trace_line("[WESTLAKE-TTEGL] eglQuerySurface #%lu surface=%p attr=0x%x "
                   "result=%u value=%d error=0x%x\n",
                   count, (void *)surface, attribute, result,
                   value ? *value : -1, error);
    }
    return result;
}

EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface)
{
    static EGLBoolean (*real)(EGLDisplay, EGLSurface);
    if (real == NULL) real = (EGLBoolean (*)(EGLDisplay, EGLSurface))real_symbol("eglSwapBuffers");
    EGLBoolean result = real == NULL ? EGL_FALSE : real(display, surface);
    EGLint error = collect_error();
    unsigned long count = __atomic_add_fetch(&g_swap_count, 1, __ATOMIC_RELAXED);
    if (count <= 20 || result == EGL_FALSE || error != EGL_SUCCESS || count % 300 == 0) {
        trace_line("[WESTLAKE-TTEGL] eglSwapBuffers #%lu dpy=%p surface=%p result=%u error=0x%x\n",
                   count, (void *)display, (void *)surface, result, error);
    }
    return result;
}

EGLBoolean eglDestroySurface(EGLDisplay display, EGLSurface surface)
{
    static EGLBoolean (*real)(EGLDisplay, EGLSurface);
    if (real == NULL) real = (EGLBoolean (*)(EGLDisplay, EGLSurface))real_symbol("eglDestroySurface");
    EGLBoolean result = real == NULL ? EGL_FALSE : real(display, surface);
    EGLint error = collect_error();
    forget_surface_window(surface);
    trace_line("[WESTLAKE-TTEGL] eglDestroySurface surface=%p result=%u error=0x%x\n",
               (void *)surface, result, error);
    return result;
}

EGLBoolean eglDestroyContext(EGLDisplay display, EGLContext context)
{
    static EGLBoolean (*real)(EGLDisplay, EGLContext);
    if (real == NULL) real = (EGLBoolean (*)(EGLDisplay, EGLContext))real_symbol("eglDestroyContext");
    EGLBoolean result = real == NULL ? EGL_FALSE : real(display, context);
    EGLint error = collect_error();
    trace_line("[WESTLAKE-TTEGL] eglDestroyContext context=%p result=%u error=0x%x\n",
               (void *)context, result, error);
    return result;
}

EGLBoolean eglTerminate(EGLDisplay display)
{
    static EGLBoolean (*real)(EGLDisplay);
    if (real == NULL) real = (EGLBoolean (*)(EGLDisplay))real_symbol("eglTerminate");
    EGLBoolean result = real == NULL ? EGL_FALSE : real(display);
    EGLint error = collect_error();
    trace_line("[WESTLAKE-TTEGL] eglTerminate dpy=%p result=%u error=0x%x\n",
               (void *)display, result, error);
    return result;
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname)
{
    static __eglMustCastToProperFunctionPointerType (*real)(const char *);
    if (real == NULL) real = (__eglMustCastToProperFunctionPointerType (*)(const char *))real_symbol("eglGetProcAddress");
    __eglMustCastToProperFunctionPointerType result = real == NULL ? NULL : real(procname);
    if (result == NULL && procname != NULL &&
        strcmp(procname, "eglPresentationTimeANDROID") == 0) {
        result = (__eglMustCastToProperFunctionPointerType)
            westlake_egl_presentation_time;
    }
    EGLint error = collect_error();
    trace_line("[WESTLAKE-TTEGL] eglGetProcAddress name=%s result=%p error=0x%x\n",
               procname ? procname : "(null)", (void *)result, error);
    return result;
}
