/*
 * Copyright (c) 2021-2023 Huawei Device Co., Ltd.
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

#ifndef OHOS_ROSEN_WM_COMMON_H
#define OHOS_ROSEN_WM_COMMON_H

#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include <float.h>

#include <parcel.h>

#include "dm_common.h"
#include "securec.h"
#include "wm_animation_common.h"
#include "wm_math.h"
#include "wm_type.h"

namespace OHOS {
namespace Rosen {
namespace {
constexpr uint32_t DEFAULT_SPACING_BETWEEN_BUTTONS = 12;
constexpr uint32_t DEFAULT_BUTTON_BACKGROUND_SIZE = 28;
constexpr uint32_t DEFAULT_CLOSE_BUTTON_RIGHT_MARGIN = 20;
constexpr uint32_t DEFAULT_BUTTON_ICON_SIZE = 20;
constexpr uint32_t DEFAULT_BUTTON_BACKGROUND_CORNER_RADIUS = 4;
constexpr int32_t DEFAULT_COLOR_MODE = -1;
constexpr int32_t MIN_COLOR_MODE = -1;
constexpr int32_t MAX_COLOR_MODE = 1;
constexpr int32_t LIGHT_COLOR_MODE = 0;
constexpr int32_t DARK_COLOR_MODE = 1;
constexpr int32_t ANCO_SERVICE_BROKER_UID = 5557;
constexpr uint32_t MIN_SPACING_BETWEEN_BUTTONS = 8;
constexpr uint32_t MAX_SPACING_BETWEEN_BUTTONS = 24;
constexpr uint32_t MIN_BUTTON_BACKGROUND_SIZE = 20;
constexpr uint32_t MAX_BUTTON_BACKGROUND_SIZE = 40;
constexpr uint32_t MIN_CLOSE_BUTTON_RIGHT_MARGIN = 6;
constexpr uint32_t MAX_CLOSE_BUTTON_RIGHT_MARGIN = 22;
constexpr uint32_t MIN_BUTTON_ICON_SIZE = 16;
constexpr uint32_t MAX_BUTTON_ICON_SIZE = 24;
constexpr uint32_t MIN_BUTTON_BACKGROUND_CORNER_RADIUS = 4;
constexpr uint32_t MAX_BUTTON_BACKGROUND_CORNER_RADIUS = 8;
constexpr int32_t API_VERSION_INVALID = -1;
constexpr uint32_t MAX_SIZE_PIP_CONTROL_GROUP = 8;
constexpr uint32_t MAX_SIZE_PIP_CONTROL = 9;
constexpr int32_t SPECIFIC_ZINDEX_INVALID = -1;
constexpr double POS_ZERO = 0.001f;
constexpr uint32_t SUPPORT_ROTATION_SIZE = 4;
/*
 * PC Window Sidebar Blur
 */
constexpr float SIDEBAR_BLUR_NUMBER_ZERO = 0.0f;
constexpr float SIDEBAR_DEFAULT_RADIUS_LIGHT = 57.0f;
constexpr float SIDEBAR_DEFAULT_RADIUS_DARK = 57.0f;
constexpr float SIDEBAR_DEFAULT_SATURATION_LIGHT = 2.0f;
constexpr float SIDEBAR_DEFAULT_SATURATION_DARK = 2.6f;
constexpr float SIDEBAR_DEFAULT_BRIGHTNESS_LIGHT = 1.0f;
constexpr float SIDEBAR_DEFAULT_BRIGHTNESS_DARK = 0.4f;
constexpr uint32_t SIDEBAR_DEFAULT_MASKCOLOR_LIGHT = 0xdbf1f1f1;
constexpr uint32_t SIDEBAR_DEFAULT_MASKCOLOR_DARK = 0xe61a1a1a;
constexpr float SIDEBAR_MAXIMIZE_RADIUS_LIGHT = 57.0f;
constexpr float SIDEBAR_MAXIMIZE_RADIUS_DARK = 57.0f;
constexpr float SIDEBAR_MAXIMIZE_SATURATION_LIGHT = 2.0f;
constexpr float SIDEBAR_MAXIMIZE_SATURATION_DARK = 1.0f;
constexpr float SIDEBAR_MAXIMIZE_BRIGHTNESS_LIGHT = 1.0f;
constexpr float SIDEBAR_MAXIMIZE_BRIGHTNESS_DARK = 0.9f;
constexpr uint32_t SIDEBAR_MAXIMIZE_MASKCOLOR_LIGHT = 0xf2f1f1f1;
constexpr uint32_t SIDEBAR_MAXIMIZE_MASKCOLOR_DARK = 0xf21a1a1a;
constexpr uint32_t SIDEBAR_SNAPSHOT_MASKCOLOR_LIGHT = 0xffe5e5e5;
constexpr uint32_t SIDEBAR_SNAPSHOT_MASKCOLOR_DARK = 0xff414141;

/*
 * Compatible Mode
 */
constexpr float COMPACT_SIMULATION_SCALE_DPI = 3.25f;
constexpr float COMPACT_NORMAL_SCALE = 1.0f;

/*
 * Sub Window
 */
constexpr uint32_t MAX_SUB_WINDOW_LEVEL = 10;

/*
 * Window outline
 */
constexpr uint32_t OUTLINE_COLOR_DEFAULT = 0xff64bb5c; // Default color, ARGB format.
constexpr uint32_t OUTLINE_WIDTH_MIN = 1; // vp
constexpr uint32_t OUTLINE_WIDTH_DEFAULT = 4; // vp
constexpr uint32_t OUTLINE_WIDTH_MAX = 8; // vp
constexpr uint32_t OUTLINE_FOR_WINDOW_MAX_NUM = 256; // Up to 256 windows can show simultaneously.
constexpr uint32_t OUTLINE_COLOR_OPAQUE_OFFSET = 24; // Shift right 24 bits.
constexpr uint32_t OUTLINE_COLOR_OPAQUE = 0xff; // Color opaque byte.
}

/**
 * @brief Enumerates type of window.
 */
enum class WindowType : uint32_t {
    APP_WINDOW_BASE = 1,
    APP_MAIN_WINDOW_BASE = APP_WINDOW_BASE,
    WINDOW_TYPE_APP_MAIN_WINDOW = APP_MAIN_WINDOW_BASE,
    APP_MAIN_WINDOW_END,

    APP_SUB_WINDOW_BASE = 1000,
    WINDOW_TYPE_MEDIA = APP_SUB_WINDOW_BASE,
    WINDOW_TYPE_APP_SUB_WINDOW,
    WINDOW_TYPE_APP_COMPONENT,
    APP_SUB_WINDOW_END,
    APP_WINDOW_END = APP_SUB_WINDOW_END,

    SYSTEM_WINDOW_BASE = 2000,
    BELOW_APP_SYSTEM_WINDOW_BASE = SYSTEM_WINDOW_BASE,
    WINDOW_TYPE_WALLPAPER = SYSTEM_WINDOW_BASE,
    WINDOW_TYPE_DESKTOP,
    BELOW_APP_SYSTEM_WINDOW_END,

    ABOVE_APP_SYSTEM_WINDOW_BASE = 2100,
    WINDOW_TYPE_APP_LAUNCHING = ABOVE_APP_SYSTEM_WINDOW_BASE,
    WINDOW_TYPE_DOCK_SLICE,
    WINDOW_TYPE_INCOMING_CALL,
    WINDOW_TYPE_SEARCHING_BAR,
    WINDOW_TYPE_SYSTEM_ALARM_WINDOW,
    WINDOW_TYPE_INPUT_METHOD_FLOAT,
    WINDOW_TYPE_FLOAT,
    WINDOW_TYPE_TOAST,
    WINDOW_TYPE_STATUS_BAR,
    WINDOW_TYPE_PANEL,
    WINDOW_TYPE_KEYGUARD,
    WINDOW_TYPE_VOLUME_OVERLAY,
    WINDOW_TYPE_NAVIGATION_BAR,
    WINDOW_TYPE_DRAGGING_EFFECT,
    WINDOW_TYPE_POINTER,
    WINDOW_TYPE_LAUNCHER_RECENT,
    WINDOW_TYPE_LAUNCHER_DOCK,
    WINDOW_TYPE_BOOT_ANIMATION,
    WINDOW_TYPE_FREEZE_DISPLAY,
    WINDOW_TYPE_VOICE_INTERACTION,
    WINDOW_TYPE_FLOAT_CAMERA,
    WINDOW_TYPE_PLACEHOLDER,
    WINDOW_TYPE_DIALOG,
    WINDOW_TYPE_SCREENSHOT,
    WINDOW_TYPE_INPUT_METHOD_STATUS_BAR,
    WINDOW_TYPE_GLOBAL_SEARCH,
    WINDOW_TYPE_NEGATIVE_SCREEN,
    WINDOW_TYPE_SYSTEM_TOAST,
    WINDOW_TYPE_SYSTEM_FLOAT,
    WINDOW_TYPE_PIP,
    WINDOW_TYPE_THEME_EDITOR,
    WINDOW_TYPE_NAVIGATION_INDICATOR,
    WINDOW_TYPE_HANDWRITE,
    WINDOW_TYPE_SCENE_BOARD,
    WINDOW_TYPE_KEYBOARD_PANEL,
    WINDOW_TYPE_SCB_DEFAULT,
    WINDOW_TYPE_TRANSPARENT_VIEW,
    WINDOW_TYPE_WALLET_SWIPE_CARD,
    WINDOW_TYPE_SCREEN_CONTROL,
    WINDOW_TYPE_FLOAT_NAVIGATION,
    WINDOW_TYPE_MUTISCREEN_COLLABORATION,
    WINDOW_TYPE_DYNAMIC,
    WINDOW_TYPE_MAGNIFICATION,
    WINDOW_TYPE_MAGNIFICATION_MENU,
    WINDOW_TYPE_SELECTION,
    WINDOW_TYPE_FB,
    ABOVE_APP_SYSTEM_WINDOW_END,

    SYSTEM_SUB_WINDOW_BASE = 2500,
    WINDOW_TYPE_SYSTEM_SUB_WINDOW = SYSTEM_SUB_WINDOW_BASE,
    WINDOW_TYPE_SCB_SUB_WINDOW,
    SYSTEM_SUB_WINDOW_END,

    SYSTEM_WINDOW_END = SYSTEM_SUB_WINDOW_END,

    WINDOW_TYPE_UI_EXTENSION = 3000
};
 
/**
 * @struct RealTimeSwitchInfo.
 *
 * @brief the information for compatible mode App when switching window style in real time.
 */
struct RealTimeSwitchInfo {
    bool isNeedChange_{false};
    uint32_t showTypes_{0};
};

/**
 * @brief Enumerates mode of window.
 */
enum class WindowMode : uint32_t {
    WINDOW_MODE_UNDEFINED = 0,
    WINDOW_MODE_FULLSCREEN = 1,
    WINDOW_MODE_SPLIT_PRIMARY = 100,
    WINDOW_MODE_SPLIT_SECONDARY,
    WINDOW_MODE_FLOATING,
    WINDOW_MODE_PIP,
    WINDOW_MODE_FB,
    END = WINDOW_MODE_FB,
};

/**
 * @brief Enumerates modeType of window.
 */
enum class WindowModeType : uint8_t {
    WINDOW_MODE_SPLIT_FLOATING = 0,
    WINDOW_MODE_SPLIT = 1,
    WINDOW_MODE_FLOATING = 2,
    WINDOW_MODE_FULLSCREEN = 3,
    WINDOW_MODE_FULLSCREEN_FLOATING = 4,
    WINDOW_MODE_OTHER = 5
};

/**
 * @brief Enumerates global mode of window.
 */
enum class GlobalWindowMode : uint32_t {
    UNKNOWN = 0,
    FULLSCREEN = 1,
    SPLIT = 1 << 1,
    FLOAT = 1 << 2,
    PIP = 1 << 3,
    ALL = FULLSCREEN | SPLIT | FLOAT | PIP
};

