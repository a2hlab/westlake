/*
 * Android NDK AChoreographer compatibility for the OHOS boundary.
 *
 * Chromium obtains these entry points with dlopen("libandroid.so") + dlsym.
 * Keep this implementation independent of Android framework internals: an
 * Android one-shot frame request is translated to OH_NativeVSync_RequestFrame,
 * and the opaque Android callback data is produced locally.
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct OH_NativeVSync OH_NativeVSync;
typedef void (*OH_NativeVSync_FrameCallback)(long long timestamp, void *data);

typedef OH_NativeVSync *(*OHCreateFn)(const char *, unsigned int);
typedef void (*OHDestroyFn)(OH_NativeVSync *);
typedef int (*OHRequestFrameFn)(OH_NativeVSync *,
                                OH_NativeVSync_FrameCallback, void *);
typedef int (*OHGetPeriodFn)(OH_NativeVSync *, long long *);

typedef struct {
    void *handle;
    OHCreateFn create;
    OHDestroyFn destroy;
    OHRequestFrameFn request_frame;
    OHGetPeriodFn get_period;
} OHVSyncApi;

static OHVSyncApi g_oh_vsync;
static pthread_once_t g_oh_vsync_once = PTHREAD_ONCE_INIT;

static void load_oh_vsync(void)
{
    const char *candidates[] = {
        "libnative_vsync.so",
        "/system/lib64/platformsdk/libnative_vsync.so",
        NULL,
    };
    for (size_t i = 0; candidates[i] != NULL && g_oh_vsync.handle == NULL; ++i) {
        g_oh_vsync.handle = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
    }
    if (g_oh_vsync.handle == NULL) {
        fprintf(stderr, "[WESTLAKE-CHOREO] libnative_vsync load failed: %s\n",
                dlerror());
        return;
    }

    g_oh_vsync.create = (OHCreateFn)dlsym(
            g_oh_vsync.handle, "OH_NativeVSync_Create");
    g_oh_vsync.destroy = (OHDestroyFn)dlsym(
            g_oh_vsync.handle, "OH_NativeVSync_Destroy");
    g_oh_vsync.request_frame = (OHRequestFrameFn)dlsym(
            g_oh_vsync.handle, "OH_NativeVSync_RequestFrame");
    g_oh_vsync.get_period = (OHGetPeriodFn)dlsym(
            g_oh_vsync.handle, "OH_NativeVSync_GetPeriod");
    if (g_oh_vsync.create == NULL || g_oh_vsync.destroy == NULL ||
            g_oh_vsync.request_frame == NULL || g_oh_vsync.get_period == NULL) {
        fprintf(stderr, "[WESTLAKE-CHOREO] incomplete OH NativeVSync API\n");
        g_oh_vsync.create = NULL;
        g_oh_vsync.destroy = NULL;
        g_oh_vsync.request_frame = NULL;
        g_oh_vsync.get_period = NULL;
    }
}

static int ensure_oh_vsync(void)
{
    pthread_once(&g_oh_vsync_once, load_oh_vsync);
    return g_oh_vsync.create != NULL && g_oh_vsync.request_frame != NULL;
}

typedef struct AChoreographer {
    OH_NativeVSync *vsync;
    int64_t period_nanos;
} AChoreographer;

typedef struct AChoreographerFrameCallbackData {
    int64_t frame_time_nanos;
    int64_t vsync_id;
    int64_t expected_presentation_time_nanos;
    int64_t deadline_nanos;
} AChoreographerFrameCallbackData;

typedef void (*AChoreographer_frameCallback)(long, void *);
typedef void (*AChoreographer_frameCallback64)(int64_t, void *);
typedef void (*AChoreographer_vsyncCallback)(
        const AChoreographerFrameCallbackData *, void *);
typedef void (*AChoreographer_extendedFrameCallback)(
        int64_t, int64_t, int64_t, void *);
typedef void (*AChoreographer_refreshRateCallback)(int64_t, void *);

static __thread AChoreographer *g_thread_choreographer;
static uint64_t g_choreographer_name_id;
static int64_t g_vsync_id;

static int64_t period_for(AChoreographer *choreographer)
{
    long long period = 0;
    if (choreographer != NULL && choreographer->vsync != NULL &&
            g_oh_vsync.get_period != NULL &&
            g_oh_vsync.get_period(choreographer->vsync, &period) == 0 &&
            period > 0) {
        choreographer->period_nanos = (int64_t)period;
    }
    return choreographer != NULL && choreographer->period_nanos > 0
            ? choreographer->period_nanos : INT64_C(16666666);
}

AChoreographer *AChoreographer_create(void)
{
    if (!ensure_oh_vsync()) {
        return NULL;
    }

    AChoreographer *choreographer =
            (AChoreographer *)calloc(1, sizeof(*choreographer));
    if (choreographer == NULL) {
        return NULL;
    }

    char name[32];
    const uint64_t id = __atomic_add_fetch(
            &g_choreographer_name_id, 1, __ATOMIC_RELAXED);
    const int length = snprintf(name, sizeof(name), "westlake_choreo_%llu",
                                (unsigned long long)id);
    choreographer->vsync = g_oh_vsync.create(
            name, length > 0 ? (unsigned int)length : 0);
    if (choreographer->vsync == NULL) {
        free(choreographer);
        return NULL;
    }
    choreographer->period_nanos = INT64_C(16666666);
    (void)period_for(choreographer);
    fprintf(stderr, "[WESTLAKE-CHOREO] create=%p period=%lld\n",
            (void *)choreographer,
            (long long)choreographer->period_nanos);
    return choreographer;
}

void AChoreographer_destroy(AChoreographer *choreographer)
{
    if (choreographer == NULL) {
        return;
    }
    if (g_thread_choreographer == choreographer) {
        g_thread_choreographer = NULL;
    }
    if (choreographer->vsync != NULL && g_oh_vsync.destroy != NULL) {
        g_oh_vsync.destroy(choreographer->vsync);
    }
    free(choreographer);
}

AChoreographer *AChoreographer_getInstance(void)
{
    if (g_thread_choreographer == NULL) {
        g_thread_choreographer = AChoreographer_create();
    }
    return g_thread_choreographer;
}

typedef enum {
    CALLBACK_VSYNC,
    CALLBACK_EXTENDED,
    CALLBACK_FRAME_LONG,
    CALLBACK_FRAME_64,
} CallbackKind;

typedef struct {
    CallbackKind kind;
    AChoreographer *choreographer;
    void *callback;
    void *user_data;
} FrameCallbackContext;

static void dispatch_frame(long long timestamp, void *data)
{
    FrameCallbackContext *context = (FrameCallbackContext *)data;
    if (context == NULL || context->callback == NULL) {
        free(context);
        return;
    }

    const int64_t frame_time = (int64_t)timestamp;
    const int64_t period = period_for(context->choreographer);
    const int64_t expected = frame_time + period;
    const int64_t vsync_id = __atomic_add_fetch(
            &g_vsync_id, 1, __ATOMIC_RELAXED);

    switch (context->kind) {
        case CALLBACK_VSYNC: {
            AChoreographerFrameCallbackData callback_data = {
                frame_time, vsync_id, expected, expected,
            };
            ((AChoreographer_vsyncCallback)context->callback)(
                    &callback_data, context->user_data);
            break;
        }
        case CALLBACK_EXTENDED:
            ((AChoreographer_extendedFrameCallback)context->callback)(
                    vsync_id, frame_time, expected, context->user_data);
            break;
        case CALLBACK_FRAME_LONG:
            ((AChoreographer_frameCallback)context->callback)(
                    (long)frame_time, context->user_data);
            break;
        case CALLBACK_FRAME_64:
            ((AChoreographer_frameCallback64)context->callback)(
                    frame_time, context->user_data);
            break;
    }
    free(context);
}

static int request_frame(AChoreographer *choreographer, CallbackKind kind,
                         void *callback, void *user_data)
{
    if (choreographer == NULL || choreographer->vsync == NULL ||
            callback == NULL || !ensure_oh_vsync()) {
        return -1;
    }
    FrameCallbackContext *context =
            (FrameCallbackContext *)calloc(1, sizeof(*context));
    if (context == NULL) {
        return -1;
    }
    context->kind = kind;
    context->choreographer = choreographer;
    context->callback = callback;
    context->user_data = user_data;
    const int result = g_oh_vsync.request_frame(
            choreographer->vsync, dispatch_frame, context);
    if (result != 0) {
        fprintf(stderr, "[WESTLAKE-CHOREO] request failed result=%d\n", result);
        free(context);
    }
    return result;
}

void AChoreographer_postVsyncCallback(AChoreographer *choreographer,
                                      AChoreographer_vsyncCallback callback,
                                      void *data)
{
    (void)request_frame(choreographer, CALLBACK_VSYNC, (void *)callback, data);
}

void AChoreographer_postExtendedFrameCallback(
        AChoreographer *choreographer,
        AChoreographer_extendedFrameCallback callback, void *data)
{
    (void)request_frame(choreographer, CALLBACK_EXTENDED, (void *)callback, data);
}

void AChoreographer_postFrameCallback(AChoreographer *choreographer,
                                      AChoreographer_frameCallback callback,
                                      void *data)
{
    (void)request_frame(choreographer, CALLBACK_FRAME_LONG, (void *)callback, data);
}

void AChoreographer_postFrameCallbackDelayed(
        AChoreographer *choreographer, AChoreographer_frameCallback callback,
        void *data, long delay_millis)
{
    /* OH NativeVSync is one-shot and has no delayed request. The next-vsync
     * contract is preferable to blocking Chromium's caller thread. */
    (void)delay_millis;
    AChoreographer_postFrameCallback(choreographer, callback, data);
}

