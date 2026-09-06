/*
 * Copyright (c) 2023-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OHOS_ROSEN_WINDOW_SCENE_WS_COMMON_H
#define OHOS_ROSEN_WINDOW_SCENE_WS_COMMON_H

#include <bitset>
#include <charconv>
#include <inttypes.h>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>

#include <iremote_broker.h>
#include <want.h>
#include "wm_animation_common.h"
#include "pixel_map.h"

namespace OHOS::AAFwk {
class AbilityStartSetting;
class ProcessOptions;
class StartWindowOption;
}
namespace OHOS::AppExecFwk {
struct AbilityInfo;
enum class SupportWindowMode;
}

namespace OHOS::Rosen {
class RSTransaction;

// Type alias for screen identifier, consistent with ScreenId defined in dm_common.h.
using ScreenId = uint64_t;

constexpr int32_t ROTATE_ANIMATION_DURATION = 400;
constexpr int32_t INVALID_SESSION_ID = 0;
constexpr int64_t INVALID_TIME_STAMP = 0;
constexpr float INVALID_SCALE = 0;
constexpr int32_t MIN_REQUEST_ID_FROM_ABILITY = 1;
constexpr int32_t DEFAULT_REQUEST_FROM_SCB_ID = -1;
constexpr int32_t WINDOW_SUPPORT_MODE_MAX_SIZE = 4;
constexpr int32_t DEFAULT_SCALE_RATIO = 100;
constexpr uint32_t COLOR_WHITE = 0xffffffff;
constexpr uint32_t COLOR_BLACK = 0xff000000;
extern const std::string WINDOW_SCREEN_LOCK_PREFIX;
extern const std::string VIEW_SCREEN_LOCK_PREFIX;
constexpr int32_t DEFAULT_INVALID_WINDOW_MODE = 0;
constexpr uint32_t ICON_MAX_SIZE = 128 * 1024 * 1024;

constexpr uint32_t ADVANCED_FEATURE_BIT_MAX = 32;
constexpr uint32_t ADVANCED_FEATURE_BIT_LOCK_CURSOR = 0x00;
constexpr uint32_t ADVANCED_FEATURE_BIT_CURSOR_FOLLOW_MOVEMENT = 0x01;
constexpr uint32_t ADVANCED_FEATURE_BIT_WINDOW_SEPARATION_TOUCH_ENABLED = 0x02;
constexpr uint32_t ADVANCED_FEATURE_BIT_RECEIVE_DRAG_EVENT = 0x03;

constexpr int32_t DECIMAL_BASE = 10;

enum class WSError : int32_t {
    WS_OK = 0,
    WS_DO_NOTHING,
    WS_ERROR_NO_MEM,
    WS_ERROR_DESTROYED_OBJECT,
    WS_ERROR_INVALID_WINDOW,
    WS_ERROR_INVALID_WINDOW_MODE_OR_SIZE,
    WS_ERROR_INVALID_OPERATION,
    WS_ERROR_INVALID_PERMISSION,
    WS_ERROR_NOT_SYSTEM_APP,
    WS_ERROR_NO_REMOTE_ANIMATION,
    WS_ERROR_INVALID_DISPLAY,
    WS_ERROR_INVALID_PARENT,
    WS_ERROR_OPER_FULLSCREEN_FAILED,
    WS_ERROR_REPEAT_OPERATION,
    WS_ERROR_INVALID_SESSION,
    WS_ERROR_INVALID_CALLING,
    WS_ERROR_UNCLEARABLE_SESSION,
    WS_ERROR_FAIL_TO_GET_SNAPSHOT,
    WS_ERROR_INTERNAL_ERROR,
    WS_ERROR_NO_UI_CONTENT_ERROR,

    WS_ERROR_DEVICE_NOT_SUPPORT = 801, // the value do not change.It is defined on all system

    WS_ERROR_NEED_REPORT_BASE = 1000, // error code > 1000 means need report
    WS_ERROR_NULLPTR,
    WS_ERROR_INVALID_TYPE,
    WS_ERROR_INVALID_PARAM,
    WS_ERROR_SAMGR,
    WS_ERROR_IPC_FAILED,
    WS_ERROR_NEED_REPORT_END,
    WS_ERROR_START_ABILITY_FAILED,
    WS_ERROR_SET_SESSION_LABEL_FAILED,
    WS_ERROR_SET_SESSION_ICON_FAILED,
    WS_ERROR_INVALID_SESSION_LISTENER,
    WS_ERROR_START_UI_EXTENSION_ABILITY_FAILED,
    WS_ERROR_MIN_UI_EXTENSION_ABILITY_FAILED,
    WS_ERROR_TERMINATE_UI_EXTENSION_ABILITY_FAILED,
    WS_ERROR_PRE_HANDLE_COLLABORATOR_FAILED,
    WS_ERROR_START_UI_ABILITY_TIMEOUT,

    WS_ERROR_EDM_CONTROLLED = 2097215, // enterprise limit
};

enum class WSErrorCode : int32_t {
    WS_OK = 0,
    WS_ERROR_NO_PERMISSION = 201,
    WS_ERROR_INVALID_PARAM = 401,
    WS_ERROR_DEVICE_NOT_SUPPORT = 801,
    WS_ERROR_NOT_REGISTER_SYNC_CALLBACK = 100011,
    WS_ERROR_TRANSFER_DATA_FAILED       = 100012,
    WS_ERROR_REPEAT_OPERATION = 1300001,
    WS_ERROR_STATE_ABNORMALLY = 1300002,
    WS_ERROR_SYSTEM_ABNORMALLY = 1300003,
    WS_ERROR_INVALID_CALLING = 1300004,
    WS_ERROR_STAGE_ABNORMALLY = 1300005,
    WS_ERROR_CONTEXT_ABNORMALLY = 1300006,

    WS_ERROR_EDM_CONTROLLED = 16000013, // enterprise limit
};

extern const std::map<WSError, WSErrorCode> WS_JS_TO_ERROR_CODE_MAP;

bool CheckCollaboratorType(int32_t type);

enum class SessionState : uint32_t {
    STATE_DISCONNECT = 0,
    STATE_CONNECT,
    STATE_FOREGROUND,
    STATE_ACTIVE,
    STATE_INACTIVE,
    STATE_BACKGROUND,
    STATE_END,
};

enum ContinueState {
    CONTINUESTATE_UNKNOWN = -1,
    CONTINUESTATE_ACTIVE = 0,
    CONTINUESTATE_INACTIVE = 1,
    CONTINUESTATE_MAX
};

enum class StartMethod : int32_t {
    START_NORMAL,
    START_CALL
};

enum class SingleHandMode : int32_t {
    LEFT = 0,
    RIGHT,
    MIDDLE
};

/**
 * @brief collaborator type.
 */
