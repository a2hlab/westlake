/*
 * Android's OpenSL ES, on top of OH's.
 *
 * Two incompatibilities, both measured on the board:
 *
 * 1. The ABI. OH's SLES/OpenSLES_Platform.h has `typedef unsigned long sl_uint32_t`, so on arm64
 *    SLuint32, SLint32, SLboolean, SLresult and SLmillisecond are 8 bytes; Android's header uses
 *    `unsigned int`, 4 bytes. OH therefore reads an Android app's SLDataFormat_PCM with 8-byte
 *    fields -- channels and sample rate fused into one value, "channel count not supported" --
 *    reads a locator's buffer count as 0, widens nothing in SLboolean arrays, and writes 8 bytes
 *    into the app's 4-byte out-parameters. On arm64 a 32-bit argument's upper register half is
 *    also undefined, so even by-value SLuint32 arguments must be widened before OH sees them.
 *
 * 2. The buffer queue. OH answers SL_IID_BUFFERQUEUE and Android's SL_IID_ANDROIDSIMPLEBUFFERQUEUE
 *    with FEATURE_UNSUPPORTED and offers SL_IID_OH_BUFFERQUEUE, which pulls: OH calls back when it
 *    wants data, the app takes a buffer with GetBuffer, fills it and Enqueues it.
 *
 * 3. Android's configuration interface, SL_IID_ANDROIDCONFIGURATION. OH's library has no such
 *    symbol, so Godot's and Oboe's libraries, which import it, failed to load at all (Ball2Box,
 *    Jigsaw, V3, Rhythm Engine). It is Android-only: the ID is exported here with AOSP's value,
 *    kept out of the interface lists OH sees, and answered by a per-object interface that takes
 *    and reports the keys AOSP's player and recorder take (stream type, recording preset,
 *    performance mode). They are hints to Android's audio policy; OH's player has no
 *    equivalent, so they are recorded, not applied. The Java-proxy calls are unsupported.
 *
 * 4. Interface IDs by address. OH's library compares an interface ID by its address, not its
 *    contents. An app library loaded in the isolated Android namespace binds its SL_IID_*
 *    references to that namespace's copy of libOpenSLES, whose IDs have other addresses, so OH
 *    refused Godot's own SL_IID_ENGINE (Ball2Box: "res != 0" at audio_driver_opensl.cpp:120, no
 *    audio). Every ID handed to OH is first swapped for OH's own by its contents.
 *
 * Mindustry's audio engine (SoLoud) was refused a player, ignored the refusal, and called through
 * the null interface -- a SIGSEGV at address 0 on its main thread.
 *
 * So the app never touches an OH object. slCreateEngine is interposed here (the shim is preloaded,
 * so an app library's reference binds to it) and every object and interface an app obtains is a
 * wrapper whose methods take Android's 4-byte types and call OH with OH's: engine, output mix,
 * player and recorder objects; the engine, play and volume interfaces; and Android's buffer queue,
 * implemented over OH's with Android's contract (app buffers are held until consumed, the app's
 * callback fires once per consumed buffer) driven by OH's pull. Data sources and sinks are
 * rebuilt in OH's layout. Interfaces not wrapped here are handed back raw and logged by name, so
 * the next gap announces itself instead of corrupting memory quietly.
 *
 * Everything in this file compiled against OH's headers is OH-ABI; the a_* types are Android's.
 */
/* OH's header declares slCreateEngine with OH's types; ours takes Android's. */
#define slCreateEngine westlake_oh_slCreateEngine_decl
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_OpenHarmony.h>
#undef slCreateEngine
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------ Android ABI types ------------ */

typedef uint32_t a_u32;
typedef int32_t a_i32;
typedef uint32_t a_bool;
typedef uint32_t a_result;

typedef const struct AObjectItf_ *const *AObjectItf;
typedef const struct AEngineItf_ *const *AEngineItf;
typedef const struct APlayItf_ *const *APlayItf;
typedef const struct AVolumeItf_ *const *AVolumeItf;
typedef const struct ASbqItf_ *const *ASbqItf;
typedef const struct AConfigItf_ *const *AConfigItf;

struct AConfigItf_ {
    a_result (*SetConfiguration)(AConfigItf, const SLchar *, const void *, a_u32);
    a_result (*GetConfiguration)(AConfigItf, const SLchar *, a_u32 *, void *);
    a_result (*AcquireJavaProxy)(AConfigItf, a_u32, void **);
    a_result (*ReleaseJavaProxy)(AConfigItf, a_u32);
};

typedef void (*AObjectCallback)(AObjectItf caller, const void *context, a_u32 event, a_result result,
                                a_u32 param, void *itf);
typedef void (*APlayCallback)(APlayItf caller, void *context, a_u32 event);
typedef void (*ASbqCallback)(ASbqItf caller, void *context);

typedef struct { void *pLocator; void *pFormat; } ADataSource;
typedef struct { void *pLocator; void *pFormat; } ADataSink;
typedef struct { a_u32 feature; a_u32 data; } AEngineOption;
typedef struct { a_u32 count; a_u32 index; } ASbqState;

struct AObjectItf_ {
    a_result (*Realize)(AObjectItf, a_bool);
    a_result (*Resume)(AObjectItf, a_bool);
    a_result (*GetState)(AObjectItf, a_u32 *);
    a_result (*GetInterface)(AObjectItf, const SLInterfaceID, void *);
    a_result (*RegisterCallback)(AObjectItf, AObjectCallback, void *);
    void (*AbortAsyncOperation)(AObjectItf);
    void (*Destroy)(AObjectItf);
    a_result (*SetPriority)(AObjectItf, a_i32, a_bool);
    a_result (*GetPriority)(AObjectItf, a_i32 *, a_bool *);
    a_result (*SetLossOfControlInterfaces)(AObjectItf, int16_t, SLInterfaceID *, a_bool);
};

