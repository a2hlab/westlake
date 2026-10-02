#pragma once
#include <stdarg.h>
#include <stdio.h>
#include <string>
namespace android {
namespace base {
__attribute__((format(printf, 1, 2))) inline std::string StringPrintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string out(n > 0 ? n : 0, '\0');
    if (n > 0) vsnprintf(&out[0], n + 1, fmt, ap);
    va_end(ap);
    return out;
}
}  // namespace base
}  // namespace android
