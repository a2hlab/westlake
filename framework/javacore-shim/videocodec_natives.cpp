/*
 * Video decoding for android.media.MediaCodec and MediaCodecList, over OH's OH_VideoDecoder.
 *
 * The natives the runtime registers for MediaCodec are the bridge's port for one app's MP3 stream
 * (framework/core/jni/oh_mediacodec_shim.cpp, in liboh_adapter_bridge.so, which cannot be rebuilt
 * here). Its native_setup makes an OH *audio* codec whatever the type, so createDecoderByType
 * ("video/avc") left a null codec behind and no input buffer ever came (the media-contracts
 * probe's failure), and its MediaCodecList reports one codec, an MP3 decoder, so a player that asks
 * the list for an H.264 or HEVC decoder is told there is none. No app could decode video.
 *
 * This file wraps those natives rather than replacing them: each wrapper keeps the bridge's own
 * function (read from ART's method record, as canvas_natives.c does) and hands it every codec that
 * is not a video decoder, so the audio path and its hard-won fixes stay exactly as they are. A video
 * decoder is created here, its context recorded, and every later call on that MediaCodec is
 * answered here:
 *
 *   - OH's decoder is asynchronous (callbacks hand over buffer indices); MediaCodec's synchronous
 *     dequeue calls are served from queues of those indices, and its callback mode is driven from
 *     them directly, the way the bridge's audio path does it.
 *   - The codec-specific data a player passes in the format (csd-0, csd-1: H.264's SPS and PPS,
 *     HEVC's VPS/SPS/PPS) is submitted first as a codec-data buffer, as Android's codec framework
 *     does itself, since a player does not queue it.
 *   - Output goes to the Surface the player configured (OH_VideoDecoder_SetSurface on the window
 *     behind it; releaseOutputBuffer(render=true) renders) or, without one, to NV12 ByteBuffers
 *     reported as COLOR_FormatYUV420SemiPlanar.
 *
 * MediaCodecList gains one entry per video decoder OH reports (OH_AVCodec_GetCapability), after
 * the bridge's own entries, with capabilities built by the constructor AOSP's own JNI uses.
 *
 * Not ported: video encoders and input surfaces, secure (DRM) decoding, tunneled playback, and
 * per-frame render timestamps (a frame renders when released); each answers as AOSP's MediaCodec
 * answers a codec without the feature.
 */
#include <jni.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace {

void vlog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void vlog(const char* fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int) sizeof(line) - 2) n = (int) sizeof(line) - 2;
    line[n++] = '\n';
    (void) write(2, line, (size_t) n);
}

/* ---- OH's codec API, opened at the first video codec ------------------------------------- */

struct OH_AVCodec;
struct OH_AVFormat;
struct OH_AVBuffer;
struct OH_AVCapability;
struct OHNativeWindow;
struct OH_AVCodecBufferAttr { int64_t pts; int32_t size; int32_t offset; uint32_t flags; };
struct OH_AVRange { int32_t minVal; int32_t maxVal; };
typedef void (*OnError)(OH_AVCodec*, int32_t, void*);
typedef void (*OnStreamChanged)(OH_AVCodec*, OH_AVFormat*, void*);
typedef void (*OnBuffer)(OH_AVCodec*, uint32_t, OH_AVBuffer*, void*);
struct OH_AVCodecCallback { OnError onError; OnStreamChanged onStreamChanged; OnBuffer onNeedInputBuffer;
                            OnBuffer onNewOutputBuffer; };

enum { OH_FLAG_EOS = 1 << 0, OH_FLAG_SYNC = 1 << 1, OH_FLAG_CODEC_DATA = 1 << 3 };
enum { OH_PIXEL_NV12 = 2 };

struct Oh {
    int loaded = 0;
    OH_AVCodec* (*create)(const char*);
    OH_AVCodec* (*createByName)(const char*);
    int (*destroy)(OH_AVCodec*);
    int (*registerCallback)(OH_AVCodec*, OH_AVCodecCallback, void*);
    int (*setSurface)(OH_AVCodec*, OHNativeWindow*);
    int (*configure)(OH_AVCodec*, OH_AVFormat*);
    int (*prepare)(OH_AVCodec*);
    int (*start)(OH_AVCodec*);
    int (*stop)(OH_AVCodec*);
    int (*flush)(OH_AVCodec*);
    OH_AVFormat* (*outputDescription)(OH_AVCodec*);
    int (*pushInput)(OH_AVCodec*, uint32_t);
    int (*renderOutput)(OH_AVCodec*, uint32_t);
    int (*freeOutput)(OH_AVCodec*, uint32_t);
    uint8_t* (*bufferAddr)(OH_AVBuffer*);
    int32_t (*bufferCapacity)(OH_AVBuffer*);
    int (*bufferGetAttr)(OH_AVBuffer*, OH_AVCodecBufferAttr*);
    int (*bufferSetAttr)(OH_AVBuffer*, const OH_AVCodecBufferAttr*);
    OH_AVFormat* (*formatCreate)();
    void (*formatDestroy)(OH_AVFormat*);
    bool (*formatSetInt)(OH_AVFormat*, const char*, int32_t);
    bool (*formatSetBuffer)(OH_AVFormat*, const char*, const uint8_t*, size_t);
    bool (*formatGetInt)(OH_AVFormat*, const char*, int32_t*);
    OH_AVCapability* (*capability)(const char*, bool);
    OH_AVCapability* (*capabilityByCategory)(const char*, bool, int);
    const char* (*capabilityName)(OH_AVCapability*);
    int (*capabilityProfiles)(OH_AVCapability*, const int32_t**, uint32_t*);
    int (*widthRange)(OH_AVCapability*, OH_AVRange*);
    int (*heightRange)(OH_AVCapability*, OH_AVRange*);
    const char* const* keyWidth;
    const char* const* keyHeight;
    const char* const* keyPixelFormat;
    const char* const* keyStride;
    const char* const* keyCodecConfig;
    const char* const* keyProfile;
    const char* const* keySliceHeight;
} oh;

std::mutex g_load;

bool loadOh()
{
    std::lock_guard<std::mutex> l(g_load);
    if (oh.loaded != 0) return oh.loaded > 0;
    oh.loaded = -1;
    void* vdec = dlopen("libnative_media_vdec.so", RTLD_NOW);
    void* core = dlopen("libnative_media_core.so", RTLD_NOW);
    void* base = dlopen("libnative_media_codecbase.so", RTLD_NOW);
    if (vdec == nullptr || core == nullptr || base == nullptr) {
        vlog("[WL-VIDEOCODEC] OH video decoder unavailable: %s", dlerror());
        return false;
    }
#define SYM(field, lib, name) (*(void**) &oh.field = dlsym(lib, name))
    SYM(create, vdec, "OH_VideoDecoder_CreateByMime");
    SYM(createByName, vdec, "OH_VideoDecoder_CreateByName");
    SYM(destroy, vdec, "OH_VideoDecoder_Destroy");
    SYM(registerCallback, vdec, "OH_VideoDecoder_RegisterCallback");
    SYM(setSurface, vdec, "OH_VideoDecoder_SetSurface");
    SYM(configure, vdec, "OH_VideoDecoder_Configure");
    SYM(prepare, vdec, "OH_VideoDecoder_Prepare");
    SYM(start, vdec, "OH_VideoDecoder_Start");
    SYM(stop, vdec, "OH_VideoDecoder_Stop");
    SYM(flush, vdec, "OH_VideoDecoder_Flush");
    SYM(outputDescription, vdec, "OH_VideoDecoder_GetOutputDescription");
    SYM(pushInput, vdec, "OH_VideoDecoder_PushInputBuffer");
    SYM(renderOutput, vdec, "OH_VideoDecoder_RenderOutputBuffer");
    SYM(freeOutput, vdec, "OH_VideoDecoder_FreeOutputBuffer");
    SYM(bufferAddr, core, "OH_AVBuffer_GetAddr");
    SYM(bufferCapacity, core, "OH_AVBuffer_GetCapacity");
    SYM(bufferGetAttr, core, "OH_AVBuffer_GetBufferAttr");
    SYM(bufferSetAttr, core, "OH_AVBuffer_SetBufferAttr");
    SYM(formatCreate, core, "OH_AVFormat_Create");
    SYM(formatDestroy, core, "OH_AVFormat_Destroy");
    SYM(formatSetInt, core, "OH_AVFormat_SetIntValue");
    SYM(formatSetBuffer, core, "OH_AVFormat_SetBuffer");
    SYM(formatGetInt, core, "OH_AVFormat_GetIntValue");
    SYM(capability, base, "OH_AVCodec_GetCapability");
    SYM(capabilityByCategory, base, "OH_AVCodec_GetCapabilityByCategory");
    SYM(capabilityName, base, "OH_AVCapability_GetName");
    SYM(capabilityProfiles, base, "OH_AVCapability_GetSupportedProfiles");
    SYM(widthRange, base, "OH_AVCapability_GetVideoWidthRange");
    SYM(heightRange, base, "OH_AVCapability_GetVideoHeightRange");
    SYM(keyWidth, base, "OH_MD_KEY_WIDTH");
    SYM(keyHeight, base, "OH_MD_KEY_HEIGHT");
    SYM(keyPixelFormat, base, "OH_MD_KEY_PIXEL_FORMAT");
    SYM(keyStride, base, "OH_MD_KEY_VIDEO_STRIDE");
    SYM(keyCodecConfig, base, "OH_MD_KEY_CODEC_CONFIG");
    SYM(keyProfile, base, "OH_MD_KEY_PROFILE");
    SYM(keySliceHeight, base, "OH_MD_KEY_VIDEO_SLICE_HEIGHT");
#undef SYM
    if (!oh.create || !oh.destroy || !oh.registerCallback || !oh.configure || !oh.prepare || !oh.start
            || !oh.stop || !oh.flush || !oh.pushInput || !oh.freeOutput || !oh.bufferAddr
            || !oh.bufferCapacity || !oh.bufferGetAttr || !oh.bufferSetAttr || !oh.formatCreate
            || !oh.formatDestroy || !oh.formatSetInt || !oh.formatGetInt || !oh.keyWidth
            || !oh.keyHeight || !oh.keyPixelFormat) {
        vlog("[WL-VIDEOCODEC] OH video decoder is missing a required entry point");
        return false;
    }
    oh.loaded = 1;
    return true;
}

