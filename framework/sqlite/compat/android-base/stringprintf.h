/* Minimal stand-in for AOSP system/libbase/include/android-base/stringprintf.h.
 *
 * Pulled in by androidfw/CursorWindow.h for its inline toString(), which is the
 * only caller.
 */
#ifndef WL_ANDROID_BASE_STRINGPRINTF_H
#define WL_ANDROID_BASE_STRINGPRINTF_H

#include <string>

namespace android {
namespace base {

std::string StringPrintf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace base
}  // namespace android

#endif  // WL_ANDROID_BASE_STRINGPRINTF_H
