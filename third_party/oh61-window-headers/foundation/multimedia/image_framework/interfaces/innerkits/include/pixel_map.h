/*
 * Copyright (C) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INTERFACES_INNERKITS_INCLUDE_PIXEL_MAP_H_
#define INTERFACES_INNERKITS_INCLUDE_PIXEL_MAP_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#ifdef IMAGE_COLORSPACE_FLAG
#include "color_space.h"
#endif
#include "image_type.h"
#include "parcel.h"
#ifdef IMAGE_PURGEABLE_PIXELMAP
#include "purgeable_mem_base.h"
#include "purgeable_mem_builder.h"
#endif
#if !defined(_WIN32) && !defined(_APPLE) && !defined(IOS_PLATFORM) && !defined(ANDROID_PLATFORM)
#include "pixel_map_parcel.h"
#endif

namespace OHOS::Rosen {
class PixelMapStorage;
class RSMarshallingHelper;
class RSProfiler;
class RSModifiersDraw;
};

namespace OHOS {
namespace Media {
struct HdrMetadata;
enum class ImageHdrType : int32_t;
using TransColorProc = bool (*)(const uint8_t *in, uint32_t inCount, uint32_t *out, uint32_t outCount);
using CustomFreePixelMap = void (*)(void *addr, void *context, uint32_t size);

typedef struct {
    float scaleX;
    float scaleY;
    float rotateD;
    float cropLeft;
    float cropTop;
    float cropWidth;
    float cropHeight;
    float translateX;
    float translateY;
    bool flipX;
    bool flipY;
} TransformData;

struct InitializationOptions {
    Size size;
    PixelFormat srcPixelFormat = PixelFormat::BGRA_8888;
    PixelFormat pixelFormat = PixelFormat::UNKNOWN;
    AlphaType alphaType = AlphaType::IMAGE_ALPHA_TYPE_UNKNOWN;
    ScaleMode scaleMode = ScaleMode::FIT_TARGET_SIZE;
    YUVConvertColorSpaceDetails convertColorSpace;
    int32_t srcRowStride = 0;
    AllocatorType allocatorType = AllocatorType::DEFAULT;
    bool editable = false;
    bool useSourceIfMatch = false;
    bool useDMA = false;
};
struct TransInfos;
struct HdrInfo;

// Build ARGB_8888 pixel value
constexpr uint8_t ARGB_MASK = 0xFF;
constexpr uint8_t ARGB_A_SHIFT = 24;
constexpr uint8_t ARGB_R_SHIFT = 16;
constexpr uint8_t ARGB_G_SHIFT = 8;
constexpr uint8_t ARGB_B_SHIFT = 0;
// Define pixel map malloc max size 600MB
// Memory copy will be performed twice on heap memory during IPC, so the size has to be limited
constexpr int32_t PIXEL_MAP_MAX_RAM_SIZE = 600 * 1024 * 1024;

typedef struct PixelMapError {
    uint32_t errorCode = 0;
    std::string errorInfo = "";
} PIXEL_MAP_ERR;

typedef struct BuildParam {
    int32_t offset_ = 0;
    int32_t width_ = 0;
    bool flag_ = true;
} BUILD_PARAM;

struct PixelMemInfo {
    uint8_t* base = nullptr;
    void* context = nullptr;
    int32_t bufferSize = 0;
    AllocatorType allocatorType = AllocatorType::SHARE_MEM_ALLOC;
    bool isAstc = false;
    bool displayOnly = false;
};

struct RWPixelsOptions {
    const uint8_t* pixels = nullptr;
    uint64_t bufferSize = 0;
    uint32_t offset = 0;
    uint32_t stride = 0;
    Rect region;
    PixelFormat pixelFormat = PixelFormat::BGRA_8888;
};

class ExifMetadata;
class AbsMemory;

#define PIXELMAP_VERSION_START (1<<16)
#define PIXELMAP_VERSION_DISPLAY_ONLY (PIXELMAP_VERSION_START + 1)
#define PIXELMAP_VERSION_LATEST PIXELMAP_VERSION_DISPLAY_ONLY

class PixelMap : public Parcelable, public PIXEL_MAP_ERR {
public:
#if !defined(_WIN32) && !defined(_APPLE) && !defined(IOS_PLATFORM) && !defined(ANDROID_PLATFORM)
    friend class PixelMapRecordParcel;
#endif
    static std::atomic<uint32_t> currentId;
    PixelMap()
    {
        uniqueId_ = currentId.fetch_add(1, std::memory_order_relaxed);
    }
    virtual ~PixelMap();

    /**
     * Create a PixelMap through pixel data.
     *
     * @param colors The pixel data.
     * @param colorLength The length of the pixel data.
     * @param opts Initialization Options.
     * @return The PixelMap.
     */
    NATIVEEXPORT static std::unique_ptr<PixelMap> Create(const uint32_t *colors, uint32_t colorLength,
                                                         const InitializationOptions &opts);

    /**
     * Create a PixelMap through pixel data.
     *
     * @param colors The pixel data.
     * @param colorLength The length of the pixel data.
     * @param offset The location of the pixel data.
     * @param stride the stride.
     * @param opts Initialization Options.
     * @return The PixelMap.
     */
    NATIVEEXPORT static std::unique_ptr<PixelMap> Create(const uint32_t *colors, uint32_t colorLength, int32_t offset,
                                                         int32_t stride, const InitializationOptions &opts);
    /**
     * Create a PixelMap through pixel data.
     *
     * @param colors The pixel data.
     * @param colorLength The length of the pixel data.
     * @param offset The location of the pixel data.
     * @param stride the stride.
     * @param opts Initialization Options.
     * @param useCustomFormat Use default value.
     * @return The PixelMap.
     */
    NATIVEEXPORT static std::unique_ptr<PixelMap> Create(const uint32_t *colors, uint32_t colorLength, int32_t offset,
        int32_t stride, const InitializationOptions &opts, bool useCustomFormat);

    /**
     * Create a PixelMap through pixel data.
     *
     * @param colors The pixel data.
     * @param colorLength The length of the pixel data.
     * @param info params.
     * @param opts Initialization Options.
     * @param errorCode error code.
     * @return The PixelMap.
     */
    NATIVEEXPORT static std::unique_ptr<PixelMap> Create(const uint32_t *colors, uint32_t colorLength,
        BUILD_PARAM &info, const InitializationOptions &opts, int &errorCode);

    /**
     * Create a PixelMap through InitializationOptions.
     *
     * @param opts Initialization Options.
     * @return The PixelMap.
     */
    NATIVEEXPORT static std::unique_ptr<PixelMap> Create(const InitializationOptions &opts);

    /**
     * Create a new pixelmap using the pixelmap.
     *
     * @param source The source pixelmap.
     * @param opts Initialization Options.
     * @return The PixelMap.
     */
    NATIVEEXPORT static std::unique_ptr<PixelMap> Create(PixelMap &source, const InitializationOptions &opts);

    /**
     * Create a new pixelmap using the pixelmap.
     *
     * @param source The source pixelmap.
     * @param srcRect Pixel range.
     * @param opts Initialization Options.
     * @return The PixelMap.
     */
    NATIVEEXPORT static std::unique_ptr<PixelMap> Create(PixelMap &source, const Rect &srcRect,
                                                         const InitializationOptions &opts);

    /**
     * Create a new pixelmap using the pixelmap.
     *
     * @param source The source pixelmap.
     * @param srcRect Pixel range.
     * @param opts Initialization Options.
     * @param errorCode error code.
     * @return The PixelMap.
     */
    NATIVEEXPORT static std::unique_ptr<PixelMap> Create(PixelMap &source, const Rect &srcRect,
        const InitializationOptions &opts, int32_t &errorCode);

    /**
     * Create a new pixelmap using the astc.
     *
     * @param source The source pixelmap.
     * @param errorCode error code.
     * @param destFormat object format.
     * @return The PixelMap.
     */
    NATIVEEXPORT static std::unique_ptr<PixelMap> ConvertFromAstc(PixelMap *source, uint32_t &errorCode,
        PixelFormat destFormat);

    /**
     * Set image information.
     *
     * @param info The objects that need to be set up.
     * @return Success returns 0, failure returns error code.
     */
    NATIVEEXPORT virtual uint32_t SetImageInfo(ImageInfo &info);

    /**
     * Set image information.
     *
     * @param info The objects that need to be set up.
     * @param isReused Memory needs to be released.
     * @return Success returns 0, failure returns error code.
     */
    NATIVEEXPORT virtual uint32_t SetImageInfo(ImageInfo &info, bool isReused);

    /**
     * Obtain the pixel address through byte coordinates.
     *
     * @param x x-coordinate.
     * @param y y-coordinate.
     * @return Return to the destination address.
     */
    NATIVEEXPORT virtual const uint8_t *GetPixel(int32_t x, int32_t y);

    /**
     * Obtain the pixel address through coordinates
     *
     * @param x x-coordinate.
     * @param y y-coordinate.
     * @return Return to the destination address.
     */
    NATIVEEXPORT virtual const uint8_t *GetPixel8(int32_t x, int32_t y);

    /**
     * Obtain the pixel address through two-byte coordinates.
     *
     * @param x x-coordinate.
     * @param y y-coordinate.
     * @return Return to the destination address.
     */
    NATIVEEXPORT virtual const uint16_t *GetPixel16(int32_t x, int32_t y);

    /**
     * Obtain the pixel address through four-byte coordinates.
     *
     * @param x x-coordinate.
     * @param y y-coordinate.
     * @return Return to the destination address.
     */
    NATIVEEXPORT virtual const uint32_t *GetPixel32(int32_t x, int32_t y);

    /**
     * Get ARGB pixel points based on the coordinates.
     *
     * @param x x-coordinate.
     * @param y y-coordinate.
     * @param color The pixels that need to be obtained.
     */
    NATIVEEXPORT virtual bool GetARGB32Color(int32_t x, int32_t y, uint32_t &color);

    /**
     * Get RGBA1010102 pixel points based on the coordinates.
     *
     * @param x x-coordinate.
     * @param y y-coordinate.
     * @param color The pixels that need to be obtained.
     */
    NATIVEEXPORT virtual bool GetRGBA1010102Color(int32_t x, int32_t y, uint32_t &color);

    /**
     * Set pixel buffer information.
     *
     * @param addr Pixel address.
     * @param context Buffer descriptor.
     * @param size Pixel size.
     * @param type Memory type.
     * @param func Memory Reclaimer.
     */
    NATIVEEXPORT virtual void SetPixelsAddr(void *addr, void *context, uint32_t size, AllocatorType type,
                                    CustomFreePixelMap func);

    /**
     * Set pixel buffer information.
     *
     * @param addr Pixel address.
     * @param context Buffer descriptor.
     * @param size Pixel size.
     * @param type Memory type.
     * @param displayOnly Whether to flush buffer.
     */
    NATIVEEXPORT virtual void SetPixelsAddr(void *addr, void *context, uint32_t size, AllocatorType type,
                                    bool displayOnly);

    /**
     * Get pixel step size.
     */
    NATIVEEXPORT virtual int32_t GetPixelBytes();

    /**
     * Get step length.
     */
    NATIVEEXPORT virtual int32_t GetRowBytes();

    /**
     * Get the pixel length.
     */
    NATIVEEXPORT virtual int32_t GetByteCount();

    /**
     * Obtain the size of the pixel buffer.
     */
    NATIVEEXPORT virtual uint32_t GetAllocationByteCount();

    /**
     * Get the width of the bitmap.
     */
    NATIVEEXPORT virtual int32_t GetWidth();

    /**
     * Get the height of the bitmap.
     */
    NATIVEEXPORT virtual int32_t GetHeight();

    /**
     * Get the actual size of ASTC.
     */
    NATIVEEXPORT void GetAstcRealSize(Size &size)
    {
        size = astcrealSize_;
    }

    /**
     * set the actual size of ASTC.
     */
    NATIVEEXPORT void SetAstcRealSize(Size size)
    {
        astcrealSize_ = size;
    }

    /**
     * Get the ASTC transform information.
     */
    NATIVEEXPORT void GetTransformData(TransformData &transformData);

    /**
     * Set the ASTC transform information.
     */
    NATIVEEXPORT void SetTransformData(TransformData transformData);

    /**
     * Get the baseDensity.
     */
    NATIVEEXPORT virtual int32_t GetBaseDensity();

    /**
     * PixelMap zooming.
     *
     * @param xAxis X-axis scaling ratio.
     * @param yAxis y-axis scaling ratio.
     */
    NATIVEEXPORT virtual void scale(float xAxis, float yAxis);

    /**
     * PixelMap zooming.
     *
     * @param xAxis X-axis scaling ratio.
     * @param yAxis y-axis scaling ratio.
     * @param option Scaling algorithm type.
     */
    NATIVEEXPORT virtual void scale(float xAxis, float yAxis, const AntiAliasingOption &option);

    /**
     * PixelMap zooming.
     *
     * @param xAxis X-axis scaling ratio.
     * @param yAxis y-axis scaling ratio.
     */
    NATIVEEXPORT virtual bool resize(float xAxis, float yAxis);

    /**
     * PixelMap traverse.
     *
     * @param xAxis X-axis scaling ratio.
     * @param yAxis y-axis scaling ratio.
     */
    NATIVEEXPORT virtual void translate(float xAxis, float yAxis);

    /**
     * PixelMap rotation.
     *
     * @param degrees rotation angle.
     */
    NATIVEEXPORT virtual void rotate(float degrees);

    /**
     * PixelMap inversion.
     *
     * @param xAxis X-axis scaling ratio.
     * @param yAxis y-axis scaling ratio.
     */
    NATIVEEXPORT virtual void flip(bool xAxis, bool yAxis);

    /**
     * PixelMap crop.
     *
     * @param rect The area that has been cut off.
     */
    NATIVEEXPORT virtual uint32_t crop(const Rect &rect);

    /**
     * Get pixelmap information.
     */
    NATIVEEXPORT virtual void GetImageInfo(ImageInfo &imageInfo);

    /**
     * Get pixelmap format.
     */
    NATIVEEXPORT virtual PixelFormat GetPixelFormat();

    /**
     * Get pixelmap colorspace.
     */
    NATIVEEXPORT virtual ColorSpace GetColorSpace();

    /**
     * Get pixelmap alpha type.
     */
    NATIVEEXPORT virtual AlphaType GetAlphaType();

    /**
     * Set pixelmap alpha.
     */
    NATIVEEXPORT virtual uint32_t SetAlpha(const float percent);

    /**
     * Get the pixel address.
     */
    NATIVEEXPORT virtual const uint8_t *GetPixels();

    /**
     * Obtain the A channel of the ARGB pixel point.
     *
     * @param color the pixel.
     */
    NATIVEEXPORT virtual uint8_t GetARGB32ColorA(uint32_t color);

    /**
     * Obtain the R channel of the ARGB pixel point.
     *
     * @param color the pixel.
     */
    NATIVEEXPORT virtual uint8_t GetARGB32ColorR(uint32_t color);

    /**
     * Obtain the G channel of the ARGB pixel point.
     *
     * @param color the pixel.
     */
    NATIVEEXPORT virtual uint8_t GetARGB32ColorG(uint32_t color);

    /**
     * Obtain the B channel of the ARGB pixel point.
     *
     * @param color the pixel.
     */
    NATIVEEXPORT virtual uint8_t GetARGB32ColorB(uint32_t color);

    /**
     * Pixelmap comparison function.
     *
     * @param other the pixel.
     * @return Return true if they are the same, otherwise return false.
     */
    NATIVEEXPORT virtual bool IsSameImage(const PixelMap &other);

    /**
     * Read the pixel buffer.
     *
     * @param opts RWPixelsOptions.
     * @return Return 0 if successful, otherwise return errorcode.
     */
    NATIVEEXPORT virtual uint32_t ReadPixels(const RWPixelsOptions &opts);

    /**
     * Read the pixel buffer.
     *
     * @param bufferSize buffer size.
     * @param offset deviation position.
     * @param stride stride.
     * @param region region.
     * @param dst To read the pixel buffer.
     * @return Return 0 if successful, otherwise return errorcode.
     */
    NATIVEEXPORT virtual uint32_t ReadPixels(const uint64_t &bufferSize, const uint32_t &offset, const uint32_t &stride,
        const Rect &region, uint8_t *dst);

    /**
     * Read the pixel buffer.
     *
     * @param bufferSize buffer size.
     * @param dst To read the pixel buffer.
     * @return Return 0 if successful, otherwise return errorcode.
     */
    NATIVEEXPORT virtual uint32_t ReadPixels(const uint64_t &bufferSize, uint8_t *dst);

    /**
     * Read the pixel information in the ARGB format.
     *
     * @param bufferSize buffer size.
     * @param dst To read the pixel buffer.
     * @return Return 0 if successful, otherwise return errorcode.
     */
    NATIVEEXPORT virtual uint32_t ReadARGBPixels(const uint64_t &bufferSize, uint8_t *dst);

    /**
     * Read the pixel address at the target position.
     *
     * @param pos impact point.
     * @param dst To read the pixel buffer.
     * @return Return 0 if successful, otherwise return errorcode.
     */
    NATIVEEXPORT virtual uint32_t ReadPixel(const Position &pos, uint32_t &dst);

    /**
     * ResetConfig.
     *
     * @param size buffer size.
     * @param format pixel format.
     * @return Return 0 if successful, otherwise return errorcode.
     */
    NATIVEEXPORT virtual uint32_t ResetConfig(const Size &size, const PixelFormat &format);

    /**
     * Set alpha type.
     *
     * @param alphaType alpha type.
     * @return Return true if successful, otherwise return false.
     */
    NATIVEEXPORT virtual bool SetAlphaType(const AlphaType &alphaType);

    /**
     * Write pixel points at the target position.
     *
     * @param pos target location.
     * @param color pixel.
     * @return Return 0 if successful, otherwise return errorcode.
     */
    NATIVEEXPORT virtual uint32_t WritePixel(const Position &pos, const uint32_t &color);

    /**
     * Write pixels at the target position.
     *
     * @param opts RWPixelsOptions.
     * @return Return 0 if successful, otherwise return errorcode.
     */
    NATIVEEXPORT virtual uint32_t WritePixels(const RWPixelsOptions &opts);

    /**
     * Write pixels at the target regin.
     *
     * @param source pixels addr.
     * @param bufferSize bufer size.
     * @param offset Offset point.
     * @param stride stride.
     * @param region region.
     * @return Return 0 if successful, otherwise return errorcode.
     */
    NATIVEEXPORT virtual uint32_t WritePixels(const uint8_t *source, const uint64_t &bufferSize, const uint32_t &offset,
                         const uint32_t &stride, const Rect &region);

    /**
     * Write pixels.
     *
     * @param source pixels addr.
     * @param bufferSize bufer size.
     * @return Return 0 if successful, otherwise return errorcode.
     */
    NATIVEEXPORT virtual uint32_t WritePixels(const uint8_t *source, const uint64_t &bufferSize);
    NATIVEEXPORT virtual bool WritePixels(const uint32_t &color);
    /**
     * Release the pixel buffer.
     */
    NATIVEEXPORT virtual void FreePixelMap();
    /**
     * IsStrideAlignment.
     */
    NATIVEEXPORT bool IsStrideAlignment();
    /**
     * Get memory type.
     */
    NATIVEEXPORT virtual AllocatorType GetAllocatorType();
    /**
     * Get file descriptor.
     */
    NATIVEEXPORT virtual void *GetFd() const;
    NATIVEEXPORT virtual void SetFreePixelMapProc(CustomFreePixelMap func);
    NATIVEEXPORT virtual void SetTransformered(bool isTransformered);
    NATIVEEXPORT uint32_t ConvertAlphaFormat(PixelMap &wPixelMap, const bool isPremul);
    NATIVEEXPORT bool AttachAddrBySurfaceBuffer();
    NATIVEEXPORT void SetPixelMapError(uint32_t code, const std::string &info)
    {
        errorCode = code;
        errorInfo = info;
    }

    NATIVEEXPORT static void ConstructPixelMapError(PIXEL_MAP_ERR &err, uint32_t code, const std::string &info)
    {
        err.errorCode = code;
        err.errorInfo = info;
    }

    NATIVEEXPORT virtual void SetRowStride(uint32_t stride);
    NATIVEEXPORT virtual int32_t GetRowStride()
    {
        return rowStride_;
    }
    NATIVEEXPORT virtual uint32_t GetCapacity()
    {
        return pixelsSize_;
    }

    NATIVEEXPORT virtual bool IsEditable()
    {
        return editable_;
    }

    NATIVEEXPORT virtual void SetModifiable(bool modifiable)
    {
        modifiable_ = modifiable;
    }
    NATIVEEXPORT virtual bool IsModifiable()
    {
        return modifiable_;
    }

    NATIVEEXPORT virtual bool IsTransformered()
    {
        return isTransformered_;
    }

    // judgement whether create pixelmap use source as result
    NATIVEEXPORT virtual bool IsSourceAsResponse()
    {
        return useSourceAsResponse_;
    }

    NATIVEEXPORT virtual void *GetWritablePixels() const
    {
        if (!const_cast<PixelMap*>(this)->AttachAddrBySurfaceBuffer()) {
            return nullptr;
        }
        return static_cast<void *>(data_);
    }

    NATIVEEXPORT virtual uint32_t GetUniqueId() const
    {
        return uniqueId_;
    }

    /**
     * Serialize the pixelmap into a parcel.
     */
    NATIVEEXPORT virtual bool Marshalling(Parcel &data) const override;
    /**
     * Deserialize the parcel to generate the pixelmap.
     */
    NATIVEEXPORT static PixelMap *UnmarshallingWithIsDisplay(Parcel &parcel,
        std::function<int(Parcel &parcel, std::function<int(Parcel&)> readFdDefaultFunc)> readSafeFdFunc,
        bool isDisplay = false);
    NATIVEEXPORT static PixelMap *Unmarshalling(Parcel &data,
        std::function<int(Parcel &parcel, std::function<int(Parcel&)> readFdDefaultFunc)> readSafeFdFunc = nullptr);
    NATIVEEXPORT static PixelMap *Unmarshalling(Parcel &parcel, PIXEL_MAP_ERR &error,
        std::function<int(Parcel &parcel, std::function<int(Parcel&)> readFdDefaultFunc)> readSafeFdFunc = nullptr,
        bool isDisplay = false);
    /**
     * Serialize the pixelmap into a vector in TLV format.
     */
    NATIVEEXPORT virtual bool EncodeTlv(std::vector<uint8_t> &buff) const;
    /**
     * Deserialize the vector data in the form of TLV to generate a pixelmap.
     */
    NATIVEEXPORT static PixelMap *DecodeTlv(std::vector<uint8_t> &buff);
    NATIVEEXPORT virtual void SetImageYUVInfo(YUVDataInfo &yuvinfo)
    {
        yuvDataInfo_ = yuvinfo;
    }
    NATIVEEXPORT virtual void AssignYuvDataOnType(PixelFormat format, int32_t width, int32_t height);
    NATIVEEXPORT virtual void UpdateYUVDataInfo(PixelFormat format, int32_t width, int32_t height,
        YUVStrideInfo &strides);
    NATIVEEXPORT virtual void GetImageYUVInfo(YUVDataInfo &yuvInfo) const
    {
        yuvInfo = yuvDataInfo_;
    }
    NATIVEEXPORT bool IsYuvFormat() const;

