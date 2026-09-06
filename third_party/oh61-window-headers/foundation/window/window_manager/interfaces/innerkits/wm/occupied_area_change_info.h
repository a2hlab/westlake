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

#ifndef OHOS_OCCUPIED_AREA_CHANGE_INFO_H
#define OHOS_OCCUPIED_AREA_CHANGE_INFO_H

#include "wm_common.h"

namespace OHOS::Rosen {
class OccupiedAreaChangeInfo : public Parcelable {
public:
    
    OccupiedAreaChangeInfo() = default;
    OccupiedAreaChangeInfo(OccupiedAreaType type, Rect rect) : type_(type), rect_(rect) {};
    OccupiedAreaChangeInfo(OccupiedAreaType type, Rect rect, uint32_t safeHeight)
        : type_(type), rect_(rect), safeHeight_(safeHeight) {};
    OccupiedAreaChangeInfo(OccupiedAreaType type, Rect rect,
                            double textFieldPositionY, double textFieldHeight)
        : type_(type), rect_(rect), textFieldPositionY_(textFieldPositionY), textFieldHeight_(textFieldHeight) {};
    OccupiedAreaChangeInfo(OccupiedAreaType type, Rect rect, uint32_t safeHeight,
                           double textFieldPositionY, double textFieldHeight)
        : type_(type), rect_(rect), safeHeight_(safeHeight),
          textFieldPositionY_(textFieldPositionY), textFieldHeight_(textFieldHeight) {};
    ~OccupiedAreaChangeInfo() = default;

    virtual bool Marshalling(Parcel& parcel) const
    {
        return parcel.WriteInt32(rect_.posX_) && parcel.WriteInt32(rect_.posY_) &&
            parcel.WriteUint32(rect_.width_) && parcel.WriteUint32(rect_.height_) &&
            parcel.WriteUint32(static_cast<uint32_t>(type_)) &&
            parcel.WriteUint32(safeHeight_) &&
            parcel.WriteDouble(textFieldPositionY_) && parcel.WriteDouble(textFieldHeight_);
    }
   
    static OccupiedAreaChangeInfo* Unmarshalling(Parcel& parcel)
    {
        OccupiedAreaChangeInfo* occupiedAreaChangeInfo = new OccupiedAreaChangeInfo();
        bool res = parcel.ReadInt32(occupiedAreaChangeInfo->rect_.posX_) &&
            parcel.ReadInt32(occupiedAreaChangeInfo->rect_.posY_) &&
            parcel.ReadUint32(occupiedAreaChangeInfo->rect_.width_) &&
            parcel.ReadUint32(occupiedAreaChangeInfo->rect_.height_);
        if (!res) {
            delete occupiedAreaChangeInfo;
            return nullptr;
        }
        occupiedAreaChangeInfo->type_ = static_cast<OccupiedAreaType>(parcel.ReadUint32());
        occupiedAreaChangeInfo->safeHeight_ = parcel.ReadUint32();
        occupiedAreaChangeInfo->textFieldPositionY_ = parcel.ReadDouble();
        occupiedAreaChangeInfo->textFieldHeight_ = parcel.ReadDouble();
        return occupiedAreaChangeInfo;
    }

    OccupiedAreaType type_ = OccupiedAreaType::TYPE_INPUT;
    Rect rect_ = { 0, 0, 0, 0 };
    uint32_t safeHeight_ = 0;
    double textFieldPositionY_ = 0.0;
    double textFieldHeight_ = 0.0;
};
} // namespace OHOS::Rosen
#endif // OHOS_OCCUPIED_AREA_CHANGE_INFO_H