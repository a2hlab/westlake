/*
 * Android SoundPool JNI contract over the public OpenHarmony AVPlayer NDK.
 *
 * SoundPool is optimized around decoded, reusable short samples. OH AVPlayer
 * does not expose that cache directly, so this boundary preserves the Android
 * IDs, asynchronous load completion, concurrent-stream limit, priority
 * stealing, volume, pause/resume, loop, rate, stop, unload and release
 * contracts while creating an OH player for each playback instance.
 */
#include <jni.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct OH_AVPlayer OH_AVPlayer;
typedef struct OH_AVBuffer OH_AVBuffer;
typedef void (*OH_AVPlayerOnInfo)(OH_AVPlayer *, int, int32_t);
typedef void (*OH_AVPlayerOnError)(OH_AVPlayer *, int32_t, const char *);
typedef struct {
    OH_AVPlayerOnInfo onInfo;
    OH_AVPlayerOnError onError;
} AVPlayerCallback;

enum {
    OH_AV_OK = 0,
    OH_AV_INFO_EOS = 3,
    OH_AV_INFO_STATE_CHANGE = 4,
    OH_AV_STATE_PREPARED = 2,
    OH_AV_SEEK_PREVIOUS_SYNC = 1,
    OH_AUDIO_USAGE_MUSIC = 1,
    OH_AUDIO_USAGE_VOICE_COMMUNICATION = 2,
    OH_AUDIO_USAGE_NOTIFICATION = 7,
    OH_AUDIO_USAGE_MOVIE = 10,
    OH_AUDIO_USAGE_GAME = 11,
};

typedef OH_AVPlayer *(*FnCreate)(void);
typedef int32_t (*FnPlayer)(OH_AVPlayer *);
typedef int32_t (*OH_AVDataSourceReadAt)(OH_AVBuffer *, int32_t, int64_t, void *);
typedef struct {
    int64_t size;
    OH_AVDataSourceReadAt readAt;
} OH_AVDataSourceExt;
typedef int32_t (*FnSetDataSource)(OH_AVPlayer *, OH_AVDataSourceExt *, void *);
typedef uint8_t *(*FnBufferGetAddr)(OH_AVBuffer *);
typedef int32_t (*FnBufferGetCapacity)(OH_AVBuffer *);
typedef int32_t (*FnSetCallback)(OH_AVPlayer *, AVPlayerCallback);
typedef int32_t (*FnSetVolume)(OH_AVPlayer *, float, float);
typedef int32_t (*FnSetBool)(OH_AVPlayer *, _Bool);
typedef int32_t (*FnSetRate)(OH_AVPlayer *, float);
typedef int32_t (*FnSetUsage)(OH_AVPlayer *, int);
typedef int32_t (*FnSeek)(OH_AVPlayer *, int32_t, int);

static struct {
    void *handle;
    FnCreate create;
    FnPlayer prepare;
    FnPlayer play;
    FnPlayer pause;
    FnPlayer stop;
    FnPlayer release;
    FnPlayer release_sync;
    FnSetDataSource set_data_source;
    FnBufferGetAddr buffer_get_addr;
    FnBufferGetCapacity buffer_get_capacity;
    FnSetCallback set_callback;
    FnSetVolume set_volume;
    FnSetBool set_looping;
    FnSetRate set_rate;
    FnSetUsage set_usage;
    FnSeek seek;
} g_backend;

typedef enum BackendOperation {
    BACKEND_CREATE,
    BACKEND_PREPARE,
    BACKEND_PLAY,
    BACKEND_PAUSE,
    BACKEND_STOP,
    BACKEND_RELEASE,
    BACKEND_RELEASE_SYNC,
    BACKEND_SET_DATA_SOURCE,
    BACKEND_SET_CALLBACK,
    BACKEND_SET_VOLUME,
    BACKEND_SET_LOOPING,
    BACKEND_SET_RATE,
    BACKEND_SET_USAGE,
    BACKEND_SEEK,
} BackendOperation;

typedef struct BackendRequest {
    struct BackendRequest *next;
    BackendOperation operation;
    OH_AVPlayer *player;
    OH_AVPlayer *created;
    AVPlayerCallback callback;
    OH_AVDataSourceExt *data_source;
    void *user_data;
    int int_value;
    int64_t offset;
    float first_float;
    float second_float;
    int32_t result;
    int done;
} BackendRequest;

/*
 * Android libbinder and OH IPC both talk to the board binder driver, but each
 * owns per-thread command state. Calling them in succession from an ART/JNI
 * thread makes the second stack consume the first stack's driver protocol.
 * Keep every OH player operation on one dedicated thread; callers only wait
 * on process-local condition variables.
 */
static pthread_mutex_t g_backend_queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_backend_queue_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_backend_request_done = PTHREAD_COND_INITIALIZER;
static BackendRequest *g_backend_queue_head;
static BackendRequest *g_backend_queue_tail;
static pthread_t g_backend_thread;
static int g_backend_thread_started;

