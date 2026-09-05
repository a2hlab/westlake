/*
 * oh_input_bridge.cpp
 *
 * Native input event bridge implementation.
 *
 * Writes Android-format InputMessage structs to the server side of
 * InputChannel socket pairs. The message format must match what
 * InputConsumer (in ViewRootImpl) expects to read.
 *
 * InputMessage format (simplified for single-pointer touch):
 *   - Header: type, seq
 *   - Body (motion): action, deviceId, source, displayId, pointerCount,
 *                     downTime, eventTime, pointerProperties, pointerCoords
 */
#include "oh_input_bridge.h"

#include <android/log.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <thread>
#include <atomic>
#include <ctime>
#include <new>
#include <string>

extern "C" int wl_MessageQueue_post(void* looper, void (*callback)(void*), void* data);

// OH MMI inner_api headers — pulled in here only (forward-decl'd in .h)
#include "axis_event.h"
#include "event_handler.h"
#include "event_runner.h"
#include "i_input_event_consumer.h"
#include "input_manager.h"
#include "key_event.h"
#include "pointer_event.h"

// WESTLAKE §408: injected-input window selector (see wl_dump_view_roots below).
static std::atomic<int> g_rootIndex{-1};
void wl_set_root_index(int idx) { g_rootIndex.store(idx); }

#define LOG_TAG "OH_InputBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Android InputMessage constants (from InputTransport.h)
// These must match the AOSP InputMessage struct layout
namespace {

// InputMessage types (mirror enum class InputMessage::Type at
// frameworks/native/include/input/InputTransport.h:69-77)
constexpr uint32_t INPUT_MSG_TYPE_KEY      = 0;
constexpr uint32_t INPUT_MSG_TYPE_MOTION   = 1;
constexpr uint32_t INPUT_MSG_TYPE_FINISHED = 2;
constexpr uint32_t INPUT_MSG_TYPE_FOCUS    = 3;
constexpr uint32_t INPUT_MSG_TYPE_CAPTURE  = 4;
constexpr uint32_t INPUT_MSG_TYPE_DRAG     = 5;
constexpr uint32_t INPUT_MSG_TYPE_TIMELINE = 6;
constexpr uint32_t INPUT_MSG_TYPE_TOUCHMODE = 7;

// MotionEvent source (from system/core/include/android/input.h)
constexpr int32_t AINPUT_SOURCE_TOUCHSCREEN = 0x00001002;

// MotionEvent actions
constexpr int32_t AMOTION_EVENT_ACTION_DOWN = 0;
constexpr int32_t AMOTION_EVENT_ACTION_UP = 1;
constexpr int32_t AMOTION_EVENT_ACTION_MOVE = 2;
constexpr int32_t AMOTION_EVENT_ACTION_CANCEL = 3;

// MotionEvent tool type (enum class ToolType { UNKNOWN=0, FINGER=1, ... }
// from frameworks/native/include/input/Input.h:234 — underlying type int)
constexpr int32_t AMOTION_EVENT_TOOL_TYPE_FINGER = 1;

// AOSP 14 layout constants (frameworks/native/include/input/Input.h)
constexpr size_t MAX_POINTERS = 16;            // line 165
constexpr size_t MAX_POINTER_COORDS_AXES = 30; // PointerCoords::MAX_AXES

// Axis bit indices (subset; X/Y/Pressure/Size are the only ones we set)
constexpr uint64_t AXIS_X_BIT        = (1ULL << 0);
constexpr uint64_t AXIS_Y_BIT        = (1ULL << 1);
constexpr uint64_t AXIS_PRESSURE_BIT = (1ULL << 2);
constexpr uint64_t AXIS_SIZE_BIT     = (1ULL << 3);

// ============================================================
// AOSP 14 InputMessage canonical layout
// (frameworks/native/include/input/InputTransport.h:67-...)
//
// Audited 2026-05-18 against ECS ~/aosp source. ALL field order, alignment
// directives, and padding MUST match exactly — InputConsumer::consume()
// parses by struct size + offsets. Any drift = client reads garbage.
// ============================================================

// Header: 8 bytes total (4 type + 4 seq).
struct InputMessageHeader {
    uint32_t type;
    uint32_t seq;
};
static_assert(sizeof(InputMessageHeader) == 8, "Header must be 8 bytes");

// PointerProperties: 8 bytes (int32 id + enum class ToolType, default int).
struct PointerProperties {
    int32_t id;
    int32_t toolType;  // ToolType enum class — int underlying
};
static_assert(sizeof(PointerProperties) == 8, "PointerProperties must be 8B");

// PointerCoords: 136 bytes (bits 8 + values 120 + bool 1 + empty 7).
struct PointerCoords {
    uint64_t bits __attribute__((aligned(8)));
    float values[MAX_POINTER_COORDS_AXES];
    bool isResampled;
    uint8_t empty[7];
};
static_assert(sizeof(PointerCoords) == 136, "PointerCoords must be 136B");

// One pointer = properties + coords = 144 bytes.
struct InputMessagePointer {
    PointerProperties properties;
    PointerCoords coords;
};
static_assert(sizeof(InputMessagePointer) == 144,
              "InputMessagePointer must be 144B");

// Motion body — order/alignment mirrors AOSP InputMessage::Body::Motion.
// Note: pointers[MAX_POINTERS] is the FULL array on the C++ side, but
// writeMotionEvent only sends `pointerCount` actual pointers over the wire
// (AOSP's Motion::size() formula). This struct's sizeof is the upper bound.
struct MotionEventBody {
    int32_t eventId;
    uint32_t pointerCount;
    int64_t eventTime __attribute__((aligned(8)));
    int32_t deviceId;
    int32_t source;
    int32_t displayId;
    uint8_t hmac[32];
    int32_t action;
    int32_t actionButton;
    int32_t flags;
    int32_t metaState;
    int32_t buttonState;
    uint8_t classification;       // MotionClassification : uint8_t
    uint8_t empty2[3];            // 3-byte gap before edgeFlags
    int32_t edgeFlags;
    int64_t downTime __attribute__((aligned(8)));
    // Window transform (6 floats, AOSP order: dsdx, dtdx, dtdy, dsdy, tx, ty)
    float dsdx;
    float dtdx;
    float dtdy;
    float dsdy;
    float tx;
    float ty;
    float xPrecision;
    float yPrecision;
    float xCursorPosition;
    float yCursorPosition;
    // Raw transform (6 floats, same order as window transform)
    float dsdxRaw;
    float dtdxRaw;
    float dtdyRaw;
    float dsdyRaw;
    float txRaw;
    float tyRaw;
    // Pointer data array — MUST be the last field of the struct so
    // wire-format size() can truncate trailing pointers (see AOSP comment).
    InputMessagePointer pointers[MAX_POINTERS] __attribute__((aligned(8)));
};

// Wire-format size for `pointerCount` actual pointers.
// Matches AOSP: sizeof(Motion) - MAX_POINTERS*sizeof(Pointer) + n*sizeof(Pointer).
constexpr size_t motionBodyWireSize(uint32_t pointerCount) {
    return sizeof(MotionEventBody)
            - sizeof(InputMessagePointer) * MAX_POINTERS
            + sizeof(InputMessagePointer) * pointerCount;
}

// Finished body — for ACK back from client to server.
struct FinishedBody {
    bool handled;
    uint8_t empty[7];
    int64_t consumeTime;
};
static_assert(sizeof(FinishedBody) == 16, "FinishedBody must be 16B");

// ============================================================
// Key body — bit-exact AOSP 14 InputMessage::Body::Key
// (frameworks/native/include/input/InputTransport.h struct Key).
// ============================================================
struct KeyEventBody {
    int32_t eventId;
    uint32_t empty1;
    int64_t eventTime __attribute__((aligned(8)));
    int32_t deviceId;
    int32_t source;
    int32_t displayId;
    uint8_t hmac[32];
    int32_t action;
    int32_t flags;
    int32_t keyCode;
    int32_t scanCode;
    int32_t metaState;
    int32_t repeatCount;
    uint32_t empty2;
    int64_t downTime __attribute__((aligned(8)));
};

// KeyEvent source + actions (system/core/include/android/input.h)
constexpr int32_t AINPUT_SOURCE_KEYBOARD     = 0x00000101;
constexpr int32_t AKEY_EVENT_ACTION_DOWN     = 0;
constexpr int32_t AKEY_EVENT_ACTION_UP       = 1;

// OH MMI keycode -> AOSP Android keycode (navigation subset).
// OH d-pad/system codes are 2000+; AOSP are the classic <100 values.
static int32_t ohKeyCodeToAndroid(int32_t oh) {
    switch (oh) {
        case 2012: return 19;  // DPAD_UP
        case 2013: return 20;  // DPAD_DOWN
        case 2014: return 21;  // DPAD_LEFT
        case 2015: return 22;  // DPAD_RIGHT
        case 2016: return 23;  // DPAD_CENTER
        case 2054: return 66;  // ENTER
        case 2:    return 4;   // BACK
        case 1:    return 3;   // HOME
        case 2049: return 62;  // SPACE  (KEYCODE_SPACE)
        case 2050: return 61;  // TAB    (KEYCODE_TAB)
        case 2055: return 67;  // DEL / BACKSPACE
        case 2071: return 92;  // PAGE_UP
        case 2072: return 93;  // PAGE_DOWN
        default:   return -1;  // unmapped -> drop
    }
}

}  // anonymous namespace

namespace oh_adapter {

// §414: forward declarations so the key path (defined above the helpers) can use them.
static bool wl_report_exception(JNIEnv* env, const char* where);
static void wl_ensure_looper(JNIEnv* env);

// WESTLAKE §785: WindowManagerGlobal.mRoots may retain a shown PopupDecorView whose laid-out
// width is zero.  Android's InputDispatcher cannot target a zero-area input window, but this OHOS
// boundary used isShown() alone and therefore selected that stale popup merely because it was the
// newest root.  Preserve Android's hit-test invariant for automatic selection: only a laid-out,
// non-empty root may win.  An explicit `rN` diagnostic selection still wins unchanged.
// Return a priority rather than a boolean. A focused Android window must beat a merely shown
// stale root even when the stale root was appended later to WindowManagerGlobal.mRoots.
// Explicit diagnostic selection remains strongest; among roots at the same priority, callers
// keep the last entry so topmost dialogs still win.
static int wl_root_target_priority(JNIEnv* env, jobject view, jclass viewCls,
                                   jmethodID hasFocusM, int wantedRoot, jint rootIndex) {
    if (wantedRoot >= 0) {
        return rootIndex == wantedRoot ? 3 : 0;
    }

    jmethodID getWidthM = env->GetMethodID(viewCls, "getWidth", "()I");
    jmethodID getHeightM = env->GetMethodID(viewCls, "getHeight", "()I");
    if (!getWidthM || !getHeightM || env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }
    const jint width = env->CallIntMethod(view, getWidthM);
    const jint height = env->CallIntMethod(view, getHeightM);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }
    if (width <= 0 || height <= 0) {
        LOGI("[WESTLAKE-ROOTBOUNDS-785] skip root[%d] bounds=%dx%d",
             (int)rootIndex, (int)width, (int)height);
        return 0;
    }

    jboolean focused = env->CallBooleanMethod(view, hasFocusM);
    if (env->ExceptionCheck()) { env->ExceptionClear(); focused = JNI_FALSE; }
    if (focused) return 2;

    jmethodID shownM = env->GetMethodID(viewCls, "isShown", "()Z");
    jboolean shown = shownM ? env->CallBooleanMethod(view, shownM) : JNI_FALSE;
    if (env->ExceptionCheck()) { env->ExceptionClear(); shown = JNI_FALSE; }
    return shown ? 1 : 0;
}


OHInputBridge& OHInputBridge::getInstance() {
    static OHInputBridge instance;
    return instance;
}

void OHInputBridge::registerInputChannel(int32_t sessionId, int serverFd) {
    std::lock_guard<std::mutex> lock(mutex_);

    SessionInput& session = sessions_[sessionId];
    session.serverFd = serverFd;
    session.seq = 0;

    LOGI("Registered input channel: session=%d, fd=%d", sessionId, serverFd);
}

void OHInputBridge::unregisterInputChannel(int32_t sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        // Don't close serverFd here; Java InputChannel owns it
        LOGI("Unregistered input channel: session=%d", sessionId);
        sessions_.erase(it);
    }
}

void OHInputBridge::registerOHInputFd(int32_t sessionId, int ohInputFd) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        it->second.ohInputFd = ohInputFd;
        LOGI("Registered OH input fd: session=%d, ohFd=%d", sessionId, ohInputFd);
    }

    // Start monitoring thread if not already running
    if (!monitoring_.load()) {
        monitoring_ = true;
        monitorThread_ = std::thread(&OHInputBridge::monitorOHInputEvents, this);
        monitorThread_.detach();
    }
}

int32_t OHInputBridge::injectTouchEvent(int32_t sessionId, int32_t action,
                                          float x, float y,
                                          int64_t downTime, int64_t eventTime) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        LOGE("injectTouchEvent: session %d not found", sessionId);
        return -1;
    }

    SessionInput& session = it->second;
    if (session.serverFd < 0) {
        LOGE("injectTouchEvent: session %d has no server fd", sessionId);
        return -1;
    }

    session.seq++;
    int result = writeMotionEvent(session.serverFd, session.seq,
                                   action, x, y, downTime, eventTime);

    LOGD("injectTouchEvent: session=%d, action=%d, x=%.1f, y=%.1f, result=%d",
         sessionId, action, x, y, result);

    return result;
}

int32_t OHInputBridge::injectKeyEvent(int32_t sessionId, int32_t action,
                                       int32_t androidKeyCode,
                                       int64_t downTime, int64_t eventTime) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        LOGE("injectKeyEvent: session %d not found", sessionId);
        return -1;
    }

    SessionInput& session = it->second;
    if (session.serverFd < 0) {
        LOGE("injectKeyEvent: session %d has no server fd", sessionId);
        return -1;
    }

    session.seq++;
    int result = writeKeyEvent(session.serverFd, session.seq, action,
                                androidKeyCode, downTime, eventTime);

    LOGI("injectKeyEvent: session=%d, action=%d, keyCode=%d, result=%d",
         sessionId, action, androidKeyCode, result);

    return result;
}

