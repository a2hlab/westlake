/*
 * Android's AAudio, the NDK's libaaudio.so as of Android 14, on top of OH's OHAudio.
 *
 * OH has no libaaudio.so. 22 corpus apps use it:
 * - three link it (Felicity's audio engine, Jami's core, Linphone's AAudio plugin), so their library
 *   did not load at all: Felicity's player service failed with "libaaudio.so would not load";
 * - nineteen look it up at run time through Oboe, which falls back to OpenSL ES when the dlopen
 *   fails.
 *
 * The streams are OH renderers and capturers, made here in the preloaded shim:
 * - an app library's AAudio imports bind to these exports, as its OpenSL ES imports bind to the
 *   adapter's. The imports carry no symbol version, so the shim's default version answers them;
 * - an Android dlopen of libaaudio.so is answered with this shim's own handle
 *   (webview_bionic_shim.c), so Oboe's lookups by handle find them;
 * - the libaaudio.so staged beside the shim (libaaudio_stub.c) satisfies DT_NEEDED and defines
 *   nothing.
 * OH's library is opened by the first stream, so an app that never opens one loads nothing new. The
 * streams run in the default namespace beside the OpenSL ES adapter, on one copy of OH's audio
 * client.
 *
 * What AOSP's libaaudio guarantees and this keeps (frameworks/av/media/libaaudio):
 * - the states and transitions of core/AudioStream.cpp, and the errors it returns for the others.
 *   A start, stop, pause, flush or close from inside the stream's data callback is refused;
 * - a data callback of exactly framesPerDataCallback frames when the app sets it. OH asks for its
 *   own amount per callback, so the app's buffers are cut and joined to fit;
 * - read and write that block up to their timeout, through a FIFO that OH's callback drains or
 *   fills. A write fills it up to the buffer size the app set;
 * - a data callback that returns STOP stops the stream from another thread;
 * - an OH error disconnects the stream and reaches the app's error callback on a thread of its own;
 * - a timestamp only once a frame the app wrote has been presented (legacy/AudioStreamTrack.cpp).
 * Unspecified parameters open as float samples (AOSP's default), 48 kHz, two channels out and one in.
 * An app that left the format unspecified takes the samples OH grants; one that asked for float or
 * 16-bit samples OH refuses gets them converted at OH's boundary.
 * Low latency is not asked of OH: its fast mode is untested on the board, so a stream that asked
 * for it reports performance mode NONE, which Oboe accepts.
 */
#define __INTRODUCED_IN(api)
#include <aaudio/AAudio.h>
#include <aaudio/AAudioTesting.h>
#include <ohaudio/native_audiocapturer.h>
#include <ohaudio/native_audiorenderer.h>
#include <ohaudio/native_audiostreambuilder.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AAUDIO_LOG(...) fprintf(stderr, "[WESTLAKE-AAUDIO] " __VA_ARGS__)

/* ------------------------------------------------------------------ OH's library ---------- */

static struct {
    OH_AudioStream_Result (*builder_create)(OH_AudioStreamBuilder **, OH_AudioStream_Type);
    OH_AudioStream_Result (*builder_destroy)(OH_AudioStreamBuilder *);
    OH_AudioStream_Result (*set_rate)(OH_AudioStreamBuilder *, int32_t);
    OH_AudioStream_Result (*set_channels)(OH_AudioStreamBuilder *, int32_t);
    OH_AudioStream_Result (*set_format)(OH_AudioStreamBuilder *, OH_AudioStream_SampleFormat);
    OH_AudioStream_Result (*set_latency)(OH_AudioStreamBuilder *, OH_AudioStream_LatencyMode);
    OH_AudioStream_Result (*set_renderer_info)(OH_AudioStreamBuilder *, OH_AudioStream_Usage);
    OH_AudioStream_Result (*set_capturer_info)(OH_AudioStreamBuilder *, OH_AudioStream_SourceType);
    OH_AudioStream_Result (*set_renderer_callback)(OH_AudioStreamBuilder *, OH_AudioRenderer_Callbacks,
                                                   void *);
    OH_AudioStream_Result (*set_capturer_callback)(OH_AudioStreamBuilder *, OH_AudioCapturer_Callbacks,
                                                   void *);
    OH_AudioStream_Result (*generate_renderer)(OH_AudioStreamBuilder *, OH_AudioRenderer **);
    OH_AudioStream_Result (*generate_capturer)(OH_AudioStreamBuilder *, OH_AudioCapturer **);
    OH_AudioStream_Result (*r_release)(OH_AudioRenderer *);
    OH_AudioStream_Result (*r_start)(OH_AudioRenderer *);
    OH_AudioStream_Result (*r_pause)(OH_AudioRenderer *);
    OH_AudioStream_Result (*r_stop)(OH_AudioRenderer *);
    OH_AudioStream_Result (*r_flush)(OH_AudioRenderer *);
    OH_AudioStream_Result (*r_rate)(OH_AudioRenderer *, int32_t *);
    OH_AudioStream_Result (*r_channels)(OH_AudioRenderer *, int32_t *);
    OH_AudioStream_Result (*r_format)(OH_AudioRenderer *, OH_AudioStream_SampleFormat *);
    OH_AudioStream_Result (*r_frame_size)(OH_AudioRenderer *, int32_t *);
    OH_AudioStream_Result (*r_timestamp)(OH_AudioRenderer *, clockid_t, int64_t *, int64_t *);
    OH_AudioStream_Result (*r_underflows)(OH_AudioRenderer *, uint32_t *);
    OH_AudioStream_Result (*r_stream_id)(OH_AudioRenderer *, uint32_t *);
    OH_AudioStream_Result (*c_release)(OH_AudioCapturer *);
    OH_AudioStream_Result (*c_start)(OH_AudioCapturer *);
    OH_AudioStream_Result (*c_stop)(OH_AudioCapturer *);
    OH_AudioStream_Result (*c_rate)(OH_AudioCapturer *, int32_t *);
    OH_AudioStream_Result (*c_channels)(OH_AudioCapturer *, int32_t *);
    OH_AudioStream_Result (*c_format)(OH_AudioCapturer *, OH_AudioStream_SampleFormat *);
    OH_AudioStream_Result (*c_frame_size)(OH_AudioCapturer *, int32_t *);
    OH_AudioStream_Result (*c_timestamp)(OH_AudioCapturer *, clockid_t, int64_t *, int64_t *);
    OH_AudioStream_Result (*c_overflows)(OH_AudioCapturer *, uint32_t *);
    OH_AudioStream_Result (*c_stream_id)(OH_AudioCapturer *, uint32_t *);
} oh;

static pthread_once_t oh_once = PTHREAD_ONCE_INIT;
static int oh_loaded;

static void oh_load(void)
{
    void *lib = dlopen("libohaudio.so", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) {
        AAUDIO_LOG("libohaudio.so did not load: %s\n", dlerror());
        return;
    }
    const char *missing = NULL;
#define OH_NEED(field, name) \
    if ((oh.field = (__typeof__(oh.field)) dlsym(lib, name)) == NULL && missing == NULL) missing = name
#define OH_WANT(field, name) oh.field = (__typeof__(oh.field)) dlsym(lib, name)
    OH_NEED(builder_create, "OH_AudioStreamBuilder_Create");
    OH_NEED(builder_destroy, "OH_AudioStreamBuilder_Destroy");
    OH_NEED(set_rate, "OH_AudioStreamBuilder_SetSamplingRate");
    OH_NEED(set_channels, "OH_AudioStreamBuilder_SetChannelCount");
    OH_NEED(set_format, "OH_AudioStreamBuilder_SetSampleFormat");
    OH_NEED(set_latency, "OH_AudioStreamBuilder_SetLatencyMode");
    OH_NEED(set_renderer_info, "OH_AudioStreamBuilder_SetRendererInfo");
    OH_NEED(set_capturer_info, "OH_AudioStreamBuilder_SetCapturerInfo");
    OH_NEED(set_renderer_callback, "OH_AudioStreamBuilder_SetRendererCallback");
    OH_NEED(set_capturer_callback, "OH_AudioStreamBuilder_SetCapturerCallback");
    OH_NEED(generate_renderer, "OH_AudioStreamBuilder_GenerateRenderer");
    OH_NEED(generate_capturer, "OH_AudioStreamBuilder_GenerateCapturer");
    OH_NEED(r_release, "OH_AudioRenderer_Release");
    OH_NEED(r_start, "OH_AudioRenderer_Start");
    OH_NEED(r_pause, "OH_AudioRenderer_Pause");
    OH_NEED(r_stop, "OH_AudioRenderer_Stop");
    OH_NEED(r_flush, "OH_AudioRenderer_Flush");
    OH_NEED(r_rate, "OH_AudioRenderer_GetSamplingRate");
    OH_NEED(r_channels, "OH_AudioRenderer_GetChannelCount");
    OH_NEED(r_format, "OH_AudioRenderer_GetSampleFormat");
    OH_NEED(r_frame_size, "OH_AudioRenderer_GetFrameSizeInCallback");
    OH_NEED(r_timestamp, "OH_AudioRenderer_GetTimestamp");
    OH_WANT(r_underflows, "OH_AudioRenderer_GetUnderflowCount");
    OH_WANT(r_stream_id, "OH_AudioRenderer_GetStreamId");
    OH_NEED(c_release, "OH_AudioCapturer_Release");
    OH_NEED(c_start, "OH_AudioCapturer_Start");
    OH_NEED(c_stop, "OH_AudioCapturer_Stop");
    OH_NEED(c_rate, "OH_AudioCapturer_GetSamplingRate");
    OH_NEED(c_channels, "OH_AudioCapturer_GetChannelCount");
    OH_NEED(c_format, "OH_AudioCapturer_GetSampleFormat");
    OH_NEED(c_frame_size, "OH_AudioCapturer_GetFrameSizeInCallback");
    OH_NEED(c_timestamp, "OH_AudioCapturer_GetTimestamp");
    OH_WANT(c_overflows, "OH_AudioCapturer_GetOverflowCount");
    OH_WANT(c_stream_id, "OH_AudioCapturer_GetStreamId");
#undef OH_NEED
#undef OH_WANT
    if (missing != NULL) {
        AAUDIO_LOG("libohaudio.so lacks %s; no AAudio stream can open\n", missing);
        return;
    }
    oh_loaded = 1;
}

