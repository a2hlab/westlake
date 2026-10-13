/*
 * adapter.core.NotificationSwitch.nativeEnabled: whether the user lets this application post
 * notifications. Android keeps that as POST_NOTIFICATIONS (a runtime permission from API 33) and
 * NotificationManager.areNotificationsEnabled; OpenHarmony keeps it as a per-application switch,
 * the host application's here, read through its NDK: OH_Notification_IsNotificationEnabled.
 *
 * Before, POST_NOTIFICATIONS was mapped to ohos.permission.NOTIFICATION_CONTROLLER, a system
 * permission no ordinary application holds (it controls other applications' notifications), so
 * every app read the permission as denied whatever the user had allowed.
 */
#include <dlfcn.h>
#include <jni.h>
#include <stdbool.h>
#include <stdio.h>

typedef bool (*IsEnabled)(void);

static IsEnabled query(void)
{
    static IsEnabled fn;
    static int resolved;
    if (!resolved) {
        resolved = 1;
        void* lib = dlopen("libohnotification.so", RTLD_NOW);
        if (lib == NULL) lib = dlopen("/system/lib64/ndk/libohnotification.so", RTLD_NOW);
        if (lib != NULL) fn = (IsEnabled) dlsym(lib, "OH_Notification_IsNotificationEnabled");
        fprintf(stderr, "[WL-NOTIFY] OH_Notification_IsNotificationEnabled %s\n", fn ? "bound" : "unavailable");
    }
    return fn;
}

JNIEXPORT jboolean JNICALL Java_adapter_core_NotificationSwitch_nativeEnabled(JNIEnv* env, jclass cls)
{
    (void) env;
    (void) cls;
    IsEnabled fn = query();
    return fn != NULL && fn() ? JNI_TRUE : JNI_FALSE;
}

int wl_register_notification_switch(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "adapter/core/NotificationSwitch");
    if (cls == NULL) {
        (*env)->ExceptionClear(env);
        return -1;
    }
    JNINativeMethod methods[] = {
        {"nativeEnabled", "()Z", (void*) Java_adapter_core_NotificationSwitch_nativeEnabled},
    };
    int rc = (*env)->RegisterNatives(env, cls, methods, 1);
    if (rc != 0) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, cls);
    return rc == 0 ? 1 : -1;
}