static void execute_backend_request(BackendRequest *request)
{
    switch (request->operation) {
        case BACKEND_CREATE:
            request->created = g_backend.create();
            request->result = request->created != NULL ? OH_AV_OK : -1;
            break;
        case BACKEND_PREPARE:
            request->result = g_backend.prepare(request->player);
            break;
        case BACKEND_PLAY:
            request->result = g_backend.play(request->player);
            break;
        case BACKEND_PAUSE:
            request->result = g_backend.pause(request->player);
            break;
        case BACKEND_STOP:
            request->result = g_backend.stop(request->player);
            break;
        case BACKEND_RELEASE:
            request->result = g_backend.release(request->player);
            break;
        case BACKEND_RELEASE_SYNC:
            request->result = g_backend.release_sync(request->player);
            break;
        case BACKEND_SET_DATA_SOURCE:
            request->result = g_backend.set_data_source(request->player,
                    request->data_source, request->user_data);
            break;
        case BACKEND_SET_CALLBACK:
            request->result = g_backend.set_callback(request->player, request->callback);
            break;
        case BACKEND_SET_VOLUME:
            request->result = g_backend.set_volume(request->player,
                    request->first_float, request->second_float);
            break;
        case BACKEND_SET_LOOPING:
            request->result = g_backend.set_looping(request->player,
                    request->int_value != 0);
            break;
        case BACKEND_SET_RATE:
            request->result = g_backend.set_rate != NULL
                    ? g_backend.set_rate(request->player, request->first_float) : OH_AV_OK;
            break;
        case BACKEND_SET_USAGE:
            request->result = g_backend.set_usage(request->player, request->int_value);
            break;
        case BACKEND_SEEK:
            request->result = g_backend.seek(request->player,
                    request->int_value, (int)request->offset);
            break;
    }
}

static void *backend_thread_main(void *unused)
{
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&g_backend_queue_lock);
        while (g_backend_queue_head == NULL) {
            pthread_cond_wait(&g_backend_queue_ready, &g_backend_queue_lock);
        }
        BackendRequest *request = g_backend_queue_head;
        g_backend_queue_head = request->next;
        if (g_backend_queue_head == NULL) g_backend_queue_tail = NULL;
        pthread_mutex_unlock(&g_backend_queue_lock);
        execute_backend_request(request);
        pthread_mutex_lock(&g_backend_queue_lock);
        request->done = 1;
        pthread_cond_broadcast(&g_backend_request_done);
        pthread_mutex_unlock(&g_backend_queue_lock);
    }
    return NULL;
}

static int ensure_backend_thread(void)
{
    pthread_mutex_lock(&g_backend_queue_lock);
    if (!g_backend_thread_started) {
        if (pthread_create(&g_backend_thread, NULL, backend_thread_main, NULL) != 0) {
            pthread_mutex_unlock(&g_backend_queue_lock);
            return 0;
        }
        (void)pthread_detach(g_backend_thread);
        g_backend_thread_started = 1;
    }
    pthread_mutex_unlock(&g_backend_queue_lock);
    return 1;
}

static int32_t backend_submit(BackendRequest *request)
{
    if (!ensure_backend_thread()) return -1;
    if (pthread_equal(pthread_self(), g_backend_thread)) {
        execute_backend_request(request);
        return request->result;
    }
    pthread_mutex_lock(&g_backend_queue_lock);
    request->next = NULL;
    request->done = 0;
    if (g_backend_queue_tail != NULL) g_backend_queue_tail->next = request;
    else g_backend_queue_head = request;
    g_backend_queue_tail = request;
    pthread_cond_signal(&g_backend_queue_ready);
    while (!request->done) {
        pthread_cond_wait(&g_backend_request_done, &g_backend_queue_lock);
    }
    pthread_mutex_unlock(&g_backend_queue_lock);
    return request->result;
}

static OH_AVPlayer *backend_create(void)
{
    BackendRequest request = {.operation = BACKEND_CREATE};
    (void)backend_submit(&request);
    return request.created;
}

static int32_t backend_player(BackendOperation operation, OH_AVPlayer *player)
{
    BackendRequest request = {.operation = operation, .player = player};
    return backend_submit(&request);
}

static int32_t backend_set_data_source(OH_AVPlayer *player,
        OH_AVDataSourceExt *data_source, void *user_data)
{
    BackendRequest request = {.operation = BACKEND_SET_DATA_SOURCE, .player = player,
            .data_source = data_source, .user_data = user_data};
    return backend_submit(&request);
}

static int32_t backend_set_callback(OH_AVPlayer *player, AVPlayerCallback callback)
{
    BackendRequest request = {.operation = BACKEND_SET_CALLBACK, .player = player,
            .callback = callback};
    return backend_submit(&request);
}

static int32_t backend_set_volume(OH_AVPlayer *player, float left, float right)
{
    BackendRequest request = {.operation = BACKEND_SET_VOLUME, .player = player,
            .first_float = left, .second_float = right};
    return backend_submit(&request);
}

static int32_t backend_set_looping(OH_AVPlayer *player, int looping)
{
    BackendRequest request = {.operation = BACKEND_SET_LOOPING, .player = player,
            .int_value = looping};
    return backend_submit(&request);
}

static int32_t backend_set_rate(OH_AVPlayer *player, float rate)
{
    BackendRequest request = {.operation = BACKEND_SET_RATE, .player = player,
            .first_float = rate};
    return backend_submit(&request);
}

static int32_t backend_set_usage(OH_AVPlayer *player, int usage)
{
    BackendRequest request = {.operation = BACKEND_SET_USAGE, .player = player,
            .int_value = usage};
    return backend_submit(&request);
}

static int32_t backend_seek(OH_AVPlayer *player, int position, int mode)
{
    BackendRequest request = {.operation = BACKEND_SEEK, .player = player,
            .int_value = position, .offset = mode};
    return backend_submit(&request);
}

typedef struct SourceWindow {
    OH_AVDataSourceExt descriptor;
    int fd;
    int64_t offset;
    int64_t length;
    int64_t position;
    pthread_mutex_t lock;
    int initialized;
} SourceWindow;