struct AEngineItf_ {
    a_result (*CreateLEDDevice)(AEngineItf, AObjectItf *, a_u32, a_u32, const SLInterfaceID *, const a_bool *);
    a_result (*CreateVibraDevice)(AEngineItf, AObjectItf *, a_u32, a_u32, const SLInterfaceID *, const a_bool *);
    a_result (*CreateAudioPlayer)(AEngineItf, AObjectItf *, ADataSource *, ADataSink *, a_u32,
                                  const SLInterfaceID *, const a_bool *);
    a_result (*CreateAudioRecorder)(AEngineItf, AObjectItf *, ADataSource *, ADataSink *, a_u32,
                                    const SLInterfaceID *, const a_bool *);
    a_result (*CreateMidiPlayer)(AEngineItf, AObjectItf *, ADataSource *, ADataSource *, ADataSink *,
                                 ADataSink *, ADataSink *, a_u32, const SLInterfaceID *, const a_bool *);
    a_result (*CreateListener)(AEngineItf, AObjectItf *, a_u32, const SLInterfaceID *, const a_bool *);
    a_result (*Create3DGroup)(AEngineItf, AObjectItf *, a_u32, const SLInterfaceID *, const a_bool *);
    a_result (*CreateOutputMix)(AEngineItf, AObjectItf *, a_u32, const SLInterfaceID *, const a_bool *);
    a_result (*CreateMetadataExtractor)(AEngineItf, AObjectItf *, ADataSource *, a_u32,
                                        const SLInterfaceID *, const a_bool *);
    a_result (*CreateExtensionObject)(AEngineItf, AObjectItf *, void *, a_u32, a_u32,
                                      const SLInterfaceID *, const a_bool *);
    a_result (*QueryNumSupportedInterfaces)(AEngineItf, a_u32, a_u32 *);
    a_result (*QuerySupportedInterfaces)(AEngineItf, a_u32, a_u32, SLInterfaceID *);
    a_result (*QueryNumSupportedExtensions)(AEngineItf, a_u32 *);
    a_result (*QuerySupportedExtension)(AEngineItf, a_u32, SLchar *, int16_t *);
    a_result (*IsExtensionSupported)(AEngineItf, const SLchar *, a_bool *);
};

struct APlayItf_ {
    a_result (*SetPlayState)(APlayItf, a_u32);
    a_result (*GetPlayState)(APlayItf, a_u32 *);
    a_result (*GetDuration)(APlayItf, a_u32 *);
    a_result (*GetPosition)(APlayItf, a_u32 *);
    a_result (*RegisterCallback)(APlayItf, APlayCallback, void *);
    a_result (*SetCallbackEventsMask)(APlayItf, a_u32);
    a_result (*GetCallbackEventsMask)(APlayItf, a_u32 *);
    a_result (*SetMarkerPosition)(APlayItf, a_u32);
    a_result (*ClearMarkerPosition)(APlayItf);
    a_result (*GetMarkerPosition)(APlayItf, a_u32 *);
    a_result (*SetPositionUpdatePeriod)(APlayItf, a_u32);
    a_result (*GetPositionUpdatePeriod)(APlayItf, a_u32 *);
};

struct AVolumeItf_ {
    a_result (*SetVolumeLevel)(AVolumeItf, int16_t);
    a_result (*GetVolumeLevel)(AVolumeItf, int16_t *);
    a_result (*GetMaxVolumeLevel)(AVolumeItf, int16_t *);
    a_result (*SetMute)(AVolumeItf, a_bool);
    a_result (*GetMute)(AVolumeItf, a_bool *);
    a_result (*EnableStereoPosition)(AVolumeItf, a_bool);
    a_result (*IsEnabledStereoPosition)(AVolumeItf, a_bool *);
    a_result (*SetStereoPosition)(AVolumeItf, int16_t);
    a_result (*GetStereoPosition)(AVolumeItf, int16_t *);
};

struct ASbqItf_ {
    a_result (*Enqueue)(ASbqItf, const void *, a_u32);
    a_result (*Clear)(ASbqItf);
    a_result (*GetState)(ASbqItf, ASbqState *);
    a_result (*RegisterCallback)(ASbqItf, ASbqCallback, void *);
};

/* Android locator and format type values (SLES/OpenSLES.h, OpenSLES_Android.h). */
#define A_LOC_URI 0x1
#define A_LOC_IODEVICE 0x3
#define A_LOC_OUTPUTMIX 0x4
#define A_LOC_BUFFERQUEUE 0x6
#define A_LOC_ANDROID_SBQ 0x800007BD
#define A_FMT_MIME 0x1
#define A_FMT_PCM 0x2
#define A_FMT_PCM_EX 0x4

/* ------------------------------------------------------------ logging ---------------------- */

static void sles_log(const char *text)
{
    static const char tag[] = "[WESTLAKE-SLES] ";
    (void) write(2, tag, sizeof(tag) - 1);
    (void) write(2, text, strlen(text));
    (void) write(2, "\n", 1);
}

#define SLES_LOGF(...) do { char line_[256]; snprintf(line_, sizeof(line_), __VA_ARGS__); sles_log(line_); } while (0)

/* ------------------------------------------------------------ OH entry points -------------- */

static struct {
    SLresult (*create_engine)(SLObjectItf *, SLuint32, const SLEngineOption *, SLuint32,
                              const SLInterfaceID *, const SLboolean *);
    SLInterfaceID engine, play, volume, oh_bq, std_bq;
    void *lib;
} oh;

static int oh_resolve(void)
{
    static int done, ok;
    if (done) return ok;
    done = 1;
    void *lib = dlopen("libOpenSLES.so", RTLD_NOW);
    if (lib == NULL) { sles_log("libOpenSLES.so not loadable"); return 0; }
    oh.create_engine = (__typeof__(oh.create_engine)) dlsym(lib, "slCreateEngine");
    const SLInterfaceID *e = dlsym(lib, "SL_IID_ENGINE"), *p = dlsym(lib, "SL_IID_PLAY"),
                        *v = dlsym(lib, "SL_IID_VOLUME"), *q = dlsym(lib, "SL_IID_OH_BUFFERQUEUE"),
                        *s = dlsym(lib, "SL_IID_BUFFERQUEUE");
    ok = oh.create_engine && e && q;
    if (!ok) { sles_log("OH OpenSL ES symbols missing"); return 0; }
    oh.lib = lib;
    oh.engine = *e; oh.oh_bq = *q;
    oh.play = p ? *p : NULL; oh.volume = v ? *v : NULL; oh.std_bq = s ? *s : NULL;
    return ok;
}

/* Android's simple-buffer-queue ID. OH's libOpenSLES has no such symbol; PPSSPP and Mindustry
 * import it. Only its address matters -- apps pass it back, they do not inspect it. */
static const struct SLInterfaceID_ westlake_sl_iid_android_sbq = {
    0x198e4940, 0xc5d7, 0x11df, 0xa2a6, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
const SLInterfaceID SL_IID_ANDROIDSIMPLEBUFFERQUEUE = &westlake_sl_iid_android_sbq;

static int iid_is(SLInterfaceID a, SLInterfaceID b)
{
    return a != NULL && b != NULL && (a == b || memcmp(a, b, sizeof(*a)) == 0);
}

/* Android's configuration interface ID, from AOSP frameworks/wilhelm src/OpenSLES_IID.cpp. */
static const struct SLInterfaceID_ westlake_sl_iid_android_config = {
    0x89f6a7e0, 0xbeac, 0x11df, 0x8b5c, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}};