/* ------------------------------------------------------------------ builders and streams -- */

struct AAudioStreamBuilderStruct {
    int32_t device_id, sample_rate, channel_count, capacity, frames_per_callback;
    aaudio_channel_mask_t channel_mask;
    aaudio_format_t format;
    aaudio_sharing_mode_t sharing_mode;
    aaudio_direction_t direction;
    aaudio_performance_mode_t performance_mode;
    aaudio_usage_t usage;
    aaudio_content_type_t content_type;
    aaudio_input_preset_t input_preset;
    aaudio_allowed_capture_policy_t capture_policy;
    aaudio_session_id_t session_id;
    aaudio_spatialization_behavior_t spatialization;
    bool content_spatialized, privacy_sensitive;
    AAudioStream_dataCallback data_callback;
    void *data_user;
    AAudioStream_errorCallback error_callback;
    void *error_user;
};

struct AAudioStreamStruct {
    /* As opened: unspecified values replaced by what OH granted. p.format is the app's samples. */
    struct AAudioStreamBuilderStruct p;
    OH_AudioRenderer *renderer;
    OH_AudioCapturer *capturer;
    int32_t frame_bytes, oh_frame_bytes, burst, capacity, buffer_size;
    /* OH's samples differ from the app's: converted through scratch, scratch_frames at a time. */
    uint8_t *scratch;
    int32_t scratch_frames;
    pthread_mutex_t lock;
    pthread_cond_t changed; /* a state change, or room or data in the FIFO */
    aaudio_stream_state_t state; /* written under lock; OH's callback reads it without */
    int disconnected;
    /* Read and write: the app's frames between it and OH's callback, capacity of them. */
    uint8_t *fifo;
    int64_t fifo_in, fifo_out;
    int fifo_primed, starved;
    /* Exact-size data callbacks: one app buffer of framesPerDataCallback frames, and how much of
       it OH has not taken yet (output) or how much OH has filled (input). */
    uint8_t *chunk;
    int32_t chunk_frames, chunk_offset;
    int64_t frames_written, frames_read;
    int32_t xruns;
    /* The OH thread inside the app's data callback, for the calls that must not come from it. */
    pthread_t callback_thread;
    int in_callback;
    pthread_t stop_thread, error_thread;
    int stop_thread_live, error_thread_live;
};

static aaudio_policy_t aaudio_mmap_policy = AAUDIO_POLICY_NEVER;
static int32_t aaudio_next_session = 1;

static int64_t aaudio_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t) t.tv_sec * 1000000000LL + t.tv_nsec;
}

static struct timespec aaudio_deadline(int64_t timeout_ns)
{
    int64_t at = aaudio_now() + (timeout_ns > 0 ? timeout_ns : 0);
    struct timespec t = { .tv_sec = (time_t) (at / 1000000000LL), .tv_nsec = (long) (at % 1000000000LL) };
    return t;
}

static aaudio_stream_state_t aaudio_load_state(const struct AAudioStreamStruct *s)
{
    return __atomic_load_n(&s->state, __ATOMIC_ACQUIRE);
}

/* Under the stream's lock. */
static void aaudio_set_state(struct AAudioStreamStruct *s, aaudio_stream_state_t state)
{
    __atomic_store_n(&s->state, state, __ATOMIC_RELEASE);
    pthread_cond_broadcast(&s->changed);
}

static int aaudio_running(aaudio_stream_state_t state)
{
    return state == AAUDIO_STREAM_STATE_STARTING || state == AAUDIO_STREAM_STATE_STARTED;
}

static int aaudio_in_own_callback(struct AAudioStreamStruct *s)
{
    return __atomic_load_n(&s->in_callback, __ATOMIC_ACQUIRE) &&
           pthread_equal(s->callback_thread, pthread_self());
}

static int32_t aaudio_format_bytes(aaudio_format_t format)
{
    switch (format) {
        case AAUDIO_FORMAT_PCM_I16: return 2;
        case AAUDIO_FORMAT_PCM_FLOAT: return 4;
        case AAUDIO_FORMAT_PCM_I24_PACKED: return 3;
        case AAUDIO_FORMAT_PCM_I32: return 4;
        default: return 0;
    }
}

static OH_AudioStream_SampleFormat aaudio_to_oh_format(aaudio_format_t format)
{
    switch (format) {
        case AAUDIO_FORMAT_PCM_I16: return AUDIOSTREAM_SAMPLE_S16LE;
        case AAUDIO_FORMAT_PCM_I24_PACKED: return AUDIOSTREAM_SAMPLE_S24LE;
        case AAUDIO_FORMAT_PCM_I32: return AUDIOSTREAM_SAMPLE_S32LE;
        default: return AUDIOSTREAM_SAMPLE_F32LE;
    }
}

static aaudio_format_t aaudio_from_oh_format(OH_AudioStream_SampleFormat format)
{
    switch (format) {
        case AUDIOSTREAM_SAMPLE_S16LE: return AAUDIO_FORMAT_PCM_I16;
        case AUDIOSTREAM_SAMPLE_S24LE: return AAUDIO_FORMAT_PCM_I24_PACKED;
        case AUDIOSTREAM_SAMPLE_S32LE: return AAUDIO_FORMAT_PCM_I32;
        case AUDIOSTREAM_SAMPLE_F32LE: return AAUDIO_FORMAT_PCM_FLOAT;
        default: return AAUDIO_FORMAT_INVALID;
    }
}

static OH_AudioStream_Usage aaudio_to_oh_usage(aaudio_usage_t usage)
{
    switch (usage) {
        case AAUDIO_USAGE_VOICE_COMMUNICATION:
        case AAUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING: return AUDIOSTREAM_USAGE_VOICE_COMMUNICATION;
        case AAUDIO_USAGE_ALARM: return AUDIOSTREAM_USAGE_ALARM;
        case AAUDIO_USAGE_NOTIFICATION:
        case AAUDIO_USAGE_NOTIFICATION_EVENT:
        case AAUDIO_USAGE_ASSISTANCE_SONIFICATION: return AUDIOSTREAM_USAGE_NOTIFICATION;
        case AAUDIO_USAGE_NOTIFICATION_RINGTONE: return AUDIOSTREAM_USAGE_RINGTONE;
        case AAUDIO_USAGE_ASSISTANCE_ACCESSIBILITY: return AUDIOSTREAM_USAGE_ACCESSIBILITY;
        case AAUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE: return AUDIOSTREAM_USAGE_NAVIGATION;
        case AAUDIO_USAGE_GAME: return AUDIOSTREAM_USAGE_GAME;
        case AAUDIO_USAGE_ASSISTANT: return AUDIOSTREAM_USAGE_VOICE_ASSISTANT;
        default: return AUDIOSTREAM_USAGE_MUSIC;
    }
}

/* Voice calls keep OH's voice-communication capture; every other preset is the plain microphone. */
static OH_AudioStream_SourceType aaudio_to_oh_source(aaudio_input_preset_t preset)
{
    return preset == AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION ? AUDIOSTREAM_SOURCE_TYPE_VOICE_COMMUNICATION
                                                              : AUDIOSTREAM_SOURCE_TYPE_MIC;
}

static aaudio_result_t aaudio_from_oh_result(OH_AudioStream_Result result)
{
    switch (result) {
        case AUDIOSTREAM_SUCCESS: return AAUDIO_OK;
        case AUDIOSTREAM_ERROR_INVALID_PARAM: return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
        case AUDIOSTREAM_ERROR_ILLEGAL_STATE: return AAUDIO_ERROR_INVALID_STATE;
        case AUDIOSTREAM_ERROR_UNSUPPORTED_FORMAT: return AAUDIO_ERROR_INVALID_FORMAT;
        default: return AAUDIO_ERROR_INTERNAL;
    }
}

