#pragma once
#ifndef OHOS_PLATFORM
#error OH graphics C++ interfaces require the firmware OHOS_PLATFORM RefBase ABI
#endif
// Load the pinned OH 6 source declaration before any SDK NDK header. The
// toolchain SDK carries an older declaration with an extra shared-memory key;
// both headers use INCLUDE_BUFFER_HANDLE_H, so include order otherwise silently
// changes the ABI. This profile targets the separately hash-checked OH firmware.
#include <buffer_handle.h>
#include <cstddef>
static_assert(sizeof(void*) == 8);
static_assert(offsetof(BufferHandle, phyAddr) == 40);
static_assert(offsetof(BufferHandle, reserveFds) == 48);
static_assert(offsetof(BufferHandle, reserveInts) == 52);
static_assert(offsetof(BufferHandle, reserve) == 56);
static_assert(sizeof(BufferHandle) == 56);
