/*
 * oh_audiotrack_shim.cpp
 *
 * [FIX-AUDIO 2026-06-30] ABI-boundary shim: android.media.AudioTrack JNI natives
 * -> OHOS OH_AudioRenderer (libohaudio NDK). This is where Android exits into the
 * platform audio backend, so adapting HERE makes sound work for every app
 * (noice/ExoPlayer, etc.) WITHOUT patching framework internals or the boot image.
 *
 * Android pushes PCM via AudioTrack.write(); OHOS pulls via a write-data callback.
 * A per-track ring buffer bridges the two. MVP: MODE_STREAM, PCM16, mono/stereo,
 * common sample rates, blocking/non-blocking write, silence-on-underrun.
 *
 * libohaudio is dlopen'd (no link/header/build-script changes); OH_Audio types are
 * declared minimally here.
 */
#include <jni.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdint>
#include <deque>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <time.h>
#include <android/log.h>

#define ATLOG(...) __android_log_print(ANDROID_LOG_INFO, "OH_ATShim", __VA_ARGS__)
#define ATERR(...) __android_log_print(ANDROID_LOG_ERROR, "OH_ATShim", __VA_ARGS__)

// ---- minimal OH_Audio NDK decls (from native_audiostreambuilder.h / native_audiorenderer.h) ----
typedef struct OH_AudioStreamBuilderStruct OH_AudioStreamBuilder;
typedef struct OH_AudioRendererStruct OH_AudioRenderer;
typedef struct OH_AudioRenderer_Callbacks_Struct {
    int32_t (*OH_AudioRenderer_OnWriteData)(OH_AudioRenderer*, void*, void*, int32_t);
    int32_t (*OH_AudioRenderer_OnStreamEvent)(OH_AudioRenderer*, void*, int32_t);
    int32_t (*OH_AudioRenderer_OnInterruptEvent)(OH_AudioRenderer*, void*, int32_t, int32_t);
    int32_t (*OH_AudioRenderer_OnError)(OH_AudioRenderer*, void*, int32_t);
} OH_AudioRenderer_Callbacks;
enum { AUDIOSTREAM_TYPE_RENDERER = 1 };
enum { AUDIOSTREAM_SAMPLE_U8 = 0, AUDIOSTREAM_SAMPLE_S16LE = 1, AUDIOSTREAM_SAMPLE_S24LE = 2, AUDIOSTREAM_SAMPLE_S32LE = 3, AUDIOSTREAM_SAMPLE_F32LE = 4 };
enum { AUDIOSTREAM_USAGE_MUSIC = 1 };
enum { AUDIOSTREAM_LATENCY_MODE_NORMAL = 0 };

typedef int32_t (*fn_Create)(OH_AudioStreamBuilder**, int32_t);
typedef int32_t (*fn_Destroy)(OH_AudioStreamBuilder*);
typedef int32_t (*fn_SetRate)(OH_AudioStreamBuilder*, int32_t);
typedef int32_t (*fn_SetChan)(OH_AudioStreamBuilder*, int32_t);
typedef int32_t (*fn_SetFmtT)(OH_AudioStreamBuilder*, int32_t);
typedef int32_t (*fn_SetUsage)(OH_AudioStreamBuilder*, int32_t);
typedef int32_t (*fn_SetLatency)(OH_AudioStreamBuilder*, int32_t);
typedef int32_t (*fn_SetCb)(OH_AudioStreamBuilder*, OH_AudioRenderer_Callbacks, void*);
typedef int32_t (*fn_Gen)(OH_AudioStreamBuilder*, OH_AudioRenderer**);
typedef int32_t (*fn_R)(OH_AudioRenderer*);
typedef int32_t (*fn_SetVol)(OH_AudioRenderer*, float);
typedef int32_t (*fn_Frames)(OH_AudioRenderer*, int64_t*);
typedef int32_t (*fn_Timestamp)(OH_AudioRenderer*, clockid_t, int64_t*, int64_t*);

static fn_Create  p_Create=nullptr;  static fn_Destroy p_Destroy=nullptr;
static fn_SetRate p_SetRate=nullptr;  static fn_SetChan p_SetChan=nullptr;
static fn_SetFmtT p_SetFmt=nullptr;   static fn_SetUsage p_SetUsage=nullptr;
static fn_SetLatency p_SetLatency=nullptr; static fn_SetCb p_SetCb=nullptr;
static fn_Gen p_Gen=nullptr;
static fn_R p_Start=nullptr, p_Stop=nullptr, p_Pause=nullptr, p_Flush=nullptr, p_Release=nullptr;
static fn_SetVol p_SetVol=nullptr;  static fn_Frames p_Frames=nullptr;
static fn_Timestamp p_Timestamp=nullptr;
static bool g_ohLoaded=false;

