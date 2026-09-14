/*
 * oh_native_window_shim.c
 *
 * Android NDK ANativeWindow_* / native_window_* API shim, implemented as
 * thin wrappers around OH NativeWindow C API. Used by libhwui (cross-compiled
 * from AOSP) to access OH Surface buffers.
 *
 * Critical functions for libhwui RenderThread / CanvasContext:
 *   - ANativeWindow_acquire/release  (reference counting)
 *   - ANativeWindow_getWidth/Height  (geometry queries)
 *   - ANativeWindow_setBuffersDataSpace (color management)
 *   - ANativeWindow_tryAllocateBuffers (performance hint)
 *   - native_window_set_buffer_count / set_scaling_mode (configuration)
 *
 * Build:
 *   $CXX --target=arm-linux-ohos -shared -fPIC \
 *        -o liboh_native_window_shim.so oh_native_window_shim.c \
 *        -lnative_window
 *
 * The output .so must be in the runtime library search path so that
 * libhwui.so's NEEDED dependencies can be resolved at dlopen time.
 */
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <jni.h>
#include <stdio.h>

/* OH NativeWindow types - opaque to us, identical layout to AOSP NativeWindow */
struct OHNativeWindow;
typedef struct OHNativeWindow OHNativeWindow;
struct OHNativeWindowBuffer;
typedef struct OHNativeWindowBuffer OHNativeWindowBuffer;

/* Forward declarations of OH NativeWindow inner API
 * (declared in graphic_surface/interfaces/inner_api/surface/window.h) */
extern int32_t NativeObjectReference(void *obj);
extern int32_t NativeObjectUnreference(void *obj);
extern int32_t NativeWindowGetDefaultWidthAndHeight(OHNativeWindow *window,
                                                    int32_t *width,
                                                    int32_t *height);
extern int32_t NativeWindowPreAllocBuffers(OHNativeWindow *window,
                                            uint32_t allocBufferCnt);
extern int32_t NativeWindowHandleOpt(OHNativeWindow *window, int code, ...);
/* OH inner API: window.h:35 — Java/native Surface* → OHNativeWindow* */
extern OHNativeWindow* CreateNativeWindowFromSurface(void *pSurface);
/* OH inner API: window.h:67 — uint64_t surfaceId → OHNativeWindow* (out) */
extern int32_t CreateNativeWindowFromSurfaceId(uint64_t surfaceId,
                                                OHNativeWindow **window);

/* OH NativeWindowOperation enum values (from external_window.h) */
#define OH_OP_SET_BUFFER_GEOMETRY  0
#define OH_OP_GET_BUFFER_GEOMETRY  1
#define OH_OP_GET_FORMAT           2
#define OH_OP_SET_FORMAT           3
#define OH_OP_GET_USAGE            4
#define OH_OP_SET_USAGE            5
#define OH_OP_SET_COLOR_GAMUT      12

/* Android ANativeWindow type — same opaque pointer */
typedef struct ANativeWindow ANativeWindow;

/* G2.14ag: AdapterAnw shim probes (impl in liboh_adapter_bridge.so;
 * see framework/window/jni/oh_anativewindow_shim.h). Returns 1 if the
 * pointer is an AdapterAnw shim and the refcount op was applied; 0 if
 * it's a raw OH NativeWindow* (caller must forward to OH NDK). */
extern int oh_anw_try_acquire(ANativeWindow *w);
extern int oh_anw_try_release(ANativeWindow *w);
extern OHNativeWindow *oh_anw_get_oh(ANativeWindow *w);
extern ANativeWindow *oh_anw_wrap(OHNativeWindow *oh);

/* Session→window resolution exported by liboh_adapter_bridge.so (which is a
 * DT_NEEDED of libhwui).  These are the same two calls compat_shim's
 * sc_to_oh_native_window() and S_nativeGetFromBlastBufferQueue() already use
 * to recover the bound OHNativeWindow when a Surface handle has not yet been
 * stamped.  Declared here so ANativeWindow_fromSurface can apply the same
 * documented fallback instead of reporting "no surface" to hwui. */
extern int32_t oh_wm_get_last_session(void);
extern void *oh_wm_get_native_window(int32_t sessionId);

/* Internal: cast Android window pointer to OH window pointer.
 *
 * Pre-G2.14ae (legacy assumption): hwui's ANativeWindow* IS an
 * OHNativeWindow* (compat_shim returned the OH handle directly).
 * Post-G2.14ae: hwui's ANativeWindow* may be an AdapterAnw shim, in
 * which case offset 0 is AOSP magic and OH NDK rejects it. Always
 * unwrap via oh_anw_get_oh first; fall back to direct cast for the
 * legacy path (some adapter-internal callers still pass raw OH). */

