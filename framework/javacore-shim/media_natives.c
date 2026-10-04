/*
 * android.media natives the runtime declares but never registers, bound from
 * libwl_missing_natives' JNI_OnLoad.
 *
 *   MediaPlayer -- none of its 49 natives is registered, so its static initializer (native_init)
 *   throws and the first class that touches MediaPlayer dies with it: ntfy during bind, Astro Loop
 *   and Doors of Doom before a first frame. It is ported onto OH's AVPlayer (libavplayer.so), which
 *   has the same shape: a data source (fd range or URL), an asynchronous prepare, play / pause /
 *   stop / seek / reset / release, looping and volume, and state-change callbacks that become the
 *   MediaPlayer events (prepared, playback complete, seek complete, video size, error). Android's
 *   state rules are kept here rather than inherited from OH's, because the two differ at the edges
 *   (stop on a stopped player is a no-op in Android and an error in OH) and apps rely on Android's.
 *   Not ported: video output (the Surface is not handed to OH, so video plays as audio only),
 *   MediaDataSource sources, DRM, volume shapers, metadata, and the invoke() calls that add or
 *   select tracks; each answers the way AOSP's JNI answers when its player service reports failure.
 *   The invoke() queries every prepared player gets are answered: MediaPlayer asks for the track
 *   list on MEDIA_PREPARED and turns a failure into MEDIA_ERROR (1, -1010) -- which Doors of Doom
 *   handled by leaving its game.
 *
 *   AudioTrack.native_get_buffer_size_frames / native_set_buffer_size_frames /
 *   native_get_buffer_capacity_frames -- the AudioTrack shim registers the playback natives but not
 *   these. AudioTrack.getBufferSizeInFrames reached the first one in MuseKit before a first frame.
 *   The OH renderer's buffer is fixed when it is created, so the size is the one AudioTrack sized
 *   it with (mNativeBufferSizeInFrames), and a request to change it answers with that unchanged
 *   size -- which AudioTrack.setBufferSizeInFrames documents as a valid result.
 *
 *   AudioTrack.native_set_loop -- setLoopPoints on a static track (MuseKit, at startup). The shim
 *   plays through a streaming OH renderer and has no static buffer to loop, so the answer is
 *   AOSP's for a track that cannot loop, ERROR_INVALID_OPERATION: the sound plays once. That is
 *   an approximation, not looping, and stays an open gap.
 *
 *   AudioTrack.native_write_native_bytes -- ByteBuffer writes, ExoPlayer's path. The AudioTrack shim
 *   in the native bridge plays 16-bit PCM and converts float[] writes, but passed a ByteBuffer's
 *   bytes through whatever the encoding: float PCM played as loud noise at twice the length
 *   (probes/audio-tone: a 2500 Hz float tone came out as broadband noise, 7 s for 4). The
 *   bridge's library cannot be rebuilt here, so this binding replaces its ByteBuffer write: it
 *   converts float and 8-bit PCM to 16-bit and hands the samples to the bridge's own 16-bit
 *   write (native_write_short), returning the bytes of the caller's encoding it consumed.
 *
 *   MediaRecorder -- its static initializer calls native_init, so even reading
 *   MediaRecorder.AudioSource constants died (MuseKit; the runtime-answers probe). native_init is a
 *   no-op; constructing a recorder throws what AOSP's JNI throws when the recorder cannot be
 *   initialised ("Unable to initialize media recorder"): recording video or audio through
 *   MediaRecorder is not ported. AudioRecord is (audiorecord_natives.c).
 *
 *   MediaDrm / MediaCrypto -- none of their natives is registered, so MediaDrm's static initializer
 *   threw and apps that only ask which DRM schemes exist died at startup (X, Uber). There is no DRM
 *   plugin here, and these answer what AOSP's JNI answers on a device without one: no scheme is
 *   supported, the scheme list is empty, and constructing either object throws the exception its
 *   Java API documents (UnsupportedSchemeException, MediaCryptoException). No instance can exist,
 *   so the per-instance natives are never reached; release and finalize are bound anyway.
 *
 *   AudioSystem.getParameters / setParameters -- vendor key/value strings passed to the audio HAL.
 *   ExoPlayer asks for one on its playback thread (Mouse Pounce). No key is known here, which a
 *   HAL answers with an empty string; setParameters accepts and ignores, as HALs do for keys they
 *   do not recognise.
 *
 *   AudioSystem's output-capability queries -- offload, direct (passthrough) playback, direct
 *   profiles, hardware A/V sync, haptics, microphones, Bluetooth offload formats. ExoPlayer asks
 *   them on its playback thread to choose an output path; Mouse Pounce reached the offload one next.
 *   Output here is the OH renderer behind the AudioTrack shim, which does PCM only, so each answers
 *   what AOSP answers for an output without the capability: not supported, or an empty list.
 *
 * libavplayer.so and libnative_media_core.so are opened on first use, not linked, so this library
 * still loads -- and its other natives still bind -- on a system without them.
 */
#include <jni.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static JavaVM* g_vm;

static void mlog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void mlog(const char* fmt, ...)
{
    char line[256];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int) sizeof(line) - 2) n = (int) sizeof(line) - 2;
    line[n++] = '\n';
    (void) write(2, line, (size_t) n);
}

/* ---- AudioTrack buffer size ---------------------------------------------------------------- */

static jint wl_track_frames(JNIEnv* env, jobject thiz)
{
    jclass c = (*env)->GetObjectClass(env, thiz);
    jfieldID f = (*env)->GetFieldID(env, c, "mNativeBufferSizeInFrames", "I");
    (*env)->DeleteLocalRef(env, c);
    if (f == NULL) {
        (*env)->ExceptionClear(env);
        return -38; /* AudioTrack.ERROR_INVALID_OPERATION */
    }
    return (*env)->GetIntField(env, thiz, f);
}

static jint wl_track_set_frames(JNIEnv* env, jobject thiz, jint frames)
{
    (void) frames;
    return wl_track_frames(env, thiz);
}

static jint wl_track_set_loop(JNIEnv* env, jobject thiz, jint start, jint end, jint count)
{
    (void) env; (void) thiz; (void) start; (void) end; (void) count;
    return -3; /* AudioTrack.ERROR_INVALID_OPERATION */
}

static jmethodID g_write_short;

static jint wl_track_write_bytes(JNIEnv* env, jobject thiz, jobject buffer, jint position, jint size,
                                 jint format, jboolean blocking)
{
    const uint8_t* base;
    int bytes_per_sample = format == 4 ? 4 : format == 3 ? 1 : 2; /* FLOAT, 8BIT, else 16BIT */
    jint samples, written;
    jshortArray array;
    jshort* out;
    if (g_write_short == NULL || buffer == NULL || size < 0) return -2;          /* ERROR_BAD_VALUE */
    base = (const uint8_t*) (*env)->GetDirectBufferAddress(env, buffer);
    if (base == NULL) return -1;                                                 /* ERROR */
    base += position;
    samples = size / bytes_per_sample;
    if (samples == 0) return 0;
    array = (*env)->NewShortArray(env, samples);
    if (array == NULL) return -1;
    out = (*env)->GetShortArrayElements(env, array, NULL);
    if (out == NULL) { (*env)->DeleteLocalRef(env, array); return -1; }
    for (jint i = 0; i < samples; i++) {
        if (format == 4) {
            float v;
            memcpy(&v, base + 4 * i, sizeof(v));
            v = v > 1.f ? 1.f : v < -1.f ? -1.f : v;
            out[i] = (jshort) (v * 32767.f);
        } else if (format == 3) {
            out[i] = (jshort) (((int) base[i] - 128) << 8);
        } else {
            memcpy(&out[i], base + 2 * i, sizeof(jshort));
        }
    }
    (*env)->ReleaseShortArrayElements(env, array, out, 0);
    written = (*env)->CallIntMethod(env, thiz, g_write_short, array, 0, samples, 2 /* 16BIT */, blocking);
    (*env)->DeleteLocalRef(env, array);
    if ((*env)->ExceptionCheck(env)) return -1;
    return written < 0 ? written : written * bytes_per_sample;
}

