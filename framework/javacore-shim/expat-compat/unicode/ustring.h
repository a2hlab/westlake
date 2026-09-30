#pragma once
#include <stdint.h>
#include <stddef.h>

typedef int UErrorCode;
#define U_ZERO_ERROR 0
#define U_BUFFER_OVERFLOW_ERROR 15

/* UTF-8 to UTF-16 with a substitution character for malformed sequences, the subset of ICU's
 * u_strFromUTF8WithSub the SAX parser relies on (srcLength >= 0, dest large enough). */
inline char16_t* u_strFromUTF8WithSub(char16_t* dest, int32_t destCapacity, int32_t* pDestLength,
                                      const char* src, int32_t srcLength, int32_t subchar,
                                      int32_t* pNumSubstitutions, UErrorCode* pErrorCode) {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(src);
    int32_t i = 0, n = 0, subs = 0;
    auto put = [&](uint32_t c) {
        if (c >= 0x10000) {
            if (n + 1 < destCapacity) {
                dest[n] = static_cast<char16_t>(0xD7C0 + (c >> 10));
                dest[n + 1] = static_cast<char16_t>(0xDC00 | (c & 0x3FF));
            }
            n += 2;
        } else {
            if (n < destCapacity) dest[n] = static_cast<char16_t>(c);
            n += 1;
        }
    };
    while (i < srcLength) {
        uint32_t c = s[i];
        int extra = c < 0x80 ? 0 : (c >= 0xC2 && c <= 0xDF) ? 1 : (c >= 0xE0 && c <= 0xEF) ? 2
                  : (c >= 0xF0 && c <= 0xF4) ? 3 : -1;
        if (extra < 0) { put(subchar); subs++; i++; continue; }
        c &= extra == 0 ? 0x7F : extra == 1 ? 0x1F : extra == 2 ? 0x0F : 0x07;
        /* The second byte's range rules out overlongs, surrogates and > U+10FFFF up front, so a
         * bad sequence yields one U+FFFD per maximal subpart, as ICU does. */
        unsigned char lo = 0x80, hi = 0xBF;
        if (s[i] == 0xE0) lo = 0xA0;
        else if (s[i] == 0xED) hi = 0x9F;
        else if (s[i] == 0xF0) lo = 0x90;
        else if (s[i] == 0xF4) hi = 0x8F;
        int k = 1;
        for (; k <= extra; k++) {
            unsigned char b = i + k < srcLength ? s[i + k] : 0;
            if (i + k >= srcLength || b < (k == 1 ? lo : 0x80) || b > (k == 1 ? hi : 0xBF)) break;
            c = (c << 6) | (b & 0x3F);
        }
        if (k <= extra) { put(subchar); subs++; i += k; continue; }
        put(c);
        i += k;
    }
    if (pDestLength) *pDestLength = n;
    if (pNumSubstitutions) *pNumSubstitutions = subs;
    if (pErrorCode && n > destCapacity) *pErrorCode = U_BUFFER_OVERFLOW_ERROR;
    return dest;
}
