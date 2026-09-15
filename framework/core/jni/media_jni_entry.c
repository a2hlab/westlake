/*
 * Android's MediaCodec class loads libmedia_jni before invoking native_init.
 * The OH runtime implements those natives in liboh_adapter_bridge, so keep the
 * Android load contract and delegate registration at the shared-library
 * boundary instead of changing framework Java initialization semantics.
 */
#include <jni.h>
#include <unistd.h>

extern int register_MediaCodec_shim(JNIEnv *env);

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK || env == NULL) {
        return JNI_ERR;
    }
    if (register_MediaCodec_shim(env) != 0) {
        return JNI_ERR;
    }
    static const char message[] = "[SOURCE-MEDIA-JNI] OH MediaCodec natives registered\n";
    (void)write(STDERR_FILENO, message, sizeof(message) - 1);
    return JNI_VERSION_1_6;
}
