/*
 * Copyright (c) 2021-2025 Huawei Device Co., Ltd.
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

#ifndef OHOS_ROSEN_DM_COMMON_H
#define OHOS_ROSEN_DM_COMMON_H

#include <map>
#include <string>
#include <sstream>
#include <unordered_map>

#include <parcel.h>

namespace OHOS::Rosen {
using DisplayId = uint64_t;
using ScreenId = uint64_t;
using NodeId = uint64_t;
using DisplayGroupId = uint64_t;

namespace {
constexpr DisplayId DISPLAY_ID_INVALID = -1ULL;
constexpr ScreenId SCREEN_ID_INVALID = -1ULL;
constexpr DisplayGroupId DISPLAY_GROUP_ID_INVALID = -1ULL;
constexpr DisplayGroupId DISPLAY_GROUP_ID_DEFAULT = 0;
constexpr ScreenId MAIN_SCREEN_ID_DEFAULT = 0;
constexpr ScreenId SCREEN_ID_FAKE = 999;
constexpr DisplayId DISPLAY_ID_FAKE = 999;
constexpr ScreenId ERROR_ID_NO_PERMISSION = -201ULL;
constexpr ScreenId ERROR_ID_NOT_SYSTEM_APP = -202ULL;
constexpr ScreenId ERROR_INVALID_PARAM = -401ULL;
constexpr bool IS_ROTATION_LOCKED_DEFAULT = false;
constexpr int DOT_PER_INCH = 160;
constexpr int DOT_PER_INCH_MAXIMUM_VALUE = 1000;
constexpr int DOT_PER_INCH_MINIMUM_VALUE = 80;
constexpr int32_t CONCURRENT_USER_ID_DEFAULT = -1;
constexpr int32_t INVALID_UID = -1;
constexpr int32_t INVALID_USERID = -1;
constexpr int32_t USER_ID_DEFAULT = 0;
constexpr int32_t ROTATION_UNSET = -1;
constexpr int32_t ROTATION_MIN = 0;
constexpr int32_t ROTATION_MAX = 3;
constexpr int32_t ROTATION_NUM = 4;
constexpr uint32_t BASELINE_DENSITY = 160;
constexpr uint32_t HALF_SCREEN_PARAM = 2;
constexpr uint32_t DISPLAY_A_HEIGHT = 3296;
constexpr uint32_t DISPLAY_B_HEIGHT = 1608;
constexpr int32_t DEFAULT_USE_LOGIC_CAMERA = 0;
constexpr int32_t DEFAULT_CUSTOM_LOGIC_DIRECTION = 0;
}
constexpr uint32_t DISPLAY_A_WIDTH = 2472;
constexpr float DEFAULT_SNAPSHOT_SCALE = 1.0f;
constexpr uint64_t PC_WATCH_DOG_TIME_INTERVAL = 10 * 1000;
constexpr uint64_t NON_PC_WATCH_DOG_TIME_INTERVAL = 5 * 1000;

/**
 * @brief Power state change reason.
 */
