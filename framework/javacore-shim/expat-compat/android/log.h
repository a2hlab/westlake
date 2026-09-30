#pragma once
#include <stdio.h>
#include <stdlib.h>
enum { ANDROID_LOG_WARN = 5, ANDROID_LOG_ERROR = 6, ANDROID_LOG_FATAL = 7 };
#define __android_log_print(prio, tag, ...) \
    (fprintf(stderr, "[%s] ", (tag)), fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#define __android_log_assert(cond, tag, ...) (__android_log_print(0, tag, __VA_ARGS__), abort())
#define __android_log_write(prio, tag, text) __android_log_print(prio, tag, "%s", text)
