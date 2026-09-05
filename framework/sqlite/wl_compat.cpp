/* Implementations behind compat/ -- the AOSP support layer the database sources
 * expect but that does not exist on this adapter.
 *
 * The drop in westlake_upstream/ supplies AOSP's android_database_*.cpp, which
 * were written against the full AOSP header tree (libutils, libcutils,
 * libnativehelper, libbase, libbinder).  None of that is available here, so this
 * file provides the small slice they actually touch: two string classes, a UTF-8
 * to UTF-16 converter, the JNI exception throwers, the "OrDie" registration
 * helpers, a JNIEnv accessor, and deliberate failures for ashmem and Parcel.
 *
 * Nothing here is a reimplementation of AOSP for its own sake -- every function
 * exists because a specific call site in those two files needs it.
 */

#include <jni.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "android-base/logging.h"
#include "android-base/stringprintf.h"
#include "android_runtime/AndroidRuntime.h"
#include "binder/Parcel.h"
#include "core_jni_helpers.h"
#include "cutils/ashmem.h"
#include "nativehelper/JNIHelp.h"
#include "utils/String16.h"
#include "utils/Unicode.h"

// ---------------------------------------------------------------------------
// UTF-8 -> UTF-16
//
// Two callers exist and they run as a pair: fillCharArrayBufferUTF() first asks
// utf8_to_utf16_length() how many code units the string needs, allocates a
// jcharArray of exactly that size, then calls
// utf8_to_utf16_no_null_terminator() to fill it.  If the two passes ever
// disagreed about a byte sequence, the second would write past the array the
// first sized -- a heap overflow driven by whatever text the feed happens to
// contain.
//
// They are therefore both built on decodeUtf8(), one decoder used twice, and it
// never fails: a malformed sequence yields U+FFFD and consumes one byte.  That
// makes the length pass exactly predictive of the conversion pass by
// construction rather than by matching two separate implementations.  AOSP
// instead returns -1 from the length pass on malformed input, which the caller
// turns into an empty string; substituting U+FFFD keeps the rest of an article
// title readable, and cannot desynchronise the two passes.
// ---------------------------------------------------------------------------

namespace {

/* Decodes one codepoint from src[0..len). Always advances at least one byte. */
struct Decoded {
    char32_t cp;
    size_t consumed;
};

inline bool isCont(uint8_t b) { return (b & 0xC0) == 0x80; }

Decoded decodeUtf8(const uint8_t* src, size_t len) {
    const uint8_t b0 = src[0];
    if (b0 < 0x80) {
        return {b0, 1};
    }
    if ((b0 & 0xE0) == 0xC0 && len >= 2 && isCont(src[1])) {
        char32_t cp = ((b0 & 0x1Fu) << 6) | (src[1] & 0x3Fu);
        if (cp >= 0x80) return {cp, 2};                    // reject overlong
    } else if ((b0 & 0xF0) == 0xE0 && len >= 3 && isCont(src[1]) && isCont(src[2])) {
        char32_t cp = ((b0 & 0x0Fu) << 12) | ((src[1] & 0x3Fu) << 6) | (src[2] & 0x3Fu);
        // Surrogate halves are not valid scalar values in UTF-8.
        if (cp >= 0x800 && !(cp >= 0xD800 && cp <= 0xDFFF)) return {cp, 3};
    } else if ((b0 & 0xF8) == 0xF0 && len >= 4 && isCont(src[1]) && isCont(src[2])
               && isCont(src[3])) {
        char32_t cp = ((b0 & 0x07u) << 18) | ((src[1] & 0x3Fu) << 12)
                    | ((src[2] & 0x3Fu) << 6) | (src[3] & 0x3Fu);
        if (cp >= 0x10000 && cp <= 0x10FFFF) return {cp, 4};
    }
    return {0xFFFD, 1};
}

inline size_t unitsFor(char32_t cp) { return cp >= 0x10000 ? 2 : 1; }

}  // namespace

