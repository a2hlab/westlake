/* Stand-in for AOSP system/core/libcutils/include/cutils/ashmem.h.
 *
 * This header used to say "OpenHarmony has no ashmem". That was wrong, and the
 * parity27 OH header closure is what showed it: the pinned OHOS native SDK
 * sysroot ships linux/ashmem.h with ASHMEM_NAME_DEF, ASHMEM_SET_NAME and
 * ASHMEM_SET_SIZE, and OH's commonlibrary/c_utils publishes AshmemCreate on top
 * of it.  wl_compat.cpp therefore now implements these for real -- but behind
 * WESTLAKE_OH_ASHMEM=1, because a header proves the interface exists and says
 * nothing about whether the board's kernel enables the driver or lets an app
 * sandbox open the device, and nobody has run it on hardware yet.
 *
 * With the flag unset -- the shipped default -- these still fail immediately,
 * which remains the verified behaviour and the documented path through the
 * callers:
 *
 *   - CursorWindow::maybeInflate() treats a negative fd as "goto heap_fallback"
 *     and grows the window with malloc instead (Westlake §453).  Nothing hands
 *     this window across Binder in our single-process setup, so a heap buffer is
 *     a correct substitute for a shared-memory one.
 *
 *   - nativeExecuteForBlobFileDescriptor() throws IOException, which is the
 *     honest answer: without shared memory there is no fd to hand back.  That
 *     entry point exists for ParcelFileDescriptor-based blob streaming, which
 *     nothing in the start-up path uses.
 *
 * Failing here rather than faking a descriptor keeps the fallback visible in the
 * log instead of producing a window that silently misbehaves later.
 */
#ifndef WL_CUTILS_ASHMEM_H
#define WL_CUTILS_ASHMEM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int ashmem_create_region(const char* name, size_t size);
int ashmem_set_prot_region(int fd, int prot);
int ashmem_pin_region(int fd, size_t offset, size_t len);
int ashmem_unpin_region(int fd, size_t offset, size_t len);
int ashmem_get_size_region(int fd);
int ashmem_valid(int fd);

#ifdef __cplusplus
}
#endif

#endif  // WL_CUTILS_ASHMEM_H