#ifdef IMAGE_COLORSPACE_FLAG
    // -------[inner api for ImageSource/ImagePacker codec] it will get a colorspace object pointer----begin----
    NATIVEEXPORT void InnerSetColorSpace(const OHOS::ColorManager::ColorSpace &grColorSpace, bool direct = false);
    NATIVEEXPORT OHOS::ColorManager::ColorSpace InnerGetGrColorSpace();
    NATIVEEXPORT std::shared_ptr<OHOS::ColorManager::ColorSpace> InnerGetGrColorSpacePtr()
    {
        return grColorSpace_;
    }
    /**
     * ApplyColorSpace.
     */
    NATIVEEXPORT virtual uint32_t ApplyColorSpace(const OHOS::ColorManager::ColorSpace &grColorSpace);
    // -------[inner api for ImageSource/ImagePacker codec] it will get a colorspace object pointer----end-------
#endif

#ifdef IMAGE_PURGEABLE_PIXELMAP
    NATIVEEXPORT bool IsPurgeable() const
    {
        return purgeableMemPtr_ != nullptr;
    }

    NATIVEEXPORT std::shared_ptr<PurgeableMem::PurgeableMemBase> GetPurgeableMemPtr() const
    {
        return purgeableMemPtr_;
    }

    NATIVEEXPORT void SetPurgeableMemPtr(std::shared_ptr<PurgeableMem::PurgeableMemBase> pmPtr)
    {
        purgeableMemPtr_ = pmPtr;
    }