namespace android {

ssize_t utf8_to_utf16_length(const uint8_t* src, size_t srcLen) {
    if (src == nullptr) return 0;
    size_t units = 0;
    for (size_t i = 0; i < srcLen;) {
        Decoded d = decodeUtf8(src + i, srcLen - i);
        units += unitsFor(d.cp);
        i += d.consumed;
    }
    return static_cast<ssize_t>(units);
}

char16_t* utf8_to_utf16_no_null_terminator(const uint8_t* src, size_t srcLen,
                                           char16_t* dst, size_t dstLen) {
    if (src == nullptr || dst == nullptr) return dst;
    size_t out = 0;
    for (size_t i = 0; i < srcLen && out < dstLen;) {
        Decoded d = decodeUtf8(src + i, srcLen - i);
        i += d.consumed;
        if (d.cp >= 0x10000) {
            if (out + 2 > dstLen) break;   // no room for the pair; stop cleanly
            char32_t v = d.cp - 0x10000;
            dst[out++] = static_cast<char16_t>(0xD800 + (v >> 10));
            dst[out++] = static_cast<char16_t>(0xDC00 + (v & 0x3FF));
        } else {
            dst[out++] = static_cast<char16_t>(d.cp);
        }
    }
    return dst + out;
}

size_t strlen16(const char16_t* s) {
    if (s == nullptr) return 0;
    size_t n = 0;
    while (s[n] != 0) ++n;
    return n;
}

String16::String16(const char* utf8, size_t len) {
    if (utf8 == nullptr || len == 0) return;
    const uint8_t* src = reinterpret_cast<const uint8_t*>(utf8);
    ssize_t units = utf8_to_utf16_length(src, len);
    if (units <= 0) return;
    mStr.resize(static_cast<size_t>(units));
    utf8_to_utf16_no_null_terminator(src, len, &mStr[0], mStr.size());
}

// ---------------------------------------------------------------------------
// libbase
// ---------------------------------------------------------------------------

namespace base {

LogMessage::~LogMessage() {
    std::string msg = mStream.str();
    if (mWithErrno) {
        msg += ": ";
        msg += strerror(mErrno);
    }
    fprintf(stderr, "[%s/%s] %s\n", mSeverity, LOG_TAG, msg.c_str());
    fflush(stderr);
}

std::string StringPrintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    if (n < 0) return std::string();
    std::string out(static_cast<size_t>(n) + 1, '\0');
    va_start(ap, fmt);
    vsnprintf(&out[0], out.size(), fmt, ap);
    va_end(ap);
    out.resize(static_cast<size_t>(n));
    return out;
}

}  // namespace base

// ---------------------------------------------------------------------------
// ashmem -- absent on OpenHarmony; see compat/cutils/ashmem.h for why failing
// here is the designed path rather than a limitation.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Parcel -- never reached with a real object; see compat/binder/Parcel.h.
// Every method returns an error without touching `this`, so the null pointer the
// JNI layer passes is inert.
// ---------------------------------------------------------------------------

status_t Parcel::readString8(String8*) const        { return UNKNOWN_ERROR; }
status_t Parcel::readUint32(uint32_t*) const        { return UNKNOWN_ERROR; }
status_t Parcel::readBool(bool*) const              { return UNKNOWN_ERROR; }
status_t Parcel::read(void*, size_t) const          { return UNKNOWN_ERROR; }
int      Parcel::readFileDescriptor() const         { return -1; }
status_t Parcel::writeString8(const String8&)       { return UNKNOWN_ERROR; }
status_t Parcel::writeUint32(uint32_t)              { return UNKNOWN_ERROR; }
status_t Parcel::writeBool(bool)                    { return UNKNOWN_ERROR; }
status_t Parcel::writeDupFileDescriptor(int)        { return UNKNOWN_ERROR; }
void*    Parcel::writeInplace(size_t)               { return nullptr; }

// ---------------------------------------------------------------------------
// JNIEnv access
// ---------------------------------------------------------------------------

static JavaVM* gJavaVM = nullptr;

void AndroidRuntime::setJavaVM(JavaVM* vm) { gJavaVM = vm; }
JavaVM* AndroidRuntime::getJavaVM() { return gJavaVM; }

JNIEnv* AndroidRuntime::getJNIEnv() {
    if (gJavaVM == nullptr) return nullptr;
    JNIEnv* env = nullptr;
    if (gJavaVM->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        // Not attached.  Callers (the sqlite trace/profile/progress hooks) all
        // null-check, and attaching here would leave a thread attached for the
        // life of the process with nobody to detach it.
        return nullptr;
    }
    return env;
}

// ---------------------------------------------------------------------------
// core_jni_helpers
// ---------------------------------------------------------------------------

jclass FindClassOrDie(JNIEnv* env, const char* className) {
    jclass clazz = env->FindClass(className);
    if (clazz == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        ALOGE("Unable to find class %s", className);
        abort();
    }
    return clazz;
}

