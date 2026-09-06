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

#ifndef OHOS_WINDOW_SCENE_SESSION_STAGE_INTERFACE_H
#define OHOS_WINDOW_SCENE_SESSION_STAGE_INTERFACE_H

#include <iremote_broker.h>
#include <list>
#include <map>
#include "common/include/window_session_property.h"
#include "focus_notify_info.h"
#include "highlight_notify_info.h"
#include "interfaces/include/ws_common.h"
#include "occupied_area_change_info.h"

namespace OHOS::MMI {
class PointerEvent;
class KeyEvent;
class AxisEvent;
} // namespace MMI
namespace OHOS::Accessibility {
class AccessibilityElementInfo;
}
namespace OHOS::Rosen {
class RSTransaction;
class RSWindowKeyFrameNode;

class ISessionStage : public IRemoteBroker {
public:
    DECLARE_INTERFACE_DESCRIPTOR(u"OHOS.ISessionStage");

    // IPC interface
    virtual WSError SetActive(bool active) = 0;

    /**
     * @brief update the window rect.
     *
     * This function provides the ability for applications to update window rect.
     *
     * @param rect the position and size of the window.
     * @param reason the update reason.
     * @param config the animation parameter configuration.
     * @return Returns WSError::WS_OK if called success, otherwise failed.
     */
    virtual WSError UpdateRect(const WSRect& rect, SizeChangeReason reason,
        const SceneAnimationConfig& config = { nullptr, ROTATE_ANIMATION_DURATION,
            0, WindowAnimationCurve::LINEAR, {0.0f, 0.0f, 0.0f, 0.0f} },
        const std::map<AvoidAreaType, AvoidArea>& avoidAreas = {}) = 0;

    /**
     * @brief Update the window's rectangle in global coordinates from server-side state.
     *
     * This method is invoked internally by the server to synchronize the window's
     * position and size in global coordinates after layout or state changes.
     *
     * @param rect The updated rectangle (position and size) in global coordinates.
     * @param reason The reason for the size or position change.
     * @return WSError::WS_OK if the update succeeds; otherwise, returns the corresponding error code.
     */
    virtual WSError UpdateGlobalDisplayRectFromServer(const WSRect& rect, SizeChangeReason reason)
    {
        return WSError::WS_DO_NOTHING;
    }

    virtual void UpdateDensity() = 0;
    virtual WSError UpdateOrientation() = 0;

    /**
     * @brief Update session viewport config.
     *
     * Called when viewport configuration parameters such as density or orientation changes, update to the UIExtension
     * process.
     *
     * @param config Indicates the {@link SessionViewportConfig} viewport configuration parameters.
     * @return Returns WSError::WS_OK if called success, otherwise failed .
     */
    virtual WSError UpdateSessionViewportConfig(const SessionViewportConfig& config)
    {
        return WSError::WS_OK;
    };
    virtual WSError SendExtensionData(MessageParcel& data, MessageParcel& reply, MessageOption& option)
    {
        return WSError::WS_OK;
    }
    virtual WSError HandleBackEvent() = 0;
    virtual WSError MarkProcessed(int32_t eventId) = 0;

    /**
     * @brief update the focus
     *
     * Notify window session to update focus status.
     *
     * @param focusNotifyInfo focus notify information when shift focus.
     * @param isFocused set isFocused.
     * @return Returns WSError::WS_OK if called success, otherwise failed.
     */
    virtual WSError UpdateFocus(const sptr<FocusNotifyInfo>& focusNotifyInfo, bool isFocused) = 0;
    virtual WSError NotifyDestroy() = 0;
    virtual WSError NotifyExtensionSecureLimitChange(bool isLimit) = 0;
    virtual WSError NotifyHighlightChange(const sptr<HighlightNotifyInfo>& highlightNotifyInfo, bool isHighlight) = 0;