// WESTLAKE §735: complete Activity.finish() in BMS-free direct-launch mode.
//
// The Android ActivityClientController call is intact: Activity.finish() marks
// Activity.mFinished after the adapter returns true.  What direct-launch lacks
// is system_server/AMS sending the normal DestroyActivityItem back to the app.
// Consequently a finished second Activity and its window remain above the
// previous Activity forever.  At the OH input boundary, after a BACK key-up has
// traversed the stock Android dispatch path, schedule that missing standard
// client transaction for every ActivityThread record that Android itself marked
// finished.  A WebView history BACK leaves mFinished false and is untouched.
static void wl_destroy_finished_direct_launch_activities(JNIEnv* env) {
    if (getenv("ASX_DIRECT_LAUNCH") == nullptr) return;

    jclass atCls = env->FindClass("android/app/ActivityThread");
    if (atCls == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    jmethodID currentM = env->GetStaticMethodID(
        atCls, "currentActivityThread", "()Landroid/app/ActivityThread;");
    jmethodID appThreadM = env->GetMethodID(
        atCls, "getApplicationThread", "()Landroid/app/ActivityThread$ApplicationThread;");
    jfieldID activitiesF = env->GetFieldID(
        atCls, "mActivities", "Landroid/util/ArrayMap;");
    if (currentM == nullptr || appThreadM == nullptr || activitiesF == nullptr ||
        env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    jobject at = env->CallStaticObjectMethod(atCls, currentM);
    jobject appThread = at != nullptr ? env->CallObjectMethod(at, appThreadM) : nullptr;
    jobject activities = at != nullptr ? env->GetObjectField(at, activitiesF) : nullptr;
    if (at == nullptr || appThread == nullptr || activities == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jclass mapCls = env->FindClass("android/util/ArrayMap");
    jmethodID sizeM = mapCls != nullptr ? env->GetMethodID(mapCls, "size", "()I") : nullptr;
    jmethodID keyAtM = mapCls != nullptr
        ? env->GetMethodID(mapCls, "keyAt", "(I)Ljava/lang/Object;") : nullptr;
    jmethodID valueAtM = mapCls != nullptr
        ? env->GetMethodID(mapCls, "valueAt", "(I)Ljava/lang/Object;") : nullptr;
    if (mapCls == nullptr || sizeM == nullptr || keyAtM == nullptr || valueAtM == nullptr ||
        env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    jclass txCls = env->FindClass("android/app/servertransaction/ClientTransaction");
    jclass destroyCls = env->FindClass("android/app/servertransaction/DestroyActivityItem");
    jclass resultCls = env->FindClass("android/app/servertransaction/ActivityResultItem");
    jclass listCls = env->FindClass("java/util/ArrayList");
    if (txCls == nullptr || destroyCls == nullptr || resultCls == nullptr || listCls == nullptr ||
        env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }
    jmethodID txObtain = env->GetStaticMethodID(txCls, "obtain",
        "(Landroid/app/IApplicationThread;Landroid/os/IBinder;)"
        "Landroid/app/servertransaction/ClientTransaction;");
    jmethodID destroyObtain = env->GetStaticMethodID(destroyCls, "obtain",
        "(ZI)Landroid/app/servertransaction/DestroyActivityItem;");
    jmethodID setLifecycle = env->GetMethodID(txCls, "setLifecycleStateRequest",
        "(Landroid/app/servertransaction/ActivityLifecycleItem;)V");
    jmethodID addCallback = env->GetMethodID(txCls, "addCallback",
        "(Landroid/app/servertransaction/ClientTransactionItem;)V");
    jmethodID listCtor = env->GetMethodID(listCls, "<init>", "()V");
    jmethodID resultObtain = env->GetStaticMethodID(resultCls, "obtain",
        "(Ljava/util/List;)Landroid/app/servertransaction/ActivityResultItem;");
    jclass appThreadCls = env->GetObjectClass(appThread);
    jmethodID scheduleM = appThreadCls != nullptr
        ? env->GetMethodID(appThreadCls, "scheduleTransaction",
            "(Landroid/app/servertransaction/ClientTransaction;)V") : nullptr;
    if (txObtain == nullptr || destroyObtain == nullptr || setLifecycle == nullptr ||
        addCallback == nullptr || listCtor == nullptr || resultObtain == nullptr ||
        scheduleM == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return;
    }

    const jint count = env->CallIntMethod(activities, sizeM);
    for (jint i = count - 1; i >= 0; --i) {
        jobject token = env->CallObjectMethod(activities, keyAtM, i);
        jobject record = env->CallObjectMethod(activities, valueAtM, i);
        if (token == nullptr || record == nullptr || env->ExceptionCheck()) {
            env->ExceptionClear();
            if (token != nullptr) env->DeleteLocalRef(token);
            if (record != nullptr) env->DeleteLocalRef(record);
            continue;
        }
        jclass recordCls = env->GetObjectClass(record);
        jfieldID activityF = recordCls != nullptr
            ? env->GetFieldID(recordCls, "activity", "Landroid/app/Activity;") : nullptr;
        jobject activity = activityF != nullptr ? env->GetObjectField(record, activityF) : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
        jclass activityCls = activity != nullptr ? env->GetObjectClass(activity) : nullptr;
        jfieldID finishedF = activityCls != nullptr
            ? env->GetFieldID(activityCls, "mFinished", "Z") : nullptr;
        const jboolean finished = finishedF != nullptr
            ? env->GetBooleanField(activity, finishedF) : JNI_FALSE;
        if (env->ExceptionCheck()) env->ExceptionClear();

        if (finished == JNI_TRUE) {
            jobject tx = env->CallStaticObjectMethod(txCls, txObtain, appThread, token);
            jobject destroy = env->CallStaticObjectMethod(
                destroyCls, destroyObtain, JNI_TRUE, (jint)0);
            jobject emptyResults = env->NewObject(listCls, listCtor);
            jobject result = emptyResults != nullptr
                ? env->CallStaticObjectMethod(resultCls, resultObtain, emptyResults) : nullptr;
            if (!env->ExceptionCheck() && tx != nullptr && result != nullptr) {
                env->CallVoidMethod(tx, addCallback, result);
            }
            if (!env->ExceptionCheck() && tx != nullptr && destroy != nullptr) {
                env->CallVoidMethod(tx, setLifecycle, destroy);
                env->CallVoidMethod(appThread, scheduleM, tx);
            }
            const bool threw = wl_report_exception(env,
                "wl_destroy_finished_direct_launch_activities");
            LOGI("[WESTLAKE-DIRECTFINISH-735] scheduled=%d index=%d token=%p",
                 threw ? 0 : 1, (int)i, token);
            if (result != nullptr) env->DeleteLocalRef(result);
            if (emptyResults != nullptr) env->DeleteLocalRef(emptyResults);
            if (destroy != nullptr) env->DeleteLocalRef(destroy);
            if (tx != nullptr) env->DeleteLocalRef(tx);
        }
        if (activityCls != nullptr) env->DeleteLocalRef(activityCls);
        if (activity != nullptr) env->DeleteLocalRef(activity);
        if (recordCls != nullptr) env->DeleteLocalRef(recordCls);
        env->DeleteLocalRef(record);
        env->DeleteLocalRef(token);
    }
}

// ============================================================
// dispatchKeyViaViewRoot — direct in-process key dispatch
// ============================================================
// The deployed runtime 16e08711 renders noice but its InputChannel consumer
// (android_view_InputEventReceiver.cpp) DROPS type=KEY messages (the KEY case
// is a "not yet dispatched" stub), so writeKeyEvent's bytes are read and
// thrown away. Rather than rebuild that runtime (its source is gone; the only
// rebuildable runtime regresses noice's init), we dispatch the key ourselves
// from the bridge, which runs in-process with JNI access:
//   1. build android.view.KeyEvent (10-arg ctor; KeyEvent natives are
//      registered in 16e08711 via register_android_view_KeyEvent),
//   2. find the focused ViewRootImpl's mInputEventReceiver via
//      WindowManagerGlobal.mRoots reflection,
//   3. hand it to adapter.window.InputEventBridge.dispatchOnMainThread — the
//      SAME helper the MOTION path uses — which posts onto the main looper and
//      calls receiver.dispatchInputEvent(seq, event) → ViewRootImpl input
//      stages → View.dispatchKeyEvent (focus nav / BACK / etc.).
// ============================================================
int32_t OHInputBridge::dispatchKeyViaViewRoot(int32_t action, int32_t keyCode,
                                              int64_t downTimeNs,
                                              int64_t eventTimeNs,
                                              int32_t metaState) {
    if (!jvm_) { LOGE("dispatchKeyViaViewRoot: no JavaVM"); return -1; }
    JNIEnv* env = nullptr;
    bool needDetach = false;
    if (jvm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        JavaVMAttachArgs args{JNI_VERSION_1_6, "oh-key-dispatch", nullptr};
        if (jvm_->AttachCurrentThread(&env, &args) != JNI_OK) {
            LOGE("dispatchKeyViaViewRoot: AttachCurrentThread failed");
            return -1;
        }
        needDetach = true;
    }
    auto fail = [&](const char* why) -> int32_t {
        wl_report_exception(env, "dispatchKeyViaViewRoot");   // §414: ExceptionDescribe is a no-op here
        LOGE("dispatchKeyViaViewRoot: %s", why);
        if (needDetach) jvm_->DetachCurrentThread();
        return -1;
    };

    wl_ensure_looper(env);   // §414: key handlers animate too (same trap as §411)
    // --- 1. KeyEvent(downMs, evtMs, action, code, repeat, meta, devId, scan, flags, source) ---
    jclass keCls = env->FindClass("android/view/KeyEvent");
    if (!keCls || env->ExceptionCheck()) return fail("FindClass KeyEvent");
    jmethodID keCtor = env->GetMethodID(keCls, "<init>", "(JJIIIIIIII)V");
    if (!keCtor || env->ExceptionCheck()) return fail("KeyEvent ctor");
    jlong downMs = downTimeNs / 1000000LL;
    jlong evtMs  = eventTimeNs / 1000000LL;
    if (downMs == 0 || evtMs == 0) {   // §414: the side-channel passes 0; use real uptime
        jclass scCls = env->FindClass("android/os/SystemClock");
        jmethodID upmM = scCls ? env->GetStaticMethodID(scCls, "uptimeMillis", "()J") : nullptr;
        if (upmM != nullptr) {
            const jlong now = env->CallStaticLongMethod(scCls, upmM);
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (downMs == 0) downMs = now;
            if (evtMs == 0) evtMs = now;
        }
    }
    jobject keyEvent = env->NewObject(
        keCls, keCtor, downMs, evtMs, (jint)action, (jint)keyCode,
        (jint)0 /*repeat*/, (jint)metaState, (jint)-1 /*deviceId=VIRTUAL*/,
        (jint)0 /*scancode*/, (jint)0 /*flags*/, (jint)0x101 /*SOURCE_KEYBOARD*/);
    if (!keyEvent || env->ExceptionCheck()) return fail("NewObject KeyEvent");

    // --- 2. Focused ViewRootImpl receiver via WindowManagerGlobal.mRoots ---
    jclass wmgCls = env->FindClass("android/view/WindowManagerGlobal");
    if (!wmgCls || env->ExceptionCheck()) return fail("FindClass WindowManagerGlobal");
    jmethodID getInst = env->GetStaticMethodID(
        wmgCls, "getInstance", "()Landroid/view/WindowManagerGlobal;");
    if (!getInst || env->ExceptionCheck()) return fail("WMG.getInstance id");
    jobject wmg = env->CallStaticObjectMethod(wmgCls, getInst);
    if (!wmg || env->ExceptionCheck()) return fail("WMG.getInstance call");
    jfieldID mRootsF = env->GetFieldID(wmgCls, "mRoots", "Ljava/util/ArrayList;");
    if (!mRootsF || env->ExceptionCheck()) return fail("WMG.mRoots field");
    jobject roots = env->GetObjectField(wmg, mRootsF);
    if (!roots) return fail("WMG.mRoots null");

    jclass listCls = env->FindClass("java/util/ArrayList");
    jmethodID sizeM = env->GetMethodID(listCls, "size", "()I");
    jmethodID getM  = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
    jint n = env->CallIntMethod(roots, sizeM);

    jclass vriCls = env->FindClass("android/view/ViewRootImpl");
    if (!vriCls || env->ExceptionCheck()) return fail("FindClass ViewRootImpl");
    jfieldID mViewF = env->GetFieldID(vriCls, "mView", "Landroid/view/View;");
    jfieldID recvF  = env->GetFieldID(vriCls, "mInputEventReceiver",
                          "Landroid/view/ViewRootImpl$WindowInputEventReceiver;");
    if (!mViewF || !recvF || env->ExceptionCheck()) return fail("ViewRootImpl fields");
    jclass viewCls = env->FindClass("android/view/View");
    jmethodID hasFocusM = env->GetMethodID(viewCls, "hasWindowFocus", "()Z");

    jobject receiver = nullptr;   // chosen WindowInputEventReceiver
    jobject fallbackRecv = nullptr;
    // WESTLAKE §405: also keep the owning ViewRootImpl — adapter/window/InputEventBridge is a BCP
    // class that is NOT deployed on this board, so without a fallback every key (notably BACK, which
    // is how you leave a detail page) failed at "FindClass InputEventBridge".
    // ViewRootImpl.enqueueInputEvent(InputEvent) is public and hops to the UI thread by itself.
    jobject vriMatch = nullptr, vriFallback = nullptr;
    jobject decorView = nullptr, fallbackView = nullptr;
    int chosenRootK = -1, fallbackIdxK = -1;   // §552: report WHICH window got the key
    int chosenPriorityK = 0;
    for (jint i = 0; i < n; ++i) {
        jobject vri = env->CallObjectMethod(roots, getM, i);
        if (!vri) continue;
        jobject recv = env->GetObjectField(vri, recvF);
        jobject view = env->GetObjectField(vri, mViewF);
        if (view) {
            const int wantK = g_rootIndex.load();          // §408
            const int priority = wl_root_target_priority(
                env, view, viewCls, hasFocusM, wantK, i);
            if (priority > 0 && priority >= chosenPriorityK) {
                // WESTLAKE §552: keep the LAST match, not the first — the same rule the TOUCH path
                // already follows (§460). mRoots APPENDS, so a dialog is always a later entry than
                // the Activity beneath it, and the Activity's DecorView still reports isShown()==true.
                // Breaking on the first match sent every KEY to the Activity window, which is why
                // typing into a dialog's EditText did nothing and logged
                //     dispatchKeyViaViewRoot: ... DecorView.dispatchKeyEvent handled=0
                // while the touch that focused that very field had correctly gone to root[1].
                if (receiver)  env->DeleteLocalRef(receiver);
                if (vriMatch)  env->DeleteGlobalRef(vriMatch);
                if (decorView) env->DeleteGlobalRef(decorView);
                receiver  = recv ? env->NewLocalRef(recv) : nullptr;
                vriMatch  = env->NewGlobalRef(vri);
                decorView = env->NewGlobalRef(view);
                chosenRootK = (int)i;
                chosenPriorityK = priority;
            } else if (!vriFallback) {
                if (recv && !fallbackRecv) fallbackRecv = env->NewLocalRef(recv);
                vriFallback = env->NewGlobalRef(vri);
                fallbackView = env->NewGlobalRef(view);
                fallbackIdxK = (int)i;
            }
        }
        if (recv) env->DeleteLocalRef(recv);
        if (view) env->DeleteLocalRef(view);
        env->DeleteLocalRef(vri);
        // §552: no early break — we want the LAST matching root (topmost window).
    }
    if (!receiver) receiver = fallbackRecv;   // no focused window -> best effort
    if (!vriMatch) { vriMatch = vriFallback; chosenRootK = fallbackIdxK; }
    if (!decorView) decorView = fallbackView;
    if (!receiver && !vriMatch && !decorView) { env->DeleteLocalRef(keyEvent); return fail("no ViewRootImpl receiver"); }
    // §552: name the window, for the same reason §460 added it to the touch path — "handled=0" from
    // the wrong root reads exactly like a broken widget.
    LOGI("dispatchKeyViaViewRoot: chose root[%d] of %d (g_rootIndex=%d)",
         chosenRootK, (int)n, g_rootIndex.load());

    // --- 3a. preferred: DecorView.dispatchKeyEvent (§405b) ---
    // Same reasoning as the touch path: enqueueInputEvent runs the event through ViewRootImpl's
    // input stages, and on this board those reach services that do not exist (autofill ->
    // UserManager) and kill the process.  DecorView.dispatchKeyEvent goes straight to the Window
    // callback (the Activity), which is what BACK actually needs.
    // §556: a KEY-ONLY switch. WL_TOUCH_ENQUEUE flips touch as well, and touch currently works
    // (§554), so testing the ViewRootImpl route for keys must not disturb it.
    // Keep the key experiment independent from the touch experiment.  A launch
    // that opts touch into ViewRootImpl must not silently move BACK off the
    // preferred DecorView.dispatchKeyEvent path as well.
    static const bool wl_keyEnqueue = getenv("WL_KEY_ENQUEUE") != nullptr;
    if (decorView != nullptr && !wl_keyEnqueue) {
        jmethodID dkeM = env->GetMethodID(viewCls, "dispatchKeyEvent",   // §408c
            "(Landroid/view/KeyEvent;)Z");
        if (!dkeM || env->ExceptionCheck()) { env->ExceptionClear(); dkeM = nullptr; }
        if (dkeM != nullptr) {
            jboolean handled = env->CallBooleanMethod(decorView, dkeM, keyEvent);
            const bool dispatchThrew = wl_report_exception(
                env, "dispatchKeyViaViewRoot DecorView.dispatchKeyEvent");
            if (!dispatchThrew) {
                LOGI("dispatchKeyViaViewRoot: action=%d keyCode=%d -> DecorView.dispatchKeyEvent handled=%d",
                     action, keyCode, (int)handled);
                if (action == 1 /* ACTION_UP */ && keyCode == 4 /* KEYCODE_BACK */) {
                    wl_destroy_finished_direct_launch_activities(env);
                }
                env->DeleteLocalRef(keyEvent);
                if (decorView) env->DeleteGlobalRef(decorView);   // §406b
                if (vriMatch)  env->DeleteGlobalRef(vriMatch);
                if (needDetach) jvm_->DetachCurrentThread();
                return 0;
            }

            // The standalone interpreter can throw UndeclaredThrowableException
            // while invoking the Window.Callback interface specifically for a
            // BACK key-up.  DOWN has already traversed the normal callback, but
            // enqueueing UP behind the ViewRoot sync barrier never completes the
            // gesture.  Recover only this exact case by invoking the owning
            // Activity's public back contract directly, then run the existing
            // direct-launch destroy completion for Activities that marked
            // themselves finished.  WebView history handlers and app overrides
            // still run through Activity.onBackPressed().
            if (action == 1 /* ACTION_UP */ && keyCode == 4 /* KEYCODE_BACK */) {
                // DecorView.getContext() is commonly a ContextThemeWrapper, not
                // the Activity itself.  The stock key path reaches the owning
                // Activity through PhoneWindow's Window.Callback, so recover
                // that same object rather than guessing through ContextWrapper
                // layers.
                jclass decorCls = env->GetObjectClass(decorView);
                jfieldID windowF = decorCls != nullptr
                    ? env->GetFieldID(decorCls, "mWindow",
                        "Lcom/android/internal/policy/PhoneWindow;")
                    : nullptr;
                jobject window = windowF != nullptr
                    ? env->GetObjectField(decorView, windowF) : nullptr;
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    window = nullptr;
                }
                jclass windowCls = env->FindClass("android/view/Window");
                jmethodID getCallbackM = windowCls != nullptr
                    ? env->GetMethodID(windowCls, "getCallback",
                        "()Landroid/view/Window$Callback;")
                    : nullptr;
                jobject context = window != nullptr && getCallbackM != nullptr
                    ? env->CallObjectMethod(window, getCallbackM) : nullptr;
                jclass activityCls = env->FindClass("android/app/Activity");
                if (!env->ExceptionCheck() && context != nullptr &&
                        activityCls != nullptr &&
                        env->IsInstanceOf(context, activityCls)) {
                    jmethodID onBackPressedM = env->GetMethodID(
                        activityCls, "onBackPressed", "()V");
                    if (!env->ExceptionCheck() && onBackPressedM != nullptr) {
                        env->CallVoidMethod(context, onBackPressedM);
                        const bool backThrew = wl_report_exception(
                            env, "dispatchKeyViaViewRoot Activity.onBackPressed");
                        LOGI("[WESTLAKE-DIRECTBACK] fallback invoked=%d",
                             backThrew ? 0 : 1);
                        if (!backThrew) {
                            wl_destroy_finished_direct_launch_activities(env);
                            env->DeleteLocalRef(context);
                            env->DeleteLocalRef(activityCls);
                            if (windowCls != nullptr) env->DeleteLocalRef(windowCls);
                            if (window != nullptr) env->DeleteLocalRef(window);
                            if (decorCls != nullptr) env->DeleteLocalRef(decorCls);
                            env->DeleteLocalRef(keyEvent);
                            if (decorView) env->DeleteGlobalRef(decorView);
                            if (vriMatch) env->DeleteGlobalRef(vriMatch);
                            if (needDetach) jvm_->DetachCurrentThread();
                            return 0;
                        }
                    }
                }
                if (env->ExceptionCheck()) {
                    wl_report_exception(env,
                        "dispatchKeyViaViewRoot Activity.onBackPressed lookup");
                }
                if (context != nullptr) env->DeleteLocalRef(context);
                if (activityCls != nullptr) env->DeleteLocalRef(activityCls);
                if (windowCls != nullptr) env->DeleteLocalRef(windowCls);
                if (window != nullptr) env->DeleteLocalRef(window);
                if (decorCls != nullptr) env->DeleteLocalRef(decorCls);
            }
        }
    }

    // --- 3b. ViewRootImpl.enqueueInputEvent (no BCP helper needed) ---
    if (vriMatch != nullptr) {
        jmethodID enqueueM = env->GetMethodID(vriCls, "enqueueInputEvent",
            "(Landroid/view/InputEvent;)V");
        if (!enqueueM || env->ExceptionCheck()) { env->ExceptionClear(); enqueueM = nullptr; }
        if (enqueueM != nullptr) {
            env->CallVoidMethod(vriMatch, enqueueM, keyEvent);
            if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
            else {
                LOGI("dispatchKeyViaViewRoot: action=%d keyCode=%d -> enqueueInputEvent OK",
                     action, keyCode);
                env->DeleteLocalRef(keyEvent);
                if (needDetach) jvm_->DetachCurrentThread();
                return 0;
            }
        }
    }

    // --- 3c. dispatchOnMainThread(receiver, seq, keyEvent) ---
    if (!receiver) { env->DeleteLocalRef(keyEvent); return fail("no ViewRootImpl receiver"); }
    jclass iebCls = env->FindClass("adapter/window/InputEventBridge");
    if (!iebCls || env->ExceptionCheck()) { env->ExceptionClear(); return fail("FindClass InputEventBridge"); }
    jmethodID domt = env->GetStaticMethodID(iebCls, "dispatchOnMainThread",
        "(Landroid/view/InputEventReceiver;ILandroid/view/InputEvent;)V");
    if (!domt || env->ExceptionCheck()) return fail("dispatchOnMainThread id");
    static std::atomic<int32_t> s_keySeq{0x40000000};
    jint seq = s_keySeq.fetch_add(1);
    env->CallStaticVoidMethod(iebCls, domt, receiver, seq, keyEvent);
    if (env->ExceptionCheck()) return fail("dispatchOnMainThread call");

    LOGI("dispatchKeyViaViewRoot: action=%d keyCode=%d seq=%d -> ViewRootImpl OK",
         action, keyCode, seq);
    env->DeleteLocalRef(keyEvent);
    if (needDetach) jvm_->DetachCurrentThread();
    return 0;
}

// ============================================================
// dispatchCharactersViaViewRoot — commit a text STRING via one KeyEvent
// ============================================================
// Per-key events don't type on this runtime: TextView's KeyListener needs
// KeyEvent.getUnicodeChar(), which loads a KeyCharacterMap for the (virtual)
// device — that map isn't resolvable here, so unicode comes back 0 and nothing
// inserts. The classic workaround (how soft IMEs commit text via key events):
// build an ACTION_MULTIPLE KeyEvent that CARRIES the characters string via the
// KeyEvent(long downTime, String characters, int deviceId, int flags) ctor.
// ViewRootImpl delivers it to the focused view; TextView.onKeyMultiple(
// KEYCODE_UNKNOWN, ...) reads event.getCharacters() and inserts it directly —
// no KeyCharacterMap, no per-char mapping. Same in-process ViewRootImpl bypass,
// same main-thread post (InputEventBridge.dispatchOnMainThread) as the key path.
int32_t OHInputBridge::dispatchCharactersViaViewRoot(const char* utf8) {
    if (!jvm_ || !utf8 || !*utf8) return -1;
    JNIEnv* env = nullptr;
    bool needDetach = false;
    if (jvm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        JavaVMAttachArgs args{JNI_VERSION_1_6, "oh-text-dispatch", nullptr};
        if (jvm_->AttachCurrentThread(&env, &args) != JNI_OK) return -1;
        needDetach = true;
    }
    auto fail = [&](const char* why) -> int32_t {
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        LOGE("dispatchCharactersViaViewRoot: %s", why);
        if (needDetach) jvm_->DetachCurrentThread();
        return -1;
    };

    // KeyEvent(long downTime, String characters, int deviceId, int flags)
    // -> ACTION_MULTIPLE, keyCode=KEYCODE_UNKNOWN, carries the characters.
    jclass keCls = env->FindClass("android/view/KeyEvent");
    if (!keCls || env->ExceptionCheck()) return fail("FindClass KeyEvent");
    jmethodID keCtor = env->GetMethodID(keCls, "<init>", "(JLjava/lang/String;II)V");
    if (!keCtor || env->ExceptionCheck()) return fail("KeyEvent(String) ctor");
    jstring jchars = env->NewStringUTF(utf8);
    jobject keyEvent = env->NewObject(keCls, keCtor, (jlong)0, jchars,
                                      (jint)-1 /*VIRTUAL*/, (jint)0 /*flags*/);
    if (!keyEvent || env->ExceptionCheck()) return fail("NewObject KeyEvent(String)");

    // Focused ViewRootImpl receiver (same scan as dispatchKeyViaViewRoot).
    jclass wmgCls = env->FindClass("android/view/WindowManagerGlobal");
    jmethodID getInst = env->GetStaticMethodID(wmgCls, "getInstance", "()Landroid/view/WindowManagerGlobal;");
    jobject wmg = env->CallStaticObjectMethod(wmgCls, getInst);
    if (!wmg || env->ExceptionCheck()) return fail("WMG.getInstance");
    jfieldID mRootsF = env->GetFieldID(wmgCls, "mRoots", "Ljava/util/ArrayList;");
    jobject roots = env->GetObjectField(wmg, mRootsF);
    if (!roots) return fail("mRoots null");
    jclass listCls = env->FindClass("java/util/ArrayList");
    jmethodID sizeM = env->GetMethodID(listCls, "size", "()I");
    jmethodID getM = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
    jint n = env->CallIntMethod(roots, sizeM);
    jclass vriCls = env->FindClass("android/view/ViewRootImpl");
    jfieldID mViewF = env->GetFieldID(vriCls, "mView", "Landroid/view/View;");
    jfieldID recvF = env->GetFieldID(vriCls, "mInputEventReceiver",
                        "Landroid/view/ViewRootImpl$WindowInputEventReceiver;");
    jclass viewCls = env->FindClass("android/view/View");
    jmethodID hasFocusM = env->GetMethodID(viewCls, "hasWindowFocus", "()Z");
    // WESTLAKE §552: THIS is the function that types (the per-key `tapKey` path below it is only a
    // fallback that never runs, because this one reports success). It used to pick the FIRST root
    // whose view returns hasWindowFocus(), and otherwise the FIRST root of all — which on this port
    // is always the Activity, never the dialog stacked on top of it. So typing into a dialog's
    // EditText dispatched to the wrong window and logged a cheerful "-> ViewRootImpl OK" while
    // nothing changed. Same defect the TOUCH path already fixed in §460, and the same cure:
    // honour an explicit g_rootIndex, accept isShown() as well as focus, and keep the LAST match
    // (mRoots APPENDS, so the topmost window is the last entry).
    jobject receiver = nullptr, fallbackRecv = nullptr;
    jobject decorView = nullptr, fallbackView = nullptr;
    int chosenRootC = -1, fallbackIdxC = -1;
    int chosenPriorityC = 0;
    for (jint i = 0; i < n; ++i) {
        jobject vri = env->CallObjectMethod(roots, getM, i);
        if (!vri) continue;
        jobject recv = env->GetObjectField(vri, recvF);
        jobject view = env->GetObjectField(vri, mViewF);
        if (recv && view) {
            const int wantC = g_rootIndex.load();
            const int priority = wl_root_target_priority(
                env, view, viewCls, hasFocusM, wantC, i);
            if (priority > 0 && priority >= chosenPriorityC) {
                if (receiver) env->DeleteLocalRef(receiver);
                if (decorView) env->DeleteGlobalRef(decorView);
                receiver = env->NewLocalRef(recv);
                decorView = env->NewGlobalRef(view);
                chosenRootC = (int)i;
                chosenPriorityC = priority;
            } else if (!fallbackRecv) {
                fallbackRecv = env->NewLocalRef(recv);
                fallbackView = env->NewGlobalRef(view);
                fallbackIdxC = (int)i;
            }
        }
        if (recv) env->DeleteLocalRef(recv);
        if (view) env->DeleteLocalRef(view);
        env->DeleteLocalRef(vri);
        // §552: no early break — keep the LAST match.
    }
    if (!receiver) {
        receiver = fallbackRecv;
        decorView = fallbackView;
        chosenRootC = fallbackIdxC;
    } else if (fallbackView) {
        env->DeleteGlobalRef(fallbackView);
    }
    if (!receiver) { env->DeleteLocalRef(keyEvent); return fail("no ViewRootImpl receiver"); }
    LOGI("dispatchCharactersViaViewRoot: chose root[%d] of %d (g_rootIndex=%d)",
         chosenRootC, (int)n, g_rootIndex.load());

    // WESTLAKE: commit through the focused TextView on the Android main thread.
    // Synthetic virtual-key KeyEvents cannot type on this board because their
    // KeyCharacterMap lookup returns unicode 0.  OHTouchInjector is a boot-class
    // helper shared by every app, so this stays a generic OH input-boundary fix.
    jclass injectorCls = env->FindClass("adapter/window/OHTouchInjector");
    jmethodID commitM = nullptr;
    const char* helperKind = "boot";
    if (injectorCls != nullptr) {
        commitM = env->GetStaticMethodID(injectorCls,
            "commitFocusedTextOnMain", "(Landroid/view/View;Ljava/lang/String;)V");
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        commitM = nullptr;
    }

    // Bring-up path: keep the verified boot image untouched and load the generic
    // main-thread text helper from a tiny standalone dex jar.  Parent delegation
    // still resolves every android.* type from the boot class loader; only
    // adapter.window.OHTextInjector comes from this bridge-owned jar.
    jobject helperClassObject = nullptr;
    if (commitM == nullptr) {
        jclass classLoaderCls = env->FindClass("java/lang/ClassLoader");
        jclass pathLoaderCls = env->FindClass("dalvik/system/PathClassLoader");
        if (classLoaderCls != nullptr && pathLoaderCls != nullptr && !env->ExceptionCheck()) {
            jmethodID getSystemM = env->GetStaticMethodID(
                classLoaderCls, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
            jmethodID pathCtor = env->GetMethodID(
                pathLoaderCls, "<init>", "(Ljava/lang/String;Ljava/lang/ClassLoader;)V");
            jmethodID loadClassM = env->GetMethodID(
                classLoaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
            if (getSystemM != nullptr && pathCtor != nullptr && loadClassM != nullptr &&
                !env->ExceptionCheck()) {
                jobject parent = env->CallStaticObjectMethod(classLoaderCls, getSystemM);
                jstring jarPath = env->NewStringUTF(
                    "/data/local/tmp/asx/focused-text-helper.jar");
                jobject loader = env->NewObject(pathLoaderCls, pathCtor, jarPath, parent);
                jstring helperName = env->NewStringUTF("adapter.window.OHTextInjector");
                if (loader != nullptr && !env->ExceptionCheck()) {
                    helperClassObject = env->CallObjectMethod(loader, loadClassM, helperName);
                }
                if (!env->ExceptionCheck() && helperClassObject != nullptr) {
                    injectorCls = static_cast<jclass>(helperClassObject);
                    commitM = env->GetStaticMethodID(injectorCls,
                        "commitFocusedTextOnMain",
                        "(Landroid/view/View;Ljava/lang/String;)V");
                    helperKind = "dex";
                }
            }
        }
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            commitM = nullptr;
        }
    }

    if (commitM != nullptr && decorView != nullptr) {
        env->CallStaticVoidMethod(injectorCls, commitM, decorView, jchars);
        if (!env->ExceptionCheck()) {
            LOGI("dispatchCharactersViaViewRoot: \"%s\" -> focused TextView main-thread commit posted (%s helper)",
                 utf8, helperKind);
            env->DeleteGlobalRef(decorView);
            env->DeleteLocalRef(keyEvent);
            if (needDetach) jvm_->DetachCurrentThread();
            return 0;
        }
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    if (decorView) env->DeleteGlobalRef(decorView);

    jclass iebCls = env->FindClass("adapter/window/InputEventBridge");
    jmethodID domt = env->GetStaticMethodID(iebCls, "dispatchOnMainThread",
        "(Landroid/view/InputEventReceiver;ILandroid/view/InputEvent;)V");
    if (!domt || env->ExceptionCheck()) return fail("dispatchOnMainThread id");
    static std::atomic<int32_t> s_txtSeq{0x50000000};
    jint seq = s_txtSeq.fetch_add(1);
    env->CallStaticVoidMethod(iebCls, domt, receiver, seq, keyEvent);
    if (env->ExceptionCheck()) return fail("dispatchOnMainThread call");
    LOGI("dispatchCharactersViaViewRoot: \"%s\" seq=%d -> ViewRootImpl OK", utf8, seq);
    env->DeleteLocalRef(keyEvent);
    if (needDetach) jvm_->DetachCurrentThread();
    return 0;
}

// ============================================================
// VelocityTracker JNI stub
// ============================================================
// The deployed runtime (liboh_android_runtime.so) never registers
// android_view_VelocityTracker JNI (no register_android_view_VelocityTracker,
// no impl symbols). RecyclerView / ScrollView / GestureDetector touch handling
// calls VelocityTracker.obtain()->nativeInitialize(int) on the FIRST touch,
// which throws UnsatisfiedLinkError and aborts View.dispatchTouchEvent
// mid-traversal — so no click/scroll ever lands. We register no-op stubs for
// the 7 native methods the deployed framework.jar declares (verified against
// device framework.jar md5 e92991b0): velocity always reads back 0 (no fling),
// but DOWN/MOVE/UP dispatch + click detection + scrolling all work.
static jlong    VT_nativeInitialize(JNIEnv*, jclass, jint) { return (jlong)1; }
static void     VT_nativeAddMovement(JNIEnv*, jclass, jlong, jobject) {}
static void     VT_nativeClear(JNIEnv*, jclass, jlong) {}
static void     VT_nativeComputeCurrentVelocity(JNIEnv*, jclass, jlong, jint, jfloat) {}
static void     VT_nativeDispose(JNIEnv*, jclass, jlong) {}
static jfloat   VT_nativeGetVelocity(JNIEnv*, jclass, jlong, jint, jint) { return 0.0f; }
static jboolean VT_nativeIsAxisSupported(JNIEnv*, jclass, jint) { return JNI_FALSE; }

static void ensureVelocityTrackerStub(JNIEnv* env) {
    static std::atomic<bool> done{false};
    if (done.load()) return;
    jclass vt = env->FindClass("android/view/VelocityTracker");
    if (!vt || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    static const JNINativeMethod methods[] = {
        {"nativeInitialize", "(I)J", (void*)VT_nativeInitialize},
        {"nativeAddMovement", "(JLandroid/view/MotionEvent;)V", (void*)VT_nativeAddMovement},
        {"nativeClear", "(J)V", (void*)VT_nativeClear},
        {"nativeComputeCurrentVelocity", "(JIF)V", (void*)VT_nativeComputeCurrentVelocity},
        {"nativeDispose", "(J)V", (void*)VT_nativeDispose},
        {"nativeGetVelocity", "(JII)F", (void*)VT_nativeGetVelocity},
        {"nativeIsAxisSupported", "(I)Z", (void*)VT_nativeIsAxisSupported},
    };
    jint rc = env->RegisterNatives(vt, methods, 7);
    if (env->ExceptionCheck()) env->ExceptionClear();
    LOGI("ensureVelocityTrackerStub: RegisterNatives rc=%d (7 methods)", (int)rc);
    if (rc == 0) done.store(true);
    env->DeleteLocalRef(vt);
}

// ============================================================
// resolveTouchInjector — find the OHTouchInjector helper class
// ============================================================
// Resolution order (so EVERY app gets touch, not just noice):
//   1. BCP class adapter/window/OHTouchInjector via env->FindClass — FindClass
//      uses the system/boot classloader, visible to every AOSP app on the
//      device, so this is the universal path once the class is in the BCP jar.
//   2. FALLBACK: the per-app com.github.ashutoshgngwr.noice.OHTouchInjector via
//      the focused window's app classloader (keeps noice working even if the
//      BCP class is somehow absent / not yet deployed).
// Both expose the identical static dispatchTouchOnMain(View, MotionEvent).
// Returns a local-ref jclass on success, nullptr on failure (no pending
// exception left set). `viewCls` is android/view/View (already resolved by the
// caller); `decorView` is the focused decor View used to reach its classloader.
// ============================================================
// ============================================================
// WESTLAKE §408 — window (ViewRootImpl) selection for injected input
// ============================================================
// noice keeps SEVERAL windows alive at once (MainActivity + AppIntroActivity + popups), and none of
// them holds real WMS focus on this board, so `hasWindowFocus()` picks nothing and the scan falls
// back to mRoots[0] — which is often NOT the window that is actually on screen.  Injected taps then
// land in an invisible window and nothing happens (the app survives, the UI just never changes).
// g_rootIndex lets the side-channel aim at a specific window; -1 keeps the old auto behaviour.

// Log every ViewRootImpl: index, decor-view class, size, focus flag and window title, so the
// harness can see which window is which before aiming at one.
void wl_dump_view_roots(JNIEnv* env);
void wl_dump_view_roots(JNIEnv* env) {
    jclass wmgCls = env->FindClass("android/view/WindowManagerGlobal");
    if (!wmgCls || env->ExceptionCheck()) { env->ExceptionClear(); LOGE("dumpViewRoots: no WMG"); return; }
    jmethodID getInst = env->GetStaticMethodID(wmgCls, "getInstance", "()Landroid/view/WindowManagerGlobal;");
    jobject wmg = env->CallStaticObjectMethod(wmgCls, getInst);
    if (!wmg || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jfieldID mRootsF = env->GetFieldID(wmgCls, "mRoots", "Ljava/util/ArrayList;");
    jobject roots = env->GetObjectField(wmg, mRootsF);
    if (!roots) return;
    jclass listCls = env->FindClass("java/util/ArrayList");
    jmethodID sizeM = env->GetMethodID(listCls, "size", "()I");
    jmethodID getM  = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
    jint nn = env->CallIntMethod(roots, sizeM);
    jclass vriCls = env->FindClass("android/view/ViewRootImpl");
    jfieldID mViewF = env->GetFieldID(vriCls, "mView", "Landroid/view/View;");
    jclass viewCls = env->FindClass("android/view/View");
    jmethodID hasFocusM = env->GetMethodID(viewCls, "hasWindowFocus", "()Z");
    jmethodID getWM = env->GetMethodID(viewCls, "getMeasuredWidth", "()I");
    jmethodID getHM = env->GetMethodID(viewCls, "getMeasuredHeight", "()I");
    jmethodID isShownM = env->GetMethodID(viewCls, "isShown", "()Z");
    jmethodID getVisM = env->GetMethodID(viewCls, "getVisibility", "()I");
    jclass clsCls = env->FindClass("java/lang/Class");
    jmethodID getNameM = env->GetMethodID(clsCls, "getName", "()Ljava/lang/String;");
    LOGI("dumpViewRoots: %d root(s)", (int)nn);
    for (jint i = 0; i < nn; ++i) {
        jobject vri = env->CallObjectMethod(roots, getM, i);
        if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
        if (!vri) continue;
        jobject view = env->GetObjectField(vri, mViewF);
        if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(vri); continue; }
        const char* cn = nullptr; jstring jn = nullptr;
        jint w = -1, h = -1, vis = -1; jboolean foc = JNI_FALSE, shown = JNI_FALSE;
        if (view) {
            jclass vc = env->GetObjectClass(view);
            jn = vc ? static_cast<jstring>(env->CallObjectMethod(vc, getNameM)) : nullptr;
            cn = jn ? env->GetStringUTFChars(jn, nullptr) : nullptr;
            foc = env->CallBooleanMethod(view, hasFocusM);
            w = env->CallIntMethod(view, getWM);
            h = env->CallIntMethod(view, getHM);
            shown = env->CallBooleanMethod(view, isShownM);
            vis = env->CallIntMethod(view, getVisM);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        LOGI("  root[%d] view=%s %dx%d focus=%d shown=%d vis=%d",
             (int)i, cn ? cn : "(null)", (int)w, (int)h, (int)foc, (int)shown, (int)vis);
        if (jn && cn) env->ReleaseStringUTFChars(jn, cn);
        if (view) env->DeleteLocalRef(view);
        env->DeleteLocalRef(vri);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// WESTLAKE §411: give the injector thread a Looper.
// Material widgets start a ripple/state ValueAnimator from onTouchEvent, and ValueAnimator.start()
// begins with `if (Looper.myLooper() == null) throw new AndroidRuntimeException("Animators may only
// be run on Looper threads")`.  Thrown from our dispatch thread that exception unwinds the whole
// dispatch, so the DOWN never reaches the button and every tap reported handled=0.
// We only need myLooper() to be non-null; we never run the loop, so the animation simply does not
// advance — the click itself proceeds normally.
static void wl_ensure_looper(JNIEnv* env) {
    jclass loopCls = env->FindClass("android/os/Looper");
    if (!loopCls || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jmethodID myLooper = env->GetStaticMethodID(loopCls, "myLooper", "()Landroid/os/Looper;");
    if (!myLooper || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jobject lp = env->CallStaticObjectMethod(loopCls, myLooper);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
    if (lp != nullptr) { env->DeleteLocalRef(lp); return; }
    jmethodID prep = env->GetStaticMethodID(loopCls, "prepare", "()V");
    if (prep != nullptr) {
        env->CallStaticVoidMethod(loopCls, prep);
        if (env->ExceptionCheck()) { env->ExceptionClear(); }
        else { LOGI("wl_ensure_looper: Looper.prepare() on injector thread"); }
    }
    env->DeleteLocalRef(loopCls);
}

// WESTLAKE §410: report a pending Java exception AS TEXT.
// env->ExceptionDescribe() prints nothing in this runtime — Throwable.printStackTrace is a
// fork-safe no-op ("[RT] Throwable.printStackTrace (fork-safe noop)"), so an exception thrown
// inside a dispatched touch was indistinguishable from "the view tree ignored it" (handled=0).
static bool wl_report_exception(JNIEnv* env, const char* where) {
    if (!env->ExceptionCheck()) return false;
    jthrowable exc = env->ExceptionOccurred();
    env->ExceptionClear();
    if (exc == nullptr) { LOGE("%s: exception (undescribable)", where); return true; }
    jclass thCls = env->GetObjectClass(exc);
    jmethodID toStr = thCls ? env->GetMethodID(thCls, "toString", "()Ljava/lang/String;") : nullptr;
    jstring js = toStr ? static_cast<jstring>(env->CallObjectMethod(exc, toStr)) : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();
    const char* cs = js ? env->GetStringUTFChars(js, nullptr) : nullptr;
    LOGE("%s: EXCEPTION %s", where, cs ? cs : "(no message)");
    // one frame of context is usually enough to name the culprit
    jmethodID getST = thCls ? env->GetMethodID(thCls, "getStackTrace",
        "()[Ljava/lang/StackTraceElement;") : nullptr;
    jobjectArray st = getST ? static_cast<jobjectArray>(env->CallObjectMethod(exc, getST)) : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (st != nullptr) {
        const jsize n = env->GetArrayLength(st);
        jclass steCls = env->FindClass("java/lang/StackTraceElement");
        jmethodID steStr = steCls ? env->GetMethodID(steCls, "toString", "()Ljava/lang/String;") : nullptr;
        for (jsize i = 0; i < n && i < 12; ++i) {
            jobject e = env->GetObjectArrayElement(st, i);
            if (!e) continue;
            jstring es = steStr ? static_cast<jstring>(env->CallObjectMethod(e, steStr)) : nullptr;
            const char* ec = es ? env->GetStringUTFChars(es, nullptr) : nullptr;
            LOGE("    at %s", ec ? ec : "?");
            if (es && ec) env->ReleaseStringUTFChars(es, ec);
            if (es) env->DeleteLocalRef(es);
            env->DeleteLocalRef(e);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    if (js && cs) env->ReleaseStringUTFChars(js, cs);
    return true;
}

// ============================================================
// WESTLAKE §409 — dump the live view hierarchy of a window
// ============================================================
// "Tap every widget on every page" needs a map of what is actually on screen: this walks a window's
// DecorView and prints, per view, its class, resource-entry name, absolute on-screen rect, its text
// (TextView/Button) and its clickable/visible flags.  Coordinates are accumulated on the way down
// (parentX + getLeft() - parent.getScrollX()) rather than by calling getLocationOnScreen(), which
// allocates and re-enters the view from this injector thread.
// ALL method IDs are resolved on framework classes found via FindClass — never on a receiver's own
// class, because GetMethodID on e.g. DecorView drives ClassLinker::EnsureInitialized from this
// thread and that SEGVs in mirror::Class::GetDescriptor (§408c).
struct WlViewDumpCtx {
    jclass viewCls, vgCls, tvCls, webViewCls, webSettingsCls, clsCls, resCls;
    jmethodID getName, getId, getVis, isClickable, getW, getH, getLeft, getTop,
              getScrollX, getScrollY, getChildCount, getChildAt, getText, toString,
              getResources, getResourceEntryName, isEnabled,
              webGetUrl, webGetOriginalUrl, webGetTitle, webGetProgress,
              webGetContentHeight, webIsPaused, webGetSettings,
              settingsGetJavaScriptEnabled, settingsGetDomStorageEnabled, getLayerType,
              getWindowVisibility, isAttachedToWindow, isShown,
              // §555: focus diagnostics — a tap that lands but never focuses looks identical to a
              // tap that missed, and that ambiguity is what made the text-input bug unreadable.
              isFocused, isFocusableInTouchMode, isInTouchMode;
};

// WESTLAKE §688 — Toutiao's article-template state, read-only and app-scoped.
//
// A blank android.webkit.WebView is ambiguous: Chromium may have failed to commit a document, or
// the app may never have submitted one.  Toutiao keeps the state object for every article WebView
// in TemplateManager.c (WeakHashMap<WebView, KtX>).  Read that map through ordinary Java
// reflection and append the live state to the existing structural oracle.  This intentionally
// does not call KtX.h(), alter a field, or otherwise repair app state; on every other app the
// TemplateManager class lookup simply fails and the diagnostic is a no-op.
static void wl_append_toutiao_template_state(JNIEnv* env, jobject webView,
                                              char* extra, size_t extraSize) {
    if (webView == nullptr || extra == nullptr || extraSize == 0) return;

    jclass viewCls = env->FindClass("android/view/View");
    jclass ctxCls = env->FindClass("android/content/Context");
    jclass clCls = env->FindClass("java/lang/ClassLoader");
    jclass classCls = env->FindClass("java/lang/Class");
    jclass fieldCls = env->FindClass("java/lang/reflect/Field");
    jclass weakMapCls = env->FindClass("java/util/WeakHashMap");
    jclass mapCls = env->FindClass("java/util/Map");
    jclass integerCls = env->FindClass("java/lang/Integer");
    if (env->ExceptionCheck() || !viewCls || !ctxCls || !clCls || !classCls || !fieldCls ||
        !weakMapCls || !mapCls || !integerCls) {
        env->ExceptionClear();
        return;
    }

    jmethodID getContext = env->GetMethodID(viewCls, "getContext", "()Landroid/content/Context;");
    jmethodID getClassLoader = env->GetMethodID(ctxCls, "getClassLoader",
                                                "()Ljava/lang/ClassLoader;");
    jmethodID loadClass = env->GetMethodID(clCls, "loadClass",
                                           "(Ljava/lang/String;)Ljava/lang/Class;");
    jmethodID getDeclaredField = env->GetMethodID(classCls, "getDeclaredField",
                                                   "(Ljava/lang/String;)Ljava/lang/reflect/Field;");
    jmethodID getSuperclass = env->GetMethodID(classCls, "getSuperclass", "()Ljava/lang/Class;");
    jmethodID objectGetClass = env->GetMethodID(env->FindClass("java/lang/Object"), "getClass",
                                                "()Ljava/lang/Class;");
    jmethodID setAccessible = env->GetMethodID(fieldCls, "setAccessible", "(Z)V");
    jmethodID fieldGet = env->GetMethodID(fieldCls, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
    jmethodID fieldGetBoolean = env->GetMethodID(fieldCls, "getBoolean", "(Ljava/lang/Object;)Z");
    jmethodID fieldGetInt = env->GetMethodID(fieldCls, "getInt", "(Ljava/lang/Object;)I");
    jmethodID mapGet = env->GetMethodID(weakMapCls, "get",
                                        "(Ljava/lang/Object;)Ljava/lang/Object;");
    jmethodID genericMapGet = env->GetMethodID(mapCls, "get",
                                               "(Ljava/lang/Object;)Ljava/lang/Object;");
    jmethodID integerValueOf = env->GetStaticMethodID(integerCls, "valueOf",
                                                       "(I)Ljava/lang/Integer;");
    if (env->ExceptionCheck() || !getContext || !getClassLoader || !loadClass ||
        !getDeclaredField || !getSuperclass || !objectGetClass || !setAccessible || !fieldGet ||
        !fieldGetBoolean || !fieldGetInt || !mapGet || !genericMapGet || !integerValueOf) {
        env->ExceptionClear();
        return;
    }

    jobject context = env->CallObjectMethod(webView, getContext);
    jobject loader = context ? env->CallObjectMethod(context, getClassLoader) : nullptr;
    if (env->ExceptionCheck() || loader == nullptr) {
        env->ExceptionClear();
        return;
    }
    jstring tmName = env->NewStringUTF("com.bytedance.bytewebview.articletemplate.TemplateManager");
    jobject tmClass = env->CallObjectMethod(loader, loadClass, tmName);
    if (env->ExceptionCheck() || tmClass == nullptr) {
        env->ExceptionClear();
        return;  // Expected for Noice, Material Catalog, and every non-Toutiao app.
    }

    auto declaredField = [&](jobject ownerClass, const char* name) -> jobject {
        jobject current = ownerClass;
        bool ownsCurrent = false;
        for (int depth = 0; current != nullptr && depth < 12; ++depth) {
            jstring fieldName = env->NewStringUTF(name);
            jobject field = env->CallObjectMethod(current, getDeclaredField, fieldName);
            env->DeleteLocalRef(fieldName);
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (field != nullptr) {
                env->CallVoidMethod(field, setAccessible, JNI_TRUE);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    env->DeleteLocalRef(field);
                    field = nullptr;
                }
                if (ownsCurrent) env->DeleteLocalRef(current);
                return field;
            }
            jobject parent = env->CallObjectMethod(current, getSuperclass);
            if (env->ExceptionCheck()) { env->ExceptionClear(); parent = nullptr; }
            if (ownsCurrent) env->DeleteLocalRef(current);
            current = parent;
            ownsCurrent = true;
        }
        if (ownsCurrent && current != nullptr) env->DeleteLocalRef(current);
        return nullptr;
    };
    auto getObjectField = [&](jobject owner, jobject field) -> jobject {
        if (field == nullptr) return nullptr;
        jobject value = env->CallObjectMethod(field, fieldGet, owner);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return nullptr;
        }
        return value;
    };
    auto getBooleanField = [&](jobject owner, jobject field, jboolean fallback) -> jboolean {
        if (field == nullptr || owner == nullptr) return fallback;
        jboolean value = env->CallBooleanMethod(field, fieldGetBoolean, owner);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return fallback;
        }
        return value;
    };
    auto getIntField = [&](jobject owner, jobject field, jint fallback) -> jint {
        if (field == nullptr || owner == nullptr) return fallback;
        jint value = env->CallIntMethod(field, fieldGetInt, owner);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return fallback;
        }
        return value;
    };

    jobject tmMapField = declaredField(tmClass, "c");
    jobject tmMap = getObjectField(nullptr, tmMapField);
    jobject ktx = tmMap ? env->CallObjectMethod(tmMap, mapGet, webView) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); ktx = nullptr; }
    if (ktx == nullptr) {
        const size_t used = strlen(extra);
        if (used < extraSize) snprintf(extra + used, extraSize - used, " KTX map=MISS");
        return;
    }

    jobject ktxClass = env->CallObjectMethod(ktx, objectGetClass);
    if (env->ExceptionCheck() || ktxClass == nullptr) { env->ExceptionClear(); return; }
    jobject statusField = declaredField(ktxClass, "b");
    jobject configField = declaredField(ktxClass, "c");
    jobject delayField = declaredField(ktxClass, "e");
    jobject dataField = declaredField(ktxClass, "f");
    jobject injectCountField = declaredField(ktxClass, "g");
    jobject loadingField = declaredField(ktxClass, "h");
    jobject status = getObjectField(ktx, statusField);
    jobject config = getObjectField(ktx, configField);
    jstring data = static_cast<jstring>(getObjectField(ktx, dataField));

    jint templateId = -1;
    if (config != nullptr) {
        jobject configClass = env->CallObjectMethod(config, objectGetClass);
        jobject idField = configClass ? declaredField(configClass, "a") : nullptr;
        templateId = getIntField(config, idField, -1);
    }

    // KtR.h() has exactly one early return: its Robust/ChangeQuickRedirect guard.  Report that
    // guard and the factory's cached template string so phase=2/url="" can be distinguished from
    // a normal bind that merely has not received article data yet.
    int factoryPresent = 0, redirectPresent = 0, robustMapPresent = 0, factoryHtmlLen = -1;
    int actualFactoryPresent = 0, actualIsKtRSingleton = -1;
    char actualFactoryName[80] = "?";
    jstring factoryClassName = env->NewStringUTF("X.KtR");
    jobject factoryClass = env->CallObjectMethod(loader, loadClass, factoryClassName);
    if (env->ExceptionCheck()) { env->ExceptionClear(); factoryClass = nullptr; }
    if (factoryClass != nullptr) {
        jobject singletonField = declaredField(factoryClass, "b");
        jobject redirectField = declaredField(factoryClass, "c");
        jobject factory = getObjectField(nullptr, singletonField);
        jobject redirect = getObjectField(nullptr, redirectField);
        factoryPresent = factory != nullptr ? 1 : 0;
        redirectPresent = redirect != nullptr ? 1 : 0;
        if (factory != nullptr) {
            jobject factoryRuntimeClass = env->CallObjectMethod(factory, objectGetClass);
            jobject htmlField = factoryRuntimeClass ? declaredField(factoryRuntimeClass, "a") : nullptr;
            jobject robustMapField = factoryRuntimeClass
                ? declaredField(factoryRuntimeClass, "robustFieldMap") : nullptr;
            jstring factoryHtml = static_cast<jstring>(getObjectField(factory, htmlField));
            jobject robustMap = getObjectField(factory, robustMapField);
            factoryHtmlLen = factoryHtml ? env->GetStringLength(factoryHtml) : 0;
            robustMapPresent = robustMap != nullptr ? 1 : 0;
        }

        jobject factoriesField = declaredField(tmClass, "d");
        jobject factories = getObjectField(nullptr, factoriesField);
        jobject templateKey = env->CallStaticObjectMethod(integerCls, integerValueOf, templateId);
        jobject actualFactory = (factories && templateKey)
            ? env->CallObjectMethod(factories, genericMapGet, templateKey) : nullptr;
        if (env->ExceptionCheck()) { env->ExceptionClear(); actualFactory = nullptr; }
        if (actualFactory != nullptr) {
            actualFactoryPresent = 1;
            actualIsKtRSingleton = factory ? env->IsSameObject(actualFactory, factory) : 0;
            jobject actualClass = env->CallObjectMethod(actualFactory, objectGetClass);
            jmethodID classGetName = env->GetMethodID(classCls, "getName", "()Ljava/lang/String;");
            jstring actualName = (actualClass && classGetName)
                ? static_cast<jstring>(env->CallObjectMethod(actualClass, classGetName)) : nullptr;
            if (env->ExceptionCheck()) { env->ExceptionClear(); actualName = nullptr; }
            const char* actualChars = actualName
                ? env->GetStringUTFChars(actualName, nullptr) : nullptr;
            if (actualChars) {
                snprintf(actualFactoryName, sizeof(actualFactoryName), "%s", actualChars);
                env->ReleaseStringUTFChars(actualName, actualChars);
            }
        }
    }

    jint phase = -1;
    jboolean usingNow = JNI_FALSE, pageStarted = JNI_FALSE, pageCommit = JNI_FALSE;
    jboolean ready = JNI_FALSE, reuse = JNI_FALSE;
    int dataReady = -1;
    if (status != nullptr) {
        jobject statusClass = env->CallObjectMethod(status, objectGetClass);
        jobject usingField = statusClass ? declaredField(statusClass, "a") : nullptr;
        jobject phaseField = statusClass ? declaredField(statusClass, "b") : nullptr;
        jobject pageStartedField = statusClass ? declaredField(statusClass, "c") : nullptr;
        jobject pageCommitField = statusClass ? declaredField(statusClass, "d") : nullptr;
        jobject readyField = statusClass ? declaredField(statusClass, "e") : nullptr;
        jobject reuseField = statusClass ? declaredField(statusClass, "f") : nullptr;
        jobject dataReadyField = statusClass ? declaredField(statusClass, "g") : nullptr;
        usingNow = getBooleanField(status, usingField, JNI_FALSE);
        phase = getIntField(status, phaseField, -1);
        pageStarted = getBooleanField(status, pageStartedField, JNI_FALSE);
        pageCommit = getBooleanField(status, pageCommitField, JNI_FALSE);
        ready = getBooleanField(status, readyField, JNI_FALSE);
        reuse = getBooleanField(status, reuseField, JNI_FALSE);
        jobject boxedDataReady = getObjectField(status, dataReadyField);
        if (boxedDataReady != nullptr) {
            jclass booleanCls = env->FindClass("java/lang/Boolean");
            jmethodID booleanValue = booleanCls
                ? env->GetMethodID(booleanCls, "booleanValue", "()Z") : nullptr;
            if (booleanValue != nullptr) {
                dataReady = env->CallBooleanMethod(boxedDataReady, booleanValue) ? 1 : 0;
                if (env->ExceptionCheck()) { env->ExceptionClear(); dataReady = -1; }
            }
        }
    }

    const jboolean delay = getBooleanField(ktx, delayField, JNI_FALSE);
    const jboolean loading = getBooleanField(ktx, loadingField, JNI_FALSE);
    const jint injectCount = getIntField(ktx, injectCountField, -1);
    const jint dataLen = data ? env->GetStringLength(data) : 0;
    const size_t used = strlen(extra);
    if (used < extraSize) {
        snprintf(extra + used, extraSize - used,
                 " KTX template=%d delay=%d loading=%d dataLen=%d inject=%d"
                 " phase=%d using=%d started=%d commit=%d ready=%d reuse=%d dataReady=%d"
                 " factory=%d redirect=%d robust=%d factoryHtmlLen=%d"
                 " actualFactory=%s present=%d sameKtR=%d",
                 (int)templateId, (int)delay, (int)loading, (int)dataLen, (int)injectCount,
                 (int)phase, (int)usingNow, (int)pageStarted, (int)pageCommit, (int)ready,
                 (int)reuse, dataReady, factoryPresent, redirectPresent, robustMapPresent,
                 factoryHtmlLen, actualFactoryName, actualFactoryPresent, actualIsKtRSingleton);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// WESTLAKE §697 — Android WebView provider submission state, read-only.
//
// §691 proves Toutiao reaches WebView.loadDataWithBaseURL(), while the public WebView state
// remains url=""/contentHeight=1.  Chromium has two silent paths below that API which produce
// exactly that observation: WebViewChromium may leave the Runnable in its startup queue, and
// AwContents::p() returns without loading when the AwContents is destroyed, waiting for its
// deferred-finish callback, or has lost its peer.  Inspect those private implementation fields
// through reflection so we can distinguish the paths without modifying either the app or the
// WebView APK.  Field names are specific to the pinned webview-t.apk and failures are a no-op.
static void wl_append_chromium_provider_state(JNIEnv* env, jobject webView,
                                               char* extra, size_t extraSize) {
    if (webView == nullptr || extra == nullptr || extraSize == 0) return;

    jclass webViewCls = env->FindClass("android/webkit/WebView");
    jclass objectCls = env->FindClass("java/lang/Object");
    jclass classCls = env->FindClass("java/lang/Class");
    jclass fieldCls = env->FindClass("java/lang/reflect/Field");
    jclass methodCls = env->FindClass("java/lang/reflect/Method");
    jclass collectionCls = env->FindClass("java/util/Collection");
    jclass referenceCls = env->FindClass("java/lang/ref/Reference");
    if (env->ExceptionCheck() || !webViewCls || !objectCls || !classCls || !fieldCls ||
        !methodCls || !collectionCls || !referenceCls) {
        env->ExceptionClear();
        return;
    }
    jclass proxyCls = env->FindClass("java/lang/reflect/Proxy");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        proxyCls = nullptr;
    }
    jclass webViewProviderCls = env->FindClass("android/webkit/WebViewProvider");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        webViewProviderCls = nullptr;
    }

    jfieldID providerField = env->GetFieldID(webViewCls, "mProvider",
                                             "Landroid/webkit/WebViewProvider;");
    if (env->ExceptionCheck() || providerField == nullptr) {
        env->ExceptionClear();
        return;
    }
    jobject provider = env->GetObjectField(webView, providerField);
    if (env->ExceptionCheck() || provider == nullptr) {
        env->ExceptionClear();
        return;
    }

    jmethodID objectGetClass = env->GetMethodID(objectCls, "getClass", "()Ljava/lang/Class;");
    jmethodID classGetName = env->GetMethodID(classCls, "getName", "()Ljava/lang/String;");
    jmethodID classGetSuperclass = env->GetMethodID(
        classCls, "getSuperclass", "()Ljava/lang/Class;");
    jmethodID getDeclaredField = env->GetMethodID(
        classCls, "getDeclaredField", "(Ljava/lang/String;)Ljava/lang/reflect/Field;");
    jmethodID getDeclaredMethod = env->GetMethodID(
        classCls, "getDeclaredMethod",
        "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    jmethodID setAccessible = env->GetMethodID(fieldCls, "setAccessible", "(Z)V");
    jmethodID methodSetAccessible = env->GetMethodID(methodCls, "setAccessible", "(Z)V");
    jmethodID methodInvoke = env->GetMethodID(
        methodCls, "invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
    jmethodID fieldGet = env->GetMethodID(fieldCls, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
    jmethodID fieldGetInt = env->GetMethodID(fieldCls, "getInt", "(Ljava/lang/Object;)I");
    jmethodID fieldGetLong = env->GetMethodID(fieldCls, "getLong", "(Ljava/lang/Object;)J");
    jmethodID fieldGetBoolean = env->GetMethodID(fieldCls, "getBoolean", "(Ljava/lang/Object;)Z");
    jmethodID collectionSize = env->GetMethodID(collectionCls, "size", "()I");
    jmethodID referenceGet = env->GetMethodID(referenceCls, "get", "()Ljava/lang/Object;");
    jmethodID proxyGetInvocationHandler = proxyCls != nullptr
        ? env->GetStaticMethodID(
              proxyCls, "getInvocationHandler",
              "(Ljava/lang/Object;)Ljava/lang/reflect/InvocationHandler;")
        : nullptr;
    if (env->ExceptionCheck() || !objectGetClass || !classGetName || !classGetSuperclass ||
        !getDeclaredField || !getDeclaredMethod || !setAccessible || !methodSetAccessible ||
        !methodInvoke || !fieldGet || !fieldGetInt || !fieldGetLong || !fieldGetBoolean ||
        !collectionSize || !referenceGet) {
        env->ExceptionClear();
        return;
    }

    auto declaredField = [&](jobject ownerClass, const char* name) -> jobject {
        if (ownerClass == nullptr) return nullptr;
        jobject current = ownerClass;
        bool ownsCurrent = false;
        for (int depth = 0; current != nullptr && depth < 12; ++depth) {
            jstring fieldName = env->NewStringUTF(name);
            jobject field = env->CallObjectMethod(current, getDeclaredField, fieldName);
            env->DeleteLocalRef(fieldName);
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (field != nullptr) {
                env->CallVoidMethod(field, setAccessible, JNI_TRUE);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    env->DeleteLocalRef(field);
                    field = nullptr;
                }
                if (ownsCurrent) env->DeleteLocalRef(current);
                return field;
            }
            jobject parent = env->CallObjectMethod(current, classGetSuperclass);
            if (env->ExceptionCheck()) { env->ExceptionClear(); parent = nullptr; }
            if (ownsCurrent) env->DeleteLocalRef(current);
            current = parent;
            ownsCurrent = true;
        }
        if (ownsCurrent && current != nullptr) env->DeleteLocalRef(current);
        return nullptr;
    };
    auto getObjectField = [&](jobject owner, jobject field) -> jobject {
        if (owner == nullptr || field == nullptr) return nullptr;
        jobject value = env->CallObjectMethod(field, fieldGet, owner);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
        return value;
    };
    auto getIntField = [&](jobject owner, jobject field, jint fallback) -> jint {
        if (owner == nullptr || field == nullptr) return fallback;
        jint value = env->CallIntMethod(field, fieldGetInt, owner);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return fallback; }
        return value;
    };
    auto getLongField = [&](jobject owner, jobject field, jlong fallback) -> jlong {
        if (owner == nullptr || field == nullptr) return fallback;
        jlong value = env->CallLongMethod(field, fieldGetLong, owner);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return fallback; }
        return value;
    };
    auto getBooleanField = [&](jobject owner, jobject field, jboolean fallback) -> jboolean {
        if (owner == nullptr || field == nullptr) return fallback;
        jboolean value = env->CallBooleanMethod(field, fieldGetBoolean, owner);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return fallback; }
        return value;
    };

    jobject providerClass = env->CallObjectMethod(provider, objectGetClass);
    int proxyTarget = 0;
    if (proxyGetInvocationHandler != nullptr && webViewProviderCls != nullptr) {
        jobject handler = env->CallStaticObjectMethod(
            proxyCls, proxyGetInvocationHandler, provider);
        if (env->ExceptionCheck()) {
            // Ordinary WebViewChromium providers are not java.lang.Proxy
            // instances.  Keep the original diagnostic path for them.
            env->ExceptionClear();
        } else if (handler != nullptr) {
            jobject handlerClass = env->CallObjectMethod(handler, objectGetClass);
            jobject outer = getObjectField(
                handler, declaredField(handlerClass, "a"));
            jobject outerClass = outer != nullptr
                ? env->CallObjectMethod(outer, objectGetClass) : nullptr;
            jobject real = getObjectField(
                outer, declaredField(outerClass, "a"));
            if (real != nullptr && env->IsInstanceOf(real, webViewProviderCls)) {
                provider = real;
                providerClass = env->CallObjectMethod(provider, objectGetClass);
                proxyTarget = 1;
            } else {
                proxyTarget = -1;
            }
        }
    }
    jobject factory = getObjectField(provider, declaredField(providerClass, "i"));
    jobject directAw = getObjectField(provider, declaredField(providerClass, "f"));
    jobject threadState = getObjectField(provider, declaredField(providerClass, "j"));

    jint startupState = -1;
    jint queueSize = -1;
    jobject factoryClass = factory ? env->CallObjectMethod(factory, objectGetClass) : nullptr;
    if (factoryClass != nullptr) {
        // WebViewChromiumFactoryProvider.a is the startup-queue holder (ky0); its own field a is
        // the ConcurrentLinkedQueue.  Reading size() on the holder itself made the first §697
        // probe report -1 even when the provider was healthy.
        jobject queueHolder = getObjectField(factory, declaredField(factoryClass, "a"));
        jobject queueHolderClass = queueHolder
            ? env->CallObjectMethod(queueHolder, objectGetClass) : nullptr;
        jobject queue = getObjectField(queueHolder, declaredField(queueHolderClass, "a"));
        jobject startup = getObjectField(factory, declaredField(factoryClass, "b"));
        if (queue != nullptr) {
            queueSize = env->CallIntMethod(queue, collectionSize);
            if (env->ExceptionCheck()) { env->ExceptionClear(); queueSize = -1; }
        }
        jobject startupClass = startup ? env->CallObjectMethod(startup, objectGetClass) : nullptr;
        startupState = getIntField(startup, declaredField(startupClass, "o"), -1);
    }

    jobject stateClass = threadState ? env->CallObjectMethod(threadState, objectGetClass) : nullptr;
    jobject stateAw = getObjectField(threadState, declaredField(stateClass, "c"));
    jobject aw = directAw != nullptr ? directAw : stateAw;
    jobject awClass = aw ? env->CallObjectMethod(aw, objectGetClass) : nullptr;
    const jlong nativePtr = getLongField(aw, declaredField(awClass, "a"), -1);
    const jboolean destroyed = getBooleanField(aw, declaredField(awClass, "h0"), JNI_FALSE);
    jobject deferred = getObjectField(aw, declaredField(awClass, "u0"));
    jobject peer = getObjectField(aw, declaredField(awClass, "k"));
    jobject webContents = getObjectField(aw, declaredField(awClass, "i"));
    jobject navigation = getObjectField(aw, declaredField(awClass, "l"));
    jobject navigationClass = navigation
        ? env->CallObjectMethod(navigation, objectGetClass) : nullptr;
    const jlong navigationNative = getLongField(
        navigation, declaredField(navigationClass, "a"), -1);
    // §706: NavigationControllerImpl.k() asks native Chromium for the complete committed
    // history and returns ZX{ArrayList entries, int currentIndex}.  The compatibility boot path
    // does not contain WebBackForwardList/WebHistoryItem, so invoke this already-loaded provider
    // implementation reflectively instead of fabricating the missing framework API.
    jint historySize = -1;
    jint historyIndex = -2;
    int historyCall = 0;
    if (navigation != nullptr && navigationClass != nullptr) {
        jobjectArray parameterTypes = env->NewObjectArray(0, classCls, nullptr);
        jstring methodName = env->NewStringUTF("k");
        jobject method = env->CallObjectMethod(
            navigationClass, getDeclaredMethod, methodName, parameterTypes);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            historyCall = -1;
        } else if (method != nullptr) {
            historyCall = 1;
            env->CallVoidMethod(method, methodSetAccessible, JNI_TRUE);
            jobjectArray arguments = env->NewObjectArray(0, objectCls, nullptr);
            jobject history = env->CallObjectMethod(method, methodInvoke, navigation, arguments);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                historyCall = -2;
            } else if (history != nullptr) {
                historyCall = 2;
                jobject historyClass = env->CallObjectMethod(history, objectGetClass);
                jobject entries = getObjectField(
                    history, declaredField(historyClass, "a"));
                historyIndex = getIntField(
                    history, declaredField(historyClass, "b"), -2);
                if (entries != nullptr) {
                    historySize = env->CallIntMethod(entries, collectionSize);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        historySize = -1;
                    }
                }
            }
        }
    }
    jobject webContentsClass = webContents
        ? env->CallObjectMethod(webContents, objectGetClass) : nullptr;
    const jlong webContentsNative = getLongField(
        webContents, declaredField(webContentsClass, "b"), -1);
    jobject webContentsNavigation = getObjectField(
        webContents, declaredField(webContentsClass, "c"));

    int deferredFinishMissing = 0;
    if (deferred != nullptr) {
        jobject deferredClass = env->CallObjectMethod(deferred, objectGetClass);
        jobject finish = getObjectField(deferred, declaredField(deferredClass, "a"));
        deferredFinishMissing = finish == nullptr ? 1 : 0;
    }
    int peerDead = 0;
    if (peer != nullptr) {
        jobject peerClass = env->CallObjectMethod(peer, objectGetClass);
        jobject weak = getObjectField(peer, declaredField(peerClass, "a"));
        jobject referent = weak ? env->CallObjectMethod(weak, referenceGet) : nullptr;
        if (env->ExceptionCheck()) { env->ExceptionClear(); referent = nullptr; }
        peerDead = referent == nullptr ? 1 : 0;
    }

    char providerName[96] = "?";
    jstring name = providerClass
        ? static_cast<jstring>(env->CallObjectMethod(providerClass, classGetName)) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); name = nullptr; }
    const char* nameChars = name ? env->GetStringUTFChars(name, nullptr) : nullptr;
    if (nameChars) {
        snprintf(providerName, sizeof(providerName), "%s", nameChars);
        env->ReleaseStringUTFChars(name, nameChars);
    }

    const size_t used = strlen(extra);
    if (used < extraSize) {
        snprintf(extra + used, extraSize - used,
                 " WVC provider=%s proxyTarget=%d factory=%d state=%d queue=%d directAw=%d stateAw=%d"
                 " native=0x%llx destroyed=%d deferred=%d deferredFinishMissing=%d"
                 " peer=%d peerDead=%d webContents=%d wcNative=0x%llx"
                 " navigation=%d navNative=0x%llx wcSameNav=%d"
                 " histCall=%d histSize=%d histIndex=%d",
                 providerName, proxyTarget, factory != nullptr ? 1 : 0,
                 (int)startupState, (int)queueSize,
                 directAw != nullptr ? 1 : 0, stateAw != nullptr ? 1 : 0,
                 (unsigned long long)nativePtr, (int)destroyed,
                 deferred != nullptr ? 1 : 0, deferredFinishMissing,
                 peer != nullptr ? 1 : 0, peerDead,
                 webContents != nullptr ? 1 : 0,
                 (unsigned long long)webContentsNative,
                 navigation != nullptr ? 1 : 0,
                 (unsigned long long)navigationNative,
                 (webContentsNavigation && navigation)
                     ? (env->IsSameObject(webContentsNavigation, navigation) ? 1 : 0) : -1,
                 historyCall, (int)historySize, (int)historyIndex);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// WESTLAKE §780 — Toutiao short-video player handoff state, read-only.
//
// A black TextureView is downstream of several very different failures: the app may not have
// submitted PrepareData, the shared player controller may not have bound this TTVideoView, or the
// framework may not have delivered a usable SurfaceTexture.  Screenshots and native-load logs
// cannot distinguish those cases.  Inspect the already-live objects through Java reflection and
// append the result to VT.  This never invokes an app method or writes an app field; on every
// non-Toutiao view it is a no-op.
static void wl_append_toutiao_player_state(JNIEnv* env, jobject view, const char* shortName,
                                            char* extra, size_t extraSize) {
    if (view == nullptr || shortName == nullptr || extra == nullptr || extraSize == 0) return;
    const bool isVideoView = strcmp(shortName, "TTVideoView") == 0;
    const bool isTextureView = strcmp(shortName, "TextureVideoView") == 0;
    if (!isVideoView && !isTextureView) return;

    jclass objectCls = env->FindClass("java/lang/Object");
    jclass classCls = env->FindClass("java/lang/Class");
    jclass fieldCls = env->FindClass("java/lang/reflect/Field");
    if (env->ExceptionCheck() || !objectCls || !classCls || !fieldCls) {
        env->ExceptionClear();
        return;
    }
    jmethodID objectGetClass = env->GetMethodID(objectCls, "getClass", "()Ljava/lang/Class;");
    jmethodID classGetName = env->GetMethodID(classCls, "getName", "()Ljava/lang/String;");
    jmethodID classGetSuperclass = env->GetMethodID(classCls, "getSuperclass",
                                                     "()Ljava/lang/Class;");
    jmethodID classGetDeclaredField = env->GetMethodID(
        classCls, "getDeclaredField", "(Ljava/lang/String;)Ljava/lang/reflect/Field;");
    jmethodID fieldSetAccessible = env->GetMethodID(fieldCls, "setAccessible", "(Z)V");
    jmethodID fieldGet = env->GetMethodID(fieldCls, "get",
                                          "(Ljava/lang/Object;)Ljava/lang/Object;");
    jmethodID fieldGetBoolean = env->GetMethodID(fieldCls, "getBoolean",
                                                 "(Ljava/lang/Object;)Z");
    if (env->ExceptionCheck() || !objectGetClass || !classGetName || !classGetSuperclass ||
        !classGetDeclaredField || !fieldSetAccessible || !fieldGet || !fieldGetBoolean) {
        env->ExceptionClear();
        return;
    }

    auto declaredField = [&](jobject ownerClass, const char* name) -> jobject {
        jobject current = ownerClass ? env->NewLocalRef(ownerClass) : nullptr;
        for (int depth = 0; current != nullptr && depth < 16; ++depth) {
            jstring fieldName = env->NewStringUTF(name);
            jobject field = env->CallObjectMethod(current, classGetDeclaredField, fieldName);
            env->DeleteLocalRef(fieldName);
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (field != nullptr) {
                env->CallVoidMethod(field, fieldSetAccessible, JNI_TRUE);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    env->DeleteLocalRef(field);
                    field = nullptr;
                }
                env->DeleteLocalRef(current);
                return field;
            }
            jobject parent = env->CallObjectMethod(current, classGetSuperclass);
            if (env->ExceptionCheck()) { env->ExceptionClear(); parent = nullptr; }
            env->DeleteLocalRef(current);
            current = parent;
        }
        if (current != nullptr) env->DeleteLocalRef(current);
        return nullptr;
    };
    auto getObject = [&](jobject owner, jobject field) -> jobject {
        if (field == nullptr) return nullptr;
        jobject value = env->CallObjectMethod(field, fieldGet, owner);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
        return value;
    };
    auto getBoolean = [&](jobject owner, jobject field, int fallback) -> int {
        if (owner == nullptr || field == nullptr) return fallback;
        jboolean value = env->CallBooleanMethod(field, fieldGetBoolean, owner);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return fallback; }
        return value ? 1 : 0;
    };
    auto className = [&](jobject object, char* out, size_t outSize) {
        if (out == nullptr || outSize == 0) return;
        snprintf(out, outSize, "-");
        if (object == nullptr) return;
        jobject cls = env->CallObjectMethod(object, objectGetClass);
        jstring name = cls
            ? static_cast<jstring>(env->CallObjectMethod(cls, classGetName)) : nullptr;
        if (env->ExceptionCheck()) { env->ExceptionClear(); name = nullptr; }
        const char* chars = name ? env->GetStringUTFChars(name, nullptr) : nullptr;
        if (chars != nullptr) {
            snprintf(out, outSize, "%s", chars);
            env->ReleaseStringUTFChars(name, chars);
        }
        if (name != nullptr) env->DeleteLocalRef(name);
        if (cls != nullptr) env->DeleteLocalRef(cls);
    };

    jobject viewClass = env->CallObjectMethod(view, objectGetClass);
    if (env->ExceptionCheck() || viewClass == nullptr) { env->ExceptionClear(); return; }

    if (isTextureView) {
        jobject cachedSurface = getObject(view, declaredField(viewClass, "mCachedSurface"));
        jobject surfaceTexture = getObject(view, declaredField(viewClass, "mSurfaceTexture"));
        jobject surfaceListener = getObject(view, declaredField(viewClass, "mSurfaceListener"));
        jobject textureListener = getObject(view, declaredField(viewClass, "mSurfaceTextureListener"));
        const int attached = getBoolean(view, declaredField(viewClass, "attached"), -1);
        const int textureValid = getBoolean(view, declaredField(viewClass, "mTextureValid"), -1);
        const int reuse = getBoolean(view, declaredField(viewClass, "reuseSurfaceTexture"), -1);

        int surfaceValid = -1;
        if (cachedSurface != nullptr) {
            jclass surfaceCls = env->FindClass("android/view/Surface");
            jmethodID isValid = surfaceCls
                ? env->GetMethodID(surfaceCls, "isValid", "()Z") : nullptr;
            if (env->ExceptionCheck()) { env->ExceptionClear(); isValid = nullptr; }
            if (isValid != nullptr) {
                surfaceValid = env->CallBooleanMethod(cachedSurface, isValid) ? 1 : 0;
                if (env->ExceptionCheck()) { env->ExceptionClear(); surfaceValid = -1; }
            }
        }
        int available = -1;
        jclass textureViewCls = env->FindClass("android/view/TextureView");
        jmethodID isAvailable = textureViewCls
            ? env->GetMethodID(textureViewCls, "isAvailable", "()Z") : nullptr;
        if (env->ExceptionCheck()) { env->ExceptionClear(); isAvailable = nullptr; }
        if (isAvailable != nullptr) {
            available = env->CallBooleanMethod(view, isAvailable) ? 1 : 0;
            if (env->ExceptionCheck()) { env->ExceptionClear(); available = -1; }
        }
        char surfaceListenerName[96], textureListenerName[96];
        className(surfaceListener, surfaceListenerName, sizeof(surfaceListenerName));
        className(textureListener, textureListenerName, sizeof(textureListenerName));
        const size_t used = strlen(extra);
        if (used < extraSize) {
            snprintf(extra + used, extraSize - used,
                     " TTV attached=%d available=%d reuse=%d textureValid=%d"
                     " cachedSurface=%d surfaceValid=%d surfaceTexture=%d"
                     " surfaceListener=%s textureListener=%s",
                     attached, available, reuse, textureValid,
                     cachedSurface != nullptr ? 1 : 0, surfaceValid,
                     surfaceTexture != nullptr ? 1 : 0,
                     surfaceListenerName, textureListenerName);
        }
        return;
    }

    jobject videoPlayer = getObject(view, declaredField(viewClass, "mVideoPlayer"));
    jobject wrapperListener = getObject(view, declaredField(viewClass, "mWrapperListener"));
    const int released = getBoolean(view, declaredField(viewClass, "isRelease"), -1);
    const int renderStarted = getBoolean(view, declaredField(viewClass, "isRenderStart"), -1);
    const int complete = getBoolean(view, declaredField(viewClass, "isComplete"), -1);
    char playerName[112];
    className(videoPlayer, playerName, sizeof(playerName));

    // The process-wide PlayerManager owns the controller used by this exact immersive path.
    // Its four object fields tell us whether the visible view, Surface, and PrepareData crossed
    // the app handoff before OUI creates the native player.
    int managerPresent = 0, controllerPresent = 0;
    jobject controllerPlayer = nullptr, controllerSurface = nullptr;
    jobject controllerData = nullptr, controllerView = nullptr;
    int controllerPlaying = -1, controllerPreparing = -1, controllerPaused = -1;
    jclass viewBaseCls = env->FindClass("android/view/View");
    jclass contextCls = env->FindClass("android/content/Context");
    jclass loaderCls = env->FindClass("java/lang/ClassLoader");
    jmethodID getContext = viewBaseCls
        ? env->GetMethodID(viewBaseCls, "getContext", "()Landroid/content/Context;") : nullptr;
    jmethodID getClassLoader = contextCls
        ? env->GetMethodID(contextCls, "getClassLoader", "()Ljava/lang/ClassLoader;") : nullptr;
    jmethodID loadClass = loaderCls
        ? env->GetMethodID(loaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();
    jobject context = getContext ? env->CallObjectMethod(view, getContext) : nullptr;
    jobject loader = (context && getClassLoader)
        ? env->CallObjectMethod(context, getClassLoader) : nullptr;
    jstring managerName = env->NewStringUTF("X.OUJ");
    jobject managerClass = (loader && loadClass)
        ? env->CallObjectMethod(loader, loadClass, managerName) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); managerClass = nullptr; }
    jobject manager = managerClass
        ? getObject(nullptr, declaredField(managerClass, "i")) : nullptr;
    if (manager != nullptr) {
        managerPresent = 1;
        jobject managerRuntimeClass = env->CallObjectMethod(manager, objectGetClass);
        jobject controller = managerRuntimeClass
            ? getObject(manager, declaredField(managerRuntimeClass, "a")) : nullptr;
        if (controller != nullptr) {
            controllerPresent = 1;
            jobject controllerClass = env->CallObjectMethod(controller, objectGetClass);
            controllerPlayer = getObject(controller, declaredField(controllerClass, "b"));
            controllerSurface = getObject(controller, declaredField(controllerClass, "d"));
            controllerData = getObject(controller, declaredField(controllerClass, "e"));
            controllerView = getObject(controller, declaredField(controllerClass, "f"));
            controllerPaused = getBoolean(controller, declaredField(controllerClass, "l"), -1);
            controllerPlaying = getBoolean(controller, declaredField(controllerClass, "o"), -1);
            controllerPreparing = getBoolean(controller, declaredField(controllerClass, "p"), -1);
        }
    }
    char controllerPlayerName[112];
    className(controllerPlayer, controllerPlayerName, sizeof(controllerPlayerName));
    const int sameView = controllerView != nullptr ? (env->IsSameObject(controllerView, view) ? 1 : 0) : -1;
    const size_t used = strlen(extra);
    if (used < extraSize) {
        snprintf(extra + used, extraSize - used,
                 " TTVIEW player=%s wrapper=%d released=%d renderStarted=%d complete=%d"
                 " manager=%d controller=%d ctrlPlayer=%s ctrlSurface=%d ctrlData=%d"
                 " ctrlView=%d sameView=%d paused=%d playing=%d preparing=%d",
                 playerName, wrapperListener != nullptr ? 1 : 0,
                 released, renderStarted, complete,
                 managerPresent, controllerPresent, controllerPlayerName,
                 controllerSurface != nullptr ? 1 : 0, controllerData != nullptr ? 1 : 0,
                 controllerView != nullptr ? 1 : 0, sameView,
                 controllerPaused, controllerPlaying, controllerPreparing);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// Read-only Fresco state for the live feed image view.  The framebuffer shows
// the same gray rectangle for a placeholder, a missing controller result, and
// a corrupt decoded bitmap, so expose the actual controller/hierarchy objects
// in the existing structural oracle without changing the APK or its state.
static void wl_append_fresco_image_state(JNIEnv* env, jobject view, const char* shortName,
                                         char* extra, size_t extraSize) {
    if (env == nullptr || view == nullptr || shortName == nullptr || extra == nullptr ||
        extraSize == 0 || strstr(shortName, "AsyncImageView") == nullptr) {
        return;
    }
    const auto clearException = [&]() {
        if (env->ExceptionCheck()) env->ExceptionClear();
    };
    const auto objectName = [&](jobject object, char* out, size_t outSize) {
        snprintf(out, outSize, "%s", object == nullptr ? "null" : "?");
        if (object == nullptr) return;
        jclass objectClass = env->GetObjectClass(object);
        jclass classClass = env->FindClass("java/lang/Class");
        jmethodID getName = classClass == nullptr ? nullptr :
            env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
        jstring name = objectClass != nullptr && getName != nullptr
            ? static_cast<jstring>(env->CallObjectMethod(objectClass, getName)) : nullptr;
        if (env->ExceptionCheck()) { env->ExceptionClear(); name = nullptr; }
        const char* chars = name == nullptr ? nullptr : env->GetStringUTFChars(name, nullptr);
        if (chars != nullptr) {
            const char* dot = strrchr(chars, '.');
            snprintf(out, outSize, "%s", dot != nullptr ? dot + 1 : chars);
            env->ReleaseStringUTFChars(name, chars);
        }
        if (name != nullptr) env->DeleteLocalRef(name);
        if (classClass != nullptr) env->DeleteLocalRef(classClass);
        if (objectClass != nullptr) env->DeleteLocalRef(objectClass);
    };
    const auto objectField = [&](jobject object, const char* name,
                                 const char* signature) -> jobject {
        if (object == nullptr) return nullptr;
        jclass objectClass = env->GetObjectClass(object);
        jfieldID field = objectClass == nullptr ? nullptr :
            env->GetFieldID(objectClass, name, signature);
        if (env->ExceptionCheck()) { env->ExceptionClear(); field = nullptr; }
        jobject result = field == nullptr ? nullptr : env->GetObjectField(object, field);
        if (env->ExceptionCheck()) { env->ExceptionClear(); result = nullptr; }
        if (objectClass != nullptr) env->DeleteLocalRef(objectClass);
        return result;
    };
    const auto boolField = [&](jobject object, const char* name, int fallback) -> int {
        if (object == nullptr) return fallback;
        jclass objectClass = env->GetObjectClass(object);
        jfieldID field = objectClass == nullptr ? nullptr :
            env->GetFieldID(objectClass, name, "Z");
        if (env->ExceptionCheck()) { env->ExceptionClear(); field = nullptr; }
        int result = field == nullptr ? fallback :
            static_cast<int>(env->GetBooleanField(object, field));
        if (env->ExceptionCheck()) { env->ExceptionClear(); result = fallback; }
        if (objectClass != nullptr) env->DeleteLocalRef(objectClass);
        return result;
    };

    jclass viewClass = env->GetObjectClass(view);
    jmethodID getHierarchy = viewClass == nullptr ? nullptr : env->GetMethodID(
        viewClass, "getHierarchy", "()Lcom/facebook/drawee/interfaces/DraweeHierarchy;");
    if (env->ExceptionCheck()) { env->ExceptionClear(); getHierarchy = nullptr; }
    jmethodID getController = viewClass == nullptr ? nullptr : env->GetMethodID(
        viewClass, "getController", "()Lcom/facebook/drawee/interfaces/DraweeController;");
    if (env->ExceptionCheck()) { env->ExceptionClear(); getController = nullptr; }
    jobject hierarchy = getHierarchy == nullptr ? nullptr :
        env->CallObjectMethod(view, getHierarchy);
    if (env->ExceptionCheck()) { env->ExceptionClear(); hierarchy = nullptr; }
    jobject controller = getController == nullptr ? nullptr :
        env->CallObjectMethod(view, getController);
    if (env->ExceptionCheck()) { env->ExceptionClear(); controller = nullptr; }
    jmethodID isAttached = viewClass == nullptr ? nullptr :
        env->GetMethodID(viewClass, "isAttachedToWindow", "()Z");
    jmethodID isTemporary = viewClass == nullptr ? nullptr :
        env->GetMethodID(viewClass, "isTemporarilyDetached", "()Z");
    jmethodID isShown = viewClass == nullptr ? nullptr :
        env->GetMethodID(viewClass, "isShown", "()Z");
    jmethodID getWindowVisibility = viewClass == nullptr ? nullptr :
        env->GetMethodID(viewClass, "getWindowVisibility", "()I");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        isAttached = nullptr;
        isTemporary = nullptr;
        isShown = nullptr;
        getWindowVisibility = nullptr;
    }
    int viewAttached = isAttached == nullptr ? -1 :
        static_cast<int>(env->CallBooleanMethod(view, isAttached));
    int viewTemporary = isTemporary == nullptr ? -1 :
        static_cast<int>(env->CallBooleanMethod(view, isTemporary));
    int viewShown = isShown == nullptr ? -1 :
        static_cast<int>(env->CallBooleanMethod(view, isShown));
    int windowVisibility = getWindowVisibility == nullptr ? -1 :
        static_cast<int>(env->CallIntMethod(view, getWindowVisibility));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        viewAttached = viewTemporary = viewShown = windowVisibility = -1;
    }
    jobject holder = objectField(
        view, "mDraweeHolder", "Lcom/facebook/drawee/view/DraweeHolder;");

    jobject wrapper = objectField(
        hierarchy, "mActualImageWrapper", "Lcom/facebook/drawee/drawable/ForwardingDrawable;");
    jobject leaf = objectField(wrapper, "mCurrentDelegate", "Landroid/graphics/drawable/Drawable;");
    char leafChain[180] = "";
    for (int depth = 0; leaf != nullptr && depth < 5; ++depth) {
        char currentName[64];
        objectName(leaf, currentName, sizeof(currentName));
        const size_t used = strlen(leafChain);
        snprintf(leafChain + used, sizeof(leafChain) - used, "%s%s",
                 used == 0 ? "" : ">", currentName);
        jobject next = objectField(
            leaf, "mCurrentDelegate", "Landroid/graphics/drawable/Drawable;");
        if (next == nullptr || env->IsSameObject(next, leaf)) {
            if (next != nullptr) env->DeleteLocalRef(next);
            break;
        }
        env->DeleteLocalRef(leaf);
        leaf = next;
    }

    jobject fetched = objectField(controller, "mFetchedImage", "Ljava/lang/Object;");
    jobject drawable = objectField(
        controller, "mDrawable", "Landroid/graphics/drawable/Drawable;");
    jobject dataSource = objectField(
        controller, "mDataSource", "Lcom/facebook/datasource/DataSource;");
    jobject image = nullptr;
    if (fetched != nullptr) {
        jclass fetchedClass = env->GetObjectClass(fetched);
        jmethodID get = fetchedClass == nullptr ? nullptr :
            env->GetMethodID(fetchedClass, "get", "()Ljava/lang/Object;");
        if (env->ExceptionCheck()) { env->ExceptionClear(); get = nullptr; }
        image = get == nullptr ? nullptr : env->CallObjectMethod(fetched, get);
        if (env->ExceptionCheck()) { env->ExceptionClear(); image = nullptr; }
        if (fetchedClass != nullptr) env->DeleteLocalRef(fetchedClass);
    }
    jobject bitmap = nullptr;
    jint bitmapWidth = -1, bitmapHeight = -1, centerPixel = 0;
    if (image != nullptr) {
        jclass imageClass = env->GetObjectClass(image);
        jmethodID getBitmap = imageClass == nullptr ? nullptr : env->GetMethodID(
            imageClass, "getUnderlyingBitmap", "()Landroid/graphics/Bitmap;");
        if (env->ExceptionCheck()) { env->ExceptionClear(); getBitmap = nullptr; }
        bitmap = getBitmap == nullptr ? nullptr : env->CallObjectMethod(image, getBitmap);
        if (env->ExceptionCheck()) { env->ExceptionClear(); bitmap = nullptr; }
        if (imageClass != nullptr) env->DeleteLocalRef(imageClass);
    }
    if (bitmap != nullptr) {
        jclass bitmapClass = env->GetObjectClass(bitmap);
        jmethodID getWidth = bitmapClass == nullptr ? nullptr :
            env->GetMethodID(bitmapClass, "getWidth", "()I");
        jmethodID getHeight = bitmapClass == nullptr ? nullptr :
            env->GetMethodID(bitmapClass, "getHeight", "()I");
        jmethodID getPixel = bitmapClass == nullptr ? nullptr :
            env->GetMethodID(bitmapClass, "getPixel", "(II)I");
        clearException();
        if (getWidth != nullptr) bitmapWidth = env->CallIntMethod(bitmap, getWidth);
        if (getHeight != nullptr) bitmapHeight = env->CallIntMethod(bitmap, getHeight);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            bitmapWidth = bitmapHeight = -1;
        } else if (getPixel != nullptr && bitmapWidth > 0 && bitmapHeight > 0) {
            centerPixel = env->CallIntMethod(bitmap, getPixel,
                                              bitmapWidth / 2, bitmapHeight / 2);
            clearException();
        }
        if (bitmapClass != nullptr) env->DeleteLocalRef(bitmapClass);
    }

    char hierarchyName[64], controllerName[64], fetchedName[64], imageName[64],
         drawableName[64];
    objectName(hierarchy, hierarchyName, sizeof(hierarchyName));
    objectName(controller, controllerName, sizeof(controllerName));
    objectName(fetched, fetchedName, sizeof(fetchedName));
    objectName(image, imageName, sizeof(imageName));
    objectName(drawable, drawableName, sizeof(drawableName));
    const int holderAttached = boolField(holder, "mIsHolderAttached", -1);
    const int holderVisible = boolField(holder, "mIsVisible", -1);
    const int controllerAttached = boolField(holder, "mIsControllerAttached", -1);
    const size_t used = strlen(extra);
    if (used < extraSize) {
        snprintf(extra + used, extraSize - used,
                 " IMG viewAttached=%d temporary=%d shown=%d windowVis=%d"
                 " holderAttached=%d holderVisible=%d"
                 " controllerAttached=%d hierarchy=%s controller=%s submitted=%d failed=%d data=%d"
                 " fetched=%s image=%s drawable=%s leaf=%s bitmap=%dx%d center=%08x",
                 viewAttached, viewTemporary, viewShown, windowVisibility,
                 holderAttached, holderVisible, controllerAttached,
                 hierarchyName, controllerName,
                 boolField(controller, "mIsRequestSubmitted", -1),
                 boolField(controller, "mHasFetchFailed", -1),
                 dataSource != nullptr ? 1 : 0, fetchedName, imageName, drawableName,
                 leafChain[0] == '\0' ? "null" : leafChain,
                 static_cast<int>(bitmapWidth), static_cast<int>(bitmapHeight),
                 static_cast<unsigned int>(centerPixel));
    }
    // Mechanism probe only. Modern ImageView forwards this exact public
    // callback to its drawable. The untouched state above is logged first;
    // creating the sentinel asks the oracle to replay a missing visible
    // aggregation once on an already-attached, shown view.
    if (access("/data/local/tmp/wl_fresco_force_visible", F_OK) == 0 &&
        viewAttached == 1 && viewShown == 1 && holderVisible == 0 &&
        viewClass != nullptr) {
        jmethodID visibilityAggregated = env->GetMethodID(
            viewClass, "onVisibilityAggregated", "(Z)V");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            visibilityAggregated = nullptr;
        }
        if (visibilityAggregated != nullptr) {
            env->CallVoidMethod(view, visibilityAggregated, JNI_TRUE);
        }
        clearException();
    }
    if (bitmap != nullptr) env->DeleteLocalRef(bitmap);
    if (image != nullptr) env->DeleteLocalRef(image);
    if (dataSource != nullptr) env->DeleteLocalRef(dataSource);
    if (drawable != nullptr) env->DeleteLocalRef(drawable);
    if (fetched != nullptr) env->DeleteLocalRef(fetched);
    if (leaf != nullptr) env->DeleteLocalRef(leaf);
    if (wrapper != nullptr) env->DeleteLocalRef(wrapper);
    if (holder != nullptr) env->DeleteLocalRef(holder);
    if (controller != nullptr) env->DeleteLocalRef(controller);
    if (hierarchy != nullptr) env->DeleteLocalRef(hierarchy);
    if (viewClass != nullptr) env->DeleteLocalRef(viewClass);
}

