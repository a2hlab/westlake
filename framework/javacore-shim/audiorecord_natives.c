/*
 * android.media.AudioRecord over OH's audio capturer (libohaudio), bound from libwl_missing_natives.
 *
 * None of AudioRecord's natives was registered, so the first app to size a recording buffer died:
 * Telegram (on its recordQueue thread) and MuseKit (a tuner) on native_get_min_buff_size. This
 * ports the recording path: setup creates an OH capturer (16-bit PCM at the requested rate and
 * channel count; 8-bit and float reads are converted from it), OH's read callback fills a ring
 * buffer, and the read natives drain it -- blocking reads wait for the full count, as AOSP's do,
 * non-blocking reads return what is there. Error codes are AOSP's (AudioRecord.java and
 * android_media_AudioRecord.cpp). Microphone selection, routing callbacks, audio history and
 * position markers answer as a device without those features.
 *
 * The capture itself needs OH's microphone permission for the app process; when OH refuses the
 * capturer, setup fails with AOSP's native-init error and the app sees an uninitialized
 * AudioRecord, as on a device whose microphone is unavailable.
 */
#include <jni.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* AudioRecord / AudioSystem status codes. */
#define SUCCESS 0
#define ERROR (-1)
#define ERROR_BAD_VALUE (-2)
#define ERROR_INVALID_OPERATION (-3)
#define SETUP_ZEROFRAMECOUNT (-16)
#define SETUP_INVALIDCHANNELMASK (-17)
#define SETUP_INVALIDFORMAT (-18)
#define SETUP_NATIVEINITFAILED (-20)
/* AudioFormat encodings. */
#define ENCODING_PCM_16BIT 2
#define ENCODING_PCM_8BIT 3
#define ENCODING_PCM_FLOAT 4

/* The OH capturer API, opened at first use (types from ohaudio headers, opaque here). */
typedef struct OH_AudioStreamBuilder OH_AudioStreamBuilder;
typedef struct OH_AudioCapturer OH_AudioCapturer;
typedef void (*OnReadData)(OH_AudioCapturer*, void*, void*, int32_t);
static struct {
    int loaded;
    int (*create)(OH_AudioStreamBuilder**, int);
    int (*setRate)(OH_AudioStreamBuilder*, int32_t);
    int (*setChannels)(OH_AudioStreamBuilder*, int32_t);
    int (*setFormat)(OH_AudioStreamBuilder*, int);
    int (*setInfo)(OH_AudioStreamBuilder*, int);
    int (*setReadCallback)(OH_AudioStreamBuilder*, OnReadData, void*);
    int (*generate)(OH_AudioStreamBuilder*, OH_AudioCapturer**);
    int (*destroyBuilder)(OH_AudioStreamBuilder*);
    int (*start)(OH_AudioCapturer*);
    int (*stop)(OH_AudioCapturer*);
    int (*release)(OH_AudioCapturer*);
} oh;
static pthread_mutex_t g_load = PTHREAD_MUTEX_INITIALIZER;

static int load_capturer(void)
{
    void* lib;
    pthread_mutex_lock(&g_load);
    if (oh.loaded == 0) {
        oh.loaded = -1;
        lib = dlopen("libohaudio.so", RTLD_NOW);
        if (lib != NULL) {
#define S(f, n) (*(void**) &oh.f = dlsym(lib, n))
            S(create, "OH_AudioStreamBuilder_Create");
            S(setRate, "OH_AudioStreamBuilder_SetSamplingRate");
            S(setChannels, "OH_AudioStreamBuilder_SetChannelCount");
            S(setFormat, "OH_AudioStreamBuilder_SetSampleFormat");
            S(setInfo, "OH_AudioStreamBuilder_SetCapturerInfo");
            S(setReadCallback, "OH_AudioStreamBuilder_SetCapturerReadDataCallback");
            S(generate, "OH_AudioStreamBuilder_GenerateCapturer");
            S(destroyBuilder, "OH_AudioStreamBuilder_Destroy");
            S(start, "OH_AudioCapturer_Start");
            S(stop, "OH_AudioCapturer_Stop");
            S(release, "OH_AudioCapturer_Release");
#undef S
            if (oh.create && oh.setRate && oh.setChannels && oh.setFormat && oh.setInfo && oh.setReadCallback
                    && oh.generate && oh.destroyBuilder && oh.start && oh.stop && oh.release) {
                oh.loaded = 1;
            }
        }
    }
    pthread_mutex_unlock(&g_load);
    return oh.loaded > 0;
}

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t data;
    OH_AudioCapturer* capturer;
    uint8_t* ring;        /* 16-bit PCM */
    size_t cap, head, count;
    int channels, rate, encoding;
    size_t buffer_bytes;  /* in the app's encoding */
    int64_t frames_captured;
    int recording, released;
} Rec;