#endif

    NATIVEEXPORT bool IsAstc()
    {
        return isAstc_;
    }

    NATIVEEXPORT void SetAstc(bool isAstc)
    {
        isAstc_ = isAstc;
    }

    NATIVEEXPORT std::shared_ptr<ExifMetadata> GetExifMetadata()
    {
        return exifMetadata_;
    }

    NATIVEEXPORT void SetExifMetadata(std::shared_ptr<ExifMetadata> &ptr)
    {
        exifMetadata_ = ptr;
    }

    NATIVEEXPORT uint32_t GetImagePropertyInt(const std::string &key, int32_t &value);
    NATIVEEXPORT uint32_t GetImagePropertyString(const std::string &key, std::string &value);
    NATIVEEXPORT uint32_t ModifyImageProperty(const std::string &key, const std::string &value);
    NATIVEEXPORT uint32_t SetMemoryName(const std::string &pixelMapName);
    NATIVEEXPORT virtual std::unique_ptr<PixelMap> Clone(int32_t &errorCode);

    NATIVEEXPORT bool IsHdr();
    NATIVEEXPORT void SetAstcHdr(bool astcHdr);
    NATIVEEXPORT uint32_t ToSdr();
    // format support rgba8888, nv12, nv21. The default value is rgba8888
    // If toSRGB is false, pixelmap will be converted to display_p3
    NATIVEEXPORT uint32_t ToSdr(PixelFormat format, bool toSRGB);
    // use for hdr pixelmap, If isSRGB is false, the colorspace is p3 when converting to SDR.
    NATIVEEXPORT void SetToSdrColorSpaceIsSRGB(bool isSRGB);
    NATIVEEXPORT bool GetToSdrColorSpaceIsSRGB();

    NATIVEEXPORT std::shared_ptr<HdrMetadata> GetHdrMetadata()
    {
        return hdrMetadata_;
    }

    NATIVEEXPORT void SetHdrMetadata(const std::shared_ptr<HdrMetadata> &metadata)
    {
        hdrMetadata_ = metadata;
    }

    NATIVEEXPORT ImageHdrType GetHdrType()
    {
        return hdrType_;
    }

    NATIVEEXPORT void SetHdrType(ImageHdrType hdrType)
    {
        hdrType_ = hdrType;
    }

    // unmap方案, 减少RenderService内存占用
    NATIVEEXPORT bool UnMap();
    NATIVEEXPORT bool ReMap();
    NATIVEEXPORT bool IsUnMap()
    {
        std::lock_guard<std::mutex> lock(*unmapMutex_);
        return isUnMap_;
    }
    NATIVEEXPORT void IncreaseUseCount()
    {
        std::lock_guard<std::mutex> lock(*unmapMutex_);
        useCount_ += 1;
    }
    NATIVEEXPORT void DecreaseUseCount()
    {
        std::lock_guard<std::mutex> lock(*unmapMutex_);
        if (useCount_ > 0) {
            useCount_ -= 1;
        }
    }
    NATIVEEXPORT uint64_t GetUseCount()
    {
        std::lock_guard<std::mutex> lock(*unmapMutex_);
        return useCount_;
    }

    // pixelmap with DMA memory should be marked dirty when memory was changed
    NATIVEEXPORT void MarkDirty()
    {
        isMemoryDirty_ = true;
    }

    NATIVEEXPORT bool IsMemoryDirty()
    {
        return isMemoryDirty_;
    }

    NATIVEEXPORT bool IsPropertiesDirty()
    {
        std::lock_guard<std::mutex> lock(*propertiesDirtyMutex_);
        return isPropertiesDirty_;
    }

    NATIVEEXPORT void SetEditable(bool editable)
    {
        editable_ = editable;
    }

    static int32_t GetRGBxRowDataSize(const ImageInfo& info);
    static int32_t GetRGBxByteCount(const ImageInfo& info);
    static int32_t GetYUVByteCount(const ImageInfo& info);
    static int32_t GetAllocatedByteCount(const ImageInfo& info);

    NATIVEEXPORT uint32_t GetVersionId();
    NATIVEEXPORT void AddVersionId();
    uint64_t GetNoPaddingUsage();

