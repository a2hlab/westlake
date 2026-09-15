// Android display discovery backed by the OH host display manager.
#include <apex/display.h>
#include <window_manager/oh_display_manager.h>
#include <surface_type.h>

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <new>

namespace android {
struct ADisplay {
    uint32_t id;
    bool primary;
    bool displayP3;
};

int ADisplay_acquirePhysicalDisplays(ADisplay*** output) {
    if (!output) return -EINVAL;
    *output = nullptr;
    NativeDisplayManager_DisplaysInfo* host = nullptr;
    uint64_t primary = 0;
    if (OH_NativeDisplayManager_GetDefaultDisplayId(&primary) != DISPLAY_MANAGER_OK)
        return -ENODEV;
    if (OH_NativeDisplayManager_CreateAllDisplays(&host) != DISPLAY_MANAGER_OK || !host)
        return -EIO;
    if (host->displaysLength > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        OH_NativeDisplayManager_DestroyAllDisplays(host);
        return -EOVERFLOW;
    }
    auto** displays = static_cast<ADisplay**>(
            calloc(static_cast<size_t>(host->displaysLength) + 1, sizeof(ADisplay*)));
    if (!displays) {
        OH_NativeDisplayManager_DestroyAllDisplays(host);
        return -ENOMEM;
    }
    int count = 0;
    for (uint32_t i = 0; i < host->displaysLength; ++i) {
        const auto& info = host->displaysInfo[i];
        if (!info.isAlive) continue;
        bool p3 = false;
        if (info.colorSpace) {
            for (uint32_t j = 0; j < info.colorSpace->colorSpaceLength; ++j)
                p3 |= info.colorSpace->colorSpaces[j] == OHOS::GRAPHIC_CM_DISPLAY_P3_SRGB;
        }
        displays[count] = new (std::nothrow) ADisplay{info.id, info.id == primary, p3};
        if (!displays[count]) {
            ADisplay_release(displays);
            OH_NativeDisplayManager_DestroyAllDisplays(host);
            return -ENOMEM;
        }
        ++count;
    }
    OH_NativeDisplayManager_DestroyAllDisplays(host);
    *output = displays;
    return count;
}

void ADisplay_release(ADisplay** displays) {
    if (!displays) return;
    for (auto** p = displays; *p; ++p) delete *p;
    free(displays);
}

ADisplayType ADisplay_getDisplayType(ADisplay* display) {
    // Android HWUI's primary rendering target is the host's default display.
    return display->primary ? DISPLAY_TYPE_INTERNAL : DISPLAY_TYPE_EXTERNAL;
}

void ADisplay_getPreferredWideColorFormat(ADisplay* display, ADataSpace* dataspace,
                                         AHardwareBuffer_Format* format) {
    *dataspace = display->displayP3 ? ADATASPACE_DISPLAY_P3 : ADATASPACE_SRGB;
    *format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
}
} // namespace android