inline GlobalWindowMode operator|(GlobalWindowMode lhs, GlobalWindowMode rhs)
{
    return static_cast<GlobalWindowMode>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

/**
 * @brief Enumerates modal of sub session.
 */
enum class SubWindowModalType : uint32_t {
    BEGIN = 0,
    TYPE_UNDEFINED = BEGIN,
    TYPE_NORMAL,
    TYPE_DIALOG,
    TYPE_WINDOW_MODALITY,
    TYPE_TOAST,
    TYPE_APPLICATION_MODALITY,
    TYPE_TEXT_MENU,
    END = TYPE_TEXT_MENU,
};

/**
 * @brief Enumerates mode supported of window.
 */
enum WindowModeSupport : uint32_t {
    WINDOW_MODE_SUPPORT_FULLSCREEN = 1 << 0,
    WINDOW_MODE_SUPPORT_FLOATING = 1 << 1,
    WINDOW_MODE_SUPPORT_SPLIT_PRIMARY = 1 << 2,
    WINDOW_MODE_SUPPORT_SPLIT_SECONDARY = 1 << 3,
    WINDOW_MODE_SUPPORT_PIP = 1 << 4,
    WINDOW_MODE_SUPPORT_FB = 1 << 5,
    WINDOW_MODE_SUPPORT_ALL = WINDOW_MODE_SUPPORT_FULLSCREEN |
                              WINDOW_MODE_SUPPORT_SPLIT_PRIMARY |
                              WINDOW_MODE_SUPPORT_SPLIT_SECONDARY |
                              WINDOW_MODE_SUPPORT_FLOATING |
                              WINDOW_MODE_SUPPORT_PIP |
                              WINDOW_MODE_SUPPORT_FB
};

/**
 * @brief Enumerates blur style of window.
 */
enum class WindowBlurStyle : uint32_t {
    WINDOW_BLUR_OFF = 0,
    WINDOW_BLUR_THIN,
    WINDOW_BLUR_REGULAR,
    WINDOW_BLUR_THICK
};

/**
 * @brief Enumerates cross axis state of window.
 */
enum class CrossAxisState : uint32_t {
    STATE_INVALID = 0,
    STATE_CROSS,
    STATE_NO_CROSS,
    STATE_END,
};

/**
 * @brief Enumerates state of window.
 */
enum class WindowState : uint32_t {
    STATE_INITIAL,
    STATE_CREATED,
    STATE_SHOWN,
    STATE_HIDDEN,
    STATE_FROZEN,
    STATE_UNFROZEN,
    STATE_DESTROYED,
    STATE_BOTTOM = STATE_DESTROYED, // Add state type after STATE_DESTROYED is not allowed
};

/**
 * @brief Enumerates error code of window.
 */
enum class WMError : int32_t {
    WM_OK = 0,
    WM_DO_NOTHING,
    WM_ERROR_NO_MEM,
    WM_ERROR_DESTROYED_OBJECT,
    WM_ERROR_INVALID_WINDOW,
    WM_ERROR_INVALID_WINDOW_MODE_OR_SIZE,
    WM_ERROR_INVALID_OPERATION,
    WM_ERROR_INVALID_PERMISSION,
    WM_ERROR_NOT_SYSTEM_APP,
    WM_ERROR_NO_REMOTE_ANIMATION,
    WM_ERROR_INVALID_DISPLAY,
    WM_ERROR_INVALID_PARENT,
    WM_ERROR_INVALID_OP_IN_CUR_STATUS,
    WM_ERROR_REPEAT_OPERATION,
    WM_ERROR_INVALID_SESSION,
    WM_ERROR_INVALID_CALLING,
    WM_ERROR_SYSTEM_ABNORMALLY,

    WM_ERROR_DEVICE_NOT_SUPPORT = 801, // the value do not change.It is defined on all system

    WM_ERROR_NEED_REPORT_BASE = 1000, // error code > 1000 means need report
    WM_ERROR_NULLPTR,
    WM_ERROR_INVALID_TYPE,
    WM_ERROR_INVALID_PARAM,
    WM_ERROR_SAMGR,
    WM_ERROR_IPC_FAILED,
    WM_ERROR_NEED_REPORT_END,
    WM_ERROR_START_ABILITY_FAILED,
    WM_ERROR_PIP_DESTROY_FAILED,
    WM_ERROR_PIP_STATE_ABNORMALLY,
    WM_ERROR_PIP_CREATE_FAILED,
    WM_ERROR_PIP_INTERNAL_ERROR,
    WM_ERROR_PIP_REPEAT_OPERATION,
    WM_ERROR_ILLEGAL_PARAM,
    WM_ERROR_UI_EFFECT_ERROR,
    WM_ERROR_TIMEOUT,
    WM_ERROR_FB_PARAM_INVALID,
    WM_ERROR_FB_CREATE_FAILED,
    WM_ERROR_FB_REPEAT_CONTROLLER,
    WM_ERROR_FB_REPEAT_OPERATION,
    WM_ERROR_FB_INTERNAL_ERROR,
    WM_ERROR_FB_STATE_ABNORMALLY,
    WM_ERROR_FB_INVALID_STATE,
    WM_ERROR_FB_RESTORE_MAIN_WINDOW_FAILED,
    WM_ERROR_FB_UPDATE_TEMPLATE_TYPE_DENIED,
    WM_ERROR_FB_UPDATE_STATIC_TEMPLATE_DENIED,
    WM_ERROR_INVALID_WINDOW_TYPE,
    WM_ERROR_FORBID_SUBWINDOW,
};

/**
 * @brief Enumerates error code of window only used for js api.
 */
enum class WmErrorCode : int32_t {
    WM_OK = 0,
    WM_ERROR_NO_PERMISSION = 201,
    WM_ERROR_NOT_SYSTEM_APP = 202,
    WM_ERROR_INVALID_PARAM = 401,
    WM_ERROR_DEVICE_NOT_SUPPORT = 801,
    WM_ERROR_REPEAT_OPERATION = 1300001,
    WM_ERROR_STATE_ABNORMALLY = 1300002,
    WM_ERROR_SYSTEM_ABNORMALLY = 1300003,
    WM_ERROR_INVALID_CALLING = 1300004,
    WM_ERROR_STAGE_ABNORMALLY = 1300005,
    WM_ERROR_CONTEXT_ABNORMALLY = 1300006,
    WM_ERROR_START_ABILITY_FAILED = 1300007,
    WM_ERROR_INVALID_DISPLAY = 1300008,
    WM_ERROR_INVALID_PARENT = 1300009,
    WM_ERROR_INVALID_OP_IN_CUR_STATUS = 1300010,
    WM_ERROR_PIP_DESTROY_FAILED = 1300011,
    WM_ERROR_PIP_STATE_ABNORMALLY = 1300012,
    WM_ERROR_PIP_CREATE_FAILED = 1300013,
    WM_ERROR_PIP_INTERNAL_ERROR = 1300014,
    WM_ERROR_PIP_REPEAT_OPERATION = 1300015,
    WM_ERROR_ILLEGAL_PARAM = 1300016,
    WM_ERROR_UI_EFFECT_ERROR = 1300017,
    WM_ERROR_TIMEOUT = 1300018,
    WM_ERROR_FB_PARAM_INVALID = 1300019,
    WM_ERROR_FB_CREATE_FAILED = 1300020,
    WM_ERROR_FB_REPEAT_CONTROLLER = 1300021,
    WM_ERROR_FB_REPEAT_OPERATION = 1300022,
    WM_ERROR_FB_INTERNAL_ERROR = 1300023,
    WM_ERROR_FB_STATE_ABNORMALLY = 1300024,
    WM_ERROR_FB_INVALID_STATE = 1300025,
    WM_ERROR_FB_RESTORE_MAIN_WINDOW_FAILED = 1300026,
    WM_ERROR_FB_UPDATE_TEMPLATE_TYPE_DENIED = 1300027,
    WM_ERROR_FB_UPDATE_STATIC_TEMPLATE_DENIED = 1300028,
    WM_ERROR_INVALID_WINDOW_TYPE = 1300029,
    WM_ERROR_FORBID_SUBWINDOW = 1300030,
};

/**
 * @brief Enumerates status of window.
 */
enum class WindowStatus : uint32_t {
    WINDOW_STATUS_UNDEFINED = 0,
    WINDOW_STATUS_FULLSCREEN = 1,
    WINDOW_STATUS_MAXIMIZE,
    WINDOW_STATUS_MINIMIZE,
    WINDOW_STATUS_FLOATING,
    WINDOW_STATUS_SPLITSCREEN
};

/**
 * @brief Enumerates setting flag of systemStatusBar
 */
enum class SystemBarSettingFlag : uint32_t {
    DEFAULT_SETTING = 0,
    COLOR_SETTING = 1,
    ENABLE_SETTING = 1 << 1,
    ALL_SETTING = COLOR_SETTING | ENABLE_SETTING,
    FOLLOW_SETTING = 1 << 2
};

inline SystemBarSettingFlag operator|(SystemBarSettingFlag lhs, SystemBarSettingFlag rhs)
{
    using T = std::underlying_type_t<SystemBarSettingFlag>;
    return static_cast<SystemBarSettingFlag>(static_cast<T>(lhs) | static_cast<T>(rhs));
}

inline SystemBarSettingFlag& operator|=
    (SystemBarSettingFlag& lhs, SystemBarSettingFlag rhs) { return lhs = lhs | rhs; }

/**
 * @brief Enumerates flag of ControlAppType.
 */
enum class ControlAppType : uint8_t {
    CONTROL_APP_TYPE_BEGIN = 0,
    APP_LOCK = 1,
    PARENT_CONTROL,
    DLP,
    PRIVACY_WINDOW,
    CONTROL_APP_TYPE_END,
};

/**
 * @brief Enumerates flag of multiWindowUIType.
 */
enum class WindowUIType : uint8_t {
    PHONE_WINDOW = 0,
    PC_WINDOW,
    PAD_WINDOW,
    INVALID_WINDOW
};

/**
 * @brief Used to map from WMError to WmErrorCode.
 */
extern const std::map<WMError, WmErrorCode> WM_JS_TO_ERROR_CODE_MAP;

/**
 * @brief Convert WMError to corresponding WmErrorCode.
 *
 * @param error WMError value to convert.
 * @param defaultCode Value to return if mapping is not found.
 * @return Corresponding WmErrorCode or defaultCode if unmapped.
 */
WmErrorCode ConvertErrorToCode(WMError error, WmErrorCode defaultCode = WmErrorCode::WM_ERROR_STATE_ABNORMALLY);

/**
 * @brief Enumerates flag of window.
 */
enum class WindowFlag : uint32_t {
    WINDOW_FLAG_NEED_AVOID = 1,
    WINDOW_FLAG_PARENT_LIMIT = 1 << 1,
    WINDOW_FLAG_SHOW_WHEN_LOCKED = 1 << 2,
    WINDOW_FLAG_FORBID_SPLIT_MOVE = 1 << 3,
    WINDOW_FLAG_WATER_MARK = 1 << 4,
    WINDOW_FLAG_IS_MODAL = 1 << 5,
    WINDOW_FLAG_HANDWRITING = 1 << 6,
    WINDOW_FLAG_IS_TOAST = 1 << 7,
    WINDOW_FLAG_IS_APPLICATION_MODAL = 1 << 8,
    WINDOW_FLAG_IS_TEXT_MENU = 1 << 9,
    WINDOW_FLAG_END = 1 << 10,
};

/**
 * @brief Enumerates system and app sub window avoid area options
 */
enum class AvoidAreaOption : uint32_t {
    ENABLE_SYSTEM_WINDOW = 1,
    ENABLE_APP_SUB_WINDOW = 1 << 1,
};

/**
 * @brief Flag of uiextension window.
 */
union ExtensionWindowFlags {
    uint32_t bitData;
    struct {
        // Each flag should be false default, true when active
        bool hideNonSecureWindowsFlag : 1;
        bool waterMarkFlag : 1;
        bool privacyModeFlag : 1;
    };
    ExtensionWindowFlags() : bitData(0) {}
    ExtensionWindowFlags(uint32_t bits) : bitData(bits) {}
    ~ExtensionWindowFlags() {}
    void SetAllActive()
    {
        hideNonSecureWindowsFlag = true;
        waterMarkFlag = true;
        privacyModeFlag = true;
    }
};

/**
 * @brief Enumerates window size change reason.
 */
enum class WindowSizeChangeReason : uint32_t {
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
    ROOT_SCENE_CHANGE,
    SNAPSHOT_ROTATION = 37,
    SCENE_WITH_ANIMATION,
    LS_STATE_CHANGE,
    FULL_SCREEN_IN_FORCE_SPLIT,
    HOOK_INFO_CHANGE,
    END,
};

inline bool IsMoveToOrDragMove(WindowSizeChangeReason reason)
{
    return reason == WindowSizeChangeReason::MOVE || reason == WindowSizeChangeReason::DRAG_MOVE;
}

/**
 * @brief Enumerates layout mode of window.
 */
enum class WindowLayoutMode : uint32_t {
    BASE = 0,
    CASCADE = BASE,
    TILE = 1,
    END,
};

/**
 * @brief Enumerates drag event.
 */
enum class DragEvent : uint32_t {
    DRAG_EVENT_IN  = 1,
    DRAG_EVENT_OUT,
    DRAG_EVENT_MOVE,
    DRAG_EVENT_END,
};

/**
 * @brief Enumerates drag resize type.
 */
enum class DragResizeType : uint32_t {
    RESIZE_TYPE_UNDEFINED = 0,
    RESIZE_EACH_FRAME = 1,
    RESIZE_WHEN_DRAG_END = 2,
    RESIZE_KEY_FRAME = 3,
    RESIZE_SCALE = 4,
    RESIZE_MAX_VALUE,  // invalid value begin, add new value above
};

/**
 * @struct KeyFramePolicy
 *
 * @brief info for drag key frame policy.
 */
struct KeyFramePolicy : public Parcelable {
    DragResizeType dragResizeType_ = DragResizeType::RESIZE_TYPE_UNDEFINED;
    uint32_t interval_ = 1000;
    uint32_t distance_ = 1000;
    uint32_t animationDuration_ = 100;
    uint32_t animationDelay_ = 100;
    bool running_ = false;
    bool stopping_ = false;

    bool operator==(const KeyFramePolicy& right) const
    {
        return dragResizeType_ == right.dragResizeType_ &&
            interval_ == right.interval_ &&
            distance_ == right.distance_ &&
            animationDuration_ == right.animationDuration_ &&
            animationDelay_ == right.animationDelay_ &&
            running_ == right.running_ &&
            stopping_ == right.stopping_;
    }

    bool operator!=(const KeyFramePolicy& right) const
    {
        return !(*this == right);
    }

    bool enabled() const
    {
        return dragResizeType_ == DragResizeType::RESIZE_KEY_FRAME;
    }

    bool Marshalling(Parcel& parcel) const override
    {
        if (interval_ == 0) {
            return false;
        }
        return parcel.WriteUint32(static_cast<uint32_t>(dragResizeType_)) &&
            parcel.WriteUint32(interval_) && parcel.WriteUint32(distance_) &&
            parcel.WriteUint32(animationDuration_) && parcel.WriteUint32(animationDelay_) &&
            parcel.WriteBool(running_) && parcel.WriteBool(stopping_);
    }

    static KeyFramePolicy* Unmarshalling(Parcel& parcel)
    {
        KeyFramePolicy* keyFramePolicy = new KeyFramePolicy();
        uint32_t dragResizeType;
        if (!parcel.ReadUint32(dragResizeType) || !parcel.ReadUint32(keyFramePolicy->interval_) ||
            !parcel.ReadUint32(keyFramePolicy->distance_) || !parcel.ReadUint32(keyFramePolicy->animationDuration_) ||
            !parcel.ReadUint32(keyFramePolicy->animationDelay_) || !parcel.ReadBool(keyFramePolicy->running_) ||
            !parcel.ReadBool(keyFramePolicy->stopping_)) {
            delete keyFramePolicy;
            return nullptr;
        }
        if (dragResizeType >= static_cast<uint32_t>(DragResizeType::RESIZE_MAX_VALUE) ||
            keyFramePolicy->interval_ == 0) {
            delete keyFramePolicy;
            return nullptr;
        }
        keyFramePolicy->dragResizeType_ = static_cast<DragResizeType>(dragResizeType);
        return keyFramePolicy;
    }

    inline std::string ToString() const
    {
        std::ostringstream oss;
        oss << "[" << static_cast<uint32_t>(dragResizeType_) << " " << interval_ << " " << distance_;
        oss << " " << animationDuration_ << " " << animationDelay_;
        oss << " " << running_ << " " << stopping_ << "]";
        return oss.str();
    }
};

/**
 * @struct HookWindowInfo.
 *
 * @brief Configures window hook behavior based on window size ratios.
 */
struct HookWindowInfo : public Parcelable {
    bool enableHookWindow{ false };
    float widthHookRatio{ 1.0f };
    bool notifyWindowChange{ false };

    static constexpr float DEFAULT_WINDOW_SIZE_HOOK_RATIO = 1.0f;

    bool Marshalling(Parcel& parcel) const override
    {
        return WriteAllFields(parcel);
    }

    static HookWindowInfo* Unmarshalling(Parcel& parcel)
    {
        auto hookWindowInfo = std::make_unique<HookWindowInfo>();
        if (!hookWindowInfo || !ReadAllFields(parcel, *hookWindowInfo)) {
            return nullptr;
        }
        return hookWindowInfo.release();
    }

    std::string ToString() const
    {
        constexpr int precision = 6; // Print float with precision of 6 decimal places.
        std::ostringstream oss;
        oss << std::boolalpha  // For true/false instead of 1/0
            << "enableHookWindow: " << enableHookWindow
            << ", widthHookRatio: " << std::fixed << std::setprecision(precision) << widthHookRatio
            << ", notifyWindowChange: " << notifyWindowChange;
        return oss.str();
    }

private:
    bool WriteAllFields(Parcel& parcel) const
    {
        return parcel.WriteBool(enableHookWindow) &&
               parcel.WriteFloat(widthHookRatio) &&
               parcel.WriteBool(notifyWindowChange);
    }

    static bool ReadAllFields(Parcel& parcel, HookWindowInfo& info)
    {
        return parcel.ReadBool(info.enableHookWindow) &&
               parcel.ReadFloat(info.widthHookRatio) &&
               parcel.ReadBool(info.notifyWindowChange);
    }
};

/**
 * @brief Enumerates window tag.
 */
enum class WindowTag : uint32_t {
    MAIN_WINDOW = 0,
    SUB_WINDOW = 1,
    SYSTEM_WINDOW = 2,
};

/**
 * @brief Enumerates window session type.
 */
enum class WindowSessionType : uint32_t {
    SCENE_SESSION = 0,
    EXTENSION_SESSION = 1,
};

/**
 * @brief Enumerates window gravity.
 */
enum class WindowGravity : uint32_t {
    WINDOW_GRAVITY_FLOAT = 0,
    WINDOW_GRAVITY_BOTTOM,
    WINDOW_GRAVITY_DEFAULT,
};

/**
 * @brief Enumerates window setuicontent type.
 */
enum class WindowSetUIContentType : uint32_t {
    DEFAULT,
    RESTORE,
    BY_NAME,
    BY_ABC,
    BY_SHARED,
};

/**
 * @brief Enumerates restore type.
 */
enum class BackupAndRestoreType : int32_t {
    NONE = 0,                       // no backup and restore
    CONTINUATION = 1,               // distribute
    APP_RECOVERY = 2,               // app recovery
    RESOURCESCHEDULE_RECOVERY = 3,  // app is killed due to resource schedule
};

/**
 * @brief Enumerates window Style type.
 */
enum class WindowStyleType : uint8_t {
    WINDOW_STYLE_DEFAULT = 0,
    WINDOW_STYLE_FREE_MULTI_WINDOW = 1,
};

/**
 * @brief Enumerates window style status
 */
enum class WindowManagementMode : uint32_t {
    UNDEFINED,
    FREEFORM,
    FULLSCREEN
};

/**
 * @brief Disable Gesture Back Type
 */
enum class GestureBackType : uint8_t {
    GESTURE_SIDE = 0,
    GESTURE_SWIPE_UP = 1,
    GESTURE_ALL = 2,
};

/**
 * @struct PointInfo.
 *
 * @brief point Info.
 */
struct PointInfo {
    int32_t x;
    int32_t y;
};

/**
 * @struct MainWindowInfo.
 *
 * @brief topN main window info.
 */
struct MainWindowInfo : public Parcelable {
    virtual bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteInt32(pid_) && parcel.WriteString(bundleName_) &&
            parcel.WriteInt32(persistentId_) && parcel.WriteInt32(bundleType_) &&
            parcel.WriteUint64(displayId_) && parcel.WriteBool(showing_) && parcel.WriteString(label_);
    }

    static MainWindowInfo* Unmarshalling(Parcel& parcel)
    {
        MainWindowInfo* mainWindowInfo = new MainWindowInfo;
        if (!parcel.ReadInt32(mainWindowInfo->pid_) || !parcel.ReadString(mainWindowInfo->bundleName_) ||
            !parcel.ReadInt32(mainWindowInfo->persistentId_) || !parcel.ReadInt32(mainWindowInfo->bundleType_) ||
            !parcel.ReadUint64(mainWindowInfo->displayId_) || !parcel.ReadBool(mainWindowInfo->showing_) ||
            !parcel.ReadString(mainWindowInfo->label_)) {
            delete mainWindowInfo;
            return nullptr;
        }
        return mainWindowInfo;
    }

    int32_t pid_ = 0;
    std::string bundleName_ = "";
    int32_t persistentId_ = 0;
    int32_t bundleType_ = 0;
    DisplayId displayId_ = DISPLAY_ID_INVALID;
    bool showing_ = false;
    std::string label_ = "";
};

