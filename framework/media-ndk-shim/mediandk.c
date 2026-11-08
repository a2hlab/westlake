/*
 * libmediandk.so: the NDK media API of a runtime with no NDK codec, extractor, muxer, DRM or
 * image reader behind it.
 *
 * Nothing carries this API to OH's media services yet (AVCodec, AVDemuxer, AVMuxer, DRM,
 * ImageReceiver). Without the library, though, a library that names libmediandk.so in DT_NEEDED
 * does not load at all, whatever it would have used it for: Godot's engine library (for its
 * camera feed), and libraries Spotify and others extract at run time. This library lets them load
 * and tells them the truth when they ask: no codec can be created, a muxer or DRM session cannot be
 * opened, an extractor accepts no data source, and an image reader is not supported. Each of those
 * is an answer the NDK documents, and callers handle it: a decoder that cannot be created is the
 * usual signal to fall back to software decoding.
 *
 * AMediaFormat is the exception. It is a plain key/value container in AOSP too, with no service
 * behind it, so it is implemented in full: values set are read back. So are the other plain-data
 * objects, codec crypto info and an app's data source callbacks.
 *
 * Every symbol AOSP's libmediandk.so exports is here (libmediandk.map.txt): a library that names
 * one that is missing does not load at all.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define EXPORT __attribute__((visibility("default")))

/* NdkMediaError.h */
typedef int media_status_t;
#define AMEDIA_OK 0
#define AMEDIA_ERROR_UNKNOWN (-10000)
#define AMEDIA_ERROR_UNSUPPORTED (-10002)
#define AMEDIA_ERROR_INVALID_OBJECT (-10003)
#define AMEDIA_ERROR_INVALID_PARAMETER (-10004)
#define AMEDIA_ERROR_INVALID_OPERATION (-10005)
#define AMEDIA_ERROR_END_OF_STREAM (-10006)

/* --- AMediaFormat keys: AOSP's values (NdkMediaFormat.cpp) --------------------------------- */

EXPORT const char* AMEDIAFORMAT_KEY_AAC_PROFILE = "aac-profile";
EXPORT const char* AMEDIAFORMAT_KEY_BIT_RATE = "bitrate";
EXPORT const char* AMEDIAFORMAT_KEY_BITRATE_MODE = "bitrate-mode";
EXPORT const char* AMEDIAFORMAT_KEY_CAPTURE_RATE = "capture-rate";
EXPORT const char* AMEDIAFORMAT_KEY_CHANNEL_COUNT = "channel-count";
EXPORT const char* AMEDIAFORMAT_KEY_CHANNEL_MASK = "channel-mask";
EXPORT const char* AMEDIAFORMAT_KEY_COLOR_FORMAT = "color-format";
EXPORT const char* AMEDIAFORMAT_KEY_COLOR_RANGE = "color-range";
EXPORT const char* AMEDIAFORMAT_KEY_COLOR_STANDARD = "color-standard";
EXPORT const char* AMEDIAFORMAT_KEY_COLOR_TRANSFER = "color-transfer";
EXPORT const char* AMEDIAFORMAT_KEY_CSD_0 = "csd-0";
EXPORT const char* AMEDIAFORMAT_KEY_CSD_1 = "csd-1";
EXPORT const char* AMEDIAFORMAT_KEY_CSD_2 = "csd-2";
EXPORT const char* AMEDIAFORMAT_KEY_DISPLAY_HEIGHT = "display-height";
EXPORT const char* AMEDIAFORMAT_KEY_DISPLAY_WIDTH = "display-width";
EXPORT const char* AMEDIAFORMAT_KEY_DURATION = "durationUs";
EXPORT const char* AMEDIAFORMAT_KEY_ENCODER_DELAY = "encoder-delay";
EXPORT const char* AMEDIAFORMAT_KEY_ENCODER_PADDING = "encoder-padding";
EXPORT const char* AMEDIAFORMAT_KEY_FRAME_RATE = "frame-rate";
EXPORT const char* AMEDIAFORMAT_KEY_HEIGHT = "height";
EXPORT const char* AMEDIAFORMAT_KEY_I_FRAME_INTERVAL = "i-frame-interval";
EXPORT const char* AMEDIAFORMAT_KEY_INTRA_REFRESH_PERIOD = "intra-refresh-period";
EXPORT const char* AMEDIAFORMAT_KEY_IS_ADTS = "is-adts";
EXPORT const char* AMEDIAFORMAT_KEY_IS_SYNC_FRAME = "is-sync-frame";
EXPORT const char* AMEDIAFORMAT_KEY_LANGUAGE = "language";
EXPORT const char* AMEDIAFORMAT_KEY_LATENCY = "latency";
EXPORT const char* AMEDIAFORMAT_KEY_LEVEL = "level";
EXPORT const char* AMEDIAFORMAT_KEY_LOW_LATENCY = "low-latency";
EXPORT const char* AMEDIAFORMAT_KEY_MAX_HEIGHT = "max-height";
EXPORT const char* AMEDIAFORMAT_KEY_MAX_INPUT_SIZE = "max-input-size";
EXPORT const char* AMEDIAFORMAT_KEY_MAX_WIDTH = "max-width";
EXPORT const char* AMEDIAFORMAT_KEY_MIME = "mime";
EXPORT const char* AMEDIAFORMAT_KEY_OPERATING_RATE = "operating-rate";
EXPORT const char* AMEDIAFORMAT_KEY_PCM_ENCODING = "pcm-encoding";
EXPORT const char* AMEDIAFORMAT_KEY_PRIORITY = "priority";
EXPORT const char* AMEDIAFORMAT_KEY_PROFILE = "profile";
EXPORT const char* AMEDIAFORMAT_KEY_PUSH_BLANK_BUFFERS_ON_STOP = "push-blank-buffers-on-shutdown";
EXPORT const char* AMEDIAFORMAT_KEY_REPEAT_PREVIOUS_FRAME_AFTER = "repeat-previous-frame-after";
EXPORT const char* AMEDIAFORMAT_KEY_ROTATION = "rotation-degrees";
EXPORT const char* AMEDIAFORMAT_KEY_SAMPLE_RATE = "sample-rate";
EXPORT const char* AMEDIAFORMAT_KEY_SLICE_HEIGHT = "slice-height";
EXPORT const char* AMEDIAFORMAT_KEY_STRIDE = "stride";
EXPORT const char* AMEDIAFORMAT_KEY_TIME_US = "timeUs";
EXPORT const char* AMEDIAFORMAT_KEY_TRACK_ID = "track-id";
EXPORT const char* AMEDIAFORMAT_KEY_WIDTH = "width";

/* The rest of the keys AOSP's libmediandk.so exports, with its values (NdkMediaFormat.cpp,
 * NdkMediaCodec.cpp). A key is a data symbol: a library that names one this library lacked failed
 * to load, as Yandex Browser's did for AMEDIAFORMAT_KEY_TEMPORAL_LAYERING. */
