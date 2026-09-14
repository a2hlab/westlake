#pragma once

// Use the pinned Android 15 enum declarations, not the historical integer
// macros that collided with newer data_space.h and bitmap.h headers.
#include <android/data_space.h>
#include <android/bitmap.h>
#include <EGL/egl.h>
#if WESTLAKE_ANDROID_API >= 35
#include <malloc.h>
#include <android/rect.h>
#include <linux/stat.h>
// The OHOS libc exports statx; this SDK predates its public declaration.
#ifdef __cplusplus
extern "C" {
#endif
int statx(int dirfd, const char* path, int flags, unsigned int mask, struct statx* result);
#ifdef __cplusplus
}
#endif
// Android surface-control visibility values at the OH compositor boundary.
#define ASURFACE_TRANSACTION_VISIBILITY_SHOW 1
#define ASURFACE_TRANSACTION_VISIBILITY_HIDE 0
#endif
#ifdef __cplusplus
#include <apex/choreographer.h>
#endif