protected:
    static constexpr size_t MAX_IMAGEDATA_SIZE = 128 * 1024 * 1024; // 128M
    static constexpr size_t MIN_IMAGEDATA_SIZE = 32 * 1024;         // 32k
    friend class ImageSource;
    friend class OHOS::Rosen::PixelMapStorage;
    friend class OHOS::Rosen::RSMarshallingHelper;
    friend class OHOS::Rosen::RSProfiler;
    static bool ALPHA8ToARGB(const uint8_t *in, uint32_t inCount, uint32_t *out, uint32_t outCount);
    static bool RGB565ToARGB(const uint8_t *in, uint32_t inCount, uint32_t *out, uint32_t outCount);
    static bool ARGB8888ToARGB(const uint8_t *in, uint32_t inCount, uint32_t *out, uint32_t outCount);
    static bool RGBA8888ToARGB(const uint8_t *in, uint32_t inCount, uint32_t *out, uint32_t outCount);
    static bool BGRA8888ToARGB(const uint8_t *in, uint32_t inCount, uint32_t *out, uint32_t outCount);
    static bool RGB888ToARGB(const uint8_t *in, uint32_t inCount, uint32_t *out, uint32_t outCount);
    static bool CheckParams(const uint32_t *colors, uint32_t colorLength, int32_t offset, int32_t stride,
        const InitializationOptions &opts);
    static void UpdatePixelsAlpha(const AlphaType &alphaType, const PixelFormat &pixelFormat, uint8_t *dstPixels,
        PixelMap &dstPixelMap);
    static void InitDstImageInfo(const InitializationOptions &opts, const ImageInfo &srcImageInfo,
        ImageInfo &dstImageInfo);
    static bool CopyPixMapToDst(PixelMap &source, void* &dstPixels, int &fd, uint32_t bufferSize);
    static bool CopyPixelMap(PixelMap &source, PixelMap &dstPixelMap, int32_t &error);
    static bool CopyPixelMap(PixelMap &source, PixelMap &dstPixelMap);
    static bool SourceCropAndConvert(PixelMap &source, const ImageInfo &srcImageInfo, const ImageInfo &dstImageInfo,
                                     const Rect &srcRect, PixelMap &dstPixelMap);
    static bool IsSameSize(const Size &src, const Size &dst);
    static bool ScalePixelMap(const Size &targetSize, const Size &dstSize, const ScaleMode &scaleMode,
                              PixelMap &dstPixelMap);
    static bool IsYuvFormat(PixelFormat format);
    bool GetPixelFormatDetail(const PixelFormat format);
    uint32_t CheckAlphaFormatInput(PixelMap &wPixelMap, const bool isPremul);
    bool CheckPixelsInput(const uint8_t *dst, const uint64_t &bufferSize, const uint32_t &offset,
                          const uint32_t &stride, const Rect &region);
    void ReleaseSharedMemory(void *addr, void *context, uint32_t size);
    static void ReleaseBuffer(AllocatorType allocatorType, int fd, uint64_t dataSize, void **buffer);
    static void *AllocSharedMemory(const uint64_t bufferSize, int &fd, uint32_t uniqueId);
    bool WritePropertiesToParcel(Parcel &parcel) const;
    static bool ReadPropertiesFromParcel(Parcel& parcel, PixelMap*& pixelMap, ImageInfo& imgInfo,
        PixelMemInfo& memInfo);
    bool ReadBufferSizeFromParcel(Parcel& parcel, const ImageInfo& imgInfo, PixelMemInfo& memInfo,
        PIXEL_MAP_ERR& error);
    bool WriteMemInfoToParcel(Parcel &parcel, const int32_t &bufferSize) const;
    static bool ReadMemInfoFromParcel(Parcel &parcel, PixelMemInfo &pixelMemInfo, PIXEL_MAP_ERR &error,
        std::function<int(Parcel &parcel, std::function<int(Parcel&)> readFdDefaultFunc)> readSafeFdFunc = nullptr,
        bool isDisplay = false);
    bool WriteTransformDataToParcel(Parcel &parcel) const;
    bool ReadTransformData(Parcel &parcel, PixelMap *pixelMap);
    bool WriteAstcInfoToParcel(Parcel &parcel) const;
    bool ReadAstcInfo(Parcel &parcel, PixelMap *pixelMap);
    bool WriteYuvDataInfoToParcel(Parcel &parcel) const;
    bool ReadYuvDataInfoFromParcel(Parcel &parcel, PixelMap *pixelMap);
    uint32_t SetRowDataSizeForImageInfo(ImageInfo info);

    void ResetPixelMap()
    {
        rowDataSize_ = 0;
        pixelBytes_ = 0;
        colorProc_ = nullptr;
    }

    bool CheckValidParam(int32_t x, int32_t y)
    {
        return isUnMap_ || data_ == nullptr || x >= imageInfo_.size.width || x < 0 || y >= imageInfo_.size.height ||
            y < 0 || (pixelsSize_ < static_cast<uint64_t>(rowDataSize_) * imageInfo_.size.height) ? false : true;
    }

    static PixelMap *StartUnmarshalling(Parcel &parcel, ImageInfo &imgInfo,
        PixelMemInfo &pixelMemInfo, PIXEL_MAP_ERR &error);
    static PixelMap *FinishUnmarshalling(PixelMap* pixelMap, Parcel &parcel,
        ImageInfo &imgInfo, PixelMemInfo &pixelMemInfo, PIXEL_MAP_ERR &error);

    static void ReleaseMemory(AllocatorType allocType, void *addr, void *context, uint32_t size);
    static bool UpdatePixelMapMemInfo(PixelMap *pixelMap, ImageInfo &imgInfo, PixelMemInfo &pixelMemInfo);
    bool WriteImageData(Parcel &parcel, size_t size) const;
    bool WriteAshmemDataToParcel(Parcel &parcel, size_t size) const;
    static uint8_t *ReadImageData(Parcel &parcel, int32_t size,
        std::function<int(Parcel &parcel, std::function<int(Parcel&)> readFdDefaultFunc)> readSafeFdFunc = nullptr);
    static uint8_t *ReadHeapDataFromParcel(Parcel &parcel, int32_t bufferSize);
    static uint8_t *ReadAshmemDataFromParcel(Parcel &parcel, int32_t bufferSize,
        std::function<int(Parcel &parcel, std::function<int(Parcel&)> readFdDefaultFunc)> readSafeFdFunc = nullptr);
    static int ReadFileDescriptor(Parcel &parcel);
    static bool WriteFileDescriptor(Parcel &parcel, int fd);
    static bool ReadImageInfo(Parcel &parcel, ImageInfo &imgInfo);
    bool WriteImageInfo(Parcel &parcel) const;
    static uint8_t ReadUint8(std::vector<uint8_t> &buff, int32_t &cursor);
    void WriteData(std::vector<uint8_t> &buff, const uint8_t *data,
        const int32_t &height, const int32_t &rowDataSize, const int32_t &rowStride) const;
    static bool ReadTlvAttr(std::vector<uint8_t>& buff, ImageInfo& info, std::unique_ptr<AbsMemory>& mem, int32_t& csm);
    bool DoTranslation(TransInfos &infos, const AntiAliasingOption &option = AntiAliasingOption::NONE,
        bool fixPixelFormat = false);
    void UpdateImageInfo();
    static int32_t ConvertPixelAlpha(const void *srcPixels, const int32_t srcLength, const ImageInfo &srcInfo,
        void *dstPixels, const ImageInfo &dstInfo);
    void CopySurfaceBufferInfo(void *data);
    void SetVersionId(uint32_t versionId);
    std::unique_ptr<AbsMemory> CreateSdrMemory(ImageInfo &imageInfo, PixelFormat format,
                                               AllocatorType dstType, uint32_t &errorCode, bool toSRGB);
    // used to close fd after mmap in RenderService when memory type is shared-mem or dma.
    bool CloseFd();
    uint32_t CheckPixelMapForWritePixels();

    uint8_t *data_ = nullptr;
    // this info SHOULD be the final info for decoded pixelmap, not the original image info
    ImageInfo imageInfo_;
    int32_t rowDataSize_ = 0;
    int32_t rowStride_ = 0;
    int32_t pixelBytes_ = 0;
    TransColorProc colorProc_ = nullptr;
    void *context_ = nullptr;
    CustomFreePixelMap custFreePixelMap_ = nullptr;
    CustomFreePixelMap freePixelMapProc_ = nullptr;
    AllocatorType allocatorType_ = AllocatorType::SHARE_MEM_ALLOC;
    uint32_t pixelsSize_ = 0;
    bool editable_ = false;
    bool modifiable_ = true; // If this is set to false, any modifications to the pixels data is not allowed
    bool useSourceAsResponse_ = false;
    bool isTransformered_ = false;
    std::shared_ptr<std::mutex> transformMutex_ = std::make_shared<std::mutex>();

    // only used by rosen backend
    uint32_t uniqueId_ = 0;
    bool isAstc_ = false;
    TransformData transformData_ = {1, 1, 0, 0, 0, 0, 0, 0, 0, false, false};
    Size astcrealSize_;
    std::shared_ptr<HdrMetadata> hdrMetadata_ = nullptr;
    ImageHdrType hdrType_ = static_cast<ImageHdrType>(0);