static inline OHNativeWindow *to_oh(ANativeWindow *w) {
    OHNativeWindow *oh = oh_anw_get_oh(w);
    return oh ? oh : (OHNativeWindow *)w;
}

/* ============================================================ */
/* Surface→ANativeWindow conversion                              */
/* ============================================================ */
/*
 * libhwui ThreadedRenderer.setSurface invokes this to get a native window
 * pointer from the Java android.view.Surface.  Real bridge:
 *   1. Read `mNativeObject` (long) field from Java Surface.  In our adapter
 *      flow this field stores the OH-managed Surface pointer set when the
 *      Window/SurfaceControl path creates an OH RSSurface.
 *   2. Pass that pointer to OH inner API CreateNativeWindowFromSurface,
 *      which produces the OHNativeWindow handle libhwui expects.
 * If mNativeObject == 0 (Surface not yet wired to an OH backend), return
 * NULL — libhwui treats that as "no surface" and skips draw.  This is not
 * a stub but the documented unbound state of a Surface object.
 */
ANativeWindow *ANativeWindow_fromSurface(JNIEnv *env, jobject surface) {
    /* Bounded diagnostic, mirroring the [WESTLAKE-SC2NW] probe in
     * compat_shim.cpp.  This function decides whether CanvasContext ever gets
     * a surface, yet it logged nothing, so "returned NULL" and "was never
     * called" were indistinguishable in a device capture.  HiLog is unreliable
     * on this board, so mirror to the child's captured stderr. */
    static int wl_fs_calls = 0;
    const int wl_log = (wl_fs_calls < 16) ? ++wl_fs_calls : 0;

    if (!env || !surface) {
        if (wl_log) {
            fprintf(stderr, "[WESTLAKE-ANWFS] #%d null arg env=%p surface=%p -> NULL\n",
                    wl_log, (void *)env, (void *)surface);
            fflush(stderr);
        }
        return NULL;
    }
    jclass cls = (*env)->FindClass(env, "android/view/Surface");
    if (!cls) {
        (*env)->ExceptionClear(env);
        if (wl_log) {
            fprintf(stderr, "[WESTLAKE-ANWFS] #%d FindClass(android/view/Surface) failed -> NULL\n",
                    wl_log);
            fflush(stderr);
        }
        return NULL;
    }
    jfieldID fid = (*env)->GetFieldID(env, cls, "mNativeObject", "J");
    (*env)->DeleteLocalRef(env, cls);
    if (!fid) {
        (*env)->ExceptionClear(env);
        if (wl_log) {
            fprintf(stderr, "[WESTLAKE-ANWFS] #%d GetFieldID(mNativeObject) failed -> NULL\n",
                    wl_log);
            fflush(stderr);
        }
        return NULL;
    }
    jlong nativeObj = (*env)->GetLongField(env, surface, fid);
    if (wl_log) {
        fprintf(stderr, "[WESTLAKE-ANWFS] #%d mNativeObject=0x%llx\n",
                wl_log, (unsigned long long)nativeObj);
        fflush(stderr);
    }
    if (nativeObj == 0) {
        /* §283ab: an unbound Surface here is NOT benign.  DrawFrameTask::
         * syncFrameState() calls CanvasContext::prepareTree() *before* it
         * checks hasOutputTarget(), and prepareTree ->
         * RenderProperties::fitsOnLayer() reads DeviceInfo::maxTextureSize(),
         * which LOG_ALWAYS_FATALs ("MaxTextureSize has not been initialized
         * yet.") until a GrContext exists.  The GrContext is only created by
         * SkiaOpenGLPipeline::setSurface(window) when window != NULL, and by
         * RenderThread::setGrContext() -> DeviceInfo::setMaxTextureSize().  So
         * returning NULL here does not merely "skip draw" as the comment below
         * assumed -- it aborts the RenderThread on the very first frame.
         * Recover the window the window session has already bound, exactly as
         * compat_shim's sc_to_oh_native_window() and
         * S_nativeGetFromBlastBufferQueue() already do for this same race. */
        const int32_t sid = oh_wm_get_last_session();
        void *nw = sid ? oh_wm_get_native_window(sid) : NULL;
        if (wl_log) {
            fprintf(stderr,
                    "[WESTLAKE-ANWFS] #%d unbound Surface; last-session=%d -> nw=%p\n",
                    wl_log, (int)sid, nw);
            fflush(stderr);
        }
        if (!nw) return NULL;
        nativeObj = (jlong)(intptr_t)nw;
    }
    /* WESTLAKE §283m — THE nSetSurface HANG.
     * This used to call CreateNativeWindowFromSurface(nativeObj).  That OH API expects the
     * ADDRESS OF AN sptr<Surface>, but in this port Surface.mNativeObject ALREADY HOLDS AN
     * OHNativeWindow* -- it is produced by sc_to_oh_native_window() -> oh_wm_get_native_window(),
     * which itself already called CreateNativeWindowFromSurface().  Passing an OHNativeWindow*
     * where an sptr<Surface>* is expected is type confusion, and it wedged the UI thread inside
     * HardwareRenderer.nSetSurface (measured: main thread futex_wait_queue_me forever while
     * RenderThread sat idle in epoll_wait).
     * On OH, OHNativeWindow IS ANativeWindow, so hand it back directly -- exactly what the
     * bridge's own copy of this function already does (it is shadowed by this one, which is
     * first in libhwui's DT_NEEDED, so THIS is the version that actually runs). */
    /* §283y TESTED AND REVERTED: returning the AdapterAnw wrapper fixes window->query but
     * BREAKS EGL creation (mRenderPipeline->setSurface then hangs instead), because hwui's own
     * eglCreateWindowSurface wrapper wants the raw OH handle.  Keep the raw pointer here and
     * instead remove the struct-layout landmine on the other side: libhwui's setBufferCount
     * (see §283z in CanvasContext.cpp). */
#ifdef WESTLAKE_SOURCE_HWUI15
    /* §283ac — DOUBLE-WRAP.  OHWindowManagerClient::getOhNativeWindow() already
     * ends with `oh_anw_wrap(nw)` and caches/returns that AdapterAnw*, and
     * BBQ_nativeUpdate/BBQ_nativeGetSurface stamp THAT wrapper into
     * Surface.mNativeObject.  Wrapping it again here handed
     * OH_NativeWindow_NativeObjectReference() a pointer whose offset 0 holds
     * AOSP's '_wnd' magic rather than OH's NATIVE_OBJECT_MAGIC_WINDOW ('WIND'),
     * so OH rejected it (0 is success, non-zero is failure), oh_anw_wrap
     * returned NULL, and hwui saw "no surface".  That is not benign: with no
     * window, SkiaOpenGLPipeline::setSurface never calls requireGlContext(), no
     * GrContext is created, DeviceInfo::setMaxTextureSize() never runs, and the
     * first DrawFrameTask::syncFrameState -> prepareTree -> fitsOnLayer()
     * aborts the RenderThread on "MaxTextureSize has not been initialized yet."
     * Only wrap a genuinely raw OH handle; pass an already-wrapped one through.
     * This mirrors to_oh()'s existing "unwrap if wrapped, else direct cast"
     * idiom used by nine other entry points in this file.  No extra reference
     * is taken here -- hwui's ReliableSurface/ANativeWindow_acquire pairs with
     * the ANativeWindow_release in android_view_ThreadedRenderer_setSurface. */
    ANativeWindow *candidate = (ANativeWindow *)(intptr_t)nativeObj;
    const int wl_prewrapped = (oh_anw_get_oh(candidate) != NULL);
    ANativeWindow *result;
    if (wl_prewrapped) {
        /* AOSP's ANativeWindow_fromSurface contract hands back a +1 reference,
         * and android_view_ThreadedRenderer_setSurface releases it immediately
         * after proxy->setSurface().  AOSP's ReliableSurface ctor takes its own
         * acquire, but this port's override (preserved/runtime-sources/local/
         * hwui-overrides/renderthread/ReliableSurface.cpp) deliberately does
         * not.  So without this acquire, that release would drive the
         * session-cached wrapper's refCount 1 -> 0, run oh_anw_destroy(), and
         * leave OHWindowManagerClient::sessions_ holding a dangling
         * ohNativeWindow -- a use-after-free on the first frame.  Take the
         * reference the contract promises; the session map keeps its own. */
        oh_anw_try_acquire(candidate);
        result = candidate;
    } else {
        /* A genuinely raw OH handle: oh_anw_wrap() returns refCount == 1, which
         * is itself the caller's +1, so no extra acquire is needed here. */
        result = oh_anw_wrap((OHNativeWindow *)(intptr_t)nativeObj);
    }
#else
    ANativeWindow *result = (ANativeWindow *)(intptr_t)nativeObj;
    const int wl_prewrapped = 0;
#endif
    if (wl_log) {
        fprintf(stderr, "[WESTLAKE-ANWFS] #%d -> ANativeWindow=%p (prewrapped=%d)\n",
                wl_log, (void *)result, wl_prewrapped);
        fflush(stderr);
    }
    return result;
}