const SLInterfaceID SL_IID_ANDROIDCONFIGURATION = &westlake_sl_iid_android_config;

static const char *const oh_iid_names[] = {
    "SL_IID_3DCOMMIT", "SL_IID_3DDOPPLER", "SL_IID_3DGROUPING", "SL_IID_3DLOCATION",
    "SL_IID_3DMACROSCOPIC", "SL_IID_3DSOURCE", "SL_IID_AUDIODECODERCAPABILITIES",
    "SL_IID_AUDIOENCODER", "SL_IID_AUDIOENCODERCAPABILITIES", "SL_IID_AUDIOIODEVICECAPABILITIES",
    "SL_IID_BASSBOOST", "SL_IID_BUFFERQUEUE", "SL_IID_DEVICEVOLUME",
    "SL_IID_DYNAMICINTERFACEMANAGEMENT", "SL_IID_DYNAMICSOURCE", "SL_IID_EFFECTSEND",
    "SL_IID_ENGINE", "SL_IID_ENGINECAPABILITIES", "SL_IID_ENVIRONMENTALREVERB",
    "SL_IID_EQUALIZER", "SL_IID_LED", "SL_IID_METADATAEXTRACTION", "SL_IID_METADATATRAVERSAL",
    "SL_IID_MIDIMESSAGE", "SL_IID_MIDIMUTESOLO", "SL_IID_MIDITEMPO", "SL_IID_MIDITIME",
    "SL_IID_MUTESOLO", "SL_IID_NULL", "SL_IID_OBJECT", "SL_IID_OH_BUFFERQUEUE", "SL_IID_OUTPUTMIX",
    "SL_IID_PITCH", "SL_IID_PLAY", "SL_IID_PLAYBACKRATE", "SL_IID_PREFETCHSTATUS",
    "SL_IID_PRESETREVERB", "SL_IID_RATEPITCH", "SL_IID_RECORD", "SL_IID_SEEK",
    "SL_IID_THREADSYNC", "SL_IID_VIBRA", "SL_IID_VIRTUALIZER", "SL_IID_VISUALIZATION",
    "SL_IID_VOLUME"};
#define OH_IID_COUNT ((int) (sizeof(oh_iid_names) / sizeof(oh_iid_names[0])))
static SLInterfaceID oh_iids[OH_IID_COUNT];
static pthread_once_t oh_iids_once = PTHREAD_ONCE_INIT;

static void oh_iids_fill(void)
{
    for (int i = 0; i < OH_IID_COUNT; i++) {
        const SLInterfaceID *sym = dlsym(oh.lib, oh_iid_names[i]);
        oh_iids[i] = sym != NULL ? *sym : NULL;
    }
}

/* OH's own ID with the same contents as the app's, or the app's if OH has none. */
static SLInterfaceID canon_iid(SLInterfaceID id)
{
    if (id == NULL || oh.lib == NULL) return id;
    pthread_once(&oh_iids_once, oh_iids_fill);
    for (int i = 0; i < OH_IID_COUNT; i++) {
        if (oh_iids[i] != NULL && (oh_iids[i] == id || memcmp(oh_iids[i], id, sizeof(*id)) == 0)) {
            return oh_iids[i];
        }
    }
    return id;
}

static int iid_is_android_queue(SLInterfaceID id)
{
    return iid_is(id, SL_IID_ANDROIDSIMPLEBUFFERQUEUE) || iid_is(id, oh.std_bq);
}

/* ------------------------------------------------------------ Android buffer queue --------- */

#define SBQ_MAX 64
typedef struct {
    const struct ASbqItf_ *itf;   /* first: ASbqItf points here */
    SLOHBufferQueueItf oh_bq;
    pthread_mutex_t lock;
    const void *buf[SBQ_MAX];
    a_u32 size[SBQ_MAX];
    a_u32 head, count, offset, consumed;
    ASbqCallback callback;
    void *context;
} SbqAdapter;

static a_result sbq_enqueue(ASbqItf self, const void *buffer, a_u32 size)
{
    SbqAdapter *a = (SbqAdapter *) self;
    if (buffer == NULL || size == 0) return SL_RESULT_PARAMETER_INVALID;
    pthread_mutex_lock(&a->lock);
    if (a->count == SBQ_MAX) { pthread_mutex_unlock(&a->lock); return SL_RESULT_BUFFER_INSUFFICIENT; }
    a_u32 slot = (a->head + a->count) % SBQ_MAX;
    a->buf[slot] = buffer;
    a->size[slot] = size;
    a->count++;
    pthread_mutex_unlock(&a->lock);
    return SL_RESULT_SUCCESS;
}

static a_result sbq_clear(ASbqItf self)
{
    SbqAdapter *a = (SbqAdapter *) self;
    pthread_mutex_lock(&a->lock);
    a->head = a->count = a->offset = 0;
    pthread_mutex_unlock(&a->lock);
    return (a_result) (*a->oh_bq)->Clear(a->oh_bq);
}

static a_result sbq_get_state(ASbqItf self, ASbqState *state)
{
    SbqAdapter *a = (SbqAdapter *) self;
    if (state == NULL) return SL_RESULT_PARAMETER_INVALID;
    pthread_mutex_lock(&a->lock);
    state->count = a->count;
    state->index = a->consumed;
    pthread_mutex_unlock(&a->lock);
    return SL_RESULT_SUCCESS;
}

/* OH wants data: fill its buffer from queued app buffers. Each consumed buffer is reported to the
 * app outside the lock, because the app enqueues its next buffer from that callback -- and then
 * filling continues, so the buffers the app has just queued go into this same OH buffer.
 *
 * OH's buffers are much larger than a game's: Godot and Oboe double-buffer about 1024 frames, and
 * OH pulled several times that. Filling only what was queued when OH asked, and padding the rest
 * with silence, played audio for half the time or less -- choppy sound heard on Ball2Box, and on a
 * 1 kHz test tone 50% (48 kHz) and 73% (44.1 kHz) of 10 ms windows were silent. Android's
 * AudioTrack has a deep buffer behind the queue; this loop gives OH's pull the same depth. Silence
 * is written only when the app stops supplying data. */
