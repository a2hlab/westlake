/* Minimal stand-in for AOSP system/core/liblog/include/log/log.h (via utils/Log.h).
 *
 * Everything goes to stderr rather than hilog.  That is deliberate: stderr from
 * this process is already the proven channel out of the app
 * (/data/service/el1/public/appspawnx/adapter_child_<pid>.stderr), and it is
 * where every other Westlake diagnostic in this workspace already lands, so
 * SQLite messages interleave with the TLS and AMR traces in one file with one
 * timeline.  Routing them to hilog instead would split the evidence in two.
 *
 * ALOGV is compiled out unless WL_SQLITE_VERBOSE is defined: the verbose hooks
 * fire per statement, and this runtime is forced to -Xint, so leaving them on
 * would both flood the log and distort the timings we measure from it.
 */
#ifndef WL_UTILS_LOG_H
#define WL_UTILS_LOG_H

#include <stdio.h>

#ifndef LOG_TAG
#define LOG_TAG "WL"
#endif

/* Priority tokens, as used by ALOG(LOG_VERBOSE, tag, ...). */
#define LOG_VERBOSE "V"
#define LOG_DEBUG   "D"
#define LOG_INFO    "I"
#define LOG_WARN    "W"
#define LOG_ERROR   "E"
#define LOG_FATAL   "F"

#define WL_LOG_PRINT(pri, tag, ...)                                            \
    do {                                                                       \
        fprintf(stderr, "[%s/%s] ", (pri), (tag));                             \
        fprintf(stderr, __VA_ARGS__);                                          \
        fputc('\n', stderr);                                                   \
        fflush(stderr);                                                        \
    } while (0)

#define ALOG(pri, tag, ...) WL_LOG_PRINT(pri, tag, __VA_ARGS__)

#define ALOGE(...) WL_LOG_PRINT(LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) WL_LOG_PRINT(LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define ALOGI(...) WL_LOG_PRINT(LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define ALOGD(...) WL_LOG_PRINT(LOG_DEBUG, LOG_TAG, __VA_ARGS__)

#ifdef WL_SQLITE_VERBOSE
#define ALOGV(...) WL_LOG_PRINT(LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define IF_ALOGV() if (true)
#else
/* Consume the arguments so they still type-check, but emit nothing. */
#define ALOGV(...) do { if (false) fprintf(stderr, __VA_ARGS__); } while (0)
#define IF_ALOGV() if (false)
#endif

#define ALOGE_IF(cond, ...) do { if (cond) ALOGE(__VA_ARGS__); } while (0)
#define ALOGW_IF(cond, ...) do { if (cond) ALOGW(__VA_ARGS__); } while (0)
#define ALOGI_IF(cond, ...) do { if (cond) ALOGI(__VA_ARGS__); } while (0)
#define ALOGD_IF(cond, ...) do { if (cond) ALOGD(__VA_ARGS__); } while (0)
#define ALOGV_IF(cond, ...) do { if (cond) ALOGV(__VA_ARGS__); } while (0)

#define LOG_ALWAYS_FATAL_IF(cond, ...)                                         \
    do {                                                                       \
        if (cond) {                                                            \
            WL_LOG_PRINT(LOG_FATAL, LOG_TAG, __VA_ARGS__);                      \
            abort();                                                           \
        }                                                                      \
    } while (0)
#define LOG_ALWAYS_FATAL(...)                                                  \
    do { WL_LOG_PRINT(LOG_FATAL, LOG_TAG, __VA_ARGS__); abort(); } while (0)
#define LOG_FATAL_IF(cond, ...) LOG_ALWAYS_FATAL_IF(cond, __VA_ARGS__)

#endif  // WL_UTILS_LOG_H