/**
 * @struct WindowSnapshotConfiguration
 *
 * @brief main window info for all windows on the screen.
 */
struct WindowSnapshotConfiguration : public Parcelable {
    bool useCache = true;

    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteBool(useCache);
    }

    static WindowSnapshotConfiguration* Unmarshalling(Parcel& parcel)
    {
        auto windowSnapshotConfiguration = std::make_unique<WindowSnapshotConfiguration>();
        if (!parcel.ReadBool(windowSnapshotConfiguration->useCache)) {
            return nullptr;
        }
        return windowSnapshotConfiguration.release();
    }
};

/**
 * @struct SnapshotConfig
 *
 * @brief Snapshot configuration for sa inner interface.
 */
struct SnapshotConfig : public Parcelable {
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    bool useCurWindow = false;

    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteFloat(scaleX) && parcel.WriteFloat(scaleY) && parcel.WriteBool(useCurWindow);
    }

    static SnapshotConfig* Unmarshalling(Parcel& parcel)
    {
        auto snapshotConfig = std::make_unique<SnapshotConfig>();
        if (!parcel.ReadFloat(snapshotConfig->scaleX) ||
            !parcel.ReadFloat(snapshotConfig->scaleY) ||
            !parcel.ReadBool(snapshotConfig->useCurWindow)) {
            return nullptr;
        }
        return snapshotConfig.release();
    }
};

/**
 * @struct MainWindowState.
 *
 * @brief Main window state info.
 */
struct MainWindowState : public Parcelable {
    bool Marshalling(Parcel& parcel) const override
    {
        if (!parcel.WriteInt32(state_)) {
            return false;
        }
        if (!parcel.WriteBool(isVisible_)) {
            return false;
        }
        if (!parcel.WriteBool(isForegroundInteractive_)) {
            return false;
        }
        if (!parcel.WriteBool(isPcOrPadEnableActivation_)) {
            return false;
        }
        return true;
    }

    static MainWindowState* Unmarshalling(Parcel& parcel)
    {
        MainWindowState* mainWindowState = new MainWindowState();
        if (!mainWindowState) {
            return nullptr;
        }
        if (!parcel.ReadInt32(mainWindowState->state_) ||
            !parcel.ReadBool(mainWindowState->isVisible_) ||
            !parcel.ReadBool(mainWindowState->isForegroundInteractive_) ||
            !parcel.ReadBool(mainWindowState->isPcOrPadEnableActivation_)) {
            delete mainWindowState;
            return nullptr;
        }
        return mainWindowState;
    }

    int32_t state_ = 0;
    bool isVisible_ = false;
    bool isForegroundInteractive_ = false;
    bool isPcOrPadEnableActivation_ = false;
};

namespace {
constexpr uint32_t SYSTEM_COLOR_WHITE = 0xE5FFFFFF;
constexpr uint32_t SYSTEM_COLOR_BLACK = 0x66000000;
constexpr uint32_t INVALID_WINDOW_ID = 0;
constexpr float UNDEFINED_BRIGHTNESS = -1.0f;
constexpr float MINIMUM_BRIGHTNESS = 0.0f;
constexpr float MAXIMUM_BRIGHTNESS = 1.0f;
constexpr float INVALID_BRIGHTNESS = 999.0f;
constexpr int32_t INVALID_PID = -1;
constexpr int32_t INVALID_USER_ID = -1;
constexpr int32_t SYSTEM_USERID = 0;
constexpr int32_t BASE_USER_RANGE = 200000;
constexpr int32_t DEFAULT_SCREEN_ID = 0;
constexpr int32_t ZERO_CIRCLE_DEGREE = 0;
constexpr int32_t FULL_CIRCLE_DEGREE = 360;
constexpr int32_t ONE_FOURTH_FULL_CIRCLE_DEGREE = 90;
constexpr float UNDEFINED_DENSITY = -1.0f;
constexpr float MINIMUM_CUSTOM_DENSITY = 0.5f;
constexpr float MAXIMUM_CUSTOM_DENSITY = 4.0f;
constexpr int32_t MINIMUM_Z_LEVEL = -10000;
constexpr int32_t MAXIMUM_Z_LEVEL = 10000;
constexpr int32_t NORMAL_SUB_WINDOW_Z_LEVEL = 0;
constexpr int32_t MODALITY_SUB_WINDOW_Z_LEVEL = 13000;
constexpr int32_t DIALOG_SUB_WINDOW_Z_LEVEL = 13000;
constexpr int32_t TEXT_MENU_SUB_WINDOW_Z_LEVEL = 13500;
constexpr int32_t TOAST_SUB_WINDOW_Z_LEVEL = 14000;
constexpr int32_t APPLICATION_MODALITY_SUB_WINDOW_Z_LEVEL = 20000;
constexpr int32_t TOPMOST_SUB_WINDOW_Z_LEVEL = 2000;
constexpr int32_t INVALID_FINGER_ID = -1;
}

inline int32_t GetUserIdByUid(int32_t uid)
{
    return uid / BASE_USER_RANGE;
}

/**
 * @class Transform
 *
 * @brief parameter of transform and rotate.
 */
class Transform {
public:
    Transform()
        : pivotX_(0.5f), pivotY_(0.5f), scaleX_(1.f), scaleY_(1.f), scaleZ_(1.f), rotationX_(0.f),
          rotationY_(0.f), rotationZ_(0.f), translateX_(0.f), translateY_(0.f), translateZ_(0.f)
    {}
    ~Transform() {}

    bool operator==(const Transform& right) const
    {
        return NearZero(pivotX_ - right.pivotX_) &&
            NearZero(pivotY_ - right.pivotY_) &&
            NearZero(scaleX_ - right.scaleX_) &&
            NearZero(scaleY_ - right.scaleY_) &&
            NearZero(scaleZ_ - right.scaleZ_) &&
            NearZero(rotationX_ - right.rotationX_) &&
            NearZero(rotationY_ - right.rotationY_) &&
            NearZero(rotationZ_ - right.rotationZ_) &&
            NearZero(translateX_ - right.translateX_) &&
            NearZero(translateY_ - right.translateY_) &&
            NearZero(translateZ_ - right.translateZ_);
    }

    bool operator!=(const Transform& right) const
    {
        return !(*this == right);
    }

    float pivotX_;
    float pivotY_;
    float scaleX_;
    float scaleY_;
    float scaleZ_;
    float rotationX_;
    float rotationY_;
    float rotationZ_;
    float translateX_;
    float translateY_;
    float translateZ_;

    static const Transform& Identity()
    {
        static Transform I;
        return I;
    }

    bool Marshalling(Parcel& parcel) const
    {
        return parcel.WriteFloat(pivotX_) && parcel.WriteFloat(pivotY_) &&
               parcel.WriteFloat(scaleX_) && parcel.WriteFloat(scaleY_) && parcel.WriteFloat(scaleZ_) &&
               parcel.WriteFloat(rotationX_) && parcel.WriteFloat(rotationY_) && parcel.WriteFloat(rotationZ_) &&
               parcel.WriteFloat(translateX_) && parcel.WriteFloat(translateY_) && parcel.WriteFloat(translateZ_);
    }

    void Unmarshalling(Parcel& parcel)
    {
        pivotX_ = parcel.ReadFloat();
        pivotY_ = parcel.ReadFloat();
        scaleX_ = parcel.ReadFloat();
        scaleY_ = parcel.ReadFloat();
        scaleZ_ = parcel.ReadFloat();
        rotationX_ = parcel.ReadFloat();
        rotationY_ = parcel.ReadFloat();
        rotationZ_ = parcel.ReadFloat();
        translateX_ = parcel.ReadFloat();
        translateY_ = parcel.ReadFloat();
        translateZ_ = parcel.ReadFloat();
    }
private:
    static inline bool NearZero(float val)
    {
        return val < 0.001f && val > -0.001f;
    }
};

/**
 * @struct SingleHandTransform
 *
 * @brief parameter of transform in single hand mode.
 */
struct SingleHandTransform {
    int32_t posX = 0;
    int32_t posY = 0;
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    bool operator==(const SingleHandTransform& right) const
    {
        return posX == right.posX && MathHelper::NearEqual(scaleX, right.scaleX) &&
               posY == right.posY && MathHelper::NearEqual(scaleY, right.scaleY);
    }

    bool operator!=(const SingleHandTransform& right) const
    {
        return !(*this == right);
    }

    bool Marshalling(Parcel& parcel) const
    {
        return parcel.WriteInt32(posX) && parcel.WriteInt32(posY) &&
               parcel.WriteFloat(scaleX) && parcel.WriteFloat(scaleY);
    }

    void Unmarshalling(Parcel& parcel)
    {
        posX = parcel.ReadInt32();
        posY = parcel.ReadInt32();
        scaleX = parcel.ReadFloat();
        scaleY = parcel.ReadFloat();
    }
};

/**
 * @struct SystemBarProperty
 *
 * @brief Property of system bar
 */
struct SystemBarProperty {
    bool enable_;
    uint32_t backgroundColor_;
    uint32_t contentColor_;
    bool enableAnimation_;
    SystemBarSettingFlag settingFlag_;
    SystemBarProperty() : enable_(true), backgroundColor_(SYSTEM_COLOR_BLACK), contentColor_(SYSTEM_COLOR_WHITE),
                          enableAnimation_(false), settingFlag_(SystemBarSettingFlag::DEFAULT_SETTING) {}
    SystemBarProperty(bool enable, uint32_t background, uint32_t content)
        : enable_(enable), backgroundColor_(background), contentColor_(content), enableAnimation_(false),
          settingFlag_(SystemBarSettingFlag::DEFAULT_SETTING) {}
    SystemBarProperty(bool enable, uint32_t background, uint32_t content, bool enableAnimation)
        : enable_(enable), backgroundColor_(background), contentColor_(content), enableAnimation_(enableAnimation),
          settingFlag_(SystemBarSettingFlag::DEFAULT_SETTING) {}
    SystemBarProperty(bool enable, uint32_t background, uint32_t content,
                      bool enableAnimation, SystemBarSettingFlag settingFlag)
        : enable_(enable), backgroundColor_(background), contentColor_(content), enableAnimation_(enableAnimation),
          settingFlag_(settingFlag) {}
    bool operator == (const SystemBarProperty& a) const
    {
        return (enable_ == a.enable_ && backgroundColor_ == a.backgroundColor_ && contentColor_ == a.contentColor_ &&
            enableAnimation_ == a.enableAnimation_);
    }
};

/**
 * @struct SystemBarPropertyFlag
 *
 * @brief Flag of system bar
 */
struct SystemBarPropertyFlag {
    bool enableFlag = false;
    bool backgroundColorFlag = false;
    bool contentColorFlag = false;
    bool enableAnimationFlag = false;

    bool operator==(const SystemBarPropertyFlag& other) const
    {
        return (enableFlag == other.enableFlag && backgroundColorFlag == other.backgroundColorFlag &&
            contentColorFlag == other.contentColorFlag && enableAnimationFlag == other.enableAnimationFlag);
    }

    bool Contains(const SystemBarPropertyFlag& other) const
    {
        return (enableFlag || !other.enableFlag) && (backgroundColorFlag || !other.backgroundColorFlag) &&
            (contentColorFlag || !other.contentColorFlag) && (enableAnimationFlag || !other.enableAnimationFlag);
    }
};

/**
 * @struct PartialSystemBarProperty
 *
 * @brief Partial system bar property
 */
struct PartialSystemBarProperty {
    bool enable_ = false;
    uint32_t backgroundColor_ = 0;
    uint32_t contentColor_ = 0;
    bool enableAnimation_ = false;
    SystemBarPropertyFlag flag_;
};

/*
 * @enum SystemBarPropertyOwner
 *
 * @brief System bar property owner
 */
enum class SystemBarPropertyOwner {
    APPLICATION,
    ARKUI_NAVIGATION,
    ATOMIC_SERVICE,
    ABILITY_RUNTIME,
};

/**
 * @struct Rect
 *
 * @brief Window Rect
 */
struct Rect {
    int32_t posX_ = 0;
    int32_t posY_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    bool operator==(const Rect& a) const
    {
        return (posX_ == a.posX_ && posY_ == a.posY_ && width_ == a.width_ && height_ == a.height_);
    }

    bool operator!=(const Rect& a) const
    {
        return !this->operator==(a);
    }

    bool IsUninitializedRect() const
    {
        return (posX_ == 0 && posY_ == 0 && width_ == 0 && height_ == 0);
    }

    bool IsUninitializedSize() const
    {
        return width_ == 0 && height_ == 0;
    }

    bool IsInsideOf(const Rect& a) const
    {
        return (posX_ >= a.posX_ && posY_ >= a.posY_ &&
            posX_ + width_ <= a.posX_ + a.width_ && posY_ + height_ <= a.posY_ + a.height_);
    }

    bool IsSamePosition(int32_t x, int32_t y) const
    {
        return posX_ == x && posY_ == y;
    }

    inline std::string ToString() const
    {
        std::ostringstream oss;
        oss << "[" << posX_ << " " << posY_ << " " << width_ << " " << height_ << "]";
        return oss.str();
    }

    bool Marshalling(Parcel& parcel) const
    {
        return parcel.WriteInt32(posX_) && parcel.WriteInt32(posY_) &&
               parcel.WriteUint32(width_) && parcel.WriteUint32(height_);
    }

    void Unmarshalling(Parcel& parcel)
    {
        posX_ = parcel.ReadInt32();
        posY_ = parcel.ReadInt32();
        width_ = parcel.ReadUint32();
        height_ = parcel.ReadUint32();
    }

    static const Rect EMPTY_RECT;

    /**
     * @brief Checks whether the right-bottom corner of a rectangle stays within the valid range.
     *
     * @param x The x-coordinate of the left-top corner.
     * @param y The y-coordinate of the left-top corner.
     * @param width The rectangle's width.
     * @param height The rectangle's height.
     * @return true if right-bottom corner stays within int32_t range; false if overflow happens.
     */
    static bool IsRightBottomValid(int32_t x, int32_t y, uint32_t width, uint32_t height)
    {
        int64_t right = static_cast<int64_t>(x) + static_cast<int64_t>(width);
        int64_t bottom = static_cast<int64_t>(y) + static_cast<int64_t>(height);
        return right <= INT32_MAX && bottom <= INT32_MAX;
    }
};

inline constexpr Rect Rect::EMPTY_RECT { 0, 0, 0, 0 };

/**
 * @struct HookInfo.
 *
 * @brief hook diaplayinfo deepending on the window size.
 */
struct HookInfo {
    uint32_t width_;
    uint32_t height_;
    float_t density_;
    uint32_t rotation_;
    bool enableHookRotation_;
    uint32_t displayOrientation_;
    bool enableHookDisplayOrientation_;
    Rect actualRect_ = { 0, 0, 0, 0};
    
    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "width: " << width_ << ", height: " << height_ << ", density: " << density_
            << ", rotation: " << rotation_
            << ", enableHookRotation: " << (enableHookRotation_ ? "true" : "false")
            << ", displayOrientation: " << displayOrientation_
            << ", enableHookDisplayOrientation: " << (enableHookDisplayOrientation_ ? "true" : "false")
            << ", actualPosX: " << actualRect_.posX_ << ", actualPosY: " << actualRect_.posY_
            << ", actualWidth: " << actualRect_.width_ << ", actualHeight :" << actualRect_.height_;
        return oss.str();
    }
};