static int32_t source_window_read(OH_AVBuffer *buffer, int32_t requested,
        int64_t position, void *user_data)
{
    SourceWindow *source = (SourceWindow *)user_data;
    if (source == NULL || buffer == NULL || requested <= 0 ||
            g_backend.buffer_get_addr == NULL || g_backend.buffer_get_capacity == NULL) {
        return -2;
    }
    uint8_t *destination = g_backend.buffer_get_addr(buffer);
    int32_t capacity = g_backend.buffer_get_capacity(buffer);
    if (destination == NULL || capacity <= 0) return -2;
    if (requested > capacity) requested = capacity;

    pthread_mutex_lock(&source->lock);
    int64_t relative = position >= 0 ? position : source->position;
    if (relative < 0 || relative >= source->length) {
        pthread_mutex_unlock(&source->lock);
        return relative >= source->length ? -1 : -2;
    }
    int64_t remaining = source->length - relative;
    if ((int64_t)requested > remaining) requested = (int32_t)remaining;
    ssize_t count;
    do {
        count = pread(source->fd, destination, (size_t)requested,
                (off_t)(source->offset + relative));
    } while (count < 0 && errno == EINTR);
    if (count > 0) source->position = relative + count;
    pthread_mutex_unlock(&source->lock);
    if (count > 0) return (int32_t)count;
    return count == 0 ? -1 : -2;
}

static int source_window_init(SourceWindow *source, int fd, int64_t offset, int64_t length)
{
    memset(source, 0, sizeof(*source));
    source->fd = fd;
    source->offset = offset;
    source->length = length;
    source->descriptor.size = length;
    source->descriptor.readAt = source_window_read;
    if (pthread_mutex_init(&source->lock, NULL) != 0) {
        source->fd = -1;
        return 0;
    }
    source->initialized = 1;
    return 1;
}

static void source_window_destroy(SourceWindow *source)
{
    if (source == NULL || !source->initialized) return;
    if (source->fd >= 0) close(source->fd);
    source->fd = -1;
    source->initialized = 0;
    (void)pthread_mutex_destroy(&source->lock);
}

typedef struct SoundPoolContext SoundPoolContext;

typedef struct Sample {
    struct Sample *next;
    int id;
    int fd;
    int64_t offset;
    int64_t length;
    int state; /* 0 loading, 1 loaded, -1 failed */
    OH_AVPlayer *validator;
    SourceWindow validator_source;
} Sample;

typedef struct Stream {
    struct Stream *next;
    int id;
    int sample_id;
    int priority;
    int loop;
    int loops_remaining;
    float left;
    float right;
    float rate;
    int active;
    int prepared;
    int paused;
    int auto_paused;
    OH_AVPlayer *player;
    SourceWindow source;
} Stream;

struct SoundPoolContext {
    struct SoundPoolContext *next;
    jobject java_object;
    int max_streams;
    int next_sample_id;
    int next_stream_id;
    int muted;
    int usage;
    int released;
    int callbacks_busy;
    Sample *samples;
    Stream *streams;
};

static JavaVM *g_vm;
static jfieldID g_native_context;
static jfieldID g_fd_descriptor;
static jmethodID g_post_event;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_callbacks_idle = PTHREAD_COND_INITIALIZER;
static SoundPoolContext *g_contexts;