enum CollaboratorType : int32_t {
    DEFAULT_TYPE = 0,
    RESERVE_TYPE,
    OTHERS_TYPE,
    REDIRECT_TYPE,
};

enum AncoSceneState: int32_t {
    DEFAULT_STATE = 0,
    NOTIFY_START,
    NOTIFY_CREATE,
    NOTIFY_LOAD,
    NOTIFY_UPDATE,
    NOTIFY_FOREGROUND,
    NOTIFY_START_FAILED,
};

/**
 * @brief collaborator type.
 */
enum SessionOperationType : int32_t {
    TYPE_DEFAULT = 0,
    TYPE_CLEAR,
};

enum class ManagerState : uint32_t {
    MANAGER_STATE_SCREEN_LOCKED = 0,
};

/**
 * @brief Enumerates picture in picture control status.
 */
enum class WsPiPControlStatus : int32_t {
    PLAY = 1,
    PAUSE = 0,
    OPEN = 1,
    CLOSE = 0,
    ENABLED = -2,
    DISABLED = -3,
};

/**
 * @brief Enumerates picture in picture control type.
 */
enum class WsPiPControlType : uint32_t {
    VIDEO_PLAY_PAUSE = 0,
    VIDEO_PREVIOUS = 1,
    VIDEO_NEXT = 2,
    FAST_FORWARD = 3,
    FAST_BACKWARD = 4,
    HANG_UP_BUTTON = 5,
    MICROPHONE_SWITCH = 6,
    CAMERA_SWITCH = 7,
    MUTE_SWITCH = 8,
    END,
};

enum class FocusChangeReason {
    /**
     * default focus change reason
     */
    DEFAULT = 0,

    /**
     * focus change for move up
     */
    MOVE_UP,

    /**
     * focus change for click
     */
    CLICK,

    /**
     * focus change for foreground
     */
    FOREGROUND,

    /**
     * focus change for background
     */
    BACKGROUND,

    /**
     * focus change for split screen.5
     */
    SPLIT_SCREEN,

    /**
     * focus change for full screen
     */
    FULL_SCREEN,

    /**
     * focus change for global search
     */
    SCB_SESSION_REQUEST,

    /**
     * focus change for floating scene
     */
    FLOATING_SCENE,

    /**
     * focus change for losing focus
     */
    SCB_SESSION_REQUEST_UNFOCUS,

    /**
     * focus change for client requerst.10
     */
    CLIENT_REQUEST,

    /**
     * focus change for wind
     */
    WIND,

    /**
     * focus change for app foreground
     */
    APP_FOREGROUND,

    /**
     * focus change for app background
     */
    APP_BACKGROUND,

    /**
     * focus change for recent,Multitasking
     */
    RECENT,

    /**
     * focus change for inner app.
     */
    SCB_START_APP,

    /**
     * focus for setting focuable.
     */
    FOCUSABLE,

    /**
     * select last focused app when requestSessionUnFocus.
     */
    LAST_FOCUSED_APP,

    /**
     * focus for zOrder pass through VOICE_INTERACTION.
     */
    VOICE_INTERACTION,

    /**
     * focus change for SA requerst.19
     */
    SA_REQUEST,

    /**
     * focus on previous window for system keyboard
     */
    SYSTEM_KEYBOARD,

    /**
     * focus change when pressing alt+tab or dock click
     */
    REQUEST_WITH_CHECK_SUB_WINDOW,

    /**
     * focus change max.
     */
    MAX,
};

enum class SceneType : uint8_t {
    DEFAULT = 0,
    WINDOW_SCENE,
    SYSTEM_WINDOW_SCENE,
    TRANSFORM_SCENE,
    PANEL_SCENE,
    INPUT_SCENE,
};

struct SessionViewportConfig {
    bool isDensityFollowHost_ = false;
    float density_ = 1.0f; // Indicates the host's density, if following the host, use it, otherwise get it from DMS.
    uint64_t displayId_ = 0;
    int32_t orientation_ = 0;
    uint32_t transform_ = 0;
};

struct WindowSizeLimits {
    uint32_t maxWindowWidth = 0;
    uint32_t minWindowWidth = 0;
    uint32_t maxWindowHeight = 0;
    uint32_t minWindowHeight = 0;

    bool operator==(const WindowSizeLimits& sizeLimits) const
    {
        return (maxWindowWidth == sizeLimits.maxWindowWidth && minWindowWidth == sizeLimits.minWindowWidth &&
            maxWindowHeight == sizeLimits.maxWindowHeight && minWindowHeight == sizeLimits.minWindowHeight);
    }
};

enum class StartWindowType : uint32_t {
    DEFAULT = 0,
    RETAIN_AND_INVISIBLE,
    REMOVE_NODE_INVISIBLE,
};

enum class SpecifiedReason : int32_t {
    DEFAULT = 0,
    FROM_ICON,
    FROM_RECENT,
};

struct AtomicServiceInfo {
    std::string appNameInfo_ = "";
    std::string eyelashRingIcon_ = "";
    std::string circleIcon_ = "";
    int32_t resizable_ = 0;
    std::vector<std::string> deviceTypes_;
    std::vector<std::string> supportWindowMode_;
};