static void sbq_on_oh_need(SLOHBufferQueueItf caller, void *context, SLuint32 want)
{
    (void) want;
    SbqAdapter *a = context;
    SLuint8 *out = NULL;
    SLuint32 out_size = 0;
    if ((*caller)->GetBuffer(caller, &out, &out_size) != SL_RESULT_SUCCESS || out == NULL) return;
    SLuint32 filled = 0;
    for (;;) {
        a_u32 finished = 0;
        pthread_mutex_lock(&a->lock);
        while (filled < out_size && a->count > 0) {
            a_u32 left = a->size[a->head] - a->offset;
            SLuint32 n = left < out_size - filled ? left : out_size - filled;
            memcpy(out + filled, (const SLuint8 *) a->buf[a->head] + a->offset, n);
            filled += n;
            a->offset += (a_u32) n;
            if (a->offset == a->size[a->head]) {
                a->head = (a->head + 1) % SBQ_MAX;
                a->count--;
                a->offset = 0;
                a->consumed++;
                finished++;
            }
        }
        ASbqCallback cb = a->callback;
        void *ctx = a->context;
        pthread_mutex_unlock(&a->lock);
        for (a_u32 i = 0; cb != NULL && i < finished; i++) cb((ASbqItf) &a->itf, ctx);
        /* Done when full, or when nothing was consumed: the app has nothing more queued. */
        if (filled >= out_size || finished == 0) break;
    }
    if (filled < out_size) memset(out + filled, 0, out_size - filled);   /* underrun: silence */
    (*caller)->Enqueue(caller, out, out_size);
}

static a_result sbq_register_callback(ASbqItf self, ASbqCallback callback, void *context)
{
    SbqAdapter *a = (SbqAdapter *) self;
    pthread_mutex_lock(&a->lock);
    a->callback = callback;
    a->context = context;
    pthread_mutex_unlock(&a->lock);
    return SL_RESULT_SUCCESS;
}

static const struct ASbqItf_ sbq_vtable = {sbq_enqueue, sbq_clear, sbq_get_state, sbq_register_callback};

/* ------------------------------------------------------------ wrapper records -------------- */

typedef struct Obj Obj;
typedef struct { const struct AEngineItf_ *itf; SLEngineItf real; } EngineW;
typedef struct { const struct APlayItf_ *itf; SLPlayItf real; APlayCallback cb; void *ctx; } PlayW;
typedef struct { const struct AVolumeItf_ *itf; SLVolumeItf real; } VolumeW;

enum { OBJ_OTHER, OBJ_PLAYER, OBJ_RECORDER };
typedef struct {
    const struct AConfigItf_ *itf;   /* first: AConfigItf points here */
    int kind;
    a_i32 stream_type, recording_preset, performance_mode;
} ConfigW;

struct Obj {
    const struct AObjectItf_ *itf;   /* first: AObjectItf points here */
    SLObjectItf real;
    EngineW *engine;
    PlayW *play;
    VolumeW *volume;
    SbqAdapter *sbq;
    ConfigW *config;
    int kind;
    AObjectCallback cb;
    void *ctx;
};

static const struct AObjectItf_ object_vtable;
static const struct AEngineItf_ engine_vtable;
static const struct APlayItf_ play_vtable;
static const struct AVolumeItf_ volume_vtable;

static Obj *obj_of(AObjectItf self) { return (Obj *) self; }

/* The OH object behind an app-visible one. An object the app got from us has our vtable. */
static SLObjectItf real_object(AObjectItf a)
{
    if (a == NULL) return NULL;
    if (*(const void *const *) a == (const void *) &object_vtable) return obj_of(a)->real;
    return (SLObjectItf) a;
}

static AObjectItf wrap_object(SLObjectItf real)
{
    Obj *w = calloc(1, sizeof(*w));
    if (w == NULL) return NULL;
    w->itf = &object_vtable;
    w->real = real;
    return (AObjectItf) &w->itf;
}

/* ------------------------------------------------------------ translation helpers ---------- */

typedef struct {
    SLboolean required[32];
    SLInterfaceID ids[32];
} OhInterfaceList;

static SLuint32 widen_interfaces(OhInterfaceList *out, a_u32 n, const SLInterfaceID *ids,
                                 const a_bool *required)
{
    SLuint32 m = 0;
    if (n > 32) n = 32;
    for (a_u32 i = 0; i < n; i++) {
        /* Answered here, not by OH, which would refuse an ID it does not know. */
        if (ids != NULL && iid_is(ids[i], SL_IID_ANDROIDCONFIGURATION)) continue;
        out->ids[m] = ids != NULL && iid_is_android_queue(ids[i]) ? oh.oh_bq : (ids ? canon_iid(ids[i]) : NULL);
        out->required[m] = required != NULL ? (SLboolean) required[i] : SL_BOOLEAN_FALSE;
        m++;
    }
    return m;
}

/* Storage for one rebuilt locator or format in OH's layout. */
typedef union {
    SLDataLocator_BufferQueue bq;
    SLDataLocator_OutputMix mix;
    SLDataLocator_URI uri;
    SLDataLocator_IODevice io;
    SLDataFormat_PCM pcm;
    SLDataFormat_MIME mime;
} OhBlock;

static void *translate_locator(const void *a_loc, OhBlock *out)
{
    if (a_loc == NULL) return NULL;
    const a_u32 *w = a_loc;
    switch (w[0]) {
    case A_LOC_ANDROID_SBQ:
    case A_LOC_BUFFERQUEUE:
        out->bq.locatorType = SL_DATALOCATOR_BUFFERQUEUE;
        out->bq.numBuffers = w[1];
        return &out->bq;
    case A_LOC_OUTPUTMIX: {
        const struct { a_u32 type; AObjectItf mix; } *m = a_loc;
        out->mix.locatorType = SL_DATALOCATOR_OUTPUTMIX;
        out->mix.outputMix = real_object(m->mix);
        return &out->mix;
    }
    case A_LOC_URI: {
        const struct { a_u32 type; SLchar *uri; } *u = a_loc;
        out->uri.locatorType = SL_DATALOCATOR_URI;
        out->uri.URI = u->uri;
        return &out->uri;
    }
    case A_LOC_IODEVICE: {
        const struct { a_u32 type; a_u32 deviceType; a_u32 deviceID; AObjectItf device; } *d = a_loc;
        out->io.locatorType = SL_DATALOCATOR_IODEVICE;
        out->io.deviceType = d->deviceType;
        out->io.deviceID = d->deviceID;
        out->io.device = real_object(d->device);
        return &out->io;
    }
    default:
        SLES_LOGF("locator type 0x%x not translated; passed through as is", (unsigned) w[0]);
        return (void *) a_loc;
    }
}

