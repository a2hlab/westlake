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

#ifndef INTERFACES_INNERKITS_INCLUDE_PIXEL_MAP_PARCEL_H
#define INTERFACES_INNERKITS_INCLUDE_PIXEL_MAP_PARCEL_H

#include "message_parcel.h"
#include "image_type.h"

namespace OHOS::Rosen {
    class PixelMapStorage;
    class RSProfiler;
};

namespace OHOS {
namespace Media {
class PixelMap;
class PixelMapParcel {
public:
    static bool WriteToParcel(PixelMap* pixelMap, OHOS::MessageParcel& data);
    static bool WriteImageInfo(PixelMap* pixelMap, OHOS::MessageParcel& data);
};

struct PixelMemInfo;
struct PixelMapError;
typedef struct PixelMapError PIXEL_MAP_ERR;
class PixelMapRecordParcel {
public:
    static constexpr size_t MAX_IMAGEDATA_SIZE = 128 * 1024 * 1024; // 128M
    static constexpr size_t MIN_IMAGEDATA_SIZE = 32 * 1024;         // 32k

    struct ParcelInfo {
        YUVDataInfo yuvDataInfo_;
        ImageInfo imageInfo_;
        Size astcrealSize_;
        AllocatorType allocatorType_;
#ifdef IMAGE_COLORSPACE_FLAG
        std::shared_ptr<OHOS::ColorManager::ColorSpace> grColorSpace_ = nullptr;
#else
        std::shared_ptr<uint8_t> grColorSpace_ = nullptr;
#endif
        uint32_t versionId_ = 1;
        uint8_t *data_ = nullptr;
        int32_t rowDataSize_ = 0;
        int32_t rowStride_ = 0;
        int32_t pixelBytes_ = 0;
        uint32_t uniqueId_ = 0;
        uint32_t pixelsSize_ = 0;
        void *context_ = nullptr;
        bool isUnMap_ = false;
        mutable bool isMemoryDirty_ = false;
        bool editable_ = false;
        bool isAstc_ = false;
        bool displayOnly_ = false;
    };
    using ParcelInfo = struct ParcelInfo;

    static bool MarshallingPixelMapForRecord(Parcel& parcel, PixelMap& pixelmap);
    static PixelMap *UnmarshallingPixelMapForRecord(Parcel &parcel,
        std::function<int(Parcel &parcel, std::function<int(Parcel&)> readFdDefaultFunc)> readSafeFdFunc = nullptr);

    bool Marshalling(Parcel &parcel, PixelMap& pixelmap);
    static bool IsYUV(const PixelFormat &format);
    ParcelInfo& GetParcelInfo() {
        return parcelInfo_;
    }

private:
    friend class OHOS::Rosen::PixelMapStorage;
    friend class OHOS::Rosen::RSProfiler;
    bool WriteMemInfoToParcel(Parcel &parcel, const int32_t &bufferSize);
    bool WritePropertiesToParcel(Parcel &parcel);
    bool WriteImageInfo(Parcel &parcel);
    bool WriteAstcRealSizeToParcel(Parcel &parcel);
    bool WriteImageData(Parcel &parcel, size_t size);
    bool WriteAshmemDataToParcel(Parcel &parcel, size_t size);
    bool WriteFileDescriptor(Parcel &parcel, int fd);
    bool WriteTransformDataToParcel(Parcel &parcel) const;
    bool WriteYuvDataInfoToParcel(Parcel &parcel) const;
    static bool IsYuvFormat(PixelFormat format);
    static PixelMap *Unmarshalling(Parcel &parcel, PIXEL_MAP_ERR &error,
        std::function<int(Parcel &parcel, std::function<int(Parcel&)> readFdDefaultFunc)> readSafeFdFunc);
    static bool ReadPropertiesFromParcel(Parcel& parcel, PixelMap*& pixelMap,
        ImageInfo& imgInfo, PixelMemInfo& memInfo);
    static PixelMap *StartUnmarshalling(Parcel &parcel, ImageInfo &imgInfo,
        PixelMemInfo& pixelMemInfo, PIXEL_MAP_ERR &error);
    static PixelMap *FinishUnmarshalling(PixelMap *pixelMap, Parcel &parcel,
        ImageInfo &imgInfo, PixelMemInfo &pixelMemInfo, PIXEL_MAP_ERR &error);
    static bool UpdatePixelMapMemInfo(PixelMap *pixelMap, ImageInfo &imgInfo, PixelMemInfo &pixelMemInfo);
    static void ReleaseMemory(AllocatorType allocType, void *addr, void *context, uint32_t size);
    static bool ReadMemInfoFromParcel(Parcel &parcel, PixelMemInfo &pixelMemInfo, PIXEL_MAP_ERR &error,
        std::function<int(Parcel &parcel, std::function<int(Parcel&)> readFdDefaultFunc)> readSafeFdFunc);
    static bool ReadDmaMemInfoFromParcel(Parcel &parcel, PixelMemInfo &pixelMemInfo,
        std::function<int(Parcel &parcel, std::function<int(Parcel&)> readFdDefaultFunc)> readSafeFdFunc);
    static uint8_t *ReadImageData(Parcel &parcel, int32_t bufferSize,
        std::function<int(Parcel &parcel, std::function<int(Parcel&)> readFdDefaultFunc)> readSafeFdFunc);
    static uint8_t *ReadHeapDataFromParcel(Parcel &parcel, int32_t bufferSize);
    static uint8_t *ReadAshmemDataFromParcel(Parcel &parcel, int32_t bufferSize,
        std::function<int(Parcel &parcel, std::function<int(Parcel&)> readFdDefaultFunc)> readSafeFdFunc);
    static int ReadFileDescriptor(Parcel &parcel);
    static bool ReadTransformData(Parcel &parcel, PixelMap *pixelMap);
    static bool ReadYuvDataInfoFromParcel(Parcel &parcel, PixelMap *pixelMap);

    ParcelInfo parcelInfo_;
};
}  // namespace Media
}  // namespace OHOS

#endif // INTERFACES_INNERKITS_INCLUDE_PIXEL_MAP_PARCEL_H