struct PendingSessionActivationConfig {
    // is compulsion open
    bool forceStart = false;
    // is execute new want callback
    bool forceNewWant = true;
};

struct SessionInfo {
    std::string bundleName_ = "";
    std::string moduleName_ = "";
    std::string abilityName_ = "";
    int32_t appIndex_ = 0;
    bool isSystem_ = false;
    SceneType sceneType_ = SceneType::WINDOW_SCENE;
    uint32_t windowType_ = 1; // WINDOW_TYPE_APP_MAIN_WINDOW
    sptr<IRemoteObject> callerToken_ = nullptr;
    sptr<IRemoteObject> rootToken_ = nullptr;
    uint64_t screenId_ = -1ULL; // -1ULL：SCREEN_ID_INVALID
    bool isPersistentRecover_ = false;
    AtomicServiceInfo atomicServiceInfo_;

    mutable std::shared_ptr<AAFwk::Want> want = nullptr; // want for ability start
    std::shared_ptr<std::mutex> wantMutex_ = std::make_shared<std::mutex>();
    std::shared_ptr<AAFwk::Want> closeAbilityWant = nullptr;
    std::shared_ptr<AAFwk::AbilityStartSetting> startSetting = nullptr;
    std::shared_ptr<AAFwk::ProcessOptions> processOptions = nullptr;
    std::shared_ptr<AAFwk::StartWindowOption> startWindowOption = nullptr;
    mutable std::shared_ptr<AppExecFwk::AbilityInfo> abilityInfo = nullptr;
    int32_t resultCode = -1;
    int32_t requestCode = -1;
    int32_t errorCode = -1;
    std::string errorReason = "";
    bool shouldSkipKillInStartup = false;
    int32_t persistentId_ = INVALID_SESSION_ID;
    int32_t callerPersistentId_ = INVALID_SESSION_ID;
    std::string callerBundleName_ = "";
    std::string callerAbilityName_ = "";
    uint32_t callState_ = 0;
    uint32_t callingTokenId_ = 0;
    int32_t callerTypeForAnco = 0;
    bool reuse = false;
    int32_t windowMode = 0;
    StartMethod startMethod = StartMethod::START_NORMAL;
    bool lockedState = false;
    std::string time = "";
    ContinueState continueState = ContinueState::CONTINUESTATE_ACTIVE;
    int64_t uiAbilityId_ = 0;
    int32_t ancoSceneState = 0;
    bool isClearSession = false;
    std::string sessionAffinity = "";
    int32_t collaboratorType_ = CollaboratorType::DEFAULT_TYPE;
    SessionState sessionState_ = SessionState::STATE_DISCONNECT;
    uint32_t requestOrientation_ = 0;
    int32_t specificSessionRequestOrientation_ = -1;
    bool isRotable_ = false;
    bool isSetPointerAreas_ = false;
    bool isCastSession_ = false;
    uint32_t windowInputType_ = 0;
    uint32_t expandInputFlag_ = 0;
    std::bitset<ADVANCED_FEATURE_BIT_MAX> advancedFeatureFlag_ = 0;
    bool isReceiveDragEventEnabled_ = true;
    bool isSeparationTouchEnabled_ = true;
    bool cursorDragFlag_ = false;
    int32_t cursorDragCount_ = 0;
    std::string continueSessionId_ = "";
    bool isCalledRightlyByCallerId_ = false;
    bool fullScreenStart_ = false;
    bool isAtomicService_ = false;
    bool isBackTransition_ = false;
    bool needClearInNotShowRecent_ = false;
    bool isPcOrPadEnableActivation_ = false;
    bool canStartAbilityFromBackground_ = false;
    bool isFoundationCall_ = false;
    int32_t requestId = 0;
    std::string specifiedFlag_ = "";
    bool disableDelegator = false;
    bool reuseDelegatorWindow = false;
    bool isAbilityHook_ = false;
    int32_t scenarios = 0;
    bool isRestartApp_ = false;
    bool isRestartInSameProcess_ = true;
    int32_t restartCallerPersistentId_ = INVALID_SESSION_ID;
    std::string label_ = "";
    StartWindowType startWindowType_ = StartWindowType::DEFAULT;
    bool isSetStartWindowType_ = false;
    SpecifiedReason specifiedReason_ = SpecifiedReason::DEFAULT;
    // only init when requestSceneSession from SCB
    bool isAncoApplication_ = false;
    bool isPrelaunch_ = false;
    int32_t frameNum_ = 0;
    bool isTargetPlugin = false;
    std::string hostBundleName = "";
    int32_t hostAppIndex = 0;
    std::string hostAppInstanceKey = "";
    std::string hostAbilityName = "";

    /*
     * Keyboard
     */
    bool isKeyboardWillShowRegistered_ { false };
    bool isKeyboardWillHideRegistered_ { false };
    bool isKeyboardDidShowRegistered_ { false };
    bool isKeyboardDidHideRegistered_ { false };

    /*
     * App Use Control
     */
    bool isUseControlSession = false; // Indicates whether the session is used for controlling a main session.
    bool hasPrivacyModeControl = false;

    /*
     * UIExtension
     */
    int32_t realParentId_ = INVALID_SESSION_ID;
    uint32_t uiExtensionUsage_ = 0;
    bool isAsyncModalBinding_ = false;
    uint32_t parentWindowType_ = 1; // WINDOW_TYPE_APP_MAIN_WINDOW
    SessionViewportConfig config_;

    /*
     * Multi Instance
     */
    bool isNewAppInstance_ = false;
    std::string appInstanceKey_;

    /*
     * PC Window
     */
    std::vector<AppExecFwk::SupportWindowMode> supportedWindowModes;
    WindowSizeLimits windowSizeLimits;
    bool isFollowParentMultiScreenPolicy = false;

    /*
     * Window Rotation
     */
    int32_t currentRotation_ = 0;

