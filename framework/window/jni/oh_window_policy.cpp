#include "oh_window_manager_client.h"
#include "message_parcel.h"
#include "message_option.h"
#include "session/host/include/zidl/session_ipc_interface_code.h"
#include "ws_common_inner.h"
#include <cerrno>
#include <dlfcn.h>

namespace oh_adapter {
int32_t OHWindowManagerClient::setKeepScreenOn(int32_t sessionId, bool enabled) {
#ifdef OH_WINDOW_POLICY_OVERLAY
    // Checked against the pinned bridge's getter/teardown disassembly.
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
    // Official SessionProxy/WindowSessionProperty wire format. OH uses async
    // IPC here; success means submitted, not read back.
    OHOS::MessageParcel data, reply;
    OHOS::MessageOption option(OHOS::MessageOption::TF_ASYNC);
    const auto action = OHOS::Rosen::WSPropertyChangeAction::ACTION_UPDATE_KEEP_SCREEN_ON;
    if (!data.WriteInterfaceToken(u"OHOS.ISession") ||
            !data.WriteUint64(static_cast<uint64_t>(action)) ||
            !data.WriteBool(true) || !data.WriteUint32(sessionId) ||
            !data.WriteBool(enabled)) return -EIO;
    return remote->SendRequest(static_cast<uint32_t>(
        OHOS::Rosen::SessionInterfaceCode::TRANS_ID_UPDATE_SESSION_PROPERTY), data, reply, option);
}
}
extern "C" JNIEXPORT jint JNICALL Java_adapter_window_OhWindowPolicy_nativeSetKeepScreenOn(
        JNIEnv*, jclass, jint session, jboolean enabled) {
#ifdef OH_WINDOW_POLICY_OVERLAY
    using Get = oh_adapter::OHWindowManagerClient& (*)();
    static auto get = reinterpret_cast<Get>(dlsym(RTLD_DEFAULT,
        "_ZN10oh_adapter21OHWindowManagerClient11getInstanceEv"));
    if (!get) return -ENOSYS;
    return get().setKeepScreenOn(session, enabled == JNI_TRUE);
#else
    return oh_adapter::OHWindowManagerClient::getInstance().setKeepScreenOn(session, enabled == JNI_TRUE);
#endif
}