/* ---- the video decoders this process can offer ------------------------------------------ */

struct Offered {
    const char* mime;
    const char* name;           /* MediaCodecList's name for it */
    int profile, level;         /* a CodecProfileLevel the capabilities are derived from */
    int minWidth, maxWidth, minHeight, maxHeight;
    /* OH's software decoder for the type, when it has one: the board's hardware decoder refuses
     * frames below its minimum (176x144), which small clips and thumbnails are. */
    std::string software;
    int swMinWidth, swMaxWidth, swMinHeight, swMaxHeight;
    std::vector<int32_t> profiles, swProfiles;   /* OH profiles each decoder reports */
};

/* Android's own profile and level constants (MediaCodecInfo.CodecProfileLevel): High@5.1 for AVC,
 * Main@Main-tier 5.1 for HEVC, the levels a 1080p and 4K stream need. */
std::vector<Offered> g_offered;
bool g_offeredDone = false;

std::vector<int32_t> profilesOf(OH_AVCapability* capability)
{
    const int32_t* profiles = nullptr;
    uint32_t count = 0;
    if (oh.capabilityProfiles == nullptr || oh.capabilityProfiles(capability, &profiles, &count) != 0
            || profiles == nullptr) return {};
    return std::vector<int32_t>(profiles, profiles + count);
}

std::string profileList(const std::vector<int32_t>& profiles)
{
    std::string out;
    for (int32_t p : profiles) out += (out.empty() ? "" : ",") + std::to_string(p);
    return out.empty() ? "?" : out;
}

void findOffered()
{
    if (g_offeredDone) return;
    g_offeredDone = true;
    static const Offered kCandidates[] = {
        { "video/avc", "OH.video.avc.decoder", 8, 0x8000, 0, 0, 0, 0, "", 0, 0, 0, 0, {}, {} },
        { "video/hevc", "OH.video.hevc.decoder", 1, 0x10000, 0, 0, 0, 0, "", 0, 0, 0, 0, {}, {} },
    };
    if (!loadOh() || oh.capability == nullptr) return;
    for (const Offered& candidate : kCandidates) {
        OH_AVCapability* capability = oh.capability(candidate.mime, false);
        if (capability == nullptr) continue;
        Offered offered = candidate;
        OH_AVRange width{0, 0}, height{0, 0};
        if (oh.widthRange != nullptr && oh.widthRange(capability, &width) == 0) {
            offered.minWidth = width.minVal; offered.maxWidth = width.maxVal;
        }
        if (oh.heightRange != nullptr && oh.heightRange(capability, &height) == 0) {
            offered.minHeight = height.minVal; offered.maxHeight = height.maxVal;
        }
        const char* hardware = oh.capabilityName != nullptr ? oh.capabilityName(capability) : nullptr;
        /* Category 1 is OH's SOFTWARE; the list then offers the sizes either decoder takes. */
        OH_AVCapability* sw = oh.capabilityByCategory != nullptr && oh.createByName != nullptr
                ? oh.capabilityByCategory(candidate.mime, false, 1) : nullptr;
        const char* swName = sw != nullptr && oh.capabilityName != nullptr ? oh.capabilityName(sw) : nullptr;
        if (swName != nullptr && (hardware == nullptr || strcmp(swName, hardware) != 0)) {
            offered.software = swName;
            OH_AVRange w{0, 0}, h{0, 0};
            if (oh.widthRange != nullptr && oh.widthRange(sw, &w) == 0) {
                offered.swMinWidth = w.minVal; offered.swMaxWidth = w.maxVal;
            }
            if (oh.heightRange != nullptr && oh.heightRange(sw, &h) == 0) {
                offered.swMinHeight = h.minVal; offered.swMaxHeight = h.maxVal;
            }
        }
        offered.profiles = profilesOf(capability);
        offered.swProfiles = sw != nullptr ? profilesOf(sw) : std::vector<int32_t>();
        g_offered.push_back(offered);
        vlog("[WL-VIDEOCODEC] %s profiles: hardware %s; software %s", candidate.mime,
             profileList(offered.profiles).c_str(), profileList(offered.swProfiles).c_str());
        vlog("[WL-VIDEOCODEC] offers %s as %s (%s), %dx%d..%dx%d; software %s %dx%d..%dx%d",
             candidate.mime, candidate.name, hardware != nullptr ? hardware : "?",
             offered.minWidth, offered.minHeight, offered.maxWidth, offered.maxHeight,
             offered.software.empty() ? "none" : offered.software.c_str(), offered.swMinWidth,
             offered.swMinHeight, offered.swMaxWidth, offered.swMaxHeight);
    }
}

const Offered* offeredByName(const char* name)
{
    for (const Offered& offered : g_offered) if (strcmp(offered.name, name) == 0) return &offered;
    return nullptr;
}

const Offered* offeredByMime(const char* mime)
{
    for (const Offered& offered : g_offered) if (strcmp(offered.mime, mime) == 0) return &offered;
    return nullptr;
}

/* ---- one video decoder ----------------------------------------------------------------- */

/* MediaCodec's dequeue answers and buffer flag. */
enum { MC_TRY_AGAIN = -1, MC_FORMAT_CHANGED = -2 };
enum { MC_FLAG_SYNC = 1, MC_FLAG_CODEC_CONFIG = 2, MC_FLAG_EOS = 4 };
/* MediaCodecInfo.CodecCapabilities color formats. */
enum { COLOR_YUV420_SEMIPLANAR = 21, COLOR_YUV420_FLEXIBLE = 0x7F420888, COLOR_SURFACE = 0x7F000789 };

struct Video {
    OH_AVCodec* codec = nullptr;
    std::string name, mime;
    std::mutex mu;
    std::condition_variable cv;
    std::deque<uint32_t> inputs;                 /* free input indices, in OH's order */
    struct Output { uint32_t index; OH_AVCodecBufferAttr attr; };
    std::deque<Output> outputs;
    std::map<uint32_t, OH_AVBuffer*> inputBuffers, outputBuffers;
    std::vector<uint8_t> codecData;              /* csd-0 then csd-1, submitted first */
    bool codecDataSent = false;
    int inputsLogged = 0;                        /* the first inputs are logged, as evidence */
    void* window = nullptr;                      /* ANativeWindow from the Surface, released at the end */
    bool surface = false;
    bool software = false;                       /* OH's software decoder replaced the hardware one */
    bool err = false;
    bool running = false;                       /* OH started, and not stopped since */
    bool formatPending = true;                   /* sync mode: FORMAT_CHANGED before the first output */
    int32_t width = 0, height = 0, stride = 0, sliceHeight = 0;
    jobject javaCodec = nullptr;                 /* async mode */
    jobject callback = nullptr;
    bool asyncFormatSent = false;
};

std::mutex g_registry;
std::set<Video*> g_videos;

jfieldID g_context;
jfieldID g_infoOffset, g_infoSize, g_infoPts, g_infoFlags;
JavaVM* g_vm;
jclass g_hashMap, g_integer, g_infoClass, g_formatClass, g_string;
jmethodID g_hashMapInit, g_hashMapPut, g_integerValueOf, g_infoInit, g_infoSet, g_formatFromMap;
jmethodID g_cbInput, g_cbOutput, g_cbFormat;
jmethodID g_bufferOrder;
jobject g_nativeOrder;

Video* videoOf(JNIEnv* env, jobject thiz)
{
    if (g_context == nullptr) return nullptr;
    Video* v = reinterpret_cast<Video*>((intptr_t) env->GetLongField(thiz, g_context));
    std::lock_guard<std::mutex> l(g_registry);
    return g_videos.count(v) ? v : nullptr;
}

/* OH's callback threads call into Java for the callback mode; ART must see them detach. */
pthread_key_t g_detachKey;
pthread_once_t g_detachOnce = PTHREAD_ONCE_INIT;
void detachThread(void*) { if (g_vm != nullptr) g_vm->DetachCurrentThread(); }
void makeDetachKey() { pthread_key_create(&g_detachKey, detachThread); }
JNIEnv* attachedEnv()
{
    JNIEnv* env = nullptr;
    if (g_vm == nullptr) return nullptr;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != 0) return nullptr;
        pthread_once(&g_detachOnce, makeDetachKey);
        pthread_setspecific(g_detachKey, reinterpret_cast<void*>(1));
    }
    return env;
}

jobject newMap(JNIEnv* env) { return env->NewObject(g_hashMap, g_hashMapInit); }

void mapPut(JNIEnv* env, jobject map, const char* key, jobject value)
{
    jstring k = env->NewStringUTF(key);
    if (k != nullptr && value != nullptr) env->CallObjectMethod(map, g_hashMapPut, k, value);
    if (k != nullptr) env->DeleteLocalRef(k);
    if (value != nullptr) env->DeleteLocalRef(value);
}

