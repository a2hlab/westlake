/* Stand-in for AOSP frameworks/base/core/jni/android_os_Parcel.h.
 *
 * Included by android_database_CursorWindow.cpp.  parcelForJavaObject is
 * declared for completeness but is never called: the two parcel entry points in
 * that file set `Parcel* parcel = nullptr` outright, because unwrapping a Java
 * Parcel needs the Binder JNI layer this port does not have.  Declaring it
 * without defining it is intentional -- if a future edit starts calling it, the
 * link fails loudly instead of silently binding to something that cannot work.
 */
#ifndef WL_ANDROID_OS_PARCEL_H
#define WL_ANDROID_OS_PARCEL_H

#include <jni.h>

#include "binder/Parcel.h"

namespace android {

Parcel* parcelForJavaObject(JNIEnv* env, jobject obj);

}  // namespace android

#endif  // WL_ANDROID_OS_PARCEL_H
