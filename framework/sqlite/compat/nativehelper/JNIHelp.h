/* Minimal stand-in for AOSP libnativehelper/include/nativehelper/JNIHelp.h.
 *
 * Only the exception throwers used by the database sources are provided.  Each
 * takes the same "class name, message" shape as the real one so the Java side
 * sees exactly the exception types SQLiteConnection.java and CursorWindow.java
 * are written to catch -- getting these wrong would turn a recoverable SQLite
 * error into an unhandled crash somewhere far from here.
 */
#ifndef WL_NATIVEHELPER_JNIHELP_H
#define WL_NATIVEHELPER_JNIHELP_H

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

int jniThrowException(JNIEnv* env, const char* className, const char* msg);

int jniThrowExceptionFmt(JNIEnv* env, const char* className, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));

int jniThrowNullPointerException(JNIEnv* env, const char* msg);
int jniThrowRuntimeException(JNIEnv* env, const char* msg);
int jniThrowIOException(JNIEnv* env, int errnum);

int jniRegisterNativeMethods(JNIEnv* env, const char* className,
                             const JNINativeMethod* methods, int numMethods);

void jniLogException(JNIEnv* env, int priority, const char* tag, jthrowable thrown);

#ifdef __cplusplus
}
#endif

#endif  // WL_NATIVEHELPER_JNIHELP_H