/* Float and 16-bit samples, the one pair converted at OH's boundary. */
static void aaudio_convert(aaudio_format_t from_format, const uint8_t *from, uint8_t *to, size_t samples)
{
    if (from_format == AAUDIO_FORMAT_PCM_FLOAT) {
        const float *f = (const float *) from;
        int16_t *t = (int16_t *) to;
        for (size_t i = 0; i < samples; i++) {
            float v = f[i] * 32768.0f;
            t[i] = v >= 32767.0f ? 32767 : v <= -32768.0f ? -32768 : (int16_t) (v < 0 ? v - 0.5f : v + 0.5f);
        }
    } else {
        const int16_t *f = (const int16_t *) from;
        float *t = (float *) to;
        for (size_t i = 0; i < samples; i++) t[i] = f[i] * (1.0f / 32768.0f);
    }
}

/* The FIFO holds capacity frames; fifo_in and fifo_out count every frame ever put and taken. */
static void aaudio_fifo_put(struct AAudioStreamStruct *s, const uint8_t *from, int32_t frames)
{
    size_t fb = (size_t) s->frame_bytes;
    int32_t at = (int32_t) (s->fifo_in % s->capacity), first = s->capacity - at;
    if (first > frames) first = frames;
    memcpy(s->fifo + at * fb, from, first * fb);
    memcpy(s->fifo, from + first * fb, (size_t) (frames - first) * fb);
    s->fifo_in += frames;
}

static void aaudio_fifo_take(struct AAudioStreamStruct *s, uint8_t *to, int32_t frames)
{
    size_t fb = (size_t) s->frame_bytes;
    int32_t at = (int32_t) (s->fifo_out % s->capacity), first = s->capacity - at;
    if (first > frames) first = frames;
    memcpy(to, s->fifo + at * fb, first * fb);
    memcpy(to + first * fb, s->fifo, (size_t) (frames - first) * fb);
    s->fifo_out += frames;
}

/* ------------------------------------------------------------------ OH's callbacks -------- */

static void aaudio_stop_oh(struct AAudioStreamStruct *s)
{
    if (s->renderer != NULL) oh.r_stop(s->renderer);
    if (s->capturer != NULL) oh.c_stop(s->capturer);
}

static void *aaudio_stop_after_callback(void *arg)
{
    struct AAudioStreamStruct *s = arg;
    aaudio_stop_oh(s);
    pthread_mutex_lock(&s->lock);
    if (s->state == AAUDIO_STREAM_STATE_STOPPING) aaudio_set_state(s, AAUDIO_STREAM_STATE_STOPPED);
    pthread_mutex_unlock(&s->lock);
    return NULL;
}

/* The data callback returned STOP: AAudio stops the stream after it, never from inside it. */
static void aaudio_callback_said_stop(struct AAudioStreamStruct *s)
{
    pthread_mutex_lock(&s->lock);
    if (aaudio_running(s->state) && !s->stop_thread_live) {
        aaudio_set_state(s, AAUDIO_STREAM_STATE_STOPPING);
        if (pthread_create(&s->stop_thread, NULL, aaudio_stop_after_callback, s) == 0) {
            s->stop_thread_live = 1;
        } else {
            aaudio_set_state(s, AAUDIO_STREAM_STATE_STOPPED);
        }
    }
    pthread_mutex_unlock(&s->lock);
}

static void *aaudio_report_error(void *arg)
{
    struct AAudioStreamStruct *s = arg;
    s->p.error_callback(s, s->p.error_user, AAUDIO_ERROR_DISCONNECTED);
    return NULL;
}

static void aaudio_disconnect(struct AAudioStreamStruct *s, const char *why)
{
    pthread_mutex_lock(&s->lock);
    if (!s->disconnected) {
        AAUDIO_LOG("stream %p disconnected: %s\n", (void *) s, why);
        __atomic_store_n(&s->disconnected, 1, __ATOMIC_RELEASE);
        pthread_cond_broadcast(&s->changed);
        if (s->p.error_callback != NULL && !s->error_thread_live &&
            pthread_create(&s->error_thread, NULL, aaudio_report_error, s) == 0) {
            s->error_thread_live = 1;
        }
    }
    pthread_mutex_unlock(&s->lock);
}

/* Up to frames of the app's output, in its own samples; answers how many there were. */
static int32_t aaudio_app_output(struct AAudioStreamStruct *s, uint8_t *out, int32_t frames)
{
    size_t fb = (size_t) s->frame_bytes;
    int32_t per = s->p.frames_per_callback, done = 0;
    if (s->p.data_callback == NULL) {
        pthread_mutex_lock(&s->lock);
        int64_t queued = s->fifo_in - s->fifo_out;
        done = queued < frames ? (int32_t) queued : frames;
        aaudio_fifo_take(s, out, done);
        s->frames_read += done;
        if (done < frames && s->fifo_primed && s->state == AAUDIO_STREAM_STATE_STARTED) {
            if (!s->starved) s->xruns++;
            s->starved = 1;
        } else if (done == frames) {
            s->starved = 0;
        }
        pthread_cond_broadcast(&s->changed);
        pthread_mutex_unlock(&s->lock);
        return done;
    }
    if (per <= 0) {
        /* A callback that said STOP is not called again for the rest of OH's buffer. */
        if (!aaudio_running(aaudio_load_state(s))) return 0;
        aaudio_data_callback_result_t result = s->p.data_callback(s, s->p.data_user, out, frames);
        __atomic_add_fetch(&s->frames_written, frames, __ATOMIC_RELAXED);
        if (result != AAUDIO_CALLBACK_RESULT_CONTINUE) aaudio_callback_said_stop(s);
        done = frames;
    }
    while (done < frames) {
        if (s->chunk_frames == 0) {
            if (!aaudio_running(aaudio_load_state(s))) break;
            aaudio_data_callback_result_t result = s->p.data_callback(s, s->p.data_user, s->chunk, per);
            __atomic_add_fetch(&s->frames_written, per, __ATOMIC_RELAXED);
            s->chunk_frames = per;
            s->chunk_offset = 0;
            if (result != AAUDIO_CALLBACK_RESULT_CONTINUE) aaudio_callback_said_stop(s);
        }
        int32_t n = frames - done < s->chunk_frames ? frames - done : s->chunk_frames;
        memcpy(out + done * fb, s->chunk + (size_t) s->chunk_offset * fb, n * fb);
        s->chunk_offset += n;
        s->chunk_frames -= n;
        done += n;
    }
    __atomic_add_fetch(&s->frames_read, done, __ATOMIC_RELAXED);
    return done;
}

static int32_t aaudio_render(OH_AudioRenderer *renderer, void *user, void *buffer, int32_t length)
{
    (void) renderer;
    struct AAudioStreamStruct *s = user;
    uint8_t *out = buffer;
    size_t ofb = (size_t) s->oh_frame_bytes;
    int32_t frames = length / s->oh_frame_bytes, done = 0;
    int callback = s->p.data_callback != NULL;
    if (callback && (!aaudio_running(aaudio_load_state(s)) || s->disconnected)) frames = 0;
    if (callback) {
        s->callback_thread = pthread_self();
        __atomic_store_n(&s->in_callback, 1, __ATOMIC_RELEASE);
    }
    while (done < frames) {
        int32_t n = frames - done;
        if (s->scratch != NULL && n > s->scratch_frames) n = s->scratch_frames;
        uint8_t *to = s->scratch != NULL ? s->scratch : out + done * ofb;
        int32_t got = aaudio_app_output(s, to, n);
        if (s->scratch != NULL) {
            aaudio_convert(s->p.format, s->scratch, out + done * ofb, (size_t) got * s->p.channel_count);
        }
        done += got;
        if (got < n) break;
    }
    if (callback) __atomic_store_n(&s->in_callback, 0, __ATOMIC_RELEASE);
    memset(out + done * ofb, 0, (size_t) length - done * ofb);
    return 0;
}

/* Frames of captured input, in the app's samples, to its data callback or the FIFO. */
static void aaudio_app_input(struct AAudioStreamStruct *s, uint8_t *in, int32_t frames)
{
    size_t fb = (size_t) s->frame_bytes;
    int32_t per = s->p.frames_per_callback, done = 0;
    if (s->p.data_callback == NULL) {
        pthread_mutex_lock(&s->lock);
        int64_t room = s->capacity - (s->fifo_in - s->fifo_out);
        int32_t n = room < frames ? (int32_t) room : frames;
        aaudio_fifo_put(s, in, n);
        s->frames_written += n;
        if (n < frames) s->xruns++;
        pthread_cond_broadcast(&s->changed);
        pthread_mutex_unlock(&s->lock);
        return;
    }
    __atomic_add_fetch(&s->frames_written, frames, __ATOMIC_RELAXED);
    if (per <= 0) {
        aaudio_data_callback_result_t result = s->p.data_callback(s, s->p.data_user, in, frames);
        __atomic_add_fetch(&s->frames_read, frames, __ATOMIC_RELAXED);
        if (result != AAUDIO_CALLBACK_RESULT_CONTINUE) aaudio_callback_said_stop(s);
        return;
    }
    while (done < frames && aaudio_running(aaudio_load_state(s))) {
        int32_t n = per - s->chunk_frames < frames - done ? per - s->chunk_frames : frames - done;
        memcpy(s->chunk + (size_t) s->chunk_frames * fb, in + done * fb, n * fb);
        s->chunk_frames += n;
        done += n;
        if (s->chunk_frames == per) {
            aaudio_data_callback_result_t result = s->p.data_callback(s, s->p.data_user, s->chunk, per);
            __atomic_add_fetch(&s->frames_read, per, __ATOMIC_RELAXED);
            s->chunk_frames = 0;
            if (result != AAUDIO_CALLBACK_RESULT_CONTINUE) aaudio_callback_said_stop(s);
        }
    }
}