static void wl_dump_view(JNIEnv* env, WlViewDumpCtx& c, jobject v, int px, int py, int depth,
                         jobject resources) {
    if (v == nullptr || depth > 24) return;
    const jint left = env->CallIntMethod(v, c.getLeft);
    const jint top  = env->CallIntMethod(v, c.getTop);
    const jint w    = env->CallIntMethod(v, c.getW);
    const jint h    = env->CallIntMethod(v, c.getH);
    const jint vis  = env->CallIntMethod(v, c.getVis);
    const jboolean clk = env->CallBooleanMethod(v, c.isClickable);
    const jboolean en  = env->CallBooleanMethod(v, c.isEnabled);
    const jboolean foc = c.isFocused ? env->CallBooleanMethod(v, c.isFocused) : JNI_FALSE;
    const jboolean ftm = c.isFocusableInTouchMode ? env->CallBooleanMethod(v, c.isFocusableInTouchMode) : JNI_FALSE;
    const jboolean itm = c.isInTouchMode ? env->CallBooleanMethod(v, c.isInTouchMode) : JNI_FALSE;
    if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
    const int x = px + left;
    const int y = py + top;

    jclass vc = env->GetObjectClass(v);
    jstring jn = vc ? static_cast<jstring>(env->CallObjectMethod(vc, c.getName)) : nullptr;
    const char* cn = jn ? env->GetStringUTFChars(jn, nullptr) : nullptr;
    const char* shortName = cn;
    if (cn != nullptr) { const char* dot = strrchr(cn, '.'); if (dot && dot[1]) shortName = dot + 1; }

    // resource-entry name, when the view has a real id
    char idbuf[96]; idbuf[0] = 0;
    const jint id = env->CallIntMethod(v, c.getId);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (id != -1 && resources != nullptr && c.getResourceEntryName != nullptr) {
        jstring rn = static_cast<jstring>(env->CallObjectMethod(resources, c.getResourceEntryName, id));
        if (env->ExceptionCheck()) { env->ExceptionClear(); }
        else if (rn != nullptr) {
            const char* rs = env->GetStringUTFChars(rn, nullptr);
            if (rs) { snprintf(idbuf, sizeof(idbuf), "%s", rs); env->ReleaseStringUTFChars(rn, rs); }
        }
        if (rn) env->DeleteLocalRef(rn);
    }

    // text, for anything that is a TextView
    char txt[128]; txt[0] = 0;
    if (c.tvCls && env->IsInstanceOf(v, c.tvCls) && c.getText) {
        jobject cs = env->CallObjectMethod(v, c.getText);
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (cs) {
            jstring js = static_cast<jstring>(env->CallObjectMethod(cs, c.toString));
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (js) {
                const char* ts = env->GetStringUTFChars(js, nullptr);
                if (ts) { snprintf(txt, sizeof(txt), "%s", ts); env->ReleaseStringUTFChars(js, ts); }
                env->DeleteLocalRef(js);
            }
            env->DeleteLocalRef(cs);
        }
    }

    char indent[64]; int di = depth < 20 ? depth : 20;
    for (int i = 0; i < di; ++i) indent[i] = ' ';
    indent[di] = 0;
    if (vis == 0 && w > 0 && h > 0) {   // VISIBLE and laid out
        // WESTLAKE §462: for a Material Slider also print its LIVE value. Reading the row label
        // behind the dialog cannot distinguish "the drag never moved the slider" from "it moved but
        // the label only refreshes on confirm" — this settles it at the source.
        char extra[2048]; extra[0] = 0;
        if (shortName && strstr(shortName, "Slider") != nullptr) {
            jclass sc = env->GetObjectClass(v);
            jmethodID gv = sc ? env->GetMethodID(sc, "getValue", "()F") : nullptr;
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (gv != nullptr) {
                jfloat val = env->CallFloatMethod(v, gv);
                if (env->ExceptionCheck()) { env->ExceptionClear(); }
                else { snprintf(extra, sizeof(extra), " VALUE=%.3f", (double)val); }
            }
            if (sc) env->DeleteLocalRef(sc);
        } else if (shortName && strstr(shortName, "RecyclerView") != nullptr) {
            // Structural feed oracle: a RecyclerView with no attached child can
            // mean either an empty adapter or a layout/prefetch failure.  Ask
            // the public AndroidX API so view-tree dumps distinguish those two
            // cases without knowing anything about the app's adapter class.
            // Resolve on the live app class. FindClass() from this attached
            // diagnostic thread uses the boot loader and cannot see AndroidX.
            jmethodID getAdapter = vc ? env->GetMethodID(
                vc, "getAdapter",
                "()Landroidx/recyclerview/widget/RecyclerView$Adapter;") : nullptr;
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                getAdapter = nullptr;
            }
            jobject adapter = getAdapter ? env->CallObjectMethod(v, getAdapter) : nullptr;
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                adapter = nullptr;
            }
            jint adapterCount = -1;
            if (adapter != nullptr) {
                jclass adapterCls = env->GetObjectClass(adapter);
                jmethodID getItemCount = adapterCls
                    ? env->GetMethodID(adapterCls, "getItemCount", "()I") : nullptr;
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    getItemCount = nullptr;
                }
                if (getItemCount != nullptr) {
                    adapterCount = env->CallIntMethod(adapter, getItemCount);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        adapterCount = -2;
                    }
                }
                if (adapterCls != nullptr) env->DeleteLocalRef(adapterCls);
                env->DeleteLocalRef(adapter);
            }
            snprintf(extra, sizeof(extra), " RECYCLER adapter=%d itemCount=%d childCount=%d",
                     getAdapter != nullptr ? 1 : 0, static_cast<int>(adapterCount),
                     c.vgCls && env->IsInstanceOf(v, c.vgCls)
                         ? static_cast<int>(env->CallIntMethod(v, c.getChildCount)) : -1);
            if (env->ExceptionCheck()) env->ExceptionClear();
        } else if (c.webViewCls && env->IsInstanceOf(v, c.webViewCls)) {
            // A protected Toutiao window cannot be judged by framebuffer captures.  Report the
            // WebView's own navigation state as part of the structural oracle so a blank/progress
            // view can be distinguished from a committed page without patching the app or
            // Chromium.  These are ordinary android.webkit.WebView APIs; failures stay diagnostic.
            jstring url = c.webGetUrl
                ? static_cast<jstring>(env->CallObjectMethod(v, c.webGetUrl)) : nullptr;
            if (env->ExceptionCheck()) { env->ExceptionClear(); url = nullptr; }
            jstring original = c.webGetOriginalUrl
                ? static_cast<jstring>(env->CallObjectMethod(v, c.webGetOriginalUrl)) : nullptr;
            if (env->ExceptionCheck()) { env->ExceptionClear(); original = nullptr; }
            jstring title = c.webGetTitle
                ? static_cast<jstring>(env->CallObjectMethod(v, c.webGetTitle)) : nullptr;
            if (env->ExceptionCheck()) { env->ExceptionClear(); title = nullptr; }
            jint progress = c.webGetProgress ? env->CallIntMethod(v, c.webGetProgress) : -1;
            if (env->ExceptionCheck()) { env->ExceptionClear(); progress = -1; }
            jint contentHeight = c.webGetContentHeight
                ? env->CallIntMethod(v, c.webGetContentHeight) : -1;
            if (env->ExceptionCheck()) { env->ExceptionClear(); contentHeight = -1; }
            jboolean paused = c.webIsPaused
                ? env->CallBooleanMethod(v, c.webIsPaused) : JNI_FALSE;
            if (env->ExceptionCheck()) { env->ExceptionClear(); paused = JNI_FALSE; }
            jint windowVisibility = c.getWindowVisibility
                ? env->CallIntMethod(v, c.getWindowVisibility) : -1;
            if (env->ExceptionCheck()) { env->ExceptionClear(); windowVisibility = -1; }
            jboolean attached = c.isAttachedToWindow
                ? env->CallBooleanMethod(v, c.isAttachedToWindow) : JNI_FALSE;
            if (env->ExceptionCheck()) { env->ExceptionClear(); attached = JNI_FALSE; }
            jboolean shown = c.isShown
                ? env->CallBooleanMethod(v, c.isShown) : JNI_FALSE;
            if (env->ExceptionCheck()) { env->ExceptionClear(); shown = JNI_FALSE; }
            jobject settings = c.webGetSettings
                ? env->CallObjectMethod(v, c.webGetSettings) : nullptr;
            if (env->ExceptionCheck()) { env->ExceptionClear(); settings = nullptr; }
            int javascript = settings && c.settingsGetJavaScriptEnabled
                ? env->CallBooleanMethod(settings, c.settingsGetJavaScriptEnabled) : -1;
            if (env->ExceptionCheck()) { env->ExceptionClear(); javascript = -1; }
            int domStorage = settings && c.settingsGetDomStorageEnabled
                ? env->CallBooleanMethod(settings, c.settingsGetDomStorageEnabled) : -1;
            if (env->ExceptionCheck()) { env->ExceptionClear(); domStorage = -1; }
            int layerType = c.getLayerType ? env->CallIntMethod(v, c.getLayerType) : -1;
            if (env->ExceptionCheck()) { env->ExceptionClear(); layerType = -1; }
            const char* urlChars = url ? env->GetStringUTFChars(url, nullptr) : nullptr;
            const char* originalChars = original
                ? env->GetStringUTFChars(original, nullptr) : nullptr;
            const char* titleChars = title ? env->GetStringUTFChars(title, nullptr) : nullptr;
            snprintf(extra, sizeof(extra), " WEB url=\"%.180s\" original=\"%.120s\""
                     " title=\"%.100s\" progress=%d contentHeight=%d"
                     " paused=%d windowVis=%d attached=%d shown=%d"
                     " javascript=%d domStorage=%d layerType=%d",
                     urlChars ? urlChars : "", originalChars ? originalChars : "",
                     titleChars ? titleChars : "", (int)progress, (int)contentHeight,
                     (int)paused, (int)windowVisibility, (int)attached, (int)shown,
                     javascript, domStorage, layerType);
            if (title && titleChars) env->ReleaseStringUTFChars(title, titleChars);
            if (original && originalChars) env->ReleaseStringUTFChars(original, originalChars);
            if (url && urlChars) env->ReleaseStringUTFChars(url, urlChars);
            if (title) env->DeleteLocalRef(title);
            if (original) env->DeleteLocalRef(original);
            if (url) env->DeleteLocalRef(url);
            if (settings) env->DeleteLocalRef(settings);
            wl_append_toutiao_template_state(env, v, extra, sizeof(extra));
            wl_append_chromium_provider_state(env, v, extra, sizeof(extra));
        }
        wl_append_fresco_image_state(env, v, shortName, extra, sizeof(extra));
        wl_append_toutiao_player_state(env, v, shortName, extra, sizeof(extra));
        LOGI("VT %s%s id=%s rect=[%d,%d %dx%d] c=%d en=%d f=%d ftm=%d itm=%d %s%s%s%s",
             indent, shortName ? shortName : "?", idbuf[0] ? idbuf : "-",
             x, y, (int)w, (int)h, (int)clk, (int)en, (int)foc, (int)ftm, (int)itm,
             txt[0] ? "\"" : "", txt, txt[0] ? "\"" : "", extra);
    }
    if (jn && cn) env->ReleaseStringUTFChars(jn, cn);
    if (jn) env->DeleteLocalRef(jn);
    if (vc) env->DeleteLocalRef(vc);

    if (vis != 0) return;   // do not descend into hidden subtrees
    if (c.vgCls && env->IsInstanceOf(v, c.vgCls)) {
        const jint n = env->CallIntMethod(v, c.getChildCount);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
        const jint sx = env->CallIntMethod(v, c.getScrollX);
        const jint sy = env->CallIntMethod(v, c.getScrollY);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
        for (jint i = 0; i < n; ++i) {
            jobject ch = env->CallObjectMethod(v, c.getChildAt, i);
            if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
            if (ch) { wl_dump_view(env, c, ch, x - sx, y - sy, depth + 1, resources); env->DeleteLocalRef(ch); }
        }
    }
}

