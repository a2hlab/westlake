/*
 * Copyright (c) 2025-2025 Huawei Device Co., Ltd.
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

#ifndef OHOS_FLOATING_BALL_TEMPLATE_BASE_INFO_H
#define OHOS_FLOATING_BALL_TEMPLATE_BASE_INFO_H

#include "wm_common.h"

namespace OHOS::Rosen {
class FloatingBallTemplateBaseInfo {
public:
    FloatingBallTemplateBaseInfo() = default;
    FloatingBallTemplateBaseInfo(const uint32_t& templateType, const std::string& title,
        const std::string& content, const std::string& color) : template_(templateType), title_(title),
        content_(content), backgroundColor_(color) {};
    virtual ~FloatingBallTemplateBaseInfo() = default;

    uint32_t template_ {};
    std::string title_ {};
    std::string content_ {};
    std::string backgroundColor_ {};
};
} // namespace OHOS::Rosen
#endif // OHOS_FLOATING_BALL_TEMPLATE_BASE_INFO_H