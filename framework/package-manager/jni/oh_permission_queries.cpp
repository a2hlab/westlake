// Read-only permission state for the actual process token. Public check API;
// GetPermissionFlag is the documented OH inner API, resolved fail-closed.
#include <jni.h>
#include <accesstoken/ability_access_control.h>
#include <dlfcn.h>
#include <cstdint>
#include <string>

extern "C" JNIEXPORT jboolean JNICALL
Java_adapter_packagemanager_PermissionManagerAdapter_00024Native_granted(JNIEnv* env, jclass, jstring value) {
    if (!value) return JNI_FALSE;
    const char* permission = env->GetStringUTFChars(value, nullptr);
    if (!permission) return JNI_FALSE;
    bool result = OH_AT_CheckSelfPermission(permission);
    env->ReleaseStringUTFChars(value, permission);
    return result ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jint JNICALL
Java_adapter_packagemanager_PermissionManagerAdapter_00024Native_flags(JNIEnv* env, jclass, jstring value) {
    using Self = uint64_t (*)();
    using Flags = int (*)(uint32_t, const std::string&, uint32_t&);
    static void* token = dlopen("/system/lib64/platformsdk/libtokenid_sdk.z.so", RTLD_NOW);
    static void* access = dlopen("/system/lib64/platformsdk/libaccesstoken_sdk.z.so", RTLD_NOW);
    static auto self = token ? reinterpret_cast<Self>(dlsym(token, "GetSelfTokenID")) : nullptr;
    static auto flags = access ? reinterpret_cast<Flags>(dlsym(access,
        "_ZN4OHOS8Security11AccessToken14AccessTokenKit17GetPermissionFlagEjRKNSt3__h12basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEERj")) : nullptr;
    if (!value || !self || !flags) return -1;
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) return -1;
    std::string permission(chars);
    env->ReleaseStringUTFChars(value, chars);
    uint32_t result = 0;
    return flags(static_cast<uint32_t>(self()), permission, result) == 0 ? static_cast<jint>(result) : -1;
}