void mapPutInt(JNIEnv* env, jobject map, const char* key, int value)
{
    mapPut(env, map, key, env->CallStaticObjectMethod(g_integer, g_integerValueOf, value));
}

void mapPutString(JNIEnv* env, jobject map, const char* key, const char* value)
{
    mapPut(env, map, key, env->NewStringUTF(value));
}

/* The decoded picture's format, as MediaCodec reports an output format. */
jobject outputFormat(JNIEnv* env, Video* v)
{
    jobject map = newMap(env);
    if (map == nullptr) return nullptr;
    int32_t width = v->width, height = v->height, stride = v->stride, slice = v->sliceHeight;
    mapPutString(env, map, "mime", "video/raw");
    mapPutInt(env, map, "width", width);
    mapPutInt(env, map, "height", height);
    mapPutInt(env, map, "stride", stride > 0 ? stride : width);
    mapPutInt(env, map, "slice-height", slice > 0 ? slice : height);
    mapPutInt(env, map, "color-format", v->surface ? COLOR_SURFACE : COLOR_YUV420_SEMIPLANAR);
    mapPutInt(env, map, "crop-left", 0);
    mapPutInt(env, map, "crop-top", 0);
    mapPutInt(env, map, "crop-right", width > 0 ? width - 1 : 0);
    mapPutInt(env, map, "crop-bottom", height > 0 ? height - 1 : 0);
    if (env->ExceptionCheck()) env->ExceptionClear();
    return map;
}

/* What OH says the picture is now: its size and, for ByteBuffer output, the plane layout. */
void readOutputDescription(Video* v, OH_AVFormat* format)
{
    int32_t value = 0;
    if (format == nullptr) return;
    if (oh.formatGetInt(format, *oh.keyWidth, &value) && value > 0) v->width = value;
    if (oh.formatGetInt(format, *oh.keyHeight, &value) && value > 0) v->height = value;
    if (oh.keyStride != nullptr && oh.formatGetInt(format, *oh.keyStride, &value) && value > 0) v->stride = value;
    if (oh.keySliceHeight != nullptr && oh.formatGetInt(format, *oh.keySliceHeight, &value) && value > 0)
        v->sliceHeight = value;
}

void fireAsyncFormat(JNIEnv* env, Video* v)
{
    if (v->asyncFormatSent || g_cbFormat == nullptr || g_formatFromMap == nullptr) return;
    v->asyncFormatSent = true;
    jobject map = outputFormat(env, v);
    jobject format = map != nullptr ? env->NewObject(g_formatClass, g_formatFromMap, map) : nullptr;
    if (format != nullptr) env->CallVoidMethod(v->callback, g_cbFormat, v->javaCodec, format);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (format != nullptr) env->DeleteLocalRef(format);
    if (map != nullptr) env->DeleteLocalRef(map);
}

/* The first bytes of an input, for the log of what a player feeds the decoder first. */
std::string headBytes(const uint8_t* data, int32_t size)
{
    std::string out;
    char hex[4];
    for (int32_t i = 0; data != nullptr && i < size && i < 12; i++) {
        snprintf(hex, sizeof hex, "%02x", data[i]);
        out += hex;
    }
    return out;
}

/* Fills an input buffer with the codec-specific data and gives it straight back to OH. */
bool submitCodecData(Video* v, uint32_t index, OH_AVBuffer* buffer)
{
    if (v->codecDataSent || v->codecData.empty() || buffer == nullptr) return false;
    uint8_t* data = oh.bufferAddr(buffer);
    int32_t capacity = oh.bufferCapacity(buffer);
    v->codecDataSent = true;
    if (data == nullptr || capacity < (int32_t) v->codecData.size()) {
        vlog("[WL-VIDEOCODEC] codec data (%zu bytes) does not fit an input buffer (%d)",
             v->codecData.size(), capacity);
        return false;
    }
    memcpy(data, v->codecData.data(), v->codecData.size());
    vlog("[WL-VIDEOCODEC] codec data %zu bytes %s", v->codecData.size(),
         headBytes(v->codecData.data(), (int32_t) v->codecData.size()).c_str());
    OH_AVCodecBufferAttr attr{0, (int32_t) v->codecData.size(), 0, (uint32_t) OH_FLAG_CODEC_DATA};
    oh.bufferSetAttr(buffer, &attr);
    return oh.pushInput(v->codec, index) == 0;
}

void onError(OH_AVCodec*, int32_t error, void* user)
{
    Video* v = static_cast<Video*>(user);
    { std::lock_guard<std::mutex> l(v->mu); v->err = true; }
    v->cv.notify_all();
    vlog("[WL-VIDEOCODEC] OH decoder error %d (%s)", error, v->mime.c_str());
}

void onStreamChanged(OH_AVCodec*, OH_AVFormat* format, void* user)
{
    Video* v = static_cast<Video*>(user);
    {
        std::lock_guard<std::mutex> l(v->mu);
        readOutputDescription(v, format);
        v->formatPending = true;
        v->asyncFormatSent = false;
    }
    vlog("[WL-VIDEOCODEC] stream %dx%d stride %d slice %d", v->width, v->height, v->stride, v->sliceHeight);
}

