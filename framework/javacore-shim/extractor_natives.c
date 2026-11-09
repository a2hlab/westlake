/*
 * android.media.MediaExtractor over OH's AVSource and AVDemuxer, bound from
 * libwl_missing_natives' JNI_OnLoad.
 *
 * None of its 26 natives is registered, and native_init is called from the class initializer, so
 * every app that reads a media file's tracks died on the class itself: 705 of the 982 HK top-1000
 * apps call it, and TataLand stopped there with no screen. OH's demuxer has the same shape -- a
 * source from a descriptor, a track list with a format each, tracks selected for reading, samples
 * read one per track with a presentation time and flags, and a seek -- with one difference that
 * this file carries: OH reads a sample from a named track, while Android reads "the next sample"
 * across every selected track in presentation order. So one sample per selected track is held
 * here, and the current sample is the earliest of them; advance() consumes it and reads that
 * track's next.
 *
 * Not ported, each answering as AOSP's JNI answers when its extractor cannot: a MediaDataSource
 * (read through the caller's own Java methods), MediaCas and its crypto info (no conditional
 * access here), the cached-duration and metrics queries, and the audio presentations of a
 * broadcast stream.
 */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define COUNT(t) ((int) (sizeof(t) / sizeof((t)[0])))
#define MAX_TRACKS 32

static void xlog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void xlog(const char* fmt, ...)
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

typedef struct OH_AVSource OH_AVSource;
typedef struct OH_AVDemuxer OH_AVDemuxer;
typedef struct OH_AVFormat OH_AVFormat;
typedef struct OH_AVBuffer OH_AVBuffer;

typedef struct {
    int64_t pts;
    int32_t size;
    int32_t offset;
    uint32_t flags;
} OH_AVCodecBufferAttr;

/* OH's buffer flags (native_avbuffer_info.h). */
enum { OH_FLAG_EOS = 1 << 0, OH_FLAG_SYNC = 1 << 1, OH_FLAG_INCOMPLETE = 1 << 2,
       OH_FLAG_CODEC_DATA = 1 << 3 };
/* Android's MediaExtractor.SAMPLE_FLAG_*. */
enum { ANDROID_FLAG_SYNC = 1, ANDROID_FLAG_ENCRYPTED = 2, ANDROID_FLAG_PARTIAL = 4 };

static struct {
    int loaded;
    OH_AVSource* (*sourceFd)(int32_t, int64_t, int64_t);
    int (*sourceDestroy)(OH_AVSource*);
    OH_AVFormat* (*sourceFormat)(OH_AVSource*);
    OH_AVFormat* (*trackFormat)(OH_AVSource*, uint32_t);
    OH_AVDemuxer* (*demuxerCreate)(OH_AVSource*);
    int (*demuxerDestroy)(OH_AVDemuxer*);
    int (*selectTrack)(OH_AVDemuxer*, uint32_t);
    int (*unselectTrack)(OH_AVDemuxer*, uint32_t);
    int (*readSample)(OH_AVDemuxer*, uint32_t, OH_AVBuffer*);
    int (*seek)(OH_AVDemuxer*, int64_t, int);
    OH_AVBuffer* (*bufferCreate)(int32_t);
    int (*bufferDestroy)(OH_AVBuffer*);
    uint8_t* (*bufferAddr)(OH_AVBuffer*);
    int (*bufferAttr)(OH_AVBuffer*, OH_AVCodecBufferAttr*);
    void (*formatDestroy)(OH_AVFormat*);
    bool (*formatGetInt)(OH_AVFormat*, const char*, int32_t*);
    bool (*formatGetLong)(OH_AVFormat*, const char*, int64_t*);
    bool (*formatGetDouble)(OH_AVFormat*, const char*, double*);
    bool (*formatGetString)(OH_AVFormat*, const char*, const char**);
    bool (*formatGetBuffer)(OH_AVFormat*, const char*, uint8_t**, size_t*);
    const char* (*formatDump)(OH_AVFormat*);
    /* OH's format key names, which are exported strings rather than macros. */
    const char* const* keyMime;
    const char* const* keyDuration;
    const char* const* keyBitrate;
    const char* const* keyMaxInput;
    const char* const* keyWidth;
    const char* const* keyHeight;
    const char* const* keyFrameRate;
    const char* const* keyChannels;
    const char* const* keySampleRate;
    const char* const* keyRotation;
    const char* const* keyConfig;
    const char* const* keyTrackType;
    const char* const* keyTrackCount;
    const char* const* keyLanguage;
    const char* const* keyProfile;
} ohx;

static pthread_mutex_t g_extractor_load = PTHREAD_MUTEX_INITIALIZER;

