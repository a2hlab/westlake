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

#ifndef INTERFACES_INNERKITS_INCLUDE_IMAGE_TYPE_H_
#define INTERFACES_INNERKITS_INCLUDE_IMAGE_TYPE_H_

#include <cinttypes>
#include <map>
#include <set>
#include <string>
#include <sstream>
#include <vector>
#include "color_space.h"

namespace OHOS {
namespace Media {
#ifdef _WIN32
#define NATIVEEXPORT __declspec(dllexport)
#else
#define NATIVEEXPORT
#endif

#define FRAGMENT_METADATA_KEY_X "XInOriginal"
#define FRAGMENT_METADATA_KEY_Y "YInOriginal"
#define FRAGMENT_METADATA_KEY_WIDTH "FragmentImageWidth"
#define FRAGMENT_METADATA_KEY_HEIGHT "FragmentImageHeight"

#define GIF_METADATA_KEY_DELAY_TIME "GifDelayTime"
#define GIF_METADATA_KEY_DISPOSAL_TYPE "GifDisposalType"

#define HEIFS_METADATA_KEY_DELAY_TIME "HeifsDelayTime"

#define WEBP_METADATA_KEY_CANVAS_PIXEL_HEIGHT "WebPCanvasHeight"
#define WEBP_METADATA_KEY_CANVAS_PIXEL_WIDTH "WebPCanvasWidth"
#define WEBP_METADATA_KEY_DELAY_TIME "WebPDelayTime"
#define WEBP_METADATA_KEY_UNCLAMPED_DELAY_TIME "WebPUnclampedDelayTime"
#define WEBP_METADATA_KEY_LOOP_COUNT "WebPLoopCount"

// There is no definite tag name for gainmap and thumbnail
#define AUXILIARY_TAG_GAINMAP ""
#define AUXILIARY_TAG_DEPTH_MAP_BACK "DepthP"
#define AUXILIARY_TAG_DEPTH_MAP_FRONT "VShapEn"
#define AUXILIARY_TAG_UNREFOCUS_MAP "edof"
#define AUXILIARY_TAG_LINEAR_MAP "HighBit"
#define AUXILIARY_TAG_FRAGMENT_MAP "Fragmnt"
#define AUXILIARY_TAG_THUMBNAIL ""

#define HEIF_AUXTTYPE_ID_GAINMAP "urn:iso:std:iso:ts:21496:-1"
#define HEIF_AUXTTYPE_ID_DEPTH_MAP "urn:com:huawei:photo:5:0:0:aux:depthmap"
#define HEIF_AUXTTYPE_ID_UNREFOCUS_MAP "urn:com:huawei:photo:5:0:0:aux:unrefocusmap"
#define HEIF_AUXTTYPE_ID_LINEAR_MAP "urn:com:huawei:photo:5:0:0:aux:linearhmap"
#define HEIF_AUXTTYPE_ID_FRAGMENT_MAP "urn:com:huawei:photo:5:0:0:aux:fragmentmap"
#define HEIF_METADATA_ID_XTSTYLE "urn:com:huawei:photo:5:1:0:meta:xtstyle"
#define HEIF_AUXTTYPE_ID_THUMBNAIL "urn:com:huawei:photo:6:1:0:aux:thumbnail"

#define METADATA_TAG_RFDATAB "RfDataB\0"
#define METADATA_TAG_XTSTYLE "XtStyle\0"
#define METADATA_TAG_STDATA "STData\0"
#define METADATA_TAG_RFDATAB_DEPTHP "DepthP\0\0"
#define METADATA_TAG_RFDATAB_DEPTHEN "DepthEn\0"


constexpr uint8_t ASTC_EXTEND_INFO_TLV_NUM_6 = 6;

#define RFIMAGE_ID "urn:com:huawei:photo:5:1:0:meta:Res-Map"
#define METADATA_TAG_RESMAP "Res-Map\0"

constexpr const char *NS_XML = "http://www.w3.org/XML/1998/namespace";
constexpr const char *NS_XMP_BASIC = "http://ns.adobe.com/xap/1.0/";
constexpr const char *NS_XMP_RIGHTS = "http://ns.adobe.com/xap/1.0/rights/";
constexpr const char *NS_DC = "http://purl.org/dc/elements/1.1/";
constexpr const char *NS_EXIF = "http://ns.adobe.com/exif/1.0/";
constexpr const char *NS_TIFF = "http://ns.adobe.com/tiff/1.0/";

constexpr const char *PF_XML = "xml";
constexpr const char *PF_XMP_BASIC = "xmp";
constexpr const char *PF_XMP_RIGHTS = "xmpRights";
constexpr const char *PF_DC = "dc";
constexpr const char *PF_EXIF = "exif";
constexpr const char *PF_TIFF = "tiff";

enum class AllocatorType : int32_t {
    // keep same with java AllocatorType
    DEFAULT = 0,
    HEAP_ALLOC = 1,
    SHARE_MEM_ALLOC = 2,
    CUSTOM_ALLOC = 3,  // external
    DMA_ALLOC = 4, // SurfaceBuffer
};

enum class ColorSpace : int32_t {
    // unknown color space.
    UNKNOWN = 0,