static void soundpool_log(const char *event, int first, int second)
{
    char line[192];
    int length = snprintf(line, sizeof(line),
            "[SOURCE-SOUNDPOOL] %s first=%d second=%d\n", event, first, second);
    if (length > 0) {
        size_t count = (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1;
        (void)write(STDERR_FILENO, line, count);
    }
}

static int resolve_required(void **target, const char *name)
{
    *target = dlsym(g_backend.handle, name);
    if (*target != NULL) {
        return 1;
    }
    const char *error = dlerror();
    char line[256];
    int length = snprintf(line, sizeof(line),
            "[SOURCE-SOUNDPOOL] missing OH backend symbol %s: %s\n",
            name, error != NULL ? error : "unknown");
    if (length > 0) {
        size_t count = (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1;
        (void)write(STDERR_FILENO, line, count);
    }
    return 0;
}

static int load_backend(void)
{
    if (g_backend.handle != NULL) {
        return 1;
    }
    g_backend.handle = dlopen("libavplayer.so", RTLD_NOW | RTLD_LOCAL);
    if (g_backend.handle == NULL) {
        const char *error = dlerror();
        if (error != NULL) {
            (void)write(STDERR_FILENO, error, strlen(error));
            (void)write(STDERR_FILENO, "\n", 1);
        }
        return 0;
    }
#define RESOLVE(field, symbol) \
    do { if (!resolve_required((void **)&g_backend.field, symbol)) goto fail; } while (0)
    RESOLVE(create, "OH_AVPlayer_Create");
    RESOLVE(prepare, "OH_AVPlayer_Prepare");
    RESOLVE(play, "OH_AVPlayer_Play");
    RESOLVE(pause, "OH_AVPlayer_Pause");
    RESOLVE(stop, "OH_AVPlayer_Stop");
    RESOLVE(release, "OH_AVPlayer_Release");
    RESOLVE(release_sync, "OH_AVPlayer_ReleaseSync");
    RESOLVE(set_data_source, "OH_AVPlayer_SetDataSource");
    RESOLVE(buffer_get_addr, "OH_AVBuffer_GetAddr");
    RESOLVE(buffer_get_capacity, "OH_AVBuffer_GetCapacity");
    RESOLVE(set_callback, "OH_AVPlayer_SetPlayerCallback");
    RESOLVE(set_volume, "OH_AVPlayer_SetVolume");
    RESOLVE(set_looping, "OH_AVPlayer_SetLooping");
    RESOLVE(set_usage, "OH_AVPlayer_SetAudioRendererInfo");
    RESOLVE(seek, "OH_AVPlayer_Seek");
#undef RESOLVE
    g_backend.set_rate = (FnSetRate)dlsym(g_backend.handle, "OH_AVPlayer_SetPlaybackRate");
    soundpool_log("OH AVPlayer backend ready", g_backend.set_rate != NULL, 0);
    return 1;
fail:
    dlclose(g_backend.handle);
    memset(&g_backend, 0, sizeof(g_backend));
    return 0;
}

static int android_usage_to_oh(int usage)
{
    switch (usage) {
        case 2:
        case 3:
            return OH_AUDIO_USAGE_VOICE_COMMUNICATION;
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 13:
            return OH_AUDIO_USAGE_NOTIFICATION;
        case 14:
            return OH_AUDIO_USAGE_GAME;
        case 16:
            return OH_AUDIO_USAGE_MOVIE;
        default:
            return OH_AUDIO_USAGE_MUSIC;
    }
}

static SoundPoolContext *get_context(JNIEnv *env, jobject object)
{
    return (SoundPoolContext *)(intptr_t)(*env)->GetLongField(env, object, g_native_context);
}

static Sample *find_sample_locked(SoundPoolContext *context, int id)
{
    Sample *sample;
    for (sample = context->samples; sample != NULL; sample = sample->next) {
        if (sample->id == id) return sample;
    }
    return NULL;
}

static Stream *find_stream_locked(SoundPoolContext *context, int id)
{
    Stream *stream;
    for (stream = context->streams; stream != NULL; stream = stream->next) {
        if (stream->id == id) return stream;
    }
    return NULL;
}

static SoundPoolContext *find_player_locked(OH_AVPlayer *player, Sample **sample_out,
        Stream **stream_out)
{
    SoundPoolContext *context;
    *sample_out = NULL;
    *stream_out = NULL;
    for (context = g_contexts; context != NULL; context = context->next) {
        Sample *sample;
        Stream *stream;
        for (sample = context->samples; sample != NULL; sample = sample->next) {
            if (sample->validator == player) {
                *sample_out = sample;
                ++context->callbacks_busy;
                return context;
            }
        }
        for (stream = context->streams; stream != NULL; stream = stream->next) {
            if (stream->player == player) {
                *stream_out = stream;
                ++context->callbacks_busy;
                return context;
            }
        }
    }
    return NULL;
}

static void finish_callback(SoundPoolContext *context)
{
    pthread_mutex_lock(&g_lock);
    --context->callbacks_busy;
    if (context->callbacks_busy == 0) {
        pthread_cond_broadcast(&g_callbacks_idle);
    }
    pthread_mutex_unlock(&g_lock);
}

static JNIEnv *callback_env(int *attached)
{
    JNIEnv *env = NULL;
    *attached = 0;
    if ((*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) {
        return NULL;
    }
    *attached = 1;
    return env;
}

static void release_async(OH_AVPlayer *player)
{
    if (player != NULL) (void)backend_player(BACKEND_RELEASE, player);
}

static void post_load_event(SoundPoolContext *context, int sample_id, int status)
{
    int attached;
    JNIEnv *env = callback_env(&attached);
    if (env == NULL) return;
    jobject object = (*env)->NewLocalRef(env, context->java_object);
    if (object != NULL) {
        (*env)->CallVoidMethod(env, object, g_post_event, 1, sample_id, status, NULL);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, object);
    }
    if (attached) {
        (void)(*g_vm)->DetachCurrentThread(g_vm);
    }
}

static void release_sync(OH_AVPlayer *player)
{
    if (player == NULL) return;
    (void)backend_player(BACKEND_STOP, player);
    (void)backend_player(BACKEND_RELEASE_SYNC, player);
}

static void wait_for_callbacks_locked(SoundPoolContext *context)
{
    while (context->callbacks_busy != 0) {
        pthread_cond_wait(&g_callbacks_idle, &g_lock);
    }
}

static void on_player_info(OH_AVPlayer *player, int type, int32_t extra)
{
    Sample *sample;
    Stream *stream;
    SoundPoolContext *context;
    pthread_mutex_lock(&g_lock);
    context = find_player_locked(player, &sample, &stream);
    if (context == NULL) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    if (sample != NULL && type == OH_AV_INFO_STATE_CHANGE && extra == OH_AV_STATE_PREPARED) {
        int id = sample->id;
        sample->state = 1;
        pthread_mutex_unlock(&g_lock);
        soundpool_log("sample loaded", id, 0);
        post_load_event(context, id, 0);
        finish_callback(context);
        return;
    }
    if (stream != NULL && type == OH_AV_INFO_STATE_CHANGE && extra == OH_AV_STATE_PREPARED) {
        float left = context->muted ? 0.0f : stream->left;
        float right = context->muted ? 0.0f : stream->right;
        float rate = stream->rate;
        int infinite = stream->loop < 0;
        int id = stream->id;
        stream->prepared = 1;
        pthread_mutex_unlock(&g_lock);
        int32_t volume_result = backend_set_volume(player, left, right);
        int32_t loop_result = backend_set_looping(player, infinite != 0);
        int32_t rate_result = OH_AV_OK;
        if (g_backend.set_rate != NULL) rate_result = backend_set_rate(player, rate);
        int32_t play_result = backend_player(BACKEND_PLAY, player);
        pthread_mutex_lock(&g_lock);
        if (stream->player == player && play_result != OH_AV_OK) stream->active = 0;
        pthread_mutex_unlock(&g_lock);
        soundpool_log("stream prepared/play", id,
                play_result != OH_AV_OK ? play_result :
                (volume_result != OH_AV_OK ? volume_result :
                (loop_result != OH_AV_OK ? loop_result : rate_result)));
        finish_callback(context);
        return;
    }
    if (stream != NULL && type == OH_AV_INFO_EOS) {
        int replay = stream->loop < 0 || stream->loops_remaining > 0;
        int id = stream->id;
        if (stream->loops_remaining > 0) --stream->loops_remaining;
        if (!replay) {
            stream->active = 0;
            stream->prepared = 0;
            stream->player = NULL;
        }
        pthread_mutex_unlock(&g_lock);
        if (replay) {
            int32_t seek_result = backend_seek(player, 0, OH_AV_SEEK_PREVIOUS_SYNC);
            int32_t play_result = seek_result == OH_AV_OK
                    ? backend_player(BACKEND_PLAY, player) : seek_result;
            soundpool_log("stream loop", id, play_result);
        } else {
            release_async(player);
            soundpool_log("stream complete", id, 0);
        }
        finish_callback(context);
        return;
    }
    pthread_mutex_unlock(&g_lock);
    finish_callback(context);
}

static void on_player_error(OH_AVPlayer *player, int32_t error_code, const char *error_message)
{
    Sample *sample;
    Stream *stream;
    SoundPoolContext *context;
    (void)error_message;
    pthread_mutex_lock(&g_lock);
    context = find_player_locked(player, &sample, &stream);
    if (context == NULL) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    int sample_id = 0;
    int stream_id = 0;
    if (sample != NULL) {
        sample_id = sample->id;
        sample->state = -1;
        sample->validator = NULL;
    }
    if (stream != NULL) {
        stream_id = stream->id;
        stream->active = 0;
        stream->prepared = 0;
        stream->player = NULL;
    }
    pthread_mutex_unlock(&g_lock);
    release_async(player);
    if (sample_id != 0) post_load_event(context, sample_id, error_code != 0 ? error_code : -1);
    soundpool_log(sample_id != 0 ? "sample error" : "stream error",
            sample_id != 0 ? sample_id : stream_id, error_code);
    finish_callback(context);
}

static AVPlayerCallback player_callbacks(void)
{
    AVPlayerCallback callbacks;
    callbacks.onInfo = on_player_info;
    callbacks.onError = on_player_error;
    return callbacks;
}

static int configure_source(OH_AVPlayer *player, SourceWindow *source, int usage)
{
    int32_t callback_result = backend_set_callback(player, player_callbacks());
    int32_t source_result = callback_result == OH_AV_OK
            ? backend_set_data_source(player, &source->descriptor, source) : -1;
    int32_t usage_result = source_result == OH_AV_OK
            ? backend_set_usage(player, usage) : -1;
    soundpool_log("configure callback/source", callback_result, source_result);
    soundpool_log("configure usage", usage_result, usage);
    return callback_result == OH_AV_OK && source_result == OH_AV_OK
            && usage_result == OH_AV_OK;
}

static jint native_setup(JNIEnv *env, jobject object, jint max_streams,
        jobject attributes, jstring package_name)
{
    (void)package_name;
    if (max_streams <= 0 || attributes == NULL || !load_backend()) return -1;
    SoundPoolContext *old = get_context(env, object);
    if (old != NULL) return -1;
    SoundPoolContext *context = (SoundPoolContext *)calloc(1, sizeof(*context));
    if (context == NULL) return -1;
    context->java_object = (*env)->NewGlobalRef(env, object);
    if (context->java_object == NULL) {
        free(context);
        return -1;
    }
    context->max_streams = max_streams;
    context->next_sample_id = 1;
    context->next_stream_id = 1;
    context->usage = OH_AUDIO_USAGE_MUSIC;
    jclass attributes_class = (*env)->GetObjectClass(env, attributes);
    if (attributes_class != NULL) {
        jfieldID usage_field = (*env)->GetFieldID(env, attributes_class, "mUsage", "I");
        if (usage_field != NULL && !(*env)->ExceptionCheck(env)) {
            context->usage = android_usage_to_oh(
                    (*env)->GetIntField(env, attributes, usage_field));
        } else if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, attributes_class);
    }
    pthread_mutex_lock(&g_lock);
    context->next = g_contexts;
    g_contexts = context;
    pthread_mutex_unlock(&g_lock);
    (*env)->SetLongField(env, object, g_native_context, (jlong)(intptr_t)context);
    soundpool_log("native setup", max_streams, context->usage);
    return 0;
}

static jint native_load(JNIEnv *env, jobject object, jobject descriptor,
        jlong offset, jlong length, jint priority)
{
    (void)priority;
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL || descriptor == NULL || offset < 0 || length <= 0) return 0;
    int source_fd = (*env)->GetIntField(env, descriptor, g_fd_descriptor);
    struct stat metadata;
    if (source_fd < 0 || fstat(source_fd, &metadata) != 0) return 0;
    if (S_ISREG(metadata.st_mode) && ((uint64_t)offset > (uint64_t)metadata.st_size ||
            (uint64_t)length > (uint64_t)metadata.st_size - (uint64_t)offset)) return 0;
    int retained_fd = dup(source_fd);
    int validation_fd = dup(source_fd);
    if (retained_fd < 0 || validation_fd < 0) {
        if (retained_fd >= 0) close(retained_fd);
        if (validation_fd >= 0) close(validation_fd);
        return 0;
    }
    Sample *sample = (Sample *)calloc(1, sizeof(*sample));
    OH_AVPlayer *player = backend_create();
    if (sample == NULL || player == NULL) {
        close(retained_fd);
        close(validation_fd);
        free(sample);
        release_sync(player);
        return 0;
    }
    if (!source_window_init(&sample->validator_source, validation_fd, offset, length)) {
        close(retained_fd);
        close(validation_fd);
        free(sample);
        release_sync(player);
        return 0;
    }
    if (!configure_source(player, &sample->validator_source, context->usage)) {
        close(retained_fd);
        release_sync(player);
        source_window_destroy(&sample->validator_source);
        free(sample);
        return 0;
    }
    sample->fd = retained_fd;
    sample->offset = offset;
    sample->length = length;
    sample->validator = player;
    pthread_mutex_lock(&g_lock);
    if (context->released) {
        pthread_mutex_unlock(&g_lock);
        close(retained_fd);
        source_window_destroy(&sample->validator_source);
        free(sample);
        release_sync(player);
        return 0;
    }
    sample->id = context->next_sample_id++;
    if (context->next_sample_id <= 0) context->next_sample_id = 1;
    sample->next = context->samples;
    context->samples = sample;
    pthread_mutex_unlock(&g_lock);
    if (backend_player(BACKEND_PREPARE, player) != OH_AV_OK) {
        pthread_mutex_lock(&g_lock);
        Sample **link = &context->samples;
        while (*link != NULL && *link != sample) link = &(*link)->next;
        if (*link == sample) *link = sample->next;
        pthread_mutex_unlock(&g_lock);
        release_sync(player);
        source_window_destroy(&sample->validator_source);
        close(sample->fd);
        free(sample);
        return 0;
    }
    soundpool_log("load queued", sample->id, (int)(length > INT32_MAX ? INT32_MAX : length));
    return sample->id;
}