#ifdef IMAGE_COLORSPACE_FLAG
    std::shared_ptr<OHOS::ColorManager::ColorSpace> grColorSpace_ = nullptr;
#else
    std::shared_ptr<uint8_t> grColorSpace_ = nullptr;
#endif

#ifdef IMAGE_PURGEABLE_PIXELMAP
    std::shared_ptr<PurgeableMem::PurgeableMemBase> purgeableMemPtr_ = nullptr;
#else
    std::shared_ptr<uint8_t> purgeableMemPtr_ = nullptr;
#endif
    YUVDataInfo yuvDataInfo_;
    std::shared_ptr<ExifMetadata> exifMetadata_ = nullptr;
    std::shared_ptr<std::mutex> metadataMutex_ = std::make_shared<std::mutex>();
    std::shared_ptr<std::mutex> translationMutex_ = std::make_shared<std::mutex>();
    std::shared_ptr<std::shared_mutex> colorSpaceMutex_ = std::make_shared<std::shared_mutex>();
    bool toSdrColorIsSRGB_ = false;
    uint32_t versionId_ = 1;
    std::shared_ptr<std::shared_mutex> versionMutex_ = std::make_shared<std::shared_mutex>();
private:
    NATIVEEXPORT bool IsDisplayOnly()
    {
        return displayOnly_;
    }

    NATIVEEXPORT void SetDisplayOnly(bool displayOnly)
    {
        displayOnly_ = displayOnly;
    }

    NATIVEEXPORT void SetReadVersion(int32_t version)
    {
        readVersion_ = version;
    }

    NATIVEEXPORT int32_t GetReadVersion()
    {
        return readVersion_;
    }

    void MarkPropertiesDirty()
    {
        std::lock_guard<std::mutex> lock(*propertiesDirtyMutex_);
        isPropertiesDirty_ = true;
    }

    // unmap方案, 减少RenderService内存占用
    bool isUnMap_ = false;
    uint64_t useCount_ = 0ULL;
    std::shared_ptr<std::mutex> unmapMutex_ = std::make_shared<std::mutex>();

    // used to mark whether DMA memory should be refreshed
    mutable bool isMemoryDirty_ = false;

    // used to mark whether properties have been refreshed by user
    mutable bool isPropertiesDirty_ = false;
    std::shared_ptr<std::mutex> propertiesDirtyMutex_ = std::make_shared<std::mutex>();

    // used to mark whether pixelmap is unmarshalling
    bool isUnmarshalling_ = false;

    // pixelmap versioning added since 16th of April 2025
    int32_t readVersion_ = PIXELMAP_VERSION_LATEST;
    bool displayOnly_ = false;
    bool astcHdr_ = false;

    friend class OHOS::Rosen::RSModifiersDraw;
};
} // namespace Media
} // namespace OHOS

#endif // INTERFACES_INNERKITS_INCLUDE_PIXEL_MAP_H_