void onNeedInput(OH_AVCodec*, uint32_t index, OH_AVBuffer* buffer, void* user)
{
    Video* v = static_cast<Video*>(user);
    bool forApp;
    {
        std::lock_guard<std::mutex> l(v->mu);
        forApp = !submitCodecData(v, index, buffer);
        if (forApp) { v->inputs.push_back(index); v->inputBuffers[index] = buffer; }
    }
    if (!forApp) return;
    v->cv.notify_all();
    if (v->callback != nullptr && g_cbInput != nullptr) {
        JNIEnv* env = attachedEnv();
        if (env == nullptr) return;
        env->CallVoidMethod(v->callback, g_cbInput, v->javaCodec, (jint) index);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
}

void onNewOutput(OH_AVCodec*, uint32_t index, OH_AVBuffer* buffer, void* user)
{
    Video* v = static_cast<Video*>(user);
    OH_AVCodecBufferAttr attr{0, 0, 0, 0};
    if (buffer != nullptr) oh.bufferGetAttr(buffer, &attr);
    {
        std::lock_guard<std::mutex> l(v->mu);
        /* Rendered to a window, OH's frame carries no bytes for the app, so its size is 0; Android
         * reports the frame's size there, and players take a size-0 output for an empty one (or
         * the end of the stream) and drop it without rendering. */
        if (v->surface && attr.size == 0 && !(attr.flags & OH_FLAG_EOS)) {
            int32_t w = v->stride > 0 ? v->stride : v->width, h = v->sliceHeight > 0 ? v->sliceHeight : v->height;
            attr.size = w > 0 && h > 0 ? w * h * 3 / 2 : 1;
        }
        v->outputs.push_back({index, attr});
        v->outputBuffers[index] = buffer;
    }
    v->cv.notify_all();
    if (v->callback != nullptr && g_cbOutput != nullptr) {
        JNIEnv* env = attachedEnv();
        if (env == nullptr) return;
        fireAsyncFormat(env, v);
        jobject info = env->NewObject(g_infoClass, g_infoInit);
        if (info != nullptr) {
            jint flags = ((attr.flags & OH_FLAG_EOS) ? MC_FLAG_EOS : 0) | ((attr.flags & OH_FLAG_SYNC) ? MC_FLAG_SYNC : 0);
            env->CallVoidMethod(info, g_infoSet, attr.offset, attr.size, (jlong) attr.pts, flags);
            env->CallVoidMethod(v->callback, g_cbOutput, v->javaCodec, (jint) index, info);
            env->DeleteLocalRef(info);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
}

/* ---- the bridge's natives, kept for every codec that is not ours -------------------------- */

void* bridgeNative(JNIEnv* env, jclass c, const char* name, const char* signature, bool isStatic)
{
    jmethodID m = isStatic ? env->GetStaticMethodID(c, name, signature) : env->GetMethodID(c, name, signature);
    if (m == nullptr) { env->ExceptionClear(); return nullptr; }
    void* entry = *reinterpret_cast<void* const*>(reinterpret_cast<const char*>(m) + 16);  /* ArtMethod data_ */
    Dl_info info;
    if (entry == nullptr || dladdr(entry, &info) == 0 || info.dli_fname == nullptr
            || strstr(info.dli_fname, "liboh_adapter_bridge") == nullptr) {
        return nullptr;
    }
    return entry;
}

/* The bridge's MediaCodec natives. */
void (*b_setup)(JNIEnv*, jobject, jstring, jboolean, jboolean, jint, jint);
void (*b_configure)(JNIEnv*, jobject, jobjectArray, jobjectArray, jobject, jobject, jobject, jint);
void (*b_start)(JNIEnv*, jobject);
void (*b_stop)(JNIEnv*, jobject);
void (*b_flush)(JNIEnv*, jobject);
void (*b_reset)(JNIEnv*, jobject);
void (*b_release)(JNIEnv*, jobject);
void (*b_finalize)(JNIEnv*, jobject);
jint (*b_dequeueInput)(JNIEnv*, jobject, jlong);
jint (*b_dequeueOutput)(JNIEnv*, jobject, jobject, jlong);
void (*b_queueInput)(JNIEnv*, jobject, jint, jint, jint, jlong, jint);
void (*b_releaseOutput)(JNIEnv*, jobject, jint, jboolean, jboolean, jlong);
jobject (*b_getBuffer)(JNIEnv*, jobject, jboolean, jint);
jobject (*b_outputFormat)(JNIEnv*, jobject, jint);
jobject (*b_format)(JNIEnv*, jobject, jboolean);
jstring (*b_canonicalName)(JNIEnv*, jobject);
void (*b_setCallback)(JNIEnv*, jobject, jobject);
/* The bridge's MediaCodecList natives (static). */
jint (*b_count)(JNIEnv*, jclass);
jstring (*b_name)(JNIEnv*, jclass, jint);
jstring (*b_canonical)(JNIEnv*, jclass, jint);
jint (*b_attributes)(JNIEnv*, jclass, jint);
jint (*b_find)(JNIEnv*, jclass, jstring);
jobjectArray (*b_types)(JNIEnv*, jclass, jint);
jobject (*b_capabilities)(JNIEnv*, jclass, jint, jstring);

void throwState(JNIEnv* env, const char* message)
{
    jclass c = env->FindClass("java/lang/IllegalStateException");
    if (c != nullptr) env->ThrowNew(c, message);
}

/* ---- MediaCodec ------------------------------------------------------------------------ */

void wSetup(JNIEnv* env, jobject thiz, jstring jname, jboolean nameIsType, jboolean encoder, jint pid, jint uid)
{
    const char* name = jname != nullptr ? env->GetStringUTFChars(jname, nullptr) : nullptr;
    const Offered* offered = nullptr;
    if (name != nullptr && !encoder) {
        findOffered();
        offered = nameIsType ? offeredByMime(name) : offeredByName(name);
    }
    if (offered == nullptr) {
        if (name != nullptr) env->ReleaseStringUTFChars(jname, name);
        if (b_setup != nullptr) b_setup(env, thiz, jname, nameIsType, encoder, pid, uid);
        return;
    }
    env->ReleaseStringUTFChars(jname, name);
    Video* v = new Video();
    v->name = offered->name;
    v->mime = offered->mime;
    v->codec = oh.create(offered->mime);
    if (v->codec == nullptr) {
        delete v;
        /* AOSP's answer when the component cannot be allocated. */
        jclass c = env->FindClass("java/io/IOException");
        if (c != nullptr) env->ThrowNew(c, "Failed to allocate component instance");
        return;
    }
    OH_AVCodecCallback callbacks{onError, onStreamChanged, onNeedInput, onNewOutput};
    oh.registerCallback(v->codec, callbacks, v);
    { std::lock_guard<std::mutex> l(g_registry); g_videos.insert(v); }
    env->SetLongField(thiz, g_context, (jlong) (intptr_t) v);
    vlog("[WL-VIDEOCODEC] created %s", v->name.c_str());
}

/* The hardware decoder refused the format (a size below its minimum): replace it with OH's
 * software decoder for the type, before anything else has been attached to the codec. */
int configureSoftware(Video* v, OH_AVFormat* format)
{
    const Offered* offered = offeredByMime(v->mime.c_str());
    if (offered == nullptr || offered->software.empty() || v->software) return -1;
    OH_AVCodec* codec = oh.createByName(offered->software.c_str());
    if (codec == nullptr) return -1;
    OH_AVCodecCallback callbacks{onError, onStreamChanged, onNeedInput, onNewOutput};
    oh.registerCallback(codec, callbacks, v);
    int rc = oh.configure(codec, format);
    vlog("[WL-VIDEOCODEC] hardware refused %dx%d (or its profile); %s -> %d", v->width, v->height,
         offered->software.c_str(), rc);
    if (rc != 0) { oh.destroy(codec); return rc; }
    oh.destroy(v->codec);
    v->codec = codec;
    v->software = true;
    return 0;
}

/* A ByteBuffer value of the configure format (csd-0, csd-1), appended to the codec data. */
void appendBytes(JNIEnv* env, jobject value, std::vector<uint8_t>& out)
{
    jclass c = env->GetObjectClass(value);
    jmethodID remaining = env->GetMethodID(c, "remaining", "()I");
    jmethodID position = env->GetMethodID(c, "position", "()I");
    if (remaining == nullptr || position == nullptr) { env->ExceptionClear(); return; }
    jint size = env->CallIntMethod(value, remaining);
    jint start = env->CallIntMethod(value, position);
    uint8_t* direct = static_cast<uint8_t*>(env->GetDirectBufferAddress(value));
    if (direct != nullptr && size > 0) {
        out.insert(out.end(), direct + start, direct + start + size);
        return;
    }
    jmethodID get = env->GetMethodID(c, "get", "(I)B");
    for (jint i = 0; get != nullptr && i < size; i++) out.push_back((uint8_t) env->CallByteMethod(value, get, start + i));
    if (env->ExceptionCheck()) env->ExceptionClear();
}

/*
 * Whether the hardware decoder really decodes a profile it lists. The board's AVC decoder lists
 * Main and High but fails ("unknown error", OH error 6) on the first frame of any Main or High
 * stream, which is nearly every real video; a player told the codec failed does not try another.
 * So the first stream of each profile is preceded by one frame of that profile (a 176x144 gray
 * IDR, the decoder's smallest size), decoded through a hardware decoder of its own, and a profile
 * that fails goes to the software decoder. The answer is kept for the process.
 */
const uint8_t kSelfTestBaseline[] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0a, 0xd9, 0x02, 0xc4, 0xec, 0x04, 0x40, 0x00, 0x00,
    0x03, 0x00, 0x40, 0x00, 0x00, 0x03, 0x00, 0x83, 0xc4, 0x89, 0x92, 0x00, 0x00, 0x00, 0x01, 0x68,
    0xcb, 0x83, 0xcb, 0x20, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x05, 0x7c, 0x98, 0xa0, 0x00, 0x20,
    0x5b, 0x27, 0x27, 0x27, 0x27, 0x27, 0x27, 0x27, 0x27, 0x27, 0x27, 0x5d, 0x75, 0xd7, 0x5d, 0x75,
    0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7,
    0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d,
    0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75,
    0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x80,
};
const uint8_t kSelfTestMain[] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x0a, 0xec, 0xa1, 0x62, 0x76, 0x02, 0x20, 0x00, 0x00,
    0x03, 0x00, 0x20, 0x00, 0x00, 0x03, 0x00, 0x41, 0xe2, 0x44, 0xb2, 0xc0, 0x00, 0x00, 0x00, 0x01,
    0x68, 0xeb, 0xe3, 0xcb, 0x20, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, 0x15, 0xff, 0xfe, 0xf7,
    0xc9, 0xef, 0xc0, 0x9f, 0xd2, 0xad, 0x6f, 0xe4, 0xf3, 0xd2, 0x3f, 0x0b, 0x2d, 0xcf, 0xb0, 0x8a,
    0x8f, 0xdb, 0x9f, 0x6c, 0xe4, 0x15, 0x9d, 0x5a, 0xe9, 0x1c, 0xb9, 0x50, 0x7d, 0x20, 0xfa, 0x00,
    0x71, 0xc0, 0x04, 0x70, 0x27, 0x1c, 0xb8, 0xcf, 0xe1,
};
const uint8_t kSelfTestHigh[] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x0a, 0xac, 0xd9, 0x42, 0xc4, 0xec, 0x04, 0x40, 0x00,
    0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x03, 0x00, 0x83, 0xc4, 0x89, 0x65, 0x80, 0x00, 0x00, 0x00,
    0x01, 0x68, 0xeb, 0xe3, 0xcb, 0x22, 0xc0, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, 0x15, 0xff,
    0xfe, 0xf7, 0xc9, 0xef, 0xc0, 0x9f, 0xd2, 0xad, 0x6f, 0xe4, 0xf3, 0xd2, 0x3f, 0x0b, 0x2d, 0xcf,
    0xb0, 0x8a, 0x8f, 0xdb, 0x9f, 0x6c, 0xe4, 0x15, 0x9d, 0x5a, 0xe9, 0x1c, 0xb9, 0x50, 0x7d, 0x20,
    0xfa, 0x00, 0x71, 0xc0, 0x04, 0x70, 0x27, 0x1c, 0xb8, 0xcf, 0xe1,
};

struct SelfTest {
    std::mutex mu;
    std::condition_variable cv;
    const uint8_t* data = nullptr;
    size_t size = 0;
    int stage = 0;       /* 0 frame to send, 1 end of stream to send, 2 all sent */
    int result = -1;     /* -1 pending, 0 failed, 1 decoded */
};

void selfTestError(OH_AVCodec*, int32_t, void* user)
{
    SelfTest* t = static_cast<SelfTest*>(user);
    { std::lock_guard<std::mutex> l(t->mu); if (t->result < 0) t->result = 0; }
    t->cv.notify_all();
}

void selfTestFormat(OH_AVCodec*, OH_AVFormat*, void*) {}

void selfTestInput(OH_AVCodec* codec, uint32_t index, OH_AVBuffer* buffer, void* user)
{
    SelfTest* t = static_cast<SelfTest*>(user);
    std::lock_guard<std::mutex> l(t->mu);
    if (t->stage == 0 && (int32_t) t->size <= oh.bufferCapacity(buffer)) {
        memcpy(oh.bufferAddr(buffer), t->data, t->size);
        OH_AVCodecBufferAttr attr{0, (int32_t) t->size, 0, (uint32_t) OH_FLAG_SYNC};
        oh.bufferSetAttr(buffer, &attr);
    } else if (t->stage <= 1) {
        OH_AVCodecBufferAttr attr{0, 0, 0, (uint32_t) OH_FLAG_EOS};
        oh.bufferSetAttr(buffer, &attr);
        t->stage = 1;
    } else {
        return;
    }
    t->stage++;
    oh.pushInput(codec, index);
}