EXPORT const char* AMEDIACODEC_KEY_HDR10_PLUS_INFO = "hdr10-plus-info";
EXPORT const char* AMEDIACODEC_KEY_LOW_LATENCY = "low-latency";
EXPORT const char* AMEDIACODEC_KEY_OFFSET_TIME = "time-offset-us";
EXPORT const char* AMEDIACODEC_KEY_REQUEST_SYNC_FRAME = "request-sync";
EXPORT const char* AMEDIACODEC_KEY_SUSPEND = "drop-input-frames";
EXPORT const char* AMEDIACODEC_KEY_SUSPEND_TIME = "drop-start-time-us";
EXPORT const char* AMEDIACODEC_KEY_VIDEO_BITRATE = "video-bitrate";
EXPORT const char* AMEDIAFORMAT_KEY_AAC_DRC_ATTENUATION_FACTOR = "aac-drc-cut-level";
EXPORT const char* AMEDIAFORMAT_KEY_AAC_DRC_BOOST_FACTOR = "aac-drc-boost-level";
EXPORT const char* AMEDIAFORMAT_KEY_AAC_DRC_HEAVY_COMPRESSION = "aac-drc-heavy-compression";
EXPORT const char* AMEDIAFORMAT_KEY_AAC_DRC_TARGET_REFERENCE_LEVEL = "aac-target-ref-level";
EXPORT const char* AMEDIAFORMAT_KEY_AAC_ENCODED_TARGET_LEVEL = "aac-encoded-target-level";
EXPORT const char* AMEDIAFORMAT_KEY_AAC_MAX_OUTPUT_CHANNEL_COUNT = "aac-max-output-channel_count";
EXPORT const char* AMEDIAFORMAT_KEY_AAC_SBR_MODE = "aac-sbr-mode";
EXPORT const char* AMEDIAFORMAT_KEY_ALBUM = "album";
EXPORT const char* AMEDIAFORMAT_KEY_ALBUMART = "albumart";
EXPORT const char* AMEDIAFORMAT_KEY_ALBUMARTIST = "albumartist";
EXPORT const char* AMEDIAFORMAT_KEY_ALLOW_FRAME_DROP = "allow-frame-drop";
EXPORT const char* AMEDIAFORMAT_KEY_ARTIST = "artist";
EXPORT const char* AMEDIAFORMAT_KEY_AUDIO_PRESENTATION_INFO = "audio-presentation-info";
EXPORT const char* AMEDIAFORMAT_KEY_AUDIO_SESSION_ID = "audio-session-id";
EXPORT const char* AMEDIAFORMAT_KEY_AUTHOR = "author";
EXPORT const char* AMEDIAFORMAT_KEY_BITS_PER_SAMPLE = "bits-per-sample";
EXPORT const char* AMEDIAFORMAT_KEY_CDTRACKNUMBER = "cdtracknum";
EXPORT const char* AMEDIAFORMAT_KEY_COMPILATION = "compilation";
EXPORT const char* AMEDIAFORMAT_KEY_COMPLEXITY = "complexity";
EXPORT const char* AMEDIAFORMAT_KEY_COMPOSER = "composer";
EXPORT const char* AMEDIAFORMAT_KEY_CREATE_INPUT_SURFACE_SUSPENDED = "create-input-buffers-suspended";
EXPORT const char* AMEDIAFORMAT_KEY_CRYPTO_DEFAULT_IV_SIZE = "crypto-default-iv-size";
EXPORT const char* AMEDIAFORMAT_KEY_CRYPTO_ENCRYPTED_BYTE_BLOCK = "crypto-encrypted-byte-block";
EXPORT const char* AMEDIAFORMAT_KEY_CRYPTO_ENCRYPTED_SIZES = "crypto-encrypted-sizes";
EXPORT const char* AMEDIAFORMAT_KEY_CRYPTO_IV = "crypto-iv";
EXPORT const char* AMEDIAFORMAT_KEY_CRYPTO_KEY = "crypto-key";
EXPORT const char* AMEDIAFORMAT_KEY_CRYPTO_MODE = "crypto-mode";
EXPORT const char* AMEDIAFORMAT_KEY_CRYPTO_PLAIN_SIZES = "crypto-plain-sizes";
EXPORT const char* AMEDIAFORMAT_KEY_CRYPTO_SKIP_BYTE_BLOCK = "crypto-skip-byte-block";
EXPORT const char* AMEDIAFORMAT_KEY_CSD = "csd";
EXPORT const char* AMEDIAFORMAT_KEY_CSD_AVC = "csd-avc";
EXPORT const char* AMEDIAFORMAT_KEY_CSD_HEVC = "csd-hevc";
EXPORT const char* AMEDIAFORMAT_KEY_D263 = "d263";
EXPORT const char* AMEDIAFORMAT_KEY_DATE = "date";
EXPORT const char* AMEDIAFORMAT_KEY_DISCNUMBER = "discnum";
EXPORT const char* AMEDIAFORMAT_KEY_DISPLAY_CROP = "crop";
EXPORT const char* AMEDIAFORMAT_KEY_ESDS = "esds";
EXPORT const char* AMEDIAFORMAT_KEY_EXIF_OFFSET = "exif-offset";
EXPORT const char* AMEDIAFORMAT_KEY_EXIF_SIZE = "exif-size";
EXPORT const char* AMEDIAFORMAT_KEY_FLAC_COMPRESSION_LEVEL = "flac-compression-level";
EXPORT const char* AMEDIAFORMAT_KEY_FRAME_COUNT = "frame-count";
EXPORT const char* AMEDIAFORMAT_KEY_GENRE = "genre";
EXPORT const char* AMEDIAFORMAT_KEY_GRID_COLUMNS = "grid-cols";
EXPORT const char* AMEDIAFORMAT_KEY_GRID_ROWS = "grid-rows";
EXPORT const char* AMEDIAFORMAT_KEY_HAPTIC_CHANNEL_COUNT = "haptic-channel-count";
EXPORT const char* AMEDIAFORMAT_KEY_HDR10_PLUS_INFO = "hdr10-plus-info";
EXPORT const char* AMEDIAFORMAT_KEY_HDR_STATIC_INFO = "hdr-static-info";
EXPORT const char* AMEDIAFORMAT_KEY_ICC_PROFILE = "icc-profile";
EXPORT const char* AMEDIAFORMAT_KEY_IS_AUTOSELECT = "is-autoselect";
EXPORT const char* AMEDIAFORMAT_KEY_IS_DEFAULT = "is-default";
EXPORT const char* AMEDIAFORMAT_KEY_IS_FORCED_SUBTITLE = "is-forced-subtitle";
EXPORT const char* AMEDIAFORMAT_KEY_LAST_SAMPLE_INDEX_IN_CHUNK = "last-sample-index-in-chunk";
EXPORT const char* AMEDIAFORMAT_KEY_LOCATION = "location";
EXPORT const char* AMEDIAFORMAT_KEY_LOOP = "loop";
EXPORT const char* AMEDIAFORMAT_KEY_LYRICIST = "lyricist";
EXPORT const char* AMEDIAFORMAT_KEY_MANUFACTURER = "manufacturer";
EXPORT const char* AMEDIAFORMAT_KEY_MAX_BIT_RATE = "max-bitrate";
EXPORT const char* AMEDIAFORMAT_KEY_MAX_B_FRAMES = "max-bframes";
EXPORT const char* AMEDIAFORMAT_KEY_MAX_FPS_TO_ENCODER = "max-fps-to-encoder";
EXPORT const char* AMEDIAFORMAT_KEY_MAX_PTS_GAP_TO_ENCODER = "max-pts-gap-to-encoder";
EXPORT const char* AMEDIAFORMAT_KEY_MPEG2_STREAM_HEADER = "mpeg2-stream-header";
EXPORT const char* AMEDIAFORMAT_KEY_MPEGH_COMPATIBLE_SETS = "mpegh-compatible-sets";
EXPORT const char* AMEDIAFORMAT_KEY_MPEGH_PROFILE_LEVEL_INDICATION = "mpegh-profile-level-indication";
EXPORT const char* AMEDIAFORMAT_KEY_MPEGH_REFERENCE_CHANNEL_LAYOUT = "mpegh-reference-channel-layout";
EXPORT const char* AMEDIAFORMAT_KEY_MPEG_USER_DATA = "mpeg-user-data";
EXPORT const char* AMEDIAFORMAT_KEY_PCM_BIG_ENDIAN = "pcm-big-endian";
EXPORT const char* AMEDIAFORMAT_KEY_PSSH = "pssh";
EXPORT const char* AMEDIAFORMAT_KEY_SAMPLE_FILE_OFFSET = "sample-file-offset";
EXPORT const char* AMEDIAFORMAT_KEY_SAMPLE_TIME_BEFORE_APPEND = "sample-time-before-append";
EXPORT const char* AMEDIAFORMAT_KEY_SAR_HEIGHT = "sar-height";
EXPORT const char* AMEDIAFORMAT_KEY_SAR_WIDTH = "sar-width";
EXPORT const char* AMEDIAFORMAT_KEY_SEI = "sei";
EXPORT const char* AMEDIAFORMAT_KEY_SLOW_MOTION_MARKERS = "slow-motion-markers";
EXPORT const char* AMEDIAFORMAT_KEY_TARGET_TIME = "target-time";
EXPORT const char* AMEDIAFORMAT_KEY_TEMPORAL_LAYERING = "ts-schema";
EXPORT const char* AMEDIAFORMAT_KEY_TEMPORAL_LAYER_COUNT = "temporal-layer-count";
EXPORT const char* AMEDIAFORMAT_KEY_TEMPORAL_LAYER_ID = "temporal-layer-id";
EXPORT const char* AMEDIAFORMAT_KEY_TEXT_FORMAT_DATA = "text-format-data";
EXPORT const char* AMEDIAFORMAT_KEY_THUMBNAIL_CSD_AV1C = "thumbnail-csd-av1c";
EXPORT const char* AMEDIAFORMAT_KEY_THUMBNAIL_CSD_HEVC = "thumbnail-csd-hevc";
EXPORT const char* AMEDIAFORMAT_KEY_THUMBNAIL_HEIGHT = "thumbnail-height";
EXPORT const char* AMEDIAFORMAT_KEY_THUMBNAIL_TIME = "thumbnail-time";
EXPORT const char* AMEDIAFORMAT_KEY_THUMBNAIL_WIDTH = "thumbnail-width";
EXPORT const char* AMEDIAFORMAT_KEY_TILE_HEIGHT = "tile-height";
EXPORT const char* AMEDIAFORMAT_KEY_TILE_WIDTH = "tile-width";
EXPORT const char* AMEDIAFORMAT_KEY_TITLE = "title";
EXPORT const char* AMEDIAFORMAT_KEY_TRACK_INDEX = "track-index";
EXPORT const char* AMEDIAFORMAT_KEY_VALID_SAMPLES = "valid-samples";
EXPORT const char* AMEDIAFORMAT_KEY_XMP_OFFSET = "xmp-offset";
EXPORT const char* AMEDIAFORMAT_KEY_XMP_SIZE = "xmp-size";
EXPORT const char* AMEDIAFORMAT_KEY_YEAR = "year";

