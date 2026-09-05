/* Minimal stand-in for AOSP system/core/libutils/include/utils/Errors.h.
 *
 * The upstream android_database sources were written against the AOSP header
 * tree, which does not exist here -- the drop supplied the .cpp files only.  Only
 * the handful of status codes actually returned by CursorWindow are needed, and
 * their exact values do not matter to any caller: the JNI layer tests them for
 * non-zero and reports the number in an exception message.  They are kept equal
 * to the AOSP values anyway so a log line reading "error -12" means the same
 * thing it would on a real device.
 */
#ifndef WL_UTILS_ERRORS_H
#define WL_UTILS_ERRORS_H

#include <errno.h>
#include <stdint.h>

namespace android {

typedef int32_t status_t;

enum {
    OK                = 0,
    NO_ERROR          = 0,
    UNKNOWN_ERROR     = (-2147483647 - 1),   // INT32_MIN
    NO_MEMORY         = -ENOMEM,
    INVALID_OPERATION = -ENOSYS,
    BAD_VALUE         = -EINVAL,
    BAD_INDEX         = -EOVERFLOW,
    NAME_NOT_FOUND    = -ENOENT,
    PERMISSION_DENIED = -EPERM,
    NO_INIT           = -ENODEV,
    ALREADY_EXISTS    = -EEXIST,
    DEAD_OBJECT       = -EPIPE,
    BAD_TYPE          = (-2147483647 - 1) + 1,
    NOT_ENOUGH_DATA   = -ENODATA,
    WOULD_BLOCK       = -EWOULDBLOCK,
    TIMED_OUT         = -ETIMEDOUT,
    UNKNOWN_TRANSACTION = -EBADMSG,
    FDS_NOT_ALLOWED   = (-2147483647 - 1) + 7,
};

}  // namespace android

#endif  // WL_UTILS_ERRORS_H
