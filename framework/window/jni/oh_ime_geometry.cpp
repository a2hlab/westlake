// Narrow reverse-callback overlay for the pinned OH bridge. Use its actual
// declarations so the compiler emits the correct ISessionStage adjustment.
#include "session_stage_adapter.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
namespace oh_adapter {
void SessionStageAdapter::NotifyOccupiedAreaChangeInfo(
        OHOS::sptr<OHOS::Rosen::OccupiedAreaChangeInfo> info,
        const std::shared_ptr<OHOS::Rosen::RSTransaction>&,
        const OHOS::Rosen::Rect&,
        const std::map<OHOS::Rosen::AvoidAreaType,OHOS::Rosen::AvoidArea>&) {
    // Pinned bridge uses this+160 for its lock, and info+16 for the rectangle.
    // Reject a checkout/toolchain whose declarations no longer match that ABI.
    static_assert(__builtin_offsetof(SessionStageAdapter,jniMutex_) == 160);
    static_assert(__builtin_offsetof(OHOS::Rosen::OccupiedAreaChangeInfo,rect_) == 16);
    OHOS::Rosen::Rect rect = info ? info->rect_ : OHOS::Rosen::Rect{};
    std::lock_guard<std::mutex> lock(jniMutex_);
    // Do not DT_NEEDED the full bridge from a preloaded callback DSO: that
    // would pull HWUI/Skia in before their existing startup compatibility
    // boundary. Resolve the already-created object's method when invoked.
    using Call = void (*)(SessionStageAdapter*, const char*, const char*, ...);
    static auto call = reinterpret_cast<Call>(dlsym(RTLD_DEFAULT,
        "_ZN10oh_adapter19SessionStageAdapter20callBridgeVoidMethodEPKcS2_z"));
    if (!call) { fprintf(stderr,"[OH_IME_GEOMETRY] bridge callback unavailable\n"); std::abort(); }
    call(this, "onOccupiedAreaRect", "(IIII)V", static_cast<jint>(rect.posX_),
        static_cast<jint>(rect.posY_),static_cast<jint>(rect.width_),static_cast<jint>(rect.height_));
    fprintf(stderr,"[OH_IME_GEOMETRY] occupied=%d,%d %u,%u\n",rect.posX_,rect.posY_,rect.width_,rect.height_);
}
}
