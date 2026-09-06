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

#ifndef OHOS_FLOATING_BALL_TEMPLATE_INFO_H
#define OHOS_FLOATING_BALL_TEMPLATE_INFO_H

#include <pixel_map.h>

#include "wm_common.h"
#include "floating_ball_template_base_info.h"

namespace OHOS::Rosen {
class FloatingBallTemplateInfo : public FloatingBallTemplateBaseInfo,
                                 public Parcelable {
public:
    FloatingBallTemplateInfo() = default;
    FloatingBallTemplateInfo(const uint32_t& templateType, const std::string& title, const std::string& content,
        const std::string& color, const std::shared_ptr<Media::PixelMap>& icon) : FloatingBallTemplateBaseInfo(
        templateType, title, content, color), icon_(icon) {};
    // LCOV_EXCL_START
    FloatingBallTemplateInfo(const FloatingBallTemplateBaseInfo& baseInfo,
        const std::shared_ptr<Media::PixelMap>& icon) : FloatingBallTemplateBaseInfo(baseInfo.template_,
        baseInfo.title_, baseInfo.content_, baseInfo.backgroundColor_), icon_(icon) {};
    // LCOV_EXCL_STOP
    ~FloatingBallTemplateInfo() override = default;

    std::shared_ptr<Media::PixelMap> icon_ {};

    // LCOV_EXCL_START
    bool Marshalling(Parcel& parcel) const override
    {
        if (!parcel.WriteUint32(template_) || !parcel.WriteString(title_) ||
            !parcel.WriteString(content_) || !parcel.WriteString(backgroundColor_)) {
            return false;
        }
        bool hasIcon = icon_ ? true : false;
        if (!parcel.WriteBool(hasIcon)) {
            return false;
        }
        if (hasIcon && !parcel.WriteParcelable(icon_.get())) {
            return false;
        }
        return true;
    }
    // LCOV_EXCL_STOP

    static FloatingBallTemplateInfo* Unmarshalling(Parcel& parcel)
    {
        std::unique_ptr<FloatingBallTemplateInfo> fbTemplateInfo = std::make_unique<FloatingBallTemplateInfo>();
        if (!parcel.ReadUint32(fbTemplateInfo->template_) || !parcel.ReadString(fbTemplateInfo->title_) ||
            !parcel.ReadString(fbTemplateInfo->content_) || !parcel.ReadString(fbTemplateInfo->backgroundColor_)) {
            return nullptr;
        }
        bool hasIcon = false;
        if (!parcel.ReadBool(hasIcon)) {
            return nullptr;
        }
        if (hasIcon) {
            fbTemplateInfo->icon_ = std::shared_ptr<Media::PixelMap>(parcel.ReadParcelable<Media::PixelMap>());
            if (!fbTemplateInfo->icon_) {
                return nullptr;
            }
        }
        return fbTemplateInfo.release();
    }
};
} // namespace OHOS::Rosen
#endif // OHOS_FLOATING_BALL_TEMPLATE_INFO_H