    /**
     * @brief Notify client to close the existing pip window.
     *
     * Called when starting pip but there is already a pip window foreground. The previous one will be destroyed if
     * the new starting request has a higher priority.
     *
     * @return Returns WSError::WS_OK if called success, otherwise failed.
     */
    virtual WSError NotifyCloseExistPipWindow() = 0;
    virtual WSError NotifyTransferComponentData(const AAFwk::WantParams& wantParams) = 0;
    virtual WSErrorCode NotifyTransferComponentDataSync(const AAFwk::WantParams& wantParams,
        AAFwk::WantParams& reWantParams) = 0;
    virtual void NotifyOccupiedAreaChangeInfo(sptr<OccupiedAreaChangeInfo> info,
        const std::shared_ptr<RSTransaction>& rsTransaction, const Rect& callingSessionRect,
        const std::map<AvoidAreaType, AvoidArea>& avoidAreas) = 0;
    virtual WSError UpdateAvoidArea(const sptr<AvoidArea>& avoidArea, AvoidAreaType type) = 0;
    virtual void NotifyScreenshot() = 0;
    virtual WSError NotifyScreenshotAppEvent(ScreenshotEventType type) = 0;
    virtual void DumpSessionElementInfo(const std::vector<std::string>& params) = 0;
    virtual WSError NotifyTouchOutside() = 0;
    virtual WSError NotifyWindowVisibility(bool isVisible) = 0;
    virtual WSError NotifyWindowOcclusionState(const WindowVisibilityState state) = 0;
    virtual WSError UpdateWindowMode(WindowMode mode) = 0;
    virtual WSError GetTopNavDestinationName(std::string& topNavDestName) = 0;
    virtual WSError NotifyLayoutFinishAfterWindowModeChange(WindowMode mode) = 0;
    virtual WSError NotifySubWindowAfterParentWindowSizeChange(Rect rect) = 0;
    virtual WSError NotifySubWindowAfterParentWindowStatusChange(WindowMode mode) = 0;
    virtual WMError UpdateWindowModeForUITest(int32_t updateMode) { return WMError::WM_OK; };
    virtual void NotifyForegroundInteractiveStatus(bool interactive) = 0;
    virtual void NotifyLifecyclePausedStatus() = 0;
    virtual void NotifyAppUseControlStatus(bool isUseControl) = 0;
    virtual WSError UpdateMaximizeMode(MaximizeMode mode) = 0;
    virtual void NotifySessionForeground(uint32_t reason, bool withAnimation) = 0;
    virtual void NotifySessionBackground(uint32_t reason, bool withAnimation, bool isFromInnerkits) = 0;
    virtual WMError GetRouterStackInfo(std::string& routerStackInfo) = 0;
    virtual WSError UpdateTitleInTargetPos(bool isShow, int32_t height) = 0;

    /**
     * @brief Notify transform.
     *
     * Notify transform when window changed.
     *
     * @param transform transform to change.
     */
    virtual void NotifyTransformChange(const Transform& transform) = 0;
    virtual WSError NotifyDialogStateChange(bool isForeground) = 0;

    /**
     * @brief Notify single hand transform.
     *
     * Notify singleHandTransform when single hand mode changed.
     *
     * @param singleHandTransform transform to change.
     */
    virtual void NotifySingleHandTransformChange(const SingleHandTransform& singleHandTransform) = 0;

    /**
     * @brief Notify global scaled rect.
     *
     * Notify client when global scaled rect changed.
     *
     * @param globalScaledRect rect to change.
     */
    virtual void NotifyGlobalScaledRectChange(const Rect& globalScaledRect) {}

    /**
     * @brief Set pip event to client.
     *
     * Set the pip event to client. Such as close, restore, destroy events.
     *
     * @param action Indicates the action name.
     * @param status Indicates the status num.
     * @return Returns WSError::WS_OK if called success, otherwise failed.
     */
    virtual WSError SetPipActionEvent(const std::string& action, int32_t status) = 0;

    /**
     * @brief notify pip size to client.
     *
     * Notify the pip size to client. including width, height and scale.
     *
     * @param width Indicates the size width.
     * @param height Indicates the size height.
     * @param scale Indicates the size scale.
     * @return Returns WSError::WS_OK if called success, otherwise failed.
     */
    virtual WSError NotifyPipWindowSizeChange(double width, double height, double scale) = 0;
    virtual WSError NotifyPiPActiveStatusChange(bool status) = 0;

