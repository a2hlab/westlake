// Compare source Android buffer descriptors and CPU access with the OH host.
#include <stdbool.h>
#include <android/hardware_buffer.h>
#include <native_buffer/native_buffer.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "SOURCE_BUFFER_FAIL line=%d: %s\n", __LINE__, #condition); return 1; } } while (0)

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        void* library = dlopen(argv[i], RTLD_NOW | RTLD_GLOBAL);
        if (!library) { fprintf(stderr, "SOURCE_NATIVE_LOAD_FAILED %s: %s\n", argv[i], dlerror()); return 1; }
    }
    int (*allocate)(const AHardwareBuffer_Desc*, AHardwareBuffer**) = dlsym(RTLD_DEFAULT, "AHardwareBuffer_allocate");
    void (*describe)(const AHardwareBuffer*, AHardwareBuffer_Desc*) = dlsym(RTLD_DEFAULT, "AHardwareBuffer_describe");
    void (*acquire)(AHardwareBuffer*) = dlsym(RTLD_DEFAULT, "AHardwareBuffer_acquire");
    void (*release)(AHardwareBuffer*) = dlsym(RTLD_DEFAULT, "AHardwareBuffer_release");
    int (*lock)(AHardwareBuffer*, uint64_t, int32_t, const ARect*, void**) = dlsym(RTLD_DEFAULT, "AHardwareBuffer_lock");
    int (*unlock)(AHardwareBuffer*, int32_t*) = dlsym(RTLD_DEFAULT, "AHardwareBuffer_unlock");
    void (*getConfig)(OH_NativeBuffer*, OH_NativeBuffer_Config*) = dlsym(RTLD_DEFAULT, "OH_NativeBuffer_GetConfig");
    OH_NativeBuffer* (*hostAllocate)(const OH_NativeBuffer_Config*) = dlsym(RTLD_DEFAULT, "OH_NativeBuffer_Alloc");
    int32_t (*hostRelease)(OH_NativeBuffer*) = dlsym(RTLD_DEFAULT, "OH_NativeBuffer_Unreference");
    REQUIRE(allocate && describe && acquire && release && lock && unlock && getConfig && hostAllocate && hostRelease);
    const struct { uint32_t android; int32_t host; uint32_t bytes; } formats[] = {
        {AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM, NATIVEBUFFER_PIXEL_FMT_RGBA_8888, 4},
        {AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM, NATIVEBUFFER_PIXEL_FMT_RGB_565, 2},
    };
    for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        const char* selected = getenv("A2H_BUFFER_FORMAT");
        if (selected && atoi(selected) != (int)formats[i].android) continue;
        fprintf(stderr, "SOURCE_BUFFER_CONTROL_ALLOCATING android_format=%u host_format=%d\n", formats[i].android, formats[i].host);
        AHardwareBuffer_Desc request = {.width = 7, .height = 5, .layers = 1,
            .format = formats[i].android, .usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN};
        AHardwareBuffer* buffer = NULL;
        OH_NativeBuffer_Config controlConfig = {.width=7, .height=5, .format=formats[i].host,
            .usage=NATIVEBUFFER_USAGE_CPU_READ | NATIVEBUFFER_USAGE_CPU_WRITE |
                   NATIVEBUFFER_USAGE_CPU_READ_OFTEN | NATIVEBUFFER_USAGE_MEM_DMA};
        OH_NativeBuffer* control = hostAllocate(&controlConfig);
        if (!control) {
            REQUIRE(allocate(&request, &buffer) != 0 && !buffer);
            printf("SOURCE_BUFFER_HOST_UNSUPPORTED android_format=%u host_format=%d\n",
                   formats[i].android, formats[i].host);
            REQUIRE(i != 0); // RGBA8888 is required by this source renderer profile.
            continue;
        }
        hostRelease(control);
        REQUIRE(allocate(&request, &buffer) == 0 && buffer);
        acquire(buffer); release(buffer); // the caller's original reference remains alive
        AHardwareBuffer_Desc actual = {0}; describe(buffer, &actual);
        OH_NativeBuffer_Config host = {0}; getConfig((OH_NativeBuffer*)buffer, &host);
        REQUIRE(actual.width == request.width && actual.height == request.height && actual.layers == 1);
        REQUIRE(actual.format == formats[i].android && host.format == formats[i].host);
        REQUIRE(actual.stride >= actual.width && actual.stride * formats[i].bytes == host.stride);
        REQUIRE((actual.usage & request.usage) == request.usage);
        void* address = NULL;
        REQUIRE(lock(buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, NULL, &address) == 0 && address);
        size_t length = actual.stride * actual.height * formats[i].bytes;
        memset(address, 0x5a, length);
        int fence = 999;
        REQUIRE(unlock(buffer, &fence) == 0 && fence == -1);
        REQUIRE(lock(buffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, NULL, &address) == 0 && address);
        for (size_t b = 0; b < length; ++b) REQUIRE(((unsigned char*)address)[b] == 0x5a);
        REQUIRE(unlock(buffer, NULL) == 0);
        release(buffer);
        printf("SOURCE_BUFFER_PASS android_format=%u host_format=%d stride_pixels=%u stride_bytes=%d\n",
               actual.format, host.format, actual.stride, host.stride);
    }
    AHardwareBuffer_Desc invalid = {.width=4, .height=4, .layers=2,
        .format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM};
    AHardwareBuffer* output = (AHardwareBuffer*)(uintptr_t)1;
    REQUIRE(allocate(&invalid, &output) != 0 && !output);
    puts("SOURCE_GRAPHICS_BUFFERS_PASS");
    return 0;
}