static void *translate_format(const void *a_fmt, OhBlock *out)
{
    if (a_fmt == NULL) return NULL;
    const a_u32 *w = a_fmt;
    switch (w[0]) {
    case A_FMT_PCM:
    case A_FMT_PCM_EX:   /* Android's extended PCM: same first seven fields plus a representation */
        out->pcm.formatType = SL_DATAFORMAT_PCM;
        out->pcm.numChannels = w[1];
        out->pcm.samplesPerSec = w[2];
        out->pcm.bitsPerSample = w[3];
        out->pcm.containerSize = w[4];
        out->pcm.channelMask = w[5];
        out->pcm.endianness = w[6];
        SLES_LOGF("pcm: %u ch, %u mHz, %u/%u bit, mask 0x%x%s", (unsigned) w[1], (unsigned) w[2],
                  (unsigned) w[3], (unsigned) w[4], (unsigned) w[5],
                  w[0] == A_FMT_PCM_EX ? " (from PCM_EX)" : "");
        return &out->pcm;
    case A_FMT_MIME: {
        const struct { a_u32 type; SLchar *mime; a_u32 container; } *m = a_fmt;
        out->mime.formatType = SL_DATAFORMAT_MIME;
        out->mime.mimeType = m->mime;
        out->mime.containerType = m->container;
        return &out->mime;
    }
    default:
        SLES_LOGF("format type 0x%x not translated; passed through as is", (unsigned) w[0]);
        return (void *) a_fmt;
    }
}

typedef struct { OhBlock loc, fmt; SLDataSource src; } OhSource;
typedef struct { OhBlock loc, fmt; SLDataSink sink; } OhSink;

static SLDataSource *translate_source(const ADataSource *a, OhSource *o)
{
    if (a == NULL) return NULL;
    o->src.pLocator = translate_locator(a->pLocator, &o->loc);
    o->src.pFormat = translate_format(a->pFormat, &o->fmt);
    return &o->src;
}

static SLDataSink *translate_sink(const ADataSink *a, OhSink *o)
{
    if (a == NULL) return NULL;
    o->sink.pLocator = translate_locator(a->pLocator, &o->loc);
    o->sink.pFormat = translate_format(a->pFormat, &o->fmt);
    return &o->sink;
}

static a_result finish_create(SLresult r, SLObjectItf real, AObjectItf *out, const char *what)
{
    if (r != SL_RESULT_SUCCESS) {
        SLES_LOGF("%s -> %u", what, (unsigned) r);
        return (a_result) r;
    }
    AObjectItf w = wrap_object(real);
    if (w == NULL) { (*real)->Destroy(real); return SL_RESULT_MEMORY_FAILURE; }
    *out = w;
    return SL_RESULT_SUCCESS;
}

/* ------------------------------------------------------------ object ----------------------- */

static void obj_trampoline(SLObjectItf caller, const void *context, SLuint32 event, SLresult result,
                           SLuint32 param, void *itf)
{
    (void) caller;
    Obj *w = (Obj *) context;
    if (w->cb != NULL) w->cb((AObjectItf) &w->itf, w->ctx, (a_u32) event, (a_result) result, (a_u32) param, itf);
}

static a_result o_realize(AObjectItf s, a_bool async)
{ return (a_result) (*obj_of(s)->real)->Realize(obj_of(s)->real, (SLboolean) async); }
static a_result o_resume(AObjectItf s, a_bool async)
{ return (a_result) (*obj_of(s)->real)->Resume(obj_of(s)->real, (SLboolean) async); }
static a_result o_get_state(AObjectItf s, a_u32 *state)
{
    SLuint32 v = 0;
    SLresult r = (*obj_of(s)->real)->GetState(obj_of(s)->real, &v);
    if (state != NULL) *state = (a_u32) v;
    return (a_result) r;
}
static a_result o_register_callback(AObjectItf s, AObjectCallback cb, void *ctx)
{
    Obj *w = obj_of(s);
    w->cb = cb;
    w->ctx = ctx;
    return (a_result) (*w->real)->RegisterCallback(w->real, cb ? obj_trampoline : NULL, w);
}
static void o_abort(AObjectItf s) { (*obj_of(s)->real)->AbortAsyncOperation(obj_of(s)->real); }
static a_result o_set_priority(AObjectItf s, a_i32 priority, a_bool preemptable)
{ return (a_result) (*obj_of(s)->real)->SetPriority(obj_of(s)->real, (SLint32) priority, (SLboolean) preemptable); }
static a_result o_get_priority(AObjectItf s, a_i32 *priority, a_bool *preemptable)
{
    SLint32 p = 0;
    SLboolean e = 0;
    SLresult r = (*obj_of(s)->real)->GetPriority(obj_of(s)->real, &p, &e);
    if (priority != NULL) *priority = (a_i32) p;
    if (preemptable != NULL) *preemptable = (a_bool) e;
    return (a_result) r;
}
static a_result o_set_loss(AObjectItf s, int16_t n, SLInterfaceID *ids, a_bool enabled)
{ return (a_result) (*obj_of(s)->real)->SetLossOfControlInterfaces(obj_of(s)->real, n, ids, (SLboolean) enabled); }

static void o_destroy(AObjectItf s)
{
    Obj *w = obj_of(s);
    (*w->real)->Destroy(w->real);
    if (w->sbq != NULL) { pthread_mutex_destroy(&w->sbq->lock); free(w->sbq); }
    free(w->engine);
    free(w->play);
    free(w->volume);
    free(w->config);
    free(w);
}

/* ------------------------------------------------------------ Android configuration ------- */

/* AOSP's values: SL_ANDROID_STREAM_MEDIA, SL_ANDROID_RECORDING_PRESET_GENERIC,
 * SL_ANDROID_PERFORMANCE_LATENCY. */
#define A_STREAM_MEDIA 3
#define A_PRESET_GENERIC 1
#define A_PERFORMANCE_LATENCY 1

static a_i32 *config_slot(ConfigW *c, const SLchar *key)
{
    const char *k = (const char *) key;
    if (strcmp(k, "androidPerformanceMode") == 0) return &c->performance_mode;
    if (c->kind == OBJ_PLAYER && strcmp(k, "androidPlaybackStreamType") == 0) return &c->stream_type;
    if (c->kind == OBJ_RECORDER && strcmp(k, "androidRecordingPreset") == 0) return &c->recording_preset;
    return NULL;
}

static a_result c_set(AConfigItf self, const SLchar *key, const void *value, a_u32 size)
{
    ConfigW *c = (ConfigW *) self;
    a_i32 *slot;
    if (key == NULL || value == NULL || size == 0) return SL_RESULT_PARAMETER_INVALID;
    if ((slot = config_slot(c, key)) == NULL) return SL_RESULT_PARAMETER_INVALID;
    if (size < sizeof(a_i32)) return SL_RESULT_BUFFER_INSUFFICIENT;
    memcpy(slot, value, sizeof(a_i32));
    SLES_LOGF("configuration %s = %d (recorded; OH has no equivalent)", (const char *) key, (int) *slot);
    return SL_RESULT_SUCCESS;
}