    /*
     * Compatible Mode
     */
    std::string pageConfig = "";
    std::string logicalDeviceConfig = "";

    AAFwk::Want GetWantSafely() const
    {
        std::lock_guard<std::mutex> lock(*wantMutex_);
        if (want != nullptr) {
            return *(want);
        } else {
            return AAFwk::Want();
        }
    }

    void SetWantSafely(const AAFwk::Want& newWant) const
    {
        std::lock_guard<std::mutex> lock(*wantMutex_);
        if (want == nullptr) {
            want = std::make_shared<AAFwk::Want>();
        }
        *want = newWant;
    }

    void SetWantSafely(const std::shared_ptr<AAFwk::Want>& newWant) const
    {
        std::lock_guard<std::mutex> lock(*wantMutex_);
        want = newWant;
    }

    std::shared_ptr<WindowCreateParams> windowCreateParams = nullptr;
};

struct RequestTaskInfo {
    std::unordered_map<int32_t, AAFwk::Want> requestIdToWantMap;
};

enum class SessionFlag : uint32_t {
    SESSION_FLAG_NEED_AVOID = 1,
    SESSION_FLAG_PARENT_LIMIT = 1 << 1,
    SESSION_FLAG_SHOW_WHEN_LOCKED = 1 << 2,
    SESSION_FLAG_FORBID_SPLIT_MOVE = 1 << 3,
    SESSION_FLAG_WATER_MARK = 1 << 4,
    SESSION_FLAG_END = 1 << 5,
};

enum class SizeChangeReason : uint32_t {
    UNDEFINED = 0,
    MAXIMIZE,
    RECOVER,
    ROTATION,
    DRAG,
    DRAG_START,
    DRAG_END,
    RESIZE,
    MOVE,
    HIDE,
    TRANSFORM,
    CUSTOM_ANIMATION_SHOW,
    FULL_TO_SPLIT,
    SPLIT_TO_FULL,
    FULL_TO_FLOATING,
    FLOATING_TO_FULL,
    PIP_START,
    PIP_SHOW,
    PIP_AUTO_START,
    PIP_RATIO_CHANGE,
    PIP_RESTORE,
    UPDATE_DPI_SYNC,
    DRAG_MOVE,
    AVOID_AREA_CHANGE,
    MAXIMIZE_TO_SPLIT,
    SPLIT_TO_MAXIMIZE,
    PAGE_ROTATION,
    SPLIT_DRAG_START,
    SPLIT_DRAG,
    SPLIT_DRAG_END,
    RESIZE_BY_LIMIT,
    MAXIMIZE_IN_IMPLICT = 32,
    RECOVER_IN_IMPLICIT = 33,
    OCCUPIED_AREA_CHANGE = 34,
    SCREEN_RELATIVE_POSITION_CHANGE,
    SNAPSHOT_ROTATION = 37,
    SCENE_WITH_ANIMATION,
    LS_STATE_CHANGE,
    END,
};

inline bool IsMoveToOrDragMove(SizeChangeReason reason)
{
    return reason == SizeChangeReason::MOVE || reason == SizeChangeReason::DRAG_MOVE;
}

enum class SessionEvent : uint32_t {
    EVENT_MAXIMIZE = 100,
    EVENT_RECOVER,
    EVENT_MINIMIZE,
    EVENT_CLOSE,
    EVENT_START_MOVE,
    EVENT_END_MOVE,
    EVENT_MAXIMIZE_FLOATING,
    EVENT_TERMINATE,
    EVENT_EXCEPTION,
    EVENT_SPLIT_PRIMARY,
    EVENT_SPLIT_SECONDARY,
    EVENT_DRAG_START,
    EVENT_DRAG,
    EVENT_MAXIMIZE_WITHOUT_ANIMATION,
    EVENT_MAXIMIZE_WATERFALL,
    EVENT_WATERFALL_TO_MAXIMIZE,
    EVENT_COMPATIBLE_TO_MAXIMIZE,
    EVENT_COMPATIBLE_TO_RECOVER,
    EVENT_MAXIMIZE_FULLSCREEN,
    EVENT_SWITCH_COMPATIBLE_MODE = 200,
    EVENT_NOTIFY_WINDOW_STAGE_CREATE_FINISHED,
    EVENT_END
};

enum class BrokerStates: uint32_t {
    BROKER_UNKOWN = 1,
    BROKER_STARTED = 0,
    BROKER_NOT_START = -1,
};

inline bool GreatOrEqual(double left, double right)
{
    constexpr double epsilon = -0.00001f;
    return (left - right) > epsilon;
}

inline bool LessOrEqual(double left, double right)
{
    constexpr double epsilon = 0.00001f;
    return (left - right) < epsilon;
}

inline bool NearEqual(const double left, const double right, const double epsilon)
{
    return (std::fabs(left - right) <= epsilon);
}

inline bool NearEqual(const float& left, const float& right)
{
    constexpr double epsilon = 0.001f;
    return NearEqual(left, right, epsilon);
}

inline bool NearEqual(const int32_t& left, const int32_t& right)
{
    return left == right;
}

inline bool NearEqual(const int32_t& left, const int32_t& right, const int32_t threshold)
{
    return std::abs(left - right) <= threshold;
}

inline bool NearZero(const double left)
{
    constexpr double epsilon = 0.001f;
    return NearEqual(left, 0.0, epsilon);
}

template<typename T>
struct WSRectT {
    T posX_ = 0;
    T posY_ = 0;
    T width_ = 0;
    T height_ = 0;

    bool operator==(const WSRectT<T>& a) const
    {
        return (NearEqual(posX_, a.posX_) && NearEqual(posY_, a.posY_) &&
                NearEqual(width_, a.width_) && NearEqual(height_, a.height_));
    }

    bool operator!=(const WSRectT<T>& a) const
    {
        return !this->operator==(a);
    }