static const JNINativeMethod kAudioTrack[] = {
    { "native_write_native_bytes", "(Ljava/nio/ByteBuffer;IIIZ)I", (void*) wl_track_write_bytes },
    { "native_set_loop", "(III)I", (void*) wl_track_set_loop },
    { "native_get_buffer_size_frames", "()I", (void*) wl_track_frames },
    { "native_set_buffer_size_frames", "(I)I", (void*) wl_track_set_frames },
    { "native_get_buffer_capacity_frames", "()I", (void*) wl_track_frames },
};

/* ---- AudioSystem parameters ---------------------------------------------------------------- */

static jstring wl_audio_get_parameters(JNIEnv* env, jclass c, jstring keys)
{
    (void) c; (void) keys;
    return (*env)->NewStringUTF(env, "");
}

static jint wl_audio_set_parameters(JNIEnv* env, jclass c, jstring pairs)
{
    (void) env; (void) c; (void) pairs;
    return 0; /* AudioSystem.SUCCESS */
}

static jint wl_audio_zero_iiiii(JNIEnv* env, jclass c, jint a, jint b, jint d, jint e, jint f)
{ (void) env; (void) c; (void) a; (void) b; (void) d; (void) e; (void) f; return 0; }
static jint wl_audio_zero_oo(JNIEnv* env, jclass c, jobject a, jobject b)
{ (void) env; (void) c; (void) a; (void) b; return 0; }
static jint wl_audio_zero_i(JNIEnv* env, jclass c, jint a)
{ (void) env; (void) c; (void) a; return 0; }
static jint wl_audio_zero_o(JNIEnv* env, jclass c, jobject a)
{ (void) env; (void) c; (void) a; return 0; }
static jint wl_audio_zero_io(JNIEnv* env, jclass c, jint a, jobject b)
{ (void) env; (void) c; (void) a; (void) b; return 0; }
static jboolean wl_audio_false(JNIEnv* env, jclass c)
{ (void) env; (void) c; return JNI_FALSE; }

static const JNINativeMethod kAudioSystem[] = {
    { "getParameters", "(Ljava/lang/String;)Ljava/lang/String;", (void*) wl_audio_get_parameters },
    { "setParameters", "(Ljava/lang/String;)I", (void*) wl_audio_set_parameters },
    /* AudioSystem.OFFLOAD_NOT_SUPPORTED */
    { "native_get_offload_support", "(IIIII)I", (void*) wl_audio_zero_iiiii },
    /* AudioSystem.DIRECT_NOT_SUPPORTED */
    { "getDirectPlaybackSupport", "(Landroid/media/AudioFormat;Landroid/media/AudioAttributes;)I",
      (void*) wl_audio_zero_oo },
    /* SUCCESS, the list left empty */
    { "getDirectProfilesForAttributes", "(Landroid/media/AudioAttributes;Ljava/util/ArrayList;)I",
      (void*) wl_audio_zero_oo },
    { "getMicrophones", "(Ljava/util/ArrayList;)I", (void*) wl_audio_zero_o },
    { "getHwOffloadFormatsSupportedForBluetoothMedia", "(ILjava/util/ArrayList;)I",
      (void*) wl_audio_zero_io },
    /* AudioSystem.AUDIO_HW_SYNC_INVALID */
    { "getAudioHwSyncForSession", "(I)I", (void*) wl_audio_zero_i },
    { "isHapticPlaybackSupported", "()Z", (void*) wl_audio_false },
};

/* ---- MediaDrm / MediaCrypto: a device without a DRM plugin ------------------------------ */

static void drm_throw(JNIEnv* env, const char* cls, const char* msg)
{
    jclass c = (*env)->FindClass(env, cls);
    if (c != NULL) (*env)->ThrowNew(env, c, msg);
}

static void drm_init(JNIEnv* env, jclass c) { (void) env; (void) c; }
static void drm_release(JNIEnv* env, jobject thiz) { (void) env; (void) thiz; }
static void drm_setup(JNIEnv* env, jobject thiz, jobject weak, jbyteArray uuid, jstring package)
{
    (void) thiz; (void) weak; (void) uuid; (void) package;
    drm_throw(env, "android/media/UnsupportedSchemeException", "Failed to instantiate drm object.");
}
static jboolean drm_scheme_supported(JNIEnv* env, jclass c, jbyteArray uuid, jstring mime, jint level)
{ (void) env; (void) c; (void) uuid; (void) mime; (void) level; return JNI_FALSE; }
static jbyteArray drm_schemes(JNIEnv* env, jclass c) { (void) c; return (*env)->NewByteArray(env, 0); }

static const JNINativeMethod kMediaDrm[] = {
    { "native_init", "()V", (void*) drm_init },
    { "native_setup", "(Ljava/lang/Object;[BLjava/lang/String;)V", (void*) drm_setup },
    { "native_release", "()V", (void*) drm_release },
    { "isCryptoSchemeSupportedNative", "([BLjava/lang/String;I)Z", (void*) drm_scheme_supported },
    { "getSupportedCryptoSchemesNative", "()[B", (void*) drm_schemes },
};

static void crypto_setup(JNIEnv* env, jobject thiz, jbyteArray uuid, jbyteArray data)
{
    (void) thiz; (void) uuid; (void) data;
    drm_throw(env, "android/media/MediaCryptoException", "Failed to instantiate crypto object.");
}
static jboolean crypto_scheme_supported(JNIEnv* env, jclass c, jbyteArray uuid)
{ (void) env; (void) c; (void) uuid; return JNI_FALSE; }

static void recorder_setup(JNIEnv* env, jobject thiz, jobject weak, jstring package, jobject attribution)
{
    (void) thiz; (void) weak; (void) package; (void) attribution;
    drm_throw(env, "java/lang/RuntimeException", "Unable to initialize media recorder");
}

static const JNINativeMethod kMediaRecorder[] = {
    { "native_init", "()V", (void*) drm_init },
    { "native_setup", "(Ljava/lang/Object;Ljava/lang/String;Landroid/os/Parcel;)V", (void*) recorder_setup },
    { "native_finalize", "()V", (void*) drm_release },
    { "release", "()V", (void*) drm_release },
};

static const JNINativeMethod kMediaCrypto[] = {
    { "native_init", "()V", (void*) drm_init },
    { "native_setup", "([B[B)V", (void*) crypto_setup },
    { "native_finalize", "()V", (void*) drm_release },
    { "release", "()V", (void*) drm_release },
    { "isCryptoSchemeSupportedNative", "([B)Z", (void*) crypto_scheme_supported },
};

/* ---- MediaPlayer over OH AVPlayer ---------------------------------------------------------- */