void AChoreographer_postFrameCallback64(
        AChoreographer *choreographer, AChoreographer_frameCallback64 callback,
        void *data)
{
    (void)request_frame(choreographer, CALLBACK_FRAME_64, (void *)callback, data);
}

void AChoreographer_postFrameCallbackDelayed64(
        AChoreographer *choreographer, AChoreographer_frameCallback64 callback,
        void *data, uint32_t delay_millis)
{
    (void)delay_millis;
    AChoreographer_postFrameCallback64(choreographer, callback, data);
}

void AChoreographer_registerRefreshRateCallback(
        AChoreographer *choreographer,
        AChoreographer_refreshRateCallback callback, void *data)
{
    if (callback != NULL) {
        callback(period_for(choreographer), data);
    }
}

void AChoreographer_unregisterRefreshRateCallback(
        AChoreographer *choreographer,
        AChoreographer_refreshRateCallback callback, void *data)
{
    (void)choreographer;
    (void)callback;
    (void)data;
}

int AChoreographer_getFd(const AChoreographer *choreographer)
{
    (void)choreographer;
    return -1;
}

void AChoreographer_handlePendingEvents(AChoreographer *choreographer, void *data)
{
    (void)choreographer;
    (void)data;
}

int64_t AChoreographer_getFrameInterval(const AChoreographer *choreographer)
{
    return period_for((AChoreographer *)choreographer);
}

int64_t AChoreographerFrameCallbackData_getFrameTimeNanos(
        const AChoreographerFrameCallbackData *data)
{
    return data != NULL ? data->frame_time_nanos : 0;
}

size_t AChoreographerFrameCallbackData_getFrameTimelinesLength(
        const AChoreographerFrameCallbackData *data)
{
    return data != NULL ? 1 : 0;
}

size_t AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex(
        const AChoreographerFrameCallbackData *data)
{
    (void)data;
    return 0;
}

int64_t AChoreographerFrameCallbackData_getFrameTimelineVsyncId(
        const AChoreographerFrameCallbackData *data, size_t index)
{
    return data != NULL && index == 0 ? data->vsync_id : 0;
}

int64_t AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos(
        const AChoreographerFrameCallbackData *data, size_t index)
{
    return data != NULL && index == 0
            ? data->expected_presentation_time_nanos : 0;
}

int64_t AChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos(
        const AChoreographerFrameCallbackData *data, size_t index)
{
    return data != NULL && index == 0 ? data->deadline_nanos : 0;
}

int64_t AChoreographerFrameCallbackData_getVsyncId(
        const AChoreographerFrameCallbackData *data)
{
    return data != NULL ? data->vsync_id : 0;
}