    /**
     * @brief Set the media control event to client.
     *
     * Set the media control event to client. The event is from pip control panel operation.
     *
     * @param controlType Indicates the {@link WsPiPControlType} component in pip control panel.
     * @param status Indicates the {@link WsPiPControlStatus} required state of specified component.
     * @return Returns WSError::WS_OK if called success, otherwise failed.
     */
    virtual WSError SetPiPControlEvent(WsPiPControlType controlType, WsPiPControlStatus status) = 0;
    virtual WSError UpdateDisplayId(uint64_t displayId) = 0;
    virtual void NotifyDisplayMove(DisplayId from, DisplayId to) = 0;
    virtual WSError SwitchFreeMultiWindow(bool enable) = 0;
    virtual WSError PcAppInPadNormalClose()
    {
        return WSError::WS_OK;
    }
    virtual WSError NotifyCompatibleModePropertyChange(const sptr<CompatibleModeProperty> property)
    {
        return WSError::WS_OK;
    }
    virtual void SetUniqueVirtualPixelRatio(bool useUniqueDensity, float virtualPixelRatio) = 0;
    virtual void UpdateAnimationSpeed(float speed) = 0;
    virtual void NotifySessionFullScreen(bool fullScreen) {}
    virtual WSError GetUIContentRemoteObj(sptr<IRemoteObject>& uiContentRemoteObj) = 0;

    // **Non** IPC interface
    virtual void NotifyBackpressedEvent(bool& isConsumed) {}
    virtual void NotifyPointerEvent(const std::shared_ptr<MMI::PointerEvent>& pointerEvent) {}
    virtual void NotifyKeyEvent(const std::shared_ptr<MMI::KeyEvent>& keyEvent, bool& isConsumed,
        bool notifyInputMethod = true) {}
    virtual void NotifyFocusActiveEvent(bool isFocusActive) {}
    virtual void NotifyFocusStateEvent(bool focusState) {}
    virtual bool NotifyOnKeyPreImeEvent(const std::shared_ptr<MMI::KeyEvent>& keyEvent) {return false;}
    virtual int32_t GetPersistentId() const
    {
        return -1;
    }
    virtual WSError NotifyDensityFollowHost(bool isFollowHost, float densityValue)
    {
        return WSError::WS_OK;
    }
    virtual WSError NotifyAccessibilityHoverEvent(float pointX, float pointY, int32_t sourceType, int32_t eventType,
        int64_t timeMs)
    {
        return WSError::WS_OK;
    }
    virtual WSError NotifyAccessibilityChildTreeRegister(
        uint32_t windowId, int32_t treeId, int64_t accessibilityId)
    {
        return WSError::WS_OK;
    }
    virtual WSError NotifyAccessibilityChildTreeUnregister()
    {
        return WSError::WS_OK;
    }
    virtual WSError NotifyAccessibilityDumpChildInfo(
        const std::vector<std::string>& params, std::vector<std::string>& info)
    {
        return WSError::WS_OK;
    }

    virtual void NotifyKeyboardPanelInfoChange(const KeyboardPanelInfo& keyboardPanelInfo) {}

    virtual WSError NotifyDumpInfo(const std::vector<std::string>& params, std::vector<std::string>& info)
    {
        return WSError::WS_OK;
    }

    virtual WSError SetUIExtensionTransparent()
    {
        return WSError::WS_OK;
    }

    virtual WSError LinkKeyFrameNode() = 0;
    virtual WSError SetStageKeyFramePolicy(const KeyFramePolicy& keyFramePolicy) = 0;

    virtual WSError SetSplitButtonVisible(bool isVisible) = 0;

    virtual WSError SetEnableDragBySystem(bool dragEnable) = 0;

    virtual WSError SetDragActivated(bool dragActivated) = 0;

