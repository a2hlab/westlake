/* Minimal stand-in for AOSP system/libbase/include/android-base/logging.h.
 *
 * wl_CursorWindow.cpp logs with the streaming form, LOG(ERROR) << "...", and
 * PLOG(ERROR) for calls that set errno.  Both are implemented by a temporary
 * whose destructor writes one complete line to stderr, so a log statement can
 * never be interleaved halfway through by another thread the way a sequence of
 * separate fprintf calls could.
 *
 * DEBUG-severity lines are dropped unless WL_SQLITE_VERBOSE is set.  CursorWindow
 * logs one at every inflate and parcel operation, which on a forced-interpreted
 * runtime is noise that would bury the messages that matter.
 */
#ifndef WL_ANDROID_BASE_LOGGING_H
#define WL_ANDROID_BASE_LOGGING_H

#include <errno.h>
#include <string.h>

#include <ostream>
#include <sstream>

#ifndef LOG_TAG
#define LOG_TAG "WL"
#endif

namespace android {
namespace base {

class LogMessage {
public:
    LogMessage(const char* file, int line, const char* severity, bool withErrno)
        : mSeverity(severity), mErrno(withErrno ? errno : 0), mWithErrno(withErrno) {
        (void) file;
        (void) line;
    }

    ~LogMessage();

    std::ostream& stream() { return mStream; }

private:
    std::ostringstream mStream;
    const char* mSeverity;
    int mErrno;
    bool mWithErrno;
};

/* Swallows the streamed values without evaluating side effects into output. */
class NullLogStream {
public:
    template <typename T>
    NullLogStream& operator<<(const T&) { return *this; }
};

bool WlLogDebugEnabled();

}  // namespace base
}  // namespace android

#define WL_LOG_STREAM(sev, withErrno) \
    ::android::base::LogMessage(__FILE__, __LINE__, sev, withErrno).stream()

#ifdef WL_SQLITE_VERBOSE
#define WL_LOG_DEBUG_STREAM() WL_LOG_STREAM("DEBUG", false)
#else
#define WL_LOG_DEBUG_STREAM() ::android::base::NullLogStream()
#endif

/* LOG(x) / PLOG(x) take a bare severity token, e.g. LOG(ERROR) << "boom". */
#define WL_LOG_ERROR()   WL_LOG_STREAM("ERROR", false)
#define WL_LOG_WARNING() WL_LOG_STREAM("WARNING", false)
#define WL_LOG_INFO()    WL_LOG_STREAM("INFO", false)
#define WL_LOG_VERBOSE() WL_LOG_DEBUG_STREAM()
#define WL_LOG_DEBUG()   WL_LOG_DEBUG_STREAM()
#define WL_LOG_FATAL()   WL_LOG_STREAM("FATAL", false)

#define WL_PLOG_ERROR()   WL_LOG_STREAM("ERROR", true)
#define WL_PLOG_WARNING() WL_LOG_STREAM("WARNING", true)
#define WL_PLOG_INFO()    WL_LOG_STREAM("INFO", true)
#define WL_PLOG_VERBOSE() WL_LOG_DEBUG_STREAM()
#define WL_PLOG_DEBUG()   WL_LOG_DEBUG_STREAM()
#define WL_PLOG_FATAL()   WL_LOG_STREAM("FATAL", true)

#define LOG(severity)  WL_LOG_##severity()
#define PLOG(severity) WL_PLOG_##severity()

#define CHECK(cond)                                                            \
    if (!(cond)) WL_LOG_STREAM("FATAL", false) << "CHECK failed: " #cond " "

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_NE(a, b) CHECK((a) != (b))
#define CHECK_LT(a, b) CHECK((a) < (b))
#define CHECK_LE(a, b) CHECK((a) <= (b))
#define CHECK_GT(a, b) CHECK((a) > (b))
#define CHECK_GE(a, b) CHECK((a) >= (b))

#endif  // WL_ANDROID_BASE_LOGGING_H