enum class PowerStateChangeReason : uint32_t {
    POWER_BUTTON = 0,
    STATE_CHANGE_REASON_INIT = 0,
    STATE_CHANGE_REASON_TIMEOUT = 1,
    STATE_CHANGE_REASON_RUNNING_LOCK = 2,
    STATE_CHANGE_REASON_BATTERY = 3,
    STATE_CHANGE_REASON_THERMAL = 4,
    STATE_CHANGE_REASON_WORK = 5,
    STATE_CHANGE_REASON_SYSTEM = 6,
    STATE_CHANGE_REASON_APPLICATION = 10,
    STATE_CHANGE_REASON_SETTINGS = 11,
    STATE_CHANGE_REASON_HARD_KEY = 12,
    STATE_CHANGE_REASON_TOUCH = 13,
    STATE_CHANGE_REASON_CABLE = 14,
    STATE_CHANGE_REASON_SENSOR = 15,
    STATE_CHANGE_REASON_LID = 16,
    STATE_CHANGE_REASON_CAMERA = 17,
    STATE_CHANGE_REASON_ACCESS = 18,
    STATE_CHANGE_REASON_RESET = 19,
    STATE_CHANGE_REASON_POWER_KEY = 20,
    STATE_CHANGE_REASON_KEYBOARD = 21,
    STATE_CHANGE_REASON_MOUSE = 22,
    STATE_CHANGE_REASON_DOUBLE_CLICK = 23,
    STATE_CHANGE_REASON_COLLABORATION = 24,
    STATE_CHANGE_REASON_SWITCH = 25,
    STATE_CHANGE_REASON_PRE_BRIGHT = 26,
    STATE_CHANGE_REASON_PRE_BRIGHT_AUTH_SUCCESS = 27,
    STATE_CHANGE_REASON_PRE_BRIGHT_AUTH_FAIL_SCREEN_ON = 28,
    STATE_CHANGE_REASON_PRE_BRIGHT_AUTH_FAIL_SCREEN_OFF = 29,
    STATE_CHANGE_REASON_DISPLAY_SWITCH = 30,
    STATE_CHANGE_REASON_PROXIMITY = 32,
    STATE_CHANGE_REASON_AOD_SLIDING = 40,
    STATE_CHANGE_REASON_PEN = 41,
    STATE_CHANGE_REASON_SHUT_DOWN = 42,
    STATE_CHANGE_REASON_SCREEN_CONNECT = 43,
    STATE_CHANGE_REASON_HIBERNATE = 45,
    STATE_CHANGE_REASON_EX_SCREEN_INIT = 46,
    STATE_CHANGE_REASON_ABNORMAL_SCREEN_CONNECT = 47,
    STATE_CHANGE_REASON_ROLLBACK_HIBERNATE = 48,
    STATE_CHANGE_REASON_START_DREAM = 49,
    STATE_CHANGE_REASON_END_DREAM = 50,
    STATE_CHANGE_REASON_SYNCHRONIZE_POWER_STATE = 51,
    STATE_CHANGE_REASON_APPCAST = 52,
    STATE_CHANGE_REASON_AOD_SET_DOZE = 53,
    STATE_CHANGE_REASON_AOD_SET_DOZE_SUSPEND = 54,
    STATE_CHANGE_REASON_AOD_SET_OFF = 55,
    STATE_CHANGE_REASON_AOD_SET_FORCE_OFF = 56,
    STATE_CHANGE_REASON_FOR_ONE_SCREEN_OFF = 57,
    STATE_CHANGE_REASON_REMOTE = 100,
    STATE_CHANGE_REASON_UNKNOWN = 1000,
};

/**
 * @brief Enumerates the state of the screen power.
 */
enum class ScreenPowerState : uint32_t {
    POWER_ON,
    POWER_STAND_BY,
    POWER_SUSPEND,
    POWER_OFF,
    POWER_BUTT,
    INVALID_STATE,
    POWER_DOZE,
    POWER_DOZE_SUSPEND,
};

/**
 * @brief Enumerates the state of the screen power by SceneBoard.
 */
enum class ScbScreenPowerState : uint32_t {
    POWER_OFF,
    POWER_DOZE,
    POWER_DOZE_SUSPEND,
};

enum class ScreenPropertyChangeType : uint32_t {
    UNSPECIFIED = 0,
    /* Screen connection. */
    ROTATION_BEGIN,
    /* Screen disconnection. */
    ROTATION_END,
    /* Only update screen rotation property info to DMS. */
    ROTATION_UPDATE_PROPERTY_ONLY,
    /* Only update screen rotation property info not notify. */
    ROTATION_UPDATE_PROPERTY_ONLY_NOT_NOTIFY,
    /* Switch single hand mode. */
    SINGLE_HAND_SWITCH,
    /* Undefined. */
    UNDEFINED,
};

