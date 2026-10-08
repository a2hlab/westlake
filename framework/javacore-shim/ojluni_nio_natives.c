/*
 * libcore's NIO natives (AOSP libcore/ojluni/src/main/native, built unmodified by
 * tools/build_missing_natives.sh), which the staged runtime does not register: sun.nio.ch's
 * Net, IOUtil, the socket/datagram channels, PollArrayWrapper, FileDispatcherImpl, and
 * java.nio.MappedByteBuffer. Without them NIO socket channels fail in their class initializers
 * (Mindustry, Unciv: sun.nio.ch.Net.pollinValue) and MappedByteBuffer.load() throws (Zoom).
 *
 * This file supplies what those sources take from libopenjdkjvm and libnativehelper.
 */
#include <dlfcn.h>
#include <errno.h>
#include <jni.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Android tags sockets for traffic statistics (SocketTagger); nothing counts them here. */
__attribute__((visibility("hidden"))) int tagSocket(JNIEnv* env, int fd)
{
    (void) env; (void) fd;
    return 0;
}

__attribute__((visibility("hidden"))) int jio_snprintf(char* str, size_t count, const char* fmt, ...)
{
    va_list args;
    int n;
    va_start(args, fmt);
    n = vsnprintf(str, count, fmt, args);
    va_end(args);
    return n;
}

JNIEXPORT jint JNICALL JVM_InitializeSocketLibrary(void)
{
    return 0;
}

JNIEXPORT jint JNICALL JVM_GetLastErrorString(char* buf, int len)
{
    if (errno == 0 || len <= 0) return 0;
    snprintf(buf, (size_t) len, "%s", strerror(errno));
    return (jint) strlen(buf);
}

/* The POSIX strerror_r, under the name glibc gives it (jni_util_md.c calls it). */
__attribute__((visibility("hidden"))) int __xpg_strerror_r(int error, char* buf, size_t len)
{
    if (len == 0) return ERANGE;
    snprintf(buf, len, "%s", strerror(error));
    return 0;
}

JNIEXPORT void* JNICALL JVM_FindLibraryEntry(void* handle, const char* name)
{
    return dlsym(handle, name);
}

__attribute__((visibility("hidden"))) int jniRegisterNativeMethods(JNIEnv* env, const char* className,
                                                                   const JNINativeMethod* methods, int count)
{
    jclass c = (*env)->FindClass(env, className);
    int rc;
    if (c == NULL) {
        (*env)->ExceptionClear(env);
        return -1;
    }
    rc = (*env)->RegisterNatives(env, c, methods, count);
    if (rc != JNI_OK) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, c);
    return rc;
}

void register_java_net_InetAddress(JNIEnv* env);
void register_java_net_Inet4Address(JNIEnv* env);
void register_java_net_Inet6Address(JNIEnv* env);
void register_sun_nio_ch_Net(JNIEnv* env);
void register_sun_nio_ch_IOUtil(JNIEnv* env);
void register_sun_nio_ch_SocketChannelImpl(JNIEnv* env);
void register_sun_nio_ch_ServerSocketChannelImpl(JNIEnv* env);
void register_sun_nio_ch_DatagramChannelImpl(JNIEnv* env);
void register_sun_nio_ch_DatagramDispatcher(JNIEnv* env);
void register_java_sun_nio_ch_PollArrayWrapper(JNIEnv* env);
void register_sun_nio_ch_FileDispatcherImpl(JNIEnv* env);
void register_java_nio_MappedByteBuffer(JNIEnv* env);

/*
 * sun.nio.fs.UnixNativeDispatcher's file-time setters, from AOSP's UnixNativeDispatcher.c (built
 * unmodified). The runtime registers the class's other natives, not these two, so setting a file's
 * times (Files.setLastModifiedTime, BasicFileAttributeView.setTimes) threw UnsatisfiedLinkError:
 * gitling's first activity stopped on futimes. Only these are registered here; the rest keep the
 * runtime's. That file's own init, run once for its copy, looks up the functions they call through
 * (futimesat), which the runtime's init sets only for its own: without it, futimes threw
 * "my_ftimesat_func is NULL". Returns 1 when registered.
 */
JNIEXPORT jint JNICALL Java_sun_nio_fs_UnixNativeDispatcher_init(JNIEnv* env, jclass c);
JNIEXPORT void JNICALL Java_sun_nio_fs_UnixNativeDispatcher_futimes(JNIEnv* env, jclass c, jint fd,
                                                                    jlong accessTime, jlong modificationTime);
JNIEXPORT void JNICALL Java_sun_nio_fs_UnixNativeDispatcher_utimes0(JNIEnv* env, jclass c, jlong pathAddress,
                                                                    jlong accessTime, jlong modificationTime);

static int register_file_times(JNIEnv* env)
{
    static const JNINativeMethod methods[] = {
        { "futimes", "(IJJ)V", (void*) Java_sun_nio_fs_UnixNativeDispatcher_futimes },
        { "utimes0", "(JJJ)V", (void*) Java_sun_nio_fs_UnixNativeDispatcher_utimes0 },
    };
    jclass c = (*env)->FindClass(env, "sun/nio/fs/UnixNativeDispatcher");
    if (c == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    Java_sun_nio_fs_UnixNativeDispatcher_init(env, c);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, c);
    return jniRegisterNativeMethods(env, "sun/nio/fs/UnixNativeDispatcher", methods,
                                    (int) (sizeof(methods) / sizeof(methods[0]))) == JNI_OK;
}

/* Returns how many classes were registered. */
int wl_register_ojluni_nio(JNIEnv* env)
{
    /* These only look up the InetAddress field IDs the NIO sources use (this library's copies:
     * the runtime's own InetAddress natives initialize theirs, not these; Unciv's Net.connect0
     * aborted on "fid == null"). They register nothing. */
    void (*const registrations[])(JNIEnv*) = {
        register_java_net_InetAddress, register_java_net_Inet4Address, register_java_net_Inet6Address,
        register_sun_nio_ch_Net, register_sun_nio_ch_IOUtil, register_sun_nio_ch_SocketChannelImpl,
        register_sun_nio_ch_ServerSocketChannelImpl, register_sun_nio_ch_DatagramChannelImpl,
        register_sun_nio_ch_DatagramDispatcher, register_java_sun_nio_ch_PollArrayWrapper,
        register_sun_nio_ch_FileDispatcherImpl, register_java_nio_MappedByteBuffer,
    };
    int done = 0;
    for (size_t i = 0; i < sizeof(registrations) / sizeof(registrations[0]); ++i) {
        registrations[i](env);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        else done++;
    }
    done += register_file_times(env);
    return done;
}
