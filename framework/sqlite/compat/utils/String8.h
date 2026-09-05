/* Minimal stand-in for AOSP system/core/libutils/include/utils/String8.h.
 *
 * Backed by std::string rather than AOSP's refcounted shared buffer.  Nothing in
 * the database sources relies on String8's copy-on-write sharing -- the strings
 * here are a database path, a connection label, an ashmem region name and a few
 * exception messages, all short-lived and never passed between threads -- so the
 * simple representation is behaviourally equivalent and much easier to trust.
 *
 * Both c_str() and string() are provided: the AOSP tree renamed string() to
 * c_str() partway through, and the supplied .cpp files use each spelling in
 * different places.
 */
#ifndef WL_UTILS_STRING8_H
#define WL_UTILS_STRING8_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <string>

namespace android {

class String8 {
public:
    String8() {}
    String8(const char* s) : mStr(s ? s : "") {}
    String8(const char* s, size_t n) : mStr(s ? std::string(s, n) : std::string()) {}
    String8(const String8& o) : mStr(o.mStr) {}
    explicit String8(const std::string& s) : mStr(s) {}

    String8& operator=(const String8& o) { mStr = o.mStr; return *this; }
    String8& operator=(const char* s) { mStr = s ? s : ""; return *this; }

    const char* c_str() const { return mStr.c_str(); }
    const char* string() const { return mStr.c_str(); }
    size_t size() const { return mStr.size(); }
    size_t length() const { return mStr.size(); }
    bool isEmpty() const { return mStr.empty(); }
    void clear() { mStr.clear(); }

    void append(const char* s) { if (s) mStr.append(s); }
    void append(const String8& s) { mStr.append(s.mStr); }
    void setTo(const char* s) { mStr = s ? s : ""; }
    void setTo(const char* s, size_t n) { mStr.assign(s ? s : "", s ? n : 0); }

    /* Returns 0 on success, mirroring AOSP's status_t convention. */
    int appendFormat(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(nullptr, 0, fmt, ap);
        va_end(ap);
        if (n < 0) return -1;
        std::string buf(static_cast<size_t>(n) + 1, '\0');
        va_start(ap, fmt);
        vsnprintf(&buf[0], buf.size(), fmt, ap);
        va_end(ap);
        buf.resize(static_cast<size_t>(n));
        mStr.append(buf);
        return 0;
    }

    String8& operator+=(const char* s) { append(s); return *this; }
    String8& operator+=(const String8& s) { append(s); return *this; }
    bool operator==(const String8& o) const { return mStr == o.mStr; }
    bool operator!=(const String8& o) const { return mStr != o.mStr; }

    const std::string& std_string() const { return mStr; }
    std::string& std_string() { return mStr; }

private:
    std::string mStr;
};

}  // namespace android

#endif  // WL_UTILS_STRING8_H