/**
 * @brief UIExtension usage
 */
enum class UIExtensionUsage : uint32_t {
    MODAL = 0,
    EMBEDDED,
    CONSTRAINED_EMBEDDED,
    PREVIEW_EMBEDDED,
    UIEXTENSION_USAGE_END
};

/**
 * @brief UIExtension info for event
 */
struct ExtensionWindowEventInfo {
    int32_t persistentId = 0;
    int32_t pid = -1;
    int64_t startModalExtensionTimeStamp = -1;
    Rect windowRect { 0, 0, 0, 0 }; // Calculated from global rect and UIExtension windowRect
    Rect uiExtRect { 0, 0, 0, 0 };  // Transferred from arkUI
    bool hasUpdatedRect = false;
    bool isConstrainedModal = false;
};

/**
 * @brief UIExtension info from ability
 */
struct ExtensionWindowAbilityInfo {
    int32_t persistentId  { 0 };
    int32_t parentId { 0 };
    UIExtensionUsage usage { UIExtensionUsage::UIEXTENSION_USAGE_END };
};

/**
 * @struct KeyboardPanelInfo
 *
 * @brief Info of keyboard panel
 */
struct KeyboardPanelInfo : public Parcelable {
    Rect rect_ = {0, 0, 0, 0};
    Rect beginRect_ = {0, 0, 0, 0};
    Rect endRect_ = {0, 0, 0, 0};
    WindowGravity gravity_ = WindowGravity::WINDOW_GRAVITY_BOTTOM;
    bool isShowing_ = false;

    bool Marshalling(Parcel& parcel) const
    {
        return parcel.WriteInt32(rect_.posX_) && parcel.WriteInt32(rect_.posY_) &&
               parcel.WriteUint32(rect_.width_) && parcel.WriteUint32(rect_.height_) &&
               parcel.WriteInt32(beginRect_.posX_) && parcel.WriteInt32(beginRect_.posY_) &&
               parcel.WriteUint32(beginRect_.width_) && parcel.WriteUint32(beginRect_.height_) &&
               parcel.WriteInt32(endRect_.posX_) && parcel.WriteInt32(endRect_.posY_) &&
               parcel.WriteUint32(endRect_.width_) && parcel.WriteUint32(endRect_.height_) &&
               parcel.WriteUint32(static_cast<uint32_t>(gravity_)) && parcel.WriteBool(isShowing_);
    }

    static KeyboardPanelInfo* Unmarshalling(Parcel& parcel)
    {
        KeyboardPanelInfo* panelInfo = new KeyboardPanelInfo;
        bool res = parcel.ReadInt32(panelInfo->rect_.posX_) && parcel.ReadInt32(panelInfo->rect_.posY_) &&
            parcel.ReadUint32(panelInfo->rect_.width_) && parcel.ReadUint32(panelInfo->rect_.height_) &&
            parcel.ReadInt32(panelInfo->beginRect_.posX_) && parcel.ReadInt32(panelInfo->beginRect_.posY_) &&
            parcel.ReadUint32(panelInfo->beginRect_.width_) && parcel.ReadUint32(panelInfo->beginRect_.height_) &&
            parcel.ReadInt32(panelInfo->endRect_.posX_) && parcel.ReadInt32(panelInfo->endRect_.posY_) &&
            parcel.ReadUint32(panelInfo->endRect_.width_) && parcel.ReadUint32(panelInfo->endRect_.height_);
        if (!res) {
            delete panelInfo;
            return nullptr;
        }
        panelInfo->gravity_ = static_cast<WindowGravity>(parcel.ReadUint32());
        panelInfo->isShowing_ = parcel.ReadBool();

        return panelInfo;
    }
};

/**
 * @struct CallingWindowInfo
 *
 * @brief Information of keyboard calling window.
 */
struct CallingWindowInfo : public Parcelable {
    int32_t windowId_ = 0;
    int32_t callingPid_ = -1;
    DisplayId displayId_ = 0;
    int32_t userId_ = 0;

    CallingWindowInfo() {}
    CallingWindowInfo(int32_t windowId, int32_t callingPid, DisplayId displayId, int32_t userId)
        : windowId_(windowId), callingPid_(callingPid), displayId_(displayId), userId_(userId) {}

    bool Marshalling(Parcel& parcel) const
    {
        return parcel.WriteInt32(windowId_) && parcel.WriteInt32(callingPid_) &&
               parcel.WriteUint64(displayId_) && parcel.WriteInt32(userId_);
    }

    static CallingWindowInfo* Unmarshalling(Parcel& parcel)
    {
        CallingWindowInfo* callingWindowInfo = new CallingWindowInfo();
        bool res = parcel.ReadInt32(callingWindowInfo->windowId_) && parcel.ReadInt32(callingWindowInfo->callingPid_) &&
                   parcel.ReadUint64(callingWindowInfo->displayId_) && parcel.ReadInt32(callingWindowInfo->userId_);
        if (!res) {
            delete callingWindowInfo;
            return nullptr;
        }
        return callingWindowInfo;
    }
};

/**
 * @brief Enumerates avoid area type.
 */
enum class AvoidAreaType : uint32_t {
    TYPE_START = 0,
    TYPE_SYSTEM = TYPE_START,           // area of SystemUI
    TYPE_CUTOUT,                        // cutout of screen
    TYPE_SYSTEM_GESTURE,                // area for system gesture
    TYPE_KEYBOARD,                      // area for soft input keyboard
    TYPE_NAVIGATION_INDICATOR,          // area for navigation indicator
    TYPE_END,
};

/**
 * @brief Enumerates occupied area type.
 */
enum class OccupiedAreaType : uint32_t {
    TYPE_INPUT, // area of input window
};

/**
 * @brief Enumerates color space.
 */
enum class ColorSpace : uint32_t {
    COLOR_SPACE_DEFAULT = 0, // Default color space.
    COLOR_SPACE_WIDE_GAMUT,  // Wide gamut color space. The specific wide color gamut depends on the screen.
};

/**
 * @brief Enumerates window animation.
 */
enum class WindowAnimation : uint32_t {
    NONE,
    DEFAULT,
    INPUTE,
    CUSTOM,
};

/**
 * @brief Enumerates window anchor.
 */
enum class WindowAnchor : uint32_t {
    TOP_START = 0,
    TOP,
    TOP_END,
    START,
    CENTER,
    END,
    BOTTOM_START,
    BOTTOM,
    BOTTOM_END,
};

/**
 * @struct WindowAnchorInfo
 *
 * @brief Window anchor info
 */
struct WindowAnchorInfo : public Parcelable {
    bool isAnchorEnabled_ = false;
    bool isAnchoredByAttach_ = false;
    WindowAnchor windowAnchor_ = WindowAnchor::TOP_START;
    int32_t offsetX_ = 0;
    int32_t offsetY_ = 0;

    struct AttachOptions : public Parcelable {
        std::string currentLayoutMode = "";
        AttachOptions() = default;
        AttachOptions(std::string currentLayoutMode) : currentLayoutMode(currentLayoutMode) {}
        bool operator==(const AttachOptions& other) const
        {
            return currentLayoutMode == other.currentLayoutMode;
        }

        bool operator!=(const AttachOptions& other) const
        {
            return !(*this == other);
        }

        bool Marshalling(Parcel& parcel) const override
        {
            return parcel.WriteString(currentLayoutMode);
        }

        static AttachOptions* Unmarshalling(Parcel& parcel)
        {
            std::string layoutMode = "";
            auto attachOptions = std::make_unique<AttachOptions>();
            if (!attachOptions) {
                return nullptr;
            }
            if (!parcel.ReadString(layoutMode)) {
                return nullptr;
            }
            attachOptions->currentLayoutMode = layoutMode;
            return attachOptions.release();
        }
    };
    AttachOptions attachOptions;

    WindowAnchorInfo() {}
    WindowAnchorInfo(bool isAnchorEnabled) : isAnchorEnabled_(isAnchorEnabled) {}
    WindowAnchorInfo(bool isAnchorEnabled, WindowAnchor windowAnchor, int32_t offsetX,
        int32_t offsetY) : isAnchorEnabled_(isAnchorEnabled),  windowAnchor_(windowAnchor),
        offsetX_(offsetX), offsetY_(offsetY) {}
    WindowAnchorInfo(bool isAnchorEnabled, bool isAnchoredByAttach,  WindowAnchor windowAnchor, int32_t offsetX,
        int32_t offsetY) : isAnchorEnabled_(isAnchorEnabled), isAnchoredByAttach_(isAnchoredByAttach),
        windowAnchor_(windowAnchor), offsetX_(offsetX), offsetY_(offsetY) {}

    bool operator==(const WindowAnchorInfo& other) const
    {
        return isAnchorEnabled_ == other.isAnchorEnabled_ && isAnchoredByAttach_ == other.isAnchoredByAttach_ &&
            windowAnchor_ == other.windowAnchor_ && offsetX_ == other.offsetX_ && offsetY_ == other.offsetY_;
    }

    bool operator!=(const WindowAnchorInfo& other) const
    {
        return !(*this == other);
    }

    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteBool(isAnchorEnabled_) && parcel.WriteBool(isAnchoredByAttach_) &&
            parcel.WriteUint32(static_cast<uint32_t>(windowAnchor_)) && parcel.WriteInt32(offsetX_) &&
            parcel.WriteInt32(offsetY_);
    }

    static WindowAnchorInfo* Unmarshalling(Parcel& parcel)
    {
        uint32_t windowAnchorMode = 0;
        WindowAnchorInfo* windowAnchorInfo = new(std::nothrow) WindowAnchorInfo();
        if (windowAnchorInfo == nullptr) {
            return nullptr;
        }
        if (!parcel.ReadBool(windowAnchorInfo->isAnchorEnabled_)||
            !parcel.ReadBool(windowAnchorInfo->isAnchoredByAttach_) || !parcel.ReadUint32(windowAnchorMode) ||
            !parcel.ReadInt32(windowAnchorInfo->offsetX_) || !parcel.ReadInt32(windowAnchorInfo->offsetY_)) {
            delete windowAnchorInfo;
            return nullptr;
        }
        windowAnchorInfo->windowAnchor_ = static_cast<WindowAnchor>(windowAnchorMode);
        return windowAnchorInfo;
    }
};

/**
 * @brief Enumerates window maximize mode.
 */
enum class MaximizeMode : uint32_t {
    MODE_AVOID_SYSTEM_BAR,
    MODE_FULL_FILL,
    MODE_RECOVER,
    MODE_END,
};

/**
 * @class AvoidArea
 *
 * @brief Area needed to avoid.
 */
class AvoidArea : public Parcelable {
public:
    Rect topRect_ { 0, 0, 0, 0 };
    Rect leftRect_ { 0, 0, 0, 0 };
    Rect rightRect_ { 0, 0, 0, 0 };
    Rect bottomRect_ { 0, 0, 0, 0 };

    bool operator==(const AvoidArea& a) const
    {
        return (leftRect_ == a.leftRect_ && topRect_ == a.topRect_ &&
            rightRect_ == a.rightRect_ && bottomRect_ == a.bottomRect_);
    }

    bool operator!=(const AvoidArea& a) const
    {
        return !this->operator==(a);
    }

    bool isEmptyAvoidArea() const
    {
        return topRect_.IsUninitializedRect() && leftRect_.IsUninitializedRect() &&
            rightRect_.IsUninitializedRect() && bottomRect_.IsUninitializedRect();
    }

    static inline bool WriteParcel(Parcel& parcel, const Rect& rect)
    {
        return parcel.WriteInt32(rect.posX_) && parcel.WriteInt32(rect.posY_) &&
            parcel.WriteUint32(rect.width_) && parcel.WriteUint32(rect.height_);
    }

    static inline bool ReadParcel(Parcel& parcel, Rect& rect)
    {
        return parcel.ReadInt32(rect.posX_) && parcel.ReadInt32(rect.posY_) &&
            parcel.ReadUint32(rect.width_) && parcel.ReadUint32(rect.height_);
    }

    virtual bool Marshalling(Parcel& parcel) const override
    {
        return (WriteParcel(parcel, leftRect_) && WriteParcel(parcel, topRect_) &&
            WriteParcel(parcel, rightRect_) && WriteParcel(parcel, bottomRect_));
    }

    static AvoidArea* Unmarshalling(Parcel& parcel)
    {
        AvoidArea *avoidArea = new(std::nothrow) AvoidArea();
        if (avoidArea == nullptr) {
            return nullptr;
        }
        if (ReadParcel(parcel, avoidArea->leftRect_) && ReadParcel(parcel, avoidArea->topRect_) &&
            ReadParcel(parcel, avoidArea->rightRect_) && ReadParcel(parcel, avoidArea->bottomRect_)) {
            return avoidArea;
        }
        delete avoidArea;
        return nullptr;
    }

    std::string ToString() const
    {
        std::stringstream ss;
        if (isEmptyAvoidArea()) {
            ss << "empty";
            return ss.str();
        }
        if (!topRect_.IsUninitializedRect()) {
            ss << "top " << topRect_.ToString() << " ";
        }
        if (!bottomRect_.IsUninitializedRect()) {
            ss << "bottom " << bottomRect_.ToString() << " ";
        }
        if (!leftRect_.IsUninitializedRect()) {
            ss << "left " << leftRect_.ToString() << " ";
        }
        if (!rightRect_.IsUninitializedRect()) {
            ss << "right " << rightRect_.ToString() << " ";
        }
        return ss.str();
    }
};

/**
 * @struct ExceptionInfo
 *
 * @brief Exception info.
 */
struct ExceptionInfo : public Parcelable {
    /**
     * @brief Marshalling ExceptionInfo.
     *
     * @param parcel Package of ExceptionInfo.
     * @return True means marshall success, false means marshall failed.
     */
    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteBool(needRemoveSession) &&
               parcel.WriteBool(needClearCallerLink);
    }

    /**
     * @brief Unmarshalling ExceptionInfo.
     *
     * @param parcel Package of ExceptionInfo.
     * @return ExceptionInfo object.
     */
    static ExceptionInfo* Unmarshalling(Parcel& parcel)
    {
        auto info = new ExceptionInfo();
        if (!parcel.ReadBool(info->needRemoveSession) ||
            !parcel.ReadBool(info->needClearCallerLink)) {
            delete info;
            return nullptr;
        }
        return info;
    }

    bool needRemoveSession = false;
    bool needClearCallerLink = true;
};

/**
 * @struct FrameMetrics
 *
 * @brief frame metrics info.
 */
struct FrameMetrics : public Parcelable {
    bool firstDrawFrame_ = false;
    uint64_t inputHandlingDuration_ = 0;
    uint64_t layoutMeasureDuration_ = 0;
    uint64_t vsyncTimestamp_ = 0;

    /**
     * @brief Marshalling FrameMetrics.
     *
     * @param parcel Package of FrameMetrics.
     * @return True means marshall success, false means marshall failed.
     */
    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteBool(firstDrawFrame_) &&
               parcel.WriteUint64(inputHandlingDuration_) &&
               parcel.WriteUint64(layoutMeasureDuration_) &&
               parcel.WriteUint64(vsyncTimestamp_);
    }

    /**
     * @brief Unmarshalling FrameMetrics.
     *
     * @param parcel Package of FrameMetrics.
     * @return FrameMetrics object.
     */
    static FrameMetrics* Unmarshalling(Parcel& parcel)
    {
        auto info = std::make_unique<FrameMetrics>();
        if (!parcel.ReadBool(info->firstDrawFrame_) ||
            !parcel.ReadUint64(info->inputHandlingDuration_) ||
            !parcel.ReadUint64(info->layoutMeasureDuration_) ||
            !parcel.ReadUint64(info->vsyncTimestamp_)) {
            return nullptr;
        }
        return info.release();
    }
};

/**
 * @brief Enumerates window update type.
 */
