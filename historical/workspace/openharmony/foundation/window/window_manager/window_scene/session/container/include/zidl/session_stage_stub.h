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

#ifndef OHOS_WINDOW_SCENE_SESSION_STAGE_STUB_H
#define OHOS_WINDOW_SCENE_SESSION_STAGE_STUB_H

#include <map>

#include <iremote_stub.h>
#include <feature/window_keyframe/rs_window_keyframe_node.h>

#include "session/container/include/zidl/session_stage_interface.h"

namespace OHOS::Rosen {
class SessionStageStub;

class SessionStageStub : public IRemoteStub<ISessionStage> {
public:
    SessionStageStub() = default;
    ~SessionStageStub() = default;

    int OnRemoteRequest(uint32_t code, MessageParcel& data, MessageParcel& reply,
        MessageOption& option) override;

private:
    int HandleSetActive(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateRect(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateGlobalDisplayRectFromServer(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateDensity(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateOrientation(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateSessionViewportConfig(MessageParcel& data, MessageParcel& reply);
    int HandleBackEventInner(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyDestroy(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateFocus(MessageParcel& date, MessageParcel& reply);
    int HandleNotifyTransferComponentData(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyTransferComponentDataSync(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyOccupiedAreaChange(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateAvoidArea(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyScreenshot(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyScreenshotAppEvent(MessageParcel& data, MessageParcel& reply);
    int HandleDumpSessionElementInfo(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyTouchOutside(MessageParcel& data, MessageParcel& reply);
    int HandleNotifySecureLimitChange(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateWindowMode(MessageParcel& data, MessageParcel& reply);
    int HandleGetTopNavDestinationName(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyLayoutFinishAfterWindowModeChange(MessageParcel& data, MessageParcel& reply);
    int HandleNotifySubWindowAfterParentWindowSizeChange(MessageParcel& data, MessageParcel& reply);
    int HandleNotifySubWindowAfterParentWindowStatusChange(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateWindowModeForUITest(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyForegroundInteractiveStatus(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyPausedStatus();
    int HandleNotifyAppUseControlStatus(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateMaximizeMode(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyCloseExistPipWindow(MessageParcel& data, MessageParcel& reply);
    int HandleNotifySessionForeground(MessageParcel& data, MessageParcel& reply);
    int HandleNotifySessionBackground(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateTitleInTargetPos(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyDensityFollowHost(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyWindowVisibilityChange(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyWindowOcclusionState(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyTransformChange(MessageParcel& data, MessageParcel& reply);
    int HandleNotifySingleHandTransformChange(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyGlobalScaledRectChange(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyDialogStateChange(MessageParcel& data, MessageParcel& reply);
    int HandleSetPipActionEvent(MessageParcel& data, MessageParcel& reply);
    int HandleSetPiPControlEvent(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateDisplayId(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyDisplayMove(MessageParcel& data, MessageParcel& reply);
    int HandleSwitchFreeMultiWindow(MessageParcel& data, MessageParcel& reply);
    int HandleGetUIContentRemoteObj(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyKeyboardPanelInfoChange(MessageParcel& data, MessageParcel& reply);
    int HandleSetUniqueVirtualPixelRatio(MessageParcel& data, MessageParcel& reply);
    int HandlePcAppInPadNormalClose(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyCompatibleModePropertyChange(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateAnimationSpeed(MessageParcel& data, MessageParcel& reply);
    int HandleNotifySessionFullScreen(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyDumpInfo(MessageParcel& data, MessageParcel& reply);
    int HandleSetUIExtensionTransparent(MessageParcel& data, MessageParcel& reply);
    int HandleExtensionHostData(MessageParcel& data, MessageParcel& reply, MessageOption& option);
    int HandleLinkKeyFrameNode(MessageParcel& data, MessageParcel& reply);
    int HandleSetStageKeyFramePolicy(MessageParcel& data, MessageParcel& reply);
    int HandleSetDragActivated(MessageParcel& data, MessageParcel& reply);
    int HandleSetSplitButtonVisible(MessageParcel& data, MessageParcel& reply);
    int HandleSetEnableDragBySystem(MessageParcel& data, MessageParcel& reply);
    int HandleSetFullScreenWaterfallMode(MessageParcel& data, MessageParcel& reply);
    int HandleSetSupportEnterWaterfallMode(MessageParcel& data, MessageParcel& reply);
    int HandleSendContainerModalEvent(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyPipSizeChange(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyHighlightChange(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyWindowCrossAxisChange(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyPiPActiveStatusChange(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyRotationProperty(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyPageRotationIsIgnored(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyWindowAttachStateChange(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyKeyboardAnimationCompleted(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyKeyboardAnimationWillBegin(MessageParcel& data, MessageParcel& reply);
    int HandleSetCurrentRotation(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyRotationChange(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyAppForceLandscapeConfigUpdated(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyAppForceLandscapeConfigEnableUpdated(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyAppHookWindowInfoUpdated(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateAppHookWindowInfo(MessageParcel& data, MessageParcel& reply);
    int HandleGetRouterStackInfo(MessageParcel& data, MessageParcel& reply);
    int HandleGetSceneNodeCount(MessageParcel& data, MessageParcel& reply);
    int HandleCloseSpecificScene(MessageParcel& data, MessageParcel& reply);
    int HandleSendFbActionEvent(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateIsShowDecorInFreeMultiWindow(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateBrightness(MessageParcel& data, MessageParcel& reply);
    int HandleAddSidebarBlur(MessageParcel& data, MessageParcel& reply);
    int HandleSetSidebarBlurStyleWithType(MessageParcel& data, MessageParcel& reply);
    int HandleUpdateWindowUIType(MessageParcel& data, MessageParcel& reply);
    int HandleUpdatePropertyWhenTriggerMode(MessageParcel& data, MessageParcel& reply);
    int HandleNotifyParentLifecycleEvent(MessageParcel& data, MessageParcel& reply);
};
} // namespace OHOS::Rosen
#endif // OHOS_WINDOW_SCENE_SESSION_STAGE_STUB_H