/**
 * @brief Enumerates screen shape.
 */
enum class ScreenShape : uint32_t {
    RECTANGLE = 0,
    ROUND = 1,
};

/**
 * @brief displayed source mode
 */
enum class DisplaySourceMode : uint32_t {
    NONE = 0,
    MAIN = 1,
    MIRROR = 2,
    EXTEND = 3,
    ALONE = 4,
};

/**
 * @brief Enumerates the state of the display.
 */
enum class DisplayState : uint32_t {
    UNKNOWN,
    OFF,
    ON,
    DOZE,
    DOZE_SUSPEND,
    VR,
    ON_SUSPEND,
};

/**
 * @brief Enumerates display events.
 */
enum class DisplayEvent : uint32_t {
    UNLOCK,
    KEYGUARD_DRAWN,
    SCREEN_LOCK_SUSPEND,
    SCREEN_LOCK_OFF,
    SCREEN_LOCK_FINGERPRINT,
    SCREEN_LOCK_DOZE_FINISH,
    SCREEN_LOCK_START_DREAM,
    SCREEN_LOCK_END_DREAM,
};

/**
 * @brief Enumerates DMError.
 */
enum class DMError : int32_t {
    DM_ERROR_UNKNOWN = -1,
    DM_OK = 0,
    DM_ERROR_INIT_DMS_PROXY_LOCKED = 100,
    DM_ERROR_IPC_FAILED = 101,
    DM_ERROR_REMOTE_CREATE_FAILED = 110,
    DM_ERROR_NULLPTR = 120,
    DM_ERROR_INVALID_PARAM = 130,
    DM_ERROR_WRITE_INTERFACE_TOKEN_FAILED = 140,
    DM_ERROR_DEATH_RECIPIENT = 150,
    DM_ERROR_INVALID_MODE_ID = 160,
    DM_ERROR_WRITE_DATA_FAILED = 170,
    DM_ERROR_RENDER_SERVICE_FAILED = 180,
    DM_ERROR_UNREGISTER_AGENT_FAILED = 190,
    DM_ERROR_INVALID_CALLING = 200,
    DM_ERROR_INVALID_PERMISSION = 201,
    DM_ERROR_NOT_SYSTEM_APP = 202,
    DM_ERROR_DISPLAY_MODE_SWITCH_PENDING = 210,
    DM_ERROR_DEVICE_NOT_SUPPORT = 801,
    DM_ERROR_NOT_SUPPORT_COOR_WHEN_WIRED_CASTING = 100001,
    DM_ERROR_NOT_SUPPORT_COOR_WHEN_WIRLESS_CASTING = 100002,
    DM_ERROR_NOT_SUPPORT_COOR_WHEN_RECORDING = 100003,
    DM_ERROR_NOT_SUPPORT_COOR_WHEN_TENTMODE = 100004,
    DM_ERROR_ILLEGAL_PARAM = 1400004,
};

/**
 * @brief Enumerates DM error codes.
 */
enum class DmErrorCode : int32_t {
    DM_OK = 0,
    DM_ERROR_NO_PERMISSION = 201,
    DM_ERROR_NOT_SYSTEM_APP = 202,
    DM_ERROR_INVALID_PARAM = 401,
    DM_ERROR_DEVICE_NOT_SUPPORT = 801,
    DM_ERROR_INVALID_SCREEN = 1400001,
    DM_ERROR_INVALID_CALLING = 1400002,
    DM_ERROR_SYSTEM_INNORMAL = 1400003,
    DM_ERROR_ILLEGAL_PARAM = 1400004,
};

/**
 * @brief Enumerates the aod operation
 */
enum class AodOP {
    ENTER_START,
    ENTER_FINISH,
    EXIT_START,
    EXIT_FINISH,
    AOD_OP_MAX
};

/**
 * @brief Enumerates TentMode.
 */
enum class TentMode : uint32_t {
    UNKNOWN,
    TENT_MODE,
    HOVER,
};

