/*
 * Android TextureLayer JNI backed by an OH_NativeImage consumer.
 *
 * Preserve Android's TextureLayer, SurfaceTexture, DeferredLayerUpdater, and
 * RenderThread lifecycle. The only translated operation is the platform
 * buffer boundary: acquire the latest OH_NativeImage buffer, make an owned
 * Skia raster copy, release the OH buffer, and give the image to the stock
 * DeferredLayerUpdater layer.
 *
 * The preserved board libhwui was built with ASurfaceTexture_dequeueBuffer()
 * forced to return nullptr and its TextureLayer registration disabled. Its
 * internal calls to DeferredLayerUpdater::apply() are direct, not PLT calls,
 * so ordinary symbol interposition cannot restore the consumer. A guarded
 * per-process AArch64 entry hook wraps that one method while leaving the
 * libhwui file and the rest of HWUI untouched. The trampoline always runs the
 * original method first; the OH boundary step is additive and only runs for a
 * DeferredLayerUpdater associated with our OH SurfaceTexture state.
 */

#include <jni.h>
#include <native_buffer/native_buffer.h>
#include <native_image/native_image.h>

#include <DeferredLayerUpdater.h>
#include <Layer.h>
#include <SkImage.h>
#include <SkImageInfo.h>
#include <SkPixmap.h>
#include <SkRect.h>
#include <SkiaCanvas.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>

extern "C" void* oh_surface_texture_get_consumer_handle(JNIEnv*, jobject);
extern "C" OH_NativeImage* oh_surface_texture_get_native_image(void*);