void wl_dump_view_tree(JNIEnv* env, int rootIdx);
void wl_dump_view_tree(JNIEnv* env, int rootIdx) {
    jclass wmgCls = env->FindClass("android/view/WindowManagerGlobal");
    if (!wmgCls || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jmethodID getInst = env->GetStaticMethodID(wmgCls, "getInstance", "()Landroid/view/WindowManagerGlobal;");
    jobject wmg = env->CallStaticObjectMethod(wmgCls, getInst);
    if (!wmg || env->ExceptionCheck()) { env->ExceptionClear(); return; }
    jfieldID mRootsF = env->GetFieldID(wmgCls, "mRoots", "Ljava/util/ArrayList;");
    jobject roots = env->GetObjectField(wmg, mRootsF);
    if (!roots) return;
    jclass listCls = env->FindClass("java/util/ArrayList");
    jmethodID sizeM = env->GetMethodID(listCls, "size", "()I");
    jmethodID getM  = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
    jint nn = env->CallIntMethod(roots, sizeM);
    jclass vriCls = env->FindClass("android/view/ViewRootImpl");
    jfieldID mViewF = env->GetFieldID(vriCls, "mView", "Landroid/view/View;");

    WlViewDumpCtx c{};
    c.viewCls = env->FindClass("android/view/View");
    c.vgCls   = env->FindClass("android/view/ViewGroup");
    c.tvCls   = env->FindClass("android/widget/TextView");
    c.webViewCls = env->FindClass("android/webkit/WebView");
    c.webSettingsCls = env->FindClass("android/webkit/WebSettings");
    c.clsCls  = env->FindClass("java/lang/Class");
    c.resCls  = env->FindClass("android/content/res/Resources");
    if (env->ExceptionCheck()) env->ExceptionClear();
    c.getName     = c.clsCls ? env->GetMethodID(c.clsCls, "getName", "()Ljava/lang/String;") : nullptr;
    c.getId       = env->GetMethodID(c.viewCls, "getId", "()I");
    c.getVis      = env->GetMethodID(c.viewCls, "getVisibility", "()I");
    c.isClickable = env->GetMethodID(c.viewCls, "isClickable", "()Z");
    c.isEnabled   = env->GetMethodID(c.viewCls, "isEnabled", "()Z");
    c.isFocused   = env->GetMethodID(c.viewCls, "isFocused", "()Z");
    c.isFocusableInTouchMode = env->GetMethodID(c.viewCls, "isFocusableInTouchMode", "()Z");
    c.isInTouchMode = env->GetMethodID(c.viewCls, "isInTouchMode", "()Z");
    if (env->ExceptionCheck()) env->ExceptionClear();   // §555: optional, never fatal
    c.getW        = env->GetMethodID(c.viewCls, "getWidth", "()I");
    c.getH        = env->GetMethodID(c.viewCls, "getHeight", "()I");
    c.getLeft     = env->GetMethodID(c.viewCls, "getLeft", "()I");
    c.getTop      = env->GetMethodID(c.viewCls, "getTop", "()I");
    c.getScrollX  = env->GetMethodID(c.viewCls, "getScrollX", "()I");
    c.getScrollY  = env->GetMethodID(c.viewCls, "getScrollY", "()I");
    c.getResources= env->GetMethodID(c.viewCls, "getResources", "()Landroid/content/res/Resources;");
    c.getChildCount = c.vgCls ? env->GetMethodID(c.vgCls, "getChildCount", "()I") : nullptr;
    c.getChildAt    = c.vgCls ? env->GetMethodID(c.vgCls, "getChildAt", "(I)Landroid/view/View;") : nullptr;
    c.getText       = c.tvCls ? env->GetMethodID(c.tvCls, "getText", "()Ljava/lang/CharSequence;") : nullptr;
    c.webGetUrl = c.webViewCls ? env->GetMethodID(c.webViewCls, "getUrl", "()Ljava/lang/String;") : nullptr;
    c.webGetOriginalUrl = c.webViewCls ? env->GetMethodID(c.webViewCls, "getOriginalUrl", "()Ljava/lang/String;") : nullptr;
    c.webGetTitle = c.webViewCls ? env->GetMethodID(c.webViewCls, "getTitle", "()Ljava/lang/String;") : nullptr;
    c.webGetProgress = c.webViewCls ? env->GetMethodID(c.webViewCls, "getProgress", "()I") : nullptr;
    c.webGetContentHeight = c.webViewCls ? env->GetMethodID(c.webViewCls, "getContentHeight", "()I") : nullptr;
    c.webIsPaused = c.webViewCls ? env->GetMethodID(c.webViewCls, "isPaused", "()Z") : nullptr;
    c.webGetSettings = c.webViewCls ? env->GetMethodID(c.webViewCls, "getSettings",
                                            "()Landroid/webkit/WebSettings;") : nullptr;
    c.settingsGetJavaScriptEnabled = c.webSettingsCls
        ? env->GetMethodID(c.webSettingsCls, "getJavaScriptEnabled", "()Z") : nullptr;
    c.settingsGetDomStorageEnabled = c.webSettingsCls
        ? env->GetMethodID(c.webSettingsCls, "getDomStorageEnabled", "()Z") : nullptr;
    c.getLayerType = env->GetMethodID(c.viewCls, "getLayerType", "()I");
    c.getWindowVisibility = env->GetMethodID(c.viewCls, "getWindowVisibility", "()I");
    c.isAttachedToWindow = env->GetMethodID(c.viewCls, "isAttachedToWindow", "()Z");
    c.isShown = env->GetMethodID(c.viewCls, "isShown", "()Z");
    c.getResourceEntryName = c.resCls ? env->GetMethodID(c.resCls, "getResourceEntryName",
                                            "(I)Ljava/lang/String;") : nullptr;
    jclass objCls = env->FindClass("java/lang/Object");
    c.toString = env->GetMethodID(objCls, "toString", "()Ljava/lang/String;");
    if (env->ExceptionCheck()) env->ExceptionClear();

    for (jint i = 0; i < nn; ++i) {
        if (rootIdx >= 0 && i != rootIdx) continue;
        jobject vri = env->CallObjectMethod(roots, getM, i);
        if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
        if (!vri) continue;
        jobject view = env->GetObjectField(vri, mViewF);
        if (view) {
            LOGI("VT ==== root[%d] ====", (int)i);
            jobject res = c.getResources ? env->CallObjectMethod(view, c.getResources) : nullptr;
            if (env->ExceptionCheck()) { env->ExceptionClear(); res = nullptr; }
            wl_dump_view(env, c, view, 0, 0, 0, res);
            if (res) env->DeleteLocalRef(res);
            env->DeleteLocalRef(view);
        }
        env->DeleteLocalRef(vri);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    LOGI("VT ==== end ====");
}

// Dump selected Java stacks without relying on SIGQUIT/tombstoned. The OH
// standalone runtime has no readable ActivityManager trace collector, while
// Thread.getAllStackTraces() also preserves the Java caller of native waits.
static void wl_dump_java_thread_stacks(JNIEnv* env) {
    jclass threadCls = env->FindClass("java/lang/Thread");
    jclass mapCls = env->FindClass("java/util/Map");
    jclass setCls = env->FindClass("java/util/Set");
    jclass entryCls = env->FindClass("java/util/Map$Entry");
    jclass elementCls = env->FindClass("java/lang/StackTraceElement");
    if (!threadCls || !mapCls || !setCls || !entryCls || !elementCls ||
        env->ExceptionCheck()) {
        wl_report_exception(env, "wl_dump_java_thread_stacks classes");
        return;
    }
    jmethodID allM = env->GetStaticMethodID(
        threadCls, "getAllStackTraces", "()Ljava/util/Map;");
    jmethodID nameM = env->GetMethodID(threadCls, "getName", "()Ljava/lang/String;");
    jmethodID idM = env->GetMethodID(threadCls, "getId", "()J");
    jmethodID entrySetM = env->GetMethodID(mapCls, "entrySet", "()Ljava/util/Set;");
    jmethodID toArrayM = env->GetMethodID(setCls, "toArray", "()[Ljava/lang/Object;");
    jmethodID keyM = env->GetMethodID(entryCls, "getKey", "()Ljava/lang/Object;");
    jmethodID valueM = env->GetMethodID(entryCls, "getValue", "()Ljava/lang/Object;");
    jmethodID stringM = env->GetMethodID(
        elementCls, "toString", "()Ljava/lang/String;");
    if (!allM || !nameM || !idM || !entrySetM || !toArrayM || !keyM ||
        !valueM || !stringM || env->ExceptionCheck()) {
        wl_report_exception(env, "wl_dump_java_thread_stacks methods");
        return;
    }

    jobject map = env->CallStaticObjectMethod(threadCls, allM);
    jobject entriesSet = map ? env->CallObjectMethod(map, entrySetM) : nullptr;
    jobjectArray entries = entriesSet ? static_cast<jobjectArray>(
        env->CallObjectMethod(entriesSet, toArrayM)) : nullptr;
    if (!entries || env->ExceptionCheck()) {
        wl_report_exception(env, "wl_dump_java_thread_stacks collect");
        return;
    }
    const jsize count = env->GetArrayLength(entries);
    LOGI("JSTACK ==== threads=%d ====", static_cast<int>(count));
    for (jsize i = 0; i < count; ++i) {
        jobject entry = env->GetObjectArrayElement(entries, i);
        jobject thread = entry ? env->CallObjectMethod(entry, keyM) : nullptr;
        jstring name = thread ? static_cast<jstring>(
            env->CallObjectMethod(thread, nameM)) : nullptr;
        const char* chars = name ? env->GetStringUTFChars(name, nullptr) : nullptr;
        const bool selected = chars != nullptr &&
            (strcmp(chars, "main") == 0 ||
             // AppSpawnX runs ActivityThread.main() on its dedicated child
             // pthread.  Java names that thread "Thread-1", even though its
             // Looper is the process main Looper; omitting it made the stack
             // probe silently hide the exact lifecycle stalls it exists to
             // diagnose.
             strcmp(chars, "Thread-1") == 0 ||
             strstr(chars, "Lynx") != nullptr ||
             strstr(chars, "CrBrowser") != nullptr ||
             strstr(chars, "TASM") != nullptr);
        if (selected) {
            const jlong id = env->CallLongMethod(thread, idM);
            jobjectArray stack = static_cast<jobjectArray>(
                env->CallObjectMethod(entry, valueM));
            const jsize depth = stack ? env->GetArrayLength(stack) : 0;
            LOGI("JSTACK -- name=%s javaId=%lld depth=%d --", chars,
                 static_cast<long long>(id), static_cast<int>(depth));
            for (jsize frame = 0; frame < depth; ++frame) {
                jobject element = env->GetObjectArrayElement(stack, frame);
                jstring line = element ? static_cast<jstring>(
                    env->CallObjectMethod(element, stringM)) : nullptr;
                const char* lineChars = line ? env->GetStringUTFChars(line, nullptr) : nullptr;
                LOGI("JSTACK   #%d %s", static_cast<int>(frame),
                     lineChars ? lineChars : "<unavailable>");
                if (line && lineChars) env->ReleaseStringUTFChars(line, lineChars);
                if (line) env->DeleteLocalRef(line);
                if (element) env->DeleteLocalRef(element);
                if (env->ExceptionCheck()) {
                    wl_report_exception(env, "wl_dump_java_thread_stacks frame");
                    break;
                }
            }
            if (stack) env->DeleteLocalRef(stack);
        }
        if (name && chars) env->ReleaseStringUTFChars(name, chars);
        if (name) env->DeleteLocalRef(name);
        if (thread) env->DeleteLocalRef(thread);
        if (entry) env->DeleteLocalRef(entry);
        if (env->ExceptionCheck()) {
            wl_report_exception(env, "wl_dump_java_thread_stacks entry");
            break;
        }
    }
    env->DeleteLocalRef(entries);
    env->DeleteLocalRef(entriesSet);
    env->DeleteLocalRef(map);
    env->DeleteLocalRef(elementCls);
    env->DeleteLocalRef(entryCls);
    env->DeleteLocalRef(setCls);
    env->DeleteLocalRef(mapCls);
    env->DeleteLocalRef(threadCls);
}

// ============================================================
// WESTLAKE §433 — reflectively inspect an APP class (libart-free probe)
// ============================================================
// The sound-library fetch dies on `NoSuchMethodError: InvokeType(4) Lq6/b;->b(Ln7/c;)…` even though
// dexlib2 proves the interface declares that method. Rebuilding libart to instrument the resolver
// is blocked (the deployed binary is not reproducible from the tree), so ask the RUNTIME instead:
// load the class through the app's own ClassLoader and list what reflection sees.
//   reflection sees b(...)  => the class linked fine; the bug is invoke-interface resolution/IMT
//   reflection does NOT     => the interface's abstract methods were mis-loaded at class-load time
// Drive it with:  echo "q q6.b" > /data/local/tmp/noice_tap
// §437: shared helper — the app ClassLoader via a live decor view.
static jobject wl_app_class_loader(JNIEnv* env);
// §570 — mark every method of an app class NON-COMPILABLE, so ART's JIT skips it.
//
// Why this exists: with the §568 relayout storm fixed, the JIT compiles real app code and then dies
// with a StackOverflowError whose FIRST occurrence is on a kotlinx CoroutineScheduler worker (it is
// swallowed by the no-op ThreadGroup.uncaughtException, so only main's death is visible). The last
// method compiled before it is kotlin.coroutines.jvm.internal.ContinuationImpl.resumeWith. Compiled
// code does not go through the interpreter's DoCall, so westlake's §436 interface-dispatch repair —
// which lives there — is bypassed and the continuation chain recurses. Excluding just those classes
// keeps the JIT for everything else.
//
// The flag was read out of the DEPLOYED binary, not guessed from AOSP source (values move between
// versions). art::jit::Jit::CompileMethodInternal @0x95f3a0 does:
//     ldr w8, [x19, #4]        ; ArtMethod::access_flags_  -> offset 4
//     and w8, w8, #0x82800000  ; kAccIntrinsic|kAccCompileDontBother|kAccPreCompiled
//     cmp w8, #0x02800000      ; IsPreCompiled(): DontBother|PreCompiled set, Intrinsic clear
// so kAccCompileDontBother = 0x02000000. Set it ALONE — setting 0x00800000 too would make the
// method look pre-compiled instead of skipped.
//
// ★jmethodID IS the ArtMethod* in ART, so FromReflectedMethod gives the pointer directly; no need
// to reach for the private Executable.artMethod field.
static const uint32_t kWlAccCompileDontBother = 0x02000000u;

void wl_jit_exclude_class(JNIEnv* env, const char* dotted);
void wl_jit_exclude_class(JNIEnv* env, const char* dotted) {
    // Same app-ClassLoader route as §433 below: reach a live decor view and ask its Context.
    jclass wmgCls = env->FindClass("android/view/WindowManagerGlobal");
    jmethodID getInst = wmgCls ? env->GetStaticMethodID(wmgCls, "getInstance",
        "()Landroid/view/WindowManagerGlobal;") : nullptr;
    jobject wmg = getInst ? env->CallStaticObjectMethod(wmgCls, getInst) : nullptr;
    if (wmg == nullptr) { if (env->ExceptionCheck()) env->ExceptionClear();
                          LOGE("[JIT-570] no WindowManagerGlobal"); return; }
    jfieldID mRootsF = env->GetFieldID(wmgCls, "mRoots", "Ljava/util/ArrayList;");
    jobject roots = mRootsF ? env->GetObjectField(wmg, mRootsF) : nullptr;
    jclass listCls = env->FindClass("java/util/ArrayList");
    jmethodID sizeM = env->GetMethodID(listCls, "size", "()I");
    jmethodID getM  = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
    jclass vriCls = env->FindClass("android/view/ViewRootImpl");
    jfieldID mViewF = env->GetFieldID(vriCls, "mView", "Landroid/view/View;");
    jclass viewCls = env->FindClass("android/view/View");
    jmethodID getCtx = env->GetMethodID(viewCls, "getContext", "()Landroid/content/Context;");
    jobject loader = nullptr;
    const jint nn = roots ? env->CallIntMethod(roots, sizeM) : 0;
    for (jint i = 0; i < nn && loader == nullptr; ++i) {
        jobject vri = env->CallObjectMethod(roots, getM, i);
        if (!vri) continue;
        jobject view = env->GetObjectField(vri, mViewF);
        if (view) {
            jobject ctx = env->CallObjectMethod(view, getCtx);
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (ctx) {
                jclass ctxCls = env->FindClass("android/content/Context");
                jmethodID getCL = env->GetMethodID(ctxCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
                loader = getCL ? env->CallObjectMethod(ctx, getCL) : nullptr;
                if (env->ExceptionCheck()) { env->ExceptionClear(); loader = nullptr; }
                env->DeleteLocalRef(ctx);
            }
            env->DeleteLocalRef(view);
        }
        env->DeleteLocalRef(vri);
    }
    if (loader == nullptr) { LOGE("[JIT-570] no app ClassLoader"); return; }

    jclass clCls = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClassM = env->GetMethodID(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring jn = env->NewStringUTF(dotted);
    jobject klass = env->CallObjectMethod(loader, loadClassM, jn);
    if (klass == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("[JIT-570] loadClass(%{public}s) FAILED — not excluded", dotted);
        return;
    }
    jclass classCls = env->FindClass("java/lang/Class");
    jmethodID getDeclM = env->GetMethodID(classCls, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
    jobjectArray ms = getDeclM ? (jobjectArray) env->CallObjectMethod(klass, getDeclM) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); ms = nullptr; }
    if (ms == nullptr) { LOGE("[JIT-570] getDeclaredMethods(%{public}s) failed", dotted); return; }

    const jsize mn = env->GetArrayLength(ms);
    int marked = 0, already = 0;
    for (jsize i = 0; i < mn; ++i) {
        jobject m = env->GetObjectArrayElement(ms, i);
        if (m == nullptr) continue;
        jmethodID mid = env->FromReflectedMethod(m);      // == ArtMethod*
        if (mid != nullptr) {
            uint32_t* af = reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(mid) + 4);
            if ((*af & kWlAccCompileDontBother) != 0) { ++already; }
            else { *af |= kWlAccCompileDontBother; ++marked; }
        }
        env->DeleteLocalRef(m);
    }
    env->DeleteLocalRef(ms);
    // §572: fprintf(stderr) — LOGE drops %{public}s args in this build (logged empty before).
    fprintf(stderr, "[JIT-570] %s: %d method(s) marked non-compilable (%d already), of %d\n",
            dotted, marked, already, (int) mn);
    fflush(stderr);
}

void wl_inspect_app_class(JNIEnv* env, const char* dotted);
void wl_inspect_app_class(JNIEnv* env, const char* dotted) {
    jclass wmgCls = env->FindClass("android/view/WindowManagerGlobal");
    jmethodID getInst = wmgCls ? env->GetStaticMethodID(wmgCls, "getInstance",
        "()Landroid/view/WindowManagerGlobal;") : nullptr;
    jobject wmg = getInst ? env->CallStaticObjectMethod(wmgCls, getInst) : nullptr;
    if (wmg == nullptr) { if (env->ExceptionCheck()) env->ExceptionClear();
                          LOGE("§433: no WindowManagerGlobal"); return; }
    jfieldID mRootsF = env->GetFieldID(wmgCls, "mRoots", "Ljava/util/ArrayList;");
    jobject roots = mRootsF ? env->GetObjectField(wmg, mRootsF) : nullptr;
    jclass listCls = env->FindClass("java/util/ArrayList");
    jmethodID sizeM = env->GetMethodID(listCls, "size", "()I");
    jmethodID getM  = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
    jclass vriCls = env->FindClass("android/view/ViewRootImpl");
    jfieldID mViewF = env->GetFieldID(vriCls, "mView", "Landroid/view/View;");
    jclass viewCls = env->FindClass("android/view/View");
    jmethodID getCtx = env->GetMethodID(viewCls, "getContext", "()Landroid/content/Context;");
    jobject loader = nullptr;
    const jint nn = roots ? env->CallIntMethod(roots, sizeM) : 0;
    for (jint i = 0; i < nn && loader == nullptr; ++i) {
        jobject vri = env->CallObjectMethod(roots, getM, i);
        if (!vri) continue;
        jobject view = env->GetObjectField(vri, mViewF);
        if (view) {
            jobject ctx = env->CallObjectMethod(view, getCtx);
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (ctx) {
                jclass ctxCls = env->FindClass("android/content/Context");
                jmethodID getCL = env->GetMethodID(ctxCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
                loader = getCL ? env->CallObjectMethod(ctx, getCL) : nullptr;
                if (env->ExceptionCheck()) { env->ExceptionClear(); loader = nullptr; }
                env->DeleteLocalRef(ctx);
            }
            env->DeleteLocalRef(view);
        }
        env->DeleteLocalRef(vri);
    }
    if (loader == nullptr) { LOGE("§433: no app ClassLoader"); return; }

    jclass clCls = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClassM = env->GetMethodID(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring jn = env->NewStringUTF(dotted);
    jobject klass = env->CallObjectMethod(loader, loadClassM, jn);
    if (klass == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("§433: loadClass(%s) FAILED", dotted);
        return;
    }
    jclass classCls = env->FindClass("java/lang/Class");
    jmethodID isIface = env->GetMethodID(classCls, "isInterface", "()Z");
    jmethodID getMods = env->GetMethodID(classCls, "getModifiers", "()I");
    jmethodID getDM   = env->GetMethodID(classCls, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
    jmethodID getName = env->GetMethodID(classCls, "getName", "()Ljava/lang/String;");
    // §433b: identity of the Class object + its defining ClassLoader + dex origin. If the call
    // site's interface resolves to a DIFFERENT Class (duplicate load by two ClassLoaders), the
    // index-based interface lookup fails even though reflection on "a" q6.b succeeds.
    {
        jmethodID getCL2 = env->GetMethodID(classCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
        jobject kl = getCL2 ? env->CallObjectMethod(klass, getCL2) : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
        jclass objCls = env->FindClass("java/lang/Object");
        jmethodID hash = env->GetMethodID(objCls, "hashCode", "()I");
        jmethodID toStr = env->GetMethodID(objCls, "toString", "()Ljava/lang/String;");
        jstring ls = kl ? (jstring)env->CallObjectMethod(kl, toStr) : nullptr;
        const char* lc = ls ? env->GetStringUTFChars(ls, nullptr) : nullptr;
        LOGI("§433 %s: classHash=0x%x loaderHash=0x%x loader=%s",
             dotted, (int)env->CallIntMethod(klass, hash),
             kl ? (int)env->CallIntMethod(kl, hash) : 0, lc ? lc : "(null/boot)");
        if (ls && lc) env->ReleaseStringUTFChars(ls, lc);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    LOGI("§433 %s: isInterface=%d modifiers=0x%x",
         dotted, (int)env->CallBooleanMethod(klass, isIface),
         (int)env->CallIntMethod(klass, getMods));
    jobjectArray methods = (jobjectArray)env->CallObjectMethod(klass, getDM);
    if (methods == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("§433 %s: getDeclaredMethods FAILED", dotted);
        return;
    }
    jclass mCls = env->FindClass("java/lang/reflect/Method");
    jmethodID mName = env->GetMethodID(mCls, "getName", "()Ljava/lang/String;");
    jmethodID mParams = env->GetMethodID(mCls, "getParameterTypes", "()[Ljava/lang/Class;");
    jmethodID mRet = env->GetMethodID(mCls, "getReturnType", "()Ljava/lang/Class;");
    jmethodID mMods = env->GetMethodID(mCls, "getModifiers", "()I");
    const jsize n = env->GetArrayLength(methods);
    LOGI("§433 %s: %d declared method(s)", dotted, (int)n);
    for (jsize i = 0; i < n; ++i) {
        jobject m = env->GetObjectArrayElement(methods, i);
        if (!m) continue;
        jstring nm = (jstring)env->CallObjectMethod(m, mName);
        const char* nc = nm ? env->GetStringUTFChars(nm, nullptr) : nullptr;
        jobjectArray ps = (jobjectArray)env->CallObjectMethod(m, mParams);
        const jsize pn = ps ? env->GetArrayLength(ps) : 0;
        char params[256]; params[0] = 0;
        for (jsize k = 0; k < pn; ++k) {
            jobject pc = env->GetObjectArrayElement(ps, k);
            jstring pnm = pc ? (jstring)env->CallObjectMethod(pc, getName) : nullptr;
            const char* pc2 = pnm ? env->GetStringUTFChars(pnm, nullptr) : nullptr;
            if (pc2) { strncat(params, pc2, sizeof(params) - strlen(params) - 2); strncat(params, ",", 2); }
            if (pnm && pc2) env->ReleaseStringUTFChars(pnm, pc2);
            if (pnm) env->DeleteLocalRef(pnm);
            if (pc) env->DeleteLocalRef(pc);
        }
        jobject rt = env->CallObjectMethod(m, mRet);
        jstring rnm = rt ? (jstring)env->CallObjectMethod(rt, getName) : nullptr;
        const char* rc = rnm ? env->GetStringUTFChars(rnm, nullptr) : nullptr;
        LOGI("§433   %s(%s) -> %s  mods=0x%x",
             nc ? nc : "?", params, rc ? rc : "?", (int)env->CallIntMethod(m, mMods));
        if (rnm && rc) env->ReleaseStringUTFChars(rnm, rc);
        if (nm && nc) env->ReleaseStringUTFChars(nm, nc);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(m);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// §437: the app ClassLoader, via a live decor view (same route resolveTouchInjector uses).
static jobject wl_app_class_loader(JNIEnv* env) {
    jclass wmgCls = env->FindClass("android/view/WindowManagerGlobal");
    jmethodID getInst = wmgCls ? env->GetStaticMethodID(wmgCls, "getInstance",
        "()Landroid/view/WindowManagerGlobal;") : nullptr;
    jobject wmg = getInst ? env->CallStaticObjectMethod(wmgCls, getInst) : nullptr;
    if (wmg == nullptr) { if (env->ExceptionCheck()) env->ExceptionClear(); return nullptr; }
    jfieldID mRootsF = env->GetFieldID(wmgCls, "mRoots", "Ljava/util/ArrayList;");
    jobject roots = mRootsF ? env->GetObjectField(wmg, mRootsF) : nullptr;
    if (roots == nullptr) return nullptr;
    jclass listCls = env->FindClass("java/util/ArrayList");
    jmethodID sizeM = env->GetMethodID(listCls, "size", "()I");
    jmethodID getM  = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
    jclass vriCls = env->FindClass("android/view/ViewRootImpl");
    jfieldID mViewF = env->GetFieldID(vriCls, "mView", "Landroid/view/View;");
    jclass viewCls = env->FindClass("android/view/View");
    jmethodID getCtx = env->GetMethodID(viewCls, "getContext", "()Landroid/content/Context;");
    jclass ctxCls = env->FindClass("android/content/Context");
    jmethodID getCL = env->GetMethodID(ctxCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
    const jint nn = env->CallIntMethod(roots, sizeM);
    for (jint i = 0; i < nn; ++i) {
        jobject vri = env->CallObjectMethod(roots, getM, i);
        if (!vri) continue;
        jobject view = env->GetObjectField(vri, mViewF);
        if (view) {
            jobject ctx = env->CallObjectMethod(view, getCtx);
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (ctx) {
                jobject cl = env->CallObjectMethod(ctx, getCL);
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(ctx);
                if (cl) { env->DeleteLocalRef(view); env->DeleteLocalRef(vri); return cl; }
            }
            env->DeleteLocalRef(view);
        }
        env->DeleteLocalRef(vri);
    }
    return nullptr;
}

// WESTLAKE §802 diagnostic: inspect Toutiao's live inner-flow response without
// modifying the APK.  A screenshot only proves that the loading view is still
// visible; it cannot distinguish a missing HTTP response from a response that
// was decoded but never committed to the adapter.  Command `G` follows the
// objects already owned by the foreground Activity and prints both sides of
// that boundary: response/list state and adapter/loading-view state.
//
// This is deliberately an opt-in diagnostic command.  It does not run during
// normal application execution and does not alter any field.
static void wl_dump_toutiao_aggr_state(JNIEnv* env) {
    jobject loader = wl_app_class_loader(env);
    if (loader == nullptr) {
        fprintf(stderr, "[WESTLAKE-AGGR-802] no app ClassLoader\n");
        return;
    }

    jclass clCls = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClassM = clCls ? env->GetMethodID(
        clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    auto loadAppClass = [&](const char* name) -> jclass {
        jstring jname = env->NewStringUTF(name);
        jobject value = (loadClassM && jname)
            ? env->CallObjectMethod(loader, loadClassM, jname) : nullptr;
        if (jname) env->DeleteLocalRef(jname);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            value = nullptr;
        }
        return static_cast<jclass>(value);
    };

    jclass activityCls = loadAppClass(
        "com.bytedance.ugc.forum.innerfeed.ArticleInflowActivity");
    jclass fragmentCls = loadAppClass(
        "com.bytedance.ugc.forum.aggrlist.ArticleInflowFragment");
    jclass helperCls = loadAppClass(
        "com.bytedance.ugc.aggr.base.UgcAggrViewHelper");
    jclass presenterCls = loadAppClass(
        "com.bytedance.ugc.aggr.base.UgcAggrListPresenter");
    jclass responseCls = loadAppClass(
        "com.bytedance.ugc.aggr.api.UgcAggrListResponse");
    if (!activityCls || !fragmentCls || !helperCls || !presenterCls || !responseCls) {
        fprintf(stderr, "[WESTLAKE-AGGR-802] required Toutiao class missing\n");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }

    // Find the foreground ArticleInflowActivity by unwrapping each decor
    // Context.  This avoids ActivityManager/Binder and stays inside the live
    // process, like the existing view-tree diagnostics.
    jobject activity = nullptr;
    jclass wmgCls = env->FindClass("android/view/WindowManagerGlobal");
    jmethodID getInstM = wmgCls ? env->GetStaticMethodID(
        wmgCls, "getInstance", "()Landroid/view/WindowManagerGlobal;") : nullptr;
    jobject wmg = getInstM ? env->CallStaticObjectMethod(wmgCls, getInstM) : nullptr;
    jfieldID rootsF = wmgCls ? env->GetFieldID(
        wmgCls, "mRoots", "Ljava/util/ArrayList;") : nullptr;
    jobject roots = (wmg && rootsF) ? env->GetObjectField(wmg, rootsF) : nullptr;
    jclass listCls = env->FindClass("java/util/List");
    jmethodID listSizeM = listCls ? env->GetMethodID(listCls, "size", "()I") : nullptr;
    jmethodID listGetM = listCls ? env->GetMethodID(
        listCls, "get", "(I)Ljava/lang/Object;") : nullptr;
    jclass vriCls = env->FindClass("android/view/ViewRootImpl");
    jfieldID viewF = vriCls ? env->GetFieldID(vriCls, "mView", "Landroid/view/View;") : nullptr;
    jclass viewCls = env->FindClass("android/view/View");
    jmethodID getContextM = viewCls ? env->GetMethodID(
        viewCls, "getContext", "()Landroid/content/Context;") : nullptr;
    jclass wrapperCls = env->FindClass("android/content/ContextWrapper");
    jmethodID baseContextM = wrapperCls ? env->GetMethodID(
        wrapperCls, "getBaseContext", "()Landroid/content/Context;") : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();

    const jint rootCount = (roots && listSizeM)
        ? env->CallIntMethod(roots, listSizeM) : 0;
    for (jint i = rootCount - 1; i >= 0 && activity == nullptr; --i) {
        jobject vri = env->CallObjectMethod(roots, listGetM, i);
        jobject view = (vri && viewF) ? env->GetObjectField(vri, viewF) : nullptr;
        jobject context = (view && getContextM)
            ? env->CallObjectMethod(view, getContextM) : nullptr;
        for (int depth = 0; context && depth < 12; ++depth) {
            if (env->IsInstanceOf(context, activityCls)) {
                activity = env->NewLocalRef(context);
                break;
            }
            if (!wrapperCls || !baseContextM ||
                !env->IsInstanceOf(context, wrapperCls)) break;
            jobject base = env->CallObjectMethod(context, baseContextM);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                base = nullptr;
            }
            if (base == context) break;
            context = base;
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    if (activity == nullptr) {
        fprintf(stderr, "[WESTLAKE-AGGR-802] ArticleInflowActivity is not foreground\n");
        return;
    }

    jfieldID fragmentF = env->GetFieldID(
        activityCls, "fragment",
        "Lcom/bytedance/ugc/forum/aggrlist/ArticleInflowFragment;");
    jobject fragment = fragmentF ? env->GetObjectField(activity, fragmentF) : nullptr;
    jfieldID helperF = env->GetFieldID(
        fragmentCls, "a", "Lcom/bytedance/ugc/aggr/base/UgcAggrViewHelper;");
    jobject helper = (fragment && helperF) ? env->GetObjectField(fragment, helperF) : nullptr;
    jmethodID presenterM = env->GetMethodID(
        helperCls, "y", "()Lcom/bytedance/ugc/aggr/base/UgcAggrListPresenter;");
    jobject presenter = (helper && presenterM)
        ? env->CallObjectMethod(helper, presenterM) : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        presenter = nullptr;
    }
    if (!fragment || !helper || !presenter) {
        fprintf(stderr,
                "[WESTLAKE-AGGR-802] graph incomplete fragment=%d helper=%d presenter=%d\n",
                fragment != nullptr, helper != nullptr, presenter != nullptr);
        return;
    }

    jmethodID responseM = env->GetMethodID(
        presenterCls, "Q", "()Lcom/bytedance/ugc/aggr/api/UgcAggrListResponse;");
    jmethodID mergedListM = env->GetMethodID(presenterCls, "M", "()Ljava/util/List;");
    jobject response = responseM ? env->CallObjectMethod(presenter, responseM) : nullptr;
    jobject merged = mergedListM ? env->CallObjectMethod(presenter, mergedListM) : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();
    const jint mergedSize = (merged && listSizeM)
        ? env->CallIntMethod(merged, listSizeM) : -1;

    auto boolField = [&](jobject object, jclass klass, const char* name) -> int {
        jfieldID field = env->GetFieldID(klass, name, "Z");
        if (!field || env->ExceptionCheck()) {
            env->ExceptionClear();
            return -1;
        }
        return env->GetBooleanField(object, field) ? 1 : 0;
    };
    auto intField = [&](jobject object, jclass klass, const char* name) -> int {
        jfieldID field = env->GetFieldID(klass, name, "I");
        if (!field || env->ExceptionCheck()) {
            env->ExceptionClear();
            return -9999;
        }
        return env->GetIntField(object, field);
    };
    auto objectText = [&](jobject object) -> std::string {
        if (!object) return "<null>";
        jclass objectCls = env->FindClass("java/lang/Object");
        jmethodID toStringM = objectCls ? env->GetMethodID(
            objectCls, "toString", "()Ljava/lang/String;") : nullptr;
        jstring text = toStringM
            ? static_cast<jstring>(env->CallObjectMethod(object, toStringM)) : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return "<toString failed>";
        }
        const char* chars = text ? env->GetStringUTFChars(text, nullptr) : nullptr;
        std::string result = chars ? chars : "<null>";
        if (text && chars) env->ReleaseStringUTFChars(text, chars);
        if (text) env->DeleteLocalRef(text);
        return result;
    };

    if (response == nullptr) {
        fprintf(stderr,
                "[WESTLAKE-AGGR-802] response=<null> mergedSize=%d presenter "
                "b=%d d=%d h=%d C=%d E=%d F=%d G=%d I=%d J=%d K=%d L=%d M=%d P=%d Q=%d\n",
                static_cast<int>(mergedSize),
                boolField(presenter, presenterCls, "b"),
                boolField(presenter, presenterCls, "d"),
                boolField(presenter, presenterCls, "h"),
                boolField(presenter, presenterCls, "C"),
                boolField(presenter, presenterCls, "E"),
                boolField(presenter, presenterCls, "F"),
                boolField(presenter, presenterCls, "G"),
                boolField(presenter, presenterCls, "I"),
                boolField(presenter, presenterCls, "J"),
                intField(presenter, presenterCls, "K"),
                boolField(presenter, presenterCls, "L"),
                boolField(presenter, presenterCls, "M"),
                boolField(presenter, presenterCls, "P"),
                intField(presenter, presenterCls, "Q"));
    } else {
        jfieldID itemsF = env->GetFieldID(responseCls, "b", "Ljava/util/ArrayList;");
        jfieldID errorF = env->GetFieldID(responseCls, "j", "Ljava/lang/Throwable;");
        jfieldID messageF = env->GetFieldID(responseCls, "k", "Ljava/lang/String;");
        jfieldID logIdF = env->GetFieldID(responseCls, "l", "Ljava/lang/String;");
        jobject items = itemsF ? env->GetObjectField(response, itemsF) : nullptr;
        jobject error = errorF ? env->GetObjectField(response, errorF) : nullptr;
        jobject message = messageF ? env->GetObjectField(response, messageF) : nullptr;
        jobject logId = logIdF ? env->GetObjectField(response, logIdF) : nullptr;
        const jint responseSize = (items && listSizeM)
            ? env->CallIntMethod(items, listSizeM) : -1;
        const std::string errorText = objectText(error);
        const std::string messageText = objectText(message);
        const std::string logIdText = objectText(logId);
        fprintf(stderr,
                "[WESTLAKE-AGGR-802] responseSize=%d mergedSize=%d "
                "state(h)=%d code(i)=%d hasMore(a)=%d loadMore(d)=%d "
                "fromCache(m)=%d flag(u)=%d message=%s logId=%s error=%s\n",
                static_cast<int>(responseSize), static_cast<int>(mergedSize),
                intField(response, responseCls, "h"),
                intField(response, responseCls, "i"),
                boolField(response, responseCls, "a"),
                boolField(response, responseCls, "d"),
                boolField(response, responseCls, "m"),
                boolField(response, responseCls, "u"),
                messageText.c_str(), logIdText.c_str(), errorText.c_str());
    }

    // Compare presenter data with the RecyclerView adapter and state view.
    jfieldID adapterF = env->GetFieldID(
        helperCls, "K", "Lcom/bytedance/ugc/aggr/base/UGCAggrListAdapterWrapper;");
    jobject adapter = adapterF ? env->GetObjectField(helper, adapterF) : nullptr;
    jint adapterCount = -1;
    if (adapter) {
        jclass adapterCls = env->GetObjectClass(adapter);
        jmethodID countM = adapterCls
            ? env->GetMethodID(adapterCls, "getItemCount", "()I") : nullptr;
        if (countM) adapterCount = env->CallIntMethod(adapter, countM);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            adapterCount = -2;
        }
    }
    jfieldID stateF = env->GetFieldID(
        helperCls, "U", "Lcom/bytedance/ugc/aggr/base/AggrStateViewHelper;");
    jobject state = stateF ? env->GetObjectField(helper, stateF) : nullptr;
    int stateLoading = -1, stateP = -1, stateQ = -1, stateR = -1,
        stateW = -1, stateY = -1, stateMode = -9999;
    if (state) {
        jclass stateCls = env->GetObjectClass(state);
        stateLoading = boolField(state, stateCls, "i");
        stateP = boolField(state, stateCls, "p");
        stateQ = boolField(state, stateCls, "q");
        stateR = boolField(state, stateCls, "r");
        stateW = boolField(state, stateCls, "w");
        stateY = boolField(state, stateCls, "y");
        stateMode = intField(state, stateCls, "u");
    }
    fprintf(stderr,
            "[WESTLAKE-AGGR-802] adapterCount=%d state i=%d p=%d q=%d r=%d w=%d y=%d u=%d\n",
            static_cast<int>(adapterCount), stateLoading, stateP, stateQ,
            stateR, stateW, stateY, stateMode);
    fflush(stderr);
}

// WESTLAKE §803 diagnostic: report all live gates in Toutiao's Cronet success
// callback chain together.  The persisted boot-failure preference is updated
// before native startup and can legitimately be one while the first process is
// healthy; AppConfig, HttpClient and TON state in the same process distinguish
// that case from a callback that was dropped.
static void wl_dump_toutiao_cronet_state(JNIEnv* env) {
    jobject loader = wl_app_class_loader(env);
    jclass clCls = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClassM = clCls ? env->GetMethodID(
        clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    if (!loader || !loadClassM || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        fprintf(stderr, "[WESTLAKE-CRONET-803] no app ClassLoader\n");
        return;
    }
    auto loadAppClass = [&](const char* name) -> jclass {
        jstring jname = env->NewStringUTF(name);
        jobject value = env->CallObjectMethod(loader, loadClassM, jname);
        if (jname) env->DeleteLocalRef(jname);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            value = nullptr;
        }
        return static_cast<jclass>(value);
    };
    auto staticBoolean = [&](jclass klass, const char* name) -> int {
        jfieldID field = klass ? env->GetStaticFieldID(klass, name, "Z") : nullptr;
        if (!field || env->ExceptionCheck()) {
            env->ExceptionClear();
            return -1;
        }
        return env->GetStaticBooleanField(klass, field) ? 1 : 0;
    };

    jclass configCls = loadAppClass("com.bytedance.ttnet.config.AppConfig");
    jclass httpClientCls = loadAppClass("com.bytedance.ttnet.HttpClient");
    jclass listenerCls = loadAppClass("X.TON");
    jclass ssClientCls = loadAppClass(
        "com.bytedance.frameworks.baselib.network.http.cronet.impl.SsCronetHttpClient");
    jobject config = nullptr;
    if (configCls) {
        jfieldID instanceF = env->GetStaticFieldID(
            configCls, "mInstance", "Lcom/bytedance/ttnet/config/AppConfig;");
        if (instanceF) config = env->GetStaticObjectField(configCls, instanceF);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            config = nullptr;
        }
    }
    int bootFailures = -1;
    int configMain = -1;
    if (config) {
        jfieldID failuresF = env->GetFieldID(configCls, "mChromiumBootFailures", "I");
        jfieldID mainF = env->GetFieldID(configCls, "mIsMainProcess", "Z");
        if (failuresF) bootFailures = env->GetIntField(config, failuresF);
        if (mainF) configMain = env->GetBooleanField(config, mainF) ? 1 : 0;
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    jobject listener = nullptr;
    int listenerMain = -1;
    if (listenerCls) {
        jfieldID listenerF = env->GetStaticFieldID(listenerCls, "b", "LX/TON;");
        if (listenerF) listener = env->GetStaticObjectField(listenerCls, listenerF);
        if (listener) {
            jfieldID mainF = env->GetFieldID(listenerCls, "a", "Z");
            if (mainF) listenerMain = env->GetBooleanField(listener, mainF) ? 1 : 0;
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    jobject cronetClient = nullptr;
    if (ssClientCls) {
        jfieldID clientF = env->GetStaticFieldID(
            ssClientCls, "sCronetClient",
            "Lcom/bytedance/frameworks/baselib/network/http/cronet/ICronetClient;");
        if (clientF) cronetClient = env->GetStaticObjectField(ssClientCls, clientF);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            cronetClient = nullptr;
        }
    }

    jstring processName = nullptr;
    jclass activityThreadCls = env->FindClass("android/app/ActivityThread");
    jmethodID processNameM = activityThreadCls ? env->GetStaticMethodID(
        activityThreadCls, "currentProcessName", "()Ljava/lang/String;") : nullptr;
    if (processNameM) processName = static_cast<jstring>(
        env->CallStaticObjectMethod(activityThreadCls, processNameM));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        processName = nullptr;
    }
    const char* processChars = processName
        ? env->GetStringUTFChars(processName, nullptr) : nullptr;
    fprintf(stderr,
            "[WESTLAKE-CRONET-803] process=%s appConfig=%d bootFailures=%d "
            "configMain=%d listener=%d listenerMain=%d client=%d "
            "bootSucceed=%d cronetException=%d forceNoCronet=%d "
            "disableFallbackBoot=%d\n",
            processChars ? processChars : "<null>", config != nullptr,
            bootFailures, configMain, listener != nullptr, listenerMain,
            cronetClient != nullptr,
            staticBoolean(httpClientCls, "sCronetBootSucceed"),
            staticBoolean(httpClientCls, "sIsCronetException"),
            staticBoolean(configCls, "sForceNotUseCronet"),
            staticBoolean(configCls, "sDisableFallbackReasonBoot"));
    fflush(stderr);
    if (processName && processChars) env->ReleaseStringUTFChars(processName, processChars);
}

// ============================================================
// WESTLAKE §437 — PROXY PROBE: does JNI dispatch on a Proxy work?
// ============================================================
// §436 root-caused the invoke-interface NoSuchMethodError to our own validator in
// FindMethodToCall (entrypoint_utils-inl.h): it calls GetNameView()/GetSignature() on the
// *proxy* ArtMethod, which AOSP forbids (DCHECK(!IsProxyMethod()), compiled out by -DNDEBUG),
// reads the wrong dex, and fabricates a signature mismatch.
// PREDICTION: JNI dispatches from an already-resolved jmethodID and never consults that
// validator, so the SAME call that fails from app bytecode must SUCCEED through JNI.
// Annotations are java.lang.reflect.Proxy instances, and `g9.f.value()` is one of the two
// failing sites — so annotations on the app's own Retrofit interface give us live proxies
// without needing to reach into the app's object graph.
// Drive with:  echo "p q6.b" > /data/local/tmp/noice_tap
void wl_proxy_probe(JNIEnv* env, const char* dotted);
void wl_proxy_probe(JNIEnv* env, const char* dotted) {
    jobject loader = wl_app_class_loader(env);
    if (loader == nullptr) { LOGE("§437: no app ClassLoader"); return; }
    jclass clCls = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClassM = env->GetMethodID(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring jn = env->NewStringUTF(dotted);
    jobject klass = env->CallObjectMethod(loader, loadClassM, jn);
    if (klass == nullptr || env->ExceptionCheck()) { env->ExceptionClear();
        LOGE("§437: loadClass(%s) failed", dotted); return; }

    jclass classCls = env->FindClass("java/lang/Class");
    jmethodID getDM = env->GetMethodID(classCls, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
    jmethodID getName = env->GetMethodID(classCls, "getName", "()Ljava/lang/String;");
    jclass mCls = env->FindClass("java/lang/reflect/Method");
    jmethodID mAnns = env->GetMethodID(mCls, "getAnnotations", "()[Ljava/lang/annotation/Annotation;");
    jmethodID mName = env->GetMethodID(mCls, "getName", "()Ljava/lang/String;");
    jclass annCls = env->FindClass("java/lang/annotation/Annotation");
    jmethodID annType = env->GetMethodID(annCls, "annotationType", "()Ljava/lang/Class;");
    jclass objCls = env->FindClass("java/lang/Object");
    jmethodID objGetClass = env->GetMethodID(objCls, "getClass", "()Ljava/lang/Class;");
    if (env->ExceptionCheck()) env->ExceptionClear();

    jobjectArray methods = (jobjectArray)env->CallObjectMethod(klass, getDM);
    if (methods == nullptr) { if (env->ExceptionCheck()) env->ExceptionClear();
        LOGE("§437: getDeclaredMethods failed"); return; }
    const jsize nm = env->GetArrayLength(methods);
    int probed = 0;
    for (jsize i = 0; i < nm; ++i) {
        jobject m = env->GetObjectArrayElement(methods, i);
        if (!m) continue;
        jstring mn = (jstring)env->CallObjectMethod(m, mName);
        const char* mnc = mn ? env->GetStringUTFChars(mn, nullptr) : nullptr;
        jobjectArray anns = (jobjectArray)env->CallObjectMethod(m, mAnns);
        if (env->ExceptionCheck()) { env->ExceptionClear(); anns = nullptr; }
        const jsize na = anns ? env->GetArrayLength(anns) : 0;
        for (jsize k = 0; k < na; ++k) {
            jobject a = env->GetObjectArrayElement(anns, k);
            if (!a) continue;
            // concrete runtime class of the annotation instance -> expect $ProxyN
            jobject acls = env->CallObjectMethod(a, objGetClass);
            jstring acn = acls ? (jstring)env->CallObjectMethod(acls, getName) : nullptr;
            const char* acc = acn ? env->GetStringUTFChars(acn, nullptr) : nullptr;
            // the annotation INTERFACE it implements
            jobject at = env->CallObjectMethod(a, annType);
            jstring atn = at ? (jstring)env->CallObjectMethod(at, getName) : nullptr;
            const char* atc = atn ? env->GetStringUTFChars(atn, nullptr) : nullptr;
            // THE TEST: invoke value() on the proxy through JNI
            const char* result = "(no value() method)";
            char buf[192];
            if (at != nullptr) {
                jmethodID valueM = env->GetMethodID((jclass)at, "value", "()Ljava/lang/String;");
                if (!valueM || env->ExceptionCheck()) { env->ExceptionClear(); }
                else {
                    jstring v = (jstring)env->CallObjectMethod(a, valueM);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        result = "*** THREW ***";
                    } else if (v != nullptr) {
                        const char* vc = env->GetStringUTFChars(v, nullptr);
                        snprintf(buf, sizeof(buf), "OK -> \"%s\"", vc ? vc : "?");
                        if (vc) env->ReleaseStringUTFChars(v, vc);
                        result = buf;
                    } else {
                        result = "OK -> null";
                    }
                    probed++;
                }
            }
            LOGI("§437 %s.%s  annotation=%s  impl=%s  value()=%s",
                 dotted, mnc ? mnc : "?", atc ? atc : "?", acc ? acc : "?", result);
            if (atn && atc) env->ReleaseStringUTFChars(atn, atc);
            if (acn && acc) env->ReleaseStringUTFChars(acn, acc);
            if (atn) env->DeleteLocalRef(atn);
            if (acn) env->DeleteLocalRef(acn);
            if (at) env->DeleteLocalRef(at);
            if (acls) env->DeleteLocalRef(acls);
            env->DeleteLocalRef(a);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        if (mn && mnc) env->ReleaseStringUTFChars(mn, mnc);
        if (mn) env->DeleteLocalRef(mn);
        if (anns) env->DeleteLocalRef(anns);
        env->DeleteLocalRef(m);
    }
    LOGI("§437 done: %d proxy value() call(s) attempted via JNI", probed);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// ============================================================
// WESTLAKE §438 — the decisive bytecode-vs-JNI proxy test
// ============================================================
// §436: our validator in FindMethodToCall calls GetNameView()/GetSignature() on the *proxy*
// ArtMethod, which AOSP forbids, so it fabricates a signature mismatch and throws
// NoSuchMethodError naming the interface. PREDICTION: JNI dispatches from an already-resolved
// jmethodID and never runs that validator, so the SAME call must succeed through JNI.
// §437 tried to get a proxy from annotations; Method.getAnnotations() SIGSEGVs on a bridge thread.
// So instead: borrow an InvocationHandler from a proxy the adapter ALREADY installed
// (AppSpawnXInit stubs IActivityManager/IPackageManager with dynamic Proxies), and build a
// q6.b proxy with it. Then JNI-call exactly the method the app fails on.
// Drive with:  echo "x q6.b b (Ln7/c;)Ljava/lang/Object;" > /data/local/tmp/noice_tap
void wl_proxy_dispatch_test(JNIEnv* env, const char* dotted, const char* mname, const char* msig);
void wl_proxy_dispatch_test(JNIEnv* env, const char* dotted, const char* mname, const char* msig) {
    jclass proxyCls = env->FindClass("java/lang/reflect/Proxy");
    jmethodID isProxyM = proxyCls ? env->GetStaticMethodID(proxyCls, "isProxyClass",
        "(Ljava/lang/Class;)Z") : nullptr;
    jmethodID getIhM = proxyCls ? env->GetStaticMethodID(proxyCls, "getInvocationHandler",
        "(Ljava/lang/Object;)Ljava/lang/reflect/InvocationHandler;") : nullptr;
    jmethodID newProxyM = proxyCls ? env->GetStaticMethodID(proxyCls, "newProxyInstance",
        "(Ljava/lang/ClassLoader;[Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)Ljava/lang/Object;") : nullptr;
    jclass objCls = env->FindClass("java/lang/Object");
    jmethodID objGetClass = env->GetMethodID(objCls, "getClass", "()Ljava/lang/Class;");
    jclass classCls = env->FindClass("java/lang/Class");
    jmethodID clsGetName = env->GetMethodID(classCls, "getName", "()Ljava/lang/String;");
    if (!isProxyM || !getIhM || !newProxyM) { if (env->ExceptionCheck()) env->ExceptionClear();
        LOGE("§438: java.lang.reflect.Proxy statics unresolved"); return; }

    // --- 1. find ANY existing proxy in the process to borrow a handler from ---
    struct Src { const char* cls; const char* m; const char* sig; };
    static const Src kSrcs[] = {
        {"android/app/ActivityManager",  "getService",       "()Landroid/app/IActivityManager;"},
        {"android/app/ActivityThread",   "getPackageManager","()Landroid/content/pm/IPackageManager;"},
        {"android/app/ActivityManagerNative", "getDefault",  "()Landroid/app/IActivityManager;"},
    };
    jobject handler = nullptr;
    for (const Src& src : kSrcs) {
        jclass c = env->FindClass(src.cls);
        if (!c || env->ExceptionCheck()) { env->ExceptionClear(); continue; }
        jmethodID mid = env->GetStaticMethodID(c, src.m, src.sig);
        if (!mid || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); continue; }
        jobject o = env->CallStaticObjectMethod(c, mid);
        if (env->ExceptionCheck()) { env->ExceptionClear(); o = nullptr; }
        if (o != nullptr) {
            jobject oc = env->CallObjectMethod(o, objGetClass);
            jboolean isP = oc ? env->CallStaticBooleanMethod(proxyCls, isProxyM, oc) : JNI_FALSE;
            jstring ocn = oc ? (jstring)env->CallObjectMethod(oc, clsGetName) : nullptr;
            const char* occ = ocn ? env->GetStringUTFChars(ocn, nullptr) : nullptr;
            LOGI("§438 source %s.%s -> %s isProxy=%d", src.cls, src.m, occ ? occ : "?", (int)isP);
            if (ocn && occ) env->ReleaseStringUTFChars(ocn, occ);
            if (isP) {
                handler = env->CallStaticObjectMethod(proxyCls, getIhM, o);
                if (env->ExceptionCheck()) { env->ExceptionClear(); handler = nullptr; }
            }
            if (oc) env->DeleteLocalRef(oc);
            env->DeleteLocalRef(o);
        }
        env->DeleteLocalRef(c);
        if (handler != nullptr) break;
    }
    if (handler == nullptr) { LOGE("§438: found no existing Proxy to borrow a handler from"); return; }
    LOGI("§438 borrowed an InvocationHandler OK");

    // --- 2. build a proxy for the app interface with that handler ---
    jobject loader = wl_app_class_loader(env);
    jclass clCls = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClassM = env->GetMethodID(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring jn = env->NewStringUTF(dotted);
    jobject iface = (loader && loadClassM) ? env->CallObjectMethod(loader, loadClassM, jn) : nullptr;
    if (iface == nullptr || env->ExceptionCheck()) { env->ExceptionClear();
        LOGE("§438: loadClass(%s) failed", dotted); return; }
    jobjectArray ifaces = env->NewObjectArray(1, classCls, iface);
    jobject proxy = env->CallStaticObjectMethod(proxyCls, newProxyM, loader, ifaces, handler);
    if (proxy == nullptr || env->ExceptionCheck()) {
        wl_report_exception(env, "§438 newProxyInstance");
        LOGE("§438: newProxyInstance(%s) FAILED", dotted); return;
    }
    jobject pc = env->CallObjectMethod(proxy, objGetClass);
    jstring pcn = pc ? (jstring)env->CallObjectMethod(pc, clsGetName) : nullptr;
    const char* pcc = pcn ? env->GetStringUTFChars(pcn, nullptr) : nullptr;
    LOGI("§438 built proxy for %s -> %s", dotted, pcc ? pcc : "?");
    if (pcn && pcc) env->ReleaseStringUTFChars(pcn, pcc);

    // --- 3. THE TEST: reflectively invoke the exact method the app's bytecode cannot resolve.
    // NOT env->GetMethodID((jclass)iface, ...): §408c — GetMethodID on an APP class from a bridge
    // thread drives ClassLinker::EnsureInitialized and SEGVs in mirror::Class::GetDescriptor
    // (confirmed again here: the probe died right there). getDeclaredMethods() is known-safe (§433),
    // and Method.invoke() reaches the proxy through reflection.cc, which — like JNI — never consults
    // FindMethodToCall's bytecode-path validator. Same discriminator, safe route.
    (void) msig;
    jmethodID getDMs = env->GetMethodID(classCls, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
    jobjectArray ms = getDMs ? (jobjectArray)env->CallObjectMethod(iface, getDMs) : nullptr;
    if (ms == nullptr) { if (env->ExceptionCheck()) env->ExceptionClear();
        LOGE("§438: getDeclaredMethods failed"); return; }
    jclass methCls = env->FindClass("java/lang/reflect/Method");
    jmethodID methName = env->GetMethodID(methCls, "getName", "()Ljava/lang/String;");
    jmethodID methInvoke = env->GetMethodID(methCls, "invoke",
        "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
    jobject target = nullptr;
    const jsize mn = env->GetArrayLength(ms);
    for (jsize i = 0; i < mn && target == nullptr; ++i) {
        jobject m = env->GetObjectArrayElement(ms, i);
        if (!m) continue;
        jstring nm = (jstring)env->CallObjectMethod(m, methName);
        const char* nc = nm ? env->GetStringUTFChars(nm, nullptr) : nullptr;
        if (nc && strcmp(nc, mname) == 0) target = env->NewGlobalRef(m);
        if (nm && nc) env->ReleaseStringUTFChars(nm, nc);
        if (nm) env->DeleteLocalRef(nm);
        env->DeleteLocalRef(m);
    }
    if (target == nullptr) { LOGE("§438: no declared method named '%s'", mname); return; }
    LOGI("§438 resolved java.lang.reflect.Method for %s.%s", dotted, mname);

    jobjectArray args = env->NewObjectArray(1, objCls, nullptr);   // single null arg (Continuation)
    jobject r = env->CallObjectMethod(target, methInvoke, proxy, args);
    if (env->ExceptionCheck()) {
        wl_report_exception(env, "§438 Method.invoke");
        LOGE("§438 RESULT: reflective invoke on the proxy THREW (see exception above)");
    } else {
        LOGI("§438 RESULT: *** reflective invoke on the proxy SUCCEEDED *** (returned %s) "
             "=> resolution + proxy dispatch are SOUND; only the bytecode-path validator is broken",
             r == nullptr ? "null" : "an object");
    }
    env->DeleteGlobalRef(target);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

static jclass resolveTouchInjector(JNIEnv* env, jclass viewCls, jobject decorView) {
    // Diagnostic/harness escape hatch: this standalone lifecycle can leave the Android main
    // Handler queue undrained even while rendering continues.  In that state OHTouchInjector's
    // View.post() accepts every event but never runs it (invokeCount rises, runCount stays zero).
    // Reuse the existing direct-dispatch fallback when explicitly requested; production behavior
    // remains unchanged and the event still enters at the OHOS input boundary.
    if (getenv("WL_TOUCH_DIRECT") != nullptr) {
        LOGI("resolveTouchInjector: WL_TOUCH_DIRECT -> direct View.dispatchTouchEvent");
        return nullptr;
    }
    // --- 1. BCP class via FindClass (system/boot classloader) ---
    jclass bcpCls = env->FindClass("adapter/window/OHTouchInjector");
    if (bcpCls && !env->ExceptionCheck()) {
        LOGI("resolveTouchInjector: resolved BCP adapter/window/OHTouchInjector");
        return bcpCls;
    }
    // Clear the pending NoClassDefFoundError/ClassNotFoundException before the
    // fallback attempt (FindClass leaves it set on failure).
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (bcpCls) env->DeleteLocalRef(bcpCls);

    // --- 2. FALLBACK: app classloader com.github...noice.OHTouchInjector ---
    jmethodID getCtx = env->GetMethodID(viewCls, "getContext", "()Landroid/content/Context;");
    if (!getCtx || env->ExceptionCheck()) { env->ExceptionClear(); LOGE("resolveTouchInjector: getContext id"); return nullptr; }
    jobject ctx = env->CallObjectMethod(decorView, getCtx);
    if (!ctx || env->ExceptionCheck()) { env->ExceptionClear(); LOGE("resolveTouchInjector: getContext call"); return nullptr; }
    jclass ctxCls = env->FindClass("android/content/Context");
    jmethodID getCL = env->GetMethodID(ctxCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject cl = env->CallObjectMethod(ctx, getCL);
    if (!cl || env->ExceptionCheck()) { env->ExceptionClear(); LOGE("resolveTouchInjector: getClassLoader"); return nullptr; }
    jclass clCls = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClassM = env->GetMethodID(clCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring hn = env->NewStringUTF("com.github.ashutoshgngwr.noice.OHTouchInjector");
    jobject helperClsO = env->CallObjectMethod(cl, loadClassM, hn);
    env->DeleteLocalRef(hn);
    if (!helperClsO || env->ExceptionCheck()) { env->ExceptionClear(); LOGE("resolveTouchInjector: loadClass OHTouchInjector (app)"); return nullptr; }
    LOGI("resolveTouchInjector: resolved app com.github.ashutoshgngwr.noice.OHTouchInjector (fallback)");
    return static_cast<jclass>(helperClsO);
}

// WESTLAKE §765: post Android's own View.PerformClick runnable to the View's attached
// Handler.  Direct OH input currently enters View.dispatchTouchEvent on the boundary worker;
// a synchronous listener can therefore mutate View state but FragmentManager rejects the
// transaction because it is not running on the main thread.  ViewRootImpl.enqueueInputEvent()
// would normally provide this handoff, but the incomplete OH service stack currently throws in
// its Autofill/DeviceConfig pre-IME stage.  Reusing View's own click runnable keeps the Android
// listener, sound/accessibility, and main-Looper semantics intact without an app helper.
static jboolean wl_post_click_on_main(JNIEnv* env, jobject view, jclass viewCls) {
    jclass performCls = env->FindClass("android/view/View$PerformClick");
    if (!performCls || env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("wl_post_click_on_main: View$PerformClick unavailable");
        return JNI_FALSE;
    }

    jmethodID postM = env->GetMethodID(viewCls, "post", "(Ljava/lang/Runnable;)Z");
    jmethodID removeM = env->GetMethodID(
        viewCls, "removeCallbacks", "(Ljava/lang/Runnable;)Z");
    if (!postM || !removeM || env->ExceptionCheck()) {
        env->ExceptionClear();
        return JNI_FALSE;
    }

    // A normal View.onTouchEvent(ACTION_UP) creates mPerformClick once and then caches that
    // same Runnable for the lifetime of the View.  Its mere existence therefore says nothing
    // about whether the current click is queued.  The old check returned success without
    // posting anything after the first tap, which made every later direct-boundary tap a no-op.
    // Remove a possibly stale/current queue entry and post Android's own cached PerformClick
    // exactly once.  This keeps the listener, main-Looper and accessibility behavior in View.
    jfieldID cachedField = env->GetFieldID(
        viewCls, "mPerformClick", "Landroid/view/View$PerformClick;");
    if (cachedField != nullptr && !env->ExceptionCheck()) {
        jobject cached = env->GetObjectField(view, cachedField);
        if (!env->ExceptionCheck() && cached != nullptr) {
            const jboolean removed = env->CallBooleanMethod(view, removeM, cached);
            if (env->ExceptionCheck()) env->ExceptionClear();
            const jboolean posted = env->CallBooleanMethod(view, postM, cached);
            const bool threw = wl_report_exception(env,
                "wl_post_click_on_main View.post cached");
            env->DeleteLocalRef(cached);
            LOGI("[WESTLAKE-TOUCHPOST-782] existing=1 removed=%d posted=%d threw=%d",
                 (int)removed, (int)posted, threw ? 1 : 0);
            return !threw && posted ? JNI_TRUE : JNI_FALSE;
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    // D8 normally emits the non-static inner-class constructor with only the outer View.
    // Retain the javac synthetic-accessor signature as a compatibility fallback for other
    // framework builds.
    jmethodID ctor = env->GetMethodID(performCls, "<init>", "(Landroid/view/View;)V");
    bool syntheticCtor = false;
    if (!ctor || env->ExceptionCheck()) {
        env->ExceptionClear();
        ctor = env->GetMethodID(performCls, "<init>",
                                "(Landroid/view/View;Landroid/view/View$1;)V");
        syntheticCtor = ctor != nullptr && !env->ExceptionCheck();
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    if (!ctor) {
        LOGE("wl_post_click_on_main: PerformClick constructor unavailable");
        return JNI_FALSE;
    }

    jobject runnable = syntheticCtor
        ? env->NewObject(performCls, ctor, view, nullptr)
        : env->NewObject(performCls, ctor, view);
    if (!runnable || env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("wl_post_click_on_main: PerformClick allocation failed");
        return JNI_FALSE;
    }
    const jboolean posted = env->CallBooleanMethod(view, postM, runnable);
    const bool threw = wl_report_exception(env, "wl_post_click_on_main View.post");
    env->DeleteLocalRef(runnable);
    LOGI("[WESTLAKE-TOUCHPOST-765] existing=0 posted=%d threw=%d syntheticCtor=%d",
         (int)posted, threw ? 1 : 0, syntheticCtor ? 1 : 0);
    return !threw && posted ? JNI_TRUE : JNI_FALSE;
}

// Synchronous diagnostic click for WL_TOUCH_DIRECT. View.performClick() first asks ViewRootImpl
// to play a sound and therefore throws checkThread() on this OH-boundary worker. callOnClick()
// invokes the same registered listener without those UI-thread-only side effects.  With the
// opt-in §765 route, the selected clickable is posted to the real main Handler instead.
static jboolean wl_call_on_click_at(JNIEnv* env, jobject view, jfloat x, jfloat y) {
    jclass viewCls = env->FindClass("android/view/View");
    jclass groupCls = env->FindClass("android/view/ViewGroup");
    if (!viewCls || !groupCls || env->ExceptionCheck()) { env->ExceptionClear(); return JNI_FALSE; }
    if (env->IsInstanceOf(view, groupCls)) {
        jmethodID countM = env->GetMethodID(groupCls, "getChildCount", "()I");
        jmethodID childM = env->GetMethodID(groupCls, "getChildAt", "(I)Landroid/view/View;");
        jmethodID scrollXM = env->GetMethodID(viewCls, "getScrollX", "()I");
        jmethodID scrollYM = env->GetMethodID(viewCls, "getScrollY", "()I");
        jmethodID getXM = env->GetMethodID(viewCls, "getX", "()F");
        jmethodID getYM = env->GetMethodID(viewCls, "getY", "()F");
        jmethodID widthM = env->GetMethodID(viewCls, "getWidth", "()I");
        jmethodID heightM = env->GetMethodID(viewCls, "getHeight", "()I");
        jmethodID visibilityM = env->GetMethodID(viewCls, "getVisibility", "()I");
        const jint n = env->CallIntMethod(view, countM);
        const jint sx = env->CallIntMethod(view, scrollXM);
        const jint sy = env->CallIntMethod(view, scrollYM);
        for (jint i = n - 1; i >= 0; --i) {
            jobject child = env->CallObjectMethod(view, childM, i);
            if (!child || env->ExceptionCheck()) { env->ExceptionClear(); continue; }
            const jint visibility = env->CallIntMethod(child, visibilityM);
            const jfloat cx = x + sx - env->CallFloatMethod(child, getXM);
            const jfloat cy = y + sy - env->CallFloatMethod(child, getYM);
            const jint width = env->CallIntMethod(child, widthM);
            const jint height = env->CallIntMethod(child, heightM);
            if (visibility == 0 && cx >= 0 && cy >= 0 && cx < width && cy < height &&
                wl_call_on_click_at(env, child, cx, cy)) {
                env->DeleteLocalRef(child);
                return JNI_TRUE;
            }
            env->DeleteLocalRef(child);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
    }
    jmethodID clickableM = env->GetMethodID(viewCls, "isClickable", "()Z");
    jmethodID callM = env->GetMethodID(viewCls, "callOnClick", "()Z");
    if (env->CallBooleanMethod(view, clickableM)) {
        if (getenv("WL_TOUCH_POST_CLICK") != nullptr &&
            wl_post_click_on_main(env, view, viewCls)) {
            return JNI_TRUE;
        }
        return env->CallBooleanMethod(view, callM);
    }
    return JNI_FALSE;
}

struct WlMainClickTask {
    JavaVM* jvm;
    jobject decorView;
    jfloat x;
    jfloat y;
};

// Barrier-independent counterpart to OHTouchInjector's Handler.post() path.
// Some app states leave the standalone runtime's Java queue behind a sync
// barrier while the native MessageQueue continues to poll and render.  Posting
// a click is not equivalent to Android input (onTouch listeners and
// non-clickable gesture containers never see it), so keep a real DOWN/UP
// MotionEvent stream and run View.dispatchTouchEvent from the exact main
// MessageQueue's fd callback instead.
struct WlMainMotionTask {
    JavaVM* jvm;
    jobject decorView;
    jint action;
    jfloat x;
    jfloat y;
    jlong downTimeMs;
    jlong eventTimeMs;
};

static void wl_run_motion_from_main_mq(void* opaque) {
    WlMainMotionTask* task = static_cast<WlMainMotionTask*>(opaque);
    if (task == nullptr) return;
    JNIEnv* env = nullptr;
    const jint getEnv = task->jvm != nullptr
        ? task->jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6)
        : JNI_ERR;
    if (getEnv != JNI_OK || env == nullptr) {
        LOGE("[WESTLAKE-TOUCHFD-788] callback has no main-thread JNIEnv");
    } else {
        jclass motionCls = env->FindClass("android/view/MotionEvent");
        jmethodID obtainM = motionCls != nullptr ? env->GetStaticMethodID(
            motionCls, "obtain", "(JJIFFI)Landroid/view/MotionEvent;") : nullptr;
        jobject event = obtainM != nullptr ? env->CallStaticObjectMethod(
            motionCls, obtainM, task->downTimeMs, task->eventTimeMs,
            task->action, task->x, task->y, (jint)0) : nullptr;
        jclass viewCls = env->FindClass("android/view/View");
        jmethodID setSourceM = motionCls != nullptr ? env->GetMethodID(
            motionCls, "setSource", "(I)V") : nullptr;
        jmethodID dispatchM = viewCls != nullptr ? env->GetMethodID(
            viewCls, "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z") : nullptr;
        jboolean handled = JNI_FALSE;
        if (event != nullptr && setSourceM != nullptr && dispatchM != nullptr &&
            !env->ExceptionCheck()) {
            env->CallVoidMethod(event, setSourceM, (jint)0x1002 /* TOUCHSCREEN */);
            handled = env->CallBooleanMethod(task->decorView, dispatchM, event);
        }
        const bool threw = wl_report_exception(env, "wl_run_motion_from_main_mq");
        LOGI("[WESTLAKE-TOUCHFD-788] main-mq action=%d x=%.1f y=%.1f handled=%d threw=%d",
             (int)task->action, (double)task->x, (double)task->y,
             (int)handled, threw ? 1 : 0);
        // In direct-launch mode there is no AMS process to send the
        // DestroyActivityItem that normally follows Activity.finish().  A
        // toolbar Up/close listener can finish an Activity from ACTION_UP just
        // as legitimately as the BACK key can; limiting cleanup to the key
        // path leaves that finished Activity's ViewRoot and OH surface stacked
        // forever.  Complete the standard client transaction after the
        // listener has returned, while we are already on the main queue.
        if (!threw && task->action == 1 /* ACTION_UP */) {
            wl_destroy_finished_direct_launch_activities(env);
        }
        if (event != nullptr) env->DeleteLocalRef(event);
        if (viewCls != nullptr) env->DeleteLocalRef(viewCls);
        if (motionCls != nullptr) env->DeleteLocalRef(motionCls);
        env->DeleteGlobalRef(task->decorView);
    }
    delete task;
}

static void wl_run_click_from_main_mq(void* opaque) {
    WlMainClickTask* task = static_cast<WlMainClickTask*>(opaque);
    if (task == nullptr) return;
    JNIEnv* env = nullptr;
    const jint getEnv = task->jvm != nullptr
        ? task->jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6)
        : JNI_ERR;
    if (getEnv != JNI_OK || env == nullptr) {
        LOGE("[WESTLAKE-TOUCHFD-787] callback has no main-thread JNIEnv");
    } else {
        const jboolean clicked = wl_call_on_click_at(
            env, task->decorView, task->x, task->y);
        const bool threw = wl_report_exception(env, "wl_run_click_from_main_mq");
        LOGI("[WESTLAKE-TOUCHFD-787] main-mq click x=%.1f y=%.1f clicked=%d threw=%d",
             (double)task->x, (double)task->y, (int)clicked, threw ? 1 : 0);
        // callOnClick() may invoke Activity.finish() (for example a toolbar Up
        // button).  Stock AMS would now schedule DestroyActivityItem; the
        // BMS/AMS-free harness must complete that same transaction locally.
        if (!threw) {
            wl_destroy_finished_direct_launch_activities(env);
        }
        env->DeleteGlobalRef(task->decorView);
    }
    delete task;
}

// Schedule a hit-tested click through the main MessageQueue's native epoll fd.
// This mirrors InputEventReceiver's barrier-independent delivery instead of
// Handler.post(), whose wake/message can remain behind a Choreographer sync
// barrier on the standalone OH runtime.
static bool wl_post_click_to_main_mq(JNIEnv* env, JavaVM* jvm,
                                     jobject decorView, jfloat x, jfloat y) {
    jclass looperCls = env->FindClass("android/os/Looper");
    jmethodID getMain = looperCls ? env->GetStaticMethodID(
        looperCls, "getMainLooper", "()Landroid/os/Looper;") : nullptr;
    jobject mainLooper = getMain
        ? env->CallStaticObjectMethod(looperCls, getMain) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); mainLooper = nullptr; }
    jmethodID getQueue = (looperCls && mainLooper) ? env->GetMethodID(
        looperCls, "getQueue", "()Landroid/os/MessageQueue;") : nullptr;
    jobject queue = getQueue ? env->CallObjectMethod(mainLooper, getQueue) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); queue = nullptr; }
    jclass queueCls = queue ? env->GetObjectClass(queue) : nullptr;
    jfieldID ptrField = queueCls ? env->GetFieldID(queueCls, "mPtr", "J") : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); ptrField = nullptr; }
    const jlong ptr = (queue && ptrField) ? env->GetLongField(queue, ptrField) : 0;
    if (env->ExceptionCheck()) env->ExceptionClear();

    bool posted = false;
    jobject viewGlobal = (ptr != 0) ? env->NewGlobalRef(decorView) : nullptr;
    WlMainClickTask* task = viewGlobal ? new (std::nothrow) WlMainClickTask{
        jvm, viewGlobal, x, y} : nullptr;
    if (task != nullptr && wl_MessageQueue_post(
            reinterpret_cast<void*>(static_cast<uintptr_t>(ptr)),
            wl_run_click_from_main_mq, task) == 0) {
        posted = true;
    } else {
        if (viewGlobal) env->DeleteGlobalRef(viewGlobal);
        delete task;
    }
    LOGI("[WESTLAKE-TOUCHFD-787] post mainQueue=%p x=%.1f y=%.1f posted=%d",
         reinterpret_cast<void*>(static_cast<uintptr_t>(ptr)),
         (double)x, (double)y, posted ? 1 : 0);
    if (queueCls) env->DeleteLocalRef(queueCls);
    if (queue) env->DeleteLocalRef(queue);
    if (mainLooper) env->DeleteLocalRef(mainLooper);
    if (looperCls) env->DeleteLocalRef(looperCls);
    return posted;
}

static bool wl_post_motion_to_main_mq(JNIEnv* env, JavaVM* jvm,
                                      jobject decorView, jint action,
                                      jfloat x, jfloat y,
                                      jlong downTimeMs, jlong eventTimeMs) {
    jclass looperCls = env->FindClass("android/os/Looper");
    jmethodID getMain = looperCls ? env->GetStaticMethodID(
        looperCls, "getMainLooper", "()Landroid/os/Looper;") : nullptr;
    jobject mainLooper = getMain
        ? env->CallStaticObjectMethod(looperCls, getMain) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); mainLooper = nullptr; }
    jmethodID getQueue = (looperCls && mainLooper) ? env->GetMethodID(
        looperCls, "getQueue", "()Landroid/os/MessageQueue;") : nullptr;
    jobject queue = getQueue ? env->CallObjectMethod(mainLooper, getQueue) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); queue = nullptr; }
    jclass queueCls = queue ? env->GetObjectClass(queue) : nullptr;
    jfieldID ptrField = queueCls ? env->GetFieldID(queueCls, "mPtr", "J") : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); ptrField = nullptr; }
    const jlong ptr = (queue && ptrField) ? env->GetLongField(queue, ptrField) : 0;
    if (env->ExceptionCheck()) env->ExceptionClear();

    bool posted = false;
    jobject viewGlobal = (ptr != 0) ? env->NewGlobalRef(decorView) : nullptr;
    WlMainMotionTask* task = viewGlobal ? new (std::nothrow) WlMainMotionTask{
        jvm, viewGlobal, action, x, y, downTimeMs, eventTimeMs} : nullptr;
    if (task != nullptr && wl_MessageQueue_post(
            reinterpret_cast<void*>(static_cast<uintptr_t>(ptr)),
            wl_run_motion_from_main_mq, task) == 0) {
        posted = true;
    } else {
        if (viewGlobal) env->DeleteGlobalRef(viewGlobal);
        delete task;
    }
    LOGI("[WESTLAKE-TOUCHFD-788] post mainQueue=%p action=%d x=%.1f y=%.1f posted=%d",
         reinterpret_cast<void*>(static_cast<uintptr_t>(ptr)), (int)action,
         (double)x, (double)y, posted ? 1 : 0);
    if (queueCls) env->DeleteLocalRef(queueCls);
    if (queue) env->DeleteLocalRef(queue);
    if (mainLooper) env->DeleteLocalRef(mainLooper);
    if (looperCls) env->DeleteLocalRef(looperCls);
    return posted;
}

// ============================================================
// dispatchTouchViaViewRoot — direct in-process touch dispatch
// ============================================================
// Touch DOES reach the bridge (OnInputEvent(PointerEvent) fires), but the
// InputChannel MOTION path (injectTouchEvent → consumer worker) doesn't land
// on the deployed runtime — same as keys. So we build the MotionEvent here and
// dispatch it straight into the focused ViewRootImpl (bypassing the channel),
// the proven path from dispatchKeyViaViewRoot. Enables tap/click: nav-tab
// switching, sound play/info/volume buttons, etc.
// ============================================================
int32_t OHInputBridge::dispatchTouchViaViewRoot(int32_t action, float x, float y,
                                                int64_t downTimeNs,
                                                int64_t eventTimeNs) {
    (void)downTimeNs; (void)eventTimeNs;
    // Act on ACTION_UP: synthesize a DOWN -> (delay) -> UP tap dispatched to the
    // focused decor view ON THE UI THREAD via noice's OHTouchInjector helper
    // (loaded through the app classloader — no boot-image change needed). The
    // bridge builds the events + paces DOWN/UP (so RecyclerView CheckForTap sets
    // PREPRESSED and the UP performClick()s); the helper does the main-Looper post.
    // Action 42 is reserved for the shell walkthrough's explicit main-queue
    // click fallback.  It is not generated by MMI and therefore cannot alter
    // ordinary physical touch delivery.
    const bool wl_clickOnly = action == 42;
    if (action != 1 /* AMOTION_EVENT_ACTION_UP */ && !wl_clickOnly) return 0;
    if (!jvm_) { LOGE("dispatchTouchViaViewRoot: no JavaVM"); return -1; }
    JNIEnv* env = nullptr;
    bool needDetach = false;
    if (jvm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        JavaVMAttachArgs args{JNI_VERSION_1_6, "oh-touch-dispatch", nullptr};
        if (jvm_->AttachCurrentThread(&env, &args) != JNI_OK) { LOGE("touch: attach failed"); return -1; }
        needDetach = true;
    }
    auto fail = [&](const char* why) -> int32_t {
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        LOGE("dispatchTouchViaViewRoot: %s", why);
        if (needDetach) jvm_->DetachCurrentThread();
        return -1;
    };

    // Register the VelocityTracker JNI stub (once) so View.dispatchTouchEvent's
    // RecyclerView/scroll/gesture velocity tracking doesn't UnsatisfiedLinkError.
    wl_ensure_looper(env);            // §411 — before any dispatch
    ensureVelocityTrackerStub(env);

    // --- find focused decor view (mView) ---
    jclass wmgCls = env->FindClass("android/view/WindowManagerGlobal");
    if (!wmgCls || env->ExceptionCheck()) return fail("FindClass WMG");
    jmethodID getInst = env->GetStaticMethodID(wmgCls, "getInstance", "()Landroid/view/WindowManagerGlobal;");
    jobject wmg = env->CallStaticObjectMethod(wmgCls, getInst);
    if (!wmg || env->ExceptionCheck()) return fail("WMG.getInstance");
    jfieldID mRootsF = env->GetFieldID(wmgCls, "mRoots", "Ljava/util/ArrayList;");
    jobject roots = env->GetObjectField(wmg, mRootsF);
    if (!roots) return fail("mRoots null");
    jclass listCls = env->FindClass("java/util/ArrayList");
    jmethodID sizeM = env->GetMethodID(listCls, "size", "()I");
    jmethodID getM  = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
    jint nn = env->CallIntMethod(roots, sizeM);
    jclass vriCls = env->FindClass("android/view/ViewRootImpl");
    jfieldID mViewF = env->GetFieldID(vriCls, "mView", "Landroid/view/View;");
    if (!mViewF || env->ExceptionCheck()) return fail("ViewRootImpl mView");
    jclass viewCls = env->FindClass("android/view/View");
    jmethodID hasFocusM = env->GetMethodID(viewCls, "hasWindowFocus", "()Z");
    jobject decorView = nullptr, fallbackView = nullptr;
    // WESTLAKE §398: also keep the ViewRootImpl that owns the view we pick.
    // Dispatching straight to View.dispatchTouchEvent from our own thread DELIVERS the touch but
    // aborts the child: noice's click handlers run Kotlin lambdas, whose MethodHandle/MethodType
    // machinery NPEs when driven off the UI thread ("oh-touch-dispatch" ...
    // MethodType$ConcurrentWeakInternSet.get -> abort, signal 6).  ViewRootImpl.enqueueInputEvent()
    // hands the event to the real input pipeline and hops to the UI thread itself, so we get
    // main-thread delivery WITHOUT needing a Runnable (and therefore without the OHTouchInjector
    // class, whose only delivery route — a BCP jar — breaks this child).
    jobject vriMatch = nullptr, vriFallback = nullptr;
    // WESTLAKE §406b: hold the decor view in a GLOBAL ref.  As a LOCAL ref it decoded to ART's
    // poison value 0xdead10c0 on the SECOND tap (SIGSEGV addr=0xdead10c0 inside
    // art::InvokeVirtualOrInterfaceWithVarArgs -> receiver->GetClass()), because the first tap tears
    // an Activity down and the local-ref table underneath us is not stable across that.  Also bail
    // out of the scan if a JNI call leaves an exception pending (mRoots is mutated by the UI thread
    // while we walk it), instead of continuing with a poisoned env.
    int chosenRoot = -1, fallbackIdx = -1;   // WESTLAKE §460
    int chosenPriority = 0;
    for (jint i = 0; i < nn; ++i) {
        jobject vri = env->CallObjectMethod(roots, getM, i);
        if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
        if (!vri) continue;
        jobject view = env->GetObjectField(vri, mViewF);
        if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(vri); break; }
        if (view) {
            // §408: an explicit index wins; otherwise prefer a window that is focused OR actually
            // shown, and only fall back to "the first root that exists".
            const int want = g_rootIndex.load();
            const int priority = wl_root_target_priority(
                env, view, viewCls, hasFocusM, want, i);
            if (priority > 0 && priority >= chosenPriority) {
                // WESTLAKE §460: keep the LAST match, not the first. WindowManagerGlobal.mRoots
                // APPENDS new windows, so the topmost window is the last entry — and a dialog is
                // always newer than the Activity beneath it. Breaking on the first match sent every
                // tap to the oldest window, and because a stale/among-stacked window still reports
                // isShown()==true it happily returned handled=1 while nothing visible happened.
                // That is why dialog buttons and the volume Slider looked dead.
                if (decorView) env->DeleteGlobalRef(decorView);
                if (vriMatch)  env->DeleteGlobalRef(vriMatch);
                decorView = env->NewGlobalRef(view);
                vriMatch = env->NewGlobalRef(vri);
                chosenRoot = (int)i;
                chosenPriority = priority;
            } else if (!fallbackView) {
                fallbackView = env->NewGlobalRef(view);
                vriFallback = env->NewGlobalRef(vri);
                fallbackIdx = (int)i;
            }
        }
        if (view) env->DeleteLocalRef(view);
        env->DeleteLocalRef(vri);
        // (no early break — see §460: we want the LAST matching root)
    }
    if (!decorView) { decorView = fallbackView; vriMatch = vriFallback; chosenRoot = fallbackIdx; }
    else if (fallbackView) { env->DeleteGlobalRef(fallbackView); fallbackView = nullptr; }
    if (!decorView) return fail("no decorView");
    // WESTLAKE §460: say WHICH window we picked. "handled=1" from the wrong root looks identical to
    // a working tap, which is what made the dialog-input bug so hard to read.
    LOGI("dispatchTouchViaViewRoot: chose root[%d] of %d (g_rootIndex=%d)",
         chosenRoot, (int)nn, g_rootIndex.load());

    // WESTLAKE §798: some RecyclerView rows post CheckForTap/PerformClick to
    // a Java Handler that remains behind a Choreographer sync barrier in the
    // standalone runtime.  The normal DOWN/UP path must stay untouched because
    // controls such as Toutiao's search bar already consume it correctly.
    // `c x y` is an explicit walkthrough fallback: hit-test the existing view
    // tree and run its real listener from the native main MessageQueue fd.
    if (wl_clickOnly) {
        const bool posted = wl_post_click_to_main_mq(env, jvm_, decorView, x, y);
        LOGI("[WESTLAKE-TOUCHFD-798] explicit click x=%.1f y=%.1f posted=%d",
             (double)x, (double)y, posted ? 1 : 0);
        if (decorView) env->DeleteGlobalRef(decorView);
        if (vriMatch) env->DeleteGlobalRef(vriMatch);
        if (needDetach) jvm_->DetachCurrentThread();
        return posted ? 0 : -1;
    }

    // WESTLAKE §788: opt-in fd-driven REAL touch delivery for standalone
    // runtimes whose Java Handler queue is not drained.  Two native-fd
    // callbacks preserve the main-thread input contract and the DOWN/UP delay;
    // unlike performClick, this also reaches onTouch gesture containers such
    // as Toutiao's search bar.
    if (getenv("WL_TOUCH_FD_MAIN") != nullptr) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const jlong downMs = static_cast<jlong>(ts.tv_sec) * 1000LL +
            static_cast<jlong>(ts.tv_nsec / 1000000LL);
        const bool downPosted = wl_post_motion_to_main_mq(
            env, jvm_, decorView, (jint)0, (jfloat)x, (jfloat)y,
            downMs, downMs);
        if (downPosted) {
            usleep(120 * 1000);
            clock_gettime(CLOCK_MONOTONIC, &ts);
            const jlong upMs = static_cast<jlong>(ts.tv_sec) * 1000LL +
                static_cast<jlong>(ts.tv_nsec / 1000000LL);
            const bool upPosted = wl_post_motion_to_main_mq(
                env, jvm_, decorView, (jint)1, (jfloat)x, (jfloat)y,
                downMs, upMs);
            LOGI("[WESTLAKE-TOUCHFD-788] sequence down=%d up=%d",
                 downPosted ? 1 : 0, upPosted ? 1 : 0);
            if (upPosted) {
                if (decorView) env->DeleteGlobalRef(decorView);
                if (vriMatch) env->DeleteGlobalRef(vriMatch);
                if (needDetach) jvm_->DetachCurrentThread();
                return 0;
            }
            // DOWN without UP would leave Android's gesture state poisoned;
            // fail instead of also injecting a second, unrelated tap below.
            if (decorView) env->DeleteGlobalRef(decorView);
            if (vriMatch) env->DeleteGlobalRef(vriMatch);
            if (needDetach) jvm_->DetachCurrentThread();
            return -1;
        }
    }

    // --- resolve OHTouchInjector.dispatchTouchOnMain (BCP first, app fallback) ---
    // WESTLAKE §394: the OHTouchInjector helper is OPTIONAL now.
    // Injecting that class into a BCP jar breaks this child outright (any extra classesN.dex in a
    // boot-classpath jar makes initChild die in Process.setArgV0Native — A/B-proven on both
    // apache-xml.jar and adapter-runtime-bcp.jar), so fall back to calling
    // View.dispatchTouchEvent(MotionEvent) DIRECTLY via JNI when the helper is absent.
    // This is possible at all because android_view_MotionEvent_aosp now COMPILES (§393), so
    // MotionEvent.obtain/nativeInitialize actually exist in this process.
    jclass helperCls = resolveTouchInjector(env, viewCls, decorView);
    jclass meCls = env->FindClass("android/view/MotionEvent");
    jmethodID dispM = nullptr;
    if (helperCls != nullptr) {
        dispM = env->GetStaticMethodID(helperCls, "dispatchTouchOnMain",
            "(Landroid/view/View;Landroid/view/MotionEvent;)V");
        if (!dispM || env->ExceptionCheck()) { env->ExceptionClear(); dispM = nullptr; }
    }
    // WESTLAKE §406: resolve dispatchTouchEvent on the RECEIVER'S OWN class, not on the
    // android/view/View that FindClass hands this native thread.  Resolving it on View produced an
    // ArtMethod whose vtable slot does not exist for the DecorView's class in this runtime, so
    // art::FindVirtualMethod() returned NULL and CallBooleanMethod SEGV'd at addr=0x4
    // (= ArtMethod::access_flags_, i.e. a null ArtMethod) — that is what killed the child on the
    // first tap, NOT the touch itself.
    // §408c: resolve on android/view/View, NOT on the receiver's own class.  GetMethodID on
    // DecorView's class drives ClassLinker::EnsureInitialized from this injector thread and that
    // SEGV'd inside mirror::Class::GetDescriptor; View is already initialised and DecorView is a
    // View, so the virtual dispatch still reaches DecorView's override.
    jclass recvCls = env->GetObjectClass(decorView);
    if (env->ExceptionCheck()) { env->ExceptionClear(); recvCls = nullptr; }
    jmethodID dispDirect = env->GetMethodID(viewCls, "dispatchTouchEvent",
        "(Landroid/view/MotionEvent;)Z");
    if (!dispDirect || env->ExceptionCheck()) { env->ExceptionClear(); dispDirect = nullptr; }
    {   // name the receiver once — invaluable when a dispatch misbehaves
        jclass clsCls = env->FindClass("java/lang/Class");
        jmethodID gn = clsCls ? env->GetMethodID(clsCls, "getName", "()Ljava/lang/String;") : nullptr;
        jstring jn = (gn && recvCls) ? static_cast<jstring>(env->CallObjectMethod(recvCls, gn)) : nullptr;
        const char* cn = jn ? env->GetStringUTFChars(jn, nullptr) : nullptr;
        const jboolean isView = recvCls ? env->IsInstanceOf(decorView, viewCls) : JNI_FALSE;
        LOGI("dispatchTouchViaViewRoot: receiver=%s isView=%d dispDirect=%p",
             cn ? cn : "?", (int)isView, (void*)dispDirect);
        if (jn && cn) env->ReleaseStringUTFChars(jn, cn);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    // §398: ViewRootImpl.enqueueInputEvent(InputEvent) — public, thread-safe, posts to the UI thread.
    jclass vriCls2 = env->FindClass("android/view/ViewRootImpl");
    jmethodID enqueueM = nullptr;
    if (vriCls2 != nullptr) {
        enqueueM = env->GetMethodID(vriCls2, "enqueueInputEvent", "(Landroid/view/InputEvent;)V");
        if (!enqueueM || env->ExceptionCheck()) { env->ExceptionClear(); enqueueM = nullptr; }
    }
    // WESTLAKE §405b: PREFER the direct View.dispatchTouchEvent.
    // ViewRootImpl.enqueueInputEvent() looked like the clean route (§398) but it runs the event
    // through EarlyPostImeInputStage, which calls ViewRootImpl.getAutofillManager() ->
    // AutofillManager.<init> -> AutofillFeatureFlags.isFillDialogEnabled -> DeviceConfig ->
    // Settings$NameValueCache.getStringsForPrefix -> UserManager.isUserUnlocked on a NULL
    // UserManager: an uncaught NPE on the main looper, so ActivityThread.main returns and initChild
    // exits(1) (`[WESTLAKE-REAP] child ... exited(1)`) on the FIRST tap.  The direct call skips every
    // input stage, and it is safe now that §404 repaired java.lang.invoke.MethodType (the NPE that
    // made §397 blame threading).  Set WL_TOUCH_ENQUEUE=1 to go back to the ViewRootImpl route.
    static const bool wl_useEnqueue = (getenv("WL_TOUCH_ENQUEUE") != nullptr);
    const bool useEnqueue = wl_useEnqueue && enqueueM && vriMatch;
    if (useEnqueue) {
        LOGI("dispatchTouchViaViewRoot: using ViewRootImpl.enqueueInputEvent (WL_TOUCH_ENQUEUE)");
    }
    // WESTLAKE §399: force java.lang.invoke.MethodType's static init ONCE before we deliver a touch.
    // Both dispatch routes (direct View.dispatchTouchEvent and ViewRootImpl.enqueueInputEvent) abort
    // the child with the SAME NPE — "Attempt to invoke ... MethodType$ConcurrentWeakInternSet.get" —
    // so this is NOT a wrong-thread problem (that was my earlier reading): noice's Kotlin click
    // handlers go through MethodHandle/MethodType, and this runtime logs
    // "[CL] Skipping pre-init + root init (ARM64 standalone)", so MethodType's intern table static is
    // still null when the lambda machinery first touches it.  JNI FindClass does NOT run <clinit>;
    // calling a static method does.
    {
        static std::atomic<bool> wl_mtInit{false};
        bool wl_exp = false;
        if (wl_mtInit.compare_exchange_strong(wl_exp, true)) {
            // §403: FindClass + a static call was NOT enough — the child log shows ZERO `[CLINIT]`
            // traces for MethodType even though the class is referenced hundreds of thousands of
            // times, i.e. it is used while UNINITIALISED, so its `internTable` static stays null and
            // any lambda/invokedynamic path NPEs in ConcurrentWeakInternSet.get.
            // Class.forName(name, initialize=true, loader) is the JNI-reachable call that actually
            // forces <clinit>.
            {
                jclass wl_cls = env->FindClass("java/lang/Class");
                jmethodID wl_fn = wl_cls ? env->GetStaticMethodID(wl_cls, "forName",
                    "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;") : nullptr;
                if (wl_fn != nullptr) {
                    const char* wl_names[] = {"java.lang.invoke.MethodType",
                                              "java.lang.invoke.MethodHandles",
                                              "java.lang.invoke.MethodHandle"};
                    for (const char* wl_n : wl_names) {
                        jstring wl_s = env->NewStringUTF(wl_n);
                        jobject wl_c = env->CallStaticObjectMethod(wl_cls, wl_fn, wl_s, JNI_TRUE, nullptr);
                        const bool wl_exc = env->ExceptionCheck();
                        if (wl_exc) { env->ExceptionDescribe(); env->ExceptionClear(); }
                        LOGI("§403 Class.forName(%s, init=true) exc=%d", wl_n, wl_exc ? 1 : 0);
                        if (wl_c) env->DeleteLocalRef(wl_c);
                        env->DeleteLocalRef(wl_s);
                    }
                }
            }
            jclass wl_mt = env->FindClass("java/lang/invoke/MethodType");
            if (wl_mt != nullptr) {
                jmethodID wl_mtm = env->GetStaticMethodID(wl_mt, "methodType",
                    "(Ljava/lang/Class;)Ljava/lang/invoke/MethodType;");
                if (wl_mtm != nullptr) {
                    jclass wl_void = env->FindClass("java/lang/Void");
                    jfieldID wl_tf = wl_void ? env->GetStaticFieldID(wl_void, "TYPE", "Ljava/lang/Class;") : nullptr;
                    jobject wl_vt = wl_tf ? env->GetStaticObjectField(wl_void, wl_tf) : nullptr;
                    if (wl_vt != nullptr) {
                        jobject wl_r = env->CallStaticObjectMethod(wl_mt, wl_mtm, wl_vt);
                        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
                        if (wl_r) env->DeleteLocalRef(wl_r);
                    }
                }
                jfieldID wl_it = env->GetStaticFieldID(wl_mt, "internTable",
                    "Ljava/lang/invoke/MethodType$ConcurrentWeakInternSet;");
                if (env->ExceptionCheck()) { env->ExceptionClear(); wl_it = nullptr; }
                jobject wl_itv = wl_it ? env->GetStaticObjectField(wl_mt, wl_it) : nullptr;
                LOGI("§403 MethodType.internTable found=%d null=%d",
                     wl_it ? 1 : 0, wl_itv == nullptr ? 1 : 0);
                if (wl_itv) env->DeleteLocalRef(wl_itv);
            }
            if (env->ExceptionCheck()) { env->ExceptionClear(); }
        }
    }
    if (!dispDirect || env->ExceptionCheck()) { env->ExceptionClear(); }
    if (!dispM && !dispDirect) return fail("no touch dispatch path");
    if (!dispM) {
        LOGI("dispatchTouchViaViewRoot: using DIRECT View.dispatchTouchEvent (no OHTouchInjector)");
    }
    jmethodID obtain = env->GetStaticMethodID(meCls, "obtain", "(JJIFFI)Landroid/view/MotionEvent;");
    jmethodID setSrc = env->GetMethodID(meCls, "setSource", "(I)V");
    jclass scCls = env->FindClass("android/os/SystemClock");
    jmethodID upmM = env->GetStaticMethodID(scCls, "uptimeMillis", "()J");
    if (!obtain || !setSrc || !upmM || env->ExceptionCheck()) return fail("resolve obtain");

    jlong T = env->CallStaticLongMethod(scCls, upmM);
    // DOWN — direct static call (no reflection)
    jobject down = env->CallStaticObjectMethod(meCls, obtain, T, T, (jint)0, (jfloat)x, (jfloat)y, (jint)0);
    if (down) {
        // §409b: MotionEvent.obtain goes through our own ported nativeInitialize — read the
        // coordinates back, because a MotionEvent that carries (0,0) hit-tests against the wrong
        // view and every tap silently returns handled=0.
        jmethodID gx = env->GetMethodID(meCls, "getX", "()F");
        jmethodID gy = env->GetMethodID(meCls, "getY", "()F");
        jmethodID ga = env->GetMethodID(meCls, "getAction", "()I");
        if (gx && gy && ga) {
            LOGI("dispatchTouchViaViewRoot: MotionEvent readback x=%.1f y=%.1f action=%d (asked %.1f,%.1f)",
                 (double)env->CallFloatMethod(down, gx), (double)env->CallFloatMethod(down, gy),
                 (int)env->CallIntMethod(down, ga), (double)x, (double)y);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        env->CallVoidMethod(down, setSrc, (jint)0x1002);
        if (useEnqueue) { env->CallVoidMethod(vriMatch, enqueueM, down); }
        else if (dispM) { env->CallStaticVoidMethod(helperCls, dispM, decorView, down); }
        else {
            // §408b: report whether the view tree consumed it — a delivered-but-unhandled DOWN is
            // the difference between "input works" and "input goes nowhere".
            jboolean hd = env->CallBooleanMethod(decorView, dispDirect, down);
            const bool threwD = wl_report_exception(env, "dispatchTouchViaViewRoot DOWN");
            LOGI("dispatchTouchViaViewRoot: DOWN handled=%d threw=%d", (int)hd, threwD ? 1 : 0);
        }
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); LOGE("dispatchTouchViaViewRoot: DOWN dispatchTouchOnMain THREW"); }
        env->DeleteLocalRef(down);
    }
    // §559: was 150 ms. It must stay ABOVE ViewConfiguration.getTapTimeout() (100 ms) so a
    // RecyclerView row's postDelayed(CheckForTap) fires and sets PREPRESSED before the UP
    // arrives -- that is what §405 found empirically. 120 ms keeps that margin and returns
    // 30 ms per tap.
    usleep(120 * 1000);
    // UP
    jlong T2 = env->CallStaticLongMethod(scCls, upmM);
    jobject up = env->CallStaticObjectMethod(meCls, obtain, T, T2, (jint)1, (jfloat)x, (jfloat)y, (jint)0);
    if (up) {
        env->CallVoidMethod(up, setSrc, (jint)0x1002);
        if (useEnqueue) { env->CallVoidMethod(vriMatch, enqueueM, up); }
        else if (dispM) { env->CallStaticVoidMethod(helperCls, dispM, decorView, up); }
        else {
            jboolean hu = env->CallBooleanMethod(decorView, dispDirect, up);
            const bool threwU = wl_report_exception(env, "dispatchTouchViaViewRoot UP");
            LOGI("dispatchTouchViaViewRoot: UP handled=%d threw=%d", (int)hu, threwU ? 1 : 0);
            // With WL_TOUCH_DIRECT the main Handler is known not to drain, so View's delayed
            // CheckForTap cannot promote PREPRESSED to a click. Reuse the boot helper's existing
            // hit-test/performClick implementation synchronously for this diagnostic path.
            if (getenv("WL_TOUCH_DIRECT") != nullptr) {
                jboolean clicked = wl_call_on_click_at(env, decorView, (jfloat)x, (jfloat)y);
                const bool forceThrew = wl_report_exception(env,
                    "dispatchTouchViaViewRoot callOnClickAt");
                LOGI("dispatchTouchViaViewRoot: WL_TOUCH_DIRECT callOnClick=%d threw=%d",
                     (int)clicked, forceThrew ? 1 : 0);
            }
        }
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); LOGE("dispatchTouchViaViewRoot: UP dispatchTouchOnMain THREW"); }
        env->DeleteLocalRef(up);
    }
    // §559: was 100 ms. Since §554 the UP is POSTED to the UI thread, so this only has to be
    // long enough for that post to run before the OHTI counters are read below -- it does not
    // gate the tap itself, which is already in flight.
    usleep(50 * 1000);   // let the posted UP run() complete on the UI thread
    // WESTLAKE §405: these counters live on OHTouchInjector, which is ABSENT on this board (§394
    // falls back to a direct dispatch).  Reading static fields off a null jclass makes ART abort
    // ("Runtime aborting..." with no pending exception) — that abort, not the touch, is what killed
    // the child on every tap.  Only read them when the helper actually resolved.
    if (helperCls != nullptr) {
        jfieldID rcF = env->GetStaticFieldID(helperCls, "runCount", "I");
        jfieldID lhF = env->GetStaticFieldID(helperCls, "lastHandled", "I");
        jfieldID icF = env->GetStaticFieldID(helperCls, "invokeCount", "I");
        jfieldID exF = env->GetStaticFieldID(helperCls, "lastEx", "Ljava/lang/String;");
        jint rc = rcF ? env->GetStaticIntField(helperCls, rcF) : -999;
        jint lh = lhF ? env->GetStaticIntField(helperCls, lhF) : -999;
        jint ic = icF ? env->GetStaticIntField(helperCls, icF) : -999;
        jstring exs = exF ? static_cast<jstring>(env->GetStaticObjectField(helperCls, exF)) : nullptr;
        const char* exc = nullptr;
        if (exs) exc = env->GetStringUTFChars(exs, nullptr);
        if (env->ExceptionCheck()) env->ExceptionClear();
        LOGI("dispatchTouchViaViewRoot: OHTI invokeCount=%d runCount=%d lastHandled=%d lastEx=%s",
             (int)ic, (int)rc, (int)lh, exc ? exc : "none");
        if (exs && exc) env->ReleaseStringUTFChars(exs, exc);
    }
    LOGI("dispatchTouchViaViewRoot: TAP x=%.1f y=%.1f delivered", x, y);
    if (decorView) env->DeleteGlobalRef(decorView);          // §406b: globals, release them
    if (vriMatch)  env->DeleteGlobalRef(vriMatch);
    if (needDetach) jvm_->DetachCurrentThread();
    return 0;
}

// ============================================================
// dispatchDragViaViewRoot — DOWN -> N*MOVE -> UP (sliders/scroll/dial)
// ============================================================
int32_t OHInputBridge::dispatchDragViaViewRoot(float x1, float y1, float x2, float y2) {
    if (!jvm_) { LOGE("dispatchDragViaViewRoot: no JavaVM"); return -1; }
    JNIEnv* env = nullptr; bool needDetach = false;
    if (jvm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        JavaVMAttachArgs args{JNI_VERSION_1_6, "oh-drag-dispatch", nullptr};
        if (jvm_->AttachCurrentThread(&env, &args) != JNI_OK) { LOGE("drag: attach failed"); return -1; }
        needDetach = true;
    }
    auto fail = [&](const char* why) -> int32_t {
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        LOGE("dispatchDragViaViewRoot: %s", why);
        if (needDetach) jvm_->DetachCurrentThread();
        return -1;
    };
    wl_ensure_looper(env);            // §411 — before any dispatch
    ensureVelocityTrackerStub(env);

    // --- find focused decor view (mView) ---
    jclass wmgCls = env->FindClass("android/view/WindowManagerGlobal");
    if (!wmgCls || env->ExceptionCheck()) return fail("FindClass WMG");
    jmethodID getInst = env->GetStaticMethodID(wmgCls, "getInstance", "()Landroid/view/WindowManagerGlobal;");
    jobject wmg = env->CallStaticObjectMethod(wmgCls, getInst);
    if (!wmg || env->ExceptionCheck()) return fail("WMG.getInstance");
    jfieldID mRootsF = env->GetFieldID(wmgCls, "mRoots", "Ljava/util/ArrayList;");
    jobject roots = env->GetObjectField(wmg, mRootsF);
    if (!roots) return fail("mRoots null");
    jclass listCls = env->FindClass("java/util/ArrayList");
    jmethodID sizeM = env->GetMethodID(listCls, "size", "()I");
    jmethodID getM  = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
    jint nn = env->CallIntMethod(roots, sizeM);
    jclass vriCls = env->FindClass("android/view/ViewRootImpl");
    jfieldID mViewF = env->GetFieldID(vriCls, "mView", "Landroid/view/View;");
    if (!mViewF || env->ExceptionCheck()) return fail("ViewRootImpl mView");
    jclass viewCls = env->FindClass("android/view/View");
    jmethodID hasFocusM = env->GetMethodID(viewCls, "hasWindowFocus", "()Z");
    jobject decorView = nullptr, fallbackView = nullptr;
    jobject vriMatch = nullptr, vriFallback = nullptr;   // §405/§406b: global refs, guarded scan
    int dragRoot = -1, dragFallbackIdx = -1;   // WESTLAKE §460b
    int dragPriority = 0;
    for (jint i = 0; i < nn; ++i) {
        jobject vri = env->CallObjectMethod(roots, getM, i);
        if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
        if (!vri) continue;
        jobject view = env->GetObjectField(vri, mViewF);
        if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(vri); break; }
        if (view) {
            // §408: an explicit index wins; otherwise prefer a window that is focused OR actually
            // shown, and only fall back to "the first root that exists".
            const int want = g_rootIndex.load();
            const int priority = wl_root_target_priority(
                env, view, viewCls, hasFocusM, want, i);
            if (priority > 0 && priority >= dragPriority) {
                dragRoot = (int)i;
                dragPriority = priority;
                // WESTLAKE §460b: keep the LAST match — same reason as the tap path. Without this a
                // drag inside a dialog (the volume Slider) went to the Activity underneath.
                if (decorView) env->DeleteGlobalRef(decorView);
                if (vriMatch)  env->DeleteGlobalRef(vriMatch);
                decorView = env->NewGlobalRef(view);
                vriMatch = env->NewGlobalRef(vri);
            }
            else if (!fallbackView) {
                fallbackView = env->NewGlobalRef(view);
                vriFallback = env->NewGlobalRef(vri);
                dragFallbackIdx = (int)i;
            }
        }
        if (view) env->DeleteLocalRef(view);
        env->DeleteLocalRef(vri);
        // (no early break — §460b: we want the LAST matching root)
    }
    if (!decorView) { decorView = fallbackView; vriMatch = vriFallback; dragRoot = dragFallbackIdx; }
    else if (fallbackView) { env->DeleteGlobalRef(fallbackView); fallbackView = nullptr; }
    if (!decorView) return fail("no decorView");
    LOGI("dispatchDragViaViewRoot: chose root[%d] of %d (g_rootIndex=%d)",
         dragRoot, (int)nn, g_rootIndex.load());

    // --- resolve OHTouchInjector.dispatchTouchOnMain (BCP first, app fallback) ---
    // WESTLAKE §405: OHTouchInjector is ABSENT on this board and cannot be added (injecting a class
    // into a BCP jar breaks this child, §393).  Use the same three-tier fallback as
    // dispatchTouchViaViewRoot so drags/swipes work without it: helper -> ViewRootImpl
    // .enqueueInputEvent -> direct View.dispatchTouchEvent.  Without this every slider, ViewPager
    // swipe and scroll in the app is undrivable.
    jclass helperCls = resolveTouchInjector(env, viewCls, decorView);
    jclass meCls = env->FindClass("android/view/MotionEvent");
    jmethodID dispM = nullptr;
    if (helperCls != nullptr) {
        dispM = env->GetStaticMethodID(helperCls, "dispatchTouchOnMain",
            "(Landroid/view/View;Landroid/view/MotionEvent;)V");
        if (!dispM || env->ExceptionCheck()) { env->ExceptionClear(); dispM = nullptr; }
    }
    // §408c: resolve on android/view/View (see the tap path) — GetMethodID on the receiver's own
    // class runs EnsureInitialized from this thread and SEGV'd in mirror::Class::GetDescriptor.
    jmethodID dispDirect = env->GetMethodID(viewCls, "dispatchTouchEvent",
        "(Landroid/view/MotionEvent;)Z");
    if (!dispDirect || env->ExceptionCheck()) { env->ExceptionClear(); dispDirect = nullptr; }
    jmethodID enqueueM = env->GetMethodID(vriCls, "enqueueInputEvent", "(Landroid/view/InputEvent;)V");
    if (!enqueueM || env->ExceptionCheck()) { env->ExceptionClear(); enqueueM = nullptr; }
    static const bool wl_useEnqueue2 = (getenv("WL_TOUCH_ENQUEUE") != nullptr);
    const bool useEnqueue = wl_useEnqueue2 && enqueueM && vriMatch;   // §405b: direct by default
    if (!dispM && !dispDirect && !useEnqueue) return fail("no touch dispatch path");
    jmethodID obtain = env->GetStaticMethodID(meCls, "obtain", "(JJIFFI)Landroid/view/MotionEvent;");
    jmethodID setSrc = env->GetMethodID(meCls, "setSource", "(I)V");
    jclass scCls = env->FindClass("android/os/SystemClock");
    jmethodID upmM = env->GetStaticMethodID(scCls, "uptimeMillis", "()J");
    if (!obtain || !setSrc || !upmM || env->ExceptionCheck()) return fail("resolve obtain");

    jlong T = env->CallStaticLongMethod(scCls, upmM);
    auto send = [&](jint action, jfloat fx, jfloat fy, jlong et) {
        jobject e = env->CallStaticObjectMethod(meCls, obtain, T, et, action, fx, fy, (jint)0);
        if (e) {
            env->CallVoidMethod(e, setSrc, (jint)0x1002);
            if (useEnqueue) { env->CallVoidMethod(vriMatch, enqueueM, e); }
            else if (dispM) { env->CallStaticVoidMethod(helperCls, dispM, decorView, e); }
            else { env->CallBooleanMethod(decorView, dispDirect, e); }
            if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
            env->DeleteLocalRef(e);
        }
    };
    send((jint)0 /*ACTION_DOWN*/, (jfloat)x1, (jfloat)y1, T);
    usleep(50 * 1000);
    const int STEPS = 14;
    for (int i = 1; i <= STEPS; ++i) {
        jlong et = env->CallStaticLongMethod(scCls, upmM);
        jfloat fx = x1 + (x2 - x1) * (float)i / (float)STEPS;
        jfloat fy = y1 + (y2 - y1) * (float)i / (float)STEPS;
        send((jint)2 /*ACTION_MOVE*/, fx, fy, et);
        usleep(18 * 1000);
    }
    jlong T2 = env->CallStaticLongMethod(scCls, upmM);
    send((jint)1 /*ACTION_UP*/, (jfloat)x2, (jfloat)y2, T2);
    usleep(100 * 1000);
    LOGI("dispatchDragViaViewRoot: DRAG (%.0f,%.0f)->(%.0f,%.0f) %d steps", x1, y1, x2, y2, STEPS);
    if (decorView) env->DeleteGlobalRef(decorView);          // §406b
    if (vriMatch)  env->DeleteGlobalRef(vriMatch);
    if (needDetach) jvm_->DetachCurrentThread();
    return 0;
}

// ============================================================
// dispatchSingleTouchViaViewRoot — forward ONE real MotionEvent (action as-is)
// so the real-MMI DOWN/MOVE/UP stream reaches RecyclerView/ScrollView (scroll).
// dispatchTouchViaViewRoot only acts on UP (synthesizes a tap) and discards the
// real DOWN + all MOVE samples -> no scroll. This forwards the actual action.
// ============================================================
int32_t OHInputBridge::dispatchSingleTouchViaViewRoot(int32_t action, float x, float y,
                                                      int64_t downTimeNs, int64_t eventTimeNs) {
    if (!jvm_) { LOGE("dispatchSingleTouch: no JavaVM"); return -1; }
    JNIEnv* env = nullptr; bool needDetach = false;
    if (jvm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        JavaVMAttachArgs args{JNI_VERSION_1_6, "oh-touch1", nullptr};
        if (jvm_->AttachCurrentThread(&env, &args) != JNI_OK) { LOGE("touch1: attach failed"); return -1; }
        needDetach = true;
    }
    auto fail = [&](const char* why) -> int32_t {
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        LOGE("dispatchSingleTouch: %s", why);
        if (needDetach) jvm_->DetachCurrentThread();
        return -1;
    };
    wl_ensure_looper(env);            // §411 — before any dispatch
    ensureVelocityTrackerStub(env);
    // --- focused decor view (same resolution as dispatchTouchViaViewRoot) ---
    jclass wmgCls = env->FindClass("android/view/WindowManagerGlobal");
    if (!wmgCls || env->ExceptionCheck()) return fail("FindClass WMG");
    jmethodID getInst = env->GetStaticMethodID(wmgCls, "getInstance", "()Landroid/view/WindowManagerGlobal;");
    jobject wmg = env->CallStaticObjectMethod(wmgCls, getInst);
    if (!wmg || env->ExceptionCheck()) return fail("WMG.getInstance");
    jfieldID mRootsF = env->GetFieldID(wmgCls, "mRoots", "Ljava/util/ArrayList;");
    jobject roots = env->GetObjectField(wmg, mRootsF);
    if (!roots) return fail("mRoots null");
    jclass listCls = env->FindClass("java/util/ArrayList");
    jmethodID sizeM = env->GetMethodID(listCls, "size", "()I");
    jmethodID getM  = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
    jint nn = env->CallIntMethod(roots, sizeM);
    jclass vriCls = env->FindClass("android/view/ViewRootImpl");
    jfieldID mViewF = env->GetFieldID(vriCls, "mView", "Landroid/view/View;");
    if (!mViewF || env->ExceptionCheck()) return fail("ViewRootImpl mView");
    jclass viewCls = env->FindClass("android/view/View");
    jmethodID hasFocusM = env->GetMethodID(viewCls, "hasWindowFocus", "()Z");
    jobject decorView = nullptr, fallbackView = nullptr;
    jobject vriMatch = nullptr, vriFallback = nullptr;   // §405/§406b: global refs, guarded scan
    int singlePriority = 0;
    for (jint i = 0; i < nn; ++i) {
        jobject vri = env->CallObjectMethod(roots, getM, i);
        if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
        if (!vri) continue;
        jobject view = env->GetObjectField(vri, mViewF);
        if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(vri); break; }
        if (view) {
            // §408: an explicit index wins; otherwise prefer a window that is focused OR actually
            // shown, and only fall back to "the first root that exists".
            const int want = g_rootIndex.load();
            const int priority = wl_root_target_priority(
                env, view, viewCls, hasFocusM, want, i);
            if (priority > 0 && priority >= singlePriority) {
                if (decorView) env->DeleteGlobalRef(decorView);
                if (vriMatch) env->DeleteGlobalRef(vriMatch);
                decorView = env->NewGlobalRef(view);
                vriMatch = env->NewGlobalRef(vri);
                singlePriority = priority;
            }
            else if (!fallbackView) {
                fallbackView = env->NewGlobalRef(view);
                vriFallback = env->NewGlobalRef(vri);
            }
        }
        if (view) env->DeleteLocalRef(view);
        env->DeleteLocalRef(vri);
    }
    if (!decorView) { decorView = fallbackView; vriMatch = vriFallback; }
    else if (fallbackView) { env->DeleteGlobalRef(fallbackView); fallbackView = nullptr; }
    if (!decorView) return fail("no decorView");
    // --- resolve OHTouchInjector.dispatchTouchOnMain (BCP first, app fallback) ---
    // WESTLAKE §405: same three-tier fallback as the tap/drag paths — the helper class does not
    // exist on this board, so requiring it made every real-MMI forwarded touch fail.
    jclass helperCls = resolveTouchInjector(env, viewCls, decorView);
    jclass meCls = env->FindClass("android/view/MotionEvent");
    jmethodID dispM = nullptr;
    if (helperCls != nullptr) {
        dispM = env->GetStaticMethodID(helperCls, "dispatchTouchOnMain",
            "(Landroid/view/View;Landroid/view/MotionEvent;)V");
        if (!dispM || env->ExceptionCheck()) { env->ExceptionClear(); dispM = nullptr; }
    }
    // §408c: resolve on android/view/View (see the tap path) — GetMethodID on the receiver's own
    // class runs EnsureInitialized from this thread and SEGV'd in mirror::Class::GetDescriptor.
    jmethodID dispDirect = env->GetMethodID(viewCls, "dispatchTouchEvent",
        "(Landroid/view/MotionEvent;)Z");
    if (!dispDirect || env->ExceptionCheck()) { env->ExceptionClear(); dispDirect = nullptr; }
    jmethodID enqueueM = env->GetMethodID(vriCls, "enqueueInputEvent", "(Landroid/view/InputEvent;)V");
    if (!enqueueM || env->ExceptionCheck()) { env->ExceptionClear(); enqueueM = nullptr; }
    // WESTLAKE §727: a compatibility window can have no WMS session yet still owns a
    // WindowInputEventReceiver. Keep it as the fallback when the boot helper is unavailable.
    //
    // WESTLAKE §786: prefer OHTouchInjector when it exists. It posts through an asynchronous
    // main-Looper Handler, matching Android's normal fd-driven InputEventReceiver semantics
    // across Choreographer sync barriers. InputEventBridge.dispatchOnMainThread uses a normal
    // Handler in the deployed BCP jar; Toutiao measured invokeCount=2/runCount=0 while frames
    // continued rendering, so preferring that route can strand DOWN/UP indefinitely.
    jobject receiver = nullptr;
    jclass inputBridgeClass = nullptr;
    jmethodID dispatchOnMain = nullptr;
    if (vriMatch != nullptr) {
        jfieldID receiverField = env->GetFieldID(
            vriCls, "mInputEventReceiver",
            "Landroid/view/ViewRootImpl$WindowInputEventReceiver;");
        if (!env->ExceptionCheck() && receiverField != nullptr) {
            receiver = env->GetObjectField(vriMatch, receiverField);
        }
        if (env->ExceptionCheck()) { env->ExceptionClear(); receiver = nullptr; }
        inputBridgeClass = env->FindClass("adapter/window/InputEventBridge");
        if (!env->ExceptionCheck() && inputBridgeClass != nullptr) {
            dispatchOnMain = env->GetStaticMethodID(
                inputBridgeClass, "dispatchOnMainThread",
                "(Landroid/view/InputEventReceiver;ILandroid/view/InputEvent;)V");
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            inputBridgeClass = nullptr;
            dispatchOnMain = nullptr;
        }
    }
    static const bool wl_useEnqueue2 = (getenv("WL_TOUCH_ENQUEUE") != nullptr);
    const bool useEnqueue = wl_useEnqueue2 && enqueueM && vriMatch;   // §405b: direct by default
    const bool useAsyncHelper = dispM != nullptr;
    const bool useMainReceiver = receiver != nullptr &&
        inputBridgeClass != nullptr && dispatchOnMain != nullptr;
    if (!useAsyncHelper && !useMainReceiver && !dispDirect && !useEnqueue) {
        return fail("no touch dispatch path");
    }
    jmethodID obtain = env->GetStaticMethodID(meCls, "obtain", "(JJIFFI)Landroid/view/MotionEvent;");
    jmethodID setSrc = env->GetMethodID(meCls, "setSource", "(I)V");
    if (!obtain || !setSrc || env->ExceptionCheck()) return fail("resolve obtain");
    jlong downMs = (jlong)(downTimeNs  / 1000000LL);
    jlong evtMs  = (jlong)(eventTimeNs / 1000000LL);
    if (evtMs < downMs) evtMs = downMs;
    jobject e = env->CallStaticObjectMethod(meCls, obtain, downMs, evtMs,
                                            (jint)action, (jfloat)x, (jfloat)y, (jint)0);
    if (e) {
        env->CallVoidMethod(e, setSrc, (jint)0x1002 /* SOURCE_TOUCHSCREEN */);
        if (useAsyncHelper) {
            env->CallStaticVoidMethod(helperCls, dispM, decorView, e);
            LOGI("dispatchSingleTouch: action=%d -> async UI helper", action);
        }
        else if (useMainReceiver) {
            static std::atomic<int32_t> s_touchSeq{0x50000000};
            const jint seq = s_touchSeq.fetch_add(1);
            env->CallStaticVoidMethod(
                inputBridgeClass, dispatchOnMain, receiver, seq, e);
            LOGI("dispatchSingleTouch: action=%d seq=%d -> main receiver", action, seq);
        }
        else if (useEnqueue) { env->CallVoidMethod(vriMatch, enqueueM, e); }
        else { env->CallBooleanMethod(decorView, dispDirect, e); }
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); LOGE("dispatchSingleTouch: dispatch THREW"); }
        env->DeleteLocalRef(e);
    }
    if (receiver) env->DeleteLocalRef(receiver);
    if (decorView) env->DeleteGlobalRef(decorView);          // §406b
    if (vriMatch)  env->DeleteGlobalRef(vriMatch);
    if (needDetach) jvm_->DetachCurrentThread();
    return 0;
}