static int load_oh_demuxer(void)
{
    void* source;
    void* demuxer;
    void* core;
    void* base;
    pthread_mutex_lock(&g_extractor_load);
    if (ohx.loaded != 0) {
        pthread_mutex_unlock(&g_extractor_load);
        return ohx.loaded > 0;
    }
    ohx.loaded = -1;
    source = dlopen("libnative_media_avsource.so", RTLD_NOW);
    demuxer = dlopen("libnative_media_avdemuxer.so", RTLD_NOW);
    core = dlopen("libnative_media_core.so", RTLD_NOW);
    base = dlopen("libnative_media_codecbase.so", RTLD_NOW);
    if (source == NULL || demuxer == NULL || core == NULL) {
        xlog("[WL-EXTRACTOR] OH demuxer unavailable: %s", dlerror());
        pthread_mutex_unlock(&g_extractor_load);
        return 0;
    }
#define XSYM(field, lib, name) (*(void**) &ohx.field = (lib) != NULL ? dlsym(lib, name) : NULL)
    XSYM(sourceFd, source, "OH_AVSource_CreateWithFD");
    XSYM(sourceDestroy, source, "OH_AVSource_Destroy");
    XSYM(sourceFormat, source, "OH_AVSource_GetSourceFormat");
    XSYM(trackFormat, source, "OH_AVSource_GetTrackFormat");
    XSYM(demuxerCreate, demuxer, "OH_AVDemuxer_CreateWithSource");
    XSYM(demuxerDestroy, demuxer, "OH_AVDemuxer_Destroy");
    XSYM(selectTrack, demuxer, "OH_AVDemuxer_SelectTrackByID");
    XSYM(unselectTrack, demuxer, "OH_AVDemuxer_UnselectTrackByID");
    XSYM(readSample, demuxer, "OH_AVDemuxer_ReadSampleBuffer");
    XSYM(seek, demuxer, "OH_AVDemuxer_SeekToTime");
    XSYM(bufferCreate, core, "OH_AVBuffer_Create");
    XSYM(bufferDestroy, core, "OH_AVBuffer_Destroy");
    XSYM(bufferAddr, core, "OH_AVBuffer_GetAddr");
    XSYM(bufferAttr, core, "OH_AVBuffer_GetBufferAttr");
    XSYM(formatDestroy, core, "OH_AVFormat_Destroy");
    XSYM(formatGetInt, core, "OH_AVFormat_GetIntValue");
    XSYM(formatGetLong, core, "OH_AVFormat_GetLongValue");
    XSYM(formatGetDouble, core, "OH_AVFormat_GetDoubleValue");
    XSYM(formatGetString, core, "OH_AVFormat_GetStringValue");
    XSYM(formatGetBuffer, core, "OH_AVFormat_GetBuffer");
    XSYM(formatDump, core, "OH_AVFormat_DumpInfo");
    /* The key strings live with the codec base declarations; some builds export them from the
     * core library instead, so each is looked up in both. */
#define XKEY(field, name) do { XSYM(field, base, name); if (ohx.field == NULL) XSYM(field, core, name); } while (0)
    XKEY(keyMime, "OH_MD_KEY_CODEC_MIME");
    XKEY(keyDuration, "OH_MD_KEY_DURATION");
    XKEY(keyBitrate, "OH_MD_KEY_BITRATE");
    XKEY(keyMaxInput, "OH_MD_KEY_MAX_INPUT_SIZE");
    XKEY(keyWidth, "OH_MD_KEY_WIDTH");
    XKEY(keyHeight, "OH_MD_KEY_HEIGHT");
    XKEY(keyFrameRate, "OH_MD_KEY_FRAME_RATE");
    XKEY(keyChannels, "OH_MD_KEY_AUD_CHANNEL_COUNT");
    XKEY(keySampleRate, "OH_MD_KEY_AUD_SAMPLE_RATE");
    XKEY(keyRotation, "OH_MD_KEY_ROTATION");
    XKEY(keyConfig, "OH_MD_KEY_CODEC_CONFIG");
    XKEY(keyTrackType, "OH_MD_KEY_TRACK_TYPE");
    XKEY(keyTrackCount, "OH_MD_KEY_TRACK_COUNT");
    XKEY(keyLanguage, "OH_MD_KEY_LANGUAGE");
    XKEY(keyProfile, "OH_MD_KEY_PROFILE");
#undef XKEY
#undef XSYM
    if (!ohx.sourceFd || !ohx.sourceDestroy || !ohx.trackFormat || !ohx.demuxerCreate
            || !ohx.demuxerDestroy || !ohx.selectTrack || !ohx.readSample || !ohx.seek
            || !ohx.bufferCreate || !ohx.bufferDestroy || !ohx.bufferAddr || !ohx.bufferAttr
            || !ohx.formatDestroy || !ohx.formatGetInt || !ohx.formatGetLong || !ohx.formatGetString
            || !ohx.keyMime || !ohx.keyTrackCount) {
        xlog("[WL-EXTRACTOR] OH demuxer is missing a required entry point");
        pthread_mutex_unlock(&g_extractor_load);
        return 0;
    }
    ohx.loaded = 1;
    pthread_mutex_unlock(&g_extractor_load);
    return 1;
}

typedef struct Track {
    int selected;
    OH_AVBuffer* buffer;      /* holds this track's pending sample */
    int capacity;
    int pending;              /* 1 when buffer holds a sample not yet consumed */
    int ended;                /* 1 once OH reported end of stream for it */
    OH_AVCodecBufferAttr attr;
} Track;

typedef struct Extractor {
    pthread_mutex_t lock;
    int fd;
    int64_t offset, length;
    OH_AVSource* source;
    OH_AVDemuxer* demuxer;
    int tracks;
    Track track[MAX_TRACKS];
} Extractor;

static jfieldID g_extractor_context;
static jfieldID g_extractor_descriptor;   /* java.io.FileDescriptor's own int */
static jclass g_hash_map;
static jmethodID g_hash_map_init;
static jmethodID g_hash_map_put;
static jmethodID g_integer_value;
static jmethodID g_long_value;
static jmethodID g_float_value;
static jclass g_integer;
static jclass g_long;
static jclass g_float;

static Extractor* extractor_of(JNIEnv* env, jobject thiz)
{
    return g_extractor_context != NULL
        ? (Extractor*) (uintptr_t) (*env)->GetLongField(env, thiz, g_extractor_context) : NULL;
}

static void throw_at(JNIEnv* env, const char* cls, const char* message)
{
    jclass c = (*env)->FindClass(env, cls);
    if (c != NULL) (*env)->ThrowNew(env, c, message);
}