    inline bool isNearEqual(const WSRectT<T>& rect, const T threshold) const
    {
        return (NearEqual(posX_, rect.posX_, threshold) && NearEqual(posY_, rect.posY_, threshold) &&
                NearEqual(width_, rect.width_, threshold) && NearEqual(height_, rect.height_, threshold));
    }

    bool IsEmpty() const
    {
        if (NearZero(posX_) && NearZero(posY_) && NearZero(width_) && NearZero(height_)) {
            return true;
        }
        return false;
    }

    inline bool IsInRegion(int32_t pointX, int32_t pointY) const
    {
        return GreatOrEqual(pointX, posX_) && LessOrEqual(pointX, posX_ + width_) &&
               GreatOrEqual(pointY, posY_) && LessOrEqual(pointY, posY_ + height_);
    }

    inline bool IsOverlap(const WSRectT<T>& rect) const
    {
        int32_t xStart = std::max(posX_, rect.posX_);
        int32_t xEnd = std::min(posX_ + static_cast<int32_t>(width_),
            rect.posX_ + static_cast<int32_t>(rect.width_));
        int32_t yStart = std::max(posY_, rect.posY_);
        int32_t yEnd = std::min(posY_ + static_cast<int32_t>(height_),
            rect.posY_ + static_cast<int32_t>(rect.height_));
        return (yStart < yEnd) && (xStart < xEnd);
    }

    inline bool IsInvalid() const
    {
        return IsEmpty() || LessOrEqual(width_, 0) || LessOrEqual(height_, 0);
    }

    /**
     * @brief Create a new rectangle offset by (dx, dy).
     *
     * @param dx The offset in the x direction.
     * @param dy The offset in the y direction.
     * @return A new WSRectT<T> instance with updated position.
     */
    WSRectT<T> WithOffset(T dx, T dy) const
    {
        return { posX_ + dx, posY_ + dy, width_, height_ };
    }

    /**
     * @brief Compute the intersection area with another rectangle.
     *
     * @tparam F The result type for intersection area calculation.
     *           Allows returning higher precision or larger integer to avoid overflow.
     *           By default, F = T.
     *
     * @param other The other rectangle to intersect with.
     * @return The intersection area as type F.
     */
    template<typename F = T>
    F IntersectionArea(const WSRectT<T>& other) const
    {
        const T left = std::max(posX_, other.posX_);
        const T top = std::max(posY_, other.posY_);
        const T right = std::min(posX_ + width_, other.posX_ + other.width_);
        const T bottom = std::min(posY_ + height_, other.posY_ + other.height_);
        const T interWidth = std::max(static_cast<T>(0), right - left);
        const T interHeight = std::max(static_cast<T>(0), bottom - top);
        return static_cast<F>(interWidth) * static_cast<F>(interHeight);
    }

    /**
     * @brief Compare if size is equal, check only width and height
     *
     * @param other The other rectangle to intersect with.
     * @return bool Whether the size is equal
     */
    bool IsSizeEqual(const WSRectT<T>& other) const
    {
        return width_ == other.width_ && height_ == other.height_;
    }

    /**
     * @brief Returns a string in the format: [posX posY width height]
     *
     * @note Optimized for performance:
     *       - Avoid std::stringstream and other formatting streams.
     *       - Pre-allocates string capacity to minimize reallocations.
     *       - Use std::to_chars for fast, allocation-free conversions.
     *
     * @return std::string A string representing the rectangle.
     */
    std::string ToString() const
    {
        std::string result;
        if constexpr (std::is_integral_v<T>) {
            result.reserve(49); // 49: 11 digits * 4 + spaces + brackets
        } else {
            result.reserve(133); // 133: 32 digits * 4 + spaces + brackets
        }

        // Helper: append either value or INF/-INF when overflow occurs
        auto appendValueOrInf = [&](char* buf, char* ptr, const std::errc ec, T value) {
            if (ec == std::errc::value_too_large) {
                result.append(value < 0 ? "-INF" : "INF");
            } else {
                result.append(buf, ptr);
            }
        };

        // Helper: convert and append value based on type
        auto appendValue = [&](T value) {
            if constexpr (std::is_integral_v<T>) {
                char buf[11]; // 11: max digits for integer
                auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
                appendValueOrInf(buf, ptr, ec, value);
            } else {
                constexpr int precision = 2;
                char buf[32]; // 32: max digits for floating number
                auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::fixed, precision);
                appendValueOrInf(buf, ptr, ec, value);
            }
        };

        result.push_back('[');
        appendValue(posX_);
        result.push_back(' ');
        appendValue(posY_);
        result.push_back(' ');
        appendValue(width_);
        result.push_back(' ');
        appendValue(height_);
        result.push_back(']');
        return result;
    }

    static const WSRectT<T> EMPTY_RECT;
};

template<typename T>
inline constexpr WSRectT<T> WSRectT<T>::EMPTY_RECT { 0, 0, 0, 0 };

using WSRect = WSRectT<int32_t>;
using WSRectF = WSRectT<float>;

/**
 * @struct WSScreenRelativeRect
 *
 * @brief Represent a window rectangle defined relative to a specific screen.
 */
struct WSScreenRelativeRect {
    // The ID of the screen this rectangle is relative to.
    ScreenId screenId;
    // The window rectangle relative to the specified screen.
    WSRect rect;

    inline std::string ToString() const
    {
        std::ostringstream oss;
        oss << screenId << ", " << rect.ToString();
        return oss.str();
    }
};

/**
 * @struct WindowDecoration
 *
 * @brief Represents the window decoration thickness (non-drawable area).
 *        In general, the top decoration includes the title bar.
 */
struct WindowDecoration {
    uint32_t left = 0;
    uint32_t top = 0;
    uint32_t right = 0;
    uint32_t bottom = 0;

    bool operator==(const WindowDecoration& other) const
    {
        return left == other.left && top == other.top && right == other.right && bottom == other.bottom;
    }

    /**
     * @brief Calculate the total horizontal decoration.
     */
    uint32_t Horizontal() const { return left + right; }