/* The OH types are opaque here; only the calls below are used. From avplayer_base.h. */
typedef struct OH_AVPlayer OH_AVPlayer;
typedef struct OH_AVFormat OH_AVFormat;
enum { AV_IDLE = 0, AV_INITIALIZED = 1, AV_PREPARED = 2, AV_PLAYING = 3, AV_PAUSED = 4,
       AV_STOPPED = 5, AV_COMPLETED = 6, AV_RELEASED = 7, AV_ERROR = 8 };
enum { AV_INFO_TYPE_SEEKDONE = 0, AV_INFO_TYPE_STATE_CHANGE = 4,
       AV_INFO_TYPE_RESOLUTION_CHANGE = 8 };
typedef struct OH_AVBuffer OH_AVBuffer;
typedef struct {
    int64_t size;
    int32_t (*readAt)(OH_AVBuffer* data, int32_t length, int64_t pos, void* userData);
} DataSource;                                   /* OH_AVDataSourceExt */
typedef void (*OnInfo)(OH_AVPlayer*, int, OH_AVFormat*, void*);
typedef void (*OnError)(OH_AVPlayer*, int32_t, const char*, void*);

static struct {
    int loaded;
    OH_AVPlayer* (*create)(void);
    int (*setUrl)(OH_AVPlayer*, const char*);
    int (*setFd)(OH_AVPlayer*, int32_t, int64_t, int64_t);
    int (*setDataSource)(OH_AVPlayer*, DataSource*, void*);   /* optional: API 21 */
    uint8_t* (*bufferAddr)(OH_AVBuffer*);
    int (*prepare)(OH_AVPlayer*);
    int (*play)(OH_AVPlayer*);
    int (*pause)(OH_AVPlayer*);
    int (*stop)(OH_AVPlayer*);
    int (*reset)(OH_AVPlayer*);
    int (*releaseSync)(OH_AVPlayer*);
    int (*setVolume)(OH_AVPlayer*, float, float);
    int (*seek)(OH_AVPlayer*, int32_t, int);
    int (*getCurrentTime)(OH_AVPlayer*, int32_t*);
    int (*getDuration)(OH_AVPlayer*, int32_t*);
    int (*getVideoWidth)(OH_AVPlayer*, int32_t*);
    int (*getVideoHeight)(OH_AVPlayer*, int32_t*);
    int (*setLooping)(OH_AVPlayer*, int);
    int (*setRate)(OH_AVPlayer*, float);          /* optional: API 20 */
    int (*setRendererInfo)(OH_AVPlayer*, int);    /* optional */
    int (*setOnInfo)(OH_AVPlayer*, OnInfo, void*);
    int (*setOnError)(OH_AVPlayer*, OnError, void*);
    int (*formatGetInt)(OH_AVFormat*, const char*, int32_t*);
    const char* const* keyState;
    const char* const* keyWidth;
    const char* const* keyHeight;
} oh;

static pthread_mutex_t g_load_lock = PTHREAD_MUTEX_INITIALIZER;

static int load_oh_player(void)
{
    void* p;
    void* core;
    pthread_mutex_lock(&g_load_lock);
    if (oh.loaded != 0) {
        pthread_mutex_unlock(&g_load_lock);
        return oh.loaded > 0;
    }
    oh.loaded = -1;
    p = dlopen("libavplayer.so", RTLD_NOW);
    core = dlopen("libnative_media_core.so", RTLD_NOW);
    if (p == NULL || core == NULL) {
        mlog("[WL-MEDIA] OH player unavailable: %s", dlerror());
        pthread_mutex_unlock(&g_load_lock);
        return 0;
    }
#define SYM(field, lib, name) (*(void**) &oh.field = dlsym(lib, name))
    SYM(create, p, "OH_AVPlayer_Create");
    SYM(setUrl, p, "OH_AVPlayer_SetURLSource");
    SYM(setFd, p, "OH_AVPlayer_SetFDSource");
    SYM(setDataSource, p, "OH_AVPlayer_SetDataSource");
    SYM(bufferAddr, core, "OH_AVBuffer_GetAddr");
    SYM(prepare, p, "OH_AVPlayer_Prepare");
    SYM(play, p, "OH_AVPlayer_Play");
    SYM(pause, p, "OH_AVPlayer_Pause");
    SYM(stop, p, "OH_AVPlayer_Stop");
    SYM(reset, p, "OH_AVPlayer_Reset");
    SYM(releaseSync, p, "OH_AVPlayer_ReleaseSync");
    SYM(setVolume, p, "OH_AVPlayer_SetVolume");
    SYM(seek, p, "OH_AVPlayer_Seek");
    SYM(getCurrentTime, p, "OH_AVPlayer_GetCurrentTime");
    SYM(getDuration, p, "OH_AVPlayer_GetDuration");
    SYM(getVideoWidth, p, "OH_AVPlayer_GetVideoWidth");
    SYM(getVideoHeight, p, "OH_AVPlayer_GetVideoHeight");
    SYM(setLooping, p, "OH_AVPlayer_SetLooping");
    SYM(setRate, p, "OH_AVPlayer_SetPlaybackRate");
    SYM(setRendererInfo, p, "OH_AVPlayer_SetAudioRendererInfo");
    SYM(setOnInfo, p, "OH_AVPlayer_SetOnInfoCallback");
    SYM(setOnError, p, "OH_AVPlayer_SetOnErrorCallback");
    SYM(formatGetInt, core, "OH_AVFormat_GetIntValue");
    SYM(keyState, p, "OH_PLAYER_STATE");
    SYM(keyWidth, p, "OH_PLAYER_VIDEO_WIDTH");
    SYM(keyHeight, p, "OH_PLAYER_VIDEO_HEIGHT");
#undef SYM
    if (!oh.create || !oh.setUrl || !oh.setFd || !oh.prepare || !oh.play || !oh.pause || !oh.stop
            || !oh.reset || !oh.releaseSync || !oh.setVolume || !oh.seek || !oh.getCurrentTime
            || !oh.getDuration || !oh.getVideoWidth || !oh.getVideoHeight || !oh.setLooping
            || !oh.setOnInfo || !oh.setOnError || !oh.formatGetInt || !oh.keyState) {
        mlog("[WL-MEDIA] OH player is missing a required entry point");
        pthread_mutex_unlock(&g_load_lock);
        return 0;
    }
    oh.loaded = 1;
    pthread_mutex_unlock(&g_load_lock);
    return 1;
}

/* Android's player states, as android/media/mediaplayer.h numbers its flags. */
enum { S_ERROR = 0, S_IDLE = 1, S_INITIALIZED = 2, S_PREPARING = 4, S_PREPARED = 8,
       S_STARTED = 16, S_PAUSED = 32, S_STOPPED = 64, S_COMPLETED = 128 };
/* MediaPlayer's event codes. */
enum { MEDIA_PREPARED = 1, MEDIA_PLAYBACK_COMPLETE = 2, MEDIA_SEEK_COMPLETE = 4,
       MEDIA_SET_VIDEO_SIZE = 5, MEDIA_ERROR = 100 };

typedef struct Player {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    OH_AVPlayer* oh;
    jobject weak;          /* global ref to MediaPlayer's WeakReference, postEventFromNative's cookie */
    int state;
    int fd;                /* our dup of the data source's descriptor */
    int64_t offset;        /* the source's range within it */
    DataSource source;
    int looping;
    float left, right;
    float speed;
    int session;
    int stream;
    int released;
    struct Player* next;
} Player;

static jclass g_player_class;
static jfieldID g_context;
static jmethodID g_post;
static jfieldID g_descriptor;
static int g_next_session = 0x1000;

