/* Minimal stand-in for AOSP frameworks/base/core/jni/core_jni_helpers.h.
 *
 * The "OrDie" helpers abort the process when a lookup fails.  That behaviour is
 * kept rather than softened: every one of them is called from a register_ entry
 * point during JNI_OnLoad, looking up classes and fields that the boot framework
 * is required to declare (java/util/function/UnaryOperator,
 * android/database/CharArrayBuffer).  If one is genuinely missing, failing loudly
 * at load time with the name in the message is far easier to diagnose than a
 * null jmethodID surfacing as a crash during the first database query.
 *
 * The one call that must NOT die is the SQLiteConnection method table, and the
 * supplied source already handles that itself: it walks sMethods and registers
 * each method the framework actually declares, skipping the rest, instead of
 * going through RegisterMethodsOrDie.  That matters here because this board's
 * framework.jar omits four entries the table carries (nativeUpdatesTempOnly,
 * nativeLastInsertRowId, nativeChanges, nativeTotalChanges).
 */
#ifndef WL_CORE_JNI_HELPERS_H
#define WL_CORE_JNI_HELPERS_H

#include <jni.h>

#include "nativehelper/JNIHelp.h"
/* AOSP's core_jni_helpers.h pulls the scoped JNI wrappers in transitively, and
 * android_database_SQLiteConnection.cpp depends on that -- it uses
 * ScopedLocalRef and ScopedUtfChars in the custom-function trampolines without
 * including either header itself. */
#include "nativehelper/ScopedLocalRef.h"
#include "nativehelper/ScopedUtfChars.h"
#include "utils/Log.h"

#ifndef NELEM
#define NELEM(x) ((int) (sizeof(x) / sizeof((x)[0])))
#endif

namespace android {

jclass    FindClassOrDie(JNIEnv* env, const char* className);
jfieldID  GetFieldIDOrDie(JNIEnv* env, jclass clazz, const char* name, const char* sig);
jmethodID GetMethodIDOrDie(JNIEnv* env, jclass clazz, const char* name, const char* sig);
jfieldID  GetStaticFieldIDOrDie(JNIEnv* env, jclass clazz, const char* name, const char* sig);
jmethodID GetStaticMethodIDOrDie(JNIEnv* env, jclass clazz, const char* name, const char* sig);

template <typename T>
T MakeGlobalRefOrDie(JNIEnv* env, T in) {
    jobject res = env->NewGlobalRef(in);
    if (res == nullptr) {
        ALOGE("Failed to create global reference");
        abort();
    }
    return static_cast<T>(res);
}

int RegisterMethodsOrDie(JNIEnv* env, const char* className,
                         const JNINativeMethod* methods, int numMethods);

}  // namespace android

#endif  // WL_CORE_JNI_HELPERS_H
