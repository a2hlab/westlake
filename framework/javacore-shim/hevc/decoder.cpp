#include "codec_api.h"
#include "frames.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <new>
#include <thread>
#include <vector>
#include <multimedia/player_framework/native_avbuffer.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
}

namespace {
constexpr int MaxPixels = 4096 * 2160;
constexpr int Slots = 4;
struct Output {
    OH_AVBuffer* buffer = nullptr;
    AVFrame* frame = nullptr;
    bool held = false;
};
struct Decoder {
    AVCodecContext* context = nullptr;
    OH_AVCodecCallback callbacks{};
    void* user = nullptr;
    std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    bool active = false, draining = false;
    int width = 0, height = 0, inputSize = 0;
    OHNativeWindow* window = nullptr;
    std::array<OH_AVBuffer*, Slots> inputs{};
    std::array<bool, Slots> offered{};
    std::array<Output, Slots> outputs{};
    std::deque<uint32_t> submitted;
    std::vector<uint8_t> pendingConfig;

    OH_AVCodec* handle() { return reinterpret_cast<OH_AVCodec*>(this); }

    void offer(uint32_t index) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!active || draining) return;
            offered[index] = true;
        }
        callbacks.onNeedInputBuffer(handle(), index, inputs[index], user);
    }

    OH_AVFormat* describe() {
        auto format = OH_AVFormat_Create();
        if (format) {
            OH_AVFormat_SetIntValue(format, OH_MD_KEY_WIDTH, width);
            OH_AVFormat_SetIntValue(format, OH_MD_KEY_HEIGHT, height);
            OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_STRIDE, (width + 1) & ~1);
            OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_SLICE_HEIGHT, height);
        }
        return format;
    }

    int emit(AVFrame* frame) {
        std::unique_lock<std::mutex> lock(mutex);
        auto available = [&] {
            return std::find_if(outputs.begin(), outputs.end(), [](const Output& item){return !item.held;});
        };
        changed.wait(lock, [&]{return !active || available() != outputs.end();});
        if (!active) return AVERROR_EXIT;
        auto slot = available();
        int index = int(slot - outputs.begin());
        bool resized = frame && (frame->width != width || frame->height != height);
        if (frame) { width = frame->width; height = frame->height; }
        const int stride = (width + 1) & ~1;
        const int bytes = frame && !window ? stride * (height + (height + 1) / 2) : 0;
        auto& out = *slot;
        if (!out.buffer || OH_AVBuffer_GetCapacity(out.buffer) < bytes) {
            if (out.buffer) OH_AVBuffer_Destroy(out.buffer);
            out.buffer = OH_AVBuffer_Create(std::max(1, bytes));
        }
        if (!out.buffer) return AVERROR(ENOMEM);
        if (bytes && !wl_hevc_copy_nv12(frame, OH_AVBuffer_GetAddr(out.buffer), bytes, stride)) return AVERROR_INVALIDDATA;
        OH_AVCodecBufferAttr attr{frame && frame->pts != AV_NOPTS_VALUE ? frame->pts : 0, bytes, 0,
            frame ? (frame->key_frame ? uint32_t(AVCODEC_BUFFER_FLAGS_SYNC_FRAME) : 0u)
                  : uint32_t(AVCODEC_BUFFER_FLAGS_EOS)};
        if (OH_AVBuffer_SetBufferAttr(out.buffer, &attr) != AV_ERR_OK) return AVERROR_INVALIDDATA;
        if (frame && window) {
            if (!out.frame) out.frame = av_frame_alloc();
            if (!out.frame) return AVERROR(ENOMEM);
            av_frame_move_ref(out.frame, frame);
        }
        out.held = true;
        auto* format = resized ? describe() : nullptr;
        lock.unlock();
        if (format) {
            callbacks.onStreamChanged(handle(), format, user);
            OH_AVFormat_Destroy(format);
        }
        callbacks.onNewOutputBuffer(handle(), index, out.buffer, user);
        return 0;
    }

    int receive(AVFrame* frame) {
        int rc;
        while ((rc = avcodec_receive_frame(context, frame)) == 0) {
            if (frame->format != AV_PIX_FMT_YUV420P || frame->width <= 0 || frame->height <= 0 ||
                int64_t(frame->width + 1) * (frame->height + 1) > MaxPixels + 8193) return AVERROR_INVALIDDATA;
            rc = emit(frame);
            av_frame_unref(frame);
            if (rc < 0) return rc;
        }
        return rc == AVERROR(EAGAIN) ? 0 : rc;
    }

    int consume(uint32_t index, AVPacket* packet, AVFrame* frame) {
        auto buffer = inputs[index];
        OH_AVCodecBufferAttr attr{};
        if (OH_AVBuffer_GetBufferAttr(buffer, &attr) || attr.offset < 0 || attr.size < 0 ||
            attr.size > OH_AVBuffer_GetCapacity(buffer) - attr.offset) return AVERROR_INVALIDDATA;
        if (attr.size > 0) {
            const auto* data = OH_AVBuffer_GetAddr(buffer) + attr.offset;
            if (attr.flags & AVCODEC_BUFFER_FLAGS_CODEC_DATA) {
                // Keep parameter sets until the next access unit, including after a flush.
                if (pendingConfig.size() + attr.size > 1u << 20) return AVERROR_INVALIDDATA;
                pendingConfig.insert(pendingConfig.end(), data, data + attr.size);
            } else {
                int rc = av_new_packet(packet, attr.size);
                if (rc < 0) return rc;
                memcpy(packet->data, data, attr.size);
                if (!pendingConfig.empty()) {
                    auto* extra = av_packet_new_side_data(packet, AV_PKT_DATA_NEW_EXTRADATA, pendingConfig.size());
                    if (!extra) return AVERROR(ENOMEM);
                    memcpy(extra, pendingConfig.data(), pendingConfig.size());
                    pendingConfig.clear();
                }
                packet->pts = attr.pts;
                if (attr.flags & AVCODEC_BUFFER_FLAGS_SYNC_FRAME) packet->flags |= AV_PKT_FLAG_KEY;
                rc = avcodec_send_packet(context, packet);
                av_packet_unref(packet);
                if (rc < 0 || (rc = receive(frame)) < 0) return rc;
            }
        }
        if (attr.flags & AVCODEC_BUFFER_FLAGS_EOS) {
            int rc = avcodec_send_packet(context, nullptr);
            if (rc < 0 || (rc = receive(frame)) != AVERROR_EOF) return rc < 0 ? rc : AVERROR_INVALIDDATA;
            rc = emit(nullptr);
            return rc == 0 ? AVERROR_EOF : rc;
        }
        return 0;
    }

    void run() {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        int rc = packet && frame ? 0 : AVERROR(ENOMEM);
        for (uint32_t i = 0; rc == 0 && i < Slots; ++i) offer(i);
        while (rc == 0) {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [&]{return !active || !submitted.empty();});
            if (!active) break;
            uint32_t index = submitted.front();
            submitted.pop_front();
            lock.unlock();
            rc = consume(index, packet, frame);
            if (!rc) offer(index);
        }
        av_frame_free(&frame);
        av_packet_free(&packet);
        if (rc < 0 && rc != AVERROR_EOF && rc != AVERROR_EXIT)
            callbacks.onError(handle(), rc == AVERROR(ENOMEM) ? AV_ERR_NO_MEMORY : AV_ERR_UNKNOWN, user);
    }

    void stop() {
        { std::lock_guard<std::mutex> lock(mutex); active = false; }
        changed.notify_all();
        if (worker.joinable()) worker.join();
        std::lock_guard<std::mutex> lock(mutex);
        submitted.clear(); pendingConfig.clear(); offered.fill(false); draining = false;
        for (auto& out : outputs) {
            av_frame_free(&out.frame);
            if (out.buffer) OH_AVBuffer_Destroy(out.buffer);
            out = {};
        }
        if (context) avcodec_flush_buffers(context);
    }

    void clear() {
        stop();
        avcodec_free_context(&context);
        for (auto& buffer : inputs) {
            if (buffer) OH_AVBuffer_Destroy(buffer);
            buffer = nullptr;
        }
        if (window) OH_NativeWindow_NativeObjectUnreference(window);
        window = nullptr;
    }
};
Decoder* self(OH_AVCodec* c) { return reinterpret_cast<Decoder*>(c); }
OH_AVCodec* create(const char*) {
    auto* codec = new(std::nothrow) Decoder;
    return codec ? codec->handle() : nullptr;
}
int destroy(OH_AVCodec* c) { self(c)->clear(); delete self(c); return AV_ERR_OK; }
int setCallbacks(OH_AVCodec* c, OH_AVCodecCallback cb, void* user) {
    self(c)->callbacks = cb; self(c)->user = user; return AV_ERR_OK;
}
int configure(OH_AVCodec* c, OH_AVFormat* format) {
    auto* d = self(c);
    d->clear();
    if (!OH_AVFormat_GetIntValue(format, OH_MD_KEY_WIDTH, &d->width) ||
        !OH_AVFormat_GetIntValue(format, OH_MD_KEY_HEIGHT, &d->height) || d->width <= 0 || d->height <= 0 ||
        int64_t(d->width) * d->height > MaxPixels) return AV_ERR_INVALID_VAL;
    int requested = 0;
    OH_AVFormat_GetIntValue(format, OH_MD_KEY_MAX_INPUT_SIZE, &requested);
    d->inputSize = std::max({65536, d->width * d->height * 3 / 2, requested});
    if (d->inputSize > 32 * 1024 * 1024) return AV_ERR_INVALID_VAL;
    auto* decoder = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    auto* ctx = d->context = avcodec_alloc_context3(decoder);
    if (!ctx) return AV_ERR_NO_MEMORY;
    uint8_t* extra = nullptr; size_t size = 0;
    if (OH_AVFormat_GetBuffer(format, OH_MD_KEY_CODEC_CONFIG, &extra, &size) && size) {
        if (!extra || size > 1u << 20) return AV_ERR_INVALID_VAL;
        ctx->extradata = static_cast<uint8_t*>(av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!ctx->extradata) return AV_ERR_NO_MEMORY;
        memcpy(ctx->extradata, extra, size); ctx->extradata_size = int(size);
    }
    ctx->max_pixels = MaxPixels; ctx->pkt_timebase = {1,1000000}; ctx->thread_count = 1;
    return avcodec_open2(ctx, decoder, nullptr) == 0 ? AV_ERR_OK : AV_ERR_UNSUPPORT;
}
int prepare(OH_AVCodec* c) {
    auto* d = self(c);
    for (auto& input : d->inputs) {
        if (!input) input = OH_AVBuffer_Create(d->inputSize);
        if (!input) return AV_ERR_NO_MEMORY;
    }
    return AV_ERR_OK;
}
int start(OH_AVCodec* c) {
    auto* d = self(c);
    std::lock_guard<std::mutex> lock(d->mutex);
    if (d->active || !d->context || !d->inputs[0]) return AV_ERR_INVALID_STATE;
    d->active = true;
    try { d->worker = std::thread(&Decoder::run, d); }
    catch (...) { d->active = false; return AV_ERR_NO_MEMORY; }
    return AV_ERR_OK;
}
int stop(OH_AVCodec* c) { self(c)->stop(); return AV_ERR_OK; }
OH_AVFormat* description(OH_AVCodec* c) {
    auto* d = self(c); std::lock_guard<std::mutex> lock(d->mutex); return d->describe();
}
int push(OH_AVCodec* c, uint32_t index) {
    auto* d = self(c); std::lock_guard<std::mutex> lock(d->mutex);
    if (!d->active || d->draining || index >= Slots || !d->offered[index]) return AV_ERR_INVALID_STATE;
    OH_AVCodecBufferAttr attr{};
    if (OH_AVBuffer_GetBufferAttr(d->inputs[index], &attr)) return AV_ERR_INVALID_VAL;
    if (attr.flags & AVCODEC_BUFFER_FLAGS_EOS) d->draining = true;
    d->offered[index] = false; d->submitted.push_back(index); d->changed.notify_all();
    return AV_ERR_OK;
}
int setSurface(OH_AVCodec* c, OHNativeWindow* window) {
    if (!window || OH_NativeWindow_NativeObjectReference(window)) return AV_ERR_INVALID_VAL;
    auto* d = self(c); std::lock_guard<std::mutex> lock(d->mutex);
    if (d->window) OH_NativeWindow_NativeObjectUnreference(d->window);
    d->window = window; return AV_ERR_OK;
}
int releaseOutput(OH_AVCodec* c, uint32_t index, bool render, int64_t presentationNs) {
    auto* d = self(c);
    std::lock_guard<std::mutex> lock(d->mutex);
    if (index >= Slots || !d->outputs[index].held) return AV_ERR_INVALID_STATE;
    auto& out = d->outputs[index];
    int rc = render && out.frame && out.frame->data[0] ? wl_hevc_render_frame(d->window, out.frame, presentationNs) : 0;
    if (out.frame) av_frame_unref(out.frame);
    out.held = false; d->changed.notify_all();
    return rc == 0 ? AV_ERR_OK : AV_ERR_UNKNOWN;
}
int drop(OH_AVCodec* c, uint32_t index) { return releaseOutput(c, index, false, -1); }
int render(OH_AVCodec* c, uint32_t index) { return releaseOutput(c, index, true, -1); }
int renderTimed(OH_AVCodec* c, uint32_t index, int64_t time) { return releaseOutput(c, index, true, time); }
} // namespace

extern "C" __attribute__((visibility("default"))) const VideoCodecApi* wl_hevc_decoder_v1()
{
    static const VideoCodecApi api = {{create,destroy,setCallbacks,configure,prepare,start,stop,stop,
                                       description,push,drop},setSurface,render,renderTimed};
    return &api;
}