static Player* get_player(JNIEnv* env, jobject thiz)
{
    return (Player*) (intptr_t) (*env)->GetLongField(env, thiz, g_context);
}

static void throw_new(JNIEnv* env, const char* cls, const char* msg)
{
    jclass c = (*env)->FindClass(env, cls);
    if (c != NULL) (*env)->ThrowNew(env, c, msg);
}

static Player* require(JNIEnv* env, jobject thiz)
{
    Player* p = get_player(env, thiz);
    if (p == NULL) throw_new(env, "java/lang/IllegalStateException", NULL);
    return p;
}

/* Delivers an event from an OH callback thread, attaching it to the VM for the call. */
static void post(Player* p, int what, int arg1, int arg2)
{
    JNIEnv* env = NULL;
    int attached = 0;
    jobject weak;
    if ((*g_vm)->GetEnv(g_vm, (void**) &env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return;
        attached = 1;
    }
    pthread_mutex_lock(&p->lock);
    weak = p->released || p->weak == NULL ? NULL : (*env)->NewLocalRef(env, p->weak);
    pthread_mutex_unlock(&p->lock);
    if (weak != NULL) {
        (*env)->CallStaticVoidMethod(env, g_player_class, g_post, weak, what, arg1, arg2, NULL);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, weak);
    }
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}

static void apply_settings(Player* p)
{
    oh.setVolume(p->oh, p->left, p->right);
    oh.setLooping(p->oh, p->looping);
    if (oh.setRate && p->speed > 0.f && p->speed != 1.f) oh.setRate(p->oh, p->speed);
}

static void on_info(OH_AVPlayer* player, int type, OH_AVFormat* body, void* data)
{
    Player* p = (Player*) data;
    int32_t value = 0, w = 0, h = 0;
    int event = 0, arg1 = 0, arg2 = 0;
    Player* next = NULL;
    (void) player;
    if (type == AV_INFO_TYPE_SEEKDONE) {
        post(p, MEDIA_SEEK_COMPLETE, 0, 0);
        return;
    }
    if (type == AV_INFO_TYPE_RESOLUTION_CHANGE && oh.keyWidth && oh.keyHeight) {
        oh.formatGetInt(body, *oh.keyWidth, &w);
        oh.formatGetInt(body, *oh.keyHeight, &h);
        post(p, MEDIA_SET_VIDEO_SIZE, w, h);
        return;
    }
    if (type != AV_INFO_TYPE_STATE_CHANGE || !oh.formatGetInt(body, *oh.keyState, &value)) return;
    mlog("[WL-MEDIA] player %p: OH state %d (was %d)", (void*) p, (int) value, p->state);
    pthread_mutex_lock(&p->lock);
    if (value == AV_PREPARED && p->state == S_PREPARING) {
        apply_settings(p);
        p->state = S_PREPARED;
        event = MEDIA_PREPARED;
    } else if (value == AV_COMPLETED && p->state == S_STARTED) {
        p->state = S_COMPLETED;
        event = MEDIA_PLAYBACK_COMPLETE;
        next = p->next;
    } else if (value == AV_ERROR && p->state != S_IDLE) {
        p->state = S_ERROR;
    }
    pthread_cond_broadcast(&p->changed);
    pthread_mutex_unlock(&p->lock);
    /* setNextMediaPlayer: the next player starts when this one completes. */
    if (next != NULL) {
        pthread_mutex_lock(&next->lock);
        if (next->state & (S_PREPARED | S_PAUSED | S_COMPLETED)) {
            if (oh.play(next->oh) == 0) next->state = S_STARTED;
        }
        pthread_mutex_unlock(&next->lock);
    }
    if (event != 0) post(p, event, arg1, arg2);
}

static void on_error(OH_AVPlayer* player, int32_t code, const char* msg, void* data)
{
    Player* p = (Player*) data;
    (void) player;
    mlog("[WL-MEDIA] player error %d: %s", (int) code, msg ? msg : "");
    pthread_mutex_lock(&p->lock);
    p->state = S_ERROR;
    pthread_cond_broadcast(&p->changed);
    pthread_mutex_unlock(&p->lock);
    post(p, MEDIA_ERROR, 1 /* MEDIA_ERROR_UNKNOWN */, (int) code);
}

static void mp_native_init(JNIEnv* env, jclass c)
{
    (void) env; (void) c;
}

static void mp_setup(JNIEnv* env, jobject thiz, jobject weak, jobject attribution, jint session)
{
    Player* p;
    (void) attribution;
    if (!load_oh_player()) {
        throw_new(env, "java/lang/RuntimeException", "Out of memory"); /* AOSP's no-player answer */
        return;
    }
    p = (Player*) calloc(1, sizeof(Player));
    if (p == NULL) {
        throw_new(env, "java/lang/RuntimeException", "Out of memory");
        return;
    }
    p->oh = oh.create();
    if (p->oh == NULL) {
        free(p);
        throw_new(env, "java/lang/RuntimeException", "Out of memory");
        return;
    }
    {
        /* Recursive, in case OH delivers a state change on the thread that made the call. */
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&p->lock, &attr);
        pthread_mutexattr_destroy(&attr);
    }
    pthread_cond_init(&p->changed, NULL);
    p->weak = (*env)->NewGlobalRef(env, weak);
    p->state = S_IDLE;
    p->fd = -1;
    p->left = p->right = 1.f;
    p->speed = 1.f;
    p->stream = 3; /* AudioManager.STREAM_MUSIC */
    p->session = session != 0 ? session : __atomic_add_fetch(&g_next_session, 1, __ATOMIC_RELAXED);
    oh.setOnInfo(p->oh, on_info, p);
    oh.setOnError(p->oh, on_error, p);
    (*env)->SetLongField(env, thiz, g_context, (jlong) (intptr_t) p);
}

static void close_source(Player* p)
{
    if (p->fd >= 0) close(p->fd);
    p->fd = -1;
}

static void mp_release(JNIEnv* env, jobject thiz)
{
    Player* p = get_player(env, thiz);
    if (p == NULL) return;
    (*env)->SetLongField(env, thiz, g_context, 0);
    pthread_mutex_lock(&p->lock);
    p->released = 1;
    pthread_mutex_unlock(&p->lock);
    /* ReleaseSync returns once no callback can run any more, so the Player can then go. */
    oh.releaseSync(p->oh);
    close_source(p);
    (*env)->DeleteGlobalRef(env, p->weak);
    pthread_cond_destroy(&p->changed);
    pthread_mutex_destroy(&p->lock);
    free(p);
}

static void mp_reset(JNIEnv* env, jobject thiz)
{
    Player* p = require(env, thiz);
    if (p == NULL) return;
    pthread_mutex_lock(&p->lock);
    oh.reset(p->oh);
    close_source(p);
    p->state = S_IDLE;
    pthread_cond_broadcast(&p->changed);
    pthread_mutex_unlock(&p->lock);
}

/* Serves the player's reads from this process. OH's media service cannot always use a descriptor
 * the app passes it: Doors of Doom's music, an asset range of its APK, got AV_ERR_INVALID_VAL from
 * SetFDSource, and the IOException closed the game. Reading here needs no access on the service's
 * side. Returns -1 at the end of the source. */