static int32_t aaudio_capture(OH_AudioCapturer *capturer, void *user, void *buffer, int32_t length)
{
    (void) capturer;
    struct AAudioStreamStruct *s = user;
    uint8_t *in = buffer;
    size_t ofb = (size_t) s->oh_frame_bytes;
    int32_t frames = length / s->oh_frame_bytes, done = 0;
    int callback = s->p.data_callback != NULL;
    if (callback && (!aaudio_running(aaudio_load_state(s)) || s->disconnected)) return 0;
    if (callback) {
        s->callback_thread = pthread_self();
        __atomic_store_n(&s->in_callback, 1, __ATOMIC_RELEASE);
    }
    while (done < frames) {
        int32_t n = frames - done;
        uint8_t *from = in + done * ofb;
        if (s->scratch != NULL) {
            if (n > s->scratch_frames) n = s->scratch_frames;
            aaudio_convert(s->p.format == AAUDIO_FORMAT_PCM_FLOAT ? AAUDIO_FORMAT_PCM_I16 : AAUDIO_FORMAT_PCM_FLOAT,
                           from, s->scratch, (size_t) n * s->p.channel_count);
            from = s->scratch;
        }
        aaudio_app_input(s, from, n);
        done += n;
    }
    if (callback) __atomic_store_n(&s->in_callback, 0, __ATOMIC_RELEASE);
    return 0;
}

static int32_t aaudio_render_event(OH_AudioRenderer *renderer, void *user, OH_AudioStream_Event event)
{
    (void) renderer, (void) user, (void) event;
    return 0;
}

static int32_t aaudio_render_interrupt(OH_AudioRenderer *renderer, void *user, OH_AudioInterrupt_ForceType type,
                                       OH_AudioInterrupt_Hint hint)
{
    (void) renderer;
    AAUDIO_LOG("stream %p interrupted by OH: type %d, hint %d\n", user, (int) type, (int) hint);
    return 0;
}

static int32_t aaudio_render_error(OH_AudioRenderer *renderer, void *user, OH_AudioStream_Result error)
{
    (void) renderer;
    char why[48];
    snprintf(why, sizeof(why), "OH renderer error %d", (int) error);
    aaudio_disconnect(user, why);
    return 0;
}

static int32_t aaudio_capture_event(OH_AudioCapturer *capturer, void *user, OH_AudioStream_Event event)
{
    (void) capturer, (void) user, (void) event;
    return 0;
}

static int32_t aaudio_capture_interrupt(OH_AudioCapturer *capturer, void *user, OH_AudioInterrupt_ForceType type,
                                        OH_AudioInterrupt_Hint hint)
{
    (void) capturer;
    AAUDIO_LOG("stream %p interrupted by OH: type %d, hint %d\n", user, (int) type, (int) hint);
    return 0;
}

static int32_t aaudio_capture_error(OH_AudioCapturer *capturer, void *user, OH_AudioStream_Result error)
{
    (void) capturer;
    char why[48];
    snprintf(why, sizeof(why), "OH capturer error %d", (int) error);
    aaudio_disconnect(user, why);
    return 0;
}

/* ------------------------------------------------------------------ the builder ----------- */

aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **builder)
{
    if (builder == NULL) return AAUDIO_ERROR_NULL;
    struct AAudioStreamBuilderStruct *b = calloc(1, sizeof(*b));
    *builder = b;
    if (b == NULL) return AAUDIO_ERROR_NO_MEMORY;
    b->sharing_mode = AAUDIO_SHARING_MODE_SHARED;
    b->direction = AAUDIO_DIRECTION_OUTPUT;
    b->performance_mode = AAUDIO_PERFORMANCE_MODE_NONE;
    b->session_id = AAUDIO_SESSION_ID_NONE;
    return AAUDIO_OK;
}

aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *builder)
{
    free(builder);
    return AAUDIO_OK;
}

void AAudioStreamBuilder_setDeviceId(AAudioStreamBuilder *builder, int32_t deviceId)
{
    builder->device_id = deviceId;
}

/* Android's audio policy attributes a stream to them; OH attributes it to the process. */
void AAudioStreamBuilder_setPackageName(AAudioStreamBuilder *builder, const char *packageName)
{
    (void) builder, (void) packageName;
}

void AAudioStreamBuilder_setAttributionTag(AAudioStreamBuilder *builder, const char *attributionTag)
{
    (void) builder, (void) attributionTag;
}

void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *builder, int32_t sampleRate)
{
    builder->sample_rate = sampleRate;
}

void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *builder, int32_t channelCount)
{
    builder->channel_count = channelCount;
    builder->channel_mask = AAUDIO_UNSPECIFIED;
}

void AAudioStreamBuilder_setSamplesPerFrame(AAudioStreamBuilder *builder, int32_t samplesPerFrame)
{
    AAudioStreamBuilder_setChannelCount(builder, samplesPerFrame);
}

void AAudioStreamBuilder_setChannelMask(AAudioStreamBuilder *builder, aaudio_channel_mask_t channelMask)
{
    builder->channel_mask = channelMask;
    builder->channel_count = AAUDIO_UNSPECIFIED;
}

void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *builder, aaudio_format_t format)
{
    builder->format = format;
}

void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *builder, aaudio_sharing_mode_t sharingMode)
{
    builder->sharing_mode = sharingMode;
}

void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *builder, aaudio_direction_t direction)
{
    builder->direction = direction;
}

void AAudioStreamBuilder_setBufferCapacityInFrames(AAudioStreamBuilder *builder, int32_t numFrames)
{
    builder->capacity = numFrames;
}

void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *builder, aaudio_performance_mode_t mode)
{
    builder->performance_mode = mode;
}

void AAudioStreamBuilder_setUsage(AAudioStreamBuilder *builder, aaudio_usage_t usage)
{
    builder->usage = usage;
}

void AAudioStreamBuilder_setContentType(AAudioStreamBuilder *builder, aaudio_content_type_t contentType)
{
    builder->content_type = contentType;
}

void AAudioStreamBuilder_setSpatializationBehavior(AAudioStreamBuilder *builder,
                                                   aaudio_spatialization_behavior_t spatializationBehavior)
{
    builder->spatialization = spatializationBehavior;
}

void AAudioStreamBuilder_setIsContentSpatialized(AAudioStreamBuilder *builder, bool isSpatialized)
{
    builder->content_spatialized = isSpatialized;
}

void AAudioStreamBuilder_setInputPreset(AAudioStreamBuilder *builder, aaudio_input_preset_t inputPreset)
{
    builder->input_preset = inputPreset;
}

void AAudioStreamBuilder_setAllowedCapturePolicy(AAudioStreamBuilder *builder,
                                                 aaudio_allowed_capture_policy_t capturePolicy)
{
    builder->capture_policy = capturePolicy;
}

void AAudioStreamBuilder_setSessionId(AAudioStreamBuilder *builder, aaudio_session_id_t sessionId)
{
    builder->session_id = sessionId;
}

void AAudioStreamBuilder_setPrivacySensitive(AAudioStreamBuilder *builder, bool privacySensitive)
{
    builder->privacy_sensitive = privacySensitive;
}

void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *builder, AAudioStream_dataCallback callback,
                                         void *userData)
{
    builder->data_callback = callback;
    builder->data_user = userData;
}

void AAudioStreamBuilder_setFramesPerDataCallback(AAudioStreamBuilder *builder, int32_t numFrames)
{
    builder->frames_per_callback = numFrames;
}

void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *builder, AAudioStream_errorCallback callback,
                                          void *userData)
{
    builder->error_callback = callback;
    builder->error_user = userData;
}

/* AudioStreamBuilder::validate and AudioStreamParameters::validate, for what this supports. */
static aaudio_result_t aaudio_validate(const struct AAudioStreamBuilderStruct *b)
{
    if (b->direction != AAUDIO_DIRECTION_OUTPUT && b->direction != AAUDIO_DIRECTION_INPUT) {
        return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    }
    if (b->sharing_mode != AAUDIO_SHARING_MODE_SHARED && b->sharing_mode != AAUDIO_SHARING_MODE_EXCLUSIVE) {
        return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    }
    if (b->sample_rate != AAUDIO_UNSPECIFIED && (b->sample_rate < 8000 || b->sample_rate > 1600000)) {
        return AAUDIO_ERROR_INVALID_RATE;
    }
    if (b->channel_count != AAUDIO_UNSPECIFIED && (b->channel_count < 1 || b->channel_count > 28)) {
        return AAUDIO_ERROR_OUT_OF_RANGE;
    }
    if (b->format != AAUDIO_FORMAT_UNSPECIFIED && aaudio_format_bytes(b->format) == 0) {
        return AAUDIO_ERROR_INVALID_FORMAT;
    }
    if (b->performance_mode != AAUDIO_PERFORMANCE_MODE_NONE &&
        b->performance_mode != AAUDIO_PERFORMANCE_MODE_POWER_SAVING &&
        b->performance_mode != AAUDIO_PERFORMANCE_MODE_LOW_LATENCY) {
        return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    }
    if (b->capacity < 0 || b->frames_per_callback < 0 || b->frames_per_callback > 1024 * 1024) {
        return AAUDIO_ERROR_OUT_OF_RANGE;
    }
    return AAUDIO_OK;
}

