/*
 * Android 13 AChoreographer compatibility for the pinned WebView provider.
 *
 * Chromium loads these seven API-33 functions specifically from
 * dlopen("libandroid.so").  Westlake's AChoreographer object and scheduler
 * already live in liboh_hwui_shim.so.  The object is opaque, so it must not be
 * reinterpreted by a second implementation.  Forward through the owner DSO
 * instead; this keeps object creation, OH NativeVSync scheduling, callback
 * data, and its accessors in one ABI domain.
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

typedef struct AChoreographer AChoreographer;
typedef struct AChoreographerFrameCallbackData AChoreographerFrameCallbackData;
typedef void (*AChoreographer_vsyncCallback)(
        const AChoreographerFrameCallbackData *, void *);

typedef struct {
    void *handle;
    void (*post_vsync)(AChoreographer *, AChoreographer_vsyncCallback, void *);
    int64_t (*get_frame_time)(const AChoreographerFrameCallbackData *);
    size_t (*get_timelines_length)(const AChoreographerFrameCallbackData *);
    size_t (*get_preferred_index)(const AChoreographerFrameCallbackData *);
    int64_t (*get_timeline_vsync_id)(
            const AChoreographerFrameCallbackData *, size_t);
    int64_t (*get_expected_presentation)(
            const AChoreographerFrameCallbackData *, size_t);
    int64_t (*get_deadline)(const AChoreographerFrameCallbackData *, size_t);
    int ready;
} ChoreographerOwner;

static ChoreographerOwner g_owner;
static pthread_once_t g_owner_once = PTHREAD_ONCE_INIT;
static unsigned int g_post_count;

static void load_owner(void)
{
    g_owner.handle = dlopen("liboh_hwui_shim.so", RTLD_NOW | RTLD_LOCAL);
    if (g_owner.handle == NULL) {
        fprintf(stderr, "[WESTLAKE-WEBVIEW-CHOREO] owner load failed: %s\n",
                dlerror());
        return;
    }

#define LOAD(field, name) g_owner.field = (__typeof__(g_owner.field))dlsym(g_owner.handle, name)
    LOAD(post_vsync, "AChoreographer_postVsyncCallback");
    LOAD(get_frame_time,
         "AChoreographerFrameCallbackData_getFrameTimeNanos");
    LOAD(get_timelines_length,
         "AChoreographerFrameCallbackData_getFrameTimelinesLength");
    LOAD(get_preferred_index,
         "AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex");
    LOAD(get_timeline_vsync_id,
         "AChoreographerFrameCallbackData_getFrameTimelineVsyncId");
    LOAD(get_expected_presentation,
         "AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos");
    LOAD(get_deadline,
         "AChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos");
#undef LOAD

    g_owner.ready = g_owner.post_vsync != NULL &&
            g_owner.get_frame_time != NULL &&
            g_owner.get_timelines_length != NULL &&
            g_owner.get_preferred_index != NULL &&
            g_owner.get_timeline_vsync_id != NULL &&
            g_owner.get_expected_presentation != NULL &&
            g_owner.get_deadline != NULL;
    fprintf(stderr,
            "[WESTLAKE-WEBVIEW-CHOREO] owner=%p post=%p ready=%d\n",
            g_owner.handle, (void *)g_owner.post_vsync, g_owner.ready);
    fflush(stderr);
}

static int owner_ready(void)
{
    pthread_once(&g_owner_once, load_owner);
    return g_owner.ready;
}

void AChoreographer_postVsyncCallback(
        AChoreographer *choreographer,
        AChoreographer_vsyncCallback callback, void *data)
{
    const unsigned int count = __atomic_add_fetch(
            &g_post_count, 1, __ATOMIC_RELAXED);
    if (count <= 8) {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-CHOREO] post #%u choreographer=%p callback=%p data=%p\n",
                count, (void *)choreographer, (void *)callback, data);
        fflush(stderr);
    }
    if (owner_ready() && choreographer != NULL && callback != NULL) {
        g_owner.post_vsync(choreographer, callback, data);
    }
}

int64_t AChoreographerFrameCallbackData_getFrameTimeNanos(
        const AChoreographerFrameCallbackData *data)
{
    return owner_ready() ? g_owner.get_frame_time(data) : 0;
}

size_t AChoreographerFrameCallbackData_getFrameTimelinesLength(
        const AChoreographerFrameCallbackData *data)
{
    return owner_ready() ? g_owner.get_timelines_length(data) : 0;
}

size_t AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex(
        const AChoreographerFrameCallbackData *data)
{
    return owner_ready() ? g_owner.get_preferred_index(data) : 0;
}

int64_t AChoreographerFrameCallbackData_getFrameTimelineVsyncId(
        const AChoreographerFrameCallbackData *data, size_t index)
{
    return owner_ready() ? g_owner.get_timeline_vsync_id(data, index) : 0;
}

int64_t AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos(
        const AChoreographerFrameCallbackData *data, size_t index)
{
    return owner_ready() ? g_owner.get_expected_presentation(data, index) : 0;
}

int64_t AChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos(
        const AChoreographerFrameCallbackData *data, size_t index)
{
    return owner_ready() ? g_owner.get_deadline(data, index) : 0;
}