static jboolean native_unload(JNIEnv *env, jobject object, jint sample_id)
{
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL) return JNI_FALSE;
    pthread_mutex_lock(&g_lock);
    Sample **link = &context->samples;
    while (*link != NULL && (*link)->id != sample_id) link = &(*link)->next;
    if (*link == NULL) {
        pthread_mutex_unlock(&g_lock);
        return JNI_FALSE;
    }
    Sample *sample = *link;
    *link = sample->next;
    OH_AVPlayer *validator = sample->validator;
    sample->validator = NULL;
    wait_for_callbacks_locked(context);
    pthread_mutex_unlock(&g_lock);
    release_sync(validator);
    source_window_destroy(&sample->validator_source);
    close(sample->fd);
    free(sample);
    soundpool_log("unload", sample_id, 0);
    return JNI_TRUE;
}

static int active_stream_count_locked(SoundPoolContext *context)
{
    int count = 0;
    Stream *stream;
    for (stream = context->streams; stream != NULL; stream = stream->next) {
        if (stream->active && stream->player != NULL) ++count;
    }
    return count;
}

static Stream *steal_candidate_locked(SoundPoolContext *context)
{
    Stream *candidate = NULL;
    Stream *stream;
    for (stream = context->streams; stream != NULL; stream = stream->next) {
        if (!stream->active || stream->player == NULL) continue;
        if (candidate == NULL || stream->priority < candidate->priority ||
                (stream->priority == candidate->priority && stream->id < candidate->id)) {
            candidate = stream;
        }
    }
    return candidate;
}

