/* Minimal stand-in for AOSP system/core/libutils/include/utils/Unicode.h.
 *
 * Only the two UTF-8 -> UTF-16 entry points used by CursorWindow's
 * copyStringToBuffer path are declared.  Implementations live in wl_compat.cpp.
 */
#ifndef WL_UTILS_UNICODE_H
#define WL_UTILS_UNICODE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <uchar.h>

namespace android {

/* Number of UTF-16 code units needed for the UTF-8 range, or -1 if malformed. */
ssize_t utf8_to_utf16_length(const uint8_t* src, size_t srcLen);

/* Converts without appending a NUL.  dstLen is the capacity in code units. */
char16_t* utf8_to_utf16_no_null_terminator(const uint8_t* src, size_t srcLen,
                                           char16_t* dst, size_t dstLen);

size_t strlen16(const char16_t* s);

}  // namespace android

#endif  // WL_UTILS_UNICODE_H