static int32_t read_source(OH_AVBuffer* data, int32_t length, int64_t pos, void* userData)
{
    Player* p = (Player*) userData;
    uint8_t* to = oh.bufferAddr(data);
    ssize_t n;
    if (to == NULL || length < 0 || pos < 0) return -1;
    if (pos >= p->source.size) return -1;
    if (length > p->source.size - pos) length = (int32_t) (p->source.size - pos);
    do {
        n = pread(p->fd, to, (size_t) length, p->offset + pos);
    } while (n < 0 && errno == EINTR);
    return n <= 0 ? -1 : (int32_t) n;
}

/* Takes ownership of fd. Throws as AOSP's setDataSource does on a failed source. */
static void set_fd_source(JNIEnv* env, Player* p, int fd, int64_t offset, int64_t length)
{
    struct stat st;
    int rc;
    pthread_mutex_lock(&p->lock);
    if (p->state != S_IDLE) {
        pthread_mutex_unlock(&p->lock);
        close(fd);
        throw_new(env, "java/lang/IllegalStateException", NULL);
        return;
    }
    /* A FileDescriptor source without a range passes 0x7ffffffffffffff; OH wants the real size. */
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && (length <= 0 || offset + length > st.st_size)) {
        length = st.st_size > offset ? st.st_size - offset : 0;
    }
    p->fd = fd;
    p->offset = offset;
    rc = -1;
    if (oh.setDataSource && oh.bufferAddr && length > 0) {
        p->source.size = length;
        p->source.readAt = read_source;
        rc = oh.setDataSource(p->oh, &p->source, p);
    }
    if (rc != 0) rc = oh.setFd(p->oh, fd, offset, length);
    mlog("[WL-MEDIA] player %p: source fd range %lld+%lld, %s rc=%d", (void*) p, (long long) offset,
         (long long) length, p->source.readAt ? "read callback" : "descriptor", rc);
    if (rc == 0) {
        p->state = S_INITIALIZED;
    } else {
        p->fd = -1;
    }
    pthread_mutex_unlock(&p->lock);
    if (rc != 0) {
        close(fd);
        mlog("[WL-MEDIA] SetFDSource failed %d", rc);
        throw_new(env, "java/io/IOException", "setDataSourceFD failed.: status=0x80000000");
    }
}

static void mp_set_source_fd(JNIEnv* env, jobject thiz, jobject fdObject, jlong offset, jlong length)
{
    Player* p = require(env, thiz);
    int fd;
    if (p == NULL) return;
    if (fdObject == NULL) {
        throw_new(env, "java/lang/IllegalArgumentException", NULL);
        return;
    }
    /* The caller may close its descriptor as soon as this returns: MediaPlayer.create does. */
    fd = fcntl((*env)->GetIntField(env, fdObject, g_descriptor), F_DUPFD_CLOEXEC, 0);
    if (fd < 0) {
        throw_new(env, "java/io/IOException", "setDataSourceFD failed.: status=0x80000000");
        return;
    }
    set_fd_source(env, p, fd, offset, length);
}

static void mp_set_source_path(JNIEnv* env, jobject thiz, jobject http, jstring path,
                               jobjectArray keys, jobjectArray values)
{
    Player* p = require(env, thiz);
    const char* s;
    int rc = 0;
    (void) http; (void) keys; (void) values;
    if (p == NULL) return;
    if (path == NULL) {
        throw_new(env, "java/lang/IllegalArgumentException", NULL);
        return;
    }
    s = (*env)->GetStringUTFChars(env, path, NULL);
    if (s == NULL) return;
    if (strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0) {
        pthread_mutex_lock(&p->lock);
        if (p->state != S_IDLE) {
            rc = -38;
        } else {
            rc = oh.setUrl(p->oh, s);
            if (rc == 0) p->state = S_INITIALIZED;
        }
        pthread_mutex_unlock(&p->lock);
        (*env)->ReleaseStringUTFChars(env, path, s);
        if (rc == -38) {
            throw_new(env, "java/lang/IllegalStateException", NULL);
        } else if (rc != 0) {
            throw_new(env, "java/io/IOException", "setDataSource failed.: status=0x80000000");
        }
        return;
    }
    {
        const char* file = strncmp(s, "file://", 7) == 0 ? s + 7 : s;
        int fd = open(file, O_RDONLY | O_CLOEXEC);
        (*env)->ReleaseStringUTFChars(env, path, s);
        if (fd < 0) {
            throw_new(env, "java/io/IOException", "setDataSource failed.: status=0x80000000");
            return;
        }
        set_fd_source(env, p, fd, 0, 0);
    }
}

static void mp_set_source_callback(JNIEnv* env, jobject thiz, jobject source)
{
    (void) thiz; (void) source;
    throw_new(env, "java/io/IOException", "setDataSourceCallback failed.: status=0x80000000");
}

/* Starts OH's asynchronous prepare; the state-change callback completes it. */
static int begin_prepare(JNIEnv* env, Player* p)
{
    if (!(p->state & (S_INITIALIZED | S_STOPPED))) {
        throw_new(env, "java/lang/IllegalStateException", NULL);
        return -1;
    }
    if (oh.setRendererInfo) oh.setRendererInfo(p->oh, 1 /* AUDIOSTREAM_USAGE_MUSIC */);
    p->state = S_PREPARING;
    if (oh.prepare(p->oh) != 0) {
        p->state = S_ERROR;
        return 1;
    }
    return 0;
}

static jint mp_prepare(JNIEnv* env, jobject thiz, jobject piid)
{
    Player* p = require(env, thiz);
    struct timespec deadline;
    int rc;
    (void) piid;
    if (p == NULL) return 0;
    pthread_mutex_lock(&p->lock);
    rc = begin_prepare(env, p);
    if (rc < 0) {
        pthread_mutex_unlock(&p->lock);
        return 0;
    }
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 20;
    while (rc == 0 && p->state == S_PREPARING) {
        if (pthread_cond_timedwait(&p->changed, &p->lock, &deadline) == ETIMEDOUT) break;
    }
    rc = p->state == S_PREPARED ? 0 : 1;
    pthread_mutex_unlock(&p->lock);
    if (rc != 0) mlog("[WL-MEDIA] player %p: prepare failed, state %d", (void*) p, p->state);
    if (rc != 0) throw_new(env, "java/io/IOException", "Prepare failed.: status=0x1");
    return 0;
}

static jint mp_prepare_async(JNIEnv* env, jobject thiz, jobject piid)
{
    Player* p = require(env, thiz);
    int rc;
    (void) piid;
    if (p == NULL) return 0;
    pthread_mutex_lock(&p->lock);
    rc = begin_prepare(env, p);
    pthread_mutex_unlock(&p->lock);
    if (rc > 0) post(p, MEDIA_ERROR, 1, 0);
    return 0;
}

/*
 * A call the player's state does not allow. AOSP's JNI throws only when the native player is
 * missing; for start, pause, stop and seekTo it hands the status to process_media_player_call with
 * no exception, which reports it as MediaPlayer::notify does: MEDIA_ERROR with INVALID_OPERATION,
 * the "error (-38, 0)" apps see on Android, and the player in its error state. Fossify Voice
 * Recorder stops a player it never prepared; the throw here killed its process at start.
 *
 * A player still idle stays idle here: AOSP accepts setDataSource from the error state, this
 * player only from idle, and every other call is invalid in both.
 */
static void invalid_operation(Player* p)
{
    pthread_mutex_lock(&p->lock);
    if (p->state != S_IDLE) p->state = S_ERROR;
    pthread_mutex_unlock(&p->lock);
    post(p, MEDIA_ERROR, -38 /* INVALID_OPERATION */, 0);
}

