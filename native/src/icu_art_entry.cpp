// Register both Android's libcore and ICU bridge natives before core initialization.
#include <jni.h>

extern void register_libcore_icu_ICU(JNIEnv* env);
extern "C" jint JNI_OnLoad_icu_bridge(JavaVM* vm, void* reserved);

extern "C" jint JNI_OnLoad_icu(JavaVM* vm, void* reserved) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    register_libcore_icu_ICU(env);
    if (env->ExceptionCheck()) return JNI_ERR;
    return JNI_OnLoad_icu_bridge(vm, reserved);
}
