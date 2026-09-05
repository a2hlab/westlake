/*
 * Copyright (C) 2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Boundary-only subset of OpenHarmony-6.0-Release, inputmethod_imf:
// frameworks/native/inputmethod_controller/include/input_method_property.h
// Property/SubProperty declarations and inline implementations are unchanged.
// The older checkout's plain structs MUST NOT cross the OH6 Parcelable ABI.
#ifndef INPUTMETHOD_IMF_INPUT_METHOD_PROPERTY_H
#define INPUTMETHOD_IMF_INPUT_METHOD_PROPERTY_H
#include <mutex>
#include <thread>
#include <vector>
#include "input_method_status.h"
#include "parcel.h"
namespace OHOS {
namespace MiscServices {
struct Property : public Parcelable {
    std::string name;
    std::string id;
    std::string label;
    uint32_t labelId = 0;
    std::string icon;
    uint32_t iconId = 0;
    EnabledStatus status { EnabledStatus::DISABLED };
    bool ReadFromParcel(Parcel &in)
    {
        name = in.ReadString();
        id = in.ReadString();
        label = in.ReadString();
        labelId = in.ReadUint32();
        icon = in.ReadString();
        iconId = in.ReadUint32();
        status = static_cast<EnabledStatus>(in.ReadInt32());
        return true;
    }
    bool Marshalling(Parcel &out) const
    {
        if (!out.WriteString(name)) return false;
        if (!out.WriteString(id)) return false;
        if (!out.WriteString(label)) return false;
        if (!out.WriteUint32(labelId)) return false;
        if (!out.WriteString(icon)) return false;
        if (!out.WriteUint32(iconId)) return false;
        if (!out.WriteInt32(static_cast<int32_t>(status))) return false;
        return true;
    }
    static Property *Unmarshalling(Parcel &in)
    {
        Property *data = new (std::nothrow) Property();
        if (data && !data->ReadFromParcel(in)) { delete data; data = nullptr; }
        return data;
    }
};
struct SubProperty : public Parcelable {
    std::string label;
    uint32_t labelId = 0;
    std::string name;
    std::string id;
    std::string mode;
    std::string locale;
    std::string language;
    std::string icon;
    uint32_t iconId = 0;
    bool ReadFromParcel(Parcel &in)
    {
        label = in.ReadString();
        labelId = in.ReadUint32();
        name = in.ReadString();
        id = in.ReadString();
        mode = in.ReadString();
        locale = in.ReadString();
        language = in.ReadString();
        icon = in.ReadString();
        iconId = in.ReadUint32();
        return true;
    }
    bool Marshalling(Parcel &out) const
    {
        if (!out.WriteString(label)) return false;
        if (!out.WriteUint32(labelId)) return false;
        if (!out.WriteString(name)) return false;
        if (!out.WriteString(id)) return false;
        if (!out.WriteString(mode)) return false;
        if (!out.WriteString(locale)) return false;
        if (!out.WriteString(language)) return false;
        if (!out.WriteString(icon)) return false;
        if (!out.WriteUint32(iconId)) return false;
        return true;
    }
    static SubProperty *Unmarshalling(Parcel &in)
    {
        SubProperty *data = new (std::nothrow) SubProperty();
        if (data && !data->ReadFromParcel(in)) { delete data; data = nullptr; }
        return data;
    }
};
} // namespace MiscServices
} // namespace OHOS
#endif
