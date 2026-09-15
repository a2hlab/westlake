#pragma once

#include <android/hardware_buffer.h>
#include <buffer_handle.h>
#include <stdint.h>

struct OH_NativeBuffer;
extern "C" const BufferHandle* OH_NativeBuffer_GetBufferHandle(const OH_NativeBuffer* buffer);

// The OH AHardwareBuffer adapter owns an OH_NativeBuffer. Its allocator reports
// the actual byte allocation in BufferHandle; Android's GraphicBuffer mapper
// cannot inspect this host object.
static inline uint64_t westlakeHardwareBufferAllocationSize(const AHardwareBuffer* buffer) {
    const BufferHandle* handle = OH_NativeBuffer_GetBufferHandle(
            reinterpret_cast<const OH_NativeBuffer*>(buffer));
    return handle && handle->size > 0 ? static_cast<uint64_t>(handle->size) : 0;
}
