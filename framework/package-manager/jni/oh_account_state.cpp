#include <jni.h>
#include <nativehelper/JNIHelp.h>
#include <os_account_manager.h>
#include <cstdio>
#include <unistd.h>

extern "C" JNIEXPORT jintArray JNICALL
Java_adapter_packagemanager_OHUserManager_nativeState(JNIEnv* env, jclass) {
    using OHOS::AccountSA::OsAccountManager;
    int account = -1;
    bool active = false, verified = false;
    int error = OsAccountManager::GetOsAccountLocalIdFromUid(getuid(), account);
    if (!error) error = OsAccountManager::IsOsAccountActived(account, active);
    if (!error) error = OsAccountManager::IsOsAccountVerified(account, verified);
    if (error) {
        char message[128];
        snprintf(message, sizeof(message), "OH account state query failed: uid=%u account=%d error=%d",
                 getuid(), account, error);
        jniThrowException(env, "java/lang/IllegalStateException", message);
        return nullptr;
    }
    jint values[] = {account, active, verified};
    jintArray result = env->NewIntArray(3);
    if (result) env->SetIntArrayRegion(result, 0, 3, values);
    return result;
}
