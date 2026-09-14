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
}