void selfTestOutput(OH_AVCodec* codec, uint32_t index, OH_AVBuffer* buffer, void* user)
{
    SelfTest* t = static_cast<SelfTest*>(user);
    OH_AVCodecBufferAttr attr{0, 0, 0, 0};
    if (buffer != nullptr) oh.bufferGetAttr(buffer, &attr);
    oh.freeOutput(codec, index);
    { std::lock_guard<std::mutex> l(t->mu); if (t->result < 0 && attr.size > 0) t->result = 1; }
    t->cv.notify_all();
}

bool hardwareDecodes(const std::string& mime, int profile)
{
    static std::mutex mu;
    static std::map<int, bool> known;
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (mime != "video/avc") return true;
    switch (profile) {
    case 0: data = kSelfTestBaseline; size = sizeof kSelfTestBaseline; break;
    case 8: data = kSelfTestMain; size = sizeof kSelfTestMain; break;
    case 4: data = kSelfTestHigh; size = sizeof kSelfTestHigh; break;
    default: return true;
    }
    std::lock_guard<std::mutex> once(mu);
    auto it = known.find(profile);
    if (it != known.end()) return it->second;
    SelfTest* t = new SelfTest;   /* kept: a late callback from the destroyed codec may still use it */
    t->data = data;
    t->size = size;
    OH_AVCodec* codec = oh.create(mime.c_str());
    int rc = -1;
    if (codec != nullptr) {
        OH_AVCodecCallback callbacks{selfTestError, selfTestFormat, selfTestInput, selfTestOutput};
        OH_AVFormat* format = oh.formatCreate();
        oh.formatSetInt(format, *oh.keyWidth, 176);
        oh.formatSetInt(format, *oh.keyHeight, 144);
        oh.formatSetInt(format, *oh.keyPixelFormat, OH_PIXEL_NV12);
        rc = oh.registerCallback(codec, callbacks, t);
        if (rc == 0) rc = oh.configure(codec, format);
        oh.formatDestroy(format);
        if (rc == 0) rc = oh.prepare(codec);
        if (rc == 0) rc = oh.start(codec);
    }
    bool decodes = true;   /* a test that could not run says nothing against the decoder */
    if (rc == 0) {
        std::unique_lock<std::mutex> l(t->mu);
        t->cv.wait_for(l, std::chrono::milliseconds(1500), [&] { return t->result >= 0; });
        decodes = t->result != 0;
        vlog("[WL-VIDEOCODEC] hardware %s profile %d self-test: %s", mime.c_str(), profile,
             t->result == 1 ? "decoded" : t->result == 0 ? "failed, software instead" : "no answer");
    } else {
        vlog("[WL-VIDEOCODEC] hardware %s profile %d self-test could not run (%d)", mime.c_str(), profile, rc);
    }
    if (codec != nullptr) {
        oh.stop(codec);
        oh.destroy(codec);
    }
    known[profile] = decodes;
    return decodes;
}

/* The OH profile of an AVC stream, from the profile_idc of the SPS in its codec data; -1 if none. */
int avcProfile(const Video* v)
{
    if (v->mime != "video/avc") return -1;
    const std::vector<uint8_t>& d = v->codecData;
    for (size_t i = 0; i + 4 < d.size(); i++) {
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1 && (d[i + 3] & 0x1f) == 7) {
            switch (d[i + 4]) {
            case 66: return 0;   /* AVC_PROFILE_BASELINE */
            case 77: return 8;   /* AVC_PROFILE_MAIN */
            case 100: return 4;  /* AVC_PROFILE_HIGH */
            default: return -1;
            }
        }
    }
    return -1;
}

void wConfigure(JNIEnv* env, jobject thiz, jobjectArray keys, jobjectArray values, jobject surface,
                jobject crypto, jobject descrambler, jint flags)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) {
        if (b_configure != nullptr) b_configure(env, thiz, keys, values, surface, crypto, descrambler, flags);
        return;
    }
    jclass integer = env->FindClass("java/lang/Integer");
    jmethodID intValue = integer != nullptr ? env->GetMethodID(integer, "intValue", "()I") : nullptr;
    jclass buffer = env->FindClass("java/nio/ByteBuffer");
    std::vector<uint8_t> csd0, csd1, csd2;
    int n = keys != nullptr ? env->GetArrayLength(keys) : 0;
    for (int i = 0; i < n; i++) {
        jstring key = (jstring) env->GetObjectArrayElement(keys, i);
        jobject value = env->GetObjectArrayElement(values, i);
        const char* k = key != nullptr ? env->GetStringUTFChars(key, nullptr) : nullptr;
        if (k != nullptr && value != nullptr) {
            if (intValue != nullptr && env->IsInstanceOf(value, integer)) {
                int iv = env->CallIntMethod(value, intValue);
                if (strcmp(k, "width") == 0) v->width = iv;
                else if (strcmp(k, "height") == 0) v->height = iv;
            } else if (buffer != nullptr && env->IsInstanceOf(value, buffer)) {
                if (strcmp(k, "csd-0") == 0) appendBytes(env, value, csd0);
                else if (strcmp(k, "csd-1") == 0) appendBytes(env, value, csd1);
                else if (strcmp(k, "csd-2") == 0) appendBytes(env, value, csd2);
            }
        }
        if (k != nullptr) env->ReleaseStringUTFChars(key, k);
        if (key != nullptr) env->DeleteLocalRef(key);
        if (value != nullptr) env->DeleteLocalRef(value);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    v->codecData = csd0;
    v->codecData.insert(v->codecData.end(), csd1.begin(), csd1.end());
    v->codecData.insert(v->codecData.end(), csd2.begin(), csd2.end());
    v->codecDataSent = false;
    if (crypto != nullptr) {
        throwState(env, "secure decoding is not supported");
        return;
    }
    OH_AVFormat* format = oh.formatCreate();
    oh.formatSetInt(format, *oh.keyWidth, v->width);
    oh.formatSetInt(format, *oh.keyHeight, v->height);
    if (surface == nullptr) oh.formatSetInt(format, *oh.keyPixelFormat, OH_PIXEL_NV12);
    /* OH's own player hands the decoder its parameter sets and profile in the format, as its
     * demuxer reports them; the board's hardware AVC decoder fails on a Main-profile stream's
     * first frame when they arrive only in band. */
    if (!v->codecData.empty() && oh.formatSetBuffer != nullptr && oh.keyCodecConfig != nullptr
            && oh.formatSetBuffer(format, *oh.keyCodecConfig, v->codecData.data(), v->codecData.size())) {
        /* also in the format, as OH's player gives them; the in-band copy stays for decoders that
         * read only that */
    }
    int profile = avcProfile(v);
    if (profile >= 0 && oh.keyProfile != nullptr) oh.formatSetInt(format, *oh.keyProfile, profile);
    /* A profile the hardware decoder does not list goes to the software one, which does. */
    const Offered* offered = offeredByMime(v->mime.c_str());
    bool unlisted = profile >= 0 && offered != nullptr && !offered->profiles.empty()
            && std::find(offered->profiles.begin(), offered->profiles.end(), profile) == offered->profiles.end();
    int rc = unlisted || !hardwareDecodes(v->mime, profile) ? -1 : oh.configure(v->codec, format);
    if (rc != 0) rc = configureSoftware(v, format);
    oh.formatDestroy(format);
    /* OH takes the output window after Configure and before Prepare. */
    if (rc == 0 && surface != nullptr && oh.setSurface != nullptr) {
        /* The window behind the Java Surface: the runtime's ANativeWindow, unwrapped to the OH
         * window it stands for, as the bionic shim's eglCreateWindowSurface does. */
        typedef void* (*FromSurface)(JNIEnv*, jobject);
        typedef void* (*Unwrap)(void*);
        FromSurface fromSurface = (FromSurface) dlsym(RTLD_DEFAULT, "ANativeWindow_fromSurface");
        Unwrap unwrap = (Unwrap) dlsym(RTLD_DEFAULT, "oh_anw_get_oh");
        if (unwrap == nullptr) {
            void* bridge = dlopen("liboh_adapter_bridge.so", RTLD_NOW | RTLD_NOLOAD);
            if (bridge != nullptr) unwrap = (Unwrap) dlsym(bridge, "oh_anw_get_oh");
        }
        void* window = fromSurface != nullptr ? fromSurface(env, surface) : nullptr;
        void* ohWindow = window != nullptr && unwrap != nullptr ? unwrap(window) : nullptr;
        if (ohWindow != nullptr && oh.setSurface(v->codec, static_cast<OHNativeWindow*>(ohWindow)) == 0) {
            v->window = window;
            v->surface = true;
        } else {
            /* Without a window OH would decode to buffers in its own default format, which the
             * player configured for a Surface cannot read; refuse, as a codec that cannot attach
             * its output does. */
            vlog("[WL-VIDEOCODEC] no OH window behind the Surface (anw=%p oh=%p)", window, ohWindow);
            if (window != nullptr) {
                typedef void (*Release)(void*);
                Release release = (Release) dlsym(RTLD_DEFAULT, "ANativeWindow_release");
                if (release != nullptr) release(window);
            }
            rc = -1;
        }
    }
    int prepared = rc == 0 ? oh.prepare(v->codec) : -1;
    vlog("[WL-VIDEOCODEC] configure %s %dx%d profile %d surface=%d csd=%zu -> %d/%d", v->mime.c_str(), v->width,
         v->height, profile, v->surface, v->codecData.size(), rc, prepared);
    if (rc != 0 || prepared != 0) throwState(env, "configure failed");
}