static Extractor* require_source(JNIEnv* env, jobject thiz)
{
    Extractor* x = extractor_of(env, thiz);
    if (x == NULL || x->demuxer == NULL) {
        throw_at(env, "java/lang/IllegalStateException", NULL);
        return NULL;
    }
    return x;
}

/* ---- the sample held per track ------------------------------------------------------------- */

static int track_buffer(Extractor* x, int index, int capacity)
{
    Track* track = &x->track[index];
    if (track->buffer != NULL && track->capacity >= capacity) return 1;
    if (track->buffer != NULL) {
        ohx.bufferDestroy(track->buffer);
        track->buffer = NULL;
        track->pending = 0;
    }
    track->buffer = ohx.bufferCreate(capacity);
    track->capacity = track->buffer != NULL ? capacity : 0;
    return track->buffer != NULL;
}

/* The size OH's largest sample on a track can need, as its format reports it. */
static int track_capacity(Extractor* x, int index)
{
    OH_AVFormat* format = ohx.trackFormat(x->source, (uint32_t) index);
    int32_t size = 0;
    int capacity;
    if (format != NULL) {
        if (ohx.keyMaxInput == NULL || !ohx.formatGetInt(format, *ohx.keyMaxInput, &size)) size = 0;
        ohx.formatDestroy(format);
    }
    capacity = size > 0 ? (int) size : 1024 * 1024;
    return capacity < 64 * 1024 ? 64 * 1024 : capacity;
}

/* Reads one sample of a selected track into its own buffer, unless it already holds one. */
static void fill_track(Extractor* x, int index)
{
    Track* track = &x->track[index];
    int rc;
    if (!track->selected || track->pending || track->ended) return;
    if (!track_buffer(x, index, track->capacity > 0 ? track->capacity : track_capacity(x, index))) {
        track->ended = 1;
        return;
    }
    rc = ohx.readSample(x->demuxer, (uint32_t) index, track->buffer);
    if (rc != 0 && track->capacity < 16 * 1024 * 1024) {
        /* A sample larger than the format's own maximum: give the track a bigger buffer once
         * before believing the stream ended, or the rest of it would be dropped silently. */
        if (track_buffer(x, index, track->capacity * 4))
            rc = ohx.readSample(x->demuxer, (uint32_t) index, track->buffer);
    }
    if (rc != 0) {
        /* OH reports the end of a track as an error on the read after its last sample. */
        track->ended = 1;
        return;
    }
    memset(&track->attr, 0, sizeof(track->attr));
    if (ohx.bufferAttr(track->buffer, &track->attr) != 0) {
        track->ended = 1;
        return;
    }
    if ((track->attr.flags & (uint32_t) OH_FLAG_EOS) != 0 && track->attr.size <= 0) {
        track->ended = 1;
        return;
    }
    track->pending = 1;
}

/* Android's current sample: the earliest pending one across the selected tracks, or -1. */
static int current_track(Extractor* x)
{
    int best = -1;
    int i;
    for (i = 0; i < x->tracks; i++) {
        fill_track(x, i);
        if (!x->track[i].pending) continue;
        if (best < 0 || x->track[i].attr.pts < x->track[best].attr.pts) best = i;
    }
    return best;
}

static void drop_samples(Extractor* x)
{
    int i;
    for (i = 0; i < x->tracks; i++) {
        x->track[i].pending = 0;
        x->track[i].ended = 0;
    }
}

/* ---- formats ------------------------------------------------------------------------------- */

static void map_put_string(JNIEnv* env, jobject map, const char* key, const char* value)
{
    jstring k = (*env)->NewStringUTF(env, key);
    jstring v = (*env)->NewStringUTF(env, value);
    if (k != NULL && v != NULL) (*env)->CallObjectMethod(env, map, g_hash_map_put, k, v);
    if (k != NULL) (*env)->DeleteLocalRef(env, k);
    if (v != NULL) (*env)->DeleteLocalRef(env, v);
}

static void map_put_int(JNIEnv* env, jobject map, const char* key, jint value)
{
    jstring k = (*env)->NewStringUTF(env, key);
    jobject v = (*env)->NewObject(env, g_integer, g_integer_value, value);
    if (k != NULL && v != NULL) (*env)->CallObjectMethod(env, map, g_hash_map_put, k, v);
    if (k != NULL) (*env)->DeleteLocalRef(env, k);
    if (v != NULL) (*env)->DeleteLocalRef(env, v);
}

static void map_put_long(JNIEnv* env, jobject map, const char* key, jlong value)
{
    jstring k = (*env)->NewStringUTF(env, key);
    jobject v = (*env)->NewObject(env, g_long, g_long_value, value);
    if (k != NULL && v != NULL) (*env)->CallObjectMethod(env, map, g_hash_map_put, k, v);
    if (k != NULL) (*env)->DeleteLocalRef(env, k);
    if (v != NULL) (*env)->DeleteLocalRef(env, v);
}

static void map_put_float(JNIEnv* env, jobject map, const char* key, jfloat value)
{
    jstring k = (*env)->NewStringUTF(env, key);
    jobject v = (*env)->NewObject(env, g_float, g_float_value, value);
    if (k != NULL && v != NULL) (*env)->CallObjectMethod(env, map, g_hash_map_put, k, v);
    if (k != NULL) (*env)->DeleteLocalRef(env, k);
    if (v != NULL) (*env)->DeleteLocalRef(env, v);
}