/* ============================================================ */
/* Reference counting                                            */
/* ============================================================ */

void ANativeWindow_acquire(ANativeWindow *window) {
    if (!window) return;
    /* G2.14ag: AdapterAnw shim → AOSP-style refcount via shim's atomic.
     * Raw OH NativeWindow* → OH RefBase IncStrongRef. */
    if (oh_anw_try_acquire(window)) return;
    NativeObjectReference((void *)window);
}

void ANativeWindow_release(ANativeWindow *window) {
    if (!window) return;
    if (oh_anw_try_release(window)) return;
    NativeObjectUnreference((void *)window);
}

/* ============================================================ */
/* Dimension queries                                             */
/* ============================================================ */

int32_t ANativeWindow_getWidth(ANativeWindow *window) {
    if (!window) return -1;
    int32_t w = 0, h = 0;
    NativeWindowGetDefaultWidthAndHeight(to_oh(window), &w, &h);
    return w;
}

int32_t ANativeWindow_getHeight(ANativeWindow *window) {
    if (!window) return -1;
    int32_t w = 0, h = 0;
    NativeWindowGetDefaultWidthAndHeight(to_oh(window), &w, &h);
    return h;
}

int32_t ANativeWindow_getFormat(ANativeWindow *window) {
    if (!window) return -1;
    int32_t format = 0;
    NativeWindowHandleOpt(to_oh(window), OH_OP_GET_FORMAT, &format);
    return format;
}