void wStart(JNIEnv* env, jobject thiz)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) { if (b_start != nullptr) b_start(env, thiz); return; }
    /* After a flush OH is already running again (see wFlush); Android's start() there resumes. */
    if (v->running) return;
    int rc = oh.start(v->codec);
    vlog("[WL-VIDEOCODEC] start -> %d", rc);
    if (rc != 0) throwState(env, "start failed");
    else v->running = true;
}

void clearQueues(Video* v)
{
    std::lock_guard<std::mutex> l(v->mu);
    v->inputs.clear();
    v->outputs.clear();
    v->inputBuffers.clear();
    v->outputBuffers.clear();
}

void wStop(JNIEnv* env, jobject thiz)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) { if (b_stop != nullptr) b_stop(env, thiz); return; }
    oh.stop(v->codec);
    v->running = false;
    clearQueues(v);
}

void wFlush(JNIEnv* env, jobject thiz)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) { if (b_flush != nullptr) b_flush(env, thiz); return; }
    /* OH stops handing out buffers on flush and asks again after start, as Android's codec does
     * after a flush in synchronous mode; the queues are emptied first so no stale index survives. */
    clearQueues(v);
    oh.flush(v->codec);
    { std::lock_guard<std::mutex> l(v->mu); v->codecDataSent = false; }
    v->running = oh.start(v->codec) == 0;
}

void destroy(JNIEnv* env, jobject thiz, Video* v)
{
    { std::lock_guard<std::mutex> l(g_registry); g_videos.erase(v); }
    oh.stop(v->codec);
    oh.destroy(v->codec);
    if (v->window != nullptr) {
        typedef void (*Release)(void*);
        Release release = (Release) dlsym(RTLD_DEFAULT, "ANativeWindow_release");
        if (release != nullptr) release(v->window);
    }
    if (v->javaCodec != nullptr) env->DeleteGlobalRef(v->javaCodec);
    if (v->callback != nullptr) env->DeleteGlobalRef(v->callback);
    env->SetLongField(thiz, g_context, 0);
    delete v;
}

void wReset(JNIEnv* env, jobject thiz)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) { if (b_reset != nullptr) b_reset(env, thiz); return; }
    oh.stop(v->codec);
    v->running = false;
    clearQueues(v);
}

void wRelease(JNIEnv* env, jobject thiz)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) { if (b_release != nullptr) b_release(env, thiz); return; }
    destroy(env, thiz, v);
}

void wFinalize(JNIEnv* env, jobject thiz)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) { if (b_finalize != nullptr) b_finalize(env, thiz); return; }
    destroy(env, thiz, v);
}

jint wDequeueInput(JNIEnv* env, jobject thiz, jlong timeoutUs)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) return b_dequeueInput != nullptr ? b_dequeueInput(env, thiz, timeoutUs) : MC_TRY_AGAIN;
    std::unique_lock<std::mutex> l(v->mu);
    auto ready = [&] { return !v->inputs.empty() || v->err; };
    if (!ready() && timeoutUs != 0) {
        if (timeoutUs < 0) v->cv.wait(l, ready);
        else v->cv.wait_for(l, std::chrono::microseconds(timeoutUs), ready);
    }
    if (v->inputs.empty()) return MC_TRY_AGAIN;
    uint32_t index = v->inputs.front();
    v->inputs.pop_front();
    return (jint) index;
}

jobject orderedBuffer(JNIEnv* env, uint8_t* address, int32_t capacity)
{
    jobject buffer = env->NewDirectByteBuffer(address, capacity);
    if (buffer == nullptr || g_bufferOrder == nullptr || g_nativeOrder == nullptr) return buffer;
    jobject same = env->CallObjectMethod(buffer, g_bufferOrder, g_nativeOrder);
    if (same != nullptr) env->DeleteLocalRef(same);
    return buffer;
}

jobject wGetBuffer(JNIEnv* env, jobject thiz, jboolean input, jint index)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) return b_getBuffer != nullptr ? b_getBuffer(env, thiz, input, index) : nullptr;
    /* Output to a Surface has no buffer to read; Android answers null for it. */
    if (!input && v->surface) return nullptr;
    OH_AVBuffer* buffer = nullptr;
    {
        std::lock_guard<std::mutex> l(v->mu);
        auto& map = input ? v->inputBuffers : v->outputBuffers;
        auto it = map.find((uint32_t) index);
        if (it != map.end()) buffer = it->second;
    }
    uint8_t* address = buffer != nullptr ? oh.bufferAddr(buffer) : nullptr;
    int32_t capacity = buffer != nullptr ? oh.bufferCapacity(buffer) : 0;
    if (address == nullptr || capacity <= 0) return nullptr;
    return orderedBuffer(env, address, capacity);
}

void wQueueInput(JNIEnv* env, jobject thiz, jint index, jint offset, jint size, jlong pts, jint flags)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) { if (b_queueInput != nullptr) b_queueInput(env, thiz, index, offset, size, pts, flags); return; }
    OH_AVBuffer* buffer = nullptr;
    {
        std::lock_guard<std::mutex> l(v->mu);
        auto it = v->inputBuffers.find((uint32_t) index);
        if (it != v->inputBuffers.end()) { buffer = it->second; v->inputBuffers.erase(it); }
    }
    if (buffer == nullptr) { throwState(env, "no such input buffer"); return; }
    uint32_t ohFlags = ((flags & MC_FLAG_EOS) ? OH_FLAG_EOS : 0) | ((flags & MC_FLAG_CODEC_CONFIG) ? OH_FLAG_CODEC_DATA : 0);
    OH_AVCodecBufferAttr attr{pts, size, offset, ohFlags};
    if (v->inputsLogged < 3) {
        v->inputsLogged++;
        uint8_t* data = oh.bufferAddr(buffer);
        vlog("[WL-VIDEOCODEC] input %d: %d bytes at %d, flags %#x, pts %lld: %s", index, size, offset, flags,
             (long long) pts, headBytes(data != nullptr ? data + offset : nullptr, size).c_str());
    }
    oh.bufferSetAttr(buffer, &attr);
    oh.pushInput(v->codec, (uint32_t) index);
}

jint wDequeueOutput(JNIEnv* env, jobject thiz, jobject info, jlong timeoutUs)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) return b_dequeueOutput != nullptr ? b_dequeueOutput(env, thiz, info, timeoutUs) : MC_TRY_AGAIN;
    std::unique_lock<std::mutex> l(v->mu);
    auto ready = [&] { return !v->outputs.empty() || v->err; };
    if (!ready() && timeoutUs != 0) {
        if (timeoutUs < 0) v->cv.wait(l, ready);
        else v->cv.wait_for(l, std::chrono::microseconds(timeoutUs), ready);
    }
    if (v->outputs.empty()) return MC_TRY_AGAIN;
    /* The format a frame comes in is announced before the frame, as Android's codec does. */
    if (v->formatPending) {
        v->formatPending = false;
        return MC_FORMAT_CHANGED;
    }
    Video::Output out = v->outputs.front();
    v->outputs.pop_front();
    l.unlock();
    if (info != nullptr) {
        env->SetIntField(info, g_infoOffset, out.attr.offset);
        env->SetIntField(info, g_infoSize, out.attr.size);
        env->SetLongField(info, g_infoPts, out.attr.pts);
        env->SetIntField(info, g_infoFlags, ((out.attr.flags & OH_FLAG_EOS) ? MC_FLAG_EOS : 0)
                         | ((out.attr.flags & OH_FLAG_SYNC) ? MC_FLAG_SYNC : 0));
    }
    return (jint) out.index;
}

void wReleaseOutput(JNIEnv* env, jobject thiz, jint index, jboolean render, jboolean updatePts, jlong time)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) {
        if (b_releaseOutput != nullptr) b_releaseOutput(env, thiz, index, render, updatePts, time);
        return;
    }
    { std::lock_guard<std::mutex> l(v->mu); v->outputBuffers.erase((uint32_t) index); }
    if (render && v->surface && oh.renderOutput != nullptr) oh.renderOutput(v->codec, (uint32_t) index);
    else oh.freeOutput(v->codec, (uint32_t) index);
}

jobject wOutputFormat(JNIEnv* env, jobject thiz, jint index)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) return b_outputFormat != nullptr ? b_outputFormat(env, thiz, index) : nullptr;
    if (oh.outputDescription != nullptr) {
        OH_AVFormat* format = oh.outputDescription(v->codec);
        if (format != nullptr) {
            std::lock_guard<std::mutex> l(v->mu);
            readOutputDescription(v, format);
            oh.formatDestroy(format);
        }
    }
    return outputFormat(env, v);
}

jobject wFormat(JNIEnv* env, jobject thiz, jboolean input)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) return b_format != nullptr ? b_format(env, thiz, input) : nullptr;
    if (!input) return wOutputFormat(env, thiz, -1);
    jobject map = newMap(env);
    if (map == nullptr) return nullptr;
    mapPutString(env, map, "mime", v->mime.c_str());
    mapPutInt(env, map, "width", v->width);
    mapPutInt(env, map, "height", v->height);
    return map;
}

jstring wCanonicalName(JNIEnv* env, jobject thiz)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) return b_canonicalName != nullptr ? b_canonicalName(env, thiz) : nullptr;
    return env->NewStringUTF(v->name.c_str());
}

