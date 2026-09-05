/* Copyright (C) 2022 Huawei Device Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
// OpenHarmony-6.0-Release/inputmethod_imf, frameworks/native/
// inputmethod_controller/include/input_method_status.h
#ifndef INPUTMETHOD_IMF_INPUT_METHOD_STATUS_H
#define INPUTMETHOD_IMF_INPUT_METHOD_STATUS_H
#include "message_parcel.h"
namespace OHOS::MiscServices {
enum InputMethodStatus : uint32_t { DISABLE = 0, ENABLE, ALL };
enum class EnabledStatus : int32_t { DISABLED = 0, BASIC_MODE, FULL_EXPERIENCE_MODE };
}
#endif