static jint native_play(JNIEnv *env, jobject object, jint sample_id, jfloat left,
        jfloat right, jint priority, jint loop, jfloat rate, jint player_id)
{
    (void)player_id;
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL || rate < 0.5f || rate > 2.0f) return 0;
    int source_fd = -1;
    int64_t offset = 0;
    int64_t length = 0;
    OH_AVPlayer *victim_player = NULL;
    SourceWindow *victim_source = NULL;
    pthread_mutex_lock(&g_lock);
    Sample *sample = find_sample_locked(context, sample_id);
    if (context->released || sample == NULL || sample->state != 1) {
        pthread_mutex_unlock(&g_lock);
        return 0;
    }
    if (active_stream_count_locked(context) >= context->max_streams) {
        Stream *victim = steal_candidate_locked(context);
        if (victim == NULL || victim->priority > priority) {
            pthread_mutex_unlock(&g_lock);
            return 0;
        }
        victim_player = victim->player;
        victim->player = NULL;
        victim->active = 0;
        victim_source = &victim->source;
        wait_for_callbacks_locked(context);
    }
    source_fd = dup(sample->fd);
    offset = sample->offset;
    length = sample->length;
    pthread_mutex_unlock(&g_lock);
    release_sync(victim_player);
    source_window_destroy(victim_source);
    if (source_fd < 0) return 0;

    Stream *stream = (Stream *)calloc(1, sizeof(*stream));
    OH_AVPlayer *player = backend_create();
    if (stream == NULL || player == NULL) {
        close(source_fd);
        free(stream);
        release_sync(player);
        return 0;
    }
    if (!source_window_init(&stream->source, source_fd, offset, length)) {
        close(source_fd);
        free(stream);
        release_sync(player);
        return 0;
    }
    if (!configure_source(player, &stream->source, context->usage)) {
        release_sync(player);
        source_window_destroy(&stream->source);
        free(stream);
        return 0;
    }
    stream->sample_id = sample_id;
    stream->priority = priority;
    stream->loop = loop;
    stream->loops_remaining = loop > 0 ? loop : 0;
    stream->left = left < 0.0f ? 0.0f : (left > 1.0f ? 1.0f : left);
    stream->right = right < 0.0f ? 0.0f : (right > 1.0f ? 1.0f : right);
    stream->rate = rate;
    stream->active = 1;
    stream->player = player;
    pthread_mutex_lock(&g_lock);
    if (context->released) {
        pthread_mutex_unlock(&g_lock);
        release_sync(player);
        source_window_destroy(&stream->source);
        free(stream);
        return 0;
    }
    stream->id = context->next_stream_id++;
    if (context->next_stream_id <= 0) context->next_stream_id = 1;
    stream->next = context->streams;
    context->streams = stream;
    pthread_mutex_unlock(&g_lock);
    if (backend_player(BACKEND_PREPARE, player) != OH_AV_OK) {
        pthread_mutex_lock(&g_lock);
        stream->active = 0;
        stream->player = NULL;
        pthread_mutex_unlock(&g_lock);
        release_sync(player);
        source_window_destroy(&stream->source);
        return 0;
    }
    soundpool_log("play queued", stream->id, sample_id);
    return stream->id;
}