void wSetCallback(JNIEnv* env, jobject thiz, jobject callback)
{
    Video* v = videoOf(env, thiz);
    if (v == nullptr) { if (b_setCallback != nullptr) b_setCallback(env, thiz, callback); return; }
    if (g_vm == nullptr) env->GetJavaVM(&g_vm);
    if (callback != nullptr && g_cbInput == nullptr) {
        jclass c = env->GetObjectClass(callback);
        g_cbInput = env->GetMethodID(c, "onInputBufferAvailable", "(Landroid/media/MediaCodec;I)V");
        g_cbOutput = env->GetMethodID(c, "onOutputBufferAvailable",
                                      "(Landroid/media/MediaCodec;ILandroid/media/MediaCodec$BufferInfo;)V");
        g_cbFormat = env->GetMethodID(c, "onOutputFormatChanged",
                                      "(Landroid/media/MediaCodec;Landroid/media/MediaFormat;)V");
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    if (v->javaCodec != nullptr) env->DeleteGlobalRef(v->javaCodec);
    if (v->callback != nullptr) env->DeleteGlobalRef(v->callback);
    v->javaCodec = env->NewGlobalRef(thiz);
    v->callback = callback != nullptr ? env->NewGlobalRef(callback) : nullptr;
}

/* ---- MediaCodecList: the bridge's entries, then the video decoders ----------------------- */

jint bridgeCount(JNIEnv* env, jclass c) { return b_count != nullptr ? b_count(env, c) : 0; }

const Offered* listed(JNIEnv* env, jclass c, jint index)
{
    findOffered();
    jint mine = index - bridgeCount(env, c);
    return mine >= 0 && mine < (jint) g_offered.size() ? &g_offered[mine] : nullptr;
}

jint lCount(JNIEnv* env, jclass c)
{
    findOffered();
    return bridgeCount(env, c) + (jint) g_offered.size();
}

jstring lName(JNIEnv* env, jclass c, jint index)
{
    const Offered* offered = listed(env, c, index);
    if (offered != nullptr) return env->NewStringUTF(offered->name);
    return b_name != nullptr ? b_name(env, c, index) : nullptr;
}

jstring lCanonical(JNIEnv* env, jclass c, jint index)
{
    const Offered* offered = listed(env, c, index);
    if (offered != nullptr) return env->NewStringUTF(offered->name);
    return b_canonical != nullptr ? b_canonical(env, c, index) : nullptr;
}

jint lAttributes(JNIEnv* env, jclass c, jint index)
{
    /* MediaCodecInfo's attributes: 0 is a decoder that is neither software-only nor an alias,
     * and OH's video decoders are the platform's (hardware where the board has it). */
    if (listed(env, c, index) != nullptr) return 0;
    return b_attributes != nullptr ? b_attributes(env, c, index) : 0;
}

jint lFind(JNIEnv* env, jclass c, jstring name)
{
    findOffered();
    const char* n = name != nullptr ? env->GetStringUTFChars(name, nullptr) : nullptr;
    jint found = -1;
    for (size_t i = 0; n != nullptr && i < g_offered.size(); i++) {
        if (strcmp(n, g_offered[i].name) == 0) found = bridgeCount(env, c) + (jint) i;
    }
    if (n != nullptr) env->ReleaseStringUTFChars(name, n);
    if (found >= 0) return found;
    return b_find != nullptr ? b_find(env, c, name) : -1;
}

jobjectArray lTypes(JNIEnv* env, jclass c, jint index)
{
    const Offered* offered = listed(env, c, index);
    if (offered == nullptr) return b_types != nullptr ? b_types(env, c, index) : nullptr;
    /* A null initializer and then the element, as AOSP's JNI does: this ART aborts on an
     * initializer it considers incompatible (see the bridge's §497). */
    jobjectArray types = env->NewObjectArray(1, g_string, nullptr);
    if (types == nullptr) return nullptr;
    jstring mime = env->NewStringUTF(offered->mime);
    env->SetObjectArrayElement(types, 0, mime);
    env->DeleteLocalRef(mime);
    return types;
}

jobject lCapabilities(JNIEnv* env, jclass c, jint index, jstring type)
{
    const Offered* offered = listed(env, c, index);
    if (offered == nullptr) return b_capabilities != nullptr ? b_capabilities(env, c, index, type) : nullptr;
    /* The constructor AOSP's own JNI calls (android_media_MediaCodecList.cpp): profile-levels,
     * color formats, encoder, the default format and the capabilities as maps. Its video
     * capabilities derive the limits a level allows; the size range narrows them to OH's. */
    jclass levelClass = env->FindClass("android/media/MediaCodecInfo$CodecProfileLevel");
    jclass capsClass = env->FindClass("android/media/MediaCodecInfo$CodecCapabilities");
    if (levelClass == nullptr || capsClass == nullptr) { env->ExceptionClear(); return nullptr; }
    jmethodID levelInit = env->GetMethodID(levelClass, "<init>", "()V");
    jfieldID profileField = env->GetFieldID(levelClass, "profile", "I");
    jfieldID levelField = env->GetFieldID(levelClass, "level", "I");
    jmethodID capsInit = env->GetMethodID(capsClass, "<init>",
            "([Landroid/media/MediaCodecInfo$CodecProfileLevel;[IZLjava/util/Map;Ljava/util/Map;)V");
    if (levelInit == nullptr || profileField == nullptr || levelField == nullptr || capsInit == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }
    jobject level = env->NewObject(levelClass, levelInit);
    jobjectArray levels = env->NewObjectArray(1, levelClass, nullptr);
    if (level == nullptr || levels == nullptr) { env->ExceptionClear(); return nullptr; }
    env->SetIntField(level, profileField, offered->profile);
    env->SetIntField(level, levelField, offered->level);
    env->SetObjectArrayElement(levels, 0, level);
    const jint kColors[] = {COLOR_YUV420_FLEXIBLE, COLOR_YUV420_SEMIPLANAR, COLOR_SURFACE};
    jintArray colors = env->NewIntArray(3);
    if (colors != nullptr) env->SetIntArrayRegion(colors, 0, 3, kColors);
    jobject defaults = newMap(env);
    jobject details = newMap(env);
    if (defaults == nullptr || details == nullptr || colors == nullptr) { env->ExceptionClear(); return nullptr; }
    mapPutString(env, defaults, "mime", offered->mime);
    if (offered->maxWidth > 0 && offered->maxHeight > 0) {
        /* A size below the hardware minimum goes to the software decoder (configureSoftware). */
        int minWidth = offered->minWidth, minHeight = offered->minHeight;
        if (!offered->software.empty() && offered->swMinWidth > 0 && offered->swMinWidth < minWidth) minWidth = offered->swMinWidth;
        if (!offered->software.empty() && offered->swMinHeight > 0 && offered->swMinHeight < minHeight) minHeight = offered->swMinHeight;
        char range[64];
        snprintf(range, sizeof(range), "%dx%d-%dx%d", minWidth > 0 ? minWidth : 2,
                 minHeight > 0 ? minHeight : 2, offered->maxWidth, offered->maxHeight);
        mapPutString(env, details, "size-range", range);
    }
    mapPutString(env, details, "alignment", "2x2");
    mapPutString(env, details, "block-size", "16x16");
    /* Seamless resolution change, which ExoPlayer asks about to avoid recreating the codec: OH's
     * decoder reports it through onStreamChanged, which this file passes on. */
    mapPutInt(env, details, "feature-adaptive-playback", 1);
    jobject caps = env->NewObject(capsClass, capsInit, levels, colors, JNI_FALSE, defaults, details);
    if (env->ExceptionCheck()) { env->ExceptionClear(); caps = nullptr; }
    vlog("[WL-VIDEOCODEC] capabilities for %s -> %p", offered->name, (void*) caps);
    return caps;
}

}  // namespace

/* Returns how many natives were wrapped; 0 leaves both classes as the bridge registered them. */
/*
 * MediaCodec.getOwnCodecInfo, which the bridge never registered (ReelShort's player stops on it):
 * the codec's own entry of MediaCodecList, built as AOSP's JNI builds it, with the
 * MediaCodecInfo(name, canonicalName, flags, capabilities[]) constructor. It answers for the
 * bridge's audio codecs too, through the same list.
 */
jclass g_listClass;

jobject wOwnCodecInfo(JNIEnv* env, jobject thiz)
{
    jstring name = wCanonicalName(env, thiz);
    if (env->ExceptionCheck()) return nullptr;
    jint index = name != nullptr ? lFind(env, g_listClass, name) : -1;
    if (index < 0) {
        throwState(env, "the codec is not in MediaCodecList");
        return nullptr;
    }
    jclass infoClass = env->FindClass("android/media/MediaCodecInfo");
    jclass capsClass = env->FindClass("android/media/MediaCodecInfo$CodecCapabilities");
    jmethodID init = infoClass != nullptr ? env->GetMethodID(infoClass, "<init>",
            "(Ljava/lang/String;Ljava/lang/String;I[Landroid/media/MediaCodecInfo$CodecCapabilities;)V") : nullptr;
    jobjectArray types = lTypes(env, g_listClass, index);
    if (init == nullptr || capsClass == nullptr || types == nullptr) {
        env->ExceptionClear();
        throwState(env, "MediaCodecInfo unavailable");
        return nullptr;
    }
    jsize n = env->GetArrayLength(types);
    jobjectArray caps = env->NewObjectArray(n, capsClass, nullptr);
    for (jsize i = 0; caps != nullptr && i < n; i++) {
        jstring type = (jstring) env->GetObjectArrayElement(types, i);
        jobject cap = lCapabilities(env, g_listClass, index, type);
        if (env->ExceptionCheck()) return nullptr;
        env->SetObjectArrayElement(caps, i, cap);
        env->DeleteLocalRef(type);
        if (cap != nullptr) env->DeleteLocalRef(cap);
    }
    if (caps == nullptr) return nullptr;
    return env->NewObject(infoClass, init, lName(env, g_listClass, index),
                          lCanonical(env, g_listClass, index), lAttributes(env, g_listClass, index), caps);
}

