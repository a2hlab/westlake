/*
 * libaudioeffect_jni.so for a board with no Android audio-effect engine.
 *
 * android.media.audiofx.AudioEffect and Visualizer load this library from their static
 * initializers. The runtime did not ship it, so the load failed inside <clinit> and every later
 * touch of the class was an unrecoverable Error -- Fossify Music Player and Just Player died
 * creating an Equalizer before their first screen.
 *
 * This library binds the same natives with the answers of a device that has no effects: none is
 * listed by queryEffects, and creating one fails with ERROR_BAD_VALUE, which AudioEffect's
 * constructor turns into IllegalArgumentException("Effect type ... not supported") -- the
 * RuntimeException apps already catch when an effect is missing. Nothing here touches OH audio.
 */
#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ERROR_BAD_VALUE (-4)
#define ERROR_NO_INIT (-3)

/*
 * Each stub ignores its Java arguments, so one per return type serves every signature: on arm64 a
 * callee that declares fewer parameters than it is passed never reads the rest.
 */
static jint setup_unsupported(JNIEnv* env, jobject thiz) { (void)env; (void)thiz; return ERROR_BAD_VALUE; }
static jint int_no_init(JNIEnv* env, jobject thiz) { (void)env; (void)thiz; return ERROR_NO_INIT; }
static jboolean bool_false(JNIEnv* env, jobject thiz) { (void)env; (void)thiz; return JNI_FALSE; }
static void void_nothing(JNIEnv* env, jobject thiz) { (void)env; (void)thiz; }

static jobjectArray no_descriptors(JNIEnv* env, jclass c)
{
    (void)c;
    jclass descriptor = (*env)->FindClass(env, "android/media/audiofx/AudioEffect$Descriptor");
    if (descriptor == NULL) return NULL;   /* the pending exception reports it */
    return (*env)->NewObjectArray(env, 0, descriptor, NULL);
}

static jintArray capture_size_range(JNIEnv* env, jclass c)
{
    (void)c;
    jint range[2] = {128, 1024};   /* Visualizer::getMinCaptureSize / getMaxCaptureSize */
    jintArray out = (*env)->NewIntArray(env, 2);
    if (out != NULL) (*env)->SetIntArrayRegion(env, out, 0, 2, range);
    return out;
}

static jint max_capture_rate(JNIEnv* env, jclass c) { (void)env; (void)c; return 20000; }

static const JNINativeMethod kAudioEffect[] = {
    {"native_init", "()V", (void*) void_nothing},
    {"native_setup", "(Ljava/lang/Object;Ljava/lang/String;Ljava/lang/String;IIILjava/lang/String;[I[Ljava/lang/Object;Landroid/os/Parcel;Z)I", (void*) setup_unsupported},
    {"native_finalize", "()V", (void*) void_nothing},
    {"native_release", "()V", (void*) void_nothing},
    {"native_setEnabled", "(Z)I", (void*) int_no_init},
    {"native_getEnabled", "()Z", (void*) bool_false},
    {"native_hasControl", "()Z", (void*) bool_false},
    {"native_setParameter", "(I[BI[B)I", (void*) int_no_init},
    {"native_getParameter", "(I[BI[B)I", (void*) int_no_init},
    {"native_command", "(II[BI[B)I", (void*) int_no_init},
    {"native_query_effects", "()[Ljava/lang/Object;", (void*) no_descriptors},
    {"native_query_pre_processing", "(I)[Ljava/lang/Object;", (void*) no_descriptors},
};

static const JNINativeMethod kVisualizer[] = {
    {"native_init", "()V", (void*) void_nothing},
    {"native_setup", "(Ljava/lang/Object;I[ILandroid/os/Parcel;)I", (void*) setup_unsupported},
    {"native_finalize", "()V", (void*) void_nothing},
    {"native_release", "()V", (void*) void_nothing},
    {"native_setEnabled", "(Z)I", (void*) int_no_init},
    {"native_getEnabled", "()Z", (void*) bool_false},
    {"getCaptureSizeRange", "()[I", (void*) capture_size_range},
    {"getMaxCaptureRate", "()I", (void*) max_capture_rate},
    {"native_setCaptureSize", "(I)I", (void*) int_no_init},
    {"native_getCaptureSize", "()I", (void*) int_no_init},
    {"native_setScalingMode", "(I)I", (void*) int_no_init},
    {"native_getScalingMode", "()I", (void*) int_no_init},
    {"native_setMeasurementMode", "(I)I", (void*) int_no_init},
    {"native_getMeasurementMode", "()I", (void*) int_no_init},
    {"native_getSamplingRate", "()I", (void*) int_no_init},
    {"native_getWaveForm", "([B)I", (void*) int_no_init},
    {"native_getFft", "([B)I", (void*) int_no_init},
    {"native_getPeakRms", "(Landroid/media/audiofx/Visualizer$MeasurementPeakRms;)I", (void*) int_no_init},
    {"native_setPeriodicCapture", "(IZZ)I", (void*) int_no_init},
};

static const JNINativeMethod kDefaultEffect[] = {
    {"native_setup", "(Ljava/lang/String;Ljava/lang/String;IILjava/lang/String;[I)I", (void*) setup_unsupported},
    {"native_release", "(I)V", (void*) void_nothing},
};

static int bind(JNIEnv* env, const char* cls, const JNINativeMethod* table, int count)
{
    int bound = 0, i;
    jclass c = (*env)->FindClass(env, cls);
    if (c == NULL) { (*env)->ExceptionClear(env); return 0; }
    for (i = 0; i < count; i++) {
        if ((*env)->RegisterNatives(env, c, &table[i], 1) == JNI_OK) bound++;
        else (*env)->ExceptionClear(env);   /* not declared native in this framework */
    }
    (*env)->DeleteLocalRef(env, c);
    return bound;
}

#define N(t) ((int) (sizeof(t) / sizeof((t)[0])))

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv* env = NULL;
    char line[160];
    (void)reserved;
    if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_6) != JNI_OK || env == NULL) return JNI_VERSION_1_6;
    int a = bind(env, "android/media/audiofx/AudioEffect", kAudioEffect, N(kAudioEffect));
    int v = bind(env, "android/media/audiofx/Visualizer", kVisualizer, N(kVisualizer));
    int s = bind(env, "android/media/audiofx/SourceDefaultEffect", kDefaultEffect, N(kDefaultEffect));
    int t = bind(env, "android/media/audiofx/StreamDefaultEffect", kDefaultEffect, N(kDefaultEffect));
    snprintf(line, sizeof(line), "[WL-AUDIOFX] no effect engine: AudioEffect=%d/%d Visualizer=%d/%d default=%d,%d\n",
             a, N(kAudioEffect), v, N(kVisualizer), s, t);
    (void) write(2, line, strlen(line));
    return JNI_VERSION_1_6;
}