enum class WindowUpdateType : int32_t {
    WINDOW_UPDATE_ADDED = 1,
    WINDOW_UPDATE_REMOVED,
    WINDOW_UPDATE_FOCUSED,
    WINDOW_UPDATE_BOUNDS,
    WINDOW_UPDATE_ACTIVE,
    WINDOW_UPDATE_PROPERTY,
    WINDOW_UPDATE_ALL,
};

/**
 * @brief Enumerates picture in picture window state.
 */
enum class PiPWindowState : uint32_t {
    STATE_UNDEFINED = 0,
    STATE_STARTING = 1,
    STATE_STARTED = 2,
    STATE_STOPPING = 3,
    STATE_STOPPED = 4,
    STATE_RESTORING = 5,
};

/**
 * @brief Enumerates picture in picture template type.
 */
enum class PiPTemplateType : uint32_t {
    VIDEO_PLAY = 0,
    VIDEO_CALL = 1,
    VIDEO_MEETING = 2,
    VIDEO_LIVE = 3,
    END,
};

/**
 * @brief Enumerates picture in picture control group.
 */
enum class PiPControlGroup : uint32_t {
    VIDEO_PLAY_START = 100,
    VIDEO_PREVIOUS_NEXT = 101,
    FAST_FORWARD_BACKWARD = 102,
    VIDEO_PLAY_END,

    VIDEO_CALL_START = 200,
    VIDEO_CALL_MICROPHONE_SWITCH = 201,
    VIDEO_CALL_HANG_UP_BUTTON = 202,
    VIDEO_CALL_CAMERA_SWITCH = 203,
    VIDEO_CALL_MUTE_SWITCH = 204,
    VIDEO_CALL_END,

    VIDEO_MEETING_START = 300,
    VIDEO_MEETING_HANG_UP_BUTTON = 301,
    VIDEO_MEETING_CAMERA_SWITCH = 302,
    VIDEO_MEETING_MUTE_SWITCH = 303,
    VIDEO_MEETING_MICROPHONE_SWITCH = 304,
    VIDEO_MEETING_END,

    VIDEO_LIVE_START = 400,
    VIDEO_PLAY_PAUSE = 401,
    VIDEO_LIVE_MUTE_SWITCH = 402,
    VIDEO_LIVE_END,
    END,
};

/**
 * @brief Enumerates picture in picture state.
 */
enum class PiPState : int32_t {
    ABOUT_TO_START = 1,
    STARTED = 2,
    ABOUT_TO_STOP = 3,
    STOPPED = 4,
    ABOUT_TO_RESTORE = 5,
    ERROR = 6,
};

/**
 * @brief Enumerates picture in picture control status.
 */
enum class PiPControlStatus : int32_t {
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
enum class PiPControlType : uint32_t {
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

/**
 * @brief Enumerates picture in picture state change reason.
 */
enum class PiPStateChangeReason : int32_t {
    REQUEST_START = 0,
    AUTO_START = 1,
    REQUEST_DELETE = 2,
    PANEL_ACTION_DELETE = 3,
    DRAG_DELETE = 4,
    PANEL_ACTION_RESTORE = 5,
    OTHER = 6,
};

struct PiPControlStatusInfo {
    PiPControlType controlType;
    PiPControlStatus status;
};

struct PiPControlEnableInfo {
    PiPControlType controlType;
    PiPControlStatus enabled;
};

struct PiPTemplateInfo : public Parcelable {
    uint32_t pipTemplateType{0};
    uint32_t priority{0};
    std::vector<uint32_t> controlGroup;
    std::vector<PiPControlStatusInfo> pipControlStatusInfoList;
    std::vector<PiPControlEnableInfo> pipControlEnableInfoList;
    uint32_t defaultWindowSizeType{0};
    bool cornerAdsorptionEnabled{true};
    bool isWeb{false};

    PiPTemplateInfo() {}

    bool Marshalling(Parcel& parcel) const override
    {
        if (!(parcel.WriteUint32(pipTemplateType) && parcel.WriteUint32(priority))) {
            return false;
        }
        if (controlGroup.size() > MAX_SIZE_PIP_CONTROL_GROUP || !parcel.WriteUInt32Vector(controlGroup)) {
            return false;
        }
        auto controlStatusSize = pipControlStatusInfoList.size();
        if (controlStatusSize > MAX_SIZE_PIP_CONTROL || !parcel.WriteUint32(static_cast<uint32_t>(controlStatusSize))) {
            return false;
        }
        for (auto& info : pipControlStatusInfoList) {
            if (!parcel.WriteUint32(static_cast<uint32_t>(info.controlType)) ||
                !parcel.WriteInt32(static_cast<int32_t>(info.status))) {
                return false;
            }
        }
        auto controlEnableSize = pipControlEnableInfoList.size();
        if (controlEnableSize > MAX_SIZE_PIP_CONTROL || !parcel.WriteUint32(static_cast<uint32_t>(controlEnableSize))) {
            return false;
        }
        for (auto& info : pipControlEnableInfoList) {
            if (!parcel.WriteUint32(static_cast<uint32_t>(info.controlType)) ||
                !parcel.WriteInt32(static_cast<int32_t>(info.enabled))) {
                return false;
            }
        }
        if (!parcel.WriteUint32(defaultWindowSizeType)) {
            return false;
        }
        if (!parcel.WriteBool(cornerAdsorptionEnabled) || !parcel.WriteBool(isWeb)) {
            return false;
        }
        return true;
    }

    static PiPTemplateInfo* Unmarshalling(Parcel& parcel)
    {
        auto* pipTemplateInfo = new PiPTemplateInfo();
        if (!parcel.ReadUint32(pipTemplateInfo->pipTemplateType) || !parcel.ReadUint32(pipTemplateInfo->priority)) {
            delete pipTemplateInfo;
            return nullptr;
        }
        uint32_t controlStatusSize = 0;
        if (!parcel.ReadUInt32Vector(&pipTemplateInfo->controlGroup) ||
            pipTemplateInfo->controlGroup.size() > MAX_SIZE_PIP_CONTROL_GROUP ||
            !parcel.ReadUint32(controlStatusSize) || controlStatusSize > MAX_SIZE_PIP_CONTROL) {
            delete pipTemplateInfo;
            return nullptr;
        }
        for (uint32_t i = 0; i < controlStatusSize; i++) {
            uint32_t controlType;
            int32_t status;
            if (!parcel.ReadUint32(controlType) || !parcel.ReadInt32(status)) {
                break;
            }
            PiPControlStatusInfo info{};
            info.controlType = static_cast<PiPControlType>(controlType);
            info.status = static_cast<PiPControlStatus>(status);
            pipTemplateInfo->pipControlStatusInfoList.emplace_back(info);
        }
        uint32_t controlEnableSize = 0;
        if (!parcel.ReadUint32(controlEnableSize) || controlEnableSize > MAX_SIZE_PIP_CONTROL) {
            delete pipTemplateInfo;
            return nullptr;
        }
        for (uint32_t i = 0; i < controlEnableSize; i++) {
            uint32_t controlType;
            int32_t enabled;
            if (!parcel.ReadUint32(controlType) || !parcel.ReadInt32(enabled)) {
                break;
            }
            PiPControlEnableInfo info{};
            info.controlType = static_cast<PiPControlType>(controlType);
            info.enabled = static_cast<PiPControlStatus>(enabled);
            pipTemplateInfo->pipControlEnableInfoList.emplace_back(info);
        }
        if (!parcel.ReadUint32(pipTemplateInfo->defaultWindowSizeType)) {
            delete pipTemplateInfo;
            return nullptr;
        }
        if (!parcel.ReadBool(pipTemplateInfo->cornerAdsorptionEnabled) || !parcel.ReadBool(pipTemplateInfo->isWeb)) {
            delete pipTemplateInfo;
            return nullptr;
        }
        return pipTemplateInfo;
    }
};


/**
 * @brief Enumerates floating ball state.
 */
enum class FloatingBallState : uint32_t {
    STARTED = 1,
    STOPPED = 2,
    ERROR = 3,
};
 
/**
 * @brief Enumerates floating ball template.
 */
enum class FloatingBallTemplate : uint32_t {
    STATIC = 1,
    NORMAL = 2,
    EMPHATIC = 3,
    SIMPLE = 4,
    END = 5,
};

struct PiPWindowSize {
    uint32_t width;
    uint32_t height;
    double scale;
};

/**
 * @brief Enumerates floating ball window state.
 */
enum class FbWindowState : uint32_t {
    STATE_UNDEFINED = 0,
    STATE_STARTING = 1,
    STATE_STARTED = 2,
    STATE_STOPPING = 3,
    STATE_STOPPED = 4,
};

/**
 * @struct VsyncCallback
 *
 * @brief Vsync callback
 */
struct VsyncCallback {
    OnCallback onCallback;
};

/**
 * @brief Enumerates window size unit type.
 */
enum class PixelUnit : uint32_t {
    PX = 0, // Physical pixel
    VP = 1, // Virtual pixel
};

struct WindowLimits {
    uint32_t maxWidth_ = static_cast<uint32_t>(INT32_MAX); // The width and height are no larger than INT32_MAX.
    uint32_t maxHeight_ = static_cast<uint32_t>(INT32_MAX);
    uint32_t minWidth_ = 1; // The width and height of the window cannot be less than or equal to 0.
    uint32_t minHeight_ = 1;
    float maxRatio_ = FLT_MAX;
    float minRatio_ = 0.0f;
    float vpRatio_ = 1.0f;
    PixelUnit pixelUnit_ = PixelUnit::PX;

    WindowLimits() {}
    WindowLimits(uint32_t maxWidth, uint32_t maxHeight, uint32_t minWidth, uint32_t minHeight, float maxRatio,
        float minRatio) : maxWidth_(maxWidth), maxHeight_(maxHeight), minWidth_(minWidth), minHeight_(minHeight),
        maxRatio_(maxRatio), minRatio_(minRatio) {}
    WindowLimits(uint32_t maxWidth, uint32_t maxHeight, uint32_t minWidth, uint32_t minHeight, float maxRatio,
        float minRatio, float vpRatio) : maxWidth_(maxWidth), maxHeight_(maxHeight), minWidth_(minWidth),
        minHeight_(minHeight), maxRatio_(maxRatio), minRatio_(minRatio), vpRatio_(vpRatio) {}
    WindowLimits(uint32_t maxWidth, uint32_t maxHeight, uint32_t minWidth, uint32_t minHeight, float maxRatio,
        float minRatio, float vpRatio, PixelUnit pixelUnit) : maxWidth_(maxWidth), maxHeight_(maxHeight),
        minWidth_(minWidth), minHeight_(minHeight), maxRatio_(maxRatio), minRatio_(minRatio), vpRatio_(vpRatio),
        pixelUnit_(pixelUnit) {}

    void Clip(uint32_t clipWidth, uint32_t clipHeight)
    {
        auto safeSub = [](uint32_t base, uint32_t dec) -> uint32_t {
            return (dec >= base) ? 0 : base - dec;
        };

        minWidth_ = safeSub(minWidth_, clipWidth);
        maxWidth_ = safeSub(maxWidth_, clipWidth);
        minHeight_ = safeSub(minHeight_, clipHeight);
        maxHeight_ = safeSub(maxHeight_, clipHeight);
    }

    void Expand(uint32_t expandWidth, uint32_t expandHeight)
    {
        auto safeAdd = [](uint32_t base, uint32_t inc) -> uint32_t {
            return (base > UINT32_MAX - inc) ? UINT32_MAX : base + inc;
        };

        minWidth_ = safeAdd(minWidth_, expandWidth);
        maxWidth_ = safeAdd(maxWidth_, expandWidth);
        minHeight_ = safeAdd(minHeight_, expandHeight);
        maxHeight_ = safeAdd(maxHeight_, expandHeight);
    }

    bool IsEmpty() const
    {
        return (maxWidth_ == 0 || minWidth_ == 0 || maxHeight_ == 0 || minHeight_ == 0);
    }

    bool IsValid() const
    {
        return minWidth_ <= maxWidth_ && minHeight_ <= maxHeight_;
    }

    static const WindowLimits DEFAULT_VP_LIMITS()
    {
        return {
            static_cast<uint32_t>(INT32_MAX),  // maxWidth
            static_cast<uint32_t>(INT32_MAX),  // maxHeight
            1,                                 // minWidth
            1,                                 // minHeight
            FLT_MAX,                           // maxRatio
            0.0f,                              // minRatio
            1.0f,                              // vpRatio
            PixelUnit::VP                      // pixelUnit
        };
    }

    bool IsDefault() const
    {
        return (maxWidth_ == static_cast<uint32_t>(INT32_MAX) &&
                maxHeight_ == static_cast<uint32_t>(INT32_MAX) &&
                minWidth_ == 1 &&
                minHeight_ == 1);
    }

    std::string ToString() const
    {
        constexpr int precision = 6;
        std::ostringstream oss;
        oss << "[" << maxWidth_ << " " << maxHeight_ << " " << minWidth_ << " " << minHeight_
            << " " << std::fixed << std::setprecision(precision) << maxRatio_ << " " << minRatio_
            << " " << vpRatio_ << " " << static_cast<uint32_t>(pixelUnit_) << "]";
        return oss.str();
    }
};

/**
 * @struct TitleButtonRect
 *
 * @brief An area of title buttons relative to the upper right corner of the window.
 */
struct TitleButtonRect {
    int32_t posX_ = 0;
    int32_t posY_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    bool operator==(const TitleButtonRect& a) const
    {
        return (posX_ == a.posX_ && posY_ == a.posY_ && width_ == a.width_ && height_ == a.height_);
    }

    bool operator!=(const TitleButtonRect& a) const
    {
        return !this->operator==(a);
    }

    void ResetRect()
    {
        posX_ = 0;
        posY_ = 0;
        width_ = 0;
        height_ = 0;
    }

    bool IsUninitializedRect() const
    {
        return (posX_ == 0 && posY_ == 0 && width_ == 0 && height_ == 0);
    }

    bool IsInsideOf(const TitleButtonRect& a) const
    {
        return (posX_ >= a.posX_ && posY_ >= a.posY_ &&
            posX_ + width_ <= a.posX_ + a.width_ && posY_ + height_ <= a.posY_ + a.height_);
    }
};

/**
 * @brief WindowInfo filter Option
 */
enum class WindowInfoFilterOption : WindowInfoFilterOptionDataType {
    ALL = 0,
    EXCLUDE_SYSTEM = 1,
    VISIBLE = 1 << 1,
    FOREGROUND = 1 << 2,
};

inline WindowInfoFilterOption operator|(WindowInfoFilterOption lhs, WindowInfoFilterOption rhs)
{
    return static_cast<WindowInfoFilterOption>(static_cast<WindowInfoFilterOptionDataType>(lhs) |
        static_cast<WindowInfoFilterOptionDataType>(rhs));
}

inline bool IsChosenWindowOption(WindowInfoFilterOption options, WindowInfoFilterOption option)
{
    return (static_cast<WindowInfoFilterOptionDataType>(options) &
        static_cast<WindowInfoFilterOptionDataType>(option)) != 0;
}

/**
 * @brief WindowInfo Type Option
 */
enum class WindowInfoTypeOption : WindowInfoTypeOptionDataType {
    WINDOW_UI_INFO = 1,
    WINDOW_DISPLAY_INFO = 1 << 1,
    WINDOW_LAYOUT_INFO = 1 << 2,
    WINDOW_META_INFO = 1 << 3,
    ALL = ~0,
};

inline WindowInfoTypeOption operator|(WindowInfoTypeOption lhs, WindowInfoTypeOption rhs)
{
    return static_cast<WindowInfoTypeOption>(static_cast<WindowInfoTypeOptionDataType>(lhs) |
        static_cast<WindowInfoTypeOptionDataType>(rhs));
}

inline bool IsChosenWindowOption(WindowInfoTypeOption options, WindowInfoTypeOption option)
{
    return (static_cast<WindowInfoTypeOptionDataType>(options) &
        static_cast<WindowInfoTypeOptionDataType>(option)) != 0;
}

/**
 * @enum WindowVisibilityState
 *
 * @brief Visibility state of a window
 */
enum WindowVisibilityState : uint32_t {
    START = 0,
    WINDOW_VISIBILITY_STATE_NO_OCCLUSION = START,
    WINDOW_VISIBILITY_STATE_PARTICALLY_OCCLUSION,
    WINDOW_VISIBILITY_STATE_TOTALLY_OCCUSION,
    WINDOW_LAYER_STATE_MAX,
    END = WINDOW_LAYER_STATE_MAX,
};

/**
 * @struct WindowUIInfo
 *
 * @brief Window UI info
 */
struct WindowUIInfo : public Parcelable {
    WindowVisibilityState visibilityState = WINDOW_LAYER_STATE_MAX;

    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteUint32(static_cast<uint32_t>(visibilityState));
    }