extern "C" int wl_register_video_codec(JNIEnv* env)
{
    jclass codec = env->FindClass("android/media/MediaCodec");
    jclass list = env->FindClass("android/media/MediaCodecList");
    jclass info = env->FindClass("android/media/MediaCodec$BufferInfo");
    jclass format = env->FindClass("android/media/MediaFormat");
    jclass map = env->FindClass("java/util/HashMap");
    jclass integer = env->FindClass("java/lang/Integer");
    jclass string = env->FindClass("java/lang/String");
    jclass byteBuffer = env->FindClass("java/nio/ByteBuffer");
    jclass byteOrder = env->FindClass("java/nio/ByteOrder");
    if (codec == nullptr || list == nullptr || info == nullptr || format == nullptr || map == nullptr
            || integer == nullptr || string == nullptr || byteBuffer == nullptr || byteOrder == nullptr) {
        env->ExceptionClear();
        return 0;
    }
    env->GetJavaVM(&g_vm);
    g_context = env->GetFieldID(codec, "mNativeContext", "J");
    g_infoOffset = env->GetFieldID(info, "offset", "I");
    g_infoSize = env->GetFieldID(info, "size", "I");
    g_infoPts = env->GetFieldID(info, "presentationTimeUs", "J");
    g_infoFlags = env->GetFieldID(info, "flags", "I");
    g_infoClass = (jclass) env->NewGlobalRef(info);
    g_infoInit = env->GetMethodID(info, "<init>", "()V");
    g_infoSet = env->GetMethodID(info, "set", "(IIJI)V");
    g_formatClass = (jclass) env->NewGlobalRef(format);
    g_formatFromMap = env->GetMethodID(format, "<init>", "(Ljava/util/Map;)V");
    g_hashMap = (jclass) env->NewGlobalRef(map);
    g_hashMapInit = env->GetMethodID(map, "<init>", "()V");
    g_hashMapPut = env->GetMethodID(map, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    g_integer = (jclass) env->NewGlobalRef(integer);
    g_integerValueOf = env->GetStaticMethodID(integer, "valueOf", "(I)Ljava/lang/Integer;");
    g_string = (jclass) env->NewGlobalRef(string);
    g_bufferOrder = env->GetMethodID(byteBuffer, "order", "(Ljava/nio/ByteOrder;)Ljava/nio/ByteBuffer;");
    jmethodID nativeOrder = env->GetStaticMethodID(byteOrder, "nativeOrder", "()Ljava/nio/ByteOrder;");
    jobject order = nativeOrder != nullptr ? env->CallStaticObjectMethod(byteOrder, nativeOrder) : nullptr;
    if (order != nullptr) g_nativeOrder = env->NewGlobalRef(order);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (g_context == nullptr || g_infoOffset == nullptr || g_infoInit == nullptr || g_formatFromMap == nullptr
            || g_hashMapInit == nullptr || g_integerValueOf == nullptr || g_nativeOrder == nullptr) {
        vlog("[WL-VIDEOCODEC] framework classes incomplete; video decoding not offered");
        return 0;
    }

    /* The bridge's own functions first: once the wrappers are registered, ART's record holds them. */
#define KEEP(var, cls, name, sig, isStatic) *(void**) &var = bridgeNative(env, cls, name, sig, isStatic)
    KEEP(b_setup, codec, "native_setup", "(Ljava/lang/String;ZZII)V", false);
    KEEP(b_configure, codec, "native_configure",
         "([Ljava/lang/String;[Ljava/lang/Object;Landroid/view/Surface;Landroid/media/MediaCrypto;Landroid/os/IHwBinder;I)V", false);
    KEEP(b_start, codec, "native_start", "()V", false);
    KEEP(b_stop, codec, "native_stop", "()V", false);
    KEEP(b_flush, codec, "native_flush", "()V", false);
    KEEP(b_reset, codec, "native_reset", "()V", false);
    KEEP(b_release, codec, "native_release", "()V", false);
    KEEP(b_finalize, codec, "native_finalize", "()V", false);
    KEEP(b_dequeueInput, codec, "native_dequeueInputBuffer", "(J)I", false);
    KEEP(b_dequeueOutput, codec, "native_dequeueOutputBuffer", "(Landroid/media/MediaCodec$BufferInfo;J)I", false);
    KEEP(b_queueInput, codec, "native_queueInputBuffer", "(IIIJI)V", false);
    KEEP(b_releaseOutput, codec, "releaseOutputBuffer", "(IZZJ)V", false);
    KEEP(b_getBuffer, codec, "getBuffer", "(ZI)Ljava/nio/ByteBuffer;", false);
    KEEP(b_outputFormat, codec, "getOutputFormatNative", "(I)Ljava/util/Map;", false);
    KEEP(b_format, codec, "getFormatNative", "(Z)Ljava/util/Map;", false);
    KEEP(b_canonicalName, codec, "getCanonicalName", "()Ljava/lang/String;", false);
    KEEP(b_setCallback, codec, "native_setCallback", "(Landroid/media/MediaCodec$Callback;)V", false);
    KEEP(b_count, list, "native_getCodecCount", "()I", true);
    KEEP(b_name, list, "getCodecName", "(I)Ljava/lang/String;", true);
    KEEP(b_canonical, list, "getCanonicalName", "(I)Ljava/lang/String;", true);
    KEEP(b_attributes, list, "getAttributes", "(I)I", true);
    KEEP(b_find, list, "findCodecByName", "(Ljava/lang/String;)I", true);
    KEEP(b_types, list, "getSupportedTypes", "(I)[Ljava/lang/String;", true);
    KEEP(b_capabilities, list, "getCodecCapabilities",
         "(ILjava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;", true);
#undef KEEP
    if (b_setup == nullptr || b_count == nullptr) {
        /* Without the bridge's own setup and list there is nothing to keep for audio: leave the
         * classes alone rather than replace an audio path this file does not have. */
        vlog("[WL-VIDEOCODEC] the bridge's MediaCodec natives were not found; not wrapping");
        return 0;
    }

    static const JNINativeMethod kCodec[] = {
        {"native_setup", "(Ljava/lang/String;ZZII)V", (void*) wSetup},
        {"native_configure",
         "([Ljava/lang/String;[Ljava/lang/Object;Landroid/view/Surface;Landroid/media/MediaCrypto;Landroid/os/IHwBinder;I)V",
         (void*) wConfigure},
        {"native_start", "()V", (void*) wStart},
        {"native_stop", "()V", (void*) wStop},
        {"native_flush", "()V", (void*) wFlush},
        {"native_reset", "()V", (void*) wReset},
        {"native_release", "()V", (void*) wRelease},
        {"native_finalize", "()V", (void*) wFinalize},
        {"native_dequeueInputBuffer", "(J)I", (void*) wDequeueInput},
        {"native_dequeueOutputBuffer", "(Landroid/media/MediaCodec$BufferInfo;J)I", (void*) wDequeueOutput},
        {"native_queueInputBuffer", "(IIIJI)V", (void*) wQueueInput},
        {"releaseOutputBuffer", "(IZZJ)V", (void*) wReleaseOutput},
        {"getBuffer", "(ZI)Ljava/nio/ByteBuffer;", (void*) wGetBuffer},
        {"getOutputFormatNative", "(I)Ljava/util/Map;", (void*) wOutputFormat},
        {"getFormatNative", "(Z)Ljava/util/Map;", (void*) wFormat},
        {"getCanonicalName", "()Ljava/lang/String;", (void*) wCanonicalName},
        {"native_setCallback", "(Landroid/media/MediaCodec$Callback;)V", (void*) wSetCallback},
        {"getOwnCodecInfo", "()Landroid/media/MediaCodecInfo;", (void*) wOwnCodecInfo},
    };
    static const JNINativeMethod kList[] = {
        {"native_getCodecCount", "()I", (void*) lCount},
        {"getCodecName", "(I)Ljava/lang/String;", (void*) lName},
        {"getCanonicalName", "(I)Ljava/lang/String;", (void*) lCanonical},
        {"getAttributes", "(I)I", (void*) lAttributes},
        {"findCodecByName", "(Ljava/lang/String;)I", (void*) lFind},
        {"getSupportedTypes", "(I)[Ljava/lang/String;", (void*) lTypes},
        {"getCodecCapabilities", "(ILjava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;",
         (void*) lCapabilities},
    };
    g_listClass = (jclass) env->NewGlobalRef(list);
    int wrapped = 0;
    for (const JNINativeMethod& m : kCodec) {
        if (env->RegisterNatives(codec, &m, 1) == JNI_OK) wrapped++;
        else env->ExceptionClear();
    }
    for (const JNINativeMethod& m : kList) {
        if (env->RegisterNatives(list, &m, 1) == JNI_OK) wrapped++;
        else env->ExceptionClear();
    }
    return wrapped;
}