/* ============================================================ */
/* Buffer geometry / format / usage                              */
/* ============================================================ */

int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *window,
                                          int32_t width, int32_t height,
                                          int32_t format) {
    if (!window) return -1;
    NativeWindowHandleOpt(to_oh(window), OH_OP_SET_BUFFER_GEOMETRY, width, height);
    if (format > 0) {
        NativeWindowHandleOpt(to_oh(window), OH_OP_SET_FORMAT, format);
    }
    return 0;
}

int32_t ANativeWindow_setBuffersTransform(ANativeWindow *window, int32_t transform) {
    /* OH supports transform via SET_TRANSFORM, but we stub for Phase 1 */
    (void)window; (void)transform;
    return 0;
}

int32_t ANativeWindow_setBuffersDataSpace(ANativeWindow *window, int32_t dataSpace) {
    if (!window) return -1;
    /* Map Android dataSpace to OH color gamut (simplified) */
    int32_t ohColorGamut = 0; /* sRGB default */
    if (dataSpace == 143261696) ohColorGamut = 1; /* Display P3 */
    else if (dataSpace == 163971072) ohColorGamut = 2; /* BT2020 */
    NativeWindowHandleOpt(to_oh(window), OH_OP_SET_COLOR_GAMUT, ohColorGamut);
    return 0;
}

int32_t ANativeWindow_getBuffersDataSpace(ANativeWindow *window) {
    (void)window;
    return 0; /* sRGB */
}

/* ============================================================ */
/* Performance / pre-allocation                                  */
/* ============================================================ */

void ANativeWindow_tryAllocateBuffers(ANativeWindow *window) {
    if (window) {
        NativeWindowPreAllocBuffers(to_oh(window), 3);  /* triple buffer */
    }
}

void ANativeWindow_setDequeueTimeout(ANativeWindow *window, int64_t timeout) {
    /* OH uses default timeout; no direct API to set it */
    (void)window; (void)timeout;
}

/* ============================================================ */
/* Frame timing queries (return 0/sentinels — used by JankTracker)*/
/* ============================================================ */

int64_t ANativeWindow_getLastDequeueDuration(ANativeWindow *window) {
    (void)window; return 0;
}

int64_t ANativeWindow_getLastDequeueStartTime(ANativeWindow *window) {
    (void)window; return 0;
}

int64_t ANativeWindow_getLastQueueDuration(ANativeWindow *window) {
    (void)window; return 0;
}

int64_t ANativeWindow_getNextFrameId(ANativeWindow *window) {
    static int64_t counter = 0;
    (void)window;
    return ++counter;
}

int32_t ANativeWindow_setFrameRate(ANativeWindow *window, float frameRate, int8_t compatibility) {
    (void)window; (void)frameRate; (void)compatibility;
    return 0;
}

/* ============================================================ */
/* Legacy native_window_* perform-style API                      */
/* ============================================================ */