    static WindowUIInfo* Unmarshalling(Parcel& parcel)
    {
        WindowUIInfo* windowUIInfo = new WindowUIInfo();
        uint32_t visibilityState = 0;
        if (!parcel.ReadUint32(visibilityState)) {
            delete windowUIInfo;
            return nullptr;
        }
        windowUIInfo->visibilityState = static_cast<WindowVisibilityState>(visibilityState);
        return windowUIInfo;
    }
};

/**
 * @struct WindowDisplayInfo
 *
 * @brief Window display info
 */
struct WindowDisplayInfo : public Parcelable {
    DisplayId displayId = DISPLAY_ID_INVALID;
    std::string displayName;
    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteUint64(displayId) && parcel.WriteString(displayName);
    }

    static WindowDisplayInfo* Unmarshalling(Parcel& parcel)
    {
        WindowDisplayInfo* windowDisplayInfo = new WindowDisplayInfo();
        if (!parcel.ReadUint64(windowDisplayInfo->displayId) ||
            !parcel.ReadString(windowDisplayInfo->displayName)) {
            delete windowDisplayInfo;
            return nullptr;
        }
        return windowDisplayInfo;
    }
};

/**
 * @struct WindowLayoutInfo
 *
 * @brief Layout info for all windows on the screen.
 */
struct WindowLayoutInfo : public Parcelable {
    Rect rect = Rect::EMPTY_RECT;
    uint32_t zOrder = 0;

    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteInt32(rect.posX_) && parcel.WriteInt32(rect.posY_) && parcel.WriteUint32(rect.width_) &&
               parcel.WriteUint32(rect.height_) && parcel.WriteUint32(zOrder);
    }

    static WindowLayoutInfo* Unmarshalling(Parcel& parcel)
    {
        WindowLayoutInfo* windowLayoutInfo = new WindowLayoutInfo();
        if (!parcel.ReadInt32(windowLayoutInfo->rect.posX_) || !parcel.ReadInt32(windowLayoutInfo->rect.posY_) ||
            !parcel.ReadUint32(windowLayoutInfo->rect.width_) || !parcel.ReadUint32(windowLayoutInfo->rect.height_) ||
            !parcel.ReadUint32(windowLayoutInfo->zOrder)) {
            delete windowLayoutInfo;
            return nullptr;
        }
        return windowLayoutInfo;
    }
};

/**
 * @struct WindowMetaInfo
 *
 * @brief Window meta info
 */
struct WindowMetaInfo : public Parcelable {
    int32_t windowId = 0;
    std::string windowName;
    std::string bundleName;
    std::string abilityName;
    int32_t appIndex = 0;
    int32_t pid = -1;
    WindowType windowType = WindowType::WINDOW_TYPE_APP_MAIN_WINDOW;
    uint32_t parentWindowId = INVALID_WINDOW_ID;
    uint64_t surfaceNodeId = 0;
    uint64_t leashWinSurfaceNodeId = 0;
    bool isPrivacyMode = false;
    WindowMode windowMode = WindowMode::WINDOW_MODE_UNDEFINED;
    bool isMidScene = false;
    bool isFocused = false;
    bool isTouchable = true;

    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteInt32(windowId) && parcel.WriteString(windowName) && parcel.WriteString(bundleName) &&
               parcel.WriteString(abilityName) && parcel.WriteInt32(appIndex) && parcel.WriteInt32(pid) &&
               parcel.WriteUint32(static_cast<uint32_t>(windowType)) && parcel.WriteUint32(parentWindowId) &&
               parcel.WriteUint64(surfaceNodeId) && parcel.WriteUint64(leashWinSurfaceNodeId) &&
               parcel.WriteBool(isPrivacyMode) && parcel.WriteBool(isMidScene) &&
               parcel.WriteBool(isFocused) && parcel.WriteUint32(static_cast<uint32_t>(windowMode)) &&
               parcel.WriteBool(isTouchable);
    }

    static WindowMetaInfo* Unmarshalling(Parcel& parcel)
    {
        uint32_t windowTypeValue = 1;
        uint32_t windowModeValue = 1;
        WindowMetaInfo* windowMetaInfo = new WindowMetaInfo();
        if (!parcel.ReadInt32(windowMetaInfo->windowId) || !parcel.ReadString(windowMetaInfo->windowName) ||
            !parcel.ReadString(windowMetaInfo->bundleName) || !parcel.ReadString(windowMetaInfo->abilityName) ||
            !parcel.ReadInt32(windowMetaInfo->appIndex) || !parcel.ReadInt32(windowMetaInfo->pid) ||
            !parcel.ReadUint32(windowTypeValue) || !parcel.ReadUint32(windowMetaInfo->parentWindowId) ||
            !parcel.ReadUint64(windowMetaInfo->surfaceNodeId) ||
            !parcel.ReadUint64(windowMetaInfo->leashWinSurfaceNodeId) ||
            !parcel.ReadBool(windowMetaInfo->isPrivacyMode) || !parcel.ReadBool(windowMetaInfo->isMidScene) ||
            !parcel.ReadBool(windowMetaInfo->isFocused) || !parcel.ReadUint32(windowModeValue) ||
            !parcel.ReadBool(windowMetaInfo->isTouchable)) {
            delete windowMetaInfo;
            return nullptr;
        }
        windowMetaInfo->windowType = static_cast<WindowType>(windowTypeValue);
        windowMetaInfo->windowMode = static_cast<WindowMode>(windowModeValue);
        return windowMetaInfo;
    }
};

/**
 * @struct WindowInfo
 *
 * @brief Classified window info
 */
struct WindowInfo : public Parcelable {
    WindowUIInfo windowUIInfo;
    WindowDisplayInfo windowDisplayInfo;
    WindowLayoutInfo windowLayoutInfo;
    WindowMetaInfo windowMetaInfo;

    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteParcelable(&windowUIInfo) && parcel.WriteParcelable(&windowDisplayInfo) &&
               parcel.WriteParcelable(&windowLayoutInfo) && parcel.WriteParcelable(&windowMetaInfo);
    }

    static WindowInfo* Unmarshalling(Parcel& parcel)
    {
        WindowInfo* windowInfo = new WindowInfo();
        sptr<WindowUIInfo> windowUIInfo = parcel.ReadParcelable<WindowUIInfo>();
        sptr<WindowDisplayInfo> windowDisplayInfo = parcel.ReadParcelable<WindowDisplayInfo>();
        sptr<WindowLayoutInfo> windowLayoutInfo = parcel.ReadParcelable<WindowLayoutInfo>();
        sptr<WindowMetaInfo> windowMetaInfo = parcel.ReadParcelable<WindowMetaInfo>();
        if (!windowUIInfo || !windowDisplayInfo || !windowLayoutInfo || !windowMetaInfo) {
            delete windowInfo;
            return nullptr;
        }
        windowInfo->windowUIInfo = *windowUIInfo;
        windowInfo->windowDisplayInfo = *windowDisplayInfo;
        windowInfo->windowLayoutInfo = *windowLayoutInfo;
        windowInfo->windowMetaInfo = *windowMetaInfo;
        return windowInfo;
    }
};

/**
 * @struct WindowInfoOption
 *
 * @brief Option of list window info
 */
struct WindowInfoOption : public Parcelable {
    WindowInfoFilterOption windowInfoFilterOption = WindowInfoFilterOption::ALL;
    WindowInfoTypeOption windowInfoTypeOption = WindowInfoTypeOption::ALL;
    DisplayId displayId = DISPLAY_ID_INVALID;
    int32_t windowId = 0;

    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteUint32(static_cast<uint32_t>(windowInfoFilterOption)) &&
               parcel.WriteUint32(static_cast<uint32_t>(windowInfoTypeOption)) &&
               parcel.WriteUint64(displayId) &&
               parcel.WriteInt32(windowId);
    }

    static WindowInfoOption* Unmarshalling(Parcel& parcel)
    {
        WindowInfoOption* windowInfoOption = new WindowInfoOption();
        uint32_t windowInfoFilterOption;
        uint32_t windowInfoTypeOption;
        if (!parcel.ReadUint32(windowInfoFilterOption) || !parcel.ReadUint32(windowInfoTypeOption) ||
            !parcel.ReadUint64(windowInfoOption->displayId) || !parcel.ReadInt32(windowInfoOption->windowId)) {
            delete windowInfoOption;
            return nullptr;
        }
        windowInfoOption->windowInfoFilterOption = static_cast<WindowInfoFilterOption>(windowInfoFilterOption);
        windowInfoOption->windowInfoTypeOption = static_cast<WindowInfoTypeOption>(windowInfoTypeOption);
        return windowInfoOption;
    }
};

struct CrossProcessWindowInfo : public Parcelable {
    int32_t persistentId = 0;
    uint64_t displayId = 0;
    bool isPcAppInPad = false;
    bool isPcAppInpadCompatibleMode = false;

    bool Marshalling(Parcel& parcel) const override
    {
        if (!parcel.WriteInt32(persistentId)) {
            return false;
        }
        if (!parcel.WriteUint64(displayId)) {
            return false;
        }
        if (!parcel.WriteBool(isPcAppInPad)) {
            return false;
        }
        if (!parcel.WriteBool(isPcAppInpadCompatibleMode)) {
            return false;
        }
        return true;
    }

    static CrossProcessWindowInfo* Unmarshalling(Parcel& parcel)
    {
        auto info = new CrossProcessWindowInfo();
        if (!parcel.ReadInt32(info->persistentId) || !parcel.ReadUint64(info->displayId) ||
            !parcel.ReadBool(info->isPcAppInPad) || !parcel.ReadBool(info->isPcAppInpadCompatibleMode)) {
            delete info;
            return nullptr;
        }
        return info;
    }
};

enum class ParentLifeCycleEventType : uint32_t {
    FOREGROUND = 1,
    ACTIVE,
    INACTIVE,
    BACKGROUND,
    RESUMED,
    PAUSED,
    DESTROYED,
};

/**
 * Config of keyboard animation
 */
class KeyboardAnimationCurve : public Parcelable {
public:
    KeyboardAnimationCurve() = default;
    KeyboardAnimationCurve(const std::string& curveType, const std::vector<float>& curveParams, uint32_t duration)
        : curveType_(curveType), duration_(duration)
    {
        curveParams_.assign(curveParams.begin(), curveParams.end());
    }

    virtual bool Marshalling(Parcel& parcel) const override
    {
        if (!parcel.WriteString(curveType_)) {
            return false;
        }

        auto paramSize = curveParams_.size();
        if (paramSize == 4) { // 4: param size
            if (!parcel.WriteUint32(static_cast<uint32_t>(paramSize))) {
                return false;
            }
            for (auto& param : curveParams_) {
                if (!parcel.WriteFloat(param)) {
                    return false;
                }
            }
        } else {
            if (!parcel.WriteUint32(0)) {
                return false;
            }
        }

        if (!parcel.WriteUint32(duration_)) {
            return false;
        }
        return true;
    }

    static KeyboardAnimationCurve* Unmarshalling(Parcel& parcel)
    {
        KeyboardAnimationCurve* config = new KeyboardAnimationCurve;
        uint32_t paramSize = 0;
        if (!parcel.ReadString(config->curveType_) || !parcel.ReadUint32(paramSize)) {
            delete config;
            return nullptr;
        }

        if (paramSize == 4) { // 4: paramSize
            for (uint32_t i = 0; i < paramSize; i++) {
                float param = 0.0f;
                if (!parcel.ReadFloat(param)) {
                    delete config;
                    return nullptr;
                } else {
                    config->curveParams_.push_back(param);
                }
            }
        }

        if (!parcel.ReadUint32(config->duration_)) {
            delete config;
            return nullptr;
        }
        return config;
    }

    std::string curveType_ = "";
    std::vector<float> curveParams_ = {};
    uint32_t duration_ = 0;
};

/**
 * @struct KeyboardAnimationInfo
 *
 * @brief Info of keyboard animation
 */
struct KeyboardAnimationInfo: public Parcelable {
    Rect beginRect { 0, 0, 0, 0 };
    Rect endRect { 0, 0, 0, 0 };
    bool isShow { false };
    bool withAnimation { false };

    bool Marshalling(Parcel& parcel) const
    {
        bool result = parcel.WriteInt32(beginRect.posX_) &&
            parcel.WriteInt32(beginRect.posY_) &&
            parcel.WriteUint32(beginRect.width_) &&
            parcel.WriteUint32(beginRect.height_) &&
            parcel.WriteInt32(endRect.posX_) && parcel.WriteInt32(endRect.posY_) &&
            parcel.WriteUint32(endRect.width_) && parcel.WriteUint32(endRect.height_) &&
            parcel.WriteBool(isShow) && parcel.WriteBool(withAnimation);
        return result;
    }

    static KeyboardAnimationInfo* Unmarshalling(Parcel& parcel)
    {
        KeyboardAnimationInfo* animationInfo = new KeyboardAnimationInfo();
        bool res = parcel.ReadInt32(animationInfo->beginRect.posX_) &&
            parcel.ReadInt32(animationInfo->beginRect.posY_) &&
            parcel.ReadUint32(animationInfo->beginRect.width_) &&
            parcel.ReadUint32(animationInfo->beginRect.height_) &&
            parcel.ReadInt32(animationInfo->endRect.posX_) &&
            parcel.ReadInt32(animationInfo->endRect.posY_) &&
            parcel.ReadUint32(animationInfo->endRect.width_) &&
            parcel.ReadUint32(animationInfo->endRect.height_) &&
            parcel.ReadBool(animationInfo->isShow) &&
            parcel.ReadBool(animationInfo->withAnimation);
        if (!res) {
            delete animationInfo;
            return nullptr;
        }
        return animationInfo;
    }
};

struct KeyboardAnimationConfig {
    KeyboardAnimationCurve curveIn;
    KeyboardAnimationCurve curveOut;
};

struct MoveConfiguration {
    DisplayId displayId = DISPLAY_ID_INVALID;
    std::string ToString() const
    {
        std::string str;
        constexpr int BUFFER_SIZE = 11;
        char buffer[BUFFER_SIZE] = { 0 };
        if (snprintf_s(buffer, sizeof(buffer), sizeof(buffer) - 1, "[%llu]", displayId) > 0) {
            str.append(buffer);
        }
        return str;
    }
};

enum class CaseType {
    CASE_WINDOW_MANAGER = 0,
    CASE_WINDOW,
    CASE_STAGE
};

enum class MaximizePresentation {
    FOLLOW_APP_IMMERSIVE_SETTING = 0,  // follow app set immersiveStateEnable
    EXIT_IMMERSIVE = 1,                // immersiveStateEnable will be set as false
    ENTER_IMMERSIVE = 2,               // immersiveStateEnable will be set as true
    // immersiveStateEnable will be set as true, titleHoverShowEnabled and dockHoverShowEnabled will be set as false
    ENTER_IMMERSIVE_DISABLE_TITLE_AND_DOCK_HOVER = 3,
};

enum ForceHideState : uint32_t {
    NOT_HIDDEN = 0,
    HIDDEN_WHEN_FOCUSED,
    HIDDEN_WHEN_UNFOCUSED
};

enum class ExtensionWindowAttribute : int32_t {
    SYSTEM_WINDOW = 0,
    SUB_WINDOW = 1,
    UNKNOWN = 2
};

struct SystemWindowOptions {
    int32_t windowType = -1;
};

enum class ModalityType : uint8_t {
    WINDOW_MODALITY,
    APPLICATION_MODALITY,
};

struct SubWindowOptions {
    std::string title;
    bool decorEnabled = false;
    bool isModal = false;
    bool isTopmost = false;
    int32_t zLevel = 0;
    bool maximizeSupported = false;
    ModalityType modalityType = ModalityType::WINDOW_MODALITY;
};