jfieldID GetFieldIDOrDie(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    jfieldID id = env->GetFieldID(clazz, name, sig);
    if (id == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        ALOGE("Unable to find field %s with signature %s", name, sig);
        abort();
    }
    return id;
}

jmethodID GetMethodIDOrDie(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    jmethodID id = env->GetMethodID(clazz, name, sig);
    if (id == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        ALOGE("Unable to find method %s with signature %s", name, sig);
        abort();
    }
    return id;
}

jfieldID GetStaticFieldIDOrDie(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    jfieldID id = env->GetStaticFieldID(clazz, name, sig);
    if (id == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        ALOGE("Unable to find static field %s with signature %s", name, sig);
        abort();
    }
    return id;
}

jmethodID GetStaticMethodIDOrDie(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    jmethodID id = env->GetStaticMethodID(clazz, name, sig);
    if (id == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        ALOGE("Unable to find static method %s with signature %s", name, sig);
        abort();
    }
    return id;
}

int RegisterMethodsOrDie(JNIEnv* env, const char* className,
                         const JNINativeMethod* methods, int numMethods) {
    int res = jniRegisterNativeMethods(env, className, methods, numMethods);
    if (res < 0) {
        ALOGE("Unable to register native methods for %s", className);
        abort();
    }
    return res;
}

}  // namespace android

// ---------------------------------------------------------------------------
// JNIHelp -- C linkage, matching libnativehelper.
// ---------------------------------------------------------------------------

extern "C" {

int ashmem_create_region(const char*, size_t) { errno = ENOSYS; return -1; }
int ashmem_set_prot_region(int, int)          { errno = ENOSYS; return -1; }
int ashmem_pin_region(int, size_t, size_t)    { errno = ENOSYS; return -1; }
int ashmem_unpin_region(int, size_t, size_t)  { errno = ENOSYS; return -1; }
int ashmem_get_size_region(int)               { errno = ENOSYS; return -1; }
int ashmem_valid(int)                         { return 0; }

int jniThrowException(JNIEnv* env, const char* className, const char* msg) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    jclass clazz = env->FindClass(className);
    if (clazz == nullptr) {
        // The requested exception type does not exist in this framework.  Say so
        // and fall back, rather than returning with no exception pending -- the
        // callers all return immediately afterwards assuming one was raised.
        if (env->ExceptionCheck()) env->ExceptionClear();
        fprintf(stderr, "[WL-SQLITE] no such exception class %s; message was: %s\n",
                className, msg ? msg : "(null)");
        fflush(stderr);
        jclass rt = env->FindClass("java/lang/RuntimeException");
        if (rt == nullptr) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            return -1;
        }
        env->ThrowNew(rt, msg ? msg : "");
        env->DeleteLocalRef(rt);
        return 0;
    }
    env->ThrowNew(clazz, msg ? msg : "");
    env->DeleteLocalRef(clazz);
    return 0;
}

int jniThrowExceptionFmt(JNIEnv* env, const char* className, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return jniThrowException(env, className, buf);
}

int jniThrowNullPointerException(JNIEnv* env, const char* msg) {
    return jniThrowException(env, "java/lang/NullPointerException", msg);
}

int jniThrowRuntimeException(JNIEnv* env, const char* msg) {
    return jniThrowException(env, "java/lang/RuntimeException", msg);
}

int jniThrowIOException(JNIEnv* env, int errnum) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", strerror(errnum));
    return jniThrowException(env, "java/io/IOException", buf);
}

int jniRegisterNativeMethods(JNIEnv* env, const char* className,
                             const JNINativeMethod* methods, int numMethods) {
    jclass clazz = env->FindClass(className);
    if (clazz == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        fprintf(stderr, "[WL-SQLITE] RegisterNatives: class not found: %s\n", className);
        fflush(stderr);
        return -1;
    }
    jint rc = env->RegisterNatives(clazz, methods, numMethods);
    if (rc != JNI_OK && env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(clazz);
    if (rc != JNI_OK) {
        fprintf(stderr, "[WL-SQLITE] RegisterNatives failed for %s rc=%d\n", className, (int) rc);
        fflush(stderr);
        return -1;
    }
    return 0;
}

void jniLogException(JNIEnv* env, int priority, const char* tag, jthrowable thrown) {
    (void) priority;
    (void) thrown;
    if (env->ExceptionCheck()) {
        fprintf(stderr, "[WL-SQLITE/%s] pending exception:\n", tag ? tag : "?");
        fflush(stderr);
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

}  // extern "C"
