// Registers Android's Java OpenGL bindings onto the framework classes.
//
// The runtime's libandroid_runtime ships none of them -- EGLImpl, GLImpl, android.opengl.GLES* --
// so GLSurfaceView and every android.opengl call failed with "No implementation found".
// AppSchedulerBridge loads this library through Runtime.nativeLoad with the boot class loader, so
// FindClass here resolves the framework classes, exactly as libwl_missing_natives does.
#include <jni.h>
#include <stdio.h>
#include <unistd.h>

int register_com_google_android_gles_jni_EGLImpl(JNIEnv*);
int register_com_google_android_gles_jni_GLImpl(JNIEnv*);
int register_android_opengl_jni_GLES10(JNIEnv*);
int register_android_opengl_jni_GLES10Ext(JNIEnv*);
int register_android_opengl_jni_GLES11(JNIEnv*);
int register_android_opengl_jni_GLES11Ext(JNIEnv*);
int register_android_opengl_jni_GLES20(JNIEnv*);
int register_android_opengl_jni_GLES30(JNIEnv*);
int register_android_opengl_jni_GLES31(JNIEnv*);
int register_android_opengl_jni_GLES31Ext(JNIEnv*);
int register_android_opengl_jni_GLES32(JNIEnv*);

struct Registration { const char* name; int (*fn)(JNIEnv*); };

static const Registration kRegistrations[] = {
    {"EGLImpl", register_com_google_android_gles_jni_EGLImpl},
    {"GLImpl", register_com_google_android_gles_jni_GLImpl},
    {"GLES10", register_android_opengl_jni_GLES10},
    {"GLES10Ext", register_android_opengl_jni_GLES10Ext},
    {"GLES11", register_android_opengl_jni_GLES11},
    {"GLES11Ext", register_android_opengl_jni_GLES11Ext},
    {"GLES20", register_android_opengl_jni_GLES20},
    {"GLES30", register_android_opengl_jni_GLES30},
    {"GLES31", register_android_opengl_jni_GLES31},
    {"GLES31Ext", register_android_opengl_jni_GLES31Ext},
    {"GLES32", register_android_opengl_jni_GLES32},
};

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    char line[512];
    int n = snprintf(line, sizeof(line), "[WL-OPENGL] registered");
    for (const Registration& r : kRegistrations) {
        int rc = r.fn(env);
        if (env->ExceptionCheck()) env->ExceptionClear();
        n += snprintf(line + n, sizeof(line) - n, " %s=%s", r.name, rc < 0 ? "FAIL" : "ok");
    }
    // write(2), not fprintf: the bionic shim interposes stdio in this process.
    line[n++] = '\n';
    (void) write(2, line, n);
    return JNI_VERSION_1_6;
}