static void aaudio_free(struct AAudioStreamStruct *s)
{
    if (s->renderer != NULL) oh.r_release(s->renderer);
    if (s->capturer != NULL) oh.c_release(s->capturer);
    pthread_cond_destroy(&s->changed);
    pthread_mutex_destroy(&s->lock);
    free(s->fifo);
    free(s->chunk);
    free(s->scratch);
    free(s);
}

/* An OH renderer or capturer for the stream's parameters, in OH's format. */
static OH_AudioStream_Result aaudio_make_oh(struct AAudioStreamStruct *s, OH_AudioStream_SampleFormat format)
{
    int output = s->p.direction == AAUDIO_DIRECTION_OUTPUT;
    OH_AudioStreamBuilder *ob = NULL;
    OH_AudioStream_Result r = oh.builder_create(&ob, output ? AUDIOSTREAM_TYPE_RENDERER : AUDIOSTREAM_TYPE_CAPTURER);
    if (r == AUDIOSTREAM_SUCCESS) r = oh.set_rate(ob, s->p.sample_rate);
    if (r == AUDIOSTREAM_SUCCESS) r = oh.set_channels(ob, s->p.channel_count);
    if (r == AUDIOSTREAM_SUCCESS) r = oh.set_format(ob, format);
    if (r == AUDIOSTREAM_SUCCESS) r = oh.set_latency(ob, AUDIOSTREAM_LATENCY_MODE_NORMAL);
    if (output) {
        OH_AudioRenderer_Callbacks callbacks = {
            .OH_AudioRenderer_OnWriteData = aaudio_render,
            .OH_AudioRenderer_OnStreamEvent = aaudio_render_event,
            .OH_AudioRenderer_OnInterruptEvent = aaudio_render_interrupt,
            .OH_AudioRenderer_OnError = aaudio_render_error,
        };
        if (r == AUDIOSTREAM_SUCCESS) r = oh.set_renderer_info(ob, aaudio_to_oh_usage(s->p.usage));
        if (r == AUDIOSTREAM_SUCCESS) r = oh.set_renderer_callback(ob, callbacks, s);
        if (r == AUDIOSTREAM_SUCCESS) r = oh.generate_renderer(ob, &s->renderer);
    } else {
        OH_AudioCapturer_Callbacks callbacks = {
            .OH_AudioCapturer_OnReadData = aaudio_capture,
            .OH_AudioCapturer_OnStreamEvent = aaudio_capture_event,
            .OH_AudioCapturer_OnInterruptEvent = aaudio_capture_interrupt,
            .OH_AudioCapturer_OnError = aaudio_capture_error,
        };
        if (r == AUDIOSTREAM_SUCCESS) r = oh.set_capturer_info(ob, aaudio_to_oh_source(s->p.input_preset));
        if (r == AUDIOSTREAM_SUCCESS) r = oh.set_capturer_callback(ob, callbacks, s);
        if (r == AUDIOSTREAM_SUCCESS) r = oh.generate_capturer(ob, &s->capturer);
    }
    if (ob != NULL) oh.builder_destroy(ob);
    if (r == AUDIOSTREAM_SUCCESS && s->renderer == NULL && s->capturer == NULL) r = AUDIOSTREAM_ERROR_SYSTEM;
    return r;
}

aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *builder, AAudioStream **stream)
{
    if (builder == NULL || stream == NULL) return AAUDIO_ERROR_NULL;
    *stream = NULL;
    aaudio_result_t result = aaudio_validate(builder);
    if (result != AAUDIO_OK) return result;
    if (pthread_once(&oh_once, oh_load) != 0 || !oh_loaded) return AAUDIO_ERROR_UNAVAILABLE;

    int output = builder->direction == AAUDIO_DIRECTION_OUTPUT;
    struct AAudioStreamStruct *s = calloc(1, sizeof(*s));
    if (s == NULL) return AAUDIO_ERROR_NO_MEMORY;
    pthread_condattr_t attributes;
    pthread_condattr_init(&attributes);
    pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    pthread_cond_init(&s->changed, &attributes);
    pthread_condattr_destroy(&attributes);
    pthread_mutex_init(&s->lock, NULL);
    s->p = *builder;
    if (s->p.channel_count == AAUDIO_UNSPECIFIED && s->p.channel_mask != AAUDIO_UNSPECIFIED) {
        s->p.channel_count = __builtin_popcount(s->p.channel_mask);
    }
    if (s->p.channel_count == AAUDIO_UNSPECIFIED) s->p.channel_count = output ? 2 : 1;
    if (s->p.sample_rate == AAUDIO_UNSPECIFIED) s->p.sample_rate = 48000;
    if (s->p.format == AAUDIO_FORMAT_UNSPECIFIED) s->p.format = AAUDIO_FORMAT_PCM_FLOAT;

    OH_AudioStream_Result r = aaudio_make_oh(s, aaudio_to_oh_format(s->p.format));
    if (r != AUDIOSTREAM_SUCCESS && s->p.format == AAUDIO_FORMAT_PCM_FLOAT) {
        r = aaudio_make_oh(s, AUDIOSTREAM_SAMPLE_S16LE);
    } else if (r != AUDIOSTREAM_SUCCESS && s->p.format == AAUDIO_FORMAT_PCM_I16) {
        r = aaudio_make_oh(s, AUDIOSTREAM_SAMPLE_F32LE);
    }
    if (r != AUDIOSTREAM_SUCCESS) {
        AAUDIO_LOG("OH refused a %s stream (%d Hz, %d channels, format %d): error %d\n",
                   output ? "output" : "input", s->p.sample_rate, s->p.channel_count, s->p.format, (int) r);
        aaudio_free(s);
        return r == AUDIOSTREAM_ERROR_UNSUPPORTED_FORMAT ? AAUDIO_ERROR_INVALID_FORMAT : AAUDIO_ERROR_UNAVAILABLE;
    }

    /* What OH granted. */
    int32_t rate = 0, channels = 0, burst = 0;
    OH_AudioStream_SampleFormat granted = aaudio_to_oh_format(s->p.format);
    uint32_t id = 0;
    if (output) {
        oh.r_rate(s->renderer, &rate);
        oh.r_channels(s->renderer, &channels);
        oh.r_format(s->renderer, &granted);
        oh.r_frame_size(s->renderer, &burst);
        if (oh.r_stream_id != NULL) oh.r_stream_id(s->renderer, &id);
    } else {
        oh.c_rate(s->capturer, &rate);
        oh.c_channels(s->capturer, &channels);
        oh.c_format(s->capturer, &granted);
        oh.c_frame_size(s->capturer, &burst);
        if (oh.c_stream_id != NULL) oh.c_stream_id(s->capturer, &id);
    }
    aaudio_format_t oh_format = aaudio_from_oh_format(granted);
    if (rate > 0) s->p.sample_rate = rate;
    if (channels > 0) s->p.channel_count = channels;
    if (builder->format == AAUDIO_FORMAT_UNSPECIFIED && oh_format != AAUDIO_FORMAT_INVALID) s->p.format = oh_format;
    if (oh_format != s->p.format &&
        !(oh_format == AAUDIO_FORMAT_PCM_I16 && s->p.format == AAUDIO_FORMAT_PCM_FLOAT) &&
        !(oh_format == AAUDIO_FORMAT_PCM_FLOAT && s->p.format == AAUDIO_FORMAT_PCM_I16)) {
        AAUDIO_LOG("OH granted format %d for a stream asking for %d\n", (int) granted, s->p.format);
        aaudio_free(s);
        return AAUDIO_ERROR_INVALID_FORMAT;
    }
    s->frame_bytes = s->p.channel_count * aaudio_format_bytes(s->p.format);
    s->oh_frame_bytes = s->p.channel_count * aaudio_format_bytes(oh_format);
    s->burst = burst > 0 ? burst : s->p.sample_rate / 50;
    s->capacity = s->p.capacity > 4 * s->burst ? s->p.capacity : 4 * s->burst;
    if (s->capacity < 2 * s->p.frames_per_callback) s->capacity = 2 * s->p.frames_per_callback;
    s->buffer_size = s->capacity;
    s->p.capacity = s->capacity;
    if (s->p.performance_mode == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY) {
        s->p.performance_mode = AAUDIO_PERFORMANCE_MODE_NONE;
    }
    s->p.sharing_mode = AAUDIO_SHARING_MODE_SHARED;
    if (s->p.session_id == AAUDIO_SESSION_ID_ALLOCATE) {
        s->p.session_id = id > 0 && id < 0x7fffffff ? (int32_t) id
                                                    : __atomic_fetch_add(&aaudio_next_session, 1, __ATOMIC_RELAXED);
    }
    int ok = 1;
    if (oh_format != s->p.format) {
        s->scratch_frames = s->burst > 1024 ? s->burst : 1024;
        ok = (s->scratch = malloc((size_t) s->scratch_frames * s->frame_bytes)) != NULL;
    }
    if (s->p.data_callback == NULL) {
        ok = ok && (s->fifo = malloc((size_t) s->capacity * s->frame_bytes)) != NULL;
    } else if (s->p.frames_per_callback > 0) {
        ok = ok && (s->chunk = malloc((size_t) s->p.frames_per_callback * s->frame_bytes)) != NULL;
    }
    if (!ok) {
        aaudio_free(s);
        return AAUDIO_ERROR_NO_MEMORY;
    }
    s->state = AAUDIO_STREAM_STATE_OPEN;
    AAUDIO_LOG("opened %s stream %p: %d Hz, %d channels, format %d%s, %s, %d frames per callback, burst %d, "
               "capacity %d\n", output ? "output" : "input", (void *) s, s->p.sample_rate, s->p.channel_count,
               s->p.format, s->scratch != NULL ? " (converted)" : "",
               s->p.data_callback != NULL ? "data callback" : "read/write", s->p.frames_per_callback, s->burst,
               s->capacity);
    *stream = s;
    return AAUDIO_OK;
}

