/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#ifndef OHOS_ABILITY_BASE_ELEMENT_NAME_H
#define OHOS_ABILITY_BASE_ELEMENT_NAME_H

#include <string>

#include "parcel.h"

namespace OHOS {
namespace AppExecFwk {
class ElementName : public Parcelable {
    /*
     * How to locate unique Ability: deviceId/bundleName/abilityName
     */
public:
    ElementName(const std::string &deviceId, const std::string &bundleName,
        const std::string &abilityName, const std::string &moduleName = "");
    ElementName();
    ~ElementName();

    std::string GetURI() const;

    bool ParseURI(const std::string &uri);

    bool operator==(const ElementName &element) const;

    void SetDeviceID(const std::string &id)
    {
        deviceId_ = id;
    }

    std::string GetDeviceID() const
    {
        return deviceId_;
    }

    void SetBundleName(const std::string &name)
    {
        bundleName_ = name;
    }

    std::string GetBundleName() const
    {
        return bundleName_;
    }

    void SetAbilityName(const std::string &name)
    {
        abilityName_ = name;
    }

    std::string GetAbilityName() const
    {
        return abilityName_;
    }

    void SetModuleName(const std::string &moduleName)
    {
        moduleName_ = moduleName;
    }

    std::string GetModuleName() const
    {
        return moduleName_;
    }

    bool ReadFromParcel(Parcel &parcel);
    virtual bool Marshalling(Parcel &parcel) const override;
    static ElementName *Unmarshalling(Parcel &parcel);

    void SetElementDeviceID(ElementName *element, const char *deviceId);
    void SetElementBundleName(ElementName *element, const char *bundleName);
    void SetElementAbilityName(ElementName *element, const char *abilityName);
    void SetElementModuleName(ElementName *element, const char *moduleName);
    void ClearElement(ElementName *element);

    void Split(const std::string &str, const std::string &delim, std::vector<std::string> &vec);

private:
    std::string deviceId_;
    std::string bundleName_;
    std::string abilityName_;
    std::string moduleName_;
};
}  // namespace AppExecFwk
}  // namespace OHOS
#endif  // OHOS_ABILITY_BASE_ELEMENT_NAME_H
