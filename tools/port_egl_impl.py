#!/usr/bin/env python3
"""Derive framework/opengl-jni/oh_egl_impl.cpp from AOSP's com_google_android_gles_jni_EGLImpl.cpp.

AOSP's EGL10 binding is plain EGL plus JNI except for three things that reach into Android's
private graphics stack, none of which exists on OH:

  eglCreateWindowSurface         android_view_Surface_getNativeWindow() -> sp<ANativeWindow>
  eglCreateWindowSurfaceTexture  SurfaceTexture_getProducer() -> new Surface(producer)
  getInitCount                   android::egl_get_init_count(), the EGL loader's private counter

Everything else is kept byte-for-byte, so a reviewer can diff this port against the AOSP file and
see only those replacements. The window surface comes from wl_window_from_surface
(oh_surface_window.cpp), which resolves it the way the runtime's hwui does; a SurfaceTexture's
comes from wl_window_from_surface_texture, the OH producer window of its OH_NativeImage.

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

// OH port: the OH window a Java Surface carries (oh_surface_window.cpp, shared with EGL14).
extern "C" void* wl_window_from_surface(JNIEnv* env, jobject surface);
extern "C" void* wl_window_from_surface_texture(JNIEnv* env, jobject surfaceTexture);
static void* oh_window_from_surface(JNIEnv* env, jobject surface) {
    return wl_window_from_surface(env, surface);
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
    if (display == NULL || config == NULL
        || !validAttribList(_env, attrib_list)) {
        jniThrowException(_env, "java/lang/IllegalArgumentException", NULL);
        return JNI_FALSE;
    }
    EGLDisplay dpy = getDisplay(_env, display);
    EGLContext cnf = getConfig(_env, config);
    // OH port: the SurfaceTexture's own OH producer window (oh_surface_window.cpp).
    void* window = native_window != NULL ? wl_window_from_surface_texture(_env, native_window) : NULL;
    if (window == NULL) {
        jniThrowException(_env, "java/lang/IllegalArgumentException",
                "Make sure the SurfaceTexture is valid");
        return 0;
    }
    jint* base = beginNativeAttribList(_env, attrib_list);
    EGLSurface sur = eglCreateWindowSurface(dpy, cnf, (EGLNativeWindowType) window, base);
    if (sur == EGL_NO_SURFACE) {
        fprintf(stderr, "[WL-OPENGL] eglCreateWindowSurface(SurfaceTexture window=%p) failed: EGL error 0x%x\n",
                window, eglGetError());
    }
    endNativeAttributeList(_env, attrib_list, base);
    return reinterpret_cast<jlong>(sur);
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
