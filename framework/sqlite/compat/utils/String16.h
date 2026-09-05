/* Minimal stand-in for AOSP system/core/libutils/include/utils/String16.h.
 *
 * One construction site uses this, in CursorWindow's nativeGetString:
 *
 *     String16 utf16(value, sizeIncludingNull - 1);
 *     return env->NewString(reinterpret_cast<const jchar*>(utf16.string()), utf16.size());
 *
 * i.e. build UTF-16 from a UTF-8 byte range and hand it to NewString.  That path
 * exists because NewStringUTF expects Modified UTF-8 and mangles real UTF-8 with
 * codepoints above the BMP, so this conversion has to be correct for the article
 * text the feed will actually store -- emoji included.
 */
#ifndef WL_UTILS_STRING16_H
#define WL_UTILS_STRING16_H

#include <stddef.h>
#include <uchar.h>

#include <string>

namespace android {

class String16 {
public:
    String16() {}
    /* utf8 is a UTF-8 byte range of len bytes, not necessarily NUL-terminated. */
    String16(const char* utf8, size_t len);

    const char16_t* string() const { return mStr.c_str(); }
    const char16_t* c_str() const { return mStr.c_str(); }
    size_t size() const { return mStr.size(); }
    bool isEmpty() const { return mStr.empty(); }

private:
    std::u16string mStr;
};

}  // namespace android

#endif  // WL_UTILS_STRING16_H
