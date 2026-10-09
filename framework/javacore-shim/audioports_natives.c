/*
 * android.media.AudioSystem.listAudioPorts with the board's built-in speaker and microphone, bound
 * from libwl_missing_natives over liboh_android_runtime's empty list (WESTLAKE-679).
 *
 * The empty list made AudioManager.getDevices report no output and no input: an app choosing its
 * output, sizing for the speaker, or checking for a microphone before it records saw a device
 * with neither. The harness's framework-contracts probe measured it (svc:audio: no output
 * devices). OH routes and mixes for the app, so the two ports describe what an app can use, as a
 * phone's do: 16-bit PCM at 48 kHz, stereo out and mono in. listAudioPatches stays empty;
 * AudioManager compares the two lists' generations, and both are 1.
 */
#include <jni.h>
#include <stdio.h>

#define SUCCESS 0
#define ERROR (-1)
#define BAD_VALUE (-2)
/* AudioSystem device types and AudioFormat constants. */
#define DEVICE_OUT_SPEAKER 0x2
#define DEVICE_IN_BUILTIN_MIC ((jint) 0x80000004u)
#define ENCODING_PCM_16BIT 2
#define CHANNEL_OUT_STEREO 12
#define CHANNEL_IN_MONO 16

static jintArray int_array(JNIEnv* env, const jint* values, jsize count)
{
    jintArray array = (*env)->NewIntArray(env, count);
    if (array != NULL && count > 0) (*env)->SetIntArrayRegion(env, array, 0, count, values);
    return array;
}

static jobject new_list(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "java/util/ArrayList");
    if (cls == NULL) return NULL;
    jmethodID init = (*env)->GetMethodID(env, cls, "<init>", "()V");
    jobject list = init != NULL ? (*env)->NewObject(env, cls, init) : NULL;
    (*env)->DeleteLocalRef(env, cls);
    return list;
}

static int add(JNIEnv* env, jobject list, jobject item)
{
    jclass cls = (*env)->GetObjectClass(env, list);
    jmethodID method = (*env)->GetMethodID(env, cls, "add", "(Ljava/lang/Object;)Z");
    (*env)->DeleteLocalRef(env, cls);
    if (method == NULL || item == NULL) return 0;
    (*env)->CallBooleanMethod(env, list, method, item);
    return !(*env)->ExceptionCheck(env);
}

/* new AudioDevicePort(handle, name, [profile], no gains, type, "", no encapsulation, no descriptors).
 * Returns NULL, with the exception pending, at the first JNI call that fails. */
static jobject device_port(JNIEnv* env, jint id, const char* name, jint type, jint channel_mask)
{
    static const jint kRates[] = {48000};
    jclass handle_class = (*env)->FindClass(env, "android/media/AudioHandle");
    if (handle_class == NULL) return NULL;
    jclass profile_class = (*env)->FindClass(env, "android/media/AudioProfile");
    if (profile_class == NULL) return NULL;
    jclass gain_class = (*env)->FindClass(env, "android/media/AudioGain");
    if (gain_class == NULL) return NULL;
    jclass port_class = (*env)->FindClass(env, "android/media/AudioDevicePort");
    if (port_class == NULL) return NULL;
    jmethodID handle_init = (*env)->GetMethodID(env, handle_class, "<init>", "(I)V");
    if (handle_init == NULL) return NULL;
    jmethodID profile_init = (*env)->GetMethodID(env, profile_class, "<init>", "(I[I[I[II)V");
    if (profile_init == NULL) return NULL;
    jmethodID port_init = (*env)->GetMethodID(env, port_class, "<init>",
            "(Landroid/media/AudioHandle;Ljava/lang/String;Ljava/util/List;[Landroid/media/AudioGain;"
            "ILjava/lang/String;[I[ILjava/util/List;)V");
    if (port_init == NULL) return NULL;
    jobject handle = (*env)->NewObject(env, handle_class, handle_init, id);
    if (handle == NULL) return NULL;
    jobject profile = (*env)->NewObject(env, profile_class, profile_init, ENCODING_PCM_16BIT,
            int_array(env, kRates, 1), int_array(env, &channel_mask, 1), int_array(env, NULL, 0), 0);
    if (profile == NULL) return NULL;
    jobject profiles = new_list(env);
    if (profiles == NULL || !add(env, profiles, profile)) return NULL;
    jobject descriptors = new_list(env);
    if (descriptors == NULL) return NULL;
    jobjectArray gains = (*env)->NewObjectArray(env, 0, gain_class, NULL);
    jstring label = (*env)->NewStringUTF(env, name);
    jstring address = (*env)->NewStringUTF(env, "");
    if (gains == NULL || label == NULL || address == NULL) return NULL;
    jobject port = (*env)->NewObject(env, port_class, port_init, handle, label, profiles, gains, type,
            address, int_array(env, NULL, 0), int_array(env, NULL, 0), descriptors);
    return (*env)->ExceptionCheck(env) ? NULL : port;
}

static jint wl_list_audio_ports(JNIEnv* env, jclass cls, jobject ports, jintArray generation)
{
    (void) cls;
    if (ports == NULL) return BAD_VALUE;
    if (generation != NULL && (*env)->GetArrayLength(env, generation) > 0) {
        const jint value = 1;
        (*env)->SetIntArrayRegion(env, generation, 0, 1, &value);
    }
    if ((*env)->PushLocalFrame(env, 64) != JNI_OK) return ERROR;
    jobject speaker = device_port(env, 1, "Speaker", DEVICE_OUT_SPEAKER, CHANNEL_OUT_STEREO);
    jobject mic = speaker != NULL
            ? device_port(env, 2, "Built-In Mic", DEVICE_IN_BUILTIN_MIC, CHANNEL_IN_MONO) : NULL;
    if (mic == NULL || !add(env, ports, speaker) || !add(env, ports, mic)) {
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
        }
        fprintf(stderr, "[WL-AUDIOPORTS] could not build the ports; listing none\n");
        jclass list = (*env)->GetObjectClass(env, ports);
        jmethodID clear = (*env)->GetMethodID(env, list, "clear", "()V");
        if (clear != NULL) (*env)->CallVoidMethod(env, ports, clear);
        (*env)->ExceptionClear(env);
    }
    (*env)->PopLocalFrame(env, NULL);
    return SUCCESS;
}

int wl_register_audio_ports(JNIEnv* env)
{
    static const JNINativeMethod kMethods[] = {
        {"listAudioPorts", "(Ljava/util/ArrayList;[I)I", (void*) wl_list_audio_ports},
    };
    jclass cls = (*env)->FindClass(env, "android/media/AudioSystem");
    if (cls == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    int bound = (*env)->RegisterNatives(env, cls, kMethods, 1) == JNI_OK;
    if (!bound) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, cls);
    return bound;
}
