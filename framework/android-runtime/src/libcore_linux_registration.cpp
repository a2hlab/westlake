// Bind the original Android 15 Linux and asynchronous-close implementations
// after core initialization, before framework resources or applications use I/O.
#include <jni.h>
#include <cstdio>

void register_libcore_io_Linux(JNIEnv*);
void register_libcore_io_AsynchronousCloseMonitor(JNIEnv*);

namespace android {
// Keep the runtime registrar ABI used by the earlier memory-only boundary.
int register_westlake_linux_memory(JNIEnv* env) {
    register_libcore_io_Linux(env);
    if (env->ExceptionCheck()) return -1;
    register_libcore_io_AsynchronousCloseMonitor(env);
    if (env->ExceptionCheck()) return -1;
    fprintf(stderr, "SOURCE_LIBCORE_LINUX_REGISTERED\n");
    return 0;
}
}
