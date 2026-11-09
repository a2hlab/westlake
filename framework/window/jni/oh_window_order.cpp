// A sub-window's z-level among the sub-windows of its parent, for the sessions the bridge creates.
//
// Every Android window here is an OH sub-window of the host's window, stacked by creation order and
// RaiseToAppTop. Android stacks a SurfaceView's layer below its window unless the app asks for it on
// top, so WindowSessionAdapter gives a SurfaceView's own session the SurfaceView's sublayer as its
// z-level (-2 by default). OH orders sibling sub-windows by z-level, then by order (SceneBoard's
// onSubSessionZLevelChange). Sent as the official SessionProxy::UpdateSessionPropertyByAction
// request with ACTION_UPDATE_SUB_WINDOW_Z_LEVEL, which needs no system permission.
#include "oh_window_manager_client.h"
#include "message_parcel.h"
#include "message_option.h"
#include "session/host/include/zidl/session_ipc_interface_code.h"
#include "ws_common_inner.h"
#include <cerrno>
#include <dlfcn.h>

namespace oh_adapter {
int32_t OHWindowManagerClient::setSubWindowZLevel(int32_t sessionId, int32_t zLevel) {
#ifdef OH_WINDOW_POLICY_OVERLAY
    // Checked against the pinned bridge's getter/teardown disassembly, as in oh_window_policy.cpp.
    static_assert(__builtin_offsetof(OHWindowManagerClient, sessions_) == 24);
    static_assert(__builtin_offsetof(OHWindowManagerClient, sessionMutex_) == 48);
    static_assert(__builtin_offsetof(SessionEntry, sessionRemote) == 24);
#endif
    OHOS::sptr<OHOS::IRemoteObject> remote;
    {
        std::lock_guard<std::recursive_mutex> lock(sessionMutex_);
        auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) return -ENOENT;
        remote = it->second.sessionRemote;
    }
    if (!remote) return -ENOTSUP;
    // WindowSessionProperty::Write: the persistent id, then WriteActionUpdateSubWindowZLevel's int32.
    // Asynchronous, as the official proxy sends it: success means submitted.
    OHOS::MessageParcel data, reply;
    OHOS::MessageOption option(OHOS::MessageOption::TF_ASYNC);
    const auto action = OHOS::Rosen::WSPropertyChangeAction::ACTION_UPDATE_SUB_WINDOW_Z_LEVEL;
    if (!data.WriteInterfaceToken(u"OHOS.ISession") ||
            !data.WriteUint64(static_cast<uint64_t>(action)) ||
            !data.WriteBool(true) || !data.WriteUint32(sessionId) ||
            !data.WriteInt32(zLevel)) return -EIO;
    return remote->SendRequest(static_cast<uint32_t>(
        OHOS::Rosen::SessionInterfaceCode::TRANS_ID_UPDATE_SESSION_PROPERTY), data, reply, option);
}
}
extern "C" JNIEXPORT jint JNICALL Java_adapter_window_OhWindowOrder_nativeSetSubWindowZLevel(
        JNIEnv*, jclass, jint session, jint zLevel) {
#ifdef OH_WINDOW_POLICY_OVERLAY
    using Get = oh_adapter::OHWindowManagerClient& (*)();
    static auto get = reinterpret_cast<Get>(dlsym(RTLD_DEFAULT,
        "_ZN10oh_adapter21OHWindowManagerClient11getInstanceEv"));
    if (!get) return -ENOSYS;
    return get().setSubWindowZLevel(session, zLevel);
#else
    return oh_adapter::OHWindowManagerClient::getInstance().setSubWindowZLevel(session, zLevel);
#endif
}