static jfieldID g_handle;

static Rec* rec_of(JNIEnv* env, jobject thiz)
{
    return (Rec*) (intptr_t) (*env)->GetLongField(env, thiz, g_handle);
}

static void on_read(OH_AudioCapturer* capturer, void* user, void* buffer, int32_t size)
{
    Rec* r = (Rec*) user;
    size_t n = size > 0 ? (size_t) size : 0, i;
    const uint8_t* in = buffer;
    (void) capturer;
    pthread_mutex_lock(&r->lock);
    for (i = 0; i < n; i++) {               /* when full, the oldest audio is dropped (an overrun) */
        r->ring[(r->head + r->count) % r->cap] = in[i];
        if (r->count < r->cap) r->count++;
        else r->head = (r->head + 1) % r->cap;
    }
    r->frames_captured += (int64_t) (n / (2 * (size_t) r->channels));
    pthread_cond_broadcast(&r->data);
    pthread_mutex_unlock(&r->lock);
}

static int bytes_per_sample(int encoding)
{
    return encoding == ENCODING_PCM_8BIT ? 1 : encoding == ENCODING_PCM_FLOAT ? 4 : 2;
}

static jint get_min_buff_size(JNIEnv* env, jclass c, jint rate, jint channels, jint encoding)
{
    (void) env; (void) c;
    if (rate < 4000 || rate > 192000 || channels < 1 || channels > 2) return 0;   /* BAD_VALUE -> 0 */
    if (encoding != ENCODING_PCM_16BIT && encoding != ENCODING_PCM_8BIT && encoding != ENCODING_PCM_FLOAT) return 0;
    return (rate / 25) * channels * bytes_per_sample(encoding);                   /* 40 ms */
}

static jint setup(JNIEnv* env, jobject thiz, jobject weak, jobject attributes, jintArray jRate, jint channelMask,
                  jint channelIndexMask, jint encoding, jint bufferBytes, jintArray jSession, jobject attribution,
                  jlong nativeRecord, jint maxHistoryMs, jint halFlags)
{
    Rec* r;
    OH_AudioStreamBuilder* builder = NULL;
    jint rate = 0, session = 0;
    int channels;
    (void) weak; (void) attributes; (void) attribution; (void) maxHistoryMs; (void) halFlags;
    if (nativeRecord != 0) return ERROR;    /* wrapping an existing native AudioRecord: not ported */
    channels = __builtin_popcount((unsigned) channelMask) + __builtin_popcount((unsigned) channelIndexMask);
    if (channels < 1 || channels > 2) return SETUP_INVALIDCHANNELMASK;
    if (encoding != ENCODING_PCM_16BIT && encoding != ENCODING_PCM_8BIT && encoding != ENCODING_PCM_FLOAT) {
        return SETUP_INVALIDFORMAT;
    }
    if (bufferBytes <= 0) return SETUP_ZEROFRAMECOUNT;
    if (jRate != NULL) (*env)->GetIntArrayRegion(env, jRate, 0, 1, &rate);
    if (rate <= 0) rate = 48000;
    if (!load_capturer()) return SETUP_NATIVEINITFAILED;
    r = calloc(1, sizeof(*r));
    if (r == NULL) return SETUP_NATIVEINITFAILED;
    r->channels = channels;
    r->rate = rate;
    r->encoding = encoding;
    r->buffer_bytes = (size_t) bufferBytes;
    r->cap = (size_t) rate * (size_t) channels * 2;   /* one second of 16-bit audio */
    r->ring = malloc(r->cap);
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->data, NULL);
    if (r->ring == NULL || oh.create(&builder, 2 /* AUDIOSTREAM_TYPE_CAPTURER */) != 0) goto fail;
    oh.setRate(builder, rate);
    oh.setChannels(builder, channels);
    oh.setFormat(builder, 1 /* AUDIOSTREAM_SAMPLE_S16LE */);
    oh.setInfo(builder, 0 /* AUDIOSTREAM_SOURCE_TYPE_MIC */);
    oh.setReadCallback(builder, on_read, r);
    int rc = oh.generate(builder, &r->capturer);
    if (rc != 0 || r->capturer == NULL) {
        /* Seen: the host HAP does not request ohos.permission.MICROPHONE, so OH refuses capture. */
        fprintf(stderr, "[WL-MEDIA] AudioRecord: OH refused the capturer (rc=%d)\n", rc);
        goto fail;
    }
    oh.destroyBuilder(builder);
    if (jSession != NULL) {
        (*env)->GetIntArrayRegion(env, jSession, 0, 1, &session);
        if (session == 0) session = 1;
        (*env)->SetIntArrayRegion(env, jSession, 0, 1, &session);
    }
    if (jRate != NULL) (*env)->SetIntArrayRegion(env, jRate, 0, 1, &rate);
    (*env)->SetLongField(env, thiz, g_handle, (jlong) (intptr_t) r);
    return SUCCESS;
