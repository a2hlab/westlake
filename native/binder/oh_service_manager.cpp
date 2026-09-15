// Native service discovery uses the same Java service registry as the OH
// framework facade. No Android kernel service-manager process runs on this host.
#include <android/binder_ibinder_jni.h>
#include <android/binder_manager.h>
#include <android_runtime/AndroidRuntime.h>
#include <nativehelper/ScopedLocalRef.h>
#include <cstdio>

namespace {
AIBinder* service(const char* instance, const char* method) {
    if (!instance) return nullptr;
    JNIEnv* env = android::AndroidRuntime::getJNIEnv();
    if (!env) return nullptr;
    ScopedLocalRef<jclass> manager(env, env->FindClass("android/os/ServiceManager"));
    jmethodID query = manager.get() ? env->GetStaticMethodID(
            manager.get(), method, "(Ljava/lang/String;)Landroid/os/IBinder;") : nullptr;
    if (!query || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        std::fprintf(stderr, "[WESTLAKE-SERVICE] Java registry unavailable for %s\n", instance);
        return nullptr;
    }
    ScopedLocalRef<jstring> name(env, env->NewStringUTF(instance));
    if (!name.get()) return nullptr;
    ScopedLocalRef<jobject> binder(env, env->CallStaticObjectMethod(manager.get(), query, name.get()));
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return nullptr;
    }
    return binder.get() ? AIBinder_fromJavaBinder(env, binder.get()) : nullptr;
}
} // namespace

extern "C" AIBinder* AServiceManager_getService(const char* instance) {
    return service(instance, "getService");
}

extern "C" AIBinder* AServiceManager_checkService(const char* instance) {
    return service(instance, "checkService");
}