static a_result c_get(AConfigItf self, const SLchar *key, a_u32 *size, void *value)
{
    ConfigW *c = (ConfigW *) self;
    a_i32 *slot;
    if (key == NULL || size == NULL) return SL_RESULT_PARAMETER_INVALID;
    if ((slot = config_slot(c, key)) == NULL) return SL_RESULT_PARAMETER_INVALID;
    if (value == NULL) { *size = sizeof(a_i32); return SL_RESULT_SUCCESS; }
    if (*size < sizeof(a_i32)) return SL_RESULT_BUFFER_INSUFFICIENT;
    memcpy(value, slot, sizeof(a_i32));
    *size = sizeof(a_i32);
    return SL_RESULT_SUCCESS;
}

static a_result c_acquire_proxy(AConfigItf self, a_u32 type, void **proxy)
{ (void) self; (void) type; (void) proxy; return SL_RESULT_FEATURE_UNSUPPORTED; }
static a_result c_release_proxy(AConfigItf self, a_u32 type)
{ (void) self; (void) type; return SL_RESULT_FEATURE_UNSUPPORTED; }

static const struct AConfigItf_ config_vtable = { c_set, c_get, c_acquire_proxy, c_release_proxy };

static a_result o_get_interface(AObjectItf s, const SLInterfaceID iid, void *out)
{
    Obj *w = obj_of(s);
    if (out == NULL) return SL_RESULT_PARAMETER_INVALID;
    if (iid_is(iid, SL_IID_ANDROIDCONFIGURATION)) {
        if (w->kind == OBJ_OTHER) return SL_RESULT_FEATURE_UNSUPPORTED;
        if (w->config == NULL) {
            if ((w->config = calloc(1, sizeof(*w->config))) == NULL) return SL_RESULT_MEMORY_FAILURE;
            w->config->itf = &config_vtable;
            w->config->kind = w->kind;
            w->config->stream_type = A_STREAM_MEDIA;
            w->config->recording_preset = A_PRESET_GENERIC;
            w->config->performance_mode = A_PERFORMANCE_LATENCY;
        }
        *(AConfigItf *) out = (AConfigItf) &w->config->itf;
        return SL_RESULT_SUCCESS;
    }
    if (iid_is(iid, oh.engine)) {
        if (w->engine == NULL) {
            SLEngineItf real = NULL;
            SLresult r = (*w->real)->GetInterface(w->real, oh.engine, &real);
            if (r != SL_RESULT_SUCCESS) return (a_result) r;
            if ((w->engine = calloc(1, sizeof(*w->engine))) == NULL) return SL_RESULT_MEMORY_FAILURE;
            w->engine->itf = &engine_vtable;
            w->engine->real = real;
        }
        *(AEngineItf *) out = (AEngineItf) &w->engine->itf;
        return SL_RESULT_SUCCESS;
    }
    if (iid_is(iid, oh.play)) {
        if (w->play == NULL) {
            SLPlayItf real = NULL;
            SLresult r = (*w->real)->GetInterface(w->real, oh.play, &real);
            if (r != SL_RESULT_SUCCESS) return (a_result) r;
            if ((w->play = calloc(1, sizeof(*w->play))) == NULL) return SL_RESULT_MEMORY_FAILURE;
            w->play->itf = &play_vtable;
            w->play->real = real;
        }
        *(APlayItf *) out = (APlayItf) &w->play->itf;
        return SL_RESULT_SUCCESS;
    }
    if (iid_is(iid, oh.volume)) {
        if (w->volume == NULL) {
            SLVolumeItf real = NULL;
            SLresult r = (*w->real)->GetInterface(w->real, oh.volume, &real);
            if (r != SL_RESULT_SUCCESS) return (a_result) r;
            if ((w->volume = calloc(1, sizeof(*w->volume))) == NULL) return SL_RESULT_MEMORY_FAILURE;
            w->volume->itf = &volume_vtable;
            w->volume->real = real;
        }
        *(AVolumeItf *) out = (AVolumeItf) &w->volume->itf;
        return SL_RESULT_SUCCESS;
    }
    if (iid_is_android_queue(iid)) {
        if (w->sbq == NULL) {
            SLOHBufferQueueItf oh_bq = NULL;
            SLresult r = (*w->real)->GetInterface(w->real, oh.oh_bq, &oh_bq);
            if (r != SL_RESULT_SUCCESS) {
                SLES_LOGF("player has no OH buffer queue (%u)", (unsigned) r);
                return (a_result) r;
            }
            if ((w->sbq = calloc(1, sizeof(*w->sbq))) == NULL) return SL_RESULT_MEMORY_FAILURE;
            w->sbq->itf = &sbq_vtable;
            w->sbq->oh_bq = oh_bq;
            pthread_mutex_init(&w->sbq->lock, NULL);
            if ((*oh_bq)->RegisterCallback(oh_bq, sbq_on_oh_need, w->sbq) != SL_RESULT_SUCCESS) {
                sles_log("OH buffer queue refused a callback");
            }
        }
        *(ASbqItf *) out = (ASbqItf) &w->sbq->itf;
        return SL_RESULT_SUCCESS;
    }
    /* Not wrapped: hand OH's interface back, and say so -- its methods take OH-width types. */
    SLresult r = (*w->real)->GetInterface(w->real, canon_iid(iid), out);
    if (iid != NULL) {
        SLES_LOGF("interface %08x-... handed back unwrapped (%u)", (unsigned) iid->time_low, (unsigned) r);
    }
    return (a_result) r;
}

static const struct AObjectItf_ object_vtable = {
    o_realize, o_resume, o_get_state, o_get_interface, o_register_callback,
    o_abort, o_destroy, o_set_priority, o_get_priority, o_set_loss};

/* ------------------------------------------------------------ engine ----------------------- */

#define ENG(self) (((EngineW *) (self))->real)

static a_result e_create_audio_player(AEngineItf self, AObjectItf *player, ADataSource *src, ADataSink *sink,
                                      a_u32 n, const SLInterfaceID *ids, const a_bool *required)
{
    OhSource os; OhSink ok; OhInterfaceList il;
    SLuint32 m = widen_interfaces(&il, n, ids, required);
    SLObjectItf real = NULL;
    SLresult r = (*ENG(self))->CreateAudioPlayer(ENG(self), &real, translate_source(src, &os),
                                                 translate_sink(sink, &ok), m, il.ids, il.required);
    SLES_LOGF("CreateAudioPlayer -> %u", (unsigned) r);
    a_result result = finish_create(r, real, player, "CreateAudioPlayer");
    if (result == SL_RESULT_SUCCESS) obj_of(*player)->kind = OBJ_PLAYER;
    return result;
}