static void mp_start(JNIEnv* env, jobject thiz)
{
    Player* p = require(env, thiz);
    int bad = 0;
    if (p == NULL) return;
    pthread_mutex_lock(&p->lock);
    if (p->state & (S_PREPARED | S_PAUSED | S_COMPLETED)) {
        if (oh.play(p->oh) == 0) p->state = S_STARTED; else bad = 1;
    } else if (p->state != S_STARTED) {
        bad = 1;
    }
    pthread_mutex_unlock(&p->lock);
    if (bad) invalid_operation(p);
}

static void mp_pause(JNIEnv* env, jobject thiz)
{
    Player* p = require(env, thiz);
    int bad = 0;
    if (p == NULL) return;
    pthread_mutex_lock(&p->lock);
    if (p->state == S_STARTED) {
        if (oh.pause(p->oh) == 0) p->state = S_PAUSED; else bad = 1;
    } else if (!(p->state & (S_PAUSED | S_COMPLETED))) {
        bad = 1;
    }
    pthread_mutex_unlock(&p->lock);
    if (bad) invalid_operation(p);
}

static void mp_stop(JNIEnv* env, jobject thiz)
{
    Player* p = require(env, thiz);
    int bad = 0;
    if (p == NULL) return;
    pthread_mutex_lock(&p->lock);
    if (p->state & (S_PREPARED | S_STARTED | S_PAUSED | S_COMPLETED)) {
        oh.stop(p->oh);
        p->state = S_STOPPED;
    } else if (p->state != S_STOPPED) {
        bad = 1;
    }
    pthread_mutex_unlock(&p->lock);
    if (bad) invalid_operation(p);
}

static void mp_seek(JNIEnv* env, jobject thiz, jlong msec, jint mode)
{
    Player* p = require(env, thiz);
    int bad = 0;
    /* MediaPlayer's SEEK_PREVIOUS_SYNC/NEXT_SYNC/CLOSEST_SYNC/CLOSEST onto OH's seek modes. */
    static const int kMode[] = { 1 /* previous sync */, 0 /* next sync */, 1, 2 /* closest */ };
    if (p == NULL) return;
    pthread_mutex_lock(&p->lock);
    if (p->state & (S_PREPARED | S_STARTED | S_PAUSED | S_COMPLETED)) {
        oh.seek(p->oh, (int32_t) (msec > INT32_MAX ? INT32_MAX : msec < 0 ? 0 : msec),
                kMode[mode >= 0 && mode <= 3 ? mode : 0]);
    } else {
        bad = 1;
    }
    pthread_mutex_unlock(&p->lock);
    if (bad) invalid_operation(p);
}

static jint query(JNIEnv* env, jobject thiz, int (*get)(OH_AVPlayer*, int32_t*))
{
    Player* p = get_player(env, thiz);
    int32_t value = 0;
    if (p == NULL) return 0;
    pthread_mutex_lock(&p->lock);
    if (p->state & (S_PREPARED | S_STARTED | S_PAUSED | S_STOPPED | S_COMPLETED)) {
        if (get(p->oh, &value) != 0) value = 0;
    }
    pthread_mutex_unlock(&p->lock);
    return value;
}

static jint mp_position(JNIEnv* env, jobject thiz) { return query(env, thiz, oh.getCurrentTime); }
static jint mp_duration(JNIEnv* env, jobject thiz) { return query(env, thiz, oh.getDuration); }
static jint mp_width(JNIEnv* env, jobject thiz) { return query(env, thiz, oh.getVideoWidth); }
static jint mp_height(JNIEnv* env, jobject thiz) { return query(env, thiz, oh.getVideoHeight); }

static jboolean mp_is_playing(JNIEnv* env, jobject thiz)
{
    Player* p = get_player(env, thiz);
    return p != NULL && p->state == S_STARTED;
}

static void mp_set_looping(JNIEnv* env, jobject thiz, jboolean looping)
{
    Player* p = require(env, thiz);
    if (p == NULL) return;
    pthread_mutex_lock(&p->lock);
    p->looping = looping ? 1 : 0;
    if (p->state & (S_PREPARED | S_STARTED | S_PAUSED | S_COMPLETED)) oh.setLooping(p->oh, p->looping);
    pthread_mutex_unlock(&p->lock);
}

static jboolean mp_is_looping(JNIEnv* env, jobject thiz)
{
    Player* p = get_player(env, thiz);
    return p != NULL && p->looping;
}

static void mp_set_volume(JNIEnv* env, jobject thiz, jfloat left, jfloat right)
{
    Player* p = require(env, thiz);
    if (p == NULL) return;
    pthread_mutex_lock(&p->lock);
    p->left = left;
    p->right = right;
    if (p->state & (S_PREPARED | S_STARTED | S_PAUSED | S_COMPLETED)) oh.setVolume(p->oh, left, right);
    pthread_mutex_unlock(&p->lock);
}

static void mp_set_video_surface(JNIEnv* env, jobject thiz, jobject surface)
{
    (void) env; (void) thiz; (void) surface; /* video output is not ported: audio only */
}

static void mp_set_stream(JNIEnv* env, jobject thiz, jint stream)
{
    Player* p = get_player(env, thiz);
    if (p != NULL) p->stream = stream;
}

static jint mp_get_stream(JNIEnv* env, jobject thiz)
{
    Player* p = get_player(env, thiz);
    return p != NULL ? p->stream : 3;
}

static void mp_set_session(JNIEnv* env, jobject thiz, jint session)
{
    Player* p = get_player(env, thiz);
    if (p != NULL) p->session = session;
}

static jint mp_get_session(JNIEnv* env, jobject thiz)
{
    Player* p = get_player(env, thiz);
    return p != NULL ? p->session : 0;
}

static void mp_set_next(JNIEnv* env, jobject thiz, jobject next)
{
    Player* p = require(env, thiz);
    if (p == NULL) return;
    pthread_mutex_lock(&p->lock);
    p->next = next != NULL ? get_player(env, next) : NULL;
    pthread_mutex_unlock(&p->lock);
}

static void mp_set_playback_params(JNIEnv* env, jobject thiz, jobject params)
{
    Player* p = require(env, thiz);
    jclass c;
    jmethodID getSpeed;
    jfloat speed;
    if (p == NULL || params == NULL) return;
    c = (*env)->GetObjectClass(env, params);
    getSpeed = (*env)->GetMethodID(env, c, "getSpeed", "()F");
    (*env)->DeleteLocalRef(env, c);
    if (getSpeed == NULL) {
        (*env)->ExceptionClear(env);
        return;
    }
    speed = (*env)->CallFloatMethod(env, params, getSpeed);
    if ((*env)->ExceptionCheck(env)) {  /* speed not set in these params: nothing to change */
        (*env)->ExceptionClear(env);
        return;
    }
    pthread_mutex_lock(&p->lock);
    if (speed == 0.f) {
        if (p->state == S_STARTED && oh.pause(p->oh) == 0) p->state = S_PAUSED;
    } else {
        p->speed = speed;
        if (oh.setRate && (p->state & (S_PREPARED | S_STARTED | S_PAUSED | S_COMPLETED))) {
            oh.setRate(p->oh, speed);
        }
    }
    pthread_mutex_unlock(&p->lock);
}