int OHInputBridge::writeMotionEvent(int fd, uint32_t seq, int32_t action,
                                     float x, float y,
                                     int64_t downTime, int64_t eventTime) {
    /*
     * 2026-05-18 Phase 2: bit-exact AOSP 14 InputMessage::Body::Motion format.
     * AOSP InputConsumer::consume reads (header + body) where body size is
     * computed from pointerCount — see motionBodyWireSize(). Sending the full
     * sizeof(MotionEventBody) (incl. all MAX_POINTERS slots) would also work
     * for SOCK_SEQPACKET (boundary preserves count) but wastes bytes; AOSP
     * truncates to actual pointer count.
     *
     * Field order MUST match AOSP — see frameworks/native/include/input/
     * InputTransport.h:67-... (audited 2026-05-18).
     */

    // Stack buffer for header + body (single pointer = 144B + ~200B body
    // header + 8B msg header = well under 4KB, fits in typical stack frame).
    struct {
        InputMessageHeader header;
        MotionEventBody body;
    } msg;
    memset(&msg, 0, sizeof(msg));

    // Header
    msg.header.type = INPUT_MSG_TYPE_MOTION;
    msg.header.seq = seq;

    // Motion body — AOSP field order
    msg.body.eventId = static_cast<int32_t>(seq);
    msg.body.pointerCount = 1;
    msg.body.eventTime = eventTime;
    msg.body.deviceId = 1;
    msg.body.source = AINPUT_SOURCE_TOUCHSCREEN;
    msg.body.displayId = 0;
    // hmac left zeroed (AOSP InputDispatcher signs events for security; OH
    // side has no equivalent — consumer accepts any when hmac is zero).
    msg.body.action = action;
    msg.body.actionButton = 0;
    msg.body.flags = 0;
    msg.body.metaState = 0;
    msg.body.buttonState = 0;
    msg.body.classification = 0;   // MotionClassification::NONE
    // empty2[3] already zeroed by memset
    msg.body.edgeFlags = 0;
    msg.body.downTime = downTime;

    // Window transform = identity (AOSP order: dsdx, dtdx, dtdy, dsdy, tx, ty)
    msg.body.dsdx = 1.0f;
    msg.body.dtdx = 0.0f;
    msg.body.dtdy = 0.0f;
    msg.body.dsdy = 1.0f;
    msg.body.tx   = 0.0f;
    msg.body.ty   = 0.0f;

    msg.body.xPrecision = 1.0f;
    msg.body.yPrecision = 1.0f;
    msg.body.xCursorPosition = 0.0f;
    msg.body.yCursorPosition = 0.0f;

    // Raw transform = identity (same field order as window transform)
    msg.body.dsdxRaw = 1.0f;
    msg.body.dtdxRaw = 0.0f;
    msg.body.dtdyRaw = 0.0f;
    msg.body.dsdyRaw = 1.0f;
    msg.body.txRaw   = 0.0f;
    msg.body.tyRaw   = 0.0f;

    // Single pointer at index 0
    InputMessagePointer& p0 = msg.body.pointers[0];
    p0.properties.id = 0;
    p0.properties.toolType = AMOTION_EVENT_TOOL_TYPE_FINGER;
    p0.coords.bits = AXIS_X_BIT | AXIS_Y_BIT | AXIS_PRESSURE_BIT | AXIS_SIZE_BIT;
    p0.coords.values[0] = x;       // AXIS_X
    p0.coords.values[1] = y;       // AXIS_Y
    p0.coords.values[2] = 1.0f;    // AXIS_PRESSURE
    p0.coords.values[3] = 0.01f;   // AXIS_SIZE
    p0.coords.isResampled = false;

    // Send only header + body-for-this-pointer-count (AOSP wire format).
    const size_t wireSize = sizeof(InputMessageHeader)
                            + motionBodyWireSize(1);
    ssize_t written = send(fd, &msg, wireSize, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (written < 0) {
        LOGE("writeMotionEvent: send failed, errno=%d (%s)", errno, strerror(errno));
        return -errno;
    }
    if (static_cast<size_t>(written) != wireSize) {
        LOGE("writeMotionEvent: short write %zd / %zu", written, wireSize);
        return -1;
    }
    return 0;
}

int OHInputBridge::writeKeyEvent(int fd, uint32_t seq, int32_t action,
                                  int32_t keyCode, int64_t downTime,
                                  int64_t eventTime) {
    struct {
        InputMessageHeader header;
        KeyEventBody body;
    } msg;
    memset(&msg, 0, sizeof(msg));

    msg.header.type = INPUT_MSG_TYPE_KEY;
    msg.header.seq = seq;

    msg.body.eventId = static_cast<int32_t>(seq);
    msg.body.eventTime = eventTime;
    msg.body.deviceId = 1;
    msg.body.source = AINPUT_SOURCE_KEYBOARD;
    msg.body.displayId = 0;
    // hmac already zeroed by memset
    msg.body.action = action;
    msg.body.flags = 0;
    msg.body.keyCode = keyCode;
    msg.body.scanCode = 0;
    msg.body.metaState = 0;
    msg.body.repeatCount = 0;
    msg.body.downTime = downTime;

    const size_t wireSize = sizeof(InputMessageHeader) + sizeof(KeyEventBody);
    ssize_t written = send(fd, &msg, wireSize, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (written < 0) {
        LOGE("writeKeyEvent: send failed, errno=%d (%s)", errno, strerror(errno));
        return -errno;
    }
    if (static_cast<size_t>(written) != wireSize) {
        LOGE("writeKeyEvent: short write %zd / %zu", written, wireSize);
        return -1;
    }
    return 0;
}

void OHInputBridge::monitorOHInputEvents() {
    LOGI("OH input monitor thread started");

    while (monitoring_.load()) {
        std::vector<pollfd> fds;
        std::vector<int32_t> sessionIds;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& pair : sessions_) {
                if (pair.second.ohInputFd >= 0 && pair.second.serverFd >= 0) {
                    pollfd pfd;
                    pfd.fd = pair.second.ohInputFd;
                    pfd.events = POLLIN;
                    pfd.revents = 0;
                    fds.push_back(pfd);
                    sessionIds.push_back(pair.first);
                }
            }
        }

        if (fds.empty()) {
            // No OH input fds to monitor yet, sleep briefly
            usleep(100000); // 100ms
            continue;
        }

        int ret = poll(fds.data(), fds.size(), 100 /* timeout_ms */);
        if (ret <= 0) continue;

        for (size_t i = 0; i < fds.size(); i++) {
            if (fds[i].revents & POLLIN) {
                /*
                 * Read OH input event and convert to Android MotionEvent.
                 *
                 * OH PointerEvent format (from MultiModal Input framework):
                 * - action: DOWN/UP/MOVE
                 * - pointerId, x, y, pressure
                 * - timestamp
                 *
                 * Phase 2: Parse the actual OH MMI event format here.
                 * Phase 1: The monitoring thread is set up but real OH
                 * event parsing depends on the actual OH MMI binary format.
                 */
                uint8_t buf[4096];
                ssize_t nread = read(fds[i].fd, buf, sizeof(buf));
                if (nread > 0) {
                    LOGD("OH input event received: session=%d, %zd bytes",
                         sessionIds[i], nread);
                    // Phase 2: Parse OH event format and call injectTouchEvent()
                }
            }
        }
    }

    LOGI("OH input monitor thread stopped");
}

