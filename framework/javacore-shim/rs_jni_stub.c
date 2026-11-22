/*
 * librs_jni.so for Westlake: RenderScript is not available, and says so the way Android's API does.
 *
 * android.renderscript.RenderScript's static initializer loads "rs_jni" and calls _nInit. Without the library it
 * throws RSRuntimeException from <clinit>, so the class fails to initialize: every later use is a
 * NoClassDefFoundError, which apps that filter images with RenderScript (blurs, thumbnails) rarely catch.
 * Round hkid1 (D600): com.frontrow.vlog, com.pujie.watchfaces, com.sec.android.app.sbrowser and one more stopped
 * on it.
 *
 * With this library the class initializes, and RenderScript.create() fails as on a device without a working driver:
 * the device and context come back 0, and create() throws RSDriverException ("Failed to create RS context"), the
 * exception apps' fallbacks are written for. No RenderScript runs. Only the natives on that path are bound; nothing
 * else can be reached without a context.
 */
#include <jni.h>
#include <stdint.h>
#include <stdio.h>

static void rs_init(JNIEnv* env, jclass clazz) { (void) env; (void) clazz; }
static jint rs_pointer_size(JNIEnv* env, jclass clazz) { (void) env; (void) clazz; return (jint) sizeof(void*); }
static jlong rs_device_create(JNIEnv* env, jobject self) { (void) env; (void) self; return 0; }
static void rs_device_destroy(JNIEnv* env, jobject self, jlong device) { (void) env; (void) self; (void) device; }
static void rs_device_set_config(JNIEnv* env, jobject self, jlong device, jint param, jint value)
{
    (void) env; (void) self; (void) device; (void) param; (void) value;
}
static jlong rs_context_create(JNIEnv* env, jobject self, jlong device, jint flags, jint sdk, jint type)
{
    (void) env; (void) self; (void) device; (void) flags; (void) sdk; (void) type;
    return 0;   /* no context: RenderScript.create() throws RSDriverException */
}

static const JNINativeMethod kRenderScript[] = {
    {"_nInit", "()V", (void*) rs_init},
    {"rsnSystemGetPointerSize", "()I", (void*) rs_pointer_size},
    {"nDeviceCreate", "()J", (void*) rs_device_create},
    {"nDeviceDestroy", "(J)V", (void*) rs_device_destroy},
    {"nDeviceSetConfig", "(JII)V", (void*) rs_device_set_config},
    {"rsnContextCreate", "(JIII)J", (void*) rs_context_create},
};

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv* env = NULL;
    jclass clazz;
    int bound = 0;
    size_t i;
    (void) reserved;
    if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_6) != JNI_OK || env == NULL) return JNI_ERR;
    clazz = (*env)->FindClass(env, "android/renderscript/RenderScript");
    if (clazz == NULL) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    for (i = 0; i < sizeof(kRenderScript) / sizeof(kRenderScript[0]); i++) {
        if ((*env)->RegisterNatives(env, clazz, &kRenderScript[i], 1) == JNI_OK) {
            bound++;
        } else {
            (*env)->ExceptionClear(env);
        }
    }
    (*env)->DeleteLocalRef(env, clazz);
    fprintf(stderr, "[WL-NATIVES] RenderScript: not available (stub librs_jni.so, %d natives bound)\n", bound);
    fflush(stderr);
    return JNI_VERSION_1_6;
}
