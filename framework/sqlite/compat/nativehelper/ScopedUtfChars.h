/* Minimal stand-in for AOSP libnativehelper/include_jni/nativehelper/ScopedUtfChars.h.
 *
 * Same reasoning as ScopedLocalRef: the release on scope exit is load-bearing,
 * because GetStringUTFChars pins a copy that would otherwise leak once per call.
 *
 * A null jstring raises NullPointerException and leaves c_str() null, matching
 * the AOSP contract the call sites are written against -- they check c_str() for
 * null and return early.
 */
#ifndef WL_NATIVEHELPER_SCOPED_UTF_CHARS_H
#define WL_NATIVEHELPER_SCOPED_UTF_CHARS_H

#include <jni.h>
#include <string.h>

#include "nativehelper/JNIHelp.h"

class ScopedUtfChars {
public:
    ScopedUtfChars(JNIEnv* env, jstring s) : mEnv(env), mString(s), mUtfChars(nullptr) {
        if (s == nullptr) {
            jniThrowNullPointerException(env, nullptr);
        } else {
            mUtfChars = env->GetStringUTFChars(s, nullptr);
        }
    }

    ~ScopedUtfChars() {
        if (mUtfChars != nullptr) {
            mEnv->ReleaseStringUTFChars(mString, mUtfChars);
        }
    }

    const char* c_str() const { return mUtfChars; }
    size_t size() const { return mUtfChars != nullptr ? strlen(mUtfChars) : 0; }
    const char& operator[](size_t n) const { return mUtfChars[n]; }

    ScopedUtfChars(const ScopedUtfChars&) = delete;
    void operator=(const ScopedUtfChars&) = delete;

private:
    JNIEnv* mEnv;
    jstring mString;
    const char* mUtfChars;
};

#endif  // WL_NATIVEHELPER_SCOPED_UTF_CHARS_H