/* ------------------------------------------------------------------ stream control -------- */

/* Under the stream's lock: hands over the stop thread, if any, for the caller to join unlocked. */
static int aaudio_take_stop_thread(struct AAudioStreamStruct *s, pthread_t *thread)
{
    int live = s->stop_thread_live;
    *thread = s->stop_thread;
    s->stop_thread_live = 0;
    return live;
}

static void aaudio_join_stop_thread(struct AAudioStreamStruct *s)
{
    pthread_t thread;
    pthread_mutex_lock(&s->lock);
    int live = aaudio_take_stop_thread(s, &thread);
    pthread_mutex_unlock(&s->lock);
    if (live) pthread_join(thread, NULL);
}

aaudio_result_t AAudioStream_requestStart(AAudioStream *s)
{
    if (s == NULL) return AAUDIO_ERROR_NULL;
    if (aaudio_in_own_callback(s)) return AAUDIO_ERROR_INVALID_STATE;
    aaudio_join_stop_thread(s);
    pthread_mutex_lock(&s->lock);
    switch (s->disconnected ? AAUDIO_STREAM_STATE_DISCONNECTED : s->state) {
        case AAUDIO_STREAM_STATE_OPEN:
        case AAUDIO_STREAM_STATE_PAUSING:
        case AAUDIO_STREAM_STATE_PAUSED:
        case AAUDIO_STREAM_STATE_STOPPING:
        case AAUDIO_STREAM_STATE_STOPPED:
        case AAUDIO_STREAM_STATE_FLUSHING:
        case AAUDIO_STREAM_STATE_FLUSHED:
            break;
        default:
            pthread_mutex_unlock(&s->lock);
            return AAUDIO_ERROR_INVALID_STATE;
    }
    aaudio_stream_state_t before = s->state;
    s->starved = 0;
    aaudio_set_state(s, AAUDIO_STREAM_STATE_STARTING);
    pthread_mutex_unlock(&s->lock);
    OH_AudioStream_Result r = s->renderer != NULL ? oh.r_start(s->renderer) : oh.c_start(s->capturer);
    pthread_mutex_lock(&s->lock);
    if (s->state == AAUDIO_STREAM_STATE_STARTING) {
        aaudio_set_state(s, r == AUDIOSTREAM_SUCCESS ? AAUDIO_STREAM_STATE_STARTED : before);
    }
    pthread_mutex_unlock(&s->lock);
    if (r != AUDIOSTREAM_SUCCESS) AAUDIO_LOG("OH refused to start stream %p: error %d\n", (void *) s, (int) r);
    return aaudio_from_oh_result(r);
}

aaudio_result_t AAudioStream_requestPause(AAudioStream *s)
{
    if (s == NULL) return AAUDIO_ERROR_NULL;
    if (s->p.direction != AAUDIO_DIRECTION_OUTPUT) return AAUDIO_ERROR_UNIMPLEMENTED;
    if (aaudio_in_own_callback(s)) return AAUDIO_ERROR_INVALID_STATE;
    aaudio_join_stop_thread(s);
    pthread_mutex_lock(&s->lock);
    if (s->disconnected) {
        pthread_mutex_unlock(&s->lock);
        return AAUDIO_ERROR_DISCONNECTED;
    }
    switch (s->state) {
        case AAUDIO_STREAM_STATE_STARTING:
        case AAUDIO_STREAM_STATE_STARTED:
            break;
        case AAUDIO_STREAM_STATE_OPEN:
        case AAUDIO_STREAM_STATE_STOPPED:
        case AAUDIO_STREAM_STATE_FLUSHED:
            aaudio_set_state(s, AAUDIO_STREAM_STATE_PAUSED);
            /* fallthrough */
        case AAUDIO_STREAM_STATE_PAUSING:
        case AAUDIO_STREAM_STATE_PAUSED:
            pthread_mutex_unlock(&s->lock);
            return AAUDIO_OK;
        default:
            pthread_mutex_unlock(&s->lock);
            return AAUDIO_ERROR_INVALID_STATE;
    }
    aaudio_set_state(s, AAUDIO_STREAM_STATE_PAUSING);
    pthread_mutex_unlock(&s->lock);
    OH_AudioStream_Result r = oh.r_pause(s->renderer);
    pthread_mutex_lock(&s->lock);
    if (s->state == AAUDIO_STREAM_STATE_PAUSING) {
        aaudio_set_state(s, r == AUDIOSTREAM_SUCCESS ? AAUDIO_STREAM_STATE_PAUSED : AAUDIO_STREAM_STATE_STARTED);
    }
    pthread_mutex_unlock(&s->lock);
    return aaudio_from_oh_result(r);
}