    // based on SMPTE RP 431-2-2007 & IEC 61966-2.1:1999.
    DISPLAY_P3 = 1,

    // standard Red Green Blue based on IEC 61966-2.1:1999.
    SRGB = 2,

    // SRGB with a linear transfer function based on IEC 61966-2.1:1999.
    LINEAR_SRGB = 3,

    // based on IEC 61966-2-2:2003.
    EXTENDED_SRGB = 4,

    // based on IEC 61966-2-2:2003.
    LINEAR_EXTENDED_SRGB = 5,

    // based on standard illuminant D50 as the white point.
    GENERIC_XYZ = 6,

    // based on CIE XYZ D50 as the profile conversion space.
    GENERIC_LAB = 7,

    // based on SMPTE ST 2065-1:2012.
    ACES = 8,

    // based on Academy S-2014-004.
    ACES_CG = 9,

    // based on Adobe RGB (1998).
    ADOBE_RGB_1998 = 10,

    // based on SMPTE RP 431-2-2007.
    DCI_P3 = 11,

    // based on Rec. ITU-R BT.709-5.
    ITU_709 = 12,

    // based on Rec. ITU-R BT.2020-1.
    ITU_2020 = 13,

    // based on ROMM RGB ISO 22028-2:2013.
    ROMM_RGB = 14,

    // based on 1953 standard.
    NTSC_1953 = 15,