static bool loadOhAudio() {
    if (g_ohLoaded) return p_Gen != nullptr;
    g_ohLoaded = true;
    void* h = dlopen("libohaudio.so", RTLD_NOW|RTLD_GLOBAL);
    if (!h) h = dlopen("/system/lib/ndk/libohaudio.so", RTLD_NOW|RTLD_GLOBAL);
    if (!h) { ATERR("dlopen libohaudio failed: %s", dlerror()); return false; }
    p_Create=(fn_Create)dlsym(h,"OH_AudioStreamBuilder_Create");
    p_Destroy=(fn_Destroy)dlsym(h,"OH_AudioStreamBuilder_Destroy");
    p_SetRate=(fn_SetRate)dlsym(h,"OH_AudioStreamBuilder_SetSamplingRate");
    p_SetChan=(fn_SetChan)dlsym(h,"OH_AudioStreamBuilder_SetChannelCount");
    p_SetFmt=(fn_SetFmtT)dlsym(h,"OH_AudioStreamBuilder_SetSampleFormat");
    p_SetUsage=(fn_SetUsage)dlsym(h,"OH_AudioStreamBuilder_SetRendererInfo"); // (builder,usage) — see note
    p_SetLatency=(fn_SetLatency)dlsym(h,"OH_AudioStreamBuilder_SetLatencyMode");
    p_SetCb=(fn_SetCb)dlsym(h,"OH_AudioStreamBuilder_SetRendererCallback");
    p_Gen=(fn_Gen)dlsym(h,"OH_AudioStreamBuilder_GenerateRenderer");
    p_Start=(fn_R)dlsym(h,"OH_AudioRenderer_Start");
    p_Stop=(fn_R)dlsym(h,"OH_AudioRenderer_Stop");
    p_Pause=(fn_R)dlsym(h,"OH_AudioRenderer_Pause");
    p_Flush=(fn_R)dlsym(h,"OH_AudioRenderer_Flush");
    p_Release=(fn_R)dlsym(h,"OH_AudioRenderer_Release");
    p_SetVol=(fn_SetVol)dlsym(h,"OH_AudioRenderer_SetVolume");
    p_Frames=(fn_Frames)dlsym(h,"OH_AudioRenderer_GetFramesWritten");
    p_Timestamp=(fn_Timestamp)dlsym(h,"OH_AudioRenderer_GetTimestamp");
    ATLOG("libohaudio loaded: Create=%p Gen=%p Start=%p Timestamp=%p",
          (void*)p_Create,(void*)p_Gen,(void*)p_Start,(void*)p_Timestamp);
    return p_Create && p_Gen && p_Start;
}

// ---- per-AudioTrack shim ----
struct AudioPullSegment {
    int64_t rendererStart=0;
    int64_t realStart=0;
    int32_t realFrames=0;
    int32_t totalFrames=0;
};

struct ATShim {
    OH_AudioStreamBuilder* builder=nullptr;
    OH_AudioRenderer* renderer=nullptr;
    int sampleRate=44100, channels=2, bytesPerFrame=4;
    std::vector<uint8_t> ring;
    size_t cap=0, head=0, tail=0, count=0;   // byte ring
    std::mutex mu; std::condition_variable spaceCv;
    std::atomic<bool> started{false};
    bool stopping=false;
    int64_t framesRendered=0;
    // §514: frames actually played out of data the APP wrote. framesRendered counts every frame the
    // OH renderer pulled, including the silence we hand it on underrun, so it advances even when the
    // app has written nothing — which is NOT what AudioTrack.getPlaybackHeadPosition() means.
    int64_t framesReal=0;
    // Bytes accepted from AudioTrack.write() in the current Android playback
    // epoch.  Unlike framesReal this includes PCM still waiting in our ring.
    int64_t bytesAccepted=0;
    // OH's hardware timestamp counts every callback frame, including the
    // silence supplied on underrun.  Preserve the callback layout so that a
    // renderer-frame position can be mapped back to real app PCM exactly.
    std::deque<AudioPullSegment> pullSegments;
    int64_t ohFrameBase=0;
    bool ohFrameBaseValid=false;
    int64_t lastPresentedReal=0;
    int64_t lastTimestampNs=0;
    // §520: AudioTrack.getPlaybackHeadPosition() is documented to RESET on flush() and stop().
    // Without that, a reused or seeked track reports a position from the previous playback epoch and
    // ExoPlayer's position tracking silently reads from the wrong timeline.
    int64_t framesUnderrun=0;   // real underrun accounting — we already detect give < len
};