static jobject new_default(JNIEnv* env, const char* cls)
{
    jclass c = (*env)->FindClass(env, cls);
    jmethodID ctor;
    jobject o;
    if (c == NULL) return NULL;
    ctor = (*env)->GetMethodID(env, c, "<init>", "()V");
    o = ctor != NULL ? (*env)->NewObject(env, c, ctor) : NULL;
    (*env)->DeleteLocalRef(env, c);
    return o;
}

static jobject mp_get_playback_params(JNIEnv* env, jobject thiz)
{
    Player* p = get_player(env, thiz);
    jobject params = new_default(env, "android/media/PlaybackParams");
    jclass c;
    jmethodID setSpeed, setPitch;
    jobject r;
    if (params == NULL) return NULL;
    c = (*env)->GetObjectClass(env, params);
    setSpeed = (*env)->GetMethodID(env, c, "setSpeed", "(F)Landroid/media/PlaybackParams;");
    setPitch = (*env)->GetMethodID(env, c, "setPitch", "(F)Landroid/media/PlaybackParams;");
    (*env)->DeleteLocalRef(env, c);
    if (setSpeed == NULL || setPitch == NULL) {
        (*env)->ExceptionClear(env);
        return params;
    }
    r = (*env)->CallObjectMethod(env, params, setSpeed, p != NULL ? p->speed : 1.f);
    if (r != NULL) (*env)->DeleteLocalRef(env, r);
    r = (*env)->CallObjectMethod(env, params, setPitch, 1.f);
    if (r != NULL) (*env)->DeleteLocalRef(env, r);
    return params;
}

static void mp_set_sync_params(JNIEnv* env, jobject thiz, jobject params)
{
    (void) env; (void) thiz; (void) params;
}

static jobject mp_get_sync_params(JNIEnv* env, jobject thiz)
{
    (void) thiz;
    return new_default(env, "android/media/SyncParams");
}

static void mp_noop(JNIEnv* env, jobject thiz) { (void) env; (void) thiz; }
static void mp_noop_j(JNIEnv* env, jobject thiz, jlong v) { (void) env; (void) thiz; (void) v; }
static void mp_noop_i(JNIEnv* env, jobject thiz, jint v) { (void) env; (void) thiz; (void) v; }
static void mp_noop_f(JNIEnv* env, jobject thiz, jfloat v) { (void) env; (void) thiz; (void) v; }
static void mp_noop_z(JNIEnv* env, jobject thiz, jboolean v) { (void) env; (void) thiz; (void) v; }

/* MediaPlayer.invoke's requests, from MediaPlayer.java. */
enum { INVOKE_GET_TRACK_INFO = 1, INVOKE_SET_VIDEO_SCALE_MODE = 6, INVOKE_GET_SELECTED_TRACK = 7,
       INVOKE_SET_PLAYER_IID = 8 };

static jint parcel_call(JNIEnv* env, jobject parcel, const char* name, const char* sig, ...)
{
    jclass c = (*env)->GetObjectClass(env, parcel);
    jmethodID m = (*env)->GetMethodID(env, c, name, sig);
    jint r = 0;
    va_list ap;
    (*env)->DeleteLocalRef(env, c);
    if (m == NULL) return -1;
    va_start(ap, sig);
    if (sig[strlen(sig) - 1] == 'I') r = (*env)->CallIntMethodV(env, parcel, m, ap);
    else (*env)->CallVoidMethodV(env, parcel, m, ap);
    va_end(ap);
    return (*env)->ExceptionCheck(env) ? -1 : r;
}

/* The player has no tracks it could list or select separately (no subtitle or multi-audio
 * support), which is the answer AOSP's player gives for a plain audio file: an empty list, and no
 * selected track. */
static jint mp_invoke(JNIEnv* env, jobject thiz, jobject request, jobject reply)
{
    jstring token;
    jint id;
    (void) thiz;
    if (request == NULL || reply == NULL) return -22; /* BAD_VALUE */
    parcel_call(env, request, "setDataPosition", "(I)V", 0);
    token = (*env)->NewStringUTF(env, "android.media.IMediaPlayer");
    parcel_call(env, request, "enforceInterface", "(Ljava/lang/String;)V", token);
    (*env)->DeleteLocalRef(env, token);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return -22;
    }
    id = parcel_call(env, request, "readInt", "()I");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return -22;
    }
    switch (id) {
    case INVOKE_GET_TRACK_INFO:
        parcel_call(env, reply, "writeInt", "(I)V", 0);
        return 0;
    case INVOKE_GET_SELECTED_TRACK:
        parcel_call(env, reply, "writeInt", "(I)V", -1);
        return 0;
    case INVOKE_SET_VIDEO_SCALE_MODE:
    case INVOKE_SET_PLAYER_IID:
        return 0;
    default:
        return -38; /* INVALID_OPERATION */
    }
}

/* The calls AOSP forwards to MediaPlayerService and that no OH call answers: each returns what
 * AOSP's JNI returns when the service reports INVALID_OPERATION. */
static jint mp_invalid_p(JNIEnv* env, jobject thiz, jobject a)
{ (void) env; (void) thiz; (void) a; return -38; }
static jint mp_invalid_static_p(JNIEnv* env, jclass c, jobject a)
{ (void) env; (void) c; (void) a; return -38; }
static jint mp_invalid_si(JNIEnv* env, jobject thiz, jstring a, jint b)
{ (void) env; (void) thiz; (void) a; (void) b; return -38; }
static jboolean mp_false_zzp(JNIEnv* env, jobject thiz, jboolean a, jboolean b, jobject c)
{ (void) env; (void) thiz; (void) a; (void) b; (void) c; return JNI_FALSE; }
static jboolean mp_false_ip(JNIEnv* env, jobject thiz, jint a, jobject b)
{ (void) env; (void) thiz; (void) a; (void) b; return JNI_FALSE; }
static jboolean mp_false_i(JNIEnv* env, jobject thiz, jint a)
{ (void) env; (void) thiz; (void) a; return JNI_FALSE; }
static jint mp_zero(JNIEnv* env, jobject thiz) { (void) env; (void) thiz; return 0; }
static jobject mp_null(JNIEnv* env, jobject thiz) { (void) env; (void) thiz; return NULL; }
static jobject mp_null_i(JNIEnv* env, jobject thiz, jint a) { (void) env; (void) thiz; (void) a; return NULL; }
static jint mp_shaper(JNIEnv* env, jobject thiz, jobject a, jobject b)
{
    (void) thiz; (void) a; (void) b;
    throw_new(env, "java/lang/IllegalStateException", "VolumeShaper is not supported");
    return -38;
}
static void mp_prepare_drm(JNIEnv* env, jobject thiz, jbyteArray a, jbyteArray b)
{
    (void) thiz; (void) a; (void) b;
    throw_new(env, "java/lang/IllegalStateException", "DRM is not supported");
}