fail:
    if (builder != NULL) oh.destroyBuilder(builder);
    free(r->ring);
    pthread_cond_destroy(&r->data);
    pthread_mutex_destroy(&r->lock);
    free(r);
    return SETUP_NATIVEINITFAILED;
}

static jint start(JNIEnv* env, jobject thiz, jint syncEvent, jint session)
{
    Rec* r = rec_of(env, thiz);
    (void) syncEvent; (void) session;
    if (r == NULL) return ERROR_INVALID_OPERATION;
    if (oh.start(r->capturer) != 0) return ERROR;
    pthread_mutex_lock(&r->lock);
    r->recording = 1;
    pthread_mutex_unlock(&r->lock);
    return SUCCESS;
}

static void stop(JNIEnv* env, jobject thiz)
{
    Rec* r = rec_of(env, thiz);
    if (r == NULL) return;
    oh.stop(r->capturer);
    pthread_mutex_lock(&r->lock);
    r->recording = 0;
    pthread_cond_broadcast(&r->data);   /* a blocked read returns what it has */
    pthread_mutex_unlock(&r->lock);
}

static void release(JNIEnv* env, jobject thiz)
{
    Rec* r = rec_of(env, thiz);
    if (r == NULL) return;
    (*env)->SetLongField(env, thiz, g_handle, 0);
    oh.stop(r->capturer);
    oh.release(r->capturer);
    pthread_mutex_lock(&r->lock);
    r->released = 1;
    pthread_cond_broadcast(&r->data);
    pthread_mutex_unlock(&r->lock);
    free(r->ring);
    pthread_cond_destroy(&r->data);
    pthread_mutex_destroy(&r->lock);
    free(r);
}

/* Takes up to `samples` 16-bit samples into out; blocking waits for all of them while recording. */
static size_t take(Rec* r, int16_t* out, size_t samples, int blocking)
{
    size_t got = 0;
    pthread_mutex_lock(&r->lock);
    while (got < samples) {
        while (r->count >= 2 && got < samples) {
            uint8_t lo = r->ring[r->head], hi = r->ring[(r->head + 1) % r->cap];
            out[got++] = (int16_t) (lo | (hi << 8));
            r->head = (r->head + 2) % r->cap;
            r->count -= 2;
        }
        if (got == samples || !blocking || !r->recording || r->released) break;
        pthread_cond_wait(&r->data, &r->lock);
    }
    pthread_mutex_unlock(&r->lock);
    return got;
}

static jint read_generic(JNIEnv* env, jobject thiz, void* dest, int encoding, jint samples, jboolean blocking)
{
    Rec* r = rec_of(env, thiz);
    int16_t* tmp;
    size_t got, i;
    if (r == NULL) return ERROR_INVALID_OPERATION;
    if (samples <= 0) return 0;
    tmp = malloc((size_t) samples * sizeof(int16_t));
    if (tmp == NULL) return ERROR;
    got = take(r, tmp, (size_t) samples, blocking);
    for (i = 0; i < got; i++) {
        if (encoding == ENCODING_PCM_8BIT) ((uint8_t*) dest)[i] = (uint8_t) ((tmp[i] >> 8) + 128);
        else if (encoding == ENCODING_PCM_FLOAT) ((float*) dest)[i] = (float) tmp[i] / 32768.f;
        else ((int16_t*) dest)[i] = tmp[i];
    }
    free(tmp);
    return (jint) got;
}