struct DecorButtonStyle {
    int32_t  colorMode = DEFAULT_COLOR_MODE;
    uint32_t spacingBetweenButtons = DEFAULT_SPACING_BETWEEN_BUTTONS;
    uint32_t closeButtonRightMargin = DEFAULT_CLOSE_BUTTON_RIGHT_MARGIN;
    uint32_t buttonBackgroundSize = DEFAULT_BUTTON_BACKGROUND_SIZE;
    uint32_t buttonIconSize = DEFAULT_BUTTON_ICON_SIZE;
    uint32_t buttonBackgroundCornerRadius = DEFAULT_BUTTON_BACKGROUND_CORNER_RADIUS;
};

struct ExtensionWindowConfig {
    std::string windowName;
    ExtensionWindowAttribute windowAttribute = ExtensionWindowAttribute::UNKNOWN;
    Rect windowRect;
    SubWindowOptions subWindowOptions;
    SystemWindowOptions systemWindowOptions;
};

template <typename T>
struct SptrHash {
    std::size_t operator()(const sptr<T>& ptr) const
    {
        return std::hash<T*>{}(ptr.GetRefPtr());
    }
};

/**
 * @struct WindowDensityInfo
 *
 * @brief Currently available density
 */
struct WindowDensityInfo {
    float systemDensity = UNDEFINED_DENSITY;
    float defaultDensity = UNDEFINED_DENSITY;
    float customDensity = UNDEFINED_DENSITY;

    std::string ToString() const
    {
        std::string str;
        constexpr int BUFFER_SIZE = 64;
        char buffer[BUFFER_SIZE] = { 0 };
        if (snprintf_s(buffer, sizeof(buffer), sizeof(buffer) - 1,
            "[%f, %f, %f]", systemDensity, defaultDensity, customDensity) > 0) {
            str.append(buffer);
        }
        return str;
    }
};

/**
 * @struct WindowPropertyInfo
 *
 * @brief Currently available window property
 */
struct WindowPropertyInfo {
    Rect windowRect { 0, 0, 0, 0 };
    Rect drawableRect { 0, 0, 0, 0 };
    Rect globalDisplayRect { 0, 0, 0, 0 };
    uint32_t apiCompatibleVersion = 0;
    WindowType type = WindowType::WINDOW_TYPE_APP_MAIN_WINDOW;
    bool isLayoutFullScreen = false;
    bool isFullScreen = false;
    bool isTouchable = false;
    bool isFocusable = false;
    std::string name;
    bool isPrivacyMode = false;
    bool isKeepScreenOn = false;
    float brightness = 0.0f;
    bool isTransparent = false;
    bool isRoundCorner = false;
    float dimBehindValue = 0.0f;
    uint32_t id = INVALID_WINDOW_ID;
    DisplayId displayId = DISPLAY_ID_INVALID;
};

/**
 * @struct KeyboardLayoutParams
 *
 * @brief Keyboard needs to adjust layout
 */
struct KeyboardLayoutParams : public Parcelable {
    WindowGravity gravity_ = WindowGravity::WINDOW_GRAVITY_BOTTOM;
    int32_t landscapeAvoidHeight_ = -1;
    int32_t portraitAvoidHeight_ = -1;
    Rect LandscapeKeyboardRect_ { 0, 0, 0, 0 };
    Rect PortraitKeyboardRect_ { 0, 0, 0, 0 };
    Rect LandscapePanelRect_ { 0, 0, 0, 0 };
    Rect PortraitPanelRect_ { 0, 0, 0, 0 };
    uint64_t displayId_ = DISPLAY_ID_INVALID;

    bool operator==(const KeyboardLayoutParams& other) const
    {
        return (gravity_ == other.gravity_ &&
                landscapeAvoidHeight_ == other.landscapeAvoidHeight_ &&
                portraitAvoidHeight_ == other.portraitAvoidHeight_ &&
                LandscapeKeyboardRect_ == other.LandscapeKeyboardRect_ &&
                PortraitKeyboardRect_ == other.PortraitKeyboardRect_ &&
                LandscapePanelRect_ == other.LandscapePanelRect_ &&
                PortraitPanelRect_ == other.PortraitPanelRect_ &&
                displayId_ == other.displayId_);
    }

    bool operator!=(const KeyboardLayoutParams& params) const
    {
        return !this->operator==(params);
    }

    bool isEmpty() const
    {
        return LandscapeKeyboardRect_.IsUninitializedRect() && PortraitKeyboardRect_.IsUninitializedRect() &&
               LandscapePanelRect_.IsUninitializedRect() && PortraitPanelRect_.IsUninitializedRect();
    }
    
    bool isValidAvoidHeight() const
    {
        return landscapeAvoidHeight_ >= 0 && portraitAvoidHeight_ >= 0;
    }

    static inline bool WriteParcel(Parcel& parcel, const Rect& rect)
    {
        return parcel.WriteInt32(rect.posX_) && parcel.WriteInt32(rect.posY_) &&
               parcel.WriteUint32(rect.width_) && parcel.WriteUint32(rect.height_);
    }

    static inline bool ReadParcel(Parcel& parcel, Rect& rect)
    {
        return parcel.ReadInt32(rect.posX_) && parcel.ReadInt32(rect.posY_) &&
               parcel.ReadUint32(rect.width_) && parcel.ReadUint32(rect.height_);
    }

    virtual bool Marshalling(Parcel& parcel) const override
    {
        return (parcel.WriteUint32(static_cast<uint32_t>(gravity_)) &&
                parcel.WriteUint64(displayId_) &&
                parcel.WriteInt32(landscapeAvoidHeight_) &&
                parcel.WriteInt32(portraitAvoidHeight_) &&
                WriteParcel(parcel, LandscapeKeyboardRect_) &&
                WriteParcel(parcel, PortraitKeyboardRect_) &&
                WriteParcel(parcel, LandscapePanelRect_) &&
                WriteParcel(parcel, PortraitPanelRect_));
    }

    static KeyboardLayoutParams* Unmarshalling(Parcel& parcel)
    {
        KeyboardLayoutParams* params = new KeyboardLayoutParams();
        uint32_t gravity;
        if (parcel.ReadUint32(gravity) &&
            parcel.ReadUint64(params->displayId_) &&
            parcel.ReadInt32(params->landscapeAvoidHeight_) &&
            parcel.ReadInt32(params->portraitAvoidHeight_) &&
            ReadParcel(parcel, params->LandscapeKeyboardRect_) &&
            ReadParcel(parcel, params->PortraitKeyboardRect_) &&
            ReadParcel(parcel, params->LandscapePanelRect_) &&
            ReadParcel(parcel, params->PortraitPanelRect_)) {
            params->gravity_ = static_cast<WindowGravity>(gravity);
            return params;
        }
        delete params;
        return nullptr;
    }
};

/**
 * @struct KeyboardTouchHotAreas
 *
 * @brief Keyboard needs to set hotArea
 */
struct KeyboardTouchHotAreas {
    std::vector<Rect> landscapeKeyboardHotAreas_;
    std::vector<Rect> portraitKeyboardHotAreas_;
    std::vector<Rect> landscapePanelHotAreas_;
    std::vector<Rect> portraitPanelHotAreas_;
    uint64_t displayId_ = DISPLAY_ID_INVALID;

    bool isKeyboardEmpty() const
    {
        return (landscapeKeyboardHotAreas_.empty() || portraitKeyboardHotAreas_.empty());
    }

    bool isPanelEmpty() const
    {
        return (landscapePanelHotAreas_.empty() || portraitPanelHotAreas_.empty());
    }

    bool operator==(const KeyboardTouchHotAreas& other) const
    {
        return (landscapeKeyboardHotAreas_ == other.landscapeKeyboardHotAreas_ &&
                portraitKeyboardHotAreas_ == other.portraitKeyboardHotAreas_ &&
                landscapePanelHotAreas_ == other.landscapePanelHotAreas_ &&
                portraitPanelHotAreas_ == other.portraitPanelHotAreas_ &&
                displayId_ == other.displayId_);
    }

    bool operator!=(const KeyboardTouchHotAreas& other) const
    {
        return !this->operator==(other);
    }
};

enum class KeyboardViewMode: uint32_t {
    NON_IMMERSIVE_MODE = 0,
    IMMERSIVE_MODE,
    LIGHT_IMMERSIVE_MODE,
    DARK_IMMERSIVE_MODE,
    VIEW_MODE_END,
};

enum class KeyboardFlowLightMode: uint32_t {
    NONE = 0,
    BACKGROUND_FLOW_LIGHT,
    END,
};

enum class KeyboardGradientMode: uint32_t {
    NONE = 0,
    LINEAR_GRADIENT,
    END,
};

struct KeyboardEffectOption : public Parcelable {
    KeyboardViewMode viewMode_ = KeyboardViewMode::NON_IMMERSIVE_MODE;
    KeyboardFlowLightMode flowLightMode_ = KeyboardFlowLightMode::NONE;
    KeyboardGradientMode gradientMode_ = KeyboardGradientMode::NONE;
    uint32_t blurHeight_ = 0;

    KeyboardEffectOption() {}
    KeyboardEffectOption(KeyboardViewMode viewMode, KeyboardFlowLightMode flowLightMode,
        KeyboardGradientMode gradientMode, uint32_t blurHeight) : viewMode_(viewMode), flowLightMode_(flowLightMode),
        gradientMode_(gradientMode), blurHeight_(blurHeight) {}

    virtual bool Marshalling(Parcel& parcel) const override
    {
        return (parcel.WriteUint32(static_cast<uint32_t>(viewMode_)) &&
                parcel.WriteUint32(static_cast<uint32_t>(flowLightMode_)) &&
                parcel.WriteUint32(static_cast<uint32_t>(gradientMode_)) &&
                parcel.WriteUint32(blurHeight_));
    }

    static KeyboardEffectOption* Unmarshalling(Parcel& parcel)
    {
        KeyboardEffectOption* option = new KeyboardEffectOption();
        uint32_t viewMode = 0;
        uint32_t flowLightMode = 0;
        uint32_t gradientMode = 0;
        if (!parcel.ReadUint32(viewMode) ||
            !parcel.ReadUint32(flowLightMode) ||
            !parcel.ReadUint32(gradientMode) ||
            !parcel.ReadUint32(option->blurHeight_)) {
            delete option;
            return nullptr;
        }
        option->viewMode_ = static_cast<KeyboardViewMode>(viewMode);
        option->flowLightMode_ = static_cast<KeyboardFlowLightMode>(flowLightMode);
        option->gradientMode_ = static_cast<KeyboardGradientMode>(gradientMode);
        return option;
    }

    bool operator==(const KeyboardEffectOption& option) const
    {
        return (viewMode_ == option.viewMode_ &&
                flowLightMode_ == option.flowLightMode_ &&
                gradientMode_ == option.gradientMode_ &&
                blurHeight_ == option.blurHeight_);
    }

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "[" << static_cast<uint32_t>(viewMode_) << ", "
            << static_cast<uint32_t>(flowLightMode_) << ", "
            << static_cast<uint32_t>(gradientMode_) << ", "
            << blurHeight_ << "]";
        return oss.str();
    }
};

/*
 * Multi User
 */
enum class UserSwitchEventType: uint32_t {
    SWITCHING,
    SWITCHED,
};

/**
 * @brief Enumerates window focus change reason
 */
enum class WindowFocusChangeReason : int32_t {
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
     * focus change max.
     */
    MAX,
};

/**
 * @brief Windowinfokey
 */
enum class WindowInfoKey : int32_t {
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
 * @struct OrientationInfo
 *
 * @brief orientation info
 */
struct OrientationInfo {
    uint32_t rotation = 0;
    Rect rect = {0, 0, 0, 0};
    std::map<AvoidAreaType, AvoidArea> avoidAreas;
};

/*
 * @brief Enumerates rotation change type.
 */
enum class RotationChangeType : uint32_t {
    /**
     * rotate will begin.
     */
    WINDOW_WILL_ROTATE = 0,

    /**
     * rotate end.
     */
    WINDOW_DID_ROTATE,
};

/**
 * @brief Enumerates rect type
 */
enum class RectType : uint32_t {
    /**
     * the window rect of app relative to screen.
     */
    RELATIVE_TO_SCREEN = 0,

    /**
     * the window rect of app relative to parent window.
     */
    RELATIVE_TO_PARENT_WINDOW,
};

/**
 * @brief rotation change info to notify listener.
 */
struct RotationChangeInfo {
    RotationChangeType type_;
    uint32_t orientation_;
    DisplayId displayId_;
    Rect displayRect_;
};

/**
 * @brief rotation change result return from listener.
 */
struct RotationChangeResult {
    RectType rectType_;
    Rect windowRect_;
};

/**
 * @brief default zIndex for specific window.
 */
enum DefaultSpecificZIndex {
    MUTISCREEN_COLLABORATION = 930,
    SUPER_PRIVACY_ANIMATION = 1100,
};

/**
 * @struct ShadowsInfo
 *
 * @brief window shadows info
 */
struct ShadowsInfo : public Parcelable {
    float radius_ = 0.0f;
    std::string color_;
    float offsetX_ = 0.0f;
    float offsetY_ = 0.0f;
    bool hasRadiusValue_ = false;
    bool hasColorValue_ = false;
    bool hasOffsetXValue_ = false;
    bool hasOffsetYValue_ = false;

    ShadowsInfo() {}
    ShadowsInfo(float radius, std::string color, float offsetX, float offsetY, bool hasRadiusValue,
        bool hasColorValue, bool hasOffsetXValue, bool hasOffsetYValue) : radius_(radius), color_(color),
        offsetX_(offsetX), offsetY_(offsetY), hasRadiusValue_(hasRadiusValue), hasColorValue_(hasColorValue),
        hasOffsetXValue_(hasOffsetXValue), hasOffsetYValue_(hasOffsetYValue) {}

    bool operator==(const ShadowsInfo& other) const
    {
        return (NearEqual(radius_, other.radius_) && color_ == other.color_ &&
            NearEqual(offsetX_, other.offsetX_) && NearEqual(offsetY_, other.offsetY_));
    }

    bool operator!=(const ShadowsInfo& other) const
    {
        return !this->operator==(other);
    }

    static inline bool NearEqual(float left, float right) { return std::abs(left - right) < POS_ZERO; }

    bool Marshalling(Parcel& parcel) const override
    {
        if (!parcel.WriteBool(hasRadiusValue_)) {
            return false;
        }

        if (!parcel.WriteBool(hasColorValue_)) {
            return false;
        }

        if (!parcel.WriteBool(hasOffsetXValue_)) {
            return false;
        }

        if (!parcel.WriteBool(hasOffsetYValue_)) {
            return false;
        }

        if (hasRadiusValue_ && !parcel.WriteFloat(radius_)) {
            return false;
        }

        if (hasColorValue_ && !parcel.WriteString(color_)) {
            return false;
        }

        if (hasOffsetXValue_ && !parcel.WriteFloat(offsetX_)) {
            return false;
        }

        if (hasOffsetYValue_ && !parcel.WriteFloat(offsetY_)) {
            return false;
        }
        return true;
    }

    static ShadowsInfo* Unmarshalling(Parcel& parcel)
    {
        ShadowsInfo* shadowsInfo = new ShadowsInfo();
        if (!parcel.ReadBool(shadowsInfo->hasRadiusValue_)) {
            delete shadowsInfo;
            return nullptr;
        }

        if (!parcel.ReadBool(shadowsInfo->hasColorValue_)) {
            delete shadowsInfo;
            return nullptr;
        }

        if (!parcel.ReadBool(shadowsInfo->hasOffsetXValue_)) {
            delete shadowsInfo;
            return nullptr;
        }

        if (!parcel.ReadBool(shadowsInfo->hasOffsetYValue_)) {
            delete shadowsInfo;
            return nullptr;
        }

        if (shadowsInfo->hasRadiusValue_ && !parcel.ReadFloat(shadowsInfo->radius_)) {
            delete shadowsInfo;
            return nullptr;
        }

        if (shadowsInfo->hasColorValue_ && !parcel.ReadString(shadowsInfo->color_)) {
            delete shadowsInfo;
            return nullptr;
        }

        if (shadowsInfo->hasOffsetXValue_ && !parcel.ReadFloat(shadowsInfo->offsetX_)) {
            delete shadowsInfo;
            return nullptr;
        }

        if (shadowsInfo->hasOffsetYValue_ && !parcel.ReadFloat(shadowsInfo->offsetY_)) {
            delete shadowsInfo;
            return nullptr;
        }
        return shadowsInfo;
    }
};

/**
 * @struct MissionInfo
 *
 * @brief infos of mission
 */
struct MissionInfo : public Parcelable {
    bool startupInvisibility_ = false;

