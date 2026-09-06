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

#ifndef OHOS_ROSEN_WM_TYPE_H
#define OHOS_ROSEN_WM_TYPE_H

namespace OHOS {
namespace Rosen {
    using DisplayId = uint64_t;
    inline constexpr DisplayId DEFAULT_DISPLAY_ID = 0;
    using OnCallback = std::function<void(int64_t, int64_t)>;
    using WindowInfoFilterOptionDataType = uint8_t;
    using WindowInfoTypeOptionDataType = uint8_t;
}
}
#endif // OHOS_ROSEN_WM_TYPE_H