// ============================================================
// OhMmiInputConsumer — bridges OH MMI events to Android InputChannel
//
// Implements OHOS::MMI::IInputEventConsumer. The MMI service holds a
// shared_ptr to this object (kept alive on the bridge side via
// OHInputBridge::mmiConsumer_) and dispatches each OH input event to one of
// the three OnInputEvent overloads on the registered EventHandler thread.
//
// Pointer events are converted to Android MotionEvent semantics and forwarded
// to OHInputBridge::injectTouchEvent, which writes a binary Android
// InputMessage to the InputChannel server fd; ViewRootImpl's
// WindowInputEventReceiver reads from the client end and dispatches up the
// View tree (HelloWorld's Button.OnClickListener.onClick eventually fires).
//
// Key / Axis events stay Phase 1 stub for now (see §3.3.5 KeyEvent path).
//
// See doc/Input_Adapter_design.html §3.3.5 for design rationale.
// ============================================================
class OhMmiInputConsumer : public OHOS::MMI::IInputEventConsumer {
public:
    OhMmiInputConsumer() = default;
    ~OhMmiInputConsumer() override = default;

    void OnInputEvent(std::shared_ptr<OHOS::MMI::KeyEvent> keyEvent) const override {
        if (!keyEvent) return;
        // Phase 2: translate OH MMI KeyEvent -> Android KeyEvent + publish a
        // type=KEY InputMessage through the session InputChannel, mirroring the
        // PointerEvent path. Enables D-pad/key navigation of the app's views.
        int32_t sessionId = OHInputBridge::getInstance().getActiveSessionId();
        int32_t ohCode = keyEvent->GetKeyCode();
        int32_t ohAction = keyEvent->GetKeyAction();

        // Bottom-nav tab trigger: the OHOS systemui nav window occludes y>=1208
        // from MMI AND noice's window isn't in the WMS stack (launcher EntryView
        // is topmost) — so direct taps on noice's BottomNavigationView never
        // arrive via the display. Map OH digit keys 1..5 (2001..2005) to an
        // IN-PROCESS tap on tab N; in-process dispatchTouchViaViewRoot reaches
        // noice's decor view tree regardless of WMS z-order / systemui overlap.
        if (ohAction == 2 /*KEY_ACTION_DOWN*/ && ohCode >= 2001 && ohCode <= 2005) {
            static const float tabX[5] = {72.f, 216.f, 360.f, 504.f, 648.f};
            int idx = ohCode - 2001;
            int64_t nowNs = keyEvent->GetActionTime() * 1000LL;
            LOGI("OnInputEvent(KeyEvent): bottom-nav tab-trigger ohCode=%d -> in-process tap (%.0f,1218)",
                 ohCode, tabX[idx]);
            OHInputBridge::getInstance().dispatchTouchViaViewRoot(
                1 /*ACTION_UP: synthesizes the full DOWN+UP tap*/, tabX[idx], 1218.f, nowNs, nowNs);
            keyEvent->MarkProcessed();
            return;
        }

        int32_t androidKey = ohKeyCodeToAndroid(ohCode);
        // OH KEY_ACTION_DOWN=2, KEY_ACTION_UP=3 -> Android DOWN=0, UP=1.
        int32_t androidAction = (ohAction == 3) ? AKEY_EVENT_ACTION_UP
                                                : AKEY_EVENT_ACTION_DOWN;
        if (sessionId < 0 || androidKey < 0) {
            LOGI("OnInputEvent(KeyEvent): ohCode=%d action=%d %s — drop",
                 ohCode, ohAction, sessionId < 0 ? "(no session)" : "(unmapped)");
            keyEvent->MarkProcessed();
            return;
        }
        int64_t eventTimeNs = keyEvent->GetActionTime() * 1000LL;
        static thread_local int64_t s_keyDownNs = 0;
        int64_t downTimeNs;
        if (androidAction == AKEY_EVENT_ACTION_DOWN) {
            s_keyDownNs = eventTimeNs;
            downTimeNs = eventTimeNs;
        } else {
            downTimeNs = (s_keyDownNs > 0) ? s_keyDownNs : eventTimeNs;
        }
        LOGI("OnInputEvent(KeyEvent): ohCode=%d -> android=%d action=%d session=%d (forwarding)",
             ohCode, androidKey, androidAction, sessionId);
        // Primary path: dispatch DIRECTLY into the focused ViewRootImpl (the
        // deployed runtime's InputChannel consumer drops type=KEY). Fall back
        // to the InputChannel write if direct dispatch can't find a receiver
        // (e.g. on a runtime whose consumer DOES handle KEY).
        int32_t rc = OHInputBridge::getInstance().dispatchKeyViaViewRoot(
            androidAction, androidKey, downTimeNs, eventTimeNs);
        if (rc != 0) {
            OHInputBridge::getInstance().injectKeyEvent(sessionId, androidAction,
                                                        androidKey, downTimeNs, eventTimeNs);
        }
        keyEvent->MarkProcessed();
    }