    // based on SMPTE C.
    SMPTE_C = 16,
};

enum class EncodedFormat : int32_t {
    UNKNOWN = 0,
    JPEG = 1,
    PNG = 2,
    GIF = 3,
    HEIF = 4,
    WEBP = 5,
    DNG = 6
};

enum class PixelFormat : int32_t {
    UNKNOWN = 0,
    ARGB_8888 = 1,  // Each pixel is stored on 4 bytes.
    RGB_565 = 2,    // Each pixel is stored on 2 bytes
    RGBA_8888 = 3,
    BGRA_8888 = 4,
    RGB_888 = 5,
    ALPHA_8 = 6,
    RGBA_F16 = 7,
    NV21 = 8,  // Each pixel is sorted on 3/2 bytes.
    NV12 = 9,
    RGBA_1010102 = 10,
    YCBCR_P010 = 11, // NV12_P010
    YCRCB_P010 = 12, // NV21_P010
    RGBA_U16 = 13, // Interim format for ffmpeg and skia conversion
    YUV_400 = 14,
    EXTERNAL_MAX,
    INTERNAL_START = 100,
    CMYK = INTERNAL_START + 1,
    ASTC_4x4 = 102,
    ASTC_6x6,
    ASTC_8x8,
};

enum class DecodeDynamicRange : int32_t {
    AUTO = 0,
    SDR = 1,
    HDR = 2,
};

enum class EncodeDynamicRange : int32_t {
    AUTO = 0, //10bit jpeg will be encode as HDR_VIVID_DUAL, others will be encode as SDR
    SDR,
    HDR_VIVID_DUAL,
    HDR_VIVID_SINGLE,
};

enum class AlphaType : int32_t {
    IMAGE_ALPHA_TYPE_UNKNOWN = 0,
    IMAGE_ALPHA_TYPE_OPAQUE = 1,   // image pixels are stored as opaque.
    IMAGE_ALPHA_TYPE_PREMUL = 2,   // image have alpha component, and all pixels have premultiplied by alpha value.
    IMAGE_ALPHA_TYPE_UNPREMUL = 3, // image have alpha component, and all pixels stored without premultiply alpha value.
};

enum class MemoryUsagePreference : int32_t {
    DEFAULT = 0,
    LOW_RAM = 1,  // low memory
};

enum class FinalOutputStep : int32_t {
    NO_CHANGE = 0,
    CONVERT_CHANGE = 1,
    ROTATE_CHANGE = 2,
    SIZE_CHANGE = 3,
    DENSITY_CHANGE = 4
};

enum class ResolutionQuality : int32_t {
    UNKNOWN = 0,
    LOW = 1,
    MEDIUM,
    HIGH
};

struct ColorYuv420 {
    uint8_t colorY = 0;
    uint8_t colorU = 0;
    uint8_t colorV = 0;
};

struct Position {
    int32_t x = 0;
    int32_t y = 0;
};

struct Rect {
    int32_t left = 0;
    int32_t top = 0;
    int32_t width = 0;
    int32_t height = 0;
};

struct Size {
    int32_t width = 0;
    int32_t height = 0;
};

struct ImageInfo {
    Size size;
    PixelFormat pixelFormat = PixelFormat::UNKNOWN;
    ColorSpace colorSpace = ColorSpace::SRGB;
    AlphaType alphaType = AlphaType::IMAGE_ALPHA_TYPE_UNKNOWN;
    int32_t baseDensity = 0;
    std::string encodedFormat;
};

struct YUVDataInfo {
    Size imageSize = {0, 0};
    uint32_t yWidth = 0;
    uint32_t yHeight = 0;
    uint32_t uvWidth = 0;
    uint32_t uvHeight = 0;
    uint32_t yStride = 0;
    uint32_t uStride = 0;
    uint32_t vStride = 0;
    uint32_t uvStride = 0;
    uint32_t yOffset = 0;
    uint32_t uOffset = 0;
    uint32_t vOffset = 0;
    uint32_t uvOffset = 0;
    std::string ToString() const {
        std::stringstream message;
        message << " yWidth: " << yWidth << ", yHeight: " << yHeight << ", uvWidth: " << uvWidth <<
        ", uvHeight: " << uvHeight << ", yStride: " << yStride << ", uStride: " << uStride <<
        ", vStride: " << vStride << ", uvStride: " << uvStride << ", yOffset: " << yOffset <<
        ", uOffset: " << uOffset << ", vOffset: " << vOffset << ", uvOffset: " << uvOffset;
        return message.str();
    }
};

struct Convert10bitInfo {
    PixelFormat srcPixelFormat = PixelFormat::UNKNOWN;
    uint32_t srcBytes = 0;
    PixelFormat dstPixelFormat = PixelFormat::UNKNOWN;
    uint32_t dstBytes = 0;
};

struct YUVStrideInfo {
    uint32_t yStride = 0;
    uint32_t uvStride = 0;
    uint32_t yOffset = 0;
    uint32_t uvOffset = 0;
};

struct RGBDataInfo {
    int32_t width = 0;
    int32_t height = 0;
    uint32_t stride = 0;
};

enum class YuvConversion : int {
    BT601 = 0,
    BT709 = 1,
    BT2020 = 2,
    BT240 = 3,
    BTFCC = 4,
    BT_MAX,
};

struct YUVConvertColorSpaceDetails {
    // Range: 0 means limit range, 1 means full range.
    uint8_t srcRange = 0;
    uint8_t dstRange = 0;
    YuvConversion srcYuvConversion = YuvConversion::BT601;
    YuvConversion dstYuvConversion = YuvConversion::BT601;
};

struct DestConvertInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    PixelFormat format = PixelFormat::UNKNOWN;
    AllocatorType allocType = AllocatorType::SHARE_MEM_ALLOC;
    uint8_t *buffer = nullptr;
    uint32_t bufferSize = 0;
    uint32_t yStride = 0;
    uint32_t uvStride = 0;
    uint32_t yOffset = 0;
    uint32_t uvOffset = 0;
    void *context = nullptr;
    YUVConvertColorSpaceDetails yuvConvertCSDetails;
};

struct SrcConvertParam {
    uint32_t width = 0;
    uint32_t height = 0;
    AllocatorType allocType = AllocatorType::SHARE_MEM_ALLOC ;
    PixelFormat format = PixelFormat::UNKNOWN;
    const uint8_t *buffer = nullptr;
    uint32_t bufferSize = 0;
    int stride[4] = {0, 0, 0, 0};
    const uint8_t *slice[4] = {nullptr, nullptr, nullptr, nullptr};
};

struct DestConvertParam {
    uint32_t width = 0;
    uint32_t height = 0;
    AllocatorType allocType = AllocatorType::SHARE_MEM_ALLOC;
    PixelFormat format = PixelFormat::UNKNOWN;
    uint8_t *buffer = nullptr;
    uint32_t bufferSize = 0;
    int stride[4] = {0, 0, 0, 0};
    uint8_t *slice[4] = {nullptr, nullptr, nullptr, nullptr};
    YUVConvertColorSpaceDetails yuvConvertCSDetails;
};

struct FillColor {
    bool isValidColor = false;
    uint32_t color = 0;
};

struct SVGResize {
    bool isValidPercentage = false;
    uint32_t resizePercentage = 100;
};

struct SVGDecodeOptions {
    FillColor fillColor;
    FillColor strokeColor;
    SVGResize SVGResize;
};

enum class CropAndScaleStrategy : int32_t {
    DEFAULT = 0,
    /**
     * First scale, then crop.
     */
    SCALE_FIRST = 1,
    /**
     * Perform region decoding first, then scaling.
     */
    CROP_FIRST = 2
};

class PixelMap;
struct DecodeOptions {
    int32_t fitDensity = 0;
    Rect CropRect;
    Size desiredSize;
    Rect desiredRegion;
    float rotateDegrees = 0;
    uint32_t rotateNewDegrees = 0;
    static constexpr uint32_t DEFAULT_SAMPLE_SIZE = 1;
    uint32_t sampleSize = DEFAULT_SAMPLE_SIZE;
    PixelFormat desiredPixelFormat = PixelFormat::UNKNOWN;
    PixelFormat photoDesiredPixelFormat = PixelFormat::UNKNOWN;
#if defined(ANDROID_PLATFORM) || defined(IOS_PLATFORM)
    AllocatorType allocatorType = AllocatorType::HEAP_ALLOC;
#else
    AllocatorType allocatorType = AllocatorType::DEFAULT;
#endif
    ColorSpace desiredColorSpace = ColorSpace::SRGB;
    bool allowPartialImage = true;
    bool editable = false;
    MemoryUsagePreference preference = MemoryUsagePreference::DEFAULT;
    SVGDecodeOptions SVGOpts;
    std::shared_ptr<OHOS::ColorManager::ColorSpace> desiredColorSpaceInfo = nullptr;
    bool preferDma = false;
    bool fastAstc = false;
    uint16_t invokeType = 0;
    DecodeDynamicRange desiredDynamicRange = DecodeDynamicRange::SDR;
    ResolutionQuality resolutionQuality = ResolutionQuality::UNKNOWN;
    bool isAisr = false;
    // CreatePixelMapUsingAllocatorType is true, CreatePixelMap is false.
    bool isAppUseAllocator = false;
    std::shared_ptr<PixelMap> reusePixelmap = nullptr;
    CropAndScaleStrategy cropAndScaleStrategy = CropAndScaleStrategy::DEFAULT;
    bool isCreateWideGamutSdrPixelMap = false;
    bool isAnimationDecode = false;
};

enum class ScaleMode : int32_t {
    FIT_TARGET_SIZE = 0,
    CENTER_CROP = 1,
};

enum class IncrementalMode { FULL_DATA = 0, INCREMENTAL_DATA = 1 };

// used in ScalePixelMapEx
enum class AntiAliasingOption : int32_t {
    NONE = 0, // SWS_POINT_NEAREST
    LOW = 1, // SWS_BILINEAR
    MEDIUM = 2, // SWS_BICUBIC
    HIGH = 3, // SWS_AREA
    FAST_BILINEAER = 4, // SWS_FAST_BILINEAER
    BICUBLIN = 5, // SWS_AREA
    GAUSS = 6, // SWS_GAUSS
    SINC = 7, // SWS_SINC
    LANCZOS = 8, // SWS_LANCZOS
    SPLINE = 9, // SWS_SPLINE
    SLR = 10, // SLR
};

enum class AuxiliaryPictureType {
    NONE = 0,
    GAINMAP = 1,
    DEPTH_MAP = 2,
    UNREFOCUS_MAP = 3,
    LINEAR_MAP = 4,
    FRAGMENT_MAP = 5,
    THUMBNAIL = 10,
};

struct DownSamplingScaleFactor {
    float widthScaleFactor = 1;
    float heightScaleFactor = 1;
};

struct AuxiliaryPictureDecodeInfo {
    DownSamplingScaleFactor downSamplingScaleFactor;
    AuxiliaryPictureType type = AuxiliaryPictureType::NONE;
    std::string imageType = "";
};

struct AuxiliaryPictureInfo {
    AuxiliaryPictureType auxiliaryPictureType = AuxiliaryPictureType::NONE;
    Size size;
    uint32_t rowStride = 0;
    PixelFormat pixelFormat = PixelFormat::UNKNOWN;
    ColorSpace colorSpace = ColorSpace::SRGB;
    std::string jpegTagName = "";
};

enum class MetadataType {
    EXIF = 1,
    FRAGMENT = 2,
    XTSTYLE = 3,
    RFDATAB = 4,
    GIF = 5,
    STDATA = 6,
    RESMAP = 7,
    UNKNOWN = 0,
    HEIFS = 15,
    DNG = 16,
    WEBP = 17,
};

static const std::map<MetadataType, std::string> BLOB_METADATA_TAG_MAP = {
    {MetadataType::XTSTYLE, METADATA_TAG_XTSTYLE},
    {MetadataType::RFDATAB, METADATA_TAG_RFDATAB},
    {MetadataType::RESMAP, METADATA_TAG_RESMAP},
    {MetadataType::STDATA, METADATA_TAG_STDATA},
};

struct DecodingOptionsForPicture {
    std::set<AuxiliaryPictureType> desireAuxiliaryPictures;
    std::set<MetadataType> desiredMetadatas;
    PixelFormat desiredPixelFormat = PixelFormat::RGBA_8888;
    AllocatorType allocatorType = AllocatorType::DMA_ALLOC;
    bool needsDecodeDfxData = false;
    Size desiredSizeForMainPixelMap;
};

struct DecodingOptionsForThumbnail {
    Size desiredSize;
    bool needGenerate = false;
    PixelFormat desiredPixelFormat = PixelFormat::RGBA_8888;
    AllocatorType allocatorType = AllocatorType::DMA_ALLOC;
};

typedef struct PictureError {
    uint32_t errorCode = 0;
    std::string errorInfo = "";
} PICTURE_ERR;

struct MaintenanceData {
    std::shared_ptr<uint8_t[]> data_;
    size_t size_ = 0;
    MaintenanceData(std::shared_ptr<uint8_t[]> data, size_t size) : data_(data), size_(size) {}
};

struct AstcMetadata {
    std::vector<uint8_t> hdrMetadataTypeVec;
    std::vector<uint8_t> colorSpaceInfoVec;
    std::vector<uint8_t> staticData;
    std::vector<uint8_t> dynamicData;
};

struct AstcExtendInfo {
    uint32_t extendBufferSumBytes = 0;
    uint8_t extendNums = ASTC_EXTEND_INFO_TLV_NUM_6;
    uint8_t extendInfoType[ASTC_EXTEND_INFO_TLV_NUM_6];
    uint32_t extendInfoLength[ASTC_EXTEND_INFO_TLV_NUM_6];
    uint8_t *extendInfoValue[ASTC_EXTEND_INFO_TLV_NUM_6];
    AstcMetadata astcMetadata; // metadata for astc
};

enum class AstcExtendInfoType : uint8_t {
    COLOR_SPACE = 0,
    PIXEL_FORMAT = 1,
    HDR_METADATA_TYPE = 2,
    HDR_COLORSPACE_INFO = 3,
    HDR_STATIC_DATA = 4,
    HDR_DYNAMIC_DATA = 5,
};

struct XmageCoordinateMetadata {
    uint32_t left;
    uint32_t top;
    uint32_t right;
    uint32_t bottom;
    uint32_t imageWidth;
    uint32_t imageLength;
};

enum class PropertyValueType : int32_t {
    UNKNOWN = 0,
    STRING = 1,
    INT = 2,
    DOUBLE = 3,
    INT_ARRAY = 4,
    DOUBLE_ARRAY = 5,
    BLOB = 6,
};

struct MetadataValue {
    std::string key = "";
    PropertyValueType type = PropertyValueType::UNKNOWN;
    std::string stringValue = "";
    std::vector<int64_t> intArrayValue;
    std::vector<double> doubleArrayValue;
    std::vector<uint8_t> bufferValue;
};

enum class NapiMetadataType {
    UNKNOWN = 0,
    EXIF_METADATA = 1,
    HWMAKERNOTE_METADATA = 2,
    HEIFS_METADATA = 3,
    FRAGMENT_METADATA = 4,
    GIF_METADATA = 5,
    DNG_METADATA = 6,
    WEBP_METADATA = 7,
};

enum class XMPTagType: int32_t {
    UNKNOWN = 0,
    SIMPLE = 1,
    UNORDERED_ARRAY = 2,
    ORDERED_ARRAY = 3,
    ALTERNATE_ARRAY = 4,
    ALTERNATE_TEXT = 5,
    STRUCTURE = 6,
    QUALIFIER = 7,
};

struct XMPTag {
    std::string xmlns;
    std::string prefix;
    std::string name;
    XMPTagType type;
    std::string value;
};

struct XMPEnumerateOption {
    bool isRecursive = false;
};
} // namespace Media
} // namespace OHOS

#endif // INTERFACES_INNERKITS_INCLUDE_IMAGE_TYPE_H_