/* The codec-specific data a decoder is configured with, as MediaFormat's csd-0 ByteBuffer. */
static void map_put_config(JNIEnv* env, jobject map, OH_AVFormat* format)
{
    uint8_t* data = NULL;
    size_t size = 0;
    jobject buffer;
    jstring key;
    jclass byte_buffer;
    jmethodID wrap;
    jbyteArray bytes;
    if (ohx.formatGetBuffer == NULL || ohx.keyConfig == NULL) return;
    if (!ohx.formatGetBuffer(format, *ohx.keyConfig, &data, &size) || data == NULL || size == 0) return;
    byte_buffer = (*env)->FindClass(env, "java/nio/ByteBuffer");
    wrap = byte_buffer != NULL ? (*env)->GetStaticMethodID(env, byte_buffer, "wrap",
                                                            "([B)Ljava/nio/ByteBuffer;") : NULL;
    bytes = wrap != NULL ? (*env)->NewByteArray(env, (jsize) size) : NULL;
    if (bytes == NULL) {
        (*env)->ExceptionClear(env);
        return;
    }
    (*env)->SetByteArrayRegion(env, bytes, 0, (jsize) size, (const jbyte*) data);
    buffer = (*env)->CallStaticObjectMethod(env, byte_buffer, wrap, bytes);
    key = (*env)->NewStringUTF(env, "csd-0");
    if (buffer != NULL && key != NULL) (*env)->CallObjectMethod(env, map, g_hash_map_put, key, buffer);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

/* An OH format as the Map that MediaFormat is built from. Keys are MediaFormat's own. */
/*
 * A track's profile in Android's numbering (MediaCodecInfo.CodecProfileLevel), from OH's (OH_AACProfile,
 * OH_AVCProfile, OH_HEVCProfile), which numbers the same profiles differently: AAC LC is 0 to OH and 2
 * to Android, AVC Main is 8 to OH and High to Android. Passed through, the number named another
 * profile, and MediaCodecList.findDecoderForFormat answered null for an AAC track, since no decoder
 * offers profile 0. Returns -1 for a profile without an Android number, which leaves the key out, as
 * Android's own extractors do when they do not know it.
 */
static int android_profile(const char* mime, int32_t oh)
{
    if (mime == NULL) return -1;
    if (strcmp(mime, "audio/mp4a-latm") == 0) {
        switch (oh) {
        case 0: return 2;      /* AACObjectLC */
        case 3: return 5;      /* AACObjectHE */
        case 4: return 29;     /* AACObjectHE_PS */
        default: return -1;
        }
    }
    if (strcmp(mime, "video/avc") == 0) {
        switch (oh) {
        case 0: return 0x01;      /* AVCProfileBaseline */
        case 1: return 0x10000;   /* AVCProfileConstrainedBaseline */
        case 2: return 0x80000;   /* AVCProfileConstrainedHigh */
        case 3: return 0x04;      /* AVCProfileExtended */
        case 4: return 0x08;      /* AVCProfileHigh */
        case 5: return 0x10;      /* AVCProfileHigh10 */
        case 6: return 0x20;      /* AVCProfileHigh422 */
        case 7: return 0x40;      /* AVCProfileHigh444 */
        case 8: return 0x02;      /* AVCProfileMain */
        default: return -1;
        }
    }
    if (strcmp(mime, "video/hevc") == 0) {
        switch (oh) {
        case 0: return 0x01;      /* HEVCProfileMain */
        case 1: return 0x02;      /* HEVCProfileMain10 */
        case 2: return 0x04;      /* HEVCProfileMainStill */
        case 3: return 0x1000;    /* HEVCProfileMain10HDR10 */
        case 4: return 0x2000;    /* HEVCProfileMain10HDR10Plus */
        default: return -1;
        }
    }
    return -1;
}

static jobject format_map(JNIEnv* env, OH_AVFormat* format, int is_track)
{
    jobject map = (*env)->NewObject(env, g_hash_map, g_hash_map_init);
    const char* text = NULL;
    int32_t number = 0;
    int64_t big = 0;
    double real = 0;
    if (map == NULL) return NULL;
    char mime[64] = "";
    if (ohx.keyMime != NULL && ohx.formatGetString(format, *ohx.keyMime, &text) && text != NULL) {
        map_put_string(env, map, "mime", text);
        snprintf(mime, sizeof(mime), "%s", text);
    }
    if (ohx.keyDuration != NULL && ohx.formatGetLong(format, *ohx.keyDuration, &big))
        map_put_long(env, map, "durationUs", (jlong) big);
    if (ohx.keyBitrate != NULL) {
        if (ohx.formatGetLong(format, *ohx.keyBitrate, &big)) map_put_int(env, map, "bitrate", (jint) big);
        else if (ohx.formatGetInt(format, *ohx.keyBitrate, &number))
            map_put_int(env, map, "bitrate", (jint) number);
    }
    if (ohx.keyWidth != NULL && ohx.formatGetInt(format, *ohx.keyWidth, &number))
        map_put_int(env, map, "width", (jint) number);
    if (ohx.keyHeight != NULL && ohx.formatGetInt(format, *ohx.keyHeight, &number))
        map_put_int(env, map, "height", (jint) number);
    if (ohx.keyChannels != NULL && ohx.formatGetInt(format, *ohx.keyChannels, &number))
        map_put_int(env, map, "channel-count", (jint) number);
    if (ohx.keySampleRate != NULL && ohx.formatGetInt(format, *ohx.keySampleRate, &number))
        map_put_int(env, map, "sample-rate", (jint) number);
    if (ohx.keyRotation != NULL && ohx.formatGetInt(format, *ohx.keyRotation, &number))
        map_put_int(env, map, "rotation-degrees", (jint) number);
    if (ohx.keyMaxInput != NULL && ohx.formatGetInt(format, *ohx.keyMaxInput, &number))
        map_put_int(env, map, "max-input-size", (jint) number);
    if (ohx.keyProfile != NULL && ohx.formatGetInt(format, *ohx.keyProfile, &number)
            && android_profile(mime, number) >= 0)
        map_put_int(env, map, "profile", (jint) android_profile(mime, number));
    if (ohx.keyFrameRate != NULL) {
        if (ohx.formatGetDouble != NULL && ohx.formatGetDouble(format, *ohx.keyFrameRate, &real))
            map_put_float(env, map, "frame-rate", (jfloat) real);
        else if (ohx.formatGetInt(format, *ohx.keyFrameRate, &number))
            map_put_int(env, map, "frame-rate", (jint) number);
    }
    if (ohx.keyLanguage != NULL && ohx.formatGetString(format, *ohx.keyLanguage, &text) && text != NULL)
        map_put_string(env, map, "language", text);
    if (ohx.keyTrackCount != NULL && ohx.formatGetInt(format, *ohx.keyTrackCount, &number))
        map_put_int(env, map, "track-count", (jint) number);
    if (is_track) map_put_config(env, map, format);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    return map;
}

/* ---- the natives --------------------------------------------------------------------------- */

static void extractor_close(Extractor* x)
{
    int i;
    for (i = 0; i < MAX_TRACKS; i++) {
        if (x->track[i].buffer != NULL) ohx.bufferDestroy(x->track[i].buffer);
        memset(&x->track[i], 0, sizeof(x->track[i]));
    }
    if (x->demuxer != NULL) { ohx.demuxerDestroy(x->demuxer); x->demuxer = NULL; }
    if (x->source != NULL) { ohx.sourceDestroy(x->source); x->source = NULL; }
    if (x->fd >= 0) { close(x->fd); x->fd = -1; }
    x->tracks = 0;
}

static int extractor_open(Extractor* x, int fd, int64_t offset, int64_t length)
{
    OH_AVFormat* format;
    int32_t count = 0;
    extractor_close(x);
    x->fd = fd;
    x->offset = offset;
    x->length = length;
    x->source = ohx.sourceFd(fd, offset, length);
    if (x->source == NULL) return 0;
    x->demuxer = ohx.demuxerCreate(x->source);
    if (x->demuxer == NULL) return 0;
    format = ohx.sourceFormat != NULL ? ohx.sourceFormat(x->source) : NULL;
    if (format != NULL) {
        if (!ohx.formatGetInt(format, *ohx.keyTrackCount, &count)) count = 0;
        if (ohx.formatDump != NULL) {
            const char* dump = ohx.formatDump(format);
            if (dump != NULL) xlog("[WL-EXTRACTOR] source: %.180s", dump);
        }
        ohx.formatDestroy(format);
    }
    if (count <= 0) {
        /* No track count in the source format: count the tracks that have one. */
        for (count = 0; count < MAX_TRACKS; count++) {
            OH_AVFormat* track = ohx.trackFormat(x->source, (uint32_t) count);
            if (track == NULL) break;
            ohx.formatDestroy(track);
        }
        xlog("[WL-EXTRACTOR] source format has no track count; counted %d", (int) count);
    }
    x->tracks = count > MAX_TRACKS ? MAX_TRACKS : (int) count;
    return 1;
}

static void x_set_source_fd(JNIEnv* env, jobject thiz, jobject fd_object, jlong offset, jlong length)
{
    Extractor* x = extractor_of(env, thiz);
    struct stat status;
    int fd;
    if (x == NULL || fd_object == NULL || g_extractor_descriptor == NULL) {
        throw_at(env, "java/lang/IllegalArgumentException", NULL);
        return;
    }
    if (!load_oh_demuxer()) {
        throw_at(env, "java/io/IOException", "setDataSource failed");
        return;
    }
    fd = fcntl((*env)->GetIntField(env, fd_object, g_extractor_descriptor), F_DUPFD_CLOEXEC, 0);
    if (fd < 0) {
        throw_at(env, "java/lang/IllegalArgumentException", "cannot duplicate the descriptor");
        return;
    }
    /* A source without a range passes 0x7ffffffffffffff; OH wants the real size. */
    if (fstat(fd, &status) == 0 && S_ISREG(status.st_mode)
            && (length <= 0 || offset + length > status.st_size)) {
        length = status.st_size > offset ? status.st_size - offset : 0;
    }
    pthread_mutex_lock(&x->lock);
    if (!extractor_open(x, fd, offset, length)) {
        extractor_close(x);
        pthread_mutex_unlock(&x->lock);
        throw_at(env, "java/io/IOException", "setDataSource failed");
        return;
    }
    pthread_mutex_unlock(&x->lock);
}

static void x_set_source_path(JNIEnv* env, jobject thiz, jobject binder, jstring path,
                              jobjectArray keys, jobjectArray values)
{
    Extractor* x = extractor_of(env, thiz);
    const char* chars;
    struct stat status;
    int fd;
    (void) binder; (void) keys; (void) values;
    if (x == NULL || path == NULL) {
        throw_at(env, "java/lang/IllegalArgumentException", NULL);
        return;
    }
    if (!load_oh_demuxer()) {
        throw_at(env, "java/io/IOException", "setDataSource failed");
        return;
    }
    chars = (*env)->GetStringUTFChars(env, path, NULL);
    if (chars == NULL) return;
    /* A local file, by path or file:// URI. A network or content URI is read by the media service
     * on Android; OH's source takes a descriptor here, so those answer as AOSP does when its
     * service cannot open the source. */
    fd = open(strncmp(chars, "file://", 7) == 0 ? chars + 7 : chars, O_RDONLY | O_CLOEXEC);
    (*env)->ReleaseStringUTFChars(env, path, chars);
    if (fd < 0) {
        throw_at(env, "java/io/IOException", "setDataSource failed");
        return;
    }
    pthread_mutex_lock(&x->lock);
    if (fstat(fd, &status) != 0) status.st_size = 0;
    if (!extractor_open(x, fd, 0, status.st_size)) {
        extractor_close(x);
        pthread_mutex_unlock(&x->lock);
        throw_at(env, "java/io/IOException", "setDataSource failed");
        return;
    }
    pthread_mutex_unlock(&x->lock);
}

static void x_set_source_callback(JNIEnv* env, jobject thiz, jobject source)
{
    (void) thiz; (void) source;
    throw_at(env, "java/lang/IllegalArgumentException", "MediaDataSource is not supported");
}

static jint x_track_count(JNIEnv* env, jobject thiz)
{
    Extractor* x = require_source(env, thiz);
    return x != NULL ? (jint) x->tracks : 0;
}

static jobject x_file_format(JNIEnv* env, jobject thiz)
{
    Extractor* x = require_source(env, thiz);
    jobject map = NULL;
    if (x == NULL) return NULL;
    pthread_mutex_lock(&x->lock);
    {
        OH_AVFormat* format = ohx.sourceFormat != NULL ? ohx.sourceFormat(x->source) : NULL;
        if (format != NULL) {
            map = format_map(env, format, 0);
            ohx.formatDestroy(format);
        }
    }
    pthread_mutex_unlock(&x->lock);
    if (map == NULL) map = (*env)->NewObject(env, g_hash_map, g_hash_map_init);
    return map;
}

static jobject x_track_format(JNIEnv* env, jobject thiz, jint index)
{
    Extractor* x = require_source(env, thiz);
    jobject map = NULL;
    if (x == NULL) return NULL;
    if (index < 0 || index >= x->tracks) {
        throw_at(env, "java/lang/IllegalArgumentException", NULL);
        return NULL;
    }
    pthread_mutex_lock(&x->lock);
    {
        OH_AVFormat* format = ohx.trackFormat(x->source, (uint32_t) index);
        if (format != NULL) {
            map = format_map(env, format, 1);
            ohx.formatDestroy(format);
        }
    }
    pthread_mutex_unlock(&x->lock);
    if (map == NULL) map = (*env)->NewObject(env, g_hash_map, g_hash_map_init);
    return map;
}

static void x_select_track(JNIEnv* env, jobject thiz, jint index)
{
    Extractor* x = require_source(env, thiz);
    if (x == NULL) return;
    if (index < 0 || index >= x->tracks) {
        throw_at(env, "java/lang/IllegalArgumentException", NULL);
        return;
    }
    pthread_mutex_lock(&x->lock);
    if (!x->track[index].selected && ohx.selectTrack(x->demuxer, (uint32_t) index) == 0) {
        x->track[index].selected = 1;
        x->track[index].ended = 0;
    }
    pthread_mutex_unlock(&x->lock);
}

static void x_unselect_track(JNIEnv* env, jobject thiz, jint index)
{
    Extractor* x = require_source(env, thiz);
    if (x == NULL) return;
    if (index < 0 || index >= x->tracks) {
        throw_at(env, "java/lang/IllegalArgumentException", NULL);
        return;
    }
    pthread_mutex_lock(&x->lock);
    if (x->track[index].selected && ohx.unselectTrack != NULL)
        (void) ohx.unselectTrack(x->demuxer, (uint32_t) index);
    x->track[index].selected = 0;
    x->track[index].pending = 0;
    pthread_mutex_unlock(&x->lock);
}

static void x_seek_to(JNIEnv* env, jobject thiz, jlong time_us, jint mode)
{
    Extractor* x = require_source(env, thiz);
    /* Android's SEEK_TO_PREVIOUS_SYNC(0), NEXT_SYNC(1), CLOSEST_SYNC(2) against OH's, which
     * numbers NEXT_SYNC 0, PREVIOUS_SYNC 1 and CLOSEST_SYNC 2. */
    static const int kModes[] = { 1, 0, 2 };
    if (x == NULL) return;
    pthread_mutex_lock(&x->lock);
    drop_samples(x);
    (void) ohx.seek(x->demuxer, time_us < 0 ? 0 : time_us / 1000,
                    mode >= 0 && mode < (jint) COUNT(kModes) ? kModes[mode] : 2);
    pthread_mutex_unlock(&x->lock);
}

static jboolean x_advance(JNIEnv* env, jobject thiz)
{
    Extractor* x = require_source(env, thiz);
    jboolean more = JNI_FALSE;
    if (x == NULL) return JNI_FALSE;
    pthread_mutex_lock(&x->lock);
    {
        int current = current_track(x);
        if (current >= 0) {
            x->track[current].pending = 0;
            more = current_track(x) >= 0 ? JNI_TRUE : JNI_FALSE;
        }
    }
    pthread_mutex_unlock(&x->lock);
    return more;
}

static jint x_read_sample(JNIEnv* env, jobject thiz, jobject buffer, jint offset)
{
    Extractor* x = require_source(env, thiz);
    jint written = -1;
    if (x == NULL || buffer == NULL) return -1;
    pthread_mutex_lock(&x->lock);
    {
        int current = current_track(x);
        if (current >= 0) {
            const Track* track = &x->track[current];
            uint8_t* data = ohx.bufferAddr(track->buffer);
            jlong capacity = (*env)->GetDirectBufferCapacity(env, buffer);
            void* address = (*env)->GetDirectBufferAddress(env, buffer);
            if (data != NULL && address != NULL && offset >= 0
                    && capacity - offset >= (jlong) track->attr.size) {
                memcpy((uint8_t*) address + offset, data + track->attr.offset,
                       (size_t) track->attr.size);
                written = (jint) track->attr.size;
            } else if (data != NULL && address == NULL) {
                /* A heap ByteBuffer: Android copies into its backing array. */
                jclass c = (*env)->GetObjectClass(env, buffer);
                jmethodID has_array = (*env)->GetMethodID(env, c, "hasArray", "()Z");
                jmethodID array = (*env)->GetMethodID(env, c, "array", "()[B");
                jmethodID array_offset = (*env)->GetMethodID(env, c, "arrayOffset", "()I");
                if (has_array != NULL && array != NULL && array_offset != NULL
                        && (*env)->CallBooleanMethod(env, buffer, has_array)) {
                    jbyteArray bytes = (jbyteArray) (*env)->CallObjectMethod(env, buffer, array);
                    jint base = (*env)->CallIntMethod(env, buffer, array_offset);
                    if (bytes != NULL && (*env)->GetArrayLength(env, bytes) - base - offset
                            >= (jint) track->attr.size) {
                        (*env)->SetByteArrayRegion(env, bytes, base + offset, track->attr.size,
                                                   (const jbyte*) (data + track->attr.offset));
                        written = (jint) track->attr.size;
                    }
                }
                if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
            }
            if (written < 0) {
                pthread_mutex_unlock(&x->lock);
                throw_at(env, "java/lang/IllegalArgumentException", "the buffer is too small");
                return -1;
            }
        }
    }
    pthread_mutex_unlock(&x->lock);
    if (written >= 0) {
        /* MediaExtractor leaves the buffer's position at 0 and its limit at the sample's size. */
        jclass c = (*env)->GetObjectClass(env, buffer);
        jmethodID position = (*env)->GetMethodID(env, c, "position", "(I)Ljava/nio/Buffer;");
        jmethodID limit = (*env)->GetMethodID(env, c, "limit", "(I)Ljava/nio/Buffer;");
        if (position != NULL && limit != NULL) {
            (*env)->CallObjectMethod(env, buffer, position, offset);
            (*env)->CallObjectMethod(env, buffer, limit, offset + written);
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    }
    return written;
}

static jint x_sample_track(JNIEnv* env, jobject thiz)
{
    Extractor* x = require_source(env, thiz);
    jint index = -1;
    if (x == NULL) return -1;
    pthread_mutex_lock(&x->lock);
    index = (jint) current_track(x);
    pthread_mutex_unlock(&x->lock);
    return index;
}

static jlong x_sample_time(JNIEnv* env, jobject thiz)
{
    Extractor* x = require_source(env, thiz);
    jlong time = -1;
    if (x == NULL) return -1;
    pthread_mutex_lock(&x->lock);
    {
        int current = current_track(x);
        if (current >= 0) time = (jlong) x->track[current].attr.pts;
    }
    pthread_mutex_unlock(&x->lock);
    return time;
}

static jlong x_sample_size(JNIEnv* env, jobject thiz)
{
    Extractor* x = require_source(env, thiz);
    jlong size = -1;
    if (x == NULL) return -1;
    pthread_mutex_lock(&x->lock);
    {
        int current = current_track(x);
        if (current >= 0) size = (jlong) x->track[current].attr.size;
    }
    pthread_mutex_unlock(&x->lock);
    return size;
}

static jint x_sample_flags(JNIEnv* env, jobject thiz)
{
    Extractor* x = require_source(env, thiz);
    jint flags = -1;
    if (x == NULL) return -1;
    pthread_mutex_lock(&x->lock);
    {
        int current = current_track(x);
        if (current >= 0) {
            uint32_t oh = x->track[current].attr.flags;
            flags = 0;
            if ((oh & (uint32_t) OH_FLAG_SYNC) != 0) flags |= ANDROID_FLAG_SYNC;
            if ((oh & (uint32_t) OH_FLAG_INCOMPLETE) != 0) flags |= ANDROID_FLAG_PARTIAL;
        }
    }
    pthread_mutex_unlock(&x->lock);
    return flags;
}

static jboolean x_no_crypto(JNIEnv* env, jobject thiz, jobject info)
{
    (void) env; (void) thiz; (void) info;
    return JNI_FALSE;   /* no conditional access here: AOSP answers false for a clear sample */
}

static void x_set_cas(JNIEnv* env, jobject thiz, jobject binder)
{
    (void) thiz; (void) binder;
    throw_at(env, "java/lang/IllegalStateException", "MediaCas is not supported");
}

static jlong x_cached_duration(JNIEnv* env, jobject thiz)
{
    (void) env; (void) thiz;
    return -1;          /* AOSP's answer for a source whose cache it cannot measure */
}

static jboolean x_cache_reached_eos(JNIEnv* env, jobject thiz)
{
    (void) env; (void) thiz;
    return JNI_FALSE;
}

static jobject x_null(JNIEnv* env, jobject thiz)
{
    (void) env; (void) thiz;
    return NULL;
}

static jobject x_no_presentations(JNIEnv* env, jobject thiz, jint index)
{
    jclass list = (*env)->FindClass(env, "java/util/ArrayList");
    jmethodID init = list != NULL ? (*env)->GetMethodID(env, list, "<init>", "()V") : NULL;
    (void) thiz; (void) index;
    return init != NULL ? (*env)->NewObject(env, list, init) : NULL;
}

static void x_set_log_session(JNIEnv* env, jobject thiz, jstring id)
{
    (void) env; (void) thiz; (void) id;
}

static void x_release(JNIEnv* env, jobject thiz)
{
    Extractor* x = extractor_of(env, thiz);
    if (x == NULL) return;
    pthread_mutex_lock(&x->lock);
    extractor_close(x);
    pthread_mutex_unlock(&x->lock);
}

static void x_finalize(JNIEnv* env, jobject thiz)
{
    Extractor* x = extractor_of(env, thiz);
    if (x == NULL) return;
    x_release(env, thiz);
    (*env)->SetLongField(env, thiz, g_extractor_context, 0);
    pthread_mutex_destroy(&x->lock);
    free(x);
}

static void x_setup(JNIEnv* env, jobject thiz)
{
    Extractor* x = calloc(1, sizeof(Extractor));
    if (x == NULL || g_extractor_context == NULL) {
        free(x);
        throw_at(env, "java/lang/RuntimeException", "Out of memory");
        return;
    }
    pthread_mutex_init(&x->lock, NULL);
    x->fd = -1;
    (*env)->SetLongField(env, thiz, g_extractor_context, (jlong) (uintptr_t) x);
}

static void x_native_init(JNIEnv* env, jclass c) { (void) env; (void) c; }

static const JNINativeMethod kMediaExtractor[] = {
    { "native_init", "()V", (void*) x_native_init },
    { "native_setup", "()V", (void*) x_setup },
    { "native_finalize", "()V", (void*) x_finalize },
    { "release", "()V", (void*) x_release },
    { "setDataSource", "(Ljava/io/FileDescriptor;JJ)V", (void*) x_set_source_fd },
    { "nativeSetDataSource", "(Landroid/os/IBinder;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V",
      (void*) x_set_source_path },
    { "setDataSource", "(Landroid/media/MediaDataSource;)V", (void*) x_set_source_callback },
    { "getTrackCount", "()I", (void*) x_track_count },
    { "getFileFormatNative", "()Ljava/util/Map;", (void*) x_file_format },
    { "getTrackFormatNative", "(I)Ljava/util/Map;", (void*) x_track_format },
    { "selectTrack", "(I)V", (void*) x_select_track },
    { "unselectTrack", "(I)V", (void*) x_unselect_track },
    { "seekTo", "(JI)V", (void*) x_seek_to },
    { "advance", "()Z", (void*) x_advance },
    { "readSampleData", "(Ljava/nio/ByteBuffer;I)I", (void*) x_read_sample },
    { "getSampleTrackIndex", "()I", (void*) x_sample_track },
    { "getSampleTime", "()J", (void*) x_sample_time },
    { "getSampleSize", "()J", (void*) x_sample_size },
    { "getSampleFlags", "()I", (void*) x_sample_flags },
    { "getSampleCryptoInfo", "(Landroid/media/MediaCodec$CryptoInfo;)Z", (void*) x_no_crypto },
    { "nativeSetMediaCas", "(Landroid/os/IHwBinder;)V", (void*) x_set_cas },
    { "getCachedDuration", "()J", (void*) x_cached_duration },
    { "hasCacheReachedEndOfStream", "()Z", (void*) x_cache_reached_eos },
    { "native_getMetrics", "()Landroid/os/PersistableBundle;", (void*) x_null },
    { "native_setLogSessionId", "(Ljava/lang/String;)V", (void*) x_set_log_session },
    { "native_getAudioPresentations", "(I)Ljava/util/List;", (void*) x_no_presentations },
};

/* The boxed types and the map the formats are returned in. Returns 1 when all were found. */
static int cache_format_classes(JNIEnv* env)
{
    jclass descriptor = (*env)->FindClass(env, "java/io/FileDescriptor");
    jclass map = (*env)->FindClass(env, "java/util/HashMap");
    jclass integer = (*env)->FindClass(env, "java/lang/Integer");
    jclass big = (*env)->FindClass(env, "java/lang/Long");
    jclass real = (*env)->FindClass(env, "java/lang/Float");
    if (map == NULL || integer == NULL || big == NULL || real == NULL || descriptor == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    g_extractor_descriptor = (*env)->GetFieldID(env, descriptor, "descriptor", "I");
    g_hash_map = (jclass) (*env)->NewGlobalRef(env, map);
    g_integer = (jclass) (*env)->NewGlobalRef(env, integer);
    g_long = (jclass) (*env)->NewGlobalRef(env, big);
    g_float = (jclass) (*env)->NewGlobalRef(env, real);
    g_hash_map_init = (*env)->GetMethodID(env, map, "<init>", "()V");
    g_hash_map_put = (*env)->GetMethodID(env, map, "put",
                                          "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    g_integer_value = (*env)->GetMethodID(env, integer, "<init>", "(I)V");
    g_long_value = (*env)->GetMethodID(env, big, "<init>", "(J)V");
    g_float_value = (*env)->GetMethodID(env, real, "<init>", "(F)V");
    if (g_hash_map_init == NULL || g_hash_map_put == NULL || g_integer_value == NULL
            || g_long_value == NULL || g_float_value == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    return 1;
}

/* Returns how many of MediaExtractor's natives were bound. */
int wl_register_extractor(JNIEnv* env)
{
    jclass extractor;
    int bound = 0;
    int i;
    if (!cache_format_classes(env)) return 0;
    extractor = (*env)->FindClass(env, "android/media/MediaExtractor");
    if (extractor == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    /* The natives first: the field id below initializes the class, and its initializer calls
     * native_init. A method this runtime does not declare native is left out, as elsewhere. */
    for (i = 0; i < COUNT(kMediaExtractor); i++) {
        if ((*env)->RegisterNatives(env, extractor, &kMediaExtractor[i], 1) == JNI_OK) bound++;
        else (*env)->ExceptionClear(env);
    }
    g_extractor_context = (*env)->GetFieldID(env, extractor, "mNativeContext", "J");
    if (g_extractor_context == NULL) {
        (*env)->ExceptionClear(env);
        (*env)->UnregisterNatives(env, extractor);
        bound = 0;
    }
    (*env)->DeleteLocalRef(env, extractor);
    return bound;
}