static jfieldID g_fNative = nullptr;
static ATShim* getShim(JNIEnv* env, jobject thiz) {
    if (!g_fNative) return nullptr;
    return reinterpret_cast<ATShim*>(env->GetLongField(thiz, g_fNative));
}

// OHOS audio thread pulls here
static int32_t onWriteData(OH_AudioRenderer*, void* user, void* buffer, int32_t len) {
    ATShim* s = reinterpret_cast<ATShim*>(user);
    uint8_t* out = reinterpret_cast<uint8_t*>(buffer);
    std::unique_lock<std::mutex> lk(s->mu);
    int32_t give = (int32_t)((s->count < (size_t)len) ? s->count : (size_t)len);
    for (int32_t i=0;i<give;i++){ out[i]=s->ring[s->head]; s->head=(s->head+1)%s->cap; }
    s->count -= give;
    if (give < len) {
        memset(out+give, 0, len-give);                  // silence on underrun
        s->framesUnderrun += (len - give) / (s->bytesPerFrame>0?s->bytesPerFrame:4);   // §520
    }
    int bpf = (s->bytesPerFrame>0?s->bytesPerFrame:4);
    const int32_t totalFrames = len / bpf;
    const int32_t realFrames = give / bpf;
    s->pullSegments.push_back(AudioPullSegment{
        s->framesRendered, s->framesReal, realFrames, totalFrames});
    s->framesRendered += len / bpf;
    s->framesReal     += give / bpf;   // §514: only real PCM counts toward playback position
    lk.unlock();
    s->spaceCv.notify_all();
    return 0; // AUDIOSTREAM_SUCCESS
}

static int pushPcm(ATShim* s, const uint8_t* data, int sizeBytes, bool blocking) {
    int written=0;
    std::unique_lock<std::mutex> lk(s->mu);
    while (written < sizeBytes) {
        while (s->count >= s->cap && !s->stopping) {
            if (!blocking) { lk.unlock(); return written; }
            s->spaceCv.wait_for(lk, std::chrono::milliseconds(200));
        }
        if (s->stopping) break;
        const int before = written;
        while (written < sizeBytes && s->count < s->cap) {
            s->ring[s->tail]=data[written++]; s->tail=(s->tail+1)%s->cap; s->count++;
        }
        s->bytesAccepted += written - before;
    }
    return written;
}