    /**
     * @brief Calculate the total vertical decoration.
     */
    uint32_t Vertical() const { return top + bottom; }

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "[" << left << ", " << top << ", " << right << ", " << bottom << "]";
        return oss.str();
    }
};

struct WindowAnimationInfo {
    WSRect beginRect { 0, 0, 0, 0 };
    WSRect endRect { 0, 0, 0, 0 };
    bool animated { false };
    uint32_t callingId { 0 };
    bool isGravityChanged { false };
};

/**
 * @brief Calling Window State
 */
enum class CallingWindowState : int32_t {
    WINDOW_IN_NORMAL = 0,
    WINDOW_IN_AI = 1
};

struct KeyboardBaseInfo {
    uint32_t callingId { 0 };
    bool isGravityChanged { false };
    bool isKeyboardShow { false };
    WSRect keyboardPanelRect { 0, 0, 0, 0 };
};

struct KeyboardAnimationRectConfig {
    WSRect beginRect { 0, 0, 0, 0 };
    WSRect endRect { 0, 0, 0, 0 };
    bool animated { false };
};

struct CallingWindowInfoData {
    CallingWindowState callingWindowState = CallingWindowState::WINDOW_IN_NORMAL;
    double scaleX = 1;
    double scaleY = 1;
};

struct WindowShadowConfig {
    float offsetX_ = 0.0f;
    float offsetY_ = 0.0f;
    float alpha_ = 0.0f;
    float radius_ = 0.0f;
    std::string color_ = "#000000";
};

struct KeyboardSceneAnimationConfig {
    std::string curveType_;
    float ctrlX1_ = 0.0f;
    float ctrlY1_ = 0.0f;
    float ctrlX2_ = 0.0f;
    float ctrlY2_ = 0.0f;
    uint32_t duration_ = 0;
};

struct WindowAnimationConfig {
    int32_t duration_ = 0;
    std::string curveType_ = "easeOut";
    float ctrlX1_ = 0.2f;
    float ctrlY1_ = 0.0f;
    float ctrlX2_ = 0.2f;
    float ctrlY2_ = 1.0f;
    float scaleX_ = 0.0f;
    float scaleY_ = 0.0f;
    float rotationX_ = 0.0f;
    float rotationY_ = 0.0f;
    float rotationZ_ = 0.0f;
    int32_t angle_ = 0;
    float translateX_ = 0.0f;
    float translateY_ = 0.0f;
    float opacity_ = 0;
};

struct StartingWindowInfo {
    uint32_t backgroundColorEarlyVersion_;
    std::string iconPathEarlyVersion_;
    bool configFileEnabled_;
    uint32_t backgroundColor_;
    std::string iconPath_;
    std::string illustrationPath_;
    std::string brandingPath_;
    std::string backgroundImagePath_;
    std::string backgroundImageFit_;
    std::string startWindowType_;
};

enum class StartWindowResType {
    AppIcon = 0,
    Illustration,
    Branding,
    BgImage,
    Count
};

struct ResourceInfo {
    std::vector<std::shared_ptr<Media::PixelMap>> pixelMaps;
    std::vector<int32_t> delayTimes;
};

struct StartingWindowPageDrawInfo {
    std::shared_ptr<ResourceInfo> appIcon = nullptr;
    std::shared_ptr<ResourceInfo> illustration = nullptr;
    std::shared_ptr<ResourceInfo> bgImage = nullptr;
    std::shared_ptr<ResourceInfo> branding = nullptr;
    uint32_t bgColor = 0;
    std::string startWindowBackgroundImageFit = "";
};

struct StartingWindowAnimationConfig {
    bool enabled_ = true;
    int duration_ = 200;
    std::string curve_ = "linear";
    float opacityStart_ = 1;
    float opacityEnd_ = 0;
};

struct SystemUIStatusBarConfig {
    bool showInLandscapeMode_ = false;
    std::string immersiveStatusBarBgColor_ = "#4c000000";
    std::string immersiveStatusBarContentColor_ = "#ffffff";
};

struct StatusBarConfig {
    bool showHide_ = false;
    std::string contentColor_;
    std::string backgroundColor_;
};

struct WindowImmersive {
    StatusBarConfig desktopStatusBarConfig_;
    StatusBarConfig leftRightStatusBarConfig_;
    StatusBarConfig upDownStatusBarConfig_;
};

struct AppWindowSceneConfig {
    float floatCornerRadius_ = 0.0f;
    std::string uiType_ = "phone";
    std::string multiWindowUIType_ = "HandsetSmartWindow";
    bool backgroundScreenLock_ = false;
    std::string rotationMode_ = "windowRotation";
    WindowShadowConfig focusedShadow_;
    WindowShadowConfig unfocusedShadow_;
    WindowShadowConfig focusedShadowDark_;
    WindowShadowConfig unfocusedShadowDark_;
    KeyboardSceneAnimationConfig keyboardAnimationIn_;
    KeyboardSceneAnimationConfig keyboardAnimationOut_;
    WindowAnimationConfig windowAnimation_;
    StartingWindowAnimationConfig startingWindowAnimationConfig_;
    SystemUIStatusBarConfig systemUIStatusBarConfig_;
    WindowImmersive windowImmersive_;
};

struct SingleHandCompatibleModeConfig {
    bool enabled = false;
    float singleHandScale = 1.0f;
    float heightChangeRatio = 1.0f;
    float widthChangeRatio = 1.0f;
};

struct SingleHandScreenInfo {
    int32_t scaleRatio = DEFAULT_SCALE_RATIO;
    int32_t scalePivotX = 0;
    int32_t scalePivotY = 0;
    SingleHandMode mode = SingleHandMode::MIDDLE;
};

struct SingleHandBackgroundTextConfig {
    int32_t posX = 0;
    int32_t posY = 0;
    int32_t width = -1;
    int32_t height = -1;
    int32_t fontSize = 0;
    int32_t minFontSize = 0;
    int32_t maxLines = -1;
    int32_t textAlign = 1;
    std::string maxFontScale = "";
};