static a_result e_create_audio_recorder(AEngineItf self, AObjectItf *rec, ADataSource *src, ADataSink *sink,
                                        a_u32 n, const SLInterfaceID *ids, const a_bool *required)
{
    OhSource os; OhSink ok; OhInterfaceList il;
    SLuint32 m = widen_interfaces(&il, n, ids, required);
    SLObjectItf real = NULL;
    SLresult r = (*ENG(self))->CreateAudioRecorder(ENG(self), &real, translate_source(src, &os),
                                                   translate_sink(sink, &ok), m, il.ids, il.required);
    a_result result = finish_create(r, real, rec, "CreateAudioRecorder");
    if (result == SL_RESULT_SUCCESS) obj_of(*rec)->kind = OBJ_RECORDER;
    return result;
}

static a_result e_create_output_mix(AEngineItf self, AObjectItf *mix, a_u32 n, const SLInterfaceID *ids,
                                    const a_bool *required)
{
    OhInterfaceList il;
    SLuint32 m = widen_interfaces(&il, n, ids, required);
    SLObjectItf real = NULL;
    SLresult r = (*ENG(self))->CreateOutputMix(ENG(self), &real, m, il.ids, il.required);
    return finish_create(r, real, mix, "CreateOutputMix");
}

static a_result e_create_led(AEngineItf self, AObjectItf *d, a_u32 id, a_u32 n, const SLInterfaceID *ids, const a_bool *req)
{
    OhInterfaceList il; SLuint32 m = widen_interfaces(&il, n, ids, req); SLObjectItf real = NULL;
    return finish_create((*ENG(self))->CreateLEDDevice(ENG(self), &real, id, m, il.ids, il.required), real, d, "CreateLEDDevice");
}
static a_result e_create_vibra(AEngineItf self, AObjectItf *d, a_u32 id, a_u32 n, const SLInterfaceID *ids, const a_bool *req)
{
    OhInterfaceList il; SLuint32 m = widen_interfaces(&il, n, ids, req); SLObjectItf real = NULL;
    return finish_create((*ENG(self))->CreateVibraDevice(ENG(self), &real, id, m, il.ids, il.required), real, d, "CreateVibraDevice");
}
static a_result e_create_midi(AEngineItf self, AObjectItf *o, ADataSource *a, ADataSource *b, ADataSink *c,
                              ADataSink *d, ADataSink *e, a_u32 n, const SLInterfaceID *ids, const a_bool *req)
{
    OhSource oa, ob; OhSink oc, od, oe; OhInterfaceList il; SLObjectItf real = NULL;
    SLuint32 m = widen_interfaces(&il, n, ids, req);
    return finish_create((*ENG(self))->CreateMidiPlayer(ENG(self), &real, translate_source(a, &oa),
        translate_source(b, &ob), translate_sink(c, &oc), translate_sink(d, &od), translate_sink(e, &oe),
        m, il.ids, il.required), real, o, "CreateMidiPlayer");
}
static a_result e_create_listener(AEngineItf self, AObjectItf *o, a_u32 n, const SLInterfaceID *ids, const a_bool *req)
{
    OhInterfaceList il; SLuint32 m = widen_interfaces(&il, n, ids, req); SLObjectItf real = NULL;
    return finish_create((*ENG(self))->CreateListener(ENG(self), &real, m, il.ids, il.required), real, o, "CreateListener");
}
static a_result e_create_3dgroup(AEngineItf self, AObjectItf *o, a_u32 n, const SLInterfaceID *ids, const a_bool *req)
{
    OhInterfaceList il; SLuint32 m = widen_interfaces(&il, n, ids, req); SLObjectItf real = NULL;
    return finish_create((*ENG(self))->Create3DGroup(ENG(self), &real, m, il.ids, il.required), real, o, "Create3DGroup");
}
static a_result e_create_meta(AEngineItf self, AObjectItf *o, ADataSource *s, a_u32 n, const SLInterfaceID *ids, const a_bool *req)
{
    OhSource os; OhInterfaceList il; SLuint32 m = widen_interfaces(&il, n, ids, req); SLObjectItf real = NULL;
    return finish_create((*ENG(self))->CreateMetadataExtractor(ENG(self), &real, translate_source(s, &os),
        m, il.ids, il.required), real, o, "CreateMetadataExtractor");
}
static a_result e_create_ext(AEngineItf self, AObjectItf *o, void *p, a_u32 type, a_u32 n, const SLInterfaceID *ids, const a_bool *req)
{
    OhInterfaceList il; SLuint32 m = widen_interfaces(&il, n, ids, req); SLObjectItf real = NULL;
    return finish_create((*ENG(self))->CreateExtensionObject(ENG(self), &real, p, type, m, il.ids, il.required), real, o, "CreateExtensionObject");
}
static a_result e_query_num_ifaces(AEngineItf self, a_u32 id, a_u32 *n)
{
    SLuint32 v = 0; SLresult r = (*ENG(self))->QueryNumSupportedInterfaces(ENG(self), id, &v);
    if (n != NULL) *n = (a_u32) v;
    return (a_result) r;
}
static a_result e_query_ifaces(AEngineItf self, a_u32 id, a_u32 idx, SLInterfaceID *out)
{ return (a_result) (*ENG(self))->QuerySupportedInterfaces(ENG(self), id, idx, out); }
static a_result e_query_num_exts(AEngineItf self, a_u32 *n)
{
    SLuint32 v = 0; SLresult r = (*ENG(self))->QueryNumSupportedExtensions(ENG(self), &v);
    if (n != NULL) *n = (a_u32) v;
    return (a_result) r;
}
static a_result e_query_ext(AEngineItf self, a_u32 idx, SLchar *name, int16_t *len)
{ return (a_result) (*ENG(self))->QuerySupportedExtension(ENG(self), idx, name, len); }
static a_result e_is_ext(AEngineItf self, const SLchar *name, a_bool *supported)
{
    SLboolean v = 0; SLresult r = (*ENG(self))->IsExtensionSupported(ENG(self), name, &v);
    if (supported != NULL) *supported = (a_bool) v;
    return (a_result) r;
}