namespace android {
namespace {

using uirenderer::DeferredLayerUpdater;

using ApplyFn = void (*)(DeferredLayerUpdater*);
using SetPaintFn = void (*)(DeferredLayerUpdater*, const SkPaint*);
using UpdateLayerFn = void (*)(DeferredLayerUpdater*, bool, const sk_sp<SkImage>&,
                               uint32_t, SkRect, float);
using DrawLayerFn = bool (*)(GrRecordingContext*, SkCanvas*, uirenderer::Layer*,
                             const SkRect*, const SkRect*, bool);

std::mutex gLayerBindingsMutex;
std::mutex gHookMutex;
std::unordered_map<DeferredLayerUpdater*, void*> gLayerBindings;
void* gHwuiHandle = nullptr;
ApplyFn gOriginalApply = nullptr;
SetPaintFn gSetPaint = nullptr;
UpdateLayerFn gUpdateLayer = nullptr;
DrawLayerFn gDrawLayer = nullptr;
void* gRecordingDrawIsland = nullptr;
std::atomic<uint32_t> gUpdateCount{0};
std::atomic<uint32_t> gPrepareCount{0};
std::atomic<uint32_t> gRecordedLayerCount{0};
std::atomic<uint32_t> gComposedCount{0};
std::atomic<uint32_t> gAcquireErrorCount{0};

void* FindConsumerHandle(DeferredLayerUpdater* layer)
{
    std::lock_guard<std::mutex> lock(gLayerBindingsMutex);
    const auto found = gLayerBindings.find(layer);
    return found != gLayerBindings.end() ? found->second : nullptr;
}

void BindConsumerHandle(DeferredLayerUpdater* layer, void* consumerHandle)
{
    std::lock_guard<std::mutex> lock(gLayerBindingsMutex);
    if (consumerHandle != nullptr) {
        gLayerBindings[layer] = consumerHandle;
    } else {
        gLayerBindings.erase(layer);
    }
}

sk_sp<SkImage> AcquireLatestImage(void* consumerHandle, int* outWidth, int* outHeight)
{
    OH_NativeImage* nativeImage = oh_surface_texture_get_native_image(consumerHandle);
    if (nativeImage == nullptr) {
        return nullptr;
    }

    OHNativeWindowBuffer* windowBuffer = nullptr;
    int fenceFd = -1;
    const int32_t acquireResult = OH_NativeImage_AcquireLatestNativeWindowBuffer(
        nativeImage, &windowBuffer, &fenceFd);
    if (acquireResult != 0 || windowBuffer == nullptr) {
        if (fenceFd >= 0) {
            close(fenceFd);
        }
        // 40601000 is the normal no-buffer result when apply() was scheduled
        // for a property change rather than a frame.
        if (acquireResult != 40601000) {
            const uint32_t count = gAcquireErrorCount.fetch_add(
                1, std::memory_order_relaxed) + 1;
            if (count <= 8 || (count % 120) == 0) {
                fprintf(stderr,
                        "[WESTLAKE-TEXTURELAYER-774] acquire failed image=%p ret=%d count=%u\n",
                        nativeImage, acquireResult, count);
                fflush(stderr);
            }
        }
        return nullptr;
    }

    OH_NativeBuffer* nativeBuffer = nullptr;
    int32_t result = OH_NativeBuffer_FromNativeWindowBuffer(windowBuffer, &nativeBuffer);
    if (result != 0 || nativeBuffer == nullptr) {
        if (fenceFd >= 0) {
            close(fenceFd);
        }
        (void)OH_NativeImage_ReleaseNativeWindowBuffer(nativeImage, windowBuffer, -1);
        fprintf(stderr,
                "[WESTLAKE-TEXTURELAYER-774] native-buffer conversion failed ret=%d\n",
                result);
        fflush(stderr);
        return nullptr;
    }

    OH_NativeBuffer_Config config{};
    OH_NativeBuffer_GetConfig(nativeBuffer, &config);
    void* pixels = nullptr;
    if (fenceFd >= 0) {
        result = OH_NativeBuffer_MapWaitFence(nativeBuffer, fenceFd, &pixels);
        if (result == 0) {
            // MapWaitFence transfers ownership of the acquire fence.
            fenceFd = -1;
        } else {
            close(fenceFd);
            fenceFd = -1;
        }
    } else {
        result = OH_NativeBuffer_Map(nativeBuffer, &pixels);
    }

    sk_sp<SkImage> image;
    uint32_t composedCount = 0;
    uint64_t sampleHash = 0;
    const uint32_t sequence = OH_NativeBuffer_GetSeqNum(nativeBuffer);
    const int64_t timestamp = OH_NativeImage_GetTimestamp(nativeImage);
    if (result == 0 && pixels != nullptr && config.width > 0 && config.height > 0) {
        SkColorType colorType = kUnknown_SkColorType;
        SkAlphaType alphaType = kOpaque_SkAlphaType;
        size_t bytesPerPixel = 0;
        switch (config.format) {
            case 3:  // GRAPHIC_PIXEL_FMT_RGB_565
                colorType = kRGB_565_SkColorType;
                bytesPerPixel = 2;
                break;
            case 11:  // GRAPHIC_PIXEL_FMT_RGBX_8888
                colorType = kRGB_888x_SkColorType;
                bytesPerPixel = 4;
                break;
            case 12:  // GRAPHIC_PIXEL_FMT_RGBA_8888
                colorType = kRGBA_8888_SkColorType;
                alphaType = kPremul_SkAlphaType;
                bytesPerPixel = 4;
                break;
            default:
                break;
        }

        if (colorType != kUnknown_SkColorType) {
            // OH SurfaceBuffer::GetStride() is pixel stride on this board even
            // though the public config comment calls it bytes. Accept either
            // representation without app- or producer-specific assumptions.
            size_t rowBytes = static_cast<size_t>(config.stride);
            const size_t tightRowBytes = static_cast<size_t>(config.width) * bytesPerPixel;
            if (rowBytes < tightRowBytes && rowBytes >= static_cast<size_t>(config.width)) {
                rowBytes *= bytesPerPixel;
            }
            if (rowBytes >= tightRowBytes) {
                const SkImageInfo info = SkImageInfo::Make(
                    config.width, config.height, colorType, alphaType);
                const SkPixmap pixmap(info, pixels, rowBytes);
                image = SkImages::RasterFromPixmapCopy(pixmap);
                if (image != nullptr) {
                    composedCount = gComposedCount.fetch_add(
                        1, std::memory_order_relaxed) + 1;
                    if (composedCount <= 8 || (composedCount % 120) == 0) {
                        // Sample a regular 17x17 grid while the producer buffer is
                        // mapped. This distinguishes a genuinely advancing video
                        // stream from a producer repeatedly posting one poster frame
                        // without retaining or copying any extra frame data.
                        sampleHash = 1469598103934665603ULL;
                        const int yStep = std::max(1, config.height / 16);
                        const int xStep = std::max(1, config.width / 16);
                        const auto* bytes = static_cast<const uint8_t*>(pixels);
                        for (int y = 0; y < config.height; y += yStep) {
                            for (int x = 0; x < config.width; x += xStep) {
                                const size_t offset = static_cast<size_t>(y) * rowBytes +
                                    static_cast<size_t>(x) * bytesPerPixel;
                                for (size_t byte = 0; byte < bytesPerPixel; ++byte) {
                                    sampleHash ^= bytes[offset + byte];
                                    sampleHash *= 1099511628211ULL;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (result == 0) {
        (void)OH_NativeBuffer_Unmap(nativeBuffer);
    }
    const int32_t releaseResult = OH_NativeImage_ReleaseNativeWindowBuffer(
        nativeImage, windowBuffer, -1);

    if (image != nullptr) {
        if (outWidth != nullptr) {
            *outWidth = config.width;
        }
        if (outHeight != nullptr) {
            *outHeight = config.height;
        }
        if (composedCount <= 8 || (composedCount % 120) == 0 || releaseResult != 0) {
            fprintf(stderr,
                    "[WESTLAKE-TEXTURELAYER-774] copied frame=%u image=%p seq=%u "
                    "timestamp=%lld hash=%016llx size=%dx%d stride=%d format=%d release=%d\n",
                    composedCount, nativeImage, sequence,
                    static_cast<long long>(timestamp),
                    static_cast<unsigned long long>(sampleHash),
                    config.width, config.height, config.stride, config.format,
                    releaseResult);
            fflush(stderr);
        }
    } else {
        const uint32_t count = gAcquireErrorCount.fetch_add(
            1, std::memory_order_relaxed) + 1;
        if (count <= 8 || (count % 120) == 0) {
            fprintf(stderr,
                    "[WESTLAKE-TEXTURELAYER-774] copy failed image=%p map=%d pixels=%p "
                    "size=%dx%d stride=%d format=%d release=%d count=%u\n",
                    nativeImage, result, pixels, config.width, config.height,
                    config.stride, config.format, releaseResult, count);
            fflush(stderr);
        }
    }
    return image;
}

void TextureLayerApplyHook(DeferredLayerUpdater* layer)
{
    if (gOriginalApply != nullptr) {
        gOriginalApply(layer);
    }

    void* consumerHandle = FindConsumerHandle(layer);
    if (consumerHandle == nullptr || gUpdateLayer == nullptr) {
        return;
    }

    int width = 0;
    int height = 0;
    sk_sp<SkImage> image = AcquireLatestImage(consumerHandle, &width, &height);
    if (image == nullptr) {
        return;
    }

    const bool forceFilter = layer->getWidth() != width || layer->getHeight() != height;
    const SkRect crop = SkRect::MakeWH(static_cast<SkScalar>(width),
                                       static_cast<SkScalar>(height));
    gUpdateLayer(layer, forceFilter, image, 0, crop, -1.0f);
}

/*
 * The preserved OH libhwui was compiled with ANDROID rather than __ANDROID__.
 * Upstream guards SkiaRecordingCanvas::drawLayer() with __ANDROID__, so that
 * one four-byte method is a bare `ret` in the measured board binary. Restore
 * the stock body at the existing RecordingCanvas method boundary: record a
 * ref-counted drawable in the Skia display list. The guarded branch-island
 * repair is process-local and leaves the preserved libhwui file untouched.
 */
class OhLayerDrawable final : public SkDrawable {
public:
    explicit OhLayerDrawable(DeferredLayerUpdater* layer) : mLayerUpdater(layer) {}

protected:
    SkRect onGetBounds() override
    {
        return SkRect::MakeWH(mLayerUpdater->getWidth(), mLayerUpdater->getHeight());
    }

    void onDraw(SkCanvas* canvas) override
    {
        uirenderer::Layer* layer = mLayerUpdater->backingLayer();
        if (canvas == nullptr || layer == nullptr || gDrawLayer == nullptr) {
            return;
        }
        const SkRect source = layer->getCurrentCropRect();
        (void)gDrawLayer(canvas->recordingContext(), canvas, layer,
                         &source, nullptr, true);
    }

private:
    sp<DeferredLayerUpdater> mLayerUpdater;
};

class SkiaCanvasBoundaryAccess : public SkiaCanvas {
public:
    void drawTextureLayerBoundary(DeferredLayerUpdater* layer)
    {
        if (layer == nullptr) {
            return;
        }
        sk_sp<SkDrawable> drawable(new OhLayerDrawable(layer));
        drawDrawable(drawable.get());
    }
};

void SkiaRecordingCanvasDrawLayerHook(SkiaCanvasBoundaryAccess* canvas,
                                      DeferredLayerUpdater* layer)
{
    if (canvas == nullptr || layer == nullptr) {
        return;
    }
    canvas->drawTextureLayerBoundary(layer);
    const uint32_t count = gRecordedLayerCount.fetch_add(
        1, std::memory_order_relaxed) + 1;
    if (count <= 8 || (count % 120) == 0) {
        fprintf(stderr,
                "[WESTLAKE-TEXTURELAYER-776] recorded layer=%p canvas=%p count=%u\n",
                layer, canvas, count);
        fflush(stderr);
    }
}

void* AllocateRecordingDrawIsland(void* nearAddress, size_t pageSize)
{
    const uintptr_t near = reinterpret_cast<uintptr_t>(nearAddress);
    constexpr uintptr_t kStep = 2U * 1024U * 1024U;
    constexpr uintptr_t kLimit = 120U * 1024U * 1024U;
    for (uintptr_t distance = kStep; distance <= kLimit; distance += kStep) {
        for (int direction = 0; direction < 2; ++direction) {
            uintptr_t hint = direction == 0 ? near + distance : near - distance;
            hint &= ~(static_cast<uintptr_t>(pageSize) - 1U);
            void* island = mmap(reinterpret_cast<void*>(hint), pageSize,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (island == MAP_FAILED) {
                continue;
            }
            const intptr_t branchDistance =
                reinterpret_cast<intptr_t>(island) - static_cast<intptr_t>(near);
            if ((branchDistance & 3) == 0 &&
                branchDistance >= -(static_cast<intptr_t>(1) << 27) &&
                branchDistance < (static_cast<intptr_t>(1) << 27)) {
                return island;
            }
            munmap(island, pageSize);
        }
    }
    return nullptr;
}

bool InstallRecordingDrawHook(void* drawAddress)
{
#if !defined(__aarch64__)
    (void)drawAddress;
    return false;
#else
    if (gRecordingDrawIsland != nullptr) {
        return true;
    }

    constexpr uint32_t kExpectedRet = 0xd65f03c0U;
    if (drawAddress == nullptr ||
        *static_cast<const uint32_t*>(drawAddress) != kExpectedRet) {
        fprintf(stderr,
                "[WESTLAKE-TEXTURELAYER-776] recording prologue mismatch at=%p got=%08x\n",
                drawAddress,
                drawAddress != nullptr ? *static_cast<const uint32_t*>(drawAddress) : 0U);
        fflush(stderr);
        return false;
    }

    const long pageSizeLong = sysconf(_SC_PAGESIZE);
    if (pageSizeLong <= 0) {
        return false;
    }
    const size_t pageSize = static_cast<size_t>(pageSizeLong);
    void* island = AllocateRecordingDrawIsland(drawAddress, pageSize);
    if (island == nullptr) {
        fprintf(stderr,
                "[WESTLAKE-TEXTURELAYER-776] no branch island near=%p\n",
                drawAddress);
        fflush(stderr);
        return false;
    }

    auto* islandWords = static_cast<uint32_t*>(island);
    islandWords[0] = 0x58000050U;  // ldr x16, #8
    islandWords[1] = 0xd61f0200U;  // br x16
    *reinterpret_cast<uint64_t*>(islandWords + 2) =
        reinterpret_cast<uint64_t>(&SkiaRecordingCanvasDrawLayerHook);
    __builtin___clear_cache(static_cast<char*>(island),
                            static_cast<char*>(island) + 16);
    if (mprotect(island, pageSize, PROT_READ | PROT_EXEC) != 0) {
        munmap(island, pageSize);
        return false;
    }

    const intptr_t branchDistance = reinterpret_cast<intptr_t>(island) -
                                    reinterpret_cast<intptr_t>(drawAddress);
    const uint32_t branch = 0x14000000U |
        (static_cast<uint32_t>(branchDistance >> 2) & 0x03ffffffU);
    const uintptr_t draw = reinterpret_cast<uintptr_t>(drawAddress);
    const uintptr_t pageStart =
        draw & ~(static_cast<uintptr_t>(pageSize) - 1U);
    if (mprotect(reinterpret_cast<void*>(pageStart), pageSize,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        munmap(island, pageSize);
        return false;
    }
    *static_cast<uint32_t*>(drawAddress) = branch;
    __builtin___clear_cache(static_cast<char*>(drawAddress),
                            static_cast<char*>(drawAddress) + sizeof(branch));
    (void)mprotect(reinterpret_cast<void*>(pageStart), pageSize,
                   PROT_READ | PROT_EXEC);
    gRecordingDrawIsland = island;
    fprintf(stderr,
            "[WESTLAKE-TEXTURELAYER-776] recording hook installed draw=%p "
            "island=%p target=%p branch=%08x\n",
            drawAddress, island,
            reinterpret_cast<void*>(&SkiaRecordingCanvasDrawLayerHook), branch);
    fflush(stderr);
    return true;
#endif
}

bool InstallApplyHook()
{
#if !defined(__aarch64__)
    fprintf(stderr, "[WESTLAKE-TEXTURELAYER-774] hook skipped: non-aarch64 build\n");
    fflush(stderr);
    return false;
#else
    constexpr const char* kApplySymbol =
        "_ZN7android10uirenderer20DeferredLayerUpdater5applyEv";
    constexpr const char* kSetPaintSymbol =
        "_ZN7android10uirenderer20DeferredLayerUpdater8setPaintEPK7SkPaint";
    constexpr const char* kUpdateLayerSymbol =
        "_ZN7android10uirenderer20DeferredLayerUpdater11updateLayerEbRK5sk_spI7SkImageEj6SkRectf";
    constexpr const char* kDrawLayerSymbol =
        "_ZN7android10uirenderer12skiapipeline13LayerDrawable9DrawLayerEP18GrRecordingContextP8SkCanvasPNS0_5LayerEPK6SkRectSB_b";
    constexpr const char* kRecordingDrawLayerSymbol =
        "_ZN7android10uirenderer12skiapipeline19SkiaRecordingCanvas9drawLayerEPNS0_20DeferredLayerUpdaterE";

    std::lock_guard<std::mutex> hookLock(gHookMutex);
    if (gOriginalApply != nullptr) {
        return true;
    }

    if (gHwuiHandle == nullptr) {
        gHwuiHandle = dlopen("libhwui.so", RTLD_NOW | RTLD_NOLOAD);
    }
    void* symbolScope = gHwuiHandle != nullptr ? gHwuiHandle : RTLD_DEFAULT;
    void* applyAddress = dlsym(symbolScope, kApplySymbol);
    gSetPaint = reinterpret_cast<SetPaintFn>(dlsym(symbolScope, kSetPaintSymbol));
    gUpdateLayer = reinterpret_cast<UpdateLayerFn>(dlsym(symbolScope, kUpdateLayerSymbol));
    gDrawLayer = reinterpret_cast<DrawLayerFn>(dlsym(symbolScope, kDrawLayerSymbol));
    void* recordingDrawAddress = dlsym(symbolScope, kRecordingDrawLayerSymbol);
    if (applyAddress == nullptr || gSetPaint == nullptr || gUpdateLayer == nullptr ||
        gDrawLayer == nullptr || recordingDrawAddress == nullptr) {
        fprintf(stderr,
                "[WESTLAKE-TEXTURELAYER-774] symbols missing apply=%p paint=%p "
                "update=%p draw=%p record=%p err=%s\n",
                applyAddress, reinterpret_cast<void*>(gSetPaint),
                reinterpret_cast<void*>(gUpdateLayer),
                reinterpret_cast<void*>(gDrawLayer), recordingDrawAddress, dlerror());
        fflush(stderr);
        return false;
    }
    if (!InstallRecordingDrawHook(recordingDrawAddress)) {
        return false;
    }

    // Refuse to patch any libhwui other than the measured preserved build. The
    // first four instructions are its relocation-free stack-frame prologue.
    constexpr uint32_t kExpected[4] = {
        0xd100c3ffU, 0xa9017bfdU, 0xa9024ff4U, 0x910043fdU,
    };
    if (memcmp(applyAddress, kExpected, sizeof(kExpected)) != 0) {
        const auto* words = static_cast<const uint32_t*>(applyAddress);
        fprintf(stderr,
                "[WESTLAKE-TEXTURELAYER-774] prologue mismatch at=%p got=%08x/%08x/%08x/%08x\n",
                applyAddress, words[0], words[1], words[2], words[3]);
        fflush(stderr);
        return false;
    }

    const long pageSizeLong = sysconf(_SC_PAGESIZE);
    if (pageSizeLong <= 0) {
        return false;
    }
    const size_t pageSize = static_cast<size_t>(pageSizeLong);
    void* trampoline = mmap(nullptr, pageSize, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED) {
        fprintf(stderr, "[WESTLAKE-TEXTURELAYER-774] trampoline mmap failed errno=%d\n", errno);
        fflush(stderr);
        return false;
    }

    memcpy(trampoline, applyAddress, sizeof(kExpected));
    auto* trampolineWords = static_cast<uint32_t*>(trampoline);
    trampolineWords[4] = 0x58000050U;  // ldr x16, #8
    trampolineWords[5] = 0xd61f0200U;  // br x16
    *reinterpret_cast<uint64_t*>(trampolineWords + 6) =
        reinterpret_cast<uint64_t>(applyAddress) + sizeof(kExpected);
    __builtin___clear_cache(static_cast<char*>(trampoline),
                            static_cast<char*>(trampoline) + 32);
    if (mprotect(trampoline, pageSize, PROT_READ | PROT_EXEC) != 0) {
        fprintf(stderr, "[WESTLAKE-TEXTURELAYER-774] trampoline mprotect failed errno=%d\n", errno);
        fflush(stderr);
        munmap(trampoline, pageSize);
        return false;
    }
    // Publish the callable original before changing the entry point so even a
    // concurrent RenderThread can never observe the hook without its trampoline.
    gOriginalApply = reinterpret_cast<ApplyFn>(trampoline);

    const uintptr_t apply = reinterpret_cast<uintptr_t>(applyAddress);
    const uintptr_t pageStart = apply & ~(static_cast<uintptr_t>(pageSize) - 1U);
    const uintptr_t pageEnd = (apply + 16U + pageSize - 1U) &
                              ~(static_cast<uintptr_t>(pageSize) - 1U);
    const size_t pageLength = pageEnd - pageStart;
    if (mprotect(reinterpret_cast<void*>(pageStart), pageLength,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        fprintf(stderr, "[WESTLAKE-TEXTURELAYER-774] target mprotect failed errno=%d\n", errno);
        fflush(stderr);
        gOriginalApply = nullptr;
        munmap(trampoline, pageSize);
        return false;
    }

    uint32_t patch[4] = {
        0x58000050U,  // ldr x16, #8
        0xd61f0200U,  // br x16
        0,
        0,
    };
    *reinterpret_cast<uint64_t*>(patch + 2) =
        reinterpret_cast<uint64_t>(&TextureLayerApplyHook);
    memcpy(applyAddress, patch, sizeof(patch));
    __builtin___clear_cache(static_cast<char*>(applyAddress),
                            static_cast<char*>(applyAddress) + sizeof(patch));
    (void)mprotect(reinterpret_cast<void*>(pageStart), pageLength, PROT_READ | PROT_EXEC);

    fprintf(stderr,
            "[WESTLAKE-TEXTURELAYER-774] hook installed apply=%p trampoline=%p "
            "paint=%p update=%p draw=%p\n",
            applyAddress, trampoline, reinterpret_cast<void*>(gSetPaint),
            reinterpret_cast<void*>(gUpdateLayer), reinterpret_cast<void*>(gDrawLayer));
    fflush(stderr);
    return true;
#endif
}

jboolean TextureLayer_nPrepare(JNIEnv*, jclass, jlong layerUpdater,
                               jint width, jint height, jboolean opaque)
{
    (void)InstallApplyHook();
    auto* layer = reinterpret_cast<DeferredLayerUpdater*>(layerUpdater);
    if (layer == nullptr) {
        return JNI_FALSE;
    }
    bool changed = false;
    changed |= layer->setSize(width, height);
    changed |= layer->setBlend(opaque != JNI_TRUE);
    const uint32_t count = gPrepareCount.fetch_add(
        1, std::memory_order_relaxed) + 1;
    if (changed || count <= 8 || (count % 120) == 0) {
        fprintf(stderr,
                "[WESTLAKE-TEXTURELAYER-774] prepare layer=%p size=%dx%d opaque=%d "
                "changed=%d count=%u\n",
                layer, width, height, opaque == JNI_TRUE ? 1 : 0,
                changed ? 1 : 0, count);
        fflush(stderr);
    }
    return changed ? JNI_TRUE : JNI_FALSE;
}

void TextureLayer_nSetLayerPaint(JNIEnv*, jclass, jlong layerUpdater, jlong paint)
{
    (void)InstallApplyHook();
    auto* layer = reinterpret_cast<DeferredLayerUpdater*>(layerUpdater);
    if (layer != nullptr && gSetPaint != nullptr) {
        gSetPaint(layer, reinterpret_cast<const SkPaint*>(paint));
    }
}

void TextureLayer_nSetTransform(JNIEnv*, jclass, jlong layerUpdater, jlong matrix)
{
    (void)InstallApplyHook();
    auto* layer = reinterpret_cast<DeferredLayerUpdater*>(layerUpdater);
    if (layer != nullptr) {
        layer->setTransform(reinterpret_cast<const SkMatrix*>(matrix));
    }
}

void TextureLayer_nSetSurfaceTexture(JNIEnv* env, jclass, jlong layerUpdater,
                                     jobject surfaceTexture)
{
    (void)InstallApplyHook();
    auto* layer = reinterpret_cast<DeferredLayerUpdater*>(layerUpdater);
    if (layer == nullptr) {
        return;
    }
    void* consumerHandle = surfaceTexture != nullptr
        ? oh_surface_texture_get_consumer_handle(env, surfaceTexture) : nullptr;
    BindConsumerHandle(layer, consumerHandle);
    fprintf(stderr,
            "[WESTLAKE-TEXTURELAYER-774] surface layer=%p surfaceTexture=%p consumer=%p\n",
            layer, surfaceTexture, consumerHandle);
    fflush(stderr);
}

void TextureLayer_nUpdateSurfaceTexture(JNIEnv*, jclass, jlong layerUpdater)
{
    (void)InstallApplyHook();
    auto* layer = reinterpret_cast<DeferredLayerUpdater*>(layerUpdater);
    if (layer != nullptr) {
        // Preserve the Android state transition even though the stock apply()
        // has no ASurfaceTexture object on this OH-backed path.
        layer->updateTexImage();
    }
    const uint32_t count = gUpdateCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 8 || (count % 120) == 0) {
        fprintf(stderr, "[WESTLAKE-TEXTURELAYER-774] update layer=%p count=%u\n",
                layer, count);
        fflush(stderr);
    }
}

const JNINativeMethod kTextureLayerMethods[] = {
    {"nPrepare", "(JIIZ)Z", reinterpret_cast<void*>(TextureLayer_nPrepare)},
    {"nSetLayerPaint", "(JJ)V", reinterpret_cast<void*>(TextureLayer_nSetLayerPaint)},
    {"nSetTransform", "(JJ)V", reinterpret_cast<void*>(TextureLayer_nSetTransform)},
    {"nSetSurfaceTexture", "(JLandroid/graphics/SurfaceTexture;)V",
     reinterpret_cast<void*>(TextureLayer_nSetSurfaceTexture)},
    {"nUpdateSurfaceTexture", "(J)V",
     reinterpret_cast<void*>(TextureLayer_nUpdateSurfaceTexture)},
};

}  // namespace

int register_android_graphics_TextureLayer_probe(JNIEnv* env)
{
    const bool hookInstalled = InstallApplyHook();
    jclass clazz = env->FindClass("android/graphics/TextureLayer");
    if (clazz == nullptr) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return -1;
    }
    const int result = env->RegisterNatives(
        clazz, kTextureLayerMethods,
        static_cast<jint>(sizeof(kTextureLayerMethods) /
                          sizeof(kTextureLayerMethods[0])));
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteLocalRef(clazz);
    fprintf(stderr, "[WESTLAKE-TEXTURELAYER-774] register rc=%d hook=%d\n",
            result, hookInstalled ? 1 : 0);
    fflush(stderr);
    return result;
}

}  // namespace android