/**
 * @brief Constructs the mapping of the DM errors to the DM error codes.
 */
extern const std::map<DMError, DmErrorCode> DM_JS_TO_ERROR_CODE_MAP;

extern const std::map<DMError, std::string> DM_ERROR_JS_TO_ERROR_MESSAGE_MAP;

constexpr float DEFAULT_HEADROOM = 1.0f;
constexpr float DEFAULT_SDR_NITS = 500.0f;

using DisplayStateCallback = std::function<void(DisplayState)>;

/**
 * @brief Enumerates display power events.
 */
enum class DisplayPowerEvent : uint32_t {
    WAKE_UP,
    SLEEP,
    DISPLAY_ON,
    DISPLAY_OFF,
    DESKTOP_READY,
    DISPLAY_DOZE,
    DISPLAY_DOZE_SUSPEND,
    DISPLAY_OFF_CANCELED,
    DISPLAY_START_DREAM,
    DISPLAY_END_DREAM,
};

/**
 * @brief Enumerates event status.
 */
enum class EventStatus : uint32_t {
    BEGIN,
    END,
};

enum class VirtualScreenFlag : uint32_t {
    DEFAULT = 0,
    CAST = 1,
    MAX = 2,
};

class IDisplayPowerEventListener : public RefBase {
public:
    /**
     * @brief Notify when display power event status changed.
     */
    virtual void OnDisplayPowerEvent(DisplayPowerEvent event, EventStatus status) = 0;
};

/**
 * @brief Enumerates screen change events.
 */
enum class ScreenChangeEvent : uint32_t {
    UPDATE_ORIENTATION,
    UPDATE_ROTATION,
    CHANGE_MODE,
    VIRTUAL_PIXEL_RATIO_CHANGED,
    SCREEN_SOURCE_MODE_CHANGE,
    SCREEN_SWITCH_CHANGE,
    UNKNOWN,
};

/**
 * @brief Enumerates screen group change events.
 */
enum class ScreenGroupChangeEvent : uint32_t {
    ADD_TO_GROUP,
    REMOVE_FROM_GROUP,
    CHANGE_GROUP,
};

/**
 * @brief Enumerates rotations.
 */
enum class Rotation : uint32_t {
    ROTATION_0,
    ROTATION_90,
    ROTATION_180,
    ROTATION_270,
};

/**
 * @brief Enumerates orientations.
 */
enum class Orientation : uint32_t {
    BEGIN = 0,
    UNSPECIFIED = BEGIN,
    VERTICAL = 1,
    HORIZONTAL = 2,
    REVERSE_VERTICAL = 3,
    REVERSE_HORIZONTAL = 4,
    SENSOR = 5,
    SENSOR_VERTICAL = 6,
    SENSOR_HORIZONTAL = 7,
    AUTO_ROTATION_RESTRICTED = 8,
    AUTO_ROTATION_PORTRAIT_RESTRICTED = 9,
    AUTO_ROTATION_LANDSCAPE_RESTRICTED = 10,
    LOCKED = 11,
    FOLLOW_RECENT = 12,
    AUTO_ROTATION_UNSPECIFIED = 13,
    USER_ROTATION_PORTRAIT = 14,
    USER_ROTATION_LANDSCAPE = 15,
    USER_ROTATION_PORTRAIT_INVERTED = 16,
    USER_ROTATION_LANDSCAPE_INVERTED = 17,
    FOLLOW_DESKTOP = 18,
    END = FOLLOW_DESKTOP,
    USER_PAGE_ROTATION_PORTRAIT = 3000,
    USER_PAGE_ROTATION_LANDSCAPE = 3001,
    USER_PAGE_ROTATION_PORTRAIT_INVERTED = 3002,
    USER_PAGE_ROTATION_LANDSCAPE_INVERTED = 3003,
    INVALID = 3004,
};

/**
 * @brief Rotation info type
 */