    void OnInputEvent(std::shared_ptr<OHOS::MMI::PointerEvent> pointerEvent) const override {
        if (!pointerEvent) return;

        int32_t sessionId = OHInputBridge::getInstance().getActiveSessionId();
        if (sessionId < 0) {
            LOGD("OnInputEvent(PointerEvent): no active session, drop");
            pointerEvent->MarkProcessed();
            return;
        }

        int32_t pointerId = pointerEvent->GetPointerId();
        OHOS::MMI::PointerEvent::PointerItem item;
        if (!pointerEvent->GetPointerItem(pointerId, item)) {
            LOGE("OnInputEvent(PointerEvent): GetPointerItem(%d) failed", pointerId);
            pointerEvent->MarkProcessed();
            return;
        }

        // OH MMI PointerEvent::POINTER_ACTION_* → Android MotionEvent action.
        // OH:  CANCEL=1, DOWN=2, MOVE=3, UP=4
        // AOSP MotionEvent: ACTION_DOWN=0, UP=1, MOVE=2, CANCEL=3
        int32_t androidAction = -1;
        switch (pointerEvent->GetPointerAction()) {
            case OHOS::MMI::PointerEvent::POINTER_ACTION_DOWN:    androidAction = 0; break;
            case OHOS::MMI::PointerEvent::POINTER_ACTION_UP:      androidAction = 1; break;
            case OHOS::MMI::PointerEvent::POINTER_ACTION_MOVE:    androidAction = 2; break;
            case OHOS::MMI::PointerEvent::POINTER_ACTION_CANCEL:  androidAction = 3; break;
            default:
                LOGD("OnInputEvent(PointerEvent): unhandled OH action=%d, drop",
                     pointerEvent->GetPointerAction());
                pointerEvent->MarkProcessed();
                return;
        }

        // Use window-relative X/Y (aligned with ArkUI mmi_event_convertor.cpp:190-194).
        // InputMessage transform is identity (dsdx=dsdy=1, tx=ty=0), so AOSP consumer
        // treats MotionEvent.getX/Y as window-relative — feeding window coords
        // directly is correct regardless of where SCB places the window on display.
        // GetWindowXPos returns double for sub-pixel precision; fall back to
        // GetWindowX (int) when sub-pixel data unavailable.
        float x = static_cast<float>(item.GetWindowXPos());
        float y = static_cast<float>(item.GetWindowYPos());
        if (x == 0.0f && y == 0.0f) {
            x = static_cast<float>(item.GetWindowX());
            y = static_cast<float>(item.GetWindowY());
        }

        // OH timestamps are microseconds; Android InputMessage expects ns.
        int64_t eventTimeNs = pointerEvent->GetActionTime() * 1000LL;
        // OH PointerEvent::GetDownTime() returns 0 on this device (OH MMI
        // doesn't populate it for synthetic / non-Stage callers).  AOSP
        // ViewRootImpl click detection compares (eventTime - downTime) against
        // ViewConfiguration.getLongPressTimeout() (500ms); a downTime=0 with
        // multi-second eventTime registers as a non-click long-press and
        // Button.performClick() never fires.  Adapter caches its own
        // per-action-sequence downTime: set at DOWN, reused at MOVE/UP, reset
        // at UP/CANCEL.  Phase 1: single-touch only, single static cache;
        // multi-touch (Phase 2) needs per-pointerId map.
        static thread_local int64_t s_cachedDownTimeNs = 0;
        int64_t downTimeNs;
        if (androidAction == 0 /* AMOTION_EVENT_ACTION_DOWN */) {
            s_cachedDownTimeNs = eventTimeNs;
            downTimeNs = eventTimeNs;
        } else {
            int64_t ohDownNs = item.GetDownTime() * 1000LL;
            if (ohDownNs > 0) {
                downTimeNs = ohDownNs;        // trust OH if it actually has one
            } else if (s_cachedDownTimeNs > 0) {
                downTimeNs = s_cachedDownTimeNs;  // fall back to adapter cache
            } else {
                downTimeNs = eventTimeNs;     // no prior DOWN seen; use event time
            }
            if (androidAction == 1 /* UP */ || androidAction == 3 /* CANCEL */) {
                int64_t carry = downTimeNs;
                s_cachedDownTimeNs = 0;
                downTimeNs = carry;
            }
        }

        LOGI("OnInputEvent(PointerEvent): session=%d action=%d (oh=%d) x=%.1f y=%.1f "
             "down=%lldns evt=%lldns",
             sessionId, androidAction, pointerEvent->GetPointerAction(), x, y,
             (long long)downTimeNs, (long long)eventTimeNs);

        // Bottom-nav tab proxy: noice's BottomNavigationView (y>=1208) is
        // occluded from MMI by the systemui nav window, AND key events don't
        // reach noice (the launcher is WMS-topmost so it holds MMI key focus,
        // noice's window isn't in WMS — displayId:-1). MMI DOES deliver pointer
        // events in noice's content area, so we repurpose a deliverable strip
        // of the top toolbar (声音库 title bar, y in [76,130], no tap action)
        // as a 5-column tab selector → in-process tap on bottom-nav tab N.
        if (androidAction == 1 /*ACTION_UP*/ && y >= 76.0f && y <= 130.0f) {
            static const float tabX[5] = {72.f, 216.f, 360.f, 504.f, 648.f};
            int idx = static_cast<int>(x / 144.0f);
            if (idx < 0) idx = 0; if (idx > 4) idx = 4;
            LOGI("OnInputEvent(PointerEvent): top-band tab-proxy x=%.0f -> in-process tap tab%d (%.0f,1218)",
                 x, idx, tabX[idx]);
            OHInputBridge::getInstance().dispatchTouchViaViewRoot(
                1, tabX[idx], 1218.0f, downTimeNs, eventTimeNs);
            pointerEvent->MarkProcessed();
            return;
        }

        // Primary: forward the REAL action (DOWN/MOVE/UP/CANCEL) into the focused
        // ViewRootImpl so RecyclerView/ScrollView get a coherent moving-pointer
        // stream -> scroll works. (dispatchTouchViaViewRoot only acts on UP and
        // synthesizes a stationary tap, discarding the real DOWN+MOVE samples ->
        // no scroll; that path is kept for the control channel + tab-proxy.)
        // Fall back to the InputChannel write if no receiver.
        int32_t trc = OHInputBridge::getInstance().dispatchSingleTouchViaViewRoot(
            androidAction, x, y, downTimeNs, eventTimeNs);
        if (trc != 0) {
            OHInputBridge::getInstance().injectTouchEvent(
                sessionId, androidAction, x, y, downTimeNs, eventTimeNs);
        }

        pointerEvent->MarkProcessed();
    }

