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

#ifndef OHOS_WINDOW_SCENE_SESSION_WINDOW_EVENT_CHANNEL_INTERFACE_H
#define OHOS_WINDOW_SCENE_SESSION_WINDOW_EVENT_CHANNEL_INTERFACE_H

#include <iremote_broker.h>
#include <list>
#include <map>

#include "interfaces/include/ws_common.h"

namespace OHOS::MMI {
class PointerEvent;
class KeyEvent;
class AxisEvent;
} // namespace OHOS::MMI
namespace OHOS::Accessibility {
class AccessibilityElementInfo;
}
namespace OHOS::Rosen {
class IWindowEventChannelListener : public IRemoteBroker {
public:
    DECLARE_INTERFACE_DESCRIPTOR(u"OHOS.IWindowEventChannelListener");
    enum class WindowEventChannelListenerMessage : int32_t {
        TRANS_ID_ON_TRANSFER_KEY_EVENT_FOR_CONSUMED_ASYNC,
    };

    virtual void OnTransferKeyEventForConsumed(int32_t keyEventId, bool isPreImeEvent,
                                               bool isConsumed, WSError retCode) = 0;
};

class IWindowEventChannel : public IRemoteBroker {
public:
    DECLARE_INTERFACE_DESCRIPTOR(u"OHOS.IWindowEventChannel");

    virtual WSError TransferKeyEvent(const std::shared_ptr<MMI::KeyEvent>& keyEvent) = 0;
    virtual WSError TransferPointerEvent(const std::shared_ptr<MMI::PointerEvent>& pointerEvent) = 0;

    // transfer sync key event for weather consumed
    virtual WSError TransferBackpressedEventForConsumed(bool& isConsumed) = 0;
    virtual WSError TransferKeyEventForConsumed(const std::shared_ptr<MMI::KeyEvent>& keyEvent, bool& isConsumed,
        bool isPreImeEvent = false) = 0;
    virtual WSError TransferKeyEventForConsumedAsync(const std::shared_ptr<MMI::KeyEvent>& keyEvent, bool isPreImeEvent,
        const sptr<IRemoteObject>& listener) = 0;
    virtual WSError TransferFocusActiveEvent(bool isFocusActive) = 0;
    virtual WSError TransferFocusState(bool focusState) = 0;
    virtual WSError TransferAccessibilityHoverEvent(float pointX, float pointY, int32_t sourceType, int32_t eventType,
        int64_t timeMs) = 0;
    virtual WSError TransferAccessibilityChildTreeRegister(
        uint32_t windowId, int32_t treeId, int64_t accessibilityId) = 0;
    virtual WSError TransferAccessibilityChildTreeUnregister() = 0;
    virtual WSError TransferAccessibilityDumpChildInfo(
        const std::vector<std::string>& params, std::vector<std::string>& info) = 0;
};
} // namespace OHOS::Rosen
#endif // OHOS_WINDOW_SCENE_SESSION_WINDOW_EVENT_CHANNEL_INTERFACE_H