static jint read_bytes(JNIEnv* env, jobject thiz, jbyteArray a, jint off, jint size, jboolean blocking)
{
    Rec* r = rec_of(env, thiz);
    jbyte* data;
    jint n;
    if (r == NULL) return ERROR_INVALID_OPERATION;
    if (a == NULL || off < 0 || size < 0 || off + size > (*env)->GetArrayLength(env, a)) return ERROR_BAD_VALUE;
    data = (*env)->GetByteArrayElements(env, a, NULL);
    if (data == NULL) return ERROR;
    if (r->encoding == ENCODING_PCM_8BIT) {
        n = read_generic(env, thiz, data + off, ENCODING_PCM_8BIT, size, blocking);
    } else {
        n = read_generic(env, thiz, data + off, ENCODING_PCM_16BIT, size / 2, blocking);
        if (n > 0) n *= 2;   /* samples -> bytes; error codes pass through */
    }
    (*env)->ReleaseByteArrayElements(env, a, data, 0);
    return n;
}

static jint read_shorts(JNIEnv* env, jobject thiz, jshortArray a, jint off, jint size, jboolean blocking)
{
    jshort* data;
    jint n;
    if (rec_of(env, thiz) == NULL) return ERROR_INVALID_OPERATION;
    if (a == NULL || off < 0 || size < 0 || off + size > (*env)->GetArrayLength(env, a)) return ERROR_BAD_VALUE;
    data = (*env)->GetShortArrayElements(env, a, NULL);
    if (data == NULL) return ERROR;
    n = read_generic(env, thiz, data + off, ENCODING_PCM_16BIT, size, blocking);
    (*env)->ReleaseShortArrayElements(env, a, data, 0);
    return n;
}

static jint read_floats(JNIEnv* env, jobject thiz, jfloatArray a, jint off, jint size, jboolean blocking)
{
    jfloat* data;
    jint n;
    if (rec_of(env, thiz) == NULL) return ERROR_INVALID_OPERATION;
    if (a == NULL || off < 0 || size < 0 || off + size > (*env)->GetArrayLength(env, a)) return ERROR_BAD_VALUE;
    data = (*env)->GetFloatArrayElements(env, a, NULL);
    if (data == NULL) return ERROR;
    n = read_generic(env, thiz, data + off, ENCODING_PCM_FLOAT, size, blocking);
    (*env)->ReleaseFloatArrayElements(env, a, data, 0);
    return n;
}

static jint read_direct(JNIEnv* env, jobject thiz, jobject buffer, jint size, jboolean blocking)
{
    Rec* r = rec_of(env, thiz);
    void* base;
    int bps;
    if (r == NULL) return ERROR_INVALID_OPERATION;
    base = buffer != NULL ? (*env)->GetDirectBufferAddress(env, buffer) : NULL;
    if (base == NULL) return ERROR_BAD_VALUE;
    bps = bytes_per_sample(r->encoding);
    {
        jint n = read_generic(env, thiz, base, r->encoding, size / bps, blocking);
        return n < 0 ? n : n * bps;
    }
}

static jint buffer_frames(JNIEnv* env, jobject thiz)
{
    Rec* r = rec_of(env, thiz);
    if (r == NULL) return ERROR_INVALID_OPERATION;
    return (jint) (r->buffer_bytes / ((size_t) r->channels * (size_t) bytes_per_sample(r->encoding)));
}

static jint get_timestamp(JNIEnv* env, jobject thiz, jobject timestamp, jint timebase)
{
    Rec* r = rec_of(env, thiz);
    jclass c;
    jfieldID pos, nanos;
    struct timespec ts;
    (void) timebase;
    if (r == NULL || timestamp == NULL) return ERROR_INVALID_OPERATION;
    c = (*env)->GetObjectClass(env, timestamp);
    pos = (*env)->GetFieldID(env, c, "framePosition", "J");
    nanos = (*env)->GetFieldID(env, c, "nanoTime", "J");
    (*env)->DeleteLocalRef(env, c);
    if (pos == NULL || nanos == NULL) { (*env)->ExceptionClear(env); return ERROR; }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    pthread_mutex_lock(&r->lock);
    (*env)->SetLongField(env, timestamp, pos, r->frames_captured);
    pthread_mutex_unlock(&r->lock);
    (*env)->SetLongField(env, timestamp, nanos, (jlong) ts.tv_sec * 1000000000LL + ts.tv_nsec);
    return SUCCESS;
}

