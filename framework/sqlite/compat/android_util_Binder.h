/* Stand-in for AOSP frameworks/base/core/jni/android_util_Binder.h.
 *
 * Included by android_database_CursorWindow.cpp but nothing from it is used --
 * the file needs Binder only for the parcel paths, which are stubbed out at
 * source.  Kept as an empty header so the include resolves.
 */
#ifndef WL_ANDROID_UTIL_BINDER_H
#define WL_ANDROID_UTIL_BINDER_H

#include <jni.h>

#endif  // WL_ANDROID_UTIL_BINDER_H
