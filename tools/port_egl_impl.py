#!/usr/bin/env python3
"""Derive framework/opengl-jni/oh_egl_impl.cpp from AOSP's com_google_android_gles_jni_EGLImpl.cpp.

AOSP's EGL10 binding is plain EGL plus JNI except for three things that reach into Android's
private graphics stack, none of which exists on OH:

  eglCreateWindowSurface         android_view_Surface_getNativeWindow() -> sp<ANativeWindow>
  eglCreateWindowSurfaceTexture  SurfaceTexture_getProducer() -> new Surface(producer)
  getInitCount                   android::egl_get_init_count(), the EGL loader's private counter

Everything else is kept byte-for-byte, so a reviewer can diff this port against the AOSP file and
see only those replacements. The window surface goes through ANativeWindow_fromSurface -- the same
call the runtime's hwui uses, which returns the OHNativeWindow OH's EGL accepts (unwrapping the
adapter's AdapterAnw when present). SurfaceTexture-backed windows are refused explicitly.

Usage: port_egl_impl.py <aosp EGLImpl.cpp> <out oh_egl_impl.cpp>
"""
import re, sys

src = open(sys.argv[1]).read()

def replace_function(text, name, body):
    m = re.search(r'\nstatic [^\n]*\b' + name + r'\(', text)
    if not m:
        raise SystemExit('function not found: ' + name)
    start = m.start() + 1
    depth, i = 0, text.index('{', start)
    while True:
        if text[i] == '{': depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0: break
        i += 1
    return text[:start] + body.strip('\n') + text[i + 1:]

includes_old = src[src.index('#include <nativehelper/JNIHelp.h>'):src.index('namespace android {')]
includes_new = '''#include <nativehelper/JNIHelp.h>
#include <android_runtime/AndroidRuntime.h>
#include <utils/misc.h>

#include <EGL/egl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <map>
#include <mutex>

// OH port: the window a Java Surface carries, resolved the way the runtime's hwui resolves it.
// Looked up at run time so this library has no link-time dependency on the runtime's internals.
struct ANativeWindow;
static void* oh_window_from_surface(JNIEnv* env, jobject surface) {
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
        fprintf(stderr, "[WL-OPENGL] ANativeWindow_fromSurface not found\\n");
        return nullptr;
    }
    ANativeWindow* window = from_surface(env, surface);
    if (window == nullptr) return nullptr;
    void* oh = unwrap != nullptr ? unwrap(window) : nullptr;
    fprintf(stderr, "[WL-OPENGL] window surface: anw=%p oh=%p unwrap=%s\\n",
            static_cast<void*>(window), oh, unwrap != nullptr ? "found" : "MISSING");
    return oh != nullptr ? oh : static_cast<void*>(window);
}

// OH port: AOSP reads the EGL loader's private per-display init count. Keep our own, maintained
// by the eglInitialize/eglTerminate bindings in this file.
static std::mutex gInitCountLock;
static std::map<EGLDisplay, int> gInitCount;

'''
src = src.replace(includes_old, includes_new, 1)

src = replace_function(src, 'jni_eglCreateWindowSurface', r'''
static jlong jni_eglCreateWindowSurface(JNIEnv *_env, jobject _this, jobject display,
        jobject config, jobject native_window, jintArray attrib_list) {
    if (display == NULL || config == NULL
        || !validAttribList(_env, attrib_list)) {
        jniThrowException(_env, "java/lang/IllegalArgumentException", NULL);
        return JNI_FALSE;
    }
    EGLDisplay dpy = getDisplay(_env, display);
    EGLContext cnf = getConfig(_env, config);
    void* window = native_window != NULL ? oh_window_from_surface(_env, native_window) : NULL;
    if (window == NULL) {
        jniThrowException(_env, "java/lang/IllegalArgumentException",
                "Make sure the SurfaceView or associated SurfaceHolder has a valid Surface");
        return 0;
    }
    jint* base = beginNativeAttribList(_env, attrib_list);
    EGLSurface sur = eglCreateWindowSurface(dpy, cnf, (EGLNativeWindowType) window, base);
    if (sur == EGL_NO_SURFACE) {
        fprintf(stderr, "[WL-OPENGL] eglCreateWindowSurface(window=%p) failed: EGL error 0x%x\n",
                window, eglGetError());
    }
    endNativeAttributeList(_env, attrib_list, base);
    return reinterpret_cast<jlong>(sur);
}
''')

src = replace_function(src, 'jni_eglCreateWindowSurfaceTexture', r'''
static jlong jni_eglCreateWindowSurfaceTexture(JNIEnv *_env, jobject _this, jobject display,
        jobject config, jobject native_window, jintArray attrib_list) {
    // OH port: a SurfaceTexture has no OH native window behind it here.
    jniThrowException(_env, "java/lang/UnsupportedOperationException",
            "eglCreateWindowSurface with a SurfaceTexture is not supported on this platform");
    return 0;
}
''')

src = replace_function(src, 'jni_getInitCount', r'''
static jint jni_getInitCount(JNIEnv *_env, jobject _clazz, jobject display) {
    EGLDisplay dpy = getDisplay(_env, display);
    std::lock_guard<std::mutex> guard(gInitCountLock);
    auto it = gInitCount.find(dpy);
    return it == gInitCount.end() ? 0 : it->second;
}
''')

# Maintain the replacement counter where AOSP's loader would.
src = src.replace('    EGLBoolean success = eglInitialize(dpy, NULL, NULL);\n',
                  '    EGLBoolean success = eglInitialize(dpy, NULL, NULL);\n'
                  '    if (success) { std::lock_guard<std::mutex> g(gInitCountLock); gInitCount[dpy]++; }\n', 1)
m = re.search(r'static jboolean jni_eglTerminate\([^{]*\{', src)
body_start = m.end()
term = src[body_start:src.index('\n}\n', body_start)]
new_term = term.replace('return EglBoolToJBool(eglTerminate(dpy));',
    'EGLBoolean ok = eglTerminate(dpy);\n'
    '    if (ok) { std::lock_guard<std::mutex> g(gInitCountLock); gInitCount.erase(dpy); }\n'
    '    return EglBoolToJBool(ok);')
if new_term == term: raise SystemExit('eglTerminate body not recognised')
src = src[:body_start] + new_term + src[body_start + len(term):]

header = ('// GENERATED by tools/port_egl_impl.py from AOSP frameworks/base/core/jni/'
          'com_google_android_gles_jni_EGLImpl.cpp.\n// Only the window-surface, SurfaceTexture and '
          'init-count functions differ; see the script.\n')
open(sys.argv[2], 'w').write(header + src)
print('wrote', sys.argv[2])