/* --- AMediaFormat: a key/value store --------------------------------------------------------- */

enum { V_INT32, V_INT64, V_FLOAT, V_DOUBLE, V_SIZE, V_STRING, V_BUFFER, V_RECT };

typedef struct Entry {
    char* name;
    int type;
    union { int32_t i32; int64_t i64; float f; double d; size_t size; int32_t rect[4]; } v;
    void* data;     /* V_STRING (NUL-terminated) and V_BUFFER */
    size_t length;
    struct Entry* next;
} Entry;

typedef struct AMediaFormat {
    Entry* entries;
    char* text;     /* toString's answer, owned by the format */
} AMediaFormat;

static Entry* find(const AMediaFormat* format, const char* name) {
    if (format == NULL || name == NULL) return NULL;
    for (Entry* e = format->entries; e != NULL; e = e->next) {
        if (strcmp(e->name, name) == 0) return e;
    }
    return NULL;
}

/* The entry for name, emptied for a new value; NULL if the format or name is missing. */
static Entry* slot(AMediaFormat* format, const char* name, int type) {
    if (format == NULL || name == NULL) return NULL;
    Entry* e = find(format, name);
    if (e == NULL) {
        e = calloc(1, sizeof(Entry));
        if (e == NULL) return NULL;
        e->name = strdup(name);
        if (e->name == NULL) { free(e); return NULL; }
        e->next = format->entries;
        format->entries = e;
    } else {
        free(e->data);
        e->data = NULL;
        e->length = 0;
    }
    e->type = type;
    return e;
}

static void clear_entries(AMediaFormat* format) {
    Entry* e = format->entries;
    while (e != NULL) {
        Entry* next = e->next;
        free(e->name);
        free(e->data);
        free(e);
        e = next;
    }
    format->entries = NULL;
}

EXPORT AMediaFormat* AMediaFormat_new(void) {
    return calloc(1, sizeof(AMediaFormat));
}

EXPORT media_status_t AMediaFormat_delete(AMediaFormat* format) {
    if (format == NULL) return AMEDIA_ERROR_INVALID_PARAMETER;
    clear_entries(format);
    free(format->text);
    free(format);
    return AMEDIA_OK;
}

EXPORT void AMediaFormat_clear(AMediaFormat* format) {
    if (format != NULL) clear_entries(format);
}