enum class RotationInfoType : uint32_t {
    WINDOW_ORIENTATION = 0,
    DISPLAY_ORIENTATION = 1,
    DISPLAY_ROTATION = 2,
};

/**
 * @brief Window Orientation
 */
enum class WindowOrientation : uint32_t {
    PORTRAIT = 0,
    LANDSCAPE_INVERTED,
    PORTRAIT_INVERTED,
    LANDSCAPE,
};

/**
 * @brief Enumerates display orientations.
 */
enum class DisplayOrientation : uint32_t {
    PORTRAIT = 0,
    LANDSCAPE,
    PORTRAIT_INVERTED,
    LANDSCAPE_INVERTED,
    UNKNOWN,
};

extern const std::map<DisplayOrientation, WindowOrientation> DISPLAY_TO_WINDOW_MAP;
extern const std::map<WindowOrientation, DisplayOrientation> WINDOW_TO_DISPLAY_MAP;

/**
 * @brief Enumerates display change events.
 */
enum class DisplayChangeEvent : uint32_t {
    UPDATE_ORIENTATION,
    UPDATE_ROTATION,
    DISPLAY_SIZE_CHANGED,
    DISPLAY_FREEZED,
    DISPLAY_UNFREEZED,
    DISPLAY_VIRTUAL_PIXEL_RATIO_CHANGED,
    UPDATE_ORIENTATION_FROM_WINDOW,
    UPDATE_ROTATION_FROM_WINDOW,
    UPDATE_REFRESHRATE,
    SUPER_FOLD_RESOLUTION_CHANGED,
    SUPER_FOLD_AVAILABLE_AREA_UPDATE,
    SOURCE_MODE_CHANGED,
    UNKNOWN,
};

/**
 * @brief Enumerates display state change type.
 */
enum class DisplayStateChangeType : uint32_t {
    BEFORE_SUSPEND,
    BEFORE_UNLOCK,
    UPDATE_ROTATION,
    UPDATE_ROTATION_FROM_WINDOW,
    SIZE_CHANGE,
    CREATE,
    DESTROY,
    FREEZE,
    UNFREEZE,
    VIRTUAL_PIXEL_RATIO_CHANGE,
    DISPLAY_COMPRESS,
    UPDATE_SCALE,
    UNKNOWN,
    RESOLUTION_CHANGE,
};

/**
 * @brief Enumerates screen source mode.
 */
enum class ScreenSourceMode: uint32_t {
    SCREEN_MAIN = 0,
    SCREEN_MIRROR = 1,
    SCREEN_EXTEND = 2,
    SCREEN_ALONE = 3,
    SCREEN_UNIQUE = 4,
};

/**
 * @brief Enumerates the fold status.
 */
enum class FoldStatus: uint32_t {
    UNKNOWN = 0,
    EXPAND = 1,
    FOLDED = 2,
    HALF_FOLD = 3,
    FOLD_STATE_EXPAND_WITH_SECOND_EXPAND = 11,
    FOLD_STATE_EXPAND_WITH_SECOND_HALF_FOLDED = 21,
    FOLD_STATE_FOLDED_WITH_SECOND_EXPAND = 12,
    FOLD_STATE_FOLDED_WITH_SECOND_HALF_FOLDED = 22,
    FOLD_STATE_HALF_FOLDED_WITH_SECOND_EXPAND = 13,
    FOLD_STATE_HALF_FOLDED_WITH_SECOND_HALF_FOLDED = 23,
};

/**
 * @brief Enumerates the fold display mode.
 */
enum class FoldDisplayMode: uint32_t {
    UNKNOWN = 0,
    FULL = 1,
    MAIN = 2,
    SUB = 3,
    COORDINATION = 4,
    GLOBAL_FULL = 5,
};

struct RotationCorrectionWhiteConfig {
    std::unordered_map<FoldDisplayMode, int32_t> useLogicCamera;
    std::unordered_map<FoldDisplayMode, int32_t> customLogicDirection;