static void native_pause(JNIEnv *env, jobject object, jint stream_id)
{
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL) return;
    pthread_mutex_lock(&g_lock);
    Stream *stream = find_stream_locked(context, stream_id);
    OH_AVPlayer *player = stream != NULL ? stream->player : NULL;
    if (stream != NULL && player != NULL) stream->paused = 1;
    pthread_mutex_unlock(&g_lock);
    if (player != NULL) (void)backend_player(BACKEND_PAUSE, player);
}

static void native_resume(JNIEnv *env, jobject object, jint stream_id)
{
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL) return;
    pthread_mutex_lock(&g_lock);
    Stream *stream = find_stream_locked(context, stream_id);
    OH_AVPlayer *player = stream != NULL ? stream->player : NULL;
    if (stream != NULL && player != NULL) {
        stream->paused = 0;
        stream->auto_paused = 0;
    }
    pthread_mutex_unlock(&g_lock);
    if (player != NULL) (void)backend_player(BACKEND_PLAY, player);
}

static void native_auto_pause(JNIEnv *env, jobject object)
{
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL) return;
    pthread_mutex_lock(&g_lock);
    size_t count = 0;
    Stream *stream;
    for (stream = context->streams; stream != NULL; stream = stream->next) {
        if (stream->active && stream->player != NULL && !stream->paused) ++count;
    }
    OH_AVPlayer **players = count != 0
            ? (OH_AVPlayer **)calloc(count, sizeof(*players)) : NULL;
    if (count != 0 && players == NULL) count = 0;
    size_t index = 0;
    for (stream = context->streams; stream != NULL && index < count; stream = stream->next) {
        if (stream->active && stream->player != NULL && !stream->paused) {
            stream->auto_paused = 1;
            stream->paused = 1;
            players[index++] = stream->player;
        }
    }
    if (index != 0) ++context->callbacks_busy;
    pthread_mutex_unlock(&g_lock);
    for (size_t i = 0; i < index; ++i) {
        (void)backend_player(BACKEND_PAUSE, players[i]);
    }
    free(players);
    if (index != 0) finish_callback(context);
}

static void native_auto_resume(JNIEnv *env, jobject object)
{
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL) return;
    pthread_mutex_lock(&g_lock);
    size_t count = 0;
    Stream *stream;
    for (stream = context->streams; stream != NULL; stream = stream->next) {
        if (stream->active && stream->player != NULL && stream->auto_paused) ++count;
    }
    OH_AVPlayer **players = count != 0
            ? (OH_AVPlayer **)calloc(count, sizeof(*players)) : NULL;
    if (count != 0 && players == NULL) count = 0;
    size_t index = 0;
    for (stream = context->streams; stream != NULL && index < count; stream = stream->next) {
        if (stream->active && stream->player != NULL && stream->auto_paused) {
            stream->auto_paused = 0;
            stream->paused = 0;
            players[index++] = stream->player;
        }
    }
    if (index != 0) ++context->callbacks_busy;
    pthread_mutex_unlock(&g_lock);
    for (size_t i = 0; i < index; ++i) {
        (void)backend_player(BACKEND_PLAY, players[i]);
    }
    free(players);
    if (index != 0) finish_callback(context);
}

static void native_stop(JNIEnv *env, jobject object, jint stream_id)
{
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL) return;
    pthread_mutex_lock(&g_lock);
    Stream *stream = find_stream_locked(context, stream_id);
    OH_AVPlayer *player = stream != NULL ? stream->player : NULL;
    if (stream != NULL) {
        stream->player = NULL;
        stream->active = 0;
        stream->prepared = 0;
    }
    wait_for_callbacks_locked(context);
    pthread_mutex_unlock(&g_lock);
    release_sync(player);
    if (stream != NULL) source_window_destroy(&stream->source);
    soundpool_log("stop", stream_id, 0);
}

static void native_set_volume(JNIEnv *env, jobject object, jint stream_id,
        jfloat left, jfloat right)
{
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL) return;
    pthread_mutex_lock(&g_lock);
    Stream *stream = find_stream_locked(context, stream_id);
    OH_AVPlayer *player = stream != NULL ? stream->player : NULL;
    if (stream != NULL) {
        stream->left = left < 0.0f ? 0.0f : (left > 1.0f ? 1.0f : left);
        stream->right = right < 0.0f ? 0.0f : (right > 1.0f ? 1.0f : right);
        left = context->muted ? 0.0f : stream->left;
        right = context->muted ? 0.0f : stream->right;
    }
    pthread_mutex_unlock(&g_lock);
    if (player != NULL) (void)backend_set_volume(player, left, right);
}

static void native_mute(JNIEnv *env, jobject object, jboolean muted)
{
    typedef struct VolumeUpdate {
        OH_AVPlayer *player;
        float left;
        float right;
    } VolumeUpdate;
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL) return;
    pthread_mutex_lock(&g_lock);
    context->muted = muted == JNI_TRUE;
    size_t count = 0;
    Stream *stream;
    for (stream = context->streams; stream != NULL; stream = stream->next) {
        if (stream->player != NULL) ++count;
    }
    VolumeUpdate *updates = count != 0
            ? (VolumeUpdate *)calloc(count, sizeof(*updates)) : NULL;
    if (count != 0 && updates == NULL) count = 0;
    size_t index = 0;
    for (stream = context->streams; stream != NULL && index < count; stream = stream->next) {
        if (stream->player == NULL) continue;
        updates[index].player = stream->player;
        updates[index].left = context->muted ? 0.0f : stream->left;
        updates[index].right = context->muted ? 0.0f : stream->right;
        ++index;
    }
    if (index != 0) ++context->callbacks_busy;
    pthread_mutex_unlock(&g_lock);
    for (size_t i = 0; i < index; ++i) {
        (void)backend_set_volume(updates[i].player, updates[i].left, updates[i].right);
    }
    free(updates);
    if (index != 0) finish_callback(context);
}