EXPORT const char* AMediaFormat_toString(AMediaFormat* format) {
    if (format == NULL) return NULL;
    size_t capacity = 64, used = 0;
    char* text = malloc(capacity);
    if (text == NULL) return NULL;
    used = (size_t)snprintf(text, capacity, "AMediaFormat{");
    for (Entry* e = format->entries; e != NULL; e = e->next) {
        char value[96];
        switch (e->type) {
            case V_INT32: snprintf(value, sizeof value, "%d", e->v.i32); break;
            case V_INT64: snprintf(value, sizeof value, "%lld", (long long)e->v.i64); break;
            case V_FLOAT: snprintf(value, sizeof value, "%f", e->v.f); break;
            case V_DOUBLE: snprintf(value, sizeof value, "%f", e->v.d); break;
            case V_SIZE: snprintf(value, sizeof value, "%zu", e->v.size); break;
            case V_STRING: snprintf(value, sizeof value, "\"%.80s\"", (const char*)e->data); break;
            case V_BUFFER: snprintf(value, sizeof value, "data[%zu]", e->length); break;
            default: snprintf(value, sizeof value, "Rect(%d, %d, %d, %d)",
                              e->v.rect[0], e->v.rect[1], e->v.rect[2], e->v.rect[3]); break;
        }
        size_t need = used + strlen(e->name) + strlen(value) + 5;   /* ", " ": " NUL */
        if (need >= capacity) {
            while (need >= capacity) capacity *= 2;
            char* grown = realloc(text, capacity);
            if (grown == NULL) { free(text); return NULL; }
            text = grown;
        }
        used += (size_t)snprintf(text + used, capacity - used, "%s%s: %s",
                                 e == format->entries ? "" : ", ", e->name, value);
    }
    if (used + 2 >= capacity) {
        char* grown = realloc(text, used + 2);
        if (grown == NULL) { free(text); return NULL; }
        text = grown;
    }
    strcpy(text + used, "}");
    free(format->text);
    format->text = text;
    return text;
}

#define GETTER(fn, ctype, tag, field)                                                   \
    EXPORT bool fn(AMediaFormat* format, const char* name, ctype* out) {                \
        Entry* e = find(format, name);                                                  \
        if (e == NULL || e->type != tag || out == NULL) return false;                   \
        *out = e->v.field;                                                              \
        return true;                                                                    \
    }
#define SETTER(fn, ctype, tag, field)                                                   \
    EXPORT void fn(AMediaFormat* format, const char* name, ctype value) {                \
        Entry* e = slot(format, name, tag);                                             \
        if (e != NULL) e->v.field = value;                                              \
    }

GETTER(AMediaFormat_getInt32, int32_t, V_INT32, i32)
GETTER(AMediaFormat_getInt64, int64_t, V_INT64, i64)
GETTER(AMediaFormat_getFloat, float, V_FLOAT, f)
GETTER(AMediaFormat_getDouble, double, V_DOUBLE, d)
GETTER(AMediaFormat_getSize, size_t, V_SIZE, size)
SETTER(AMediaFormat_setInt32, int32_t, V_INT32, i32)
SETTER(AMediaFormat_setInt64, int64_t, V_INT64, i64)
SETTER(AMediaFormat_setFloat, float, V_FLOAT, f)
SETTER(AMediaFormat_setDouble, double, V_DOUBLE, d)
SETTER(AMediaFormat_setSize, size_t, V_SIZE, size)

EXPORT bool AMediaFormat_getString(AMediaFormat* format, const char* name, const char** out) {
    Entry* e = find(format, name);
    if (e == NULL || e->type != V_STRING || out == NULL) return false;
    *out = e->data;
    return true;
}

EXPORT void AMediaFormat_setString(AMediaFormat* format, const char* name, const char* value) {
    if (value == NULL) return;
    Entry* e = slot(format, name, V_STRING);
    if (e != NULL) {
        e->data = strdup(value);
        e->length = e->data != NULL ? strlen(value) : 0;
    }
}

EXPORT bool AMediaFormat_getBuffer(AMediaFormat* format, const char* name, void** data, size_t* size) {
    Entry* e = find(format, name);
    if (e == NULL || e->type != V_BUFFER || data == NULL || size == NULL) return false;
    *data = e->data;
    *size = e->length;
    return true;
}

EXPORT void AMediaFormat_setBuffer(AMediaFormat* format, const char* name, const void* data, size_t size) {
    if (data == NULL && size != 0) return;
    Entry* e = slot(format, name, V_BUFFER);
    if (e == NULL) return;
    e->data = malloc(size ? size : 1);
    if (e->data != NULL) {
        if (size) memcpy(e->data, data, size);
        e->length = size;
    }
}

EXPORT bool AMediaFormat_getRect(AMediaFormat* format, const char* name,
                                 int32_t* left, int32_t* top, int32_t* right, int32_t* bottom) {
    Entry* e = find(format, name);
    if (e == NULL || e->type != V_RECT || !left || !top || !right || !bottom) return false;
    *left = e->v.rect[0]; *top = e->v.rect[1]; *right = e->v.rect[2]; *bottom = e->v.rect[3];
    return true;
}

EXPORT void AMediaFormat_setRect(AMediaFormat* format, const char* name,
                                 int32_t left, int32_t top, int32_t right, int32_t bottom) {
    Entry* e = slot(format, name, V_RECT);
    if (e != NULL) { e->v.rect[0] = left; e->v.rect[1] = top; e->v.rect[2] = right; e->v.rect[3] = bottom; }
}

EXPORT media_status_t AMediaFormat_copy(AMediaFormat* to, AMediaFormat* from) {
    if (to == NULL || from == NULL) return AMEDIA_ERROR_INVALID_PARAMETER;
    clear_entries(to);
    for (Entry* e = from->entries; e != NULL; e = e->next) {
        Entry* c = slot(to, e->name, e->type);
        if (c == NULL) return AMEDIA_ERROR_UNKNOWN;
        c->v = e->v;
        if (e->data != NULL) {
            c->data = malloc(e->length + 1);
            if (c->data == NULL) return AMEDIA_ERROR_UNKNOWN;
            memcpy(c->data, e->data, e->length + (e->type == V_STRING ? 1 : 0));
            c->length = e->length;
        }
    }
    return AMEDIA_OK;
}

/* --- AMediaCodec: no codec can be created ------------------------------------------------------ */
/* The NDK documents NULL for a codec that cannot be created; every other call then has no codec. */

typedef struct AMediaCodec AMediaCodec;
typedef struct AMediaCrypto AMediaCrypto;

EXPORT AMediaCodec* AMediaCodec_createCodecByName(const char* name) { (void)name; return NULL; }
EXPORT AMediaCodec* AMediaCodec_createDecoderByType(const char* mime) { (void)mime; return NULL; }
EXPORT AMediaCodec* AMediaCodec_createEncoderByType(const char* mime) { (void)mime; return NULL; }
EXPORT media_status_t AMediaCodec_delete(AMediaCodec* codec) { (void)codec; return AMEDIA_OK; }