    int32_t GetUseLogicCamera(FoldDisplayMode key) const
    {
        auto it = useLogicCamera.find(key);
        if (it != useLogicCamera.end()) {
            return it->second;
        }
        return DEFAULT_USE_LOGIC_CAMERA;
    }

    int32_t GetCustomLogicDirection(FoldDisplayMode key) const
    {
        auto it = customLogicDirection.find(key);
        if (it != customLogicDirection.end()) {
            return it->second;
        }
        return DEFAULT_CUSTOM_LOGIC_DIRECTION;
    }
};

/**
 * @brief Enumerates the super fold state change events.
 */
enum class SuperFoldStatusChangeEvents : uint32_t {
    UNDEFINED = 0,
    ANGLE_CHANGE_EXPANDED,
    ANGLE_CHANGE_HALF_FOLDED,
    ANGLE_CHANGE_FOLDED,
    KEYBOARD_ON,
    KEYBOARD_OFF,
    SYSTEM_KEYBOARD_ON,
    SYSTEM_KEYBOARD_OFF,
    RESOLUTION_EFFECT_CHANGE,
    INVALID,
};

/**
 * @brief Enumerates the super fold state.
 */
enum class SuperFoldStatus : uint32_t {
    UNKNOWN,
    FOLDED,
    HALF_FOLDED,
    EXPANDED,
    KEYBOARD,
};

/**
 * @brief Enumerates the extend screen connect status.
 */
enum class ExtendScreenConnectStatus: uint32_t {
    UNKNOWN = 0,
    CONNECT,
    DISCONNECT,
};

enum class DisplayType : uint32_t {
    DEFAULT = 0,
};

enum class ScreenCombination : uint32_t {
    SCREEN_ALONE,
    SCREEN_EXPAND,
    SCREEN_MIRROR,
    SCREEN_UNIQUE,
    SCREEN_EXTEND,
    SCREEN_MAIN,
};

enum class MultiScreenPowerSwitchType : uint32_t {
    SCREEN_SWITCH_ON = 0,
    SCREEN_SWITCH_OFF,
    SCREEN_SWITCH_EXTERNAL,
};

enum class MultiScreenMode : uint32_t {
    SCREEN_MIRROR = 0,
    SCREEN_EXTEND = 1,
};

/**
 * @brief Enumerates the low temp mode.
 */
enum class LowTempMode: int32_t {
    UNKNOWN = -1,
    LOW_TEMP_OFF = 0,
    LOW_TEMP_ON = 1,
};

/**
 * @brief Enumerates the virtual screen type.
 */
enum class VirtualScreenType: uint32_t {
    UNKNOWN = 0,
    SCREEN_CASTING,
    SCREEN_RECORDING,
    SUPER_DESKTOP,
    HICAR,
};

/**
 * @brief Enumerates the screen mode change events.
 */
enum class ScreenModeChangeEvent: uint32_t {
    UNKNOWN = 0,
    BEGIN,
    END,
};

class Point : public Parcelable {
public:
    int32_t posX_{0};
    int32_t posY_{0};

    Point() = default;

    Point(int32_t posX, int32_t posY) : posX_(posX), posY_(posY) {}

    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteInt32(posX_) && parcel.WriteInt32(posY_);
    }

    static Point* Unmarshalling(Parcel& parcel)
    {
        int32_t posX;
        int32_t posY;
        if (!(parcel.ReadInt32(posX) && parcel.ReadInt32(posY))) {
            return nullptr;
        }

        return new (std::nothrow) Point(posX, posY);
    }
};

struct SupportedScreenModes : public RefBase {
    uint32_t id_;
    uint32_t width_;
    uint32_t height_;
    uint32_t refreshRate_;
};

/**
 * @brief displayRect
 */
struct DMRect {
    int32_t posX_;
    int32_t posY_;
    uint32_t width_;
    uint32_t height_;

