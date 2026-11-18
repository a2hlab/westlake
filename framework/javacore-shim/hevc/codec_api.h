#pragma once

#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_avcapability.h>
#include <native_window/external_window.h>

// Common operations exposed by OpenHarmony codecs and the optional software decoder.
struct CodecApi {
    OH_AVCodec* (*createByName)(const char*);
    int (*destroy)(OH_AVCodec*);
    int (*registerCallback)(OH_AVCodec*, OH_AVCodecCallback, void*);
    int (*configure)(OH_AVCodec*, OH_AVFormat*);
    int (*prepare)(OH_AVCodec*);
    int (*start)(OH_AVCodec*);
    int (*stop)(OH_AVCodec*);
    int (*flush)(OH_AVCodec*);
    OH_AVFormat* (*outputDescription)(OH_AVCodec*);
    int (*pushInput)(OH_AVCodec*, uint32_t);
    int (*freeOutput)(OH_AVCodec*, uint32_t);
};

struct VideoCodecApi : CodecApi {
    int (*setSurface)(OH_AVCodec*, OHNativeWindow*);
    int (*renderOutput)(OH_AVCodec*, uint32_t);
    int (*renderAtTime)(OH_AVCodec*, uint32_t, int64_t);
};