    MissionInfo() {}
    MissionInfo(bool startupInvisibility) : startupInvisibility_(startupInvisibility) {}

    bool Marshalling(Parcel& parcel) const override
    {
        if (!parcel.WriteBool(startupInvisibility_)) {
            return false;
        }
        return true;
    }

    static MissionInfo* Unmarshalling(Parcel& parcel)
    {
        auto missionInfo = std::make_unique<MissionInfo>();
        if (!missionInfo || !parcel.ReadBool(missionInfo->startupInvisibility_)) {
            return nullptr;
        }
        return missionInfo.release();
    }
};

/**
 * @brief Enumerates session state of recent session
 */
enum class RecentSessionState : uint32_t {
    DISCONNECT = 0,
    CONNECT,
    FOREGROUND,
    BACKGROUND,
    ACTIVE,
    INACTIVE,
    END,
};

/**
 * @struct RecentSessionInfo
 *
 * @brief infos of recent sessions
 */
struct RecentSessionInfo : public Parcelable {
    RecentSessionInfo() = default;
    RecentSessionInfo(int32_t persistentId) : missionId(persistentId) {}

    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteInt32(missionId) &&
               parcel.WriteString(bundleName) &&
               parcel.WriteString(moduleName) &&
               parcel.WriteString(abilityName) &&
               parcel.WriteInt32(appIndex) &&
               parcel.WriteUint32(static_cast<uint32_t>(windowType)) &&
               parcel.WriteUint32(static_cast<uint32_t>(sessionState));
    }

    static RecentSessionInfo* Unmarshalling(Parcel& parcel)
    {
        RecentSessionInfo* recentSessionInfo = new RecentSessionInfo();
        uint32_t windowType;
        uint32_t sessionState;
        if (!parcel.ReadInt32(recentSessionInfo->missionId) ||
            !parcel.ReadString(recentSessionInfo->bundleName) ||
            !parcel.ReadString(recentSessionInfo->moduleName) ||
            !parcel.ReadString(recentSessionInfo->abilityName) ||
            !parcel.ReadInt32(recentSessionInfo->appIndex) ||
            !parcel.ReadUint32(windowType) ||
            !parcel.ReadUint32(sessionState)) {
            delete recentSessionInfo;
            return nullptr;
        }
        recentSessionInfo->windowType = static_cast<WindowType>(windowType);
        recentSessionInfo->sessionState = static_cast<RecentSessionState>(sessionState);
        return recentSessionInfo;
    }

    int32_t missionId = -1;
    std::string bundleName;
    std::string moduleName;
    std::string abilityName;
    int32_t appIndex = 0;
    WindowType windowType = WindowType::APP_MAIN_WINDOW_BASE;
    RecentSessionState sessionState = RecentSessionState::DISCONNECT;
};

/**
 * @brief Screenshot event type.
 */
enum class ScreenshotEventType : int32_t {
    START = 0,

    /**
     * System screenshot.
     */
    SYSTEM_SCREENSHOT = START,

    /**
     * System screenshot abort.
     */
    SYSTEM_SCREENSHOT_ABORT = 1,

    /**
     * Scroll shot start.
     */
    SCROLL_SHOT_START = 2,

    /**
     * Scroll shot end.
     */
    SCROLL_SHOT_END = 3,

    /**
     * Scroll shot abort.
     */
    SCROLL_SHOT_ABORT = 4,

    END,
};

/**
 * @brief Enumerates support function type
 */
enum SupportFunctionType : uint32_t {
    /**
     * Supports callbacks triggered before the keyboard show/hide animations begin.
     */
    ALLOW_KEYBOARD_WILL_ANIMATION_NOTIFICATION = 1 << 0,

    /**
     * Supports callbacks triggered after the keyboard show/hide animations complete.
     */
    ALLOW_KEYBOARD_DID_ANIMATION_NOTIFICATION = 1 << 1,
};

/**
 * @brief Enumerates source of sub session.
 */
enum class SubWindowSource : uint32_t {
    SUB_WINDOW_SOURCE_UNKNOWN = 0,
    SUB_WINDOW_SOURCE_ARKUI = 1,
};

enum class RequestResultCode: uint32_t {
    INIT = 0,
    SUCCESS = 1,
    FAIL,
};

/**
 * @brief The type of outline.
 */
enum class OutlineType: uint32_t {
    OUTLINE_FOR_UNDEFINED = 0, // Invalid outline type
    OUTLINE_FOR_SCREEN,        // Update outline for screen
    OUTLINE_FOR_SCREEN_RECT,   // Update outline for a rect of a screen
    OUTLINE_FOR_WINDOW         // Update outline for windows
};

/**
 * @brief The shape of outline.
 */
enum class OutlineShape: uint32_t {
    OUTLINE_SHAPE_RECTANGLE,
    OUTLINE_SHAPE_FOUR_CORNERS,
    OUTLINE_SHAPE_END
};

/**
 * @brief Outline style params.
 */
struct OutlineStyleParams {
    uint32_t outlineColor_ = OUTLINE_COLOR_DEFAULT; // Can not be transparent.
    uint32_t outlineWidth_ = OUTLINE_WIDTH_DEFAULT; // Valid range: [1vp, 8vp].
    OutlineShape outlineShape_ = OutlineShape::OUTLINE_SHAPE_FOUR_CORNERS;

    bool operator == (const OutlineStyleParams& a) const
    {
        return outlineColor_ == a.outlineColor_ && outlineWidth_ == a.outlineWidth_ && outlineShape_ == a.outlineShape_;
    }
    inline std::string ToString() const
    {
        std::ostringstream oss;
        oss << "[ color: " << outlineColor_ << ", width: " << outlineWidth_ << "vp, shape: ";
        oss << static_cast<uint32_t>(outlineShape_) << " ]";
        return oss.str();
    }
};

/**
 * @brief Outline params.
 */
struct OutlineParams : public Parcelable {
    OutlineType type_ = OutlineType::OUTLINE_FOR_UNDEFINED; // Update outline for what kind of area.
    DisplayId displayId_ = DISPLAY_ID_INVALID; // Indicates the displayId of the screen that needs outline.
    Rect rect_;                                // Indicates the rect of a screen that needs outline.
    std::vector<int32_t> persistentIds_;       // Indicates which windows need outline.
    OutlineStyleParams outlineStyleParams_;    // Indicates the style of a outline.

    bool Marshalling(Parcel& parcel) const override
    {
        if (!parcel.WriteUint32(static_cast<uint32_t>(type_))) {
            return false;
        }

        if (!parcel.WriteUint64(static_cast<uint64_t>(displayId_))) {
            return false;
        }

        if (!parcel.WriteInt32(rect_.posX_) || !parcel.WriteInt32(rect_.posY_) ||
            !parcel.WriteUint32(rect_.width_) || !parcel.WriteUint32(rect_.height_)) {
            return false;
        }

        uint32_t size = static_cast<uint32_t>(persistentIds_.size());
        if (size > OUTLINE_FOR_WINDOW_MAX_NUM || !parcel.WriteUint32(size)) {
            return false;
        }

        for (const auto id : persistentIds_) {
            if (!parcel.WriteInt32(id)) {
                return false;
            }
        }

        if (!parcel.WriteUint32(outlineStyleParams_.outlineColor_) ||
            !parcel.WriteUint32(outlineStyleParams_.outlineWidth_) ||
            !parcel.WriteUint32(static_cast<uint32_t>(outlineStyleParams_.outlineShape_))) {
            return false;
        }
        return true;
    }

    static OutlineParams* Unmarshalling(Parcel& parcel)
    {
        std::unique_ptr<OutlineParams> params = std::make_unique<OutlineParams>();

        uint32_t type = 0;
        if (!parcel.ReadUint32(type)) {
            return nullptr;
        }
        params->type_ = static_cast<OutlineType>(type);

        uint64_t displayId = 0;
        if (!parcel.ReadUint64(displayId)) {
            return nullptr;
        }
        params->displayId_ = static_cast<DisplayId>(displayId);

        if (!parcel.ReadInt32(params->rect_.posX_) || !parcel.ReadInt32(params->rect_.posY_) ||
            !parcel.ReadUint32(params->rect_.width_) || !parcel.ReadUint32(params->rect_.height_)) {
            return nullptr;
        }

        uint32_t size = 0;
        if (!parcel.ReadUint32(size) || size > OUTLINE_FOR_WINDOW_MAX_NUM) {
            return nullptr;
        }
        for (uint32_t i = 0; i < size; i++) {
            int32_t persistentId = 0;
            if (!parcel.ReadInt32(persistentId)) {
                return nullptr;
            }
            params->persistentIds_.push_back(persistentId);
        }

        if (!parcel.ReadUint32(params->outlineStyleParams_.outlineColor_) ||
            !parcel.ReadUint32(params->outlineStyleParams_.outlineWidth_)) {
            return nullptr;
        }

        uint32_t outlineShape = 0;
        if (!parcel.ReadUint32(outlineShape)) {
            return nullptr;
        }
        params->outlineStyleParams_.outlineShape_ = static_cast<OutlineShape>(outlineShape);
        return params.release();
    }

    inline std::string ToString() const
    {
        std::ostringstream oss;
        oss << "Outline params: type=" << static_cast<uint32_t>(type_) << ", displayId=" << displayId_ << ", ";
        oss << "rect=[" << rect_.posX_ << ", " << rect_.posY_ << ", " << rect_.width_ << ", " << rect_.height_ << "], ";
        oss << "windowIdList=[ ";
        for (const auto id : persistentIds_) {
            oss << id << ", ";
        }
        oss << "], outlineStyleParams=";
        return oss.str() + outlineStyleParams_.ToString();
    }
};

/**
 * @brief support rotation of current application
 */
struct SupportRotationInfo : public Parcelable {
    DisplayId displayId_ = DISPLAY_ID_INVALID;
    int32_t persistentId_ = 0;
    // ture means support rotate to index*90 rotation
    std::vector<bool> containerSupportRotation_ = {true, false, false, false};
    std::vector<bool> sceneSupportRotation_ = {true, false, false, false};
    std::string supportRotationChangeReason_ = "";

    SupportRotationInfo() {}

    bool Marshalling(Parcel& parcel) const
    {
        if (!parcel.WriteUint64(static_cast<uint64_t>(displayId_))) {
            return false;
        }
        if (!parcel.WriteInt32(persistentId_)) {
            return false;
        }
        for (uint32_t i = 0; i < SUPPORT_ROTATION_SIZE; i++) {
            if (!parcel.WriteBool(containerSupportRotation_[i])) {
                return false;
            }
        }
        for (uint32_t i = 0; i < SUPPORT_ROTATION_SIZE; i++) {
            if (!parcel.WriteBool(sceneSupportRotation_[i])) {
                return false;
            }
        }
        if (!parcel.WriteString(supportRotationChangeReason_)) {
            return false;
        }
        return true;
    }

    static SupportRotationInfo* Unmarshalling(Parcel& parcel)
    {
        SupportRotationInfo* supportRotationInfo = new SupportRotationInfo();
        if (!parcel.ReadUint64(supportRotationInfo->displayId_)) {
            delete supportRotationInfo;
            return nullptr;
        }
        if (!parcel.ReadInt32(supportRotationInfo->persistentId_)) {
            delete supportRotationInfo;
            return nullptr;
        }
        for (uint32_t i = 0; i < SUPPORT_ROTATION_SIZE; i++) {
            bool isSupport = false;
            if (!parcel.ReadBool(isSupport)) {
                delete supportRotationInfo;
                return nullptr;
            }
            supportRotationInfo->containerSupportRotation_[i] = isSupport;
        }
        for (uint32_t i = 0; i < SUPPORT_ROTATION_SIZE; i++) {
            bool isSupport = false;
            if (!parcel.ReadBool(isSupport)) {
                delete supportRotationInfo;
                return nullptr;
            }
            supportRotationInfo->sceneSupportRotation_[i] = isSupport;
        }
        if (!parcel.ReadString(supportRotationInfo->supportRotationChangeReason_)) {
            delete supportRotationInfo;
            return nullptr;
        }
        return supportRotationInfo;
    }
};

/**
 * @enum WaterfallResidentState
 * @brief Represents the resident (persistent) state control of the waterfall layout.
 */
enum class WaterfallResidentState : uint32_t {
    /** The resident state and the current waterfall state remain unchanged. */
    UNCHANGED = 0,

    /** Enable the resident state and enter the waterfall layout. */
    OPEN = 1,

    /** Disable the resident state and exit the waterfall layout. */
    CLOSE = 2,
};

/**
 * @enum CompatibleMode
 * @brief Controls the compatible aspect ratio modes for window display.
 */
enum class CompatibleStyleMode : uint32_t {
    INVALID_VALUE = -1,
    // Default aspect ratio
    LANDSCAPE_DEFAULT = 0,
    // 18:9 aspect ratio
    LANDSCAPE_18_9 = 1,
    // 1:1 aspect ratio
    LANDSCAPE_1_1 = 3,
    // 2:3 aspect ratio
    LANDSCAPE_2_3 = 4,
    // split aspect ratio
    LANDSCAPE_SPLIT = 5,
};

enum class WindowManagerAgentType : uint32_t {
    WINDOW_MANAGER_AGENT_TYPE_FOCUS,
    WINDOW_MANAGER_AGENT_TYPE_SYSTEM_BAR,
    WINDOW_MANAGER_AGENT_TYPE_WINDOW_UPDATE,
    WINDOW_MANAGER_AGENT_TYPE_WINDOW_VISIBILITY,
    WINDOW_MANAGER_AGENT_TYPE_WINDOW_DRAWING_STATE,
    WINDOW_MANAGER_AGENT_TYPE_CAMERA_FLOAT,
    WINDOW_MANAGER_AGENT_TYPE_WATER_MARK_FLAG,
    WINDOW_MANAGER_AGENT_TYPE_VISIBLE_WINDOW_NUM = 7,
    WINDOW_MANAGER_AGENT_TYPE_GESTURE_NAVIGATION_ENABLED,
    WINDOW_MANAGER_AGENT_TYPE_CAMERA_WINDOW,
    WINDOW_MANAGER_AGENT_TYPE_WINDOW_MODE,
    WINDOW_MANAGER_AGENT_TYPE_WINDOW_STYLE,
    WINDOW_MANAGER_AGENT_TYPE_WINDOW_PID_VISIBILITY,
    WINDOW_MANAGER_AGENT_TYPE_PIP,
    WINDOW_MANAGER_AGENT_TYPE_CALLING_DISPLAY,
    WINDOW_MANAGER_AGENT_TYPE_PROPERTY,
    WINDOW_MANAGER_AGENT_STATUS_BAR_PROPERTY,
    WINDOW_MANAGER_AGENT_SUPPORT_ROTATION,
    WINDOW_MANAGER_AGENT_TYPE_DISPLAYGROUP_INFO,
    WINDOW_MANAGER_AGENT_TYPE_END,
};

struct StateChangeOption {
    int32_t parentPersistentId_ = 0;
    WindowState newState_ = WindowState::STATE_INITIAL;
    uint32_t reason_ = 0;
    bool withAnimation_ = false;
    bool withFocus_ = false;
    bool waitAttach_ = false;
    bool isFromInnerkits_ = false;
    bool waitDetach_ = false;

    StateChangeOption(int32_t parentPersistentId, WindowState newState)
        : parentPersistentId_(parentPersistentId), newState_(newState) {}

    StateChangeOption(int32_t parentPersistentId, WindowState newState, uint32_t reason, bool withAnimation,
        bool withFocus, bool waitAttach, bool isFromInnerkits, bool waitDetach)
        : parentPersistentId_(parentPersistentId), newState_(newState), reason_(reason),
          withAnimation_(withAnimation), withFocus_(withFocus), waitAttach_(waitAttach),
          isFromInnerkits_(isFromInnerkits), waitDetach_(waitDetach) {}
};
}
}
#endif // OHOS_ROSEN_WM_COMMON_H