aaudio_result_t AAudioStream_requestFlush(AAudioStream *s)
{
    if (s == NULL) return AAUDIO_ERROR_NULL;
    if (s->p.direction != AAUDIO_DIRECTION_OUTPUT) return AAUDIO_ERROR_UNIMPLEMENTED;
    if (aaudio_in_own_callback(s)) return AAUDIO_ERROR_INVALID_STATE;
    aaudio_join_stop_thread(s);
    pthread_mutex_lock(&s->lock);
    switch (s->disconnected ? AAUDIO_STREAM_STATE_DISCONNECTED : s->state) {
        case AAUDIO_STREAM_STATE_OPEN:
        case AAUDIO_STREAM_STATE_PAUSED:
        case AAUDIO_STREAM_STATE_STOPPED:
        case AAUDIO_STREAM_STATE_FLUSHED:
            break;
        default:
            pthread_mutex_unlock(&s->lock);
            return AAUDIO_ERROR_INVALID_STATE;
    }
    aaudio_stream_state_t before = s->state;
    aaudio_set_state(s, AAUDIO_STREAM_STATE_FLUSHING);
    pthread_mutex_unlock(&s->lock);
    /* OH flushes a paused or stopped renderer and refuses one it never started; either is empty after. */
    if (before == AAUDIO_STREAM_STATE_PAUSED || before == AAUDIO_STREAM_STATE_STOPPED) oh.r_flush(s->renderer);
    pthread_mutex_lock(&s->lock);
    s->fifo_out = s->fifo_in;
    s->fifo_primed = 0;
    s->chunk_frames = 0;
    aaudio_set_state(s, AAUDIO_STREAM_STATE_FLUSHED);
    pthread_mutex_unlock(&s->lock);
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_requestStop(AAudioStream *s)
{
    if (s == NULL) return AAUDIO_ERROR_NULL;
    if (aaudio_in_own_callback(s)) return AAUDIO_ERROR_INVALID_STATE;
    aaudio_join_stop_thread(s);
    pthread_mutex_lock(&s->lock);
    switch (s->state) {
        case AAUDIO_STREAM_STATE_STARTING:
        case AAUDIO_STREAM_STATE_STARTED:
        case AAUDIO_STREAM_STATE_OPEN:
        case AAUDIO_STREAM_STATE_PAUSED:
        case AAUDIO_STREAM_STATE_FLUSHED:
            break;
        case AAUDIO_STREAM_STATE_STOPPING:
        case AAUDIO_STREAM_STATE_STOPPED:
            pthread_mutex_unlock(&s->lock);
            return AAUDIO_OK;
        default:
            pthread_mutex_unlock(&s->lock);
            return AAUDIO_ERROR_INVALID_STATE;
    }
    aaudio_stream_state_t before = s->state;
    aaudio_set_state(s, AAUDIO_STREAM_STATE_STOPPING);
    pthread_mutex_unlock(&s->lock);
    if (aaudio_running(before) || before == AAUDIO_STREAM_STATE_PAUSED) aaudio_stop_oh(s);
    pthread_mutex_lock(&s->lock);
    if (s->p.direction == AAUDIO_DIRECTION_OUTPUT) {
        /* What was written and not yet played is dropped, and the next start begins empty. */
        s->fifo_out = s->fifo_in;
        s->fifo_primed = 0;
        s->chunk_frames = 0;
    }
    if (s->state == AAUDIO_STREAM_STATE_STOPPING) aaudio_set_state(s, AAUDIO_STREAM_STATE_STOPPED);
    pthread_mutex_unlock(&s->lock);
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_release(AAudioStream *s)
{
    if (s == NULL) return AAUDIO_ERROR_NULL;
    if (aaudio_in_own_callback(s)) return AAUDIO_ERROR_INVALID_STATE;
    pthread_mutex_lock(&s->lock);
    aaudio_stream_state_t before = s->state;
    if (before == AAUDIO_STREAM_STATE_CLOSING || before == AAUDIO_STREAM_STATE_CLOSED) {
        pthread_mutex_unlock(&s->lock);
        return before == AAUDIO_STREAM_STATE_CLOSING ? AAUDIO_OK : AAUDIO_ERROR_INVALID_STATE;
    }
    /* No data callback starts a stop thread once the stream is closing; one already started is
       joined before OH's objects go. */
    aaudio_set_state(s, AAUDIO_STREAM_STATE_CLOSING);
    pthread_t thread;
    int live = aaudio_take_stop_thread(s, &thread);
    pthread_mutex_unlock(&s->lock);
    if (live) pthread_join(thread, NULL);
    if (aaudio_running(before) || before == AAUDIO_STREAM_STATE_PAUSED || before == AAUDIO_STREAM_STATE_STOPPING) {
        aaudio_stop_oh(s);
    }
    /* After OH releases its renderer or capturer, its thread calls nothing here again. */
    if (s->renderer != NULL) oh.r_release(s->renderer);
    if (s->capturer != NULL) oh.c_release(s->capturer);
    pthread_mutex_lock(&s->lock);
    s->renderer = NULL;
    s->capturer = NULL;
    pthread_cond_broadcast(&s->changed);
    pthread_mutex_unlock(&s->lock);
    return AAUDIO_OK;
}

aaudio_result_t AAudioStream_close(AAudioStream *s)
{
    if (s == NULL) return AAUDIO_ERROR_NULL;
    if (aaudio_in_own_callback(s)) return AAUDIO_ERROR_INVALID_STATE;
    aaudio_result_t result = AAudioStream_release(s);
    if (result != AAUDIO_OK) return result;
    for (int i = 0; i < 1000 && __atomic_load_n(&s->in_callback, __ATOMIC_ACQUIRE); i++) {
        struct timespec pause = { 0, 1000000 };
        nanosleep(&pause, NULL);
    }
    pthread_mutex_lock(&s->lock);
    /* An app that closes the stream from its own error callback leaves that thread to finish. */
    int joinable = s->error_thread_live && !pthread_equal(s->error_thread, pthread_self());
    pthread_t thread = s->error_thread;
    if (s->error_thread_live && !joinable) pthread_detach(thread);
    s->error_thread_live = 0;
    aaudio_set_state(s, AAUDIO_STREAM_STATE_CLOSED);
    pthread_mutex_unlock(&s->lock);
    if (joinable) pthread_join(thread, NULL);
    aaudio_free(s);
    return AAUDIO_OK;
}

aaudio_stream_state_t AAudioStream_getState(AAudioStream *s)
{
    if (s == NULL) return AAUDIO_STREAM_STATE_UNINITIALIZED;
    return __atomic_load_n(&s->disconnected, __ATOMIC_ACQUIRE) ? AAUDIO_STREAM_STATE_DISCONNECTED
                                                               : aaudio_load_state(s);
}

aaudio_result_t AAudioStream_waitForStateChange(AAudioStream *s, aaudio_stream_state_t inputState,
                                                aaudio_stream_state_t *nextState, int64_t timeoutNanoseconds)
{
    if (s == NULL) return AAUDIO_ERROR_NULL;
    struct timespec deadline = aaudio_deadline(timeoutNanoseconds);
    pthread_mutex_lock(&s->lock);
    aaudio_stream_state_t state = s->disconnected ? AAUDIO_STREAM_STATE_DISCONNECTED : s->state;
    while (state == inputState && timeoutNanoseconds > 0) {
        int waited = pthread_cond_timedwait(&s->changed, &s->lock, &deadline);
        state = s->disconnected ? AAUDIO_STREAM_STATE_DISCONNECTED : s->state;
        if (waited == ETIMEDOUT) break;
    }
    pthread_mutex_unlock(&s->lock);
    if (nextState != NULL) *nextState = state;
    return state == inputState ? AAUDIO_ERROR_TIMEOUT : AAUDIO_OK;
}

/* ------------------------------------------------------------------ read and write -------- */

aaudio_result_t AAudioStream_write(AAudioStream *s, const void *buffer, int32_t numFrames, int64_t timeoutNanoseconds)
{
    if (s == NULL || buffer == NULL) return AAUDIO_ERROR_NULL;
    if (s->p.direction != AAUDIO_DIRECTION_OUTPUT) return AAUDIO_ERROR_UNIMPLEMENTED;
    if (numFrames < 0) return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    if (numFrames == 0) return 0;
    if (s->p.data_callback != NULL) return AAUDIO_ERROR_INVALID_STATE;
    const uint8_t *from = buffer;
    struct timespec deadline = aaudio_deadline(timeoutNanoseconds);
    aaudio_result_t error = AAUDIO_OK;
    int32_t written = 0;
    pthread_mutex_lock(&s->lock);
    while (written < numFrames) {
        if (s->disconnected) {
            error = AAUDIO_ERROR_DISCONNECTED;
            break;
        }
        if (s->state == AAUDIO_STREAM_STATE_CLOSING || s->state == AAUDIO_STREAM_STATE_CLOSED) {
            error = AAUDIO_ERROR_INVALID_STATE;
            break;
        }
        int64_t room = s->buffer_size - (s->fifo_in - s->fifo_out);
        if (room > 0) {
            int32_t n = room < numFrames - written ? (int32_t) room : numFrames - written;
            aaudio_fifo_put(s, from + (size_t) written * s->frame_bytes, n);
            s->frames_written += n;
            s->fifo_primed = 1;
            written += n;
            continue;
        }
        if (timeoutNanoseconds <= 0 || pthread_cond_timedwait(&s->changed, &s->lock, &deadline) == ETIMEDOUT) break;
    }
    pthread_mutex_unlock(&s->lock);
    return written > 0 || error == AAUDIO_OK ? written : error;
}

aaudio_result_t AAudioStream_read(AAudioStream *s, void *buffer, int32_t numFrames, int64_t timeoutNanoseconds)
{
    if (s == NULL || buffer == NULL) return AAUDIO_ERROR_NULL;
    if (s->p.direction != AAUDIO_DIRECTION_INPUT) return AAUDIO_ERROR_UNIMPLEMENTED;
    if (numFrames < 0) return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    if (numFrames == 0) return 0;
    if (s->p.data_callback != NULL) return AAUDIO_ERROR_INVALID_STATE;
    uint8_t *to = buffer;
    struct timespec deadline = aaudio_deadline(timeoutNanoseconds);
    aaudio_result_t error = AAUDIO_OK;
    int32_t got = 0;
    pthread_mutex_lock(&s->lock);
    while (got < numFrames) {
        int64_t queued = s->fifo_in - s->fifo_out;
        if (queued > 0) {
            int32_t n = queued < numFrames - got ? (int32_t) queued : numFrames - got;
            aaudio_fifo_take(s, to + (size_t) got * s->frame_bytes, n);
            s->frames_read += n;
            got += n;
            continue;
        }
        if (s->disconnected) {
            error = AAUDIO_ERROR_DISCONNECTED;
            break;
        }
        if (s->state == AAUDIO_STREAM_STATE_CLOSING || s->state == AAUDIO_STREAM_STATE_CLOSED) {
            error = AAUDIO_ERROR_INVALID_STATE;
            break;
        }
        if (timeoutNanoseconds <= 0 || pthread_cond_timedwait(&s->changed, &s->lock, &deadline) == ETIMEDOUT) break;
    }
    pthread_mutex_unlock(&s->lock);
    return got > 0 || error == AAUDIO_OK ? got : error;
}

/* ------------------------------------------------------------------ buffer and position --- */

aaudio_result_t AAudioStream_setBufferSizeInFrames(AAudioStream *s, int32_t numFrames)
{
    if (s == NULL) return AAUDIO_ERROR_NULL;
    if (numFrames < 0) return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    int32_t least = s->burst < s->capacity ? s->burst : s->capacity;
    int32_t size = numFrames < least ? least : numFrames > s->capacity ? s->capacity : numFrames;
    pthread_mutex_lock(&s->lock);
    s->buffer_size = size;
    pthread_cond_broadcast(&s->changed);
    pthread_mutex_unlock(&s->lock);
    return size;
}

int32_t AAudioStream_getBufferSizeInFrames(AAudioStream *s)
{
    return s != NULL ? s->buffer_size : AAUDIO_ERROR_NULL;
}

int32_t AAudioStream_getFramesPerBurst(AAudioStream *s)
{
    return s != NULL ? s->burst : AAUDIO_ERROR_NULL;
}

int32_t AAudioStream_getBufferCapacityInFrames(AAudioStream *s)
{
    return s != NULL ? s->capacity : AAUDIO_ERROR_NULL;
}

int32_t AAudioStream_getFramesPerDataCallback(AAudioStream *s)
{
    return s != NULL ? s->p.frames_per_callback : AAUDIO_ERROR_NULL;
}

int32_t AAudioStream_getXRunCount(AAudioStream *s)
{
    if (s == NULL) return AAUDIO_ERROR_NULL;
    uint32_t oh_count = 0;
    if (s->renderer != NULL && oh.r_underflows != NULL) oh.r_underflows(s->renderer, &oh_count);
    if (s->capturer != NULL && oh.c_overflows != NULL) oh.c_overflows(s->capturer, &oh_count);
    return __atomic_load_n(&s->xruns, __ATOMIC_RELAXED) + (int32_t) oh_count;
}

int64_t AAudioStream_getFramesWritten(AAudioStream *s)
{
    return s != NULL ? __atomic_load_n(&s->frames_written, __ATOMIC_RELAXED) : AAUDIO_ERROR_NULL;
}

int64_t AAudioStream_getFramesRead(AAudioStream *s)
{
    return s != NULL ? __atomic_load_n(&s->frames_read, __ATOMIC_RELAXED) : AAUDIO_ERROR_NULL;
}

aaudio_result_t AAudioStream_getTimestamp(AAudioStream *s, clockid_t clockid, int64_t *framePosition,
                                          int64_t *timeNanoseconds)
{
    if (s == NULL || framePosition == NULL || timeNanoseconds == NULL) return AAUDIO_ERROR_NULL;
    if (clockid != CLOCK_MONOTONIC && clockid != CLOCK_BOOTTIME) return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    if (s->disconnected) return AAUDIO_ERROR_DISCONNECTED;
    if (aaudio_load_state(s) != AAUDIO_STREAM_STATE_STARTED) return AAUDIO_ERROR_INVALID_STATE;
    int64_t position = 0, time = 0;
    OH_AudioStream_Result r = s->renderer != NULL ? oh.r_timestamp(s->renderer, clockid, &position, &time)
                                                  : oh.c_timestamp(s->capturer, clockid, &position, &time);
    if (r != AUDIOSTREAM_SUCCESS || time <= 0 || position < 0) return AAUDIO_ERROR_INVALID_STATE;
    /* A renderer's position counts the silence OH played before the app wrote; it is not the app's. */
    if (s->renderer != NULL && position >= AAudioStream_getFramesWritten(s)) return AAUDIO_ERROR_INVALID_STATE;
    *framePosition = position;
    *timeNanoseconds = time;
    return AAUDIO_OK;
}

/* ------------------------------------------------------------------ parameters ------------ */

int32_t AAudioStream_getSampleRate(AAudioStream *s)
{
    return s != NULL ? s->p.sample_rate : AAUDIO_ERROR_NULL;
}

int32_t AAudioStream_getHardwareSampleRate(AAudioStream *s)
{
    return AAudioStream_getSampleRate(s);
}

int32_t AAudioStream_getChannelCount(AAudioStream *s)
{
    return s != NULL ? s->p.channel_count : AAUDIO_ERROR_NULL;
}

int32_t AAudioStream_getHardwareChannelCount(AAudioStream *s)
{
    return AAudioStream_getChannelCount(s);
}

int32_t AAudioStream_getSamplesPerFrame(AAudioStream *s)
{
    return AAudioStream_getChannelCount(s);
}

aaudio_channel_mask_t AAudioStream_getChannelMask(AAudioStream *s)
{
    if (s == NULL) return AAUDIO_CHANNEL_INVALID;
    if (s->p.channel_mask != AAUDIO_UNSPECIFIED) return s->p.channel_mask;
    return s->p.channel_count == 1 ? AAUDIO_CHANNEL_MONO
           : s->p.channel_count == 2 ? AAUDIO_CHANNEL_STEREO : AAUDIO_CHANNEL_INVALID;
}

int32_t AAudioStream_getDeviceId(AAudioStream *s)
{
    return s != NULL ? s->p.device_id : AAUDIO_ERROR_NULL;
}

aaudio_format_t AAudioStream_getFormat(AAudioStream *s)
{
    return s != NULL ? s->p.format : AAUDIO_FORMAT_INVALID;
}

aaudio_format_t AAudioStream_getHardwareFormat(AAudioStream *s)
{
    return AAudioStream_getFormat(s);
}

aaudio_sharing_mode_t AAudioStream_getSharingMode(AAudioStream *s)
{
    return s != NULL ? s->p.sharing_mode : AAUDIO_SHARING_MODE_SHARED;
}

aaudio_performance_mode_t AAudioStream_getPerformanceMode(AAudioStream *s)
{
    return s != NULL ? s->p.performance_mode : AAUDIO_PERFORMANCE_MODE_NONE;
}

aaudio_direction_t AAudioStream_getDirection(AAudioStream *s)
{
    return s != NULL ? s->p.direction : AAUDIO_DIRECTION_OUTPUT;
}

aaudio_session_id_t AAudioStream_getSessionId(AAudioStream *s)
{
    return s != NULL ? s->p.session_id : AAUDIO_SESSION_ID_NONE;
}

aaudio_usage_t AAudioStream_getUsage(AAudioStream *s)
{
    return s != NULL && s->p.usage != AAUDIO_UNSPECIFIED ? s->p.usage : AAUDIO_USAGE_MEDIA;
}

aaudio_content_type_t AAudioStream_getContentType(AAudioStream *s)
{
    return s != NULL && s->p.content_type != AAUDIO_UNSPECIFIED ? s->p.content_type : AAUDIO_CONTENT_TYPE_MUSIC;
}

aaudio_spatialization_behavior_t AAudioStream_getSpatializationBehavior(AAudioStream *s)
{
    return s != NULL && s->p.spatialization != AAUDIO_UNSPECIFIED ? s->p.spatialization
                                                                  : AAUDIO_SPATIALIZATION_BEHAVIOR_AUTO;
}

bool AAudioStream_isContentSpatialized(AAudioStream *s)
{
    return s != NULL && s->p.content_spatialized;
}

aaudio_input_preset_t AAudioStream_getInputPreset(AAudioStream *s)
{
    return s != NULL && s->p.input_preset != AAUDIO_UNSPECIFIED ? s->p.input_preset
                                                                : AAUDIO_INPUT_PRESET_VOICE_RECOGNITION;
}

aaudio_allowed_capture_policy_t AAudioStream_getAllowedCapturePolicy(AAudioStream *s)
{
    return s != NULL && s->p.capture_policy != AAUDIO_UNSPECIFIED ? s->p.capture_policy
                                                                  : AAUDIO_ALLOW_CAPTURE_BY_ALL;
}

bool AAudioStream_isPrivacySensitive(AAudioStream *s)
{
    return s != NULL && s->p.privacy_sensitive;
}

/* AAudioTesting.h: there is no MMAP path, and an app that sets a policy reads it back. */
bool AAudioStream_isMMapUsed(AAudioStream *s)
{
    (void) s;
    return false;
}

aaudio_result_t AAudio_setMMapPolicy(aaudio_policy_t policy)
{
    if (policy != AAUDIO_UNSPECIFIED && policy != AAUDIO_POLICY_NEVER && policy != AAUDIO_POLICY_AUTO &&
        policy != AAUDIO_POLICY_ALWAYS) {
        return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    }
    __atomic_store_n(&aaudio_mmap_policy, policy, __ATOMIC_RELAXED);
    return AAUDIO_OK;
}

aaudio_policy_t AAudio_getMMapPolicy(void)
{
    return __atomic_load_n(&aaudio_mmap_policy, __ATOMIC_RELAXED);
}

/* ------------------------------------------------------------------ text ------------------ */

#define AAUDIO_CASE_ENUM(name) \
    case name: return #name

const char *AAudio_convertResultToText(aaudio_result_t returnCode)
{
    switch (returnCode) {
        AAUDIO_CASE_ENUM(AAUDIO_OK);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_DISCONNECTED);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_ILLEGAL_ARGUMENT);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_INTERNAL);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_INVALID_STATE);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_INVALID_HANDLE);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_UNIMPLEMENTED);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_UNAVAILABLE);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_NO_FREE_HANDLES);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_NO_MEMORY);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_NULL);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_TIMEOUT);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_WOULD_BLOCK);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_INVALID_FORMAT);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_OUT_OF_RANGE);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_NO_SERVICE);
        AAUDIO_CASE_ENUM(AAUDIO_ERROR_INVALID_RATE);
    }
    return "Unrecognized";
}

const char *AAudio_convertStreamStateToText(aaudio_stream_state_t state)
{
    switch (state) {
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_UNINITIALIZED);
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_UNKNOWN);
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_OPEN);
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_STARTING);
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_STARTED);
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_PAUSING);
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_PAUSED);
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_FLUSHING);
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_FLUSHED);
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_STOPPING);
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_STOPPED);
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_DISCONNECTED);
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_CLOSING);
        AAUDIO_CASE_ENUM(AAUDIO_STREAM_STATE_CLOSED);
    }
    return "Unrecognized";
}