    bool operator==(const DMRect& a) const
    {
        return (posX_ == a.posX_ && posY_ == a.posY_ && width_ == a.width_ && height_ == a.height_);
    }

    bool operator!=(const DMRect& a) const
    {
        return !this->operator==(a);
    }

    bool IsUninitializedRect() const
    {
        return (posX_ == 0 && posY_ == 0 && width_ == 0 && height_ == 0);
    }

    bool IsInsideOf(const DMRect& a) const
    {
        return (posX_ >= a.posX_ && posY_ >= a.posY_ &&
            posX_ + width_ <= a.posX_ + a.width_ && posY_ + height_ <= a.posY_ + a.height_);
    }

    static DMRect NONE()
    {
        return {0, 0, 0, 0};
    }
};

/**
 * @struct HookInfo.
 *
 * @brief hook diaplayinfo deepending on the window size.
 */
struct DMHookInfo {
    uint32_t width_;
    uint32_t height_;
    float_t density_;
    uint32_t rotation_;
    bool enableHookRotation_;
    uint32_t displayOrientation_;
    bool enableHookDisplayOrientation_;
    DMRect actualRect_ = { 0, 0, 0, 0};
    bool isFullScreenInForceSplit_;
};

struct CaptureOption {
    DisplayId displayId_ = DISPLAY_ID_INVALID;
    bool isNeedNotify_ = true;
    bool isNeedPointer_ = true;
    bool isCaptureFullOfScreen_ = false;
    std::vector<NodeId> surfaceNodesList_ = {}; // exclude surfacenodes in screenshot
    std::vector<NodeId> blackWindowIdList_ = {};
    float scaleX_ = DEFAULT_SNAPSHOT_SCALE;
    float scaleY_ = DEFAULT_SNAPSHOT_SCALE;
    DMRect rect = DMRect::NONE();
};

struct ExpandOption {
    ScreenId screenId_;
    uint32_t startX_;
    uint32_t startY_;
};

struct MultiScreenRecoverOption {
    ScreenId screenId_;
    uint32_t first_;
    uint32_t second_;
};

struct ScreenBrightnessInfo {
    float currentHeadroom = DEFAULT_HEADROOM;
    float maxHeadroom = DEFAULT_HEADROOM;
    float sdrNits = DEFAULT_SDR_NITS;
};

struct MultiScreenPositionOptions {
    ScreenId screenId_;
    uint32_t startX_;
    uint32_t startY_;
};

struct UniqueScreenRotationOptions {
    bool isRotationLocked_ = IS_ROTATION_LOCKED_DEFAULT;
    int32_t rotation_ = ROTATION_UNSET;
    std::map<int32_t, int32_t> rotationOrientationMap_ = {};
};

/**
 * Used to print map
 */
inline std::string MapToString(const std::map<int32_t, int32_t>& map)
{
    std::ostringstream oss;
    oss << "{";
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it != map.begin()) oss << ",";
        oss << it->first << ":" << it->second;
    }
    oss << "}";
    return oss.str();
}

/**
 * @brief fold display physical resolution
 */
class DisplayPhysicalResolution : public Parcelable {
public:
    FoldDisplayMode foldDisplayMode_{FoldDisplayMode::UNKNOWN};
    uint32_t physicalWidth_{0};
    uint32_t physicalHeight_{0};

    DisplayPhysicalResolution() = default;

    DisplayPhysicalResolution(FoldDisplayMode foldDisplayMode, uint32_t physicalWidth, uint32_t physicalHeight)
        : foldDisplayMode_(foldDisplayMode), physicalWidth_(physicalWidth), physicalHeight_(physicalHeight) {}

    bool Marshalling(Parcel& parcel) const override
    {
        return parcel.WriteUint32(static_cast<uint32_t>(foldDisplayMode_)) &&
               parcel.WriteUint32(physicalWidth_) && parcel.WriteUint32(physicalHeight_);
    }