static void native_set_priority(JNIEnv *env, jobject object, jint stream_id, jint priority)
{
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL) return;
    pthread_mutex_lock(&g_lock);
    Stream *stream = find_stream_locked(context, stream_id);
    if (stream != NULL) stream->priority = priority;
    pthread_mutex_unlock(&g_lock);
}

static void native_set_loop(JNIEnv *env, jobject object, jint stream_id, jint loop)
{
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL) return;
    pthread_mutex_lock(&g_lock);
    Stream *stream = find_stream_locked(context, stream_id);
    OH_AVPlayer *player = stream != NULL ? stream->player : NULL;
    if (stream != NULL) {
        stream->loop = loop;
        stream->loops_remaining = loop > 0 ? loop : 0;
    }
    pthread_mutex_unlock(&g_lock);
    if (player != NULL) (void)backend_set_looping(player, loop < 0);
}

static void native_set_rate(JNIEnv *env, jobject object, jint stream_id, jfloat rate)
{
    if (rate < 0.5f || rate > 2.0f) return;
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL) return;
    pthread_mutex_lock(&g_lock);
    Stream *stream = find_stream_locked(context, stream_id);
    OH_AVPlayer *player = stream != NULL ? stream->player : NULL;
    if (stream != NULL) stream->rate = rate;
    pthread_mutex_unlock(&g_lock);
    if (player != NULL && g_backend.set_rate != NULL) (void)backend_set_rate(player, rate);
}

static void native_release(JNIEnv *env, jobject object)
{
    SoundPoolContext *context = get_context(env, object);
    if (context == NULL) return;
    (*env)->SetLongField(env, object, g_native_context, 0);
    pthread_mutex_lock(&g_lock);
    context->released = 1;
    SoundPoolContext **link = &g_contexts;
    while (*link != NULL && *link != context) link = &(*link)->next;
    if (*link == context) *link = context->next;
    wait_for_callbacks_locked(context);
    pthread_mutex_unlock(&g_lock);

    Sample *sample = context->samples;
    while (sample != NULL) {
        Sample *next = sample->next;
        release_sync(sample->validator);
        source_window_destroy(&sample->validator_source);
        close(sample->fd);
        free(sample);
        sample = next;
    }
    Stream *stream = context->streams;
    while (stream != NULL) {
        Stream *next = stream->next;
        release_sync(stream->player);
        source_window_destroy(&stream->source);
        free(stream);
        stream = next;
    }
    (*env)->DeleteGlobalRef(env, context->java_object);
    free(context);
    soundpool_log("native release", 0, 0);
}

JNIEXPORT int JNICALL oh_soundpool_backend_probe(void)
{
    if (!load_backend()) return 0;
    OH_AVPlayer *player = backend_create();
    if (player == NULL) return 0;
    int32_t result = backend_player(BACKEND_RELEASE_SYNC, player);
    soundpool_log("backend probe", result, 0);
    return result == OH_AV_OK;
}

static const JNINativeMethod g_methods[] = {
    {"_load", "(Ljava/io/FileDescriptor;JJI)I", (void *)native_load},
    {"unload", "(I)Z", (void *)native_unload},
    {"_play", "(IFFIIFI)I", (void *)native_play},
    {"pause", "(I)V", (void *)native_pause},
    {"resume", "(I)V", (void *)native_resume},
    {"autoPause", "()V", (void *)native_auto_pause},
    {"autoResume", "()V", (void *)native_auto_resume},
    {"stop", "(I)V", (void *)native_stop},
    {"_setVolume", "(IFF)V", (void *)native_set_volume},
    {"_mute", "(Z)V", (void *)native_mute},
    {"setPriority", "(II)V", (void *)native_set_priority},
    {"setLoop", "(II)V", (void *)native_set_loop},
    {"setRate", "(IF)V", (void *)native_set_rate},
    {"native_setup", "(ILjava/lang/Object;Ljava/lang/String;)I", (void *)native_setup},
    {"native_release", "()V", (void *)native_release},
};

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK || env == NULL ||
            !load_backend()) return JNI_ERR;
    jclass soundpool = (*env)->FindClass(env, "android/media/SoundPool");
    if (soundpool == NULL) return JNI_ERR;
    g_native_context = (*env)->GetFieldID(env, soundpool, "mNativeContext", "J");
    g_post_event = (*env)->GetMethodID(env, soundpool, "postEventFromNative",
            "(IIILjava/lang/Object;)V");
    jclass descriptor = (*env)->FindClass(env, "java/io/FileDescriptor");
    if (descriptor == NULL) return JNI_ERR;
    g_fd_descriptor = (*env)->GetFieldID(env, descriptor, "descriptor", "I");
    if (g_native_context == NULL || g_post_event == NULL || g_fd_descriptor == NULL ||
            (*env)->RegisterNatives(env, soundpool, g_methods,
                    (jint)(sizeof(g_methods) / sizeof(g_methods[0]))) != JNI_OK) {
        return JNI_ERR;
    }
    g_vm = vm;
    soundpool_log("JNI registered", (int)(sizeof(g_methods) / sizeof(g_methods[0])), 0);
    return JNI_VERSION_1_6;
}