static const struct AEngineItf_ engine_vtable = {
    e_create_led, e_create_vibra, e_create_audio_player, e_create_audio_recorder, e_create_midi,
    e_create_listener, e_create_3dgroup, e_create_output_mix, e_create_meta, e_create_ext,
    e_query_num_ifaces, e_query_ifaces, e_query_num_exts, e_query_ext, e_is_ext};

/* ------------------------------------------------------------ play ------------------------- */

#define PLY(self) (((PlayW *) (self))->real)

static void play_trampoline(SLPlayItf caller, void *context, SLuint32 event)
{
    (void) caller;
    PlayW *p = context;
    if (p->cb != NULL) p->cb((APlayItf) &p->itf, p->ctx, (a_u32) event);
}

static a_result p_set_state(APlayItf s, a_u32 v) { return (a_result) (*PLY(s))->SetPlayState(PLY(s), v); }
static a_result p_get_state(APlayItf s, a_u32 *out)
{ SLuint32 v = 0; SLresult r = (*PLY(s))->GetPlayState(PLY(s), &v); if (out) *out = (a_u32) v; return (a_result) r; }
static a_result p_get_duration(APlayItf s, a_u32 *out)
{ SLmillisecond v = 0; SLresult r = (*PLY(s))->GetDuration(PLY(s), &v); if (out) *out = (a_u32) v; return (a_result) r; }
static a_result p_get_position(APlayItf s, a_u32 *out)
{ SLmillisecond v = 0; SLresult r = (*PLY(s))->GetPosition(PLY(s), &v); if (out) *out = (a_u32) v; return (a_result) r; }
static a_result p_register_callback(APlayItf s, APlayCallback cb, void *ctx)
{
    PlayW *p = (PlayW *) s;
    p->cb = cb;
    p->ctx = ctx;
    return (a_result) (*p->real)->RegisterCallback(p->real, cb ? play_trampoline : NULL, p);
}
static a_result p_set_mask(APlayItf s, a_u32 v) { return (a_result) (*PLY(s))->SetCallbackEventsMask(PLY(s), v); }
static a_result p_get_mask(APlayItf s, a_u32 *out)
{ SLuint32 v = 0; SLresult r = (*PLY(s))->GetCallbackEventsMask(PLY(s), &v); if (out) *out = (a_u32) v; return (a_result) r; }
static a_result p_set_marker(APlayItf s, a_u32 v) { return (a_result) (*PLY(s))->SetMarkerPosition(PLY(s), v); }
static a_result p_clear_marker(APlayItf s) { return (a_result) (*PLY(s))->ClearMarkerPosition(PLY(s)); }
static a_result p_get_marker(APlayItf s, a_u32 *out)
{ SLmillisecond v = 0; SLresult r = (*PLY(s))->GetMarkerPosition(PLY(s), &v); if (out) *out = (a_u32) v; return (a_result) r; }
static a_result p_set_period(APlayItf s, a_u32 v) { return (a_result) (*PLY(s))->SetPositionUpdatePeriod(PLY(s), v); }
static a_result p_get_period(APlayItf s, a_u32 *out)
{ SLmillisecond v = 0; SLresult r = (*PLY(s))->GetPositionUpdatePeriod(PLY(s), &v); if (out) *out = (a_u32) v; return (a_result) r; }

static const struct APlayItf_ play_vtable = {
    p_set_state, p_get_state, p_get_duration, p_get_position, p_register_callback, p_set_mask,
    p_get_mask, p_set_marker, p_clear_marker, p_get_marker, p_set_period, p_get_period};

/* ------------------------------------------------------------ volume ----------------------- */

#define VOL(self) (((VolumeW *) (self))->real)

static a_result v_set_level(AVolumeItf s, int16_t v) { return (a_result) (*VOL(s))->SetVolumeLevel(VOL(s), v); }
static a_result v_get_level(AVolumeItf s, int16_t *v) { return (a_result) (*VOL(s))->GetVolumeLevel(VOL(s), v); }
static a_result v_get_max(AVolumeItf s, int16_t *v) { return (a_result) (*VOL(s))->GetMaxVolumeLevel(VOL(s), v); }
static a_result v_set_mute(AVolumeItf s, a_bool v) { return (a_result) (*VOL(s))->SetMute(VOL(s), (SLboolean) v); }
static a_result v_get_mute(AVolumeItf s, a_bool *out)
{ SLboolean v = 0; SLresult r = (*VOL(s))->GetMute(VOL(s), &v); if (out) *out = (a_bool) v; return (a_result) r; }
static a_result v_enable_stereo(AVolumeItf s, a_bool v) { return (a_result) (*VOL(s))->EnableStereoPosition(VOL(s), (SLboolean) v); }
static a_result v_is_stereo(AVolumeItf s, a_bool *out)
{ SLboolean v = 0; SLresult r = (*VOL(s))->IsEnabledStereoPosition(VOL(s), &v); if (out) *out = (a_bool) v; return (a_result) r; }
static a_result v_set_stereo(AVolumeItf s, int16_t v) { return (a_result) (*VOL(s))->SetStereoPosition(VOL(s), v); }
static a_result v_get_stereo(AVolumeItf s, int16_t *v) { return (a_result) (*VOL(s))->GetStereoPosition(VOL(s), v); }

static const struct AVolumeItf_ volume_vtable = {
    v_set_level, v_get_level, v_get_max, v_set_mute, v_get_mute, v_enable_stereo, v_is_stereo,
    v_set_stereo, v_get_stereo};

/* ------------------------------------------------------------ entry point ------------------ */

a_result slCreateEngine(AObjectItf *engine, a_u32 num_options, const AEngineOption *options,
                        a_u32 num_interfaces, const SLInterfaceID *interfaces, const a_bool *required)
{
    if (!oh_resolve()) return SL_RESULT_INTERNAL_ERROR;
    if (engine == NULL) return SL_RESULT_PARAMETER_INVALID;
    SLEngineOption opts[16];
    if (num_options > 16) num_options = 16;
    for (a_u32 i = 0; i < num_options; i++) {
        opts[i].feature = options[i].feature;
        opts[i].data = options[i].data;
    }
    OhInterfaceList il;
    SLuint32 m = widen_interfaces(&il, num_interfaces, interfaces, required);
    SLObjectItf real = NULL;
    SLresult r = oh.create_engine(&real, num_options, num_options ? opts : NULL, m, il.ids, il.required);
    if (r != SL_RESULT_SUCCESS) {
        SLES_LOGF("slCreateEngine -> %u", (unsigned) r);
        return (a_result) r;
    }
    sles_log("engine wrapped: Android OpenSL ES ABI over OH's");
    return finish_create(r, real, engine, "slCreateEngine");
}
