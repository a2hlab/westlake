// Ability-owned window input. Build with device-matching OHOS inner headers
// and libc++ __h; never with Rosen's portable EventHandler lookalike.
#include <event_handler.h>
#include <event_runner.h>
#include <i_input_event_consumer.h>
#include <input_manager.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>

namespace {
using namespace OHOS;
using GetBridge = void* (*)();
using Touch = int32_t (*)(void*, int32_t, float, float, int64_t, int64_t);
using Key = int32_t (*)(void*, int32_t, int32_t, int64_t, int64_t, int32_t);
GetBridge bridge;
Touch touch;
Key key;
std::mutex mutex;
std::set<int32_t> windows;
std::shared_ptr<AppExecFwk::EventHandler> handler;
std::shared_ptr<MMI::IInputEventConsumer> consumer;

bool owns(int id) {
    std::lock_guard<std::mutex> guard(mutex);
    return windows.count(id) != 0;
}
class Consumer final : public MMI::IInputEventConsumer {
    mutable int64_t downNs = 0;
public:
    void OnInputEvent(std::shared_ptr<MMI::PointerEvent> event) const override {
        if (!event) return;
        if (!owns(event->GetTargetWindowId())) { event->MarkProcessed(); return; }
        int action = -1;
        switch (event->GetPointerAction()) {
            case MMI::PointerEvent::POINTER_ACTION_DOWN: action = 0; break;
            case MMI::PointerEvent::POINTER_ACTION_UP: action = 1; break;
            case MMI::PointerEvent::POINTER_ACTION_MOVE: action = 2; break;
            case MMI::PointerEvent::POINTER_ACTION_CANCEL: action = 3; break;
        }
        MMI::PointerEvent::PointerItem item;
        if (action >= 0 && event->GetPointerItem(event->GetPointerId(), item)) {
            const int64_t time = event->GetActionTime() * 1000LL;
            if (action == 0) downNs = time;
            const float x = item.GetWindowX(), y = item.GetWindowY();
            const int rc = touch(bridge(), action, x, y, downNs ? downNs : time, time);
            if (action != 2) std::fprintf(stderr,
                "[OH_OWNED_INPUT] window=%d action=%d x=%.0f y=%.0f rc=%d\n",
                event->GetTargetWindowId(), action, x, y, rc);
            if (action == 1 || action == 3) downNs = 0;
        }
        event->MarkProcessed();
    }
    void OnInputEvent(std::shared_ptr<MMI::KeyEvent> event) const override {
        if (!event) return;
        if (owns(event->GetTargetWindowId())) {
            int androidKey = -1;
            const int code = event->GetKeyCode();
            if (code == MMI::KeyEvent::KEYCODE_BACK) androidKey = 4;
            else if (code == MMI::KeyEvent::KEYCODE_ENTER) androidKey = 66;
            else if (code == MMI::KeyEvent::KEYCODE_DEL) androidKey = 67;
            else if (code == MMI::KeyEvent::KEYCODE_DPAD_LEFT) androidKey = 21;
            else if (code == MMI::KeyEvent::KEYCODE_DPAD_RIGHT) androidKey = 22;
            const int action = event->GetKeyAction();
            if (androidKey >= 0 && (action == MMI::KeyEvent::KEY_ACTION_DOWN ||
                                  action == MMI::KeyEvent::KEY_ACTION_UP)) {
                const int64_t time = event->GetActionTime() * 1000LL;
                key(bridge(), action == MMI::KeyEvent::KEY_ACTION_UP ? 1 : 0,
                    androidKey, time, time, 0);
            }
        }
        event->MarkProcessed();
    }
    void OnInputEvent(std::shared_ptr<MMI::AxisEvent> event) const override {
        if (event) event->MarkProcessed();
    }
};
}

extern "C" int oh_owned_input_subscribe(int32_t session) {
    if (!std::getenv("WL_ABILITY_OWNED_WINDOWS") || !std::getenv("WL_SUB_WINDOW") || session <= 0) return -1;
    std::lock_guard<std::mutex> guard(mutex);
    if (consumer) { windows.insert(session); return 0; }
    bridge = reinterpret_cast<GetBridge>(dlsym(RTLD_DEFAULT, "_ZN10oh_adapter13OHInputBridge11getInstanceEv"));
    touch = reinterpret_cast<Touch>(dlsym(RTLD_DEFAULT,
        "_ZN10oh_adapter13OHInputBridge30dispatchSingleTouchViaViewRootEiffll"));
    key = reinterpret_cast<Key>(dlsym(RTLD_DEFAULT,
        "_ZN10oh_adapter13OHInputBridge22dispatchKeyViaViewRootEiilli"));
    if (!bridge || !touch || !key) { std::fprintf(stderr, "[OH_OWNED_INPUT] missing bridge export\n"); return -2; }
    auto runner = AppExecFwk::EventRunner::Create("AbilityOwnedInput");
    if (!runner) return -3;
    handler = std::make_shared<AppExecFwk::EventHandler>(runner);
    auto candidate = std::make_shared<Consumer>();
    const int rc = MMI::InputManager::GetInstance()->SetWindowInputEventConsumer(candidate, handler);
    std::fprintf(stderr, "[OH_OWNED_INPUT] subscribe window=%d rc=%d handler_size=%zu\n",
        session, rc, sizeof(AppExecFwk::EventHandler));
    if (rc == 0) { consumer = candidate; windows.insert(session); }
    return rc;
}

// Narrow opt-in entry for the pinned bridge binary. The source bridge uses
// oh_owned_input_subscribe directly when this boundary helper is loaded.
#ifdef WESTLAKE_PINNED_INPUT_ENTRY
extern "C" void pinned_subscribe(void*, int32_t)
    __asm__("_ZN10oh_adapter13OHInputBridge12subscribeMmiEi");
extern "C" void pinned_subscribe(void* self, int32_t session) {
    if (!std::getenv("WL_ABILITY_OWNED_WINDOWS")) {
        auto original = reinterpret_cast<void (*)(void*, int32_t)>(dlsym(RTLD_NEXT,
            "_ZN10oh_adapter13OHInputBridge12subscribeMmiEi"));
        if (!original) std::abort();
        original(self, session);
        return;
    }
    for (const char* name : {
        "_ZN10oh_adapter13OHInputBridge22startTapControlChannelEv",
        "_ZN10oh_adapter13OHInputBridge23startTextControlChannelEv"}) {
        auto start = reinterpret_cast<void (*)(void*)>(dlsym(RTLD_DEFAULT, name));
        if (start) start(self);
    }
    oh_owned_input_subscribe(session);
}
#endif
