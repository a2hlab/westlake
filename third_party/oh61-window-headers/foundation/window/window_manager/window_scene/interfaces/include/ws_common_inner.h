/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef OHOS_ROSEN_WS_COMMON_INNER_H
#define OHOS_ROSEN_WS_COMMON_INNER_H

#include <cfloat>
#include <cinttypes>
#include <unordered_set>
#include "ws_common.h"

namespace OHOS {
namespace Rosen {
enum class WSPropertyChangeAction : uint64_t {
    ACTION_UPDATE_RECT = 1,
    ACTION_UPDATE_MODE = 1 << 1,
    ACTION_UPDATE_FLAGS = 1 << 2,
    ACTION_UPDATE_OTHER_PROPS = 1 << 3,
    ACTION_UPDATE_FOCUSABLE = 1 << 4,
    ACTION_UPDATE_TOUCHABLE = 1 << 5,
    ACTION_UPDATE_CALLING_WINDOW = 1 << 6,
    ACTION_UPDATE_ORIENTATION = 1 << 7,
    ACTION_UPDATE_TURN_SCREEN_ON = 1 << 8,
    ACTION_UPDATE_KEEP_SCREEN_ON = 1 << 9,
    ACTION_UPDATE_SET_BRIGHTNESS = 1 << 10,
    ACTION_UPDATE_MODE_SUPPORT_INFO = 1 << 11,
    ACTION_UPDATE_TOUCH_HOT_AREA = 1 << 12,
    ACTION_UPDATE_TRANSFORM_PROPERTY = 1 << 13,
    ACTION_UPDATE_ANIMATION_FLAG = 1 << 14,
    ACTION_UPDATE_PRIVACY_MODE = 1 << 15,
    ACTION_UPDATE_ASPECT_RATIO = 1 << 16,
    ACTION_UPDATE_MAXIMIZE_STATE = 1 << 17,
    ACTION_UPDATE_DECOR_ENABLE = 1 << 18,
    ACTION_UPDATE_WINDOW_LIMITS = 1 << 19,
    ACTION_UPDATE_DRAGENABLED = 1 << 20,
    ACTION_UPDATE_RAISEENABLED = 1 << 21,
    ACTION_UPDATE_HIDE_NON_SYSTEM_FLOATING_WINDOWS = 1 << 22,
    ACTION_UPDATE_SYSTEM_PRIVACY_MODE = 1 << 23,
    ACTION_UPDATE_STATUS_PROPS = 1 << 24,
    ACTION_UPDATE_NAVIGATION_PROPS = 1 << 25,
    ACTION_UPDATE_NAVIGATION_INDICATOR_PROPS = 1 << 26,
    ACTION_UPDATE_TEXTFIELD_AVOID_INFO = 1 << 27,
    ACTION_UPDATE_WINDOW_MASK = 1 << 28,
    ACTION_UPDATE_TOPMOST = 1 << 29,
    ACTION_UPDATE_SNAPSHOT_SKIP = 1 << 30,
    ACTION_UPDATE_MAIN_WINDOW_TOPMOST = 1llu << 31,
    ACTION_UPDATE_AVOID_AREA_OPTION = 1llu << 32,
    ACTION_UPDATE_KEYBOARD_TOUCH_HOT_AREA = 1llu << 33,
    ACTION_UPDATE_BACKGROUND_ALPHA = 1llu << 34,
    ACTION_UPDATE_EXCLUSIVE_HIGHLIGHTED = 1llu << 35,
    ACTION_UPDATE_SUB_WINDOW_Z_LEVEL = 1llu << 36,
    ACTION_UPDATE_VIEW_KEEP_SCREEN_ON = 1llu << 37,
    ACTION_UPDATE_FOLLOW_SCREEN_CHANGE = 1llu << 38,
    ACTION_UPDATE_WINDOW_SHADOW_ENABLED = 1llu << 39,
    ACTION_UPDATE_ROTATION_LOCK_CHANGE = 1llu << 40,
    ACTION_UPDATE_END = ACTION_UPDATE_ROTATION_LOCK_CHANGE,
};

enum class AreaType : uint32_t {
    UNDEFINED = 0,
    LEFT = 1 << 0,
    TOP = 1 << 1,
    RIGHT = 1 << 2,
    BOTTOM = 1 << 3,
    LEFT_TOP = LEFT | TOP,
    RIGHT_TOP = RIGHT | TOP,
    RIGHT_BOTTOM = RIGHT | BOTTOM,
    LEFT_BOTTOM = LEFT | BOTTOM,
};
}
}
#endif // OHOS_ROSEN_WS_COMMON_INNER_H