EXPORT media_status_t AMediaCodec_configure(AMediaCodec* codec, const AMediaFormat* format, void* surface,
                                            AMediaCrypto* crypto, uint32_t flags) {
    (void)codec; (void)format; (void)surface; (void)crypto; (void)flags;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT media_status_t AMediaCodec_start(AMediaCodec* codec) { (void)codec; return AMEDIA_ERROR_INVALID_OBJECT; }
EXPORT media_status_t AMediaCodec_stop(AMediaCodec* codec) { (void)codec; return AMEDIA_ERROR_INVALID_OBJECT; }
EXPORT media_status_t AMediaCodec_flush(AMediaCodec* codec) { (void)codec; return AMEDIA_ERROR_INVALID_OBJECT; }

EXPORT uint8_t* AMediaCodec_getInputBuffer(AMediaCodec* codec, size_t index, size_t* size) {
    (void)codec; (void)index;
    if (size != NULL) *size = 0;
    return NULL;
}

EXPORT uint8_t* AMediaCodec_getOutputBuffer(AMediaCodec* codec, size_t index, size_t* size) {
    (void)codec; (void)index;
    if (size != NULL) *size = 0;
    return NULL;
}

EXPORT ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec* codec, int64_t timeoutUs) {
    (void)codec; (void)timeoutUs;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT media_status_t AMediaCodec_queueInputBuffer(AMediaCodec* codec, size_t index, off_t offset, size_t size,
                                                   uint64_t time, uint32_t flags) {
    (void)codec; (void)index; (void)offset; (void)size; (void)time; (void)flags;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT media_status_t AMediaCodec_queueSecureInputBuffer(AMediaCodec* codec, size_t index, off_t offset,
                                                         void* cryptoInfo, uint64_t time, uint32_t flags) {
    (void)codec; (void)index; (void)offset; (void)cryptoInfo; (void)time; (void)flags;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec* codec, void* info, int64_t timeoutUs) {
    (void)codec; (void)info; (void)timeoutUs;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT AMediaFormat* AMediaCodec_getOutputFormat(AMediaCodec* codec) { (void)codec; return NULL; }
EXPORT AMediaFormat* AMediaCodec_getInputFormat(AMediaCodec* codec) { (void)codec; return NULL; }

EXPORT media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec* codec, size_t index, bool render) {
    (void)codec; (void)index; (void)render;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT media_status_t AMediaCodec_releaseOutputBufferAtTime(AMediaCodec* codec, size_t index, int64_t timestampNs) {
    (void)codec; (void)index; (void)timestampNs;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT media_status_t AMediaCodec_setParameters(AMediaCodec* codec, const AMediaFormat* params) {
    (void)codec; (void)params;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

/* The rest of NdkMediaCodec.h. Chromium's media code (Yandex Browser, Edge, Brave) imports the
 * API 28 calls; with one of them missing the browser's main library failed to load and Yandex
 * showed "Installation error. Please delete app and reinstall it". With no codec, each answers as
 * for a codec that does not exist. */

typedef struct ANativeWindow ANativeWindow;

/* Passed by value, as in AOSP: the type fixes how the arguments after it are passed. */
typedef struct AMediaCodecOnAsyncNotifyCallback {
    void (*onAsyncInputAvailable)(AMediaCodec* codec, void* userdata, int32_t index);
    void (*onAsyncOutputAvailable)(AMediaCodec* codec, void* userdata, int32_t index, void* bufferInfo);
    void (*onAsyncFormatChanged)(AMediaCodec* codec, void* userdata, AMediaFormat* format);
    void (*onAsyncError)(AMediaCodec* codec, void* userdata, media_status_t error, int32_t actionCode,
                         const char* detail);
} AMediaCodecOnAsyncNotifyCallback;

typedef void (*AMediaCodecOnFrameRendered)(AMediaCodec* codec, void* userdata, int64_t mediaTimeUs,
                                           int64_t systemNano);

EXPORT AMediaCodec* AMediaCodec_createCodecByNameForClient(const char* name, pid_t pid, uid_t uid) {
    (void)name; (void)pid; (void)uid;
    return NULL;
}

EXPORT AMediaCodec* AMediaCodec_createDecoderByTypeForClient(const char* mime, pid_t pid, uid_t uid) {
    (void)mime; (void)pid; (void)uid;
    return NULL;
}

EXPORT AMediaCodec* AMediaCodec_createEncoderByTypeForClient(const char* mime, pid_t pid, uid_t uid) {
    (void)mime; (void)pid; (void)uid;
    return NULL;
}

EXPORT media_status_t AMediaCodec_setOutputSurface(AMediaCodec* codec, ANativeWindow* surface) {
    (void)codec; (void)surface;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT media_status_t AMediaCodec_createInputSurface(AMediaCodec* codec, ANativeWindow** surface) {
    (void)codec;
    if (surface != NULL) *surface = NULL;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

/* A persistent input surface needs no codec in AOSP, but it is a codec's input surface all the same. */
EXPORT media_status_t AMediaCodec_createPersistentInputSurface(ANativeWindow** surface) {
    if (surface != NULL) *surface = NULL;
    return AMEDIA_ERROR_UNSUPPORTED;
}

EXPORT media_status_t AMediaCodec_setInputSurface(AMediaCodec* codec, ANativeWindow* surface) {
    (void)codec; (void)surface;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT media_status_t AMediaCodec_signalEndOfInputStream(AMediaCodec* codec) {
    (void)codec;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT AMediaFormat* AMediaCodec_getBufferFormat(AMediaCodec* codec, size_t index) {
    (void)codec; (void)index;
    return NULL;
}

EXPORT media_status_t AMediaCodec_getName(AMediaCodec* codec, char** outName) {
    (void)codec;
    if (outName != NULL) *outName = NULL;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

/* AOSP frees the name getName allocated. */
EXPORT void AMediaCodec_releaseName(AMediaCodec* codec, char* name) {
    (void)codec;
    free(name);
}

EXPORT media_status_t AMediaCodec_releaseCrypto(AMediaCodec* codec) {
    (void)codec;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT media_status_t AMediaCodec_setAsyncNotifyCallback(AMediaCodec* codec, AMediaCodecOnAsyncNotifyCallback callback,
                                                         void* userdata) {
    (void)codec; (void)callback; (void)userdata;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT media_status_t AMediaCodec_setOnFrameRenderedCallback(AMediaCodec* codec, AMediaCodecOnFrameRendered callback,
                                                             void* userdata) {
    (void)codec; (void)callback; (void)userdata;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

/* AOSP's action codes (MediaErrors.h): fatal 0, transient 1, recoverable 2. */
EXPORT bool AMediaCodecActionCode_isRecoverable(int32_t actionCode) { return actionCode == 2; }
EXPORT bool AMediaCodecActionCode_isTransient(int32_t actionCode) { return actionCode == 1; }

/* Crypto info is plain data on Android too, kept as given and read back as in AOSP; it only matters
 * once a codec exists. */
typedef struct AMediaCodecCryptoInfo {
    int subsamples;
    uint8_t key[16];
    uint8_t iv[16];
    int mode;
    size_t* clearBytes;       /* subsamples entries each, stored after the struct */
    size_t* encryptedBytes;
} AMediaCodecCryptoInfo;

EXPORT AMediaCodecCryptoInfo* AMediaCodecCryptoInfo_new(int subsamples, uint8_t key[16], uint8_t iv[16], int mode,
                                                         size_t* clearBytes, size_t* encryptedBytes) {
    size_t count = subsamples > 0 ? (size_t)subsamples : 0;
    size_t size;
    if (__builtin_mul_overflow(sizeof(size_t) * 2, count, &size) ||
        __builtin_add_overflow(size, sizeof(AMediaCodecCryptoInfo), &size)) {
        return NULL;
    }
    AMediaCodecCryptoInfo* info = calloc(1, size);
    if (info == NULL) return NULL;
    info->subsamples = subsamples;
    if (key != NULL) memcpy(info->key, key, 16);
    if (iv != NULL) memcpy(info->iv, iv, 16);
    info->mode = mode;
    info->clearBytes = (size_t*)(info + 1);
    info->encryptedBytes = info->clearBytes + count;
    if (clearBytes != NULL) memcpy(info->clearBytes, clearBytes, count * sizeof(size_t));
    if (encryptedBytes != NULL) memcpy(info->encryptedBytes, encryptedBytes, count * sizeof(size_t));
    return info;
}

EXPORT media_status_t AMediaCodecCryptoInfo_delete(AMediaCodecCryptoInfo* info) {
    free(info);
    return AMEDIA_OK;
}

EXPORT void AMediaCodecCryptoInfo_setPattern(AMediaCodecCryptoInfo* info, void* pattern) {
    (void)info; (void)pattern;
}

EXPORT size_t AMediaCodecCryptoInfo_getNumSubSamples(AMediaCodecCryptoInfo* info) {
    return info == NULL || info->subsamples < 0 ? 0 : (size_t)info->subsamples;
}

EXPORT int AMediaCodecCryptoInfo_getMode(AMediaCodecCryptoInfo* info) {
    return info == NULL ? AMEDIA_ERROR_INVALID_OBJECT : info->mode;
}

static media_status_t copy_out(AMediaCodecCryptoInfo* info, void* dst, const void* src, size_t size) {
    if (info == NULL) return AMEDIA_ERROR_INVALID_OBJECT;
    if (dst == NULL) return AMEDIA_ERROR_INVALID_PARAMETER;
    memcpy(dst, src, size);
    return AMEDIA_OK;
}

EXPORT media_status_t AMediaCodecCryptoInfo_getKey(AMediaCodecCryptoInfo* info, uint8_t* dst) {
    return copy_out(info, dst, info == NULL ? NULL : info->key, 16);
}

EXPORT media_status_t AMediaCodecCryptoInfo_getIV(AMediaCodecCryptoInfo* info, uint8_t* dst) {
    return copy_out(info, dst, info == NULL ? NULL : info->iv, 16);
}

EXPORT media_status_t AMediaCodecCryptoInfo_getClearBytes(AMediaCodecCryptoInfo* info, size_t* dst) {
    return copy_out(info, dst, info == NULL ? NULL : info->clearBytes,
                    AMediaCodecCryptoInfo_getNumSubSamples(info) * sizeof(size_t));
}

EXPORT media_status_t AMediaCodecCryptoInfo_getEncryptedBytes(AMediaCodecCryptoInfo* info, size_t* dst) {
    return copy_out(info, dst, info == NULL ? NULL : info->encryptedBytes,
                    AMediaCodecCryptoInfo_getNumSubSamples(info) * sizeof(size_t));
}

/* --- AMediaCrypto and AMediaDrm: no DRM scheme is supported ------------------------------------ */
/* The same answer the runtime's Java MediaDrm/MediaCrypto natives give. */

typedef struct AMediaDrm AMediaDrm;

EXPORT bool AMediaCrypto_isCryptoSchemeSupported(const uint8_t uuid[16]) { (void)uuid; return false; }
EXPORT bool AMediaCrypto_requiresSecureDecoderComponent(const char* mime) { (void)mime; return false; }

EXPORT AMediaCrypto* AMediaCrypto_new(const uint8_t uuid[16], const void* initData, size_t initDataSize) {
    (void)uuid; (void)initData; (void)initDataSize;
    return NULL;
}

EXPORT void AMediaCrypto_delete(AMediaCrypto* crypto) { (void)crypto; }

EXPORT bool AMediaDrm_isCryptoSchemeSupported(const uint8_t uuid[16], const char* mime) {
    (void)uuid; (void)mime;
    return false;
}

EXPORT AMediaDrm* AMediaDrm_createByUUID(const uint8_t uuid[16]) { (void)uuid; return NULL; }
EXPORT void AMediaDrm_release(AMediaDrm* drm) { (void)drm; }

#define NO_DRM(fn, ...)                                                                 \
    EXPORT media_status_t fn(AMediaDrm* drm, __VA_ARGS__) { (void)drm; return AMEDIA_ERROR_INVALID_OBJECT; }

NO_DRM(AMediaDrm_setOnEventListener, void* listener)
NO_DRM(AMediaDrm_setOnExpirationUpdateListener, void* listener)
NO_DRM(AMediaDrm_setOnKeysChangeListener, void* listener)
NO_DRM(AMediaDrm_openSession, void* sessionId)
NO_DRM(AMediaDrm_closeSession, const void* sessionId)
NO_DRM(AMediaDrm_getKeyRequest, const void* scope, const uint8_t* init, size_t initSize, const char* mime,
       int keyType, const void* optional, size_t numOptional, const uint8_t** request, size_t* requestSize)
NO_DRM(AMediaDrm_getKeyRequestWithDefaultUrlAndType, const void* scope, const uint8_t* init, size_t initSize,
       const char* mime, int keyType, const void* optional, size_t numOptional, const uint8_t** request,
       size_t* requestSize, const char** defaultUrl, int* keyRequestType)
NO_DRM(AMediaDrm_provideKeyResponse, const void* scope, const uint8_t* response, size_t responseSize,
       void* keySetId)
NO_DRM(AMediaDrm_getProvisionRequest, const uint8_t** request, size_t* requestSize, const char** serverUrl)
NO_DRM(AMediaDrm_provideProvisionResponse, const uint8_t* response, size_t responseSize)
NO_DRM(AMediaDrm_setPropertyString, const char* name, const char* value)
NO_DRM(AMediaDrm_setPropertyByteArray, const char* name, const uint8_t* value, size_t valueSize)
/* The rest of NdkMediaDrm.h: an anti-fraud SDK's libliteye.so imports
 * AMediaDrm_getPropertyByteArray and failed to relocate without it. */
NO_DRM(AMediaDrm_getPropertyString, const char* name, const char** value)
NO_DRM(AMediaDrm_getPropertyByteArray, const char* name, void* value)
NO_DRM(AMediaDrm_restoreKeys, const void* sessionId, const void* keySetId)
NO_DRM(AMediaDrm_removeKeys, const void* keySetIdOrSessionId)
NO_DRM(AMediaDrm_queryKeyStatus, const void* sessionId, void* keyValuePairs, size_t* numPairs)
NO_DRM(AMediaDrm_getSecureStops, void* secureStops, size_t* numSecureStops)
NO_DRM(AMediaDrm_releaseSecureStops, const void* ssRelease)
NO_DRM(AMediaDrm_encrypt, const void* sessionId, const char* cipherAlgorithm, uint8_t* keyId, uint8_t* iv,
       const uint8_t* input, uint8_t* output, size_t dataSize)
NO_DRM(AMediaDrm_decrypt, const void* sessionId, const char* cipherAlgorithm, uint8_t* keyId, uint8_t* iv,
       const uint8_t* input, uint8_t* output, size_t dataSize)
NO_DRM(AMediaDrm_sign, const void* sessionId, const char* macAlgorithm, uint8_t* keyId, uint8_t* message,
       size_t messageSize, uint8_t* signature, size_t* signatureSize)
NO_DRM(AMediaDrm_verify, const void* sessionId, const char* macAlgorithm, uint8_t* keyId,
       const uint8_t* message, size_t messageSize, const uint8_t* signature, size_t signatureSize)

/* --- AMediaExtractor: exists, but accepts no data source --------------------------------------- */

typedef struct AMediaExtractor { int unused; } AMediaExtractor;

EXPORT AMediaExtractor* AMediaExtractor_new(void) { return calloc(1, sizeof(AMediaExtractor)); }

EXPORT media_status_t AMediaExtractor_delete(AMediaExtractor* extractor) {
    free(extractor);
    return AMEDIA_OK;
}

EXPORT media_status_t AMediaExtractor_setDataSourceFd(AMediaExtractor* extractor, int fd, int64_t offset,
                                                      int64_t length) {
    (void)fd; (void)offset; (void)length;
    return extractor == NULL ? AMEDIA_ERROR_INVALID_OBJECT : AMEDIA_ERROR_UNSUPPORTED;
}

EXPORT media_status_t AMediaExtractor_setDataSource(AMediaExtractor* extractor, const char* location) {
    (void)location;
    return extractor == NULL ? AMEDIA_ERROR_INVALID_OBJECT : AMEDIA_ERROR_UNSUPPORTED;
}

EXPORT size_t AMediaExtractor_getTrackCount(AMediaExtractor* extractor) { (void)extractor; return 0; }

/* An empty format, which the caller deletes, rather than NULL: callers read it without checking. */
EXPORT AMediaFormat* AMediaExtractor_getTrackFormat(AMediaExtractor* extractor, size_t index) {
    (void)extractor; (void)index;
    return AMediaFormat_new();
}

EXPORT media_status_t AMediaExtractor_selectTrack(AMediaExtractor* extractor, size_t index) {
    (void)extractor; (void)index;
    return AMEDIA_ERROR_INVALID_PARAMETER;
}

EXPORT media_status_t AMediaExtractor_unselectTrack(AMediaExtractor* extractor, size_t index) {
    (void)extractor; (void)index;
    return AMEDIA_ERROR_INVALID_PARAMETER;
}

EXPORT ssize_t AMediaExtractor_readSampleData(AMediaExtractor* extractor, uint8_t* buffer, size_t capacity) {
    (void)extractor; (void)buffer; (void)capacity;
    return -1;   /* no sample */
}

EXPORT uint32_t AMediaExtractor_getSampleFlags(AMediaExtractor* extractor) { (void)extractor; return 0; }
EXPORT int AMediaExtractor_getSampleTrackIndex(AMediaExtractor* extractor) { (void)extractor; return -1; }
EXPORT int64_t AMediaExtractor_getSampleTime(AMediaExtractor* extractor) { (void)extractor; return -1; }
EXPORT bool AMediaExtractor_advance(AMediaExtractor* extractor) { (void)extractor; return false; }

EXPORT media_status_t AMediaExtractor_seekTo(AMediaExtractor* extractor, int64_t positionUs, int mode) {
    (void)extractor; (void)positionUs; (void)mode;
    return AMEDIA_ERROR_INVALID_OPERATION;
}

/* The rest of NdkMediaExtractor.h, answered as for an extractor with no data source: no sample,
 * no cached data, no DRM information. */

typedef struct AMediaDataSource AMediaDataSource;

EXPORT media_status_t AMediaExtractor_setDataSourceCustom(AMediaExtractor* extractor, AMediaDataSource* src) {
    (void)src;
    return extractor == NULL ? AMEDIA_ERROR_INVALID_OBJECT : AMEDIA_ERROR_UNSUPPORTED;
}

/* An empty format the caller deletes, as for getTrackFormat. */
EXPORT AMediaFormat* AMediaExtractor_getFileFormat(AMediaExtractor* extractor) {
    (void)extractor;
    return AMediaFormat_new();
}

EXPORT media_status_t AMediaExtractor_getSampleFormat(AMediaExtractor* extractor, AMediaFormat* format) {
    (void)extractor;
    return format == NULL ? AMEDIA_ERROR_INVALID_PARAMETER : AMEDIA_ERROR_END_OF_STREAM;
}

EXPORT ssize_t AMediaExtractor_getSampleSize(AMediaExtractor* extractor) { (void)extractor; return -1; }
EXPORT int64_t AMediaExtractor_getCachedDuration(AMediaExtractor* extractor) { (void)extractor; return -1; }
EXPORT void* AMediaExtractor_getPsshInfo(AMediaExtractor* extractor) { (void)extractor; return NULL; }

EXPORT AMediaCodecCryptoInfo* AMediaExtractor_getSampleCryptoInfo(AMediaExtractor* extractor) {
    (void)extractor;
    return NULL;
}

/* --- AMediaDataSource: the app's own callbacks, kept as in AOSP ------------------------------- */
/* Only an extractor or player reads from a data source, and neither accepts one here; the
 * object itself is the app's: callbacks and user data set are kept, and close calls the app's close.
 * A data source for a URI needs AOSP's media HTTP service, so that one cannot be created. */

typedef ssize_t (*AMediaDataSourceReadAt)(void* userdata, int64_t offset, void* buffer, size_t size);
typedef ssize_t (*AMediaDataSourceGetSize)(void* userdata);
typedef void (*AMediaDataSourceClose)(void* userdata);
typedef ssize_t (*AMediaDataSourceGetAvailableSize)(void* userdata, int64_t offset);

struct AMediaDataSource {
    void* userdata;
    AMediaDataSourceReadAt readAt;
    AMediaDataSourceGetSize getSize;
    AMediaDataSourceClose close;
    AMediaDataSourceGetAvailableSize getAvailableSize;
};

EXPORT AMediaDataSource* AMediaDataSource_new(void) { return calloc(1, sizeof(AMediaDataSource)); }

EXPORT AMediaDataSource* AMediaDataSource_newUri(const char* uri, int numHeaders, const char* const* keyValues) {
    (void)uri; (void)numHeaders; (void)keyValues;
    return NULL;
}

EXPORT void AMediaDataSource_delete(AMediaDataSource* source) { free(source); }

EXPORT void AMediaDataSource_setUserdata(AMediaDataSource* source, void* userdata) {
    if (source != NULL) source->userdata = userdata;
}

EXPORT void AMediaDataSource_setReadAt(AMediaDataSource* source, AMediaDataSourceReadAt readAt) {
    if (source != NULL) source->readAt = readAt;
}

EXPORT void AMediaDataSource_setGetSize(AMediaDataSource* source, AMediaDataSourceGetSize getSize) {
    if (source != NULL) source->getSize = getSize;
}

EXPORT void AMediaDataSource_setClose(AMediaDataSource* source, AMediaDataSourceClose close) {
    if (source != NULL) source->close = close;
}

EXPORT void AMediaDataSource_setGetAvailableSize(AMediaDataSource* source,
                                                 AMediaDataSourceGetAvailableSize getAvailableSize) {
    if (source != NULL) source->getAvailableSize = getAvailableSize;
}

EXPORT void AMediaDataSource_close(AMediaDataSource* source) {
    if (source != NULL && source->close != NULL) source->close(source->userdata);
}

/* --- AMediaMuxer: no muxer can be created ------------------------------------------------------ */

typedef struct AMediaMuxer AMediaMuxer;

EXPORT AMediaMuxer* AMediaMuxer_new(int fd, int format) { (void)fd; (void)format; return NULL; }
EXPORT media_status_t AMediaMuxer_delete(AMediaMuxer* muxer) { (void)muxer; return AMEDIA_OK; }

EXPORT ssize_t AMediaMuxer_addTrack(AMediaMuxer* muxer, const AMediaFormat* format) {
    (void)muxer; (void)format;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT media_status_t AMediaMuxer_setLocation(AMediaMuxer* muxer, float latitude, float longitude) {
    (void)muxer; (void)latitude; (void)longitude;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT media_status_t AMediaMuxer_setOrientationHint(AMediaMuxer* muxer, int degrees) {
    (void)muxer; (void)degrees;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT media_status_t AMediaMuxer_start(AMediaMuxer* muxer) { (void)muxer; return AMEDIA_ERROR_INVALID_OBJECT; }
EXPORT media_status_t AMediaMuxer_stop(AMediaMuxer* muxer) { (void)muxer; return AMEDIA_ERROR_INVALID_OBJECT; }

EXPORT media_status_t AMediaMuxer_writeSampleData(AMediaMuxer* muxer, size_t trackIndex, const uint8_t* data,
                                                  const void* info) {
    (void)muxer; (void)trackIndex; (void)data; (void)info;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT AMediaMuxer* AMediaMuxer_append(int fd, int mode) { (void)fd; (void)mode; return NULL; }

EXPORT ssize_t AMediaMuxer_getTrackCount(AMediaMuxer* muxer) {
    (void)muxer;
    return AMEDIA_ERROR_INVALID_OBJECT;
}

EXPORT AMediaFormat* AMediaMuxer_getTrackFormat(AMediaMuxer* muxer, size_t index) {
    (void)muxer; (void)index;
    return NULL;
}

/* --- AImageReader: not supported --------------------------------------------------------------- */

typedef struct AImageReader AImageReader;
typedef struct AImage AImage;

EXPORT media_status_t AImageReader_new(int32_t width, int32_t height, int32_t format, int32_t maxImages,
                                       AImageReader** reader) {
    (void)width; (void)height; (void)format; (void)maxImages;
    if (reader != NULL) *reader = NULL;
    return AMEDIA_ERROR_UNSUPPORTED;
}

EXPORT media_status_t AImageReader_newWithUsage(int32_t width, int32_t height, int32_t format, uint64_t usage,
                                                int32_t maxImages, AImageReader** reader) {
    (void)usage;
    return AImageReader_new(width, height, format, maxImages, reader);
}

EXPORT media_status_t AImageReader_newWithDataSpace(int32_t width, int32_t height, uint64_t usage, int32_t maxImages,
                                                    uint32_t hardwareBufferFormat, int32_t dataSpace,
                                                    AImageReader** reader) {
    (void)usage; (void)dataSpace;
    return AImageReader_new(width, height, (int32_t)hardwareBufferFormat, maxImages, reader);
}

EXPORT void AImageReader_delete(AImageReader* reader) { (void)reader; }

#define NO_READER(fn, ...)                                                              \
    EXPORT media_status_t fn(__VA_ARGS__) { return AMEDIA_ERROR_INVALID_PARAMETER; }

NO_READER(AImageReader_getWindow, AImageReader* reader, void** window)
NO_READER(AImageReader_getWidth, const AImageReader* reader, int32_t* width)
NO_READER(AImageReader_getHeight, const AImageReader* reader, int32_t* height)
NO_READER(AImageReader_getFormat, const AImageReader* reader, int32_t* format)
NO_READER(AImageReader_getMaxImages, const AImageReader* reader, int32_t* maxImages)
NO_READER(AImageReader_acquireNextImage, AImageReader* reader, AImage** image)
NO_READER(AImageReader_acquireLatestImage, AImageReader* reader, AImage** image)
NO_READER(AImageReader_acquireNextImageAsync, AImageReader* reader, AImage** image, int* fenceFd)
NO_READER(AImageReader_acquireLatestImageAsync, AImageReader* reader, AImage** image, int* fenceFd)
NO_READER(AImageReader_setImageListener, AImageReader* reader, void* listener)
NO_READER(AImageReader_setBufferRemovedListener, AImageReader* reader, void* listener)
NO_READER(AImage_getWidth, const AImage* image, int32_t* width)
NO_READER(AImage_getHeight, const AImage* image, int32_t* height)
NO_READER(AImage_getFormat, const AImage* image, int32_t* format)
NO_READER(AImage_getCropRect, const AImage* image, void* rect)
NO_READER(AImage_getTimestamp, const AImage* image, int64_t* timestampNs)
NO_READER(AImage_getNumberOfPlanes, const AImage* image, int32_t* numPlanes)
NO_READER(AImage_getPlanePixelStride, const AImage* image, int planeIndex, int32_t* pixelStride)
NO_READER(AImage_getPlaneRowStride, const AImage* image, int planeIndex, int32_t* rowStride)
NO_READER(AImage_getPlaneData, const AImage* image, int planeIndex, uint8_t** data, int* dataLength)
NO_READER(AImage_getHardwareBuffer, const AImage* image, void** buffer)
NO_READER(AImage_getDataSpace, const AImage* image, int32_t* dataSpace)
NO_READER(AImageReader_getWindowNativeHandle, AImageReader* reader, void** handle)

EXPORT void AImage_delete(AImage* image) { (void)image; }
EXPORT void AImage_deleteAsync(AImage* image, int releaseFenceFd) { (void)image; (void)releaseFenceFd; }