int native_window_set_buffer_count(ANativeWindow *window, int count) {
    /* WESTLAKE §283x — THE UI/RenderThread DEADLOCK.
     * This used to call NativeWindowPreAllocBuffers(), which eagerly allocates `count` buffers
     * from the producer surface.  That is the SAME family of consumer-side buffer operation that
     * §283i proved blocks FOREVER on this board (Surface::RequestBuffer never returns, even with
     * timeout 0).  Measured chain:
     *   RenderThread: CanvasContext::setSurface -> setupPipelineSurface ->
     *       mRenderPipeline->setSurface OK (=1, the EGL surface IS created) ->
     *       setBufferCount -> native_window_set_buffer_count -> PreAllocBuffers -> NEVER RETURNS
     *       (marker "pre setBufferCount" prints, "post setBufferCount" never does)
     *   => the RenderThread never drains its WorkQueue again
     *   => the UI thread's ThreadedRenderer.pause() runSync waits forever  (§283t)
     *   => performTraversals stops at 2 and nothing is ever composited.
     * Buffer pre-allocation is a PERFORMANCE HINT (AOSP treats setBufferCount as advisory), so
     * skipping it costs nothing but the first-frame allocation latency.
     */
    (void) window; (void) count;
    if (0) {
        NativeWindowPreAllocBuffers(to_oh(window), count);
    }
    return 0;
}

int native_window_set_scaling_mode(ANativeWindow *window, int mode) {
    (void)window; (void)mode;
    return 0;
}

int native_window_set_frame_timeline_info(ANativeWindow *window,
                                          uint64_t frameNumber,
                                          int64_t frameTimelineVsyncId,
                                          int32_t inputEventId,
                                          int64_t startTimeNanos,
                                          int useForRefreshRateSelection) {
    (void)window; (void)frameNumber; (void)frameTimelineVsyncId;
    (void)inputEventId; (void)startTimeNanos; (void)useForRefreshRateSelection;
    return 0;
}

int native_window_enable_frame_timestamps(ANativeWindow *window, int enable) {
    (void)window; (void)enable;
    return 0;
}

int native_window_get_frame_timestamps(ANativeWindow *window,
                                       uint64_t frameNumber,
                                       int64_t *outRequestedPresentTime,
                                       int64_t *outAcquireTime,
                                       int64_t *outLatchTime,
                                       int64_t *outFirstRefreshStartTime,
                                       int64_t *outLastRefreshStartTime,
                                       int64_t *outGlCompositionDoneTime,
                                       int64_t *outDisplayPresentTime,
                                       int64_t *outDequeueReadyTime,
                                       int64_t *outReleaseTime) {
    (void)window; (void)frameNumber;
    if (outRequestedPresentTime) *outRequestedPresentTime = 0;
    if (outAcquireTime) *outAcquireTime = 0;
    if (outLatchTime) *outLatchTime = 0;
    if (outFirstRefreshStartTime) *outFirstRefreshStartTime = 0;
    if (outLastRefreshStartTime) *outLastRefreshStartTime = 0;
    if (outGlCompositionDoneTime) *outGlCompositionDoneTime = 0;
    if (outDisplayPresentTime) *outDisplayPresentTime = 0;
    if (outDequeueReadyTime) *outDequeueReadyTime = 0;
    if (outReleaseTime) *outReleaseTime = 0;
    return -1;
}

/* ============================================================ */
/* A.10 PENDING STUB: ANativeWindow_getLastQueuedBuffer2         */
/*                                                                */
/* AOSP libhwui Readback.cpp uses this NDK API to grab the last  */
/* queued buffer for capture / pixelcopy. OH's libnative_window  */
/* does not expose the "last queued" concept and AOSP's inline   */
/* fallback (calling window->perform(NATIVE_WINDOW_GET_LAST_     */
/* QUEUED_BUFFER2, ...)) would trigger an undefined OH perform   */
/* code. Returning -1 (STATUS_UNKNOWN_TRANSACTION) signals the   */
/* caller to use its own fallback path.                          */
/* ============================================================ */
struct ARect;
typedef struct AHardwareBuffer AHardwareBuffer;
int ANativeWindow_getLastQueuedBuffer2(ANativeWindow* window,
                                        AHardwareBuffer** outBuffer,
                                        int* outFence,
                                        struct ARect* outCropRect,
                                        uint32_t* outTransform) {
    (void)window;
    if (outBuffer) *outBuffer = NULL;
    if (outFence) *outFence = -1;
    /* outCropRect is an opaque struct; caller passes pointer, we leave untouched */
    if (outTransform) *outTransform = 0;
    return -1;  /* STATUS_UNKNOWN_TRANSACTION — caller falls back */
}