struct SingleHandBackgroundLayoutConfig {
    bool isCustomLayout = false;
    WSRect settingButtonRect = {0, 0, 0, 0};
    bool isSettingButtonMirror = false;
    int32_t textContainerWidth = 0;
    SingleHandBackgroundTextConfig title;
    SingleHandBackgroundTextConfig content;
    SingleHandBackgroundTextConfig issueText;
};

struct DeviceScreenConfig {
    std::string rotationPolicy_ = "11"; // default use phone policy
    std::string defaultRotationPolicy_ = "1"; // default unspecified policy
    bool isRightPowerButton_ = true;
};

struct SceneAnimationConfig : public Parcelable {
    std::shared_ptr<RSTransaction> rsTransaction_ = nullptr;
    uint32_t animationDuration_ = ROTATE_ANIMATION_DURATION;
    uint32_t animationDelay_ = 0;
    WindowAnimationCurve animationCurve_ = WindowAnimationCurve::LINEAR;
    std::array<float, ANIMATION_PARAM_SIZE> animationParam_ = {0.0f, 0.0f, 0.0f, 0.0f};

    SceneAnimationConfig() = default;

    SceneAnimationConfig(
        std::shared_ptr<RSTransaction> rsTransaction,
        uint32_t animationDuration,
        uint32_t animationDelay,
        WindowAnimationCurve animationCurve,
        const std::array<float, ANIMATION_PARAM_SIZE>& animationParam)
        : rsTransaction_(std::move(rsTransaction)),
          animationDuration_(animationDuration),
          animationDelay_(animationDelay),
          animationCurve_(animationCurve),
          animationParam_(animationParam) {}

    bool Marshalling(Parcel& parcel) const override
    {
        bool hasTransaction = (rsTransaction_ != nullptr);
        if (!parcel.WriteBool(hasTransaction)) {
            return false;
        }
        if (!parcel.WriteUint32(animationDuration_)) {
            return false;
        }
        if (!parcel.WriteUint32(animationDelay_)) {
            return false;
        }
        if (!parcel.WriteUint32(static_cast<uint32_t>(animationCurve_))) {
            return false;
        }
        for (size_t i = 0; i < ANIMATION_PARAM_SIZE; ++i) {
            if (!parcel.WriteFloat(animationParam_[i])) {
                return false;
            }
        }
        return true;
    }
    
    static SceneAnimationConfig* Unmarshalling(Parcel& parcel)
    {
        auto config = new SceneAnimationConfig();
        bool hasTransaction = false;
        uint32_t animationDuration;
        uint32_t animationDelay;
        uint32_t animationCurveValue;
        std::array<float, ANIMATION_PARAM_SIZE> animationParam;
        if (!parcel.ReadBool(hasTransaction) ||
            !parcel.ReadUint32(animationDuration) ||
            !parcel.ReadUint32(animationDelay) ||
            !parcel.ReadUint32(animationCurveValue)) {
            delete config;
            return nullptr;
        }
        for (size_t i = 0; i < ANIMATION_PARAM_SIZE; ++i) {
            if (!parcel.ReadFloat(animationParam[i])) {
                delete config;
                return nullptr;
            }
        }
        config->animationDuration_ = animationDuration;
        config->animationDelay_ = animationDelay;
        config->animationCurve_ = static_cast<WindowAnimationCurve>(animationCurveValue);
        config->animationParam_ = animationParam;
        return config;
    }
};

struct RotateAnimationConfig {
    int32_t duration_ = ROTATE_ANIMATION_DURATION;
};

struct SessionEventParam {
    int32_t pointerX_ = 0;
    int32_t pointerY_ = 0;
    int32_t sessionWidth_ = 0;
    int32_t sessionHeight_ = 0;
    uint32_t dragResizeType = 0;
    uint32_t gravity = 0;
    uint32_t dragGravity = 0;
    uint32_t waterfallResidentState = 0;
    uint32_t compatibleStyleMode = 0;
    int32_t windowGlobalPosX_ = 0;
    int32_t windowGlobalPosY_ = 0;
};

struct BackgroundParams {
    int32_t persistentId = INVALID_SESSION_ID;
    bool shouldBackToCaller = true;
    AAFwk::WantParams wantParams {};
};

struct TransferSessionInfo {
    int32_t persistentId = -1;
    int32_t toScreenId = -1;
    AAFwk::WantParams wantParams {};
};

/**
 * @brief Enumerates session gravity.
 */
enum class SessionGravity : uint32_t {
    SESSION_GRAVITY_FLOAT = 0,
    SESSION_GRAVITY_BOTTOM,
    SESSION_GRAVITY_DEFAULT,
};

/**
 * @brief TerminateType session terminate type.
 */
enum class TerminateType : uint32_t {
    CLOSE_AND_KEEP_MULTITASK = 0,
    CLOSE_AND_CLEAR_MULTITASK,
    CLOSE_AND_START_CALLER,
    CLOSE_BY_EXCEPTION,
};

/**
 * @brief System animaged scene type.
 */
enum class SystemAnimatedSceneType : uint32_t {
    SCENE_ENTER_MISSION_CENTER, // Enter the mission center
    SCENE_EXIT_MISSION_CENTER, // Exit the mission center
    SCENE_ENTER_TFS_WINDOW, // Three-finger sliding window recovery
    SCENE_EXIT_TFU_WINDOW, // The three-finger up window disappears
    SCENE_ENTER_WINDOW_FULL_SCREEN, // Enter the window full screen
    SCENE_EXIT_WINDOW_FULL_SCREEN, // Exit the window full screen
    SCENE_ENTER_MAX_WINDOW, // Enter the window maximization state
    SCENE_EXIT_MAX_WINDOW, // Exit the window maximization state
    SCENE_ENTER_SPLIT_SCREEN, // Enter the split screen
    SCENE_EXIT_SPLIT_SCREEN, // Exit the split screen
    SCENE_ENTER_APP_CENTER, // Enter the app center
    SCENE_EXIT_APP_CENTER, // Exit the app center
    SCENE_APPEAR_MISSION_CENTER, // A special case scenario that displays the mission center
    SCENE_ENTER_WIND_CLEAR, // Enter win+D in clear screen mode
    SCENE_ENTER_WIND_RECOVER, // Enter win+D in recover mode
    SCENE_ENTER_RECENTS, // Enter recents
    SCENE_EXIT_RECENTS, // Exit recent.
    SCENE_LOCKSCREEN_TO_LAUNCHER, // Unlock screen.
    SCENE_ENTER_MIN_WINDOW, // Enter the window minimization state
    SCENE_RECOVER_MIN_WINDOW, // Recover minimized window
    SCENE_SNAPSHOT_ROTATION, // Snapshot rotation
    DRAG_WINDOW, // Enter scale window scene
    SCENE_OTHERS, // 1.Default state 2.The state in which the animation ends
};

