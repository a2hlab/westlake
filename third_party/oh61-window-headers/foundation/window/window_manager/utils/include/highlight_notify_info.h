/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#ifndef OHOS_ROSEN_HIGHLIGHT_NOTIFY_INFO_H
#define OHOS_ROSEN_HIGHLIGHT_NOTIFY_INFO_H

#include <cstdint>
#include <parcel.h>
#include <iremote_object.h>

#include "wm_common.h"

namespace OHOS::Rosen {
class HighlightNotifyInfo : public Parcelable {
public:
    /**
     * @brief Default constructor of HighlightNotifyInfo
     */
    HighlightNotifyInfo() = default;

    /**
     * @brief Constructor of HighlightNotifyInfo
     */
    HighlightNotifyInfo(int64_t timeStamp, const std::vector<int32_t>& notHighlightIds, int32_t highlightId,
        bool isSyncNotify): timeStamp_(timeStamp), notHighlightIds_(notHighlightIds), highlightId_(highlightId),
        isSyncNotify_(isSyncNotify) {};

    /**
     * @brief Destructor of HighlightNotifyInfo
     */
    ~HighlightNotifyInfo() = default;

    /**
     * @brief Marshalling HighlightNotifyInfo.
     *
     * @param parcel Package of HighlightNotifyInfo.
     * @return True means marshall HighlightNotifyInfo success, false means marshall failed.
     */
    virtual bool Marshalling(Parcel& parcel) const
    {
        return parcel.WriteInt64(timeStamp_) && parcel.WriteInt32Vector(notHighlightIds_) &&
            parcel.WriteInt32(highlightId_) && parcel.WriteBool(isSyncNotify_);
    }

    static HighlightNotifyInfo* Unmarshalling(Parcel& parcel)
    {
        std::unique_ptr<HighlightNotifyInfo> highlightNotifyInfo = std::make_unique<HighlightNotifyInfo>();
        bool res = parcel.ReadInt64(highlightNotifyInfo->timeStamp_) &&
            parcel.ReadInt32Vector(&highlightNotifyInfo->notHighlightIds_) &&
            parcel.ReadInt32(highlightNotifyInfo->highlightId_) && parcel.ReadBool(highlightNotifyInfo->isSyncNotify_);
        if (!res) {
            return nullptr;
        }
        return highlightNotifyInfo.release();
    }

    int64_t timeStamp_ = 0;
    std::vector<int32_t> notHighlightIds_;
    int32_t highlightId_ = INVALID_WINDOW_ID;
    bool isSyncNotify_ = false;
};
} // namespace OHOS::Rosen
#endif // OHOS_ROSEN_HIGHLIGHT_NOTIFY_INFO_H