    static DisplayPhysicalResolution* Unmarshalling(Parcel& parcel)
    {
        uint32_t foldDisplayMode;
        uint32_t physicalWidth;
        uint32_t physicalHeight;
        if (!(parcel.ReadUint32(foldDisplayMode) && parcel.ReadUint32(physicalWidth) &&
            parcel.ReadUint32(physicalHeight))) {
            return nullptr;
        }

        return new (std::nothrow) DisplayPhysicalResolution(static_cast<FoldDisplayMode>(foldDisplayMode),
            physicalWidth, physicalHeight);
    }
};

/**
 * @brief scrollable param
 */
struct ScrollableParam {
    std::string velocityScale_;
    std::string friction_;
};

/**
 * @brief screen direction info
 */
struct ScreenDirectionInfo {
    int32_t notifyRotation_;
    int32_t screenRotation_;
    int32_t rotation_;
    int32_t phyRotation_;
};

/**
 * @brief Session option when connect
 */
struct SessionOption {
    ScreenId rsId_;
    std::string name_;
    bool isExtend_;
    std::string innerName_;
    ScreenId screenId_;
    std::unordered_map<FoldDisplayMode, int32_t> rotationCorrectionMap_;
    bool supportsFocus_ {true};
    bool isRotationLocked_;
    int32_t rotation_;
    std::map<int32_t, int32_t> rotationOrientationMap_;
    bool isBooting_ { false };
};

/**
 * @brief Device state
 */
enum class DMDeviceStatus: uint32_t {
    UNKNOWN = 0,
    STATUS_FOLDED,
    STATUS_TENT_HOVER,
    STATUS_TENT,
    STATUS_GLOBAL_FULL,
    STATUS_EXPAND
};

/**
 * @struct Position
 *
 * @brief Coordinate of points on the screen
 */
struct Position {
    int32_t x = 0;
    int32_t y = 0;

    bool operator==(const Position& other) const
    {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Position& other) const
    {
        return !(*this == other);
    }

    bool SafeAdd(const Position& other, Position& result) const
    {
        return SafeOp(other, std::plus<int64_t>(), result);
    }

    bool SafeSub(const Position& other, Position& result) const
    {
        return SafeOp(other, std::minus<int64_t>(), result);
    }

    inline std::string ToString() const
    {
        std::ostringstream oss;
        oss << "[" << x << ", " << y << "]";
        return oss.str();
    }

private:
    template<typename Op>
    bool SafeOp(const Position& other, Op op, Position& result) const
    {
        int64_t newX = op(static_cast<int64_t>(x), static_cast<int64_t>(other.x));
        int64_t newY = op(static_cast<int64_t>(y), static_cast<int64_t>(other.y));
        if (newX < INT32_MIN || newX > INT32_MAX || newY < INT32_MIN || newY > INT32_MAX) {
            return false;
        }
        result.x = static_cast<int32_t>(newX);
        result.y = static_cast<int32_t>(newY);
        return true;
    }
};

/**
 * @brief Relative coordinate of points relative to the display
 */
struct RelativePosition {
    DisplayId displayId = 0;
    Position position = {0, 0};
};

/**
 * @brief Rotation option
 */
struct RotationOption {
    Rotation rotation_ = Rotation::ROTATION_0;
    bool needSetRotation_ = false;
};

/**
 * @brief Corner type
 */
enum class CornerType : int32_t {
    TOP_LEFT = 0,
    TOP_RIGHT = 1,
    BOTTOM_RIGHT = 2,
    BOTTOM_LEFT = 3
};

/**
 * @brief Rounded corner
 */
struct RoundedCorner {
    CornerType type;
    Position position;
    int radius;
};

enum class DisplayModeChangeReason : uint32_t {
    DEFAULT = 0,
    RECOVER,
    INVALID,
    SETMODE,
    FORCE_SET
};
}
#endif // OHOS_ROSEN_DM_COMMON_H