    virtual WSError SetFullScreenWaterfallMode(bool isWaterfallMode) { return WSError::WS_DO_NOTHING; }
    virtual WSError SetSupportEnterWaterfallMode(bool isSupportEnter) { return WSError::WS_DO_NOTHING; }
    virtual WSError SendContainerModalEvent(const std::string& eventName, const std::string& eventValue) = 0;
    virtual void NotifyWindowCrossAxisChange(CrossAxisState state) = 0;
    virtual WSError NotifyWindowAttachStateChange(bool isAttach) { return WSError::WS_DO_NOTHING; }
    virtual void NotifyKeyboardAnimationCompleted(const KeyboardPanelInfo& keyboardPanelInfo) {}
    virtual WSError SetCurrentRotation(int32_t currentRotation) = 0;
    virtual WSError GetSceneNodeCount(uint32_t& nodeCount) = 0;
    virtual void NotifyKeyboardAnimationWillBegin(const KeyboardAnimationInfo& keyboardAnimationInfo,
        const std::shared_ptr<RSTransaction>& rsTransaction) {};
    virtual WSError NotifyTargetRotationInfo(OrientationInfo& info, OrientationInfo& currentInfo)
    {
        return WSError::WS_DO_NOTHING;
    }
    virtual WSError NotifyPageRotationIsIgnored() { return WSError::WS_DO_NOTHING; }
    virtual RotationChangeResult NotifyRotationChange(const RotationChangeInfo& rotationChangeInfo)
    {
        return { RectType::RELATIVE_TO_SCREEN, { 0, 0, 0, 0, } };
    }
    virtual WSError NotifyAppForceLandscapeConfigUpdated() = 0;
    virtual WSError NotifyAppForceLandscapeConfigEnableUpdated(bool needUpdateViewport = false) = 0;
    virtual WSError NotifyAppHookWindowInfoUpdated() = 0;
    virtual WSError UpdateAppHookWindowInfo(const HookWindowInfo& hookWindowInfo) = 0;
    virtual WSError CloseSpecificScene() { return WSError::WS_DO_NOTHING; }
    virtual WSError UpdateBrightness(float brightness) = 0;

    /**
     * @brief Send fb event to client.
     *
     * Send the fb event to client. Such as close, click events.
     *
     * @param action Indicates the action name.
     * @return Returns WSError::WS_OK if called success, otherwise failed.
     */
    virtual WSError SendFbActionEvent(const std::string& action) = 0;

    /**
     * @brief update if show decor in free multi window.
     *
     * update decor show status. Such as true or false.
     *
     * @param isShow Indicates if the decor show.
     * @return Returns WSError::WS_OK if called success, otherwise failed.
     */
    virtual WSError UpdateIsShowDecorInFreeMultiWindow(bool isShow) = 0;

    /**
     * @brief App window sidebar blur.
     *
     * add window siderBar blur effect.
     *
     * @return Returns WSError::WS_OK if called success, otherwise failed.
     */
    virtual WSError AddSidebarBlur() { return WSError::WS_DO_NOTHING; }

    /**
     * @brief Set sidebar blur style with type.
     *
     * Set sidebar blur style with type.
     *
     * @param type Indicates the action type.
     * @return Returns WSError::WS_OK if called success, otherwise failed.
     */
    virtual WSError SetSidebarBlurStyleWithType(SidebarBlurType type) { return WSError::WS_DO_NOTHING; }

    /**
     * @brief Update window UIType when recover.
     *
     * Update window UIType when recover.
     *
     * @param windowUIType Window UIType.
     * @return Returns WSError::WS_OK if called success, otherwise failed.
     */
    virtual WSError UpdateWindowUIType(WindowUIType windowUIType)
    {
        return WSError::WS_OK;
    }

    /**
     * @brief Update session property when trigger mode.
     *
     * Update session property when trigger mode.
     *
     * @param property Window session property.
     * @return Returns WSError::WS_OK if called success, otherwise failed.
     */
    virtual WSError UpdatePropertyWhenTriggerMode(const sptr<WindowSessionProperty>& property)
    {
        return WSError::WS_OK;
    }

    /**
    * @brief Notify parent lifecycle event to subwindow.
    *
    * Notify subwindow about parent window lifecycle changes (foreground, background, destroy).
    *
    * @param eventType Indicates the lifecycle event (foreground/background/destroy).
    * @return Returns WSError::WS_OK if called success, otherwise failed.
    */
    virtual WSError NotifyParentLifecycleEvent(ParentLifeCycleEvent eventType)
    {
        return WSError::WS_OK;
    }
};
} // namespace OHOS::Rosen
#endif // OHOS_WINDOW_SCENE_SESSION_STAGE_INTERFACE_H