static const JNINativeMethod kMediaPlayer[] = {
    { "native_init", "()V", (void*) mp_native_init },
    { "native_setup", "(Ljava/lang/Object;Landroid/os/Parcel;I)V", (void*) mp_setup },
    { "native_finalize", "()V", (void*) mp_release },
    { "_release", "()V", (void*) mp_release },
    { "_reset", "()V", (void*) mp_reset },
    { "_setDataSource", "(Ljava/io/FileDescriptor;JJ)V", (void*) mp_set_source_fd },
    { "_setDataSource", "(Landroid/media/MediaDataSource;)V", (void*) mp_set_source_callback },
    { "nativeSetDataSource",
      "(Landroid/os/IBinder;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V",
      (void*) mp_set_source_path },
    { "_prepare", "(Landroid/os/Parcel;)I", (void*) mp_prepare },
    { "_prepareAsync", "(Landroid/os/Parcel;)I", (void*) mp_prepare_async },
    { "_start", "()V", (void*) mp_start },
    { "_pause", "()V", (void*) mp_pause },
    { "_stop", "()V", (void*) mp_stop },
    { "_seekTo", "(JI)V", (void*) mp_seek },
    { "getCurrentPosition", "()I", (void*) mp_position },
    { "getDuration", "()I", (void*) mp_duration },
    { "getVideoWidth", "()I", (void*) mp_width },
    { "getVideoHeight", "()I", (void*) mp_height },
    { "isPlaying", "()Z", (void*) mp_is_playing },
    { "setLooping", "(Z)V", (void*) mp_set_looping },
    { "isLooping", "()Z", (void*) mp_is_looping },
    { "_setVolume", "(FF)V", (void*) mp_set_volume },
    { "_setVideoSurface", "(Landroid/view/Surface;)V", (void*) mp_set_video_surface },
    { "_setAudioStreamType", "(I)V", (void*) mp_set_stream },
    { "_getAudioStreamType", "()I", (void*) mp_get_stream },
    { "native_setAudioSessionId", "(I)V", (void*) mp_set_session },
    { "getAudioSessionId", "()I", (void*) mp_get_session },
    { "setNextMediaPlayer", "(Landroid/media/MediaPlayer;)V", (void*) mp_set_next },
    { "setPlaybackParams", "(Landroid/media/PlaybackParams;)V", (void*) mp_set_playback_params },
    { "getPlaybackParams", "()Landroid/media/PlaybackParams;", (void*) mp_get_playback_params },
    { "setSyncParams", "(Landroid/media/SyncParams;)V", (void*) mp_set_sync_params },
    { "getSyncParams", "()Landroid/media/SyncParams;", (void*) mp_get_sync_params },
    { "_notifyAt", "(J)V", (void*) mp_noop_j },
    { "attachAuxEffect", "(I)V", (void*) mp_noop_i },
    { "_setAuxEffectSendLevel", "(F)V", (void*) mp_noop_f },
    { "native_enableDeviceCallback", "(Z)V", (void*) mp_noop_z },
    { "native_getRoutedDeviceId", "()I", (void*) mp_zero },
    { "native_setOutputDevice", "(I)Z", (void*) mp_false_i },
    { "setParameter", "(ILandroid/os/Parcel;)Z", (void*) mp_false_ip },
    { "native_invoke", "(Landroid/os/Parcel;Landroid/os/Parcel;)I", (void*) mp_invoke },
    { "native_setMetadataFilter", "(Landroid/os/Parcel;)I", (void*) mp_invalid_p },
    { "native_getMetadata", "(ZZLandroid/os/Parcel;)Z", (void*) mp_false_zzp },
    { "native_getMetrics", "()Landroid/os/PersistableBundle;", (void*) mp_null },
    { "native_pullBatteryData", "(Landroid/os/Parcel;)I", (void*) mp_invalid_static_p },
    { "native_setRetransmitEndpoint", "(Ljava/lang/String;I)I", (void*) mp_invalid_si },
    { "native_applyVolumeShaper",
      "(Landroid/media/VolumeShaper$Configuration;Landroid/media/VolumeShaper$Operation;)I",
      (void*) mp_shaper },
    { "native_getVolumeShaperState", "(I)Landroid/media/VolumeShaper$State;", (void*) mp_null_i },
    { "_prepareDrm", "([B[B)V", (void*) mp_prepare_drm },
    { "_releaseDrm", "()V", (void*) mp_noop },
};

static int bind_media(JNIEnv* env, jclass c, const JNINativeMethod* table, int count)
{
    int bound = 0, i;
    for (i = 0; i < count; i++) {
        if ((*env)->RegisterNatives(env, c, &table[i], 1) == JNI_OK) {
            bound++;
        } else {
            (*env)->ExceptionClear(env);
        }
    }
    return bound;
}

static int bind_class(JNIEnv* env, const char* name, const JNINativeMethod* table, int count)
{
    jclass c = (*env)->FindClass(env, name);
    int bound;
    if (c == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    bound = bind_media(env, c, table, count);
    (*env)->DeleteLocalRef(env, c);
    return bound;
}

#define COUNT(t) ((int) (sizeof(t) / sizeof((t)[0])))

void wl_register_media(JNIEnv* env, JavaVM* vm)
{
    jclass player, fd;
    int t, s, m = 0;
    g_vm = vm;
    {
        /* The bridge's 16-bit write, which the ByteBuffer binding hands its samples to. Looked up
         * first: without it the bridge's own ByteBuffer write is left in place. */
        jclass track = (*env)->FindClass(env, "android/media/AudioTrack");
        if (track != NULL) {
            g_write_short = (*env)->GetMethodID(env, track, "native_write_short", "([SIIIZ)I");
            (*env)->DeleteLocalRef(env, track);
        }
        (*env)->ExceptionClear(env);
    }
    t = bind_class(env, "android/media/AudioTrack", kAudioTrack + (g_write_short != NULL ? 0 : 1),
                   COUNT(kAudioTrack) - (g_write_short != NULL ? 0 : 1));
    s = bind_class(env, "android/media/AudioSystem", kAudioSystem, COUNT(kAudioSystem));
    {
        int d = bind_class(env, "android/media/MediaDrm", kMediaDrm, COUNT(kMediaDrm));
        int c = bind_class(env, "android/media/MediaCrypto", kMediaCrypto, COUNT(kMediaCrypto));
        int rc = bind_class(env, "android/media/MediaRecorder", kMediaRecorder, COUNT(kMediaRecorder));
        mlog("[WL-NATIVES] bound MediaRecorder=%d/%d", rc, COUNT(kMediaRecorder));
        mlog("[WL-NATIVES] bound MediaDrm=%d/%d MediaCrypto=%d/%d", d, COUNT(kMediaDrm), c, COUNT(kMediaCrypto));
    }
    player = (*env)->FindClass(env, "android/media/MediaPlayer");
    fd = (*env)->FindClass(env, "java/io/FileDescriptor");
    if (player != NULL && fd != NULL) {
        /* Natives first: GetFieldID and GetStaticMethodID initialize the class, and MediaPlayer's
         * static initializer calls native_init. */
        m = bind_media(env, player, kMediaPlayer, COUNT(kMediaPlayer));
        g_player_class = (jclass) (*env)->NewGlobalRef(env, player);
        g_context = (*env)->GetFieldID(env, player, "mNativeContext", "J");
        g_post = (*env)->GetStaticMethodID(env, player, "postEventFromNative",
                                           "(Ljava/lang/Object;IIILjava/lang/Object;)V");
        g_descriptor = (*env)->GetFieldID(env, fd, "descriptor", "I");
        if (g_context == NULL || g_post == NULL || g_descriptor == NULL) {
            /* Without them no MediaPlayer call can work: leave the class as unbound as it was. */
            (*env)->ExceptionClear(env);
            (*env)->UnregisterNatives(env, player);
            m = 0;
        }
    }
    (*env)->ExceptionClear(env);
    mlog("[WL-NATIVES] bound AudioTrack=%d/%d AudioSystem=%d/%d MediaPlayer=%d/%d",
         t, COUNT(kAudioTrack), s, COUNT(kAudioSystem), m, COUNT(kMediaPlayer));
}
