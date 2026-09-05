/* Minimal stand-in for AOSP libnativehelper/include_jni/nativehelper/ScopedLocalRef.h.
 *
 * Used by the custom scalar/aggregate function trampolines in
 * android_database_SQLiteConnection.cpp, which call back into Java from inside a
 * sqlite callback.  The RAII behaviour is the point, not a convenience: those
 * trampolines can be invoked once per row, and a leaked local reference per row
 * would exhaust the local reference table partway through a large query and
 * abort the process.  Deleting on scope exit is therefore reproduced exactly.
 */
#ifndef WL_NATIVEHELPER_SCOPED_LOCAL_REF_H
#define WL_NATIVEHELPER_SCOPED_LOCAL_REF_H

#include <jni.h>

#include <cstddef>

template <typename T>
class ScopedLocalRef {
public:
    ScopedLocalRef(JNIEnv* env, T localRef) : mEnv(env), mLocalRef(localRef) {}
    explicit ScopedLocalRef(JNIEnv* env) : mEnv(env), mLocalRef(nullptr) {}

    ~ScopedLocalRef() { reset(); }

    void reset(T ptr = nullptr) {
        if (ptr != mLocalRef) {
            if (mLocalRef != nullptr) {
                mEnv->DeleteLocalRef(mLocalRef);
            }
            mLocalRef = ptr;
        }
    }

    T release() __attribute__((warn_unused_result)) {
        T localRef = mLocalRef;
        mLocalRef = nullptr;
        return localRef;
    }

    T get() const { return mLocalRef; }

    ScopedLocalRef(const ScopedLocalRef&) = delete;
    void operator=(const ScopedLocalRef&) = delete;

private:
    JNIEnv* const mEnv;
    T mLocalRef;
};

#endif  // WL_NATIVEHELPER_SCOPED_LOCAL_REF_H