    void OnInputEvent(std::shared_ptr<OHOS::MMI::AxisEvent> axisEvent) const override {
        // Axis events (wheel / pinch / etc.) — not used by HelloWorld; Phase 1 noop.
        if (axisEvent) axisEvent->MarkProcessed();
    }
};

// ============================================================
// startTapControlChannel — reliable in-process tap side-channel
// ============================================================
// OHOS MMI pointer delivery to noice's process is foundation-flaky
// (displayId:-1 — noice's window isn't in WMS), so a real tap only sometimes
// reaches the bridge. This poller reads a control file and dispatches the tap
// IN-PROCESS (dispatchTouchViaViewRoot — proven to drive the view tree
// regardless of WMS/systemui), giving deterministic interaction for testing:
//   echo "84 337"  > /data/local/tmp/noice_tap.<pid>  # raw window coords
//   echo "2"        > /data/local/tmp/noice_tap.<pid>  # bottom-nav tab 2 (1..5)
// The file is truncated after each command so it fires exactly once.
void OHInputBridge::startTapControlChannel() {
    static std::atomic<bool> started{false};
    bool expected = false;
    if (!started.compare_exchange_strong(expected, true)) return;
    std::thread([]() {
        // WESTLAKE §704: appspawn-x forks the Android child after this bridge is loaded,
        // so both processes run a poller.  A single global file lets the parent consume a
        // command intended for the child, making taps and structural probes nondeterministic.
        // A PID-qualified boundary channel gives every process exactly one mailbox while
        // keeping the Android input path itself unchanged.
        char path[96];
        snprintf(path, sizeof(path), "/data/local/tmp/noice_tap.%d",
                 static_cast<int>(getpid()));
        for (;;) {
            // §558: was 300 ms. This poll is on the critical path of EVERY interaction, real
            // fingers included: touchfwd(1) reads /dev/input and writes this same file, so a
            // physical tap waited up to 300 ms (avg 150) before the bridge even looked. 25 ms
            // costs nothing measurable (an idle open()+read() of a tiny tmpfs file) and takes
            // that straight off perceived latency.
            usleep(25 * 1000);
            FILE* f = fopen(path, "r");
            if (!f) continue;
            char buf[64] = {0};
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            if (n == 0) continue;
            // WESTLAKE §405: non-numeric commands, so the whole app is drivable from the shell:
            //   echo back > /data/local/tmp/noice_tap     KEYCODE_BACK (leave a detail page)
            //   echo "x1 y1 x2 y2" > ...                  drag/swipe (ViewPager, sliders, scroll)
            //   echo "x y" > ...                          tap
            // §408 window commands:
            //   echo w        > noice_tap    list every ViewRootImpl (index/class/size/shown)
            //   echo r2       > noice_tap    aim all later input at root #2 (r-1 = auto)
            // §563: STREAMING touch. touchfwd only emits on finger LIFT, so nothing happens
            // while the finger is down (no press feedback) and a drag is replayed as one synthetic
            // swipe afterwards -- smooth scrolling is impossible by construction. MMI's own
            // DOWN/MOVE/UP stream would fix it, and dispatchSingleTouchViaViewRoot already exists
            // for exactly that, but OnInputEvent(PointerEvent) has NEVER fired on this board (the
            // WMS-focus wall), so that path is dead code. Feed it from the control channel instead:
            //     d <x> <y>   ACTION_DOWN   (also starts a new downTime)
            //     m <x> <y>   ACTION_MOVE
            //     u <x> <y>   ACTION_UP
            // Opt-in via WL_TOUCH_STREAM=1 in touchfwd; the lift-only tap path is untouched.
            if ((buf[0]=='d'||buf[0]=='m'||buf[0]=='u') && (buf[1]==' ')) {
                float sx = 0.f, sy = 0.f;
                if (sscanf(buf + 1, "%f %f", &sx, &sy) == 2) {
                    static int64_t s_streamDownNs = 0;
                    struct timespec ts_;
                    clock_gettime(CLOCK_MONOTONIC, &ts_);
                    const int64_t nowNs = (int64_t)ts_.tv_sec * 1000000000LL + ts_.tv_nsec;
                    int32_t act;
                    if (buf[0]=='d') { act = 0; s_streamDownNs = nowNs; }
                    else if (buf[0]=='m') { act = 2; }
                    else { act = 1; }
                    if (s_streamDownNs == 0) s_streamDownNs = nowNs;
                    OHInputBridge::getInstance().dispatchSingleTouchViaViewRoot(
                        act, sx, sy, s_streamDownNs, nowNs);
                }
                FILE* wS = fopen(path, "w"); if (wS) fclose(wS);
                continue;
            }
            if (buf[0] == 'w' || buf[0] == 'W') {
                FILE* ww = fopen(path, "w"); if (ww) fclose(ww);
                JNIEnv* denv = nullptr; bool det = false;
                JavaVM* vm = OHInputBridge::getInstance().javaVm();
                if (vm != nullptr) {
                    if (vm->GetEnv(reinterpret_cast<void**>(&denv), JNI_VERSION_1_6) != JNI_OK) {
                        JavaVMAttachArgs aa{JNI_VERSION_1_6, "oh-root-dump", nullptr};
                        if (vm->AttachCurrentThread(&denv, &aa) == JNI_OK) det = true; else denv = nullptr;
                    }
                    if (denv) { wl_dump_view_roots(denv); if (det) vm->DetachCurrentThread(); }
                }
                continue;
            }
            if (buf[0] == 'x' || buf[0] == 'X') {     // §438: bytecode-vs-JNI proxy test
                char c1[96] = {0}, c2[64] = {0}, c3[128] = {0};
                const int got = sscanf(buf + 1, "%95s %63s %127s", c1, c2, c3);
                FILE* wx = fopen(path, "w"); if (wx) fclose(wx);
                if (got != 3) { LOGE("§438 usage: x <class> <method> <sig>"); continue; }
                JNIEnv* denv = nullptr; bool det = false;
                JavaVM* vm = OHInputBridge::getInstance().javaVm();
                if (vm != nullptr) {
                    if (vm->GetEnv(reinterpret_cast<void**>(&denv), JNI_VERSION_1_6) != JNI_OK) {
                        JavaVMAttachArgs aa{JNI_VERSION_1_6, "oh-proxy-test", nullptr};
                        if (vm->AttachCurrentThread(&denv, &aa) == JNI_OK) det = true; else denv = nullptr;
                    }
                    if (denv) { wl_proxy_dispatch_test(denv, c1, c2, c3); if (det) vm->DetachCurrentThread(); }
                }
                continue;
            }
            if (buf[0] == 'p' || buf[0] == 'P') {     // §437: proxy-dispatch probe
                char cls[128] = {0};
                if (sscanf(buf + 1, "%127s", cls) != 1) { FILE* wp2 = fopen(path, "w"); if (wp2) fclose(wp2); continue; }
                FILE* wp2 = fopen(path, "w"); if (wp2) fclose(wp2);
                JNIEnv* denv = nullptr; bool det = false;
                JavaVM* vm = OHInputBridge::getInstance().javaVm();
                if (vm != nullptr) {
                    if (vm->GetEnv(reinterpret_cast<void**>(&denv), JNI_VERSION_1_6) != JNI_OK) {
                        JavaVMAttachArgs aa{JNI_VERSION_1_6, "oh-proxy-probe", nullptr};
                        if (vm->AttachCurrentThread(&denv, &aa) == JNI_OK) det = true; else denv = nullptr;
                    }
                    if (denv) { wl_proxy_probe(denv, cls); if (det) vm->DetachCurrentThread(); }
                }
                continue;
            }
            // WESTLAKE §727 diagnostic: drive a tap through the real Android InputChannel
            // receiver instead of invoking View.dispatchTouchEvent on this polling thread.
            // android_view_InputEventReceiver hands the event to
            // InputEventBridge.dispatchOnMainThread, preserving AOSP's main-looper input
            // contract. Use `i x y`; the ordinary numeric harness path remains unchanged for
            // a one-variable A/B comparison.
            if ((buf[0] == 'i' || buf[0] == 'I') && buf[1] == ' ') {
                float ix = -1.0f, iy = -1.0f;
                FILE* wi = fopen(path, "w"); if (wi) fclose(wi);
                if (sscanf(buf + 1, "%f %f", &ix, &iy) != 2) continue;
                OHInputBridge& bridge = OHInputBridge::getInstance();
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                const int64_t downNs = static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
                const int downResult = bridge.dispatchSingleTouchViaViewRoot(
                    0, ix, iy, downNs, downNs);
                usleep(120 * 1000);
                clock_gettime(CLOCK_MONOTONIC, &ts);
                const int64_t upNs = static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
                const int upResult = bridge.dispatchSingleTouchViaViewRoot(
                    1, ix, iy, downNs, upNs);
                fprintf(stderr,
                        "[WESTLAKE-INPUTMAIN-727] receiver tap=(%.0f,%.0f) down=%d up=%d\n",
                        static_cast<double>(ix), static_cast<double>(iy),
                        downResult, upResult);
                fflush(stderr);
                continue;
            }
            // WESTLAKE §798: explicit main-MessageQueue hit-tested click.
            // This is a harness fallback for rows whose delayed Java
            // CheckForTap/PerformClick is stranded behind a sync barrier; it
            // does not change the ordinary numeric DOWN/UP command.
            if ((buf[0] == 'c' || buf[0] == 'C') && buf[1] == ' ') {
                float cx = -1.0f, cy = -1.0f;
                FILE* wc = fopen(path, "w"); if (wc) fclose(wc);
                if (sscanf(buf + 1, "%f %f", &cx, &cy) != 2) continue;
                const int clickResult = OHInputBridge::getInstance().dispatchTouchViaViewRoot(
                    42, cx, cy, 0, 0);
                fprintf(stderr,
                        "[WESTLAKE-TOUCHFD-798] command click=(%.0f,%.0f) result=%d\n",
                        static_cast<double>(cx), static_cast<double>(cy), clickResult);
                fflush(stderr);
                continue;
            }
            // §570: mark an app class's methods non-compilable so the JIT skips them.
            //   echo "J kotlin.coroutines.jvm.internal.ContinuationImpl" > /data/local/tmp/noice_tap
            // Must run BEFORE the JIT goes live (APPSPAWNX_JIT_DELAY_MS leaves a window for it) —
            // a method already compiled keeps its compiled entry point.
            if (buf[0] == 'J') {
                char cls[192] = {0};
                if (sscanf(buf + 1, "%191s", cls) != 1) { FILE* wj = fopen(path, "w"); if (wj) fclose(wj); continue; }
                FILE* wj = fopen(path, "w"); if (wj) fclose(wj);
                JNIEnv* denv = nullptr; bool det = false;
                JavaVM* vm = OHInputBridge::getInstance().javaVm();
                if (vm != nullptr) {
                    if (vm->GetEnv(reinterpret_cast<void**>(&denv), JNI_VERSION_1_6) != JNI_OK) {
                        JavaVMAttachArgs aa{JNI_VERSION_1_6, "oh-jit-exclude", nullptr};
                        if (vm->AttachCurrentThread(&denv, &aa) == JNI_OK) det = true; else denv = nullptr;
                    }
                    if (denv) { wl_jit_exclude_class(denv, cls); if (det) vm->DetachCurrentThread(); }
                }
                continue;
            }
            if (buf[0] == 'q' || buf[0] == 'Q') {     // §433: reflect on an app class
                char cls[128] = {0};
                if (sscanf(buf + 1, "%127s", cls) != 1) { FILE* wq = fopen(path, "w"); if (wq) fclose(wq); continue; }
                FILE* wq = fopen(path, "w"); if (wq) fclose(wq);
                JNIEnv* denv = nullptr; bool det = false;
                JavaVM* vm = OHInputBridge::getInstance().javaVm();
                if (vm != nullptr) {
                    if (vm->GetEnv(reinterpret_cast<void**>(&denv), JNI_VERSION_1_6) != JNI_OK) {
                        JavaVMAttachArgs aa{JNI_VERSION_1_6, "oh-class-probe", nullptr};
                        if (vm->AttachCurrentThread(&denv, &aa) == JNI_OK) det = true; else denv = nullptr;
                    }
                    if (denv) { wl_inspect_app_class(denv, cls); if (det) vm->DetachCurrentThread(); }
                }
                continue;
            }
            if (buf[0] == 'T') {                       // Java stacks for native-wait diagnosis
                FILE* wt = fopen(path, "w"); if (wt) fclose(wt);
                JNIEnv* denv = nullptr; bool det = false;
                JavaVM* vm = OHInputBridge::getInstance().javaVm();
                if (vm != nullptr) {
                    if (vm->GetEnv(reinterpret_cast<void**>(&denv), JNI_VERSION_1_6) != JNI_OK) {
                        JavaVMAttachArgs aa{JNI_VERSION_1_6, "oh-java-stack-dump", nullptr};
                        if (vm->AttachCurrentThread(&denv, &aa) == JNI_OK) det = true; else denv = nullptr;
                    }
                    if (denv) {
                        wl_dump_java_thread_stacks(denv);
                        if (det) vm->DetachCurrentThread();
                    }
                }
                continue;
            }
            if (buf[0] == 'G') {                       // §802: Toutiao aggr response/adapter state
                FILE* wg = fopen(path, "w"); if (wg) fclose(wg);
                JNIEnv* denv = nullptr; bool det = false;
                JavaVM* vm = OHInputBridge::getInstance().javaVm();
                if (vm != nullptr) {
                    if (vm->GetEnv(reinterpret_cast<void**>(&denv), JNI_VERSION_1_6) != JNI_OK) {
                        JavaVMAttachArgs aa{JNI_VERSION_1_6, "oh-toutiao-aggr-dump", nullptr};
                        if (vm->AttachCurrentThread(&denv, &aa) == JNI_OK) det = true; else denv = nullptr;
                    }
                    if (denv) {
                        wl_dump_toutiao_aggr_state(denv);
                        if (det) vm->DetachCurrentThread();
                    }
                }
                continue;
            }
            if (buf[0] == 'N') {                       // §803: Toutiao Cronet boot-success state
                FILE* wn = fopen(path, "w"); if (wn) fclose(wn);
                JNIEnv* denv = nullptr; bool det = false;
                JavaVM* vm = OHInputBridge::getInstance().javaVm();
                if (vm != nullptr) {
                    if (vm->GetEnv(reinterpret_cast<void**>(&denv), JNI_VERSION_1_6) != JNI_OK) {
                        JavaVMAttachArgs aa{JNI_VERSION_1_6, "oh-toutiao-cronet-dump", nullptr};
                        if (vm->AttachCurrentThread(&denv, &aa) == JNI_OK) det = true; else denv = nullptr;
                    }
                    if (denv) {
                        wl_dump_toutiao_cronet_state(denv);
                        if (det) vm->DetachCurrentThread();
                    }
                }
                continue;
            }
            if (buf[0] == 'v' || buf[0] == 'V') {     // §409: dump the view hierarchy
                FILE* wv = fopen(path, "w"); if (wv) fclose(wv);
                int ri = -1; sscanf(buf + 1, "%d", &ri);
                JNIEnv* denv = nullptr; bool det = false;
                JavaVM* vm = OHInputBridge::getInstance().javaVm();
                if (vm != nullptr) {
                    if (vm->GetEnv(reinterpret_cast<void**>(&denv), JNI_VERSION_1_6) != JNI_OK) {
                        JavaVMAttachArgs aa{JNI_VERSION_1_6, "oh-view-dump", nullptr};
                        if (vm->AttachCurrentThread(&denv, &aa) == JNI_OK) det = true; else denv = nullptr;
                    }
                    if (denv) { wl_dump_view_tree(denv, ri); if (det) vm->DetachCurrentThread(); }
                }
                continue;
            }
            if (buf[0] == 'r' || buf[0] == 'R') {
                FILE* wr = fopen(path, "w"); if (wr) fclose(wr);
                int idx = -1;
                if (sscanf(buf + 1, "%d", &idx) == 1) {
                    wl_set_root_index(idx);
                    LOGI("tapControlChannel: input target root=%d", idx);
                }
                continue;
            }
            if (buf[0] == 'b' || buf[0] == 'B') {
                FILE* wb = fopen(path, "w"); if (wb) fclose(wb);
                LOGI("tapControlChannel: BACK key");
                const int64_t t = 0;
                OHInputBridge::getInstance().dispatchKeyViaViewRoot(0 /*ACTION_DOWN*/, 4 /*BACK*/, t, t);
                usleep(60 * 1000);
                OHInputBridge::getInstance().dispatchKeyViaViewRoot(1 /*ACTION_UP*/, 4 /*BACK*/, t, t);
                continue;
            }
            int a = -1, b = -1, c = -1, d = -1;
            int cnt = sscanf(buf, "%d %d %d %d", &a, &b, &c, &d);
            float x = -1.0f, y = -1.0f;
            if (cnt == 4) {
                FILE* w0 = fopen(path, "w"); if (w0) fclose(w0);
                LOGI("tapControlChannel: drag (%d,%d)->(%d,%d)", a, b, c, d);
                OHInputBridge::getInstance().dispatchDragViaViewRoot(
                    (float)a, (float)b, (float)c, (float)d);
                continue;
            }
            if (cnt == 1 && a >= 1 && a <= 5) {
                static const float tabX[5] = {72.f, 216.f, 360.f, 504.f, 648.f};
                x = tabX[a - 1]; y = 1218.0f;
            } else if (cnt == 2) {
                x = static_cast<float>(a); y = static_cast<float>(b);
            }
            // fire once: truncate the control file
            FILE* w = fopen(path, "w"); if (w) fclose(w);
            if (x < 0.0f) continue;
            LOGI("tapControlChannel: in-process tap (%.0f,%.0f)", x, y);
            OHInputBridge::getInstance().dispatchTouchViaViewRoot(1, x, y, 0, 0);
        }
    }).detach();
    LOGI("startTapControlChannel: polling /data/local/tmp/noice_tap");
}

// ============================================================
// startTextControlChannel — in-process text entry side-channel
// ============================================================
// No soft keyboard summons on this board (the IMM->OHOS-IME attach path is
// dead — OhImeBridge isn't in the deployed jars), and hardware keys don't
// reach app windows (MMI wall). So text entry into a focused EditText is done
// the same way taps are: build KeyEvents and dispatch them straight into the
// focused ViewRootImpl on the UI thread (dispatchKeyViaViewRoot), where the
// editor's KeyListener inserts the characters — exactly the physical-keyboard
// path, no IME needed. App-agnostic: whatever editable view holds focus gets
// the text. Write one command per line to /data/local/tmp/noice_text:
//   noicetest@web-library.net   -> types the string
//   ENTER                       -> KEYCODE_ENTER (submit)
//   DEL / DEL 5                 -> N backspaces
//   CLEAR                       -> empties the field (many backspaces)
namespace {
// Android KeyEvent constants (subset needed for text entry).
constexpr int32_t KC_0 = 7, KC_A = 29, KC_SPACE = 62, KC_ENTER = 66, KC_DEL = 67;
constexpr int32_t KC_PERIOD = 56, KC_MINUS = 69, KC_PLUS = 81, KC_AT = 77;
constexpr int32_t META_SHIFT = 0x00000001 /*META_SHIFT_ON*/ | 0x00000040 /*META_SHIFT_LEFT_ON*/;
// Map one ASCII char -> (keyCode, needsShift). Returns false if unsupported.
bool charToKey(char ch, int32_t& code, bool& shift) {
    shift = false;
    if (ch >= 'a' && ch <= 'z') { code = KC_A + (ch - 'a'); return true; }
    if (ch >= 'A' && ch <= 'Z') { code = KC_A + (ch - 'A'); shift = true; return true; }
    if (ch >= '0' && ch <= '9') { code = KC_0 + (ch - '0'); return true; }
    switch (ch) {
        case ' ': code = KC_SPACE; return true;
        case '@': code = KC_AT; return true;          // dedicated keycode -> '@'
        case '.': code = KC_PERIOD; return true;
        case '-': code = KC_MINUS; return true;
        case '_': code = KC_MINUS; shift = true; return true;
        case '+': code = KC_PLUS; return true;
        default: return false;
    }
}
}  // namespace

void OHInputBridge::startTextControlChannel() {
    static std::atomic<bool> started{false};
    bool expected = false;
    if (!started.compare_exchange_strong(expected, true)) return;
    std::thread([this]() {
        const char* path = "/data/local/tmp/noice_text";
        auto tapKey = [this](int32_t code, int32_t meta) {
            int64_t t = (int64_t)0;  // downTime/eventTime relative; 0 is accepted
            dispatchKeyViaViewRoot(0 /*ACTION_DOWN*/, code, t, t, meta);
            usleep(8 * 1000);
            dispatchKeyViaViewRoot(1 /*ACTION_UP*/, code, t, t, meta);
        };
        for (;;) {
            usleep(25 * 1000);   // §558: same reasoning as the tap channel above.
            FILE* f = fopen(path, "r");
            if (!f) continue;
            char buf[512] = {0};
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            if (n == 0) continue;
            // fire once: truncate the control file immediately
            FILE* w = fopen(path, "w"); if (w) fclose(w);
            // strip a single trailing newline
            while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = 0;
            if (n == 0) continue;

            if (strcmp(buf, "ENTER") == 0) {
                LOGI("textControlChannel: ENTER");
                tapKey(KC_ENTER, 0);
                continue;
            }
            if (strncmp(buf, "DEL", 3) == 0) {
                int cnt = 1; sscanf(buf + 3, "%d", &cnt); if (cnt < 1) cnt = 1;
                LOGI("textControlChannel: DEL x%d", cnt);
                for (int i = 0; i < cnt; ++i) { tapKey(KC_DEL, 0); usleep(15 * 1000); }
                continue;
            }
            if (strcmp(buf, "CLEAR") == 0) {
                LOGI("textControlChannel: CLEAR");
                for (int i = 0; i < 80; ++i) { tapKey(KC_DEL, 0); usleep(6 * 1000); }
                continue;
            }
            // Plain text: commit through the focused TextView on Android's main
            // thread (KeyCharacterMap-independent). Falls back to per-key
            // dispatch only if the focused-text path reports failure.
            LOGI("textControlChannel: type \"%s\" (%zu chars)", buf, n);
            // §556: PER-KEY IS NOW THE DEFAULT. The string path builds one ACTION_MULTIPLE
            // KeyEvent carrying the characters — deprecated in API 29, and this runtime's
            // TextView/Editor never commits it. It nonetheless returned 0 and logged
            // "-> ViewRootImpl OK", so the per-key fallback never ran and typing silently did
            // nothing into a focused, in-touch-mode EditText (f=1 ftm=1 itm=1 confirmed).
            if (dispatchCharactersViaViewRoot(buf) != 0) {
                LOGE("textControlChannel: focused-text path failed, per-key fallback");
                for (size_t i = 0; i < n; ++i) {
                    int32_t code; bool shift;
                    if (!charToKey(buf[i], code, shift)) continue;
                    tapKey(code, shift ? META_SHIFT : 0);
                    usleep(30 * 1000);
                }
            }
            LOGI("textControlChannel: type done");
        }
    }).detach();
    LOGI("startTextControlChannel: polling /data/local/tmp/noice_text");
}

void OHInputBridge::subscribeMmi(int32_t sessionId) {
    // Hosted sessions use the device-ABI boundary helper. In particular, do
    // not allocate EventHandler from Rosen's portable lookalike header, and
    // do not subscribe a global input monitor for an ordinary app window.
    if (getenv("WL_ABILITY_OWNED_WINDOWS") != nullptr) {
        startTapControlChannel();
        startTextControlChannel();
        using Subscribe = int (*)(int32_t);
        auto subscribe = reinterpret_cast<Subscribe>(dlsym(RTLD_DEFAULT, "oh_owned_input_subscribe"));
        const int rc = subscribe ? subscribe(sessionId) : -1;
        LOGI("subscribeMmi: ability-owned session=%d rc=%d", sessionId, rc);
        return;
    }
    // Update active session id first — consumer reads it via getActiveSessionId().
    activeSessionId_.store(sessionId);

    // Single-shot subscription per process. Once MMI knows our process is the
    // input target for the focused window, it dispatches all events here;
    // subsequent createSession calls (e.g. a second activity) just update the
    // session id we route to.
    bool expected = false;
    if (!mmiSubscribed_.compare_exchange_strong(expected, true)) {
        LOGI("subscribeMmi: already subscribed, updated activeSessionId=%d", sessionId);
        return;
    }

    // Start the reliable in-process tap + text side-channels (once per process).
    startTapControlChannel();
    startTextControlChannel();

    // EventHandler running on the current thread's runner; if we're on a
    // worker thread without a runner, fall back to creating a dedicated runner
    // so MMI callbacks have somewhere to land. HelloWorld createSession is
    // called from JNI on a binder/worker thread, so we explicitly Create() a
    // new runner thread for MMI delivery — this matches how OH native
    // InputTransferStation creates its INPUT_AND_VSYNC_THREAD runner.
    auto runner = OHOS::AppExecFwk::EventRunner::Create("AdapterMmiConsumer");
    if (!runner) {
        LOGE("subscribeMmi: EventRunner::Create failed");
        mmiSubscribed_.store(false);
        return;
    }
    mmiEventHandler_ = std::make_shared<OHOS::AppExecFwk::EventHandler>(runner);
    mmiConsumer_ = std::make_shared<OhMmiInputConsumer>();

    int32_t rc = OHOS::MMI::InputManager::GetInstance()->SetWindowInputEventConsumer(
        mmiConsumer_, mmiEventHandler_);
    if (rc != 0) {
        LOGE("subscribeMmi: SetWindowInputEventConsumer rc=%d (session=%d)", rc, sessionId);
        // Don't roll back state — MMI may have partially registered; future
        // calls just no-op.
        return;
    }
    LOGI("subscribeMmi: MMI consumer registered for session=%d (Input_Adapter_design §3.3.5)",
         sessionId);

    // [2ND-WINDOW INPUT FIX 2026-06-28] SetWindowInputEventConsumer only fires
    // when OHOS MMI resolves a touch to OUR window — which FAILS for 2nd-layer
    // Activities (WMS active windowId mismatch + displayId:-1 =>
    // InputWindowsManager "active window N not found" => tap dropped). A global
    // AddMonitor receives EVERY touch regardless of window resolution; the same
    // consumer forwards it to the focused app's ViewRoot
    // (getActiveSessionId -> dispatchTouchViaViewRoot), making 2nd-layer windows
    // clickable. HANDLE_EVENT_TYPE_KP = key + pointer.
    int32_t monId = OHOS::MMI::InputManager::GetInstance()->AddMonitor(mmiConsumer_);
    if (monId < 0) {
        LOGE("subscribeMmi: AddMonitor rc=%d (ohos.permission.INPUT_MONITORING?) "
             "— per-window consumer still active", monId);
    } else {
        LOGI("subscribeMmi: global input monitor id=%d registered (2nd-window fix)", monId);
    }
}

}  // namespace oh_adapter