// ================= JNI natives =================
extern "C" {

static jint nSetup(JNIEnv* env, jobject thiz, jobject /*weak*/, jobject /*attrs*/,
        jintArray sampleRateArr, jint channelMask, jint /*chIdxMask*/, jint audioFormat,
        jint buffSize, jint /*mode*/, jintArray sessionId, jobject /*parcel*/,
        jlong /*nativeAT*/, jboolean /*offload*/, jint /*encaps*/, jobject /*tuner*/, jstring /*opPkg*/) {
    if (!loadOhAudio()) return -20; // ERROR
    int rate=44100;
    if (sampleRateArr) { jint v=0; env->GetIntArrayRegion(sampleRateArr,0,1,&v); if(v>0) rate=v; }
    // channelMask -> count (popcount of the position bits; fall back to 2)
    int ch = __builtin_popcount((unsigned)channelMask & 0x000FFFFF);
    if (ch<1||ch>2) ch = (channelMask!=0 && ch>2)?2:((ch==1)?1:2);
    if (ch<1) ch=2;
    // audioFormat: ENCODING_PCM_16BIT=2, 8BIT=3, FLOAT=4. MVP -> S16LE.
    int ohFmt=AUDIOSTREAM_SAMPLE_S16LE, bpf=2*ch;
    ATShim* s=new ATShim();
    s->sampleRate=rate; s->channels=ch; s->bytesPerFrame=bpf;
    s->cap = (size_t)((buffSize>0?buffSize:rate*bpf/5) * 4); if(s->cap<rate*bpf/5) s->cap=rate*bpf/5; // ~hint*4, >=200ms
    if (s->cap < 16384) s->cap=16384;
    s->ring.assign(s->cap, 0);
    if (p_Create(&s->builder, AUDIOSTREAM_TYPE_RENDERER)!=0){ ATERR("Create fail"); delete s; return -21; }
    p_SetRate(s->builder, rate); p_SetChan(s->builder, ch); p_SetFmt(s->builder, ohFmt);
    if (p_SetUsage) p_SetUsage(s->builder, AUDIOSTREAM_USAGE_MUSIC);  // RendererInfo: required for GenerateRenderer
    if (p_SetLatency) p_SetLatency(s->builder, AUDIOSTREAM_LATENCY_MODE_NORMAL);
    OH_AudioRenderer_Callbacks cbs; memset(&cbs,0,sizeof(cbs));
    cbs.OH_AudioRenderer_OnWriteData=onWriteData;
    p_SetCb(s->builder, cbs, s);
    if (p_Gen(s->builder, &s->renderer)!=0){ ATERR("GenerateRenderer fail"); p_Destroy(s->builder); delete s; return -22; }
    if (sessionId){ jint z=0; env->SetIntArrayRegion(sessionId,0,1,&z); }
    if (!g_fNative){ jclass c=env->GetObjectClass(thiz); g_fNative=env->GetFieldID(c,"mNativeTrackInJavaObj","J"); }
    env->SetLongField(thiz, g_fNative, reinterpret_cast<jlong>(s));
    ATLOG("native_setup ok rate=%d ch=%d cap=%zu", rate, ch, s->cap);
    return 0; // SUCCESS
}

static void refreshOhFrameBase(ATShim* s) {
    if (!s || !s->renderer || !p_Frames) return;
    int64_t frames = 0;
    if (p_Frames(s->renderer, &frames) == 0) {
        std::lock_guard<std::mutex> l(s->mu);
        s->ohFrameBase = frames;
        s->ohFrameBaseValid = true;
    }
}

static void nStart(JNIEnv* env, jobject thiz){
    ATShim* s=getShim(env,thiz);
    if(s&&s->renderer&&!s->started){
        // Capture the renderer's absolute frame origin before callbacks can
        // start.  Some OH implementations retain it across stop/flush.
        if (!s->ohFrameBaseValid) refreshOhFrameBase(s);
        p_Start(s->renderer); s->started=true; ATLOG("start");
    }
}
static void resetPlaybackEpochLocked(ATShim* s) {   // §520: caller holds s->mu
    s->framesReal = 0; s->framesRendered = 0;
    s->bytesAccepted = 0;
    s->pullSegments.clear();
    s->ohFrameBase = 0; s->ohFrameBaseValid = false;
    s->lastPresentedReal = 0; s->lastTimestampNs = 0;
    s->head = s->tail = s->count = 0;
}
static void nStop(JNIEnv* env, jobject thiz){
    ATShim* s=getShim(env,thiz); if(!s) return;
    if(p_Stop&&s->renderer) p_Stop(s->renderer);
    s->started=false;   // §549: same latch as nPause — a stopped renderer must be startable again.
    { std::lock_guard<std::mutex> l(s->mu); resetPlaybackEpochLocked(s); }   // §520
    refreshOhFrameBase(s);
    s->spaceCv.notify_all();
}
// §549: clearing `started` here is the whole fix for "resume from pause never works".
// `nStart` is guarded by `!s->started` (so a redundant AudioTrack.play() cannot double-start the OH
// renderer), but nothing ever cleared the flag again — it was a one-way latch. So the SECOND
// AudioTrack.play(), i.e. every resume, saw started==true and returned WITHOUT calling
// OH_AudioRenderer_Start. Silently: no exception, no log, and the OH stream just sat at
// STOPPED/PAUSED forever while the app-side state machine believed it had resumed.
// ★Nothing above this layer was at fault — traces proved SoundPlayerManager.i() -> h() ->
// SoundPlayer.k() -> MediaPlayerImpl.L() (prepare + setPlayWhenReady) all ran on every resume, and
// AudioTrack.startImpl() reaches native_start() because PlayerBase.baseStart() is already a no-op
// on this port. The break was entirely inside this function's missing state reset.
static void nPause(JNIEnv* env, jobject thiz){
    ATShim* s=getShim(env,thiz);
    if(s&&s->renderer&&p_Pause){ p_Pause(s->renderer); s->started=false; ATLOG("pause"); }
}
static void nFlush(JNIEnv* env, jobject thiz){
    ATShim* s=getShim(env,thiz); if(!s) return;
    if(p_Flush&&s->renderer) p_Flush(s->renderer);
    { std::lock_guard<std::mutex> l(s->mu); resetPlaybackEpochLocked(s); }   // §520
    refreshOhFrameBase(s);
    s->spaceCv.notify_all();
}
static void nRelease(JNIEnv* env, jobject thiz){ ATShim* s=getShim(env,thiz); if(s){ { std::lock_guard<std::mutex> lk(s->mu); s->stopping=true; } s->spaceCv.notify_all(); if(p_Release&&s->renderer)p_Release(s->renderer); if(p_Destroy&&s->builder)p_Destroy(s->builder); env->SetLongField(thiz,g_fNative,0); delete s; ATLOG("release"); } }
static void nFinalize(JNIEnv* env, jobject thiz){ nRelease(env,thiz); }

static std::atomic<long> g_writeCalls{0};
static void logWrite(jint size, jint rc, const char* via) {
    long n = ++g_writeCalls;
    if (n <= 5 || (n % 200) == 0)
        ATLOG("write #%ld via=%s size=%d rc=%d", n, via, (int)size, (int)rc);
}

static jint nWriteByte(JNIEnv* env, jobject thiz, jbyteArray data, jint off, jint size, jint /*fmt*/, jboolean blocking){
    ATShim* s=getShim(env,thiz); if(!s) return -22;
    jbyte* buf=env->GetByteArrayElements(data,nullptr); if(!buf) return -1;
    int w=pushPcm(s,(const uint8_t*)(buf+off),size,blocking);
    env->ReleaseByteArrayElements(data,buf,JNI_ABORT);
    logWrite(size, w, "byte[]");
    return w;
}
static jint nWriteShort(JNIEnv* env, jobject thiz, jshortArray data, jint off, jint size, jint /*fmt*/, jboolean blocking){
    ATShim* s=getShim(env,thiz); if(!s) return -22;
    jshort* buf=env->GetShortArrayElements(data,nullptr); if(!buf) return -1;
    int w=pushPcm(s,(const uint8_t*)(buf+off), size*2, blocking);
    env->ReleaseShortArrayElements(data,buf,JNI_ABORT);
    logWrite(size, w, "short[]");
    return w/2; // return in shorts
}
// §507 instrumentation: the write path was completely silent, so "no write markers in the log" was
// not evidence that ExoPlayer never wrote — it was evidence that we never looked. The observed
// symptom is AudioTrack being created -> started -> released in a loop with
// "volume data counts: 0" from the OH renderer, which is equally consistent with (a) ExoPlayer never
// calling write, and (b) write being called and failing. Those need different fixes, so log it.
// Rate-limited: the first few calls, then every 200th, so a working stream costs almost nothing.

static jint nWriteNative(JNIEnv* env, jobject thiz, jobject byteBuf, jint pos, jint size, jint /*fmt*/, jboolean blocking){
    ATShim* s=getShim(env,thiz);
    if(!s) { logWrite(size, -22, "bytebuf"); return -22; }
    uint8_t* base=(uint8_t*)env->GetDirectBufferAddress(byteBuf);
    if(!base) { logWrite(size, -1, "bytebuf"); return -1; }
    jint rc = pushPcm(s, base+pos, size, blocking);
    logWrite(size, rc, "bytebuf");
    return rc;
}
static jint nWriteFloat(JNIEnv* env, jobject thiz, jfloatArray data, jint off, jint size, jint /*fmt*/, jboolean blocking){
    // MVP: convert F32 -> S16LE
    ATShim* s=getShim(env,thiz); if(!s) return -22;
    jfloat* buf=env->GetFloatArrayElements(data,nullptr); if(!buf) return -1;
    std::vector<int16_t> tmp(size);
    for(int i=0;i<size;i++){ float v=buf[off+i]; if(v>1)v=1; if(v<-1)v=-1; tmp[i]=(int16_t)(v*32767.f); }
    env->ReleaseFloatArrayElements(data,buf,JNI_ABORT);
    int w=pushPcm(s,(const uint8_t*)tmp.data(), size*2, blocking);
    return w/2;
}
static std::atomic<long> g_posCalls{0};

static int64_t mapPresentedRealLocked(ATShim* s, int64_t rendererFrame) {
    if (rendererFrame <= 0) return 0;
    if (rendererFrame > s->framesRendered) rendererFrame = s->framesRendered;
    // Presentation positions are monotonic within a playback epoch.  Retire
    // callback segments once the hardware has passed them so timestamp polls
    // remain O(1) and long video playback cannot grow this deque forever.
    while (s->pullSegments.size() > 1) {
        const AudioPullSegment& front = s->pullSegments.front();
        if (rendererFrame < front.rendererStart + front.totalFrames) break;
        s->pullSegments.pop_front();
    }
    for (const AudioPullSegment& segment : s->pullSegments) {
        const int64_t end = segment.rendererStart + segment.totalFrames;
        if (rendererFrame >= end) continue;
        if (rendererFrame <= segment.rendererStart) return segment.realStart;
        int64_t within = rendererFrame - segment.rendererStart;
        if (within > segment.realFrames) within = segment.realFrames;
        return segment.realStart + within;
    }
    return s->framesReal;
}

// Query OH's CLOCK_MONOTONIC presentation point and translate its renderer
// timeline (real PCM plus underrun silence) into Android's app-PCM timeline.
static bool getPresentedPosition(ATShim* s, int64_t* framePosition, int64_t* nanoTime) {
    if (!s || !s->renderer || !p_Timestamp) return false;
    if (!s->started) {
        std::lock_guard<std::mutex> l(s->mu);
        if (s->lastTimestampNs == 0) return false;
        *framePosition = s->lastPresentedReal;
        *nanoTime = s->lastTimestampNs;
        return true;
    }
    int64_t ohFrame = 0;
    int64_t ohTime = 0;
    if (p_Timestamp(s->renderer, CLOCK_MONOTONIC, &ohFrame, &ohTime) != 0) return false;

    std::lock_guard<std::mutex> l(s->mu);
    if (!s->ohFrameBaseValid) {
        // Late fallback if GetFramesWritten was unavailable at start.  Align
        // the absolute OH position with the callback frames already supplied.
        s->ohFrameBase = ohFrame > s->framesRendered ? ohFrame - s->framesRendered : 0;
        s->ohFrameBaseValid = true;
    } else if (ohFrame < s->ohFrameBase) {
        // The backend reset its position at stop/flush even though
        // GetFramesWritten retained an absolute count.
        s->ohFrameBase = ohFrame > s->framesRendered ? ohFrame - s->framesRendered : 0;
    }
    int64_t relative = ohFrame - s->ohFrameBase;
    if (relative < 0) relative = 0;
    *framePosition = mapPresentedRealLocked(s, relative);
    *nanoTime = ohTime;
    s->lastPresentedReal = *framePosition;
    s->lastTimestampNs = *nanoTime;
    return true;
}

static jint nGetPos(JNIEnv* env, jobject thiz){
    ATShim* s=getShim(env,thiz); if(!s) return 0;
    // §514: AudioTrack.getPlaybackHeadPosition() is "frames played from what the app wrote". Using
    // OH_AudioRenderer_GetFramesWritten reported 8916 frames while the app had written ZERO bytes,
    // because RENDER_MODE_CALLBACK starts pulling the moment the stream starts and we answer with
    // silence. ExoPlayer's AudioTrackPositionTracker compares that head position against its own
    // writtenFrames; position >> written is an impossible state and it aborts the sink with a bare
    // IllegalArgumentException, before the first write ever happens. Map OH's hardware
    // presentation position through the real/silence callback history; only use drained-from-ring
    // frames on backends that expose no timestamp.
    int64_t f = 0, when = 0;
    if (!getPresentedPosition(s, &f, &when)) {
        // Honest compatibility fallback for platforms without the timestamp
        // symbol: preserve the previous drained-from-ring behavior.
        std::lock_guard<std::mutex> l(s->mu); f = s->framesReal;
    }
    // §512: distinguishes "sink is alive and polling position" from "sink bailed and calls nothing".
    // AudioTrack.write() returns ERROR_INVALID_OPERATION *before reaching native* when
    // mState != STATE_INITIALIZED, so a silent write path and a dead sink look identical from the
    // write side alone. Also report the ring occupancy, because a sink that is polling but never
    // writing leaves count=0 forever.
    long n = ++g_posCalls;
    if (n <= 3 || (n % 100) == 0) {
        size_t cnt; { std::lock_guard<std::mutex> l(s->mu); cnt = s->count; }
        ATLOG("getPos #%ld frames=%lld ringBytes=%zu started=%d", n, (long long)f, cnt, (int)s->started);
    }
    return (jint)f;
}
static void nSetVolume(JNIEnv* env, jobject thiz, jfloat l, jfloat r){ ATShim* s=getShim(env,thiz); if(s&&p_SetVol&&s->renderer)p_SetVol(s->renderer,(l+r)/2.f); }
static jint nMinBuf(JNIEnv*, jclass, jint rate, jint chCfg, jint fmt){ int ch=__builtin_popcount((unsigned)chCfg&0xFFFFF); if(ch<1)ch=2; int bpf=2*ch; int b=rate*bpf/10; if(b<4096)b=4096; return b; }

// §505 (2026-08-04): AudioTrack.<init> calls native_setPlayerIId to hand AudioService the player id
// allocated by PlayerBase, purely for that service's own bookkeeping. We have no AudioService, so a
// no-op is the correct behaviour — but leaving it UNBOUND is not. Unbound, it raised
//   UnsatisfiedLinkError: No implementation found for
//     void android.media.AudioTrack.native_setPlayerIId(int)
//       at android.media.AudioTrack.<init>(AudioTrack.java:908)
//       at android.media.AudioTrack$Builder.build(AudioTrack.java:1450)
// on ExoPlayer's audio HandlerThread. That is an Error, so it is not caught by ExoPlayer and, with
// ThreadGroup.uncaughtException a no-op in this runtime, it killed the thread with no report — the
// exact same failure shape as §504's libcore.io.Memory.pokeByteArray. The visible symptom was the
// codec feeding a few buffers (queueInput=17, cbOutput=4) and then freezing forever, because the
// sink that was supposed to drain it no longer had a thread.
static void nSetPlayerIId(JNIEnv*, jobject, jint) { }

// §506: the same trap, once per round, all over DefaultAudioSink's setup path. ExoPlayer registers a
// routing listener right after building the track (addOnRoutingChangedListener -> AudioRouting ->
// native_enableDeviceCallback), and that one was unbound too, killing the audio thread exactly like
// §505 did — the feed froze at the identical queueInput=16/cbOutput=4.
//
// So rather than discover these one 15-minute round at a time, bind the whole cluster whose
// semantics are unambiguous on this platform. Every method below reports or subscribes to state
// owned by AudioService / AudioPolicy — device routing, port ids, output flags, underruns, log
// session ids. We have none of those, so "nothing to report" IS the correct answer, and returning it
// is strictly better than leaving an Error-throwing stub.
//
// Deliberately not guessed here: native_get_buffer_size_frames,
// native_set_buffer_size_frames, native_applyVolumeShaper and friends. Timestamp and latency are
// bound below because OH exposes the real presentation clock needed to implement them.
static void nEnableDeviceCb(JNIEnv*, jobject) { }
static void nDisableDeviceCb(JNIEnv*, jobject) { }
static jint nRoutedDeviceId(JNIEnv*, jobject) { return 0; }   // 0 = unknown/default device
static jint nPortId(JNIEnv*, jobject) { return 0; }
static jint nGetFlags(JNIEnv*, jobject) { return 0; }         // no AUDIO_OUTPUT_FLAG_* apply
// §520: we DETECT underruns (give < len), so reporting 0 is a fabrication, not an honest
// "unavailable". Callers use this diagnostically and to adapt buffering.
static jint nUnderrunCount(JNIEnv* env, jobject thiz) {
    ATShim* s = getShim(env, thiz); if (!s) return 0;
    std::lock_guard<std::mutex> l(s->mu);
    return (jint)s->framesUnderrun;
}
static jint nGetLatency(JNIEnv* env, jobject thiz) {
    ATShim* s = getShim(env, thiz); if (!s || s->sampleRate <= 0) return 0;
    int64_t presented = 0, when = 0;
    const bool haveTimestamp = getPresentedPosition(s, &presented, &when);
    std::lock_guard<std::mutex> l(s->mu);
    const int bpf = s->bytesPerFrame > 0 ? s->bytesPerFrame : 4;
    const int64_t accepted = s->bytesAccepted / bpf;
    if (!haveTimestamp) presented = s->framesReal;
    int64_t pending = accepted - presented;
    if (pending < 0) pending = 0;
    const int64_t millis = pending * 1000 / s->sampleRate;
    return static_cast<jint>(millis > INT32_MAX ? INT32_MAX : millis);
}
static void nSetLogSessionId(JNIEnv*, jobject, jstring) { }

// §506b/§522: taken from a static scan of the app dex (tools/FindClassRefs.java on
// Landroid/media/AudioTrack;) rather than another round of discover-one-crash-at-a-time. The scan is
// also why getBufferSizeInFrames is absent: the app never references it, so binding it would be
// speculation.
// ★attachAuxEffect / setAuxEffectSendLevel / setOutputDevice / set_delay_padding /
// is_direct_output_supported were previously DEFINED here but never registered — dead code that
// still raises UnsatisfiedLinkError if the app ever reaches it, i.e. the worst of both. Removed.
// If evidence (JNIMISS) names one, add the implementation and the table row together.
//
// Each unsupported operation returns the honest "not available / not supported" answer rather
// than a plausible lie, because ExoPlayer degrades gracefully on them:
//   direct output  -> false keeps ExoPlayer on the decode path that already works, instead of
//                     attempting MP3 passthrough this renderer cannot do.
//   preferred device -> false; we have no routing to honour and saying otherwise would be a lie.
// AudioTimestampPoller calls this with no catch.  Return OH's real monotonic
// presentation timestamp when available; ERROR_INVALID_OPERATION remains the
// correct transient answer before start or if the backend cannot timestamp.
static jint nGetTimestamp(JNIEnv* env, jobject thiz, jlongArray values) {
    ATShim* s = getShim(env, thiz);
    if (!s || !values || env->GetArrayLength(values) < 2) return -3;
    int64_t frame = 0, time = 0;
    if (!getPresentedPosition(s, &frame, &time)) return -3;
    jlong pair[2] = { static_cast<jlong>(frame), static_cast<jlong>(time) };
    env->SetLongArrayRegion(values, 0, 2, pair);
    return 0;
}

int register_AudioTrack_shim(JNIEnv* env) {
    if (!loadOhAudio()) { ATERR("OH audio unavailable; AudioTrack shim NOT registered"); return -1; }
    jclass c = env->FindClass("android/media/AudioTrack");
    if (!c) { if(env->ExceptionCheck())env->ExceptionClear(); ATERR("AudioTrack class not found"); return -1; }
    g_fNative = env->GetFieldID(c, "mNativeTrackInJavaObj", "J");
    if (!g_fNative){ if(env->ExceptionCheck())env->ExceptionClear(); ATERR("mNativeTrackInJavaObj field not found"); return -1; }
    // §518: required vs optional is an explicit per-entry flag, not a positional boundary.
    // It used to be `kFirstOptional = 14`, which silently mis-classifies every entry below it the
    // moment someone inserts a row — exactly the kind of latent trap this shim exists to remove.
    struct ATMethod { JNINativeMethod m; bool required; };
    const ATMethod methods[] = {
        { {"native_setup", "(Ljava/lang/Object;Ljava/lang/Object;[IIIIII[ILandroid/os/Parcel;JZILjava/lang/Object;Ljava/lang/String;)I", (void*)nSetup}, true },
        { {"native_start", "()V", (void*)nStart}, true },
        { {"native_stop", "()V", (void*)nStop}, true },
        { {"native_pause", "()V", (void*)nPause}, true },
        { {"native_flush", "()V", (void*)nFlush}, true },
        { {"native_release", "()V", (void*)nRelease}, true },
        { {"native_finalize", "()V", (void*)nFinalize}, true },
        { {"native_write_byte", "([BIIIZ)I", (void*)nWriteByte}, true },
        { {"native_write_short", "([SIIIZ)I", (void*)nWriteShort}, true },
        { {"native_write_float", "([FIIIZ)I", (void*)nWriteFloat}, true },
        { {"native_write_native_bytes", "(Ljava/nio/ByteBuffer;IIIZ)I", (void*)nWriteNative}, true },
        { {"native_get_position", "()I", (void*)nGetPos}, true },
        { {"native_setVolume", "(FF)V", (void*)nSetVolume}, true },
        { {"native_get_min_buff_size", "(III)I", (void*)nMinBuf}, true },
        // §505/§506: no-ops, but they MUST be bound — see nSetPlayerIId above. Per-method
        // registration below means any that this framework.jar does not declare are skipped
        // harmlessly rather than taking the whole table down with them.
        { {"native_setPlayerIId", "(I)V", (void*)nSetPlayerIId}, false },
        { {"native_enableDeviceCallback", "()V", (void*)nEnableDeviceCb}, false },
        { {"native_disableDeviceCallback", "()V", (void*)nDisableDeviceCb}, false },
        { {"native_getRoutedDeviceId", "()I", (void*)nRoutedDeviceId}, false },
        { {"native_getPortId", "()I", (void*)nPortId}, false },
        { {"native_get_flags", "()I", (void*)nGetFlags}, false },
        { {"native_get_underrun_count", "()I", (void*)nUnderrunCount}, false },
        { {"native_get_latency", "()I", (void*)nGetLatency}, false },
        { {"native_setLogSessionId", "(Ljava/lang/String;)V", (void*)nSetLogSessionId}, false },
        { {"native_get_timestamp", "([J)I", (void*)nGetTimestamp}, false },
        // ⛔The five other natives the dex scan turned up — attachAuxEffect,
        // setAuxEffectSendLevel, setOutputDevice, set_delay_padding and
        // is_direct_output_supported — are NOT bound, and binding them is not a free win.
        // Registering all six at once hung the child at startup: it stayed alive but never rendered
        // a frame (swaps=0 for 5 minutes, versus a 44 s boot on the build without them). Only
        // get_timestamp was ever named by JNIMISS; the rest were me predicting ahead of the
        // evidence. Bisecting cost a full board cycle, so: bind what the runtime has actually
        // asked for, one at a time, and let JNIMISS name the next one.
    };
    const int n = (int)(sizeof(methods)/sizeof(methods[0]));
    int ok = 0, reqFail = 0;
    for (int i = 0; i < n; i++) {
        JNINativeMethod one = methods[i].m;
        if (env->RegisterNatives(c, &one, 1) == 0) { ok++; continue; }
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (methods[i].required) { reqFail++; ATERR("reg fail (required): %s", one.name); }
        else                     { ATLOG("not declared, skipped: %s", one.name); }
    }
    ATLOG("AudioTrack shim registered %d/%d (required failures=%d)", ok, n, reqFail);
    return reqFail == 0 ? 0 : -1;
}

} // extern "C"