static jint zero(JNIEnv* env, jobject thiz) { (void) env; (void) thiz; return 0; }
static jint zero_i(JNIEnv* env, jobject thiz, jint a) { (void) env; (void) thiz; (void) a; return SUCCESS; }
static jint invalid_i(JNIEnv* env, jobject thiz, jint a) { (void) env; (void) thiz; (void) a; return ERROR_INVALID_OPERATION; }
static jint invalid_f(JNIEnv* env, jobject thiz, jfloat a) { (void) env; (void) thiz; (void) a; return ERROR_INVALID_OPERATION; }
static jint invalid_sj(JNIEnv* env, jobject thiz, jstring a, jlong b) { (void) env; (void) thiz; (void) a; (void) b; return ERROR_INVALID_OPERATION; }
static jint mics_none(JNIEnv* env, jobject thiz, jobject list) { (void) env; (void) thiz; (void) list; return SUCCESS; }
static jboolean false_i(JNIEnv* env, jobject thiz, jint a) { (void) env; (void) thiz; (void) a; return JNI_FALSE; }
static void noop(JNIEnv* env, jobject thiz) { (void) env; (void) thiz; }
static void noop_s(JNIEnv* env, jobject thiz, jstring a) { (void) env; (void) thiz; (void) a; }
static jobject null_metrics(JNIEnv* env, jobject thiz) { (void) env; (void) thiz; return NULL; }

static const JNINativeMethod kAudioRecord[] = {
    { "native_get_min_buff_size", "(III)I", (void*) get_min_buff_size },
    { "native_setup", "(Ljava/lang/Object;Ljava/lang/Object;[IIIII[ILandroid/os/Parcel;JII)I", (void*) setup },
    { "native_start", "(II)I", (void*) start },
    { "native_stop", "()V", (void*) stop },
    { "native_release", "()V", (void*) release },
    { "native_finalize", "()V", (void*) release },
    { "native_read_in_byte_array", "([BIIZ)I", (void*) read_bytes },
    { "native_read_in_short_array", "([SIIZ)I", (void*) read_shorts },
    { "native_read_in_float_array", "([FIIZ)I", (void*) read_floats },
    { "native_read_in_direct_buffer", "(Ljava/lang/Object;IZ)I", (void*) read_direct },
    { "native_get_buffer_size_in_frames", "()I", (void*) buffer_frames },
    { "native_get_timestamp", "(Landroid/media/AudioTimestamp;I)I", (void*) get_timestamp },
    { "native_get_marker_pos", "()I", (void*) zero },
    { "native_set_marker_pos", "(I)I", (void*) zero_i },
    { "native_get_pos_update_period", "()I", (void*) zero },
    { "native_set_pos_update_period", "(I)I", (void*) zero_i },
    { "native_getRoutedDeviceId", "()I", (void*) zero },
    { "native_getPortId", "()I", (void*) zero },
    { "native_setInputDevice", "(I)Z", (void*) false_i },
    { "native_enableDeviceCallback", "()V", (void*) noop },
    { "native_disableDeviceCallback", "()V", (void*) noop },
    { "native_get_active_microphones", "(Ljava/util/ArrayList;)I", (void*) mics_none },
    { "native_set_preferred_microphone_direction", "(I)I", (void*) invalid_i },
    { "native_set_preferred_microphone_field_dimension", "(F)I", (void*) invalid_f },
    { "native_shareAudioHistory", "(Ljava/lang/String;J)I", (void*) invalid_sj },
    { "native_setLogSessionId", "(Ljava/lang/String;)V", (void*) noop_s },
    { "native_getMetrics", "()Landroid/os/PersistableBundle;", (void*) null_metrics },
};

/* Natives first: GetFieldID initializes AudioRecord. Returns how many were bound. */
int wl_register_audiorecord(JNIEnv* env)
{
    jclass c = (*env)->FindClass(env, "android/media/AudioRecord");
    int bound = 0, i;
    if (c == NULL) { (*env)->ExceptionClear(env); return 0; }
    for (i = 0; i < (int) (sizeof(kAudioRecord) / sizeof(kAudioRecord[0])); i++) {
        if ((*env)->RegisterNatives(env, c, &kAudioRecord[i], 1) == JNI_OK) bound++;
        else (*env)->ExceptionClear(env);
    }
    g_handle = (*env)->GetFieldID(env, c, "mNativeAudioRecordHandle", "J");
    if (g_handle == NULL) {
        (*env)->ExceptionClear(env);
        (*env)->UnregisterNatives(env, c);
        bound = 0;
    }
    (*env)->DeleteLocalRef(env, c);
    return bound;
}