/**
 * @brief Session UI parameters
 */
struct SessionUIParam {
    bool interactive_ { true };
    WSRect rect_;
    float scaleX_ { 1.0f };
    float scaleY_ { 1.0f };
    float rsScaleX_ { 1.0f };
    float rsScaleY_ { 1.0f };
    float pivotX_ { 1.0f };
    float pivotY_ { 1.0f };
    float transX_ { 0.0f }; // global translateX
    float transY_ { 0.0f }; // global translateY
    uint32_t zOrder_ { 0 };
    std::string sessionName_;
    bool needSync_ { true };
};

enum class SessionUIDirtyFlag {
    NONE = 0,
    VISIBLE = 1,
    INTERACTIVE = 1 << 1,
    RECT = 1 << 2,
    SCALE = 1 << 3,
    TOUCH_HOT_AREA = 1 << 4,
    Z_ORDER = 1 << 5,
    AVOID_AREA = 1 << 6,
    DRAG_RECT = 1 << 7,
    GLOBAL_RECT = 1 << 8,
};

enum class SessionPropertyFlag {
    NONE = 0,
    WINDOW_ID = 1,
    BUNDLE_NAME = 1 << 1,
    ABILITY_NAME = 1 << 2,
    APP_INDEX = 1 << 3,
    VISIBILITY_STATE = 1 << 4,
    DISPLAY_ID = 1 << 5,
    WINDOW_RECT = 1 << 6,
    WINDOW_MODE = 1 << 7,
    FLOATING_SCALE = 1 << 8,
    MID_SCENE = 1 << 9,
    WINDOW_GLOBAL_RECT = 1 << 10,
};

/**
 * @brief State for post-process focus
 */
struct PostProcessFocusState {
    bool enabled_ { false };
    bool isFocused_ { false };
    bool byForeground_ { true };
    FocusChangeReason reason_ { FocusChangeReason::DEFAULT };

    void Reset()
    {
        enabled_ = false;
        isFocused_ = false;
        byForeground_ = true;
        reason_ = FocusChangeReason::DEFAULT;
    }
};

/**
 * @brief WindowNode for snapshot
 */
enum class SnapshotNodeType : uint32_t {
    DEFAULT_NODE = 0,
    LEASH_NODE,
    APP_NODE,
};

enum class SnapShotRecoverType : uint32_t {
    ROTATE = 0,
    EXIT_SPLIT_ON_BACKGROUND = 1,
};

enum class LifeCycleChangeReason {
    DEFAULT = 0,

    /*
     * Expanded to folded on single pocket
     */
    EXPAND_TO_FOLD_SINGLE_POCKET,

    BACK_TO_DESKTOP,

    SCREEN_LOCK,

    LAST_SCENE_TRANSFER,

    /*
     * Drive batch of windows go background quickly
     */
    QUICK_BATCH_BACKGROUND,

    SCREEN_ROTATION,

    REASON_END,
};

enum class ParentLifeCycleEvent : uint32_t {
    FOREGROUND = 1,
    ACTIVE,
    INACTIVE,
    BACKGROUND,
    RESUMED,
    PAUSED,
    DESTROYED,
};

enum class AsyncTraceTaskId: int32_t {
    THROW_SLIP_ANIMATION = 0,
};

/**
 * @brief Recover state
 */
enum class RecoverState : uint32_t {
    RECOVER_INITIAL = 0,
    RECOVER_ENABLE_INPUT,
    RECOVER_END,
};

/**
 * @brief Client window recover state
 */
enum class WindowRecoverState : uint32_t {
    WINDOW_NOT_RECONNECT = 0,
    WINDOW_START_RECONNECT,
    WINDOW_DOING_RECONNECT,
    WINDOW_FINISH_RECONNECT,
    WINDOW_RECOVER_STATE_END,
};

/**
 * @brief Server session recover state
 */
enum class SessionRecoverState : uint32_t {
    SESSION_NOT_RECONNECT = 0,
    SESSION_START_RECONNECT,
    SESSION_DOING_RECONNECT,
    SESSION_FINISH_RECONNECT,
    SESSION_RECOVER_STATE_END,
};

/**
 * @brief Set specific window zIndex reason
 */
enum class SetSpecificZIndexReason : uint32_t {
    SET = 0,
    RESET = 1,
};

enum class CrossPlaneState : uint32_t {
    UNDEFINED = 0,
    CROSS_DEFAULT_PLANE,
    CROSS_DEFAULT_CREASE_PLANE,
    CROSS_CREASE_PLANE,
    CROSS_VIRTUAL_CREASE_PLANE,
    CROSS_VIRTUAL_PLANE,
    CROSS_ALL_PLANE,
};

/**
 * @brief Sidebar blur type
 */
enum class SidebarBlurType : uint32_t {
    NONE = 0,
    INITIAL,
    DEFAULT_FLOAT,
    DEFAULT_MAXIMIZE,
    END,
};
} // namespace OHOS::Rosen
#endif // OHOS_ROSEN_WINDOW_SCENE_WS_COMMON_H
