// Build Android's complete MessageQueue JNI over its source-built libutils
// Looper. Only posting work from the OH window/input boundary is added here.
#include "android_os_MessageQueue.cpp"

namespace android {
namespace {
class OHPostedMessage final : public MessageHandler {
public:
    OHPostedMessage(void (*callback)(void*), void* data) : callback_(callback), data_(data) {}
    void handleMessage(const Message&) override { callback_(data_); }
private:
    void (*callback_)(void*);
    void* data_;
};
}

// The caller owns a live Java MessageQueue and passes its mPtr. Posting into
// that queue's native Looper preserves wakeups and bypasses Java sync barriers,
// including when an OH callback arrives from a different thread.
extern "C" int wl_MessageQueue_post(void* pointer, void (*callback)(void*), void* data) {
    if (!pointer || !callback) return -1;
    auto* queue = static_cast<NativeMessageQueue*>(pointer);
    sp<Looper> looper = queue->getLooper();
    looper->sendMessage(sp<OHPostedMessage>::make(callback, data), Message());
    return 0;
}

// Private ABI used by the Android-NDK shim loaded for WebView and app-native
// libraries.  The shim deliberately resolves these through the already-loaded
// liboh_adapter_bridge.so handle.  That bridge has liboh_android_runtime.so as
// a direct dependency, so dlsym(handle, ...) searches this provider without
// duplicating MessageQueue/Looper ownership in the OH adapter layer.
//
// Returning Looper::getForThread() is the important part of this boundary: on
// an Android Java thread NativeMessageQueue installed that same object in TLS,
// so descriptors registered through ALooper_addFd are dispatched by Java's
// nativePollOnce.  A native-only thread instead gets the normal AOSP Looper
// created by Looper::prepare and can poll it through the ordinary public NDK
// API.
extern "C" __attribute__((visibility("default"))) void* wl_ALooper_prepare(int opts) {
    sp<Looper> looper = Looper::prepare(opts);
    return looper.get();
}

extern "C" __attribute__((visibility("default"))) int wl_ALooper_addFd(
        void* pointer, int fd, int ident, int events,
        Looper_callbackFunc callback, void* data) {
    auto* looper = static_cast<Looper*>(pointer);
    return looper != nullptr ? looper->addFd(fd, ident, events, callback, data) : -1;
}

extern "C" __attribute__((visibility("default"))) int wl_ALooper_removeFd(
        void* pointer, int fd) {
    auto* looper = static_cast<Looper*>(pointer);
    return looper != nullptr ? looper->removeFd(fd) : -1;
}

extern "C" __attribute__((visibility("default"))) void wl_ALooper_wake(void* pointer) {
    auto* looper = static_cast<Looper*>(pointer);
    if (looper != nullptr) looper->wake();
}
}
