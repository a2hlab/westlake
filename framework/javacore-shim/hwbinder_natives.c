/*
 * android.os.HwBinder (HIDL) as on a device that has no hwservicemanager. OH has no HIDL and AOSP's natives (libhidl)
 * are not part of Westlake, so HwBinder's class initializer died in native_init ("No implementation found for long
 * android.os.HwBinder.native_init()") and the class could not be used at all: an SDK that probes for a vendor HIDL
 * service (a Qookka game, Alipay HK) failed where Android answers that the service is not there.
 *
 * These natives let the classes initialize and answer as such a device: getService throws NoSuchElementException,
 * what libhidl's getService gives for a service it cannot find; registering or calling a service throws
 * RemoteException; the thread-pool and property calls do nothing. HwParcel, HwRemoteBinder and HwBlob get native_init
 * and their native_setup, so their classes initialize too; their other natives stay unregistered. Each method is
 * registered by itself, so a signature that differs in this framework leaves only that one out.
 */
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void wl_hidl_free(void* object)
{
    (void) object;
}

static jlong hw_native_init(JNIEnv* env, jclass cls)
{
    (void) env;
    (void) cls;
    return (jlong) (uintptr_t) &wl_hidl_free;
}

static void hw_setup(JNIEnv* env, jobject self)
{
    (void) env;
    (void) self;
}

static void hw_setup_flag(JNIEnv* env, jobject self, jboolean allocate)
{
    (void) env;
    (void) self;
    (void) allocate;
}

static void hw_setup_size(JNIEnv* env, jobject self, jint size)
{
    (void) env;
    (void) self;
    (void) size;
}

static void hw_nothing(JNIEnv* env, jclass cls)
{
    (void) env;
    (void) cls;
}

static void hw_treble_override(JNIEnv* env, jclass cls, jboolean testing)
{
    (void) env;
    (void) cls;
    (void) testing;
}

static void hw_configure_threadpool(JNIEnv* env, jclass cls, jlong maxThreads, jboolean callerWillJoin)
{
    (void) env;
    (void) cls;
    (void) maxThreads;
    (void) callerWillJoin;
}

static void hw_throw(JNIEnv* env, const char* name, const char* message)
{
    jclass cls = (*env)->FindClass(env, name);
    if (cls == NULL) return;   /* FindClass left its own exception pending */
    (*env)->ThrowNew(env, cls, message);
    (*env)->DeleteLocalRef(env, cls);
}

static jobject hw_get_service(JNIEnv* env, jclass cls, jstring iface, jstring name, jboolean retry)
{
    (void) cls;
    (void) retry;
    static int told;
    const char* i = iface != NULL ? (*env)->GetStringUTFChars(env, iface, NULL) : NULL;
    const char* n = name != NULL ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
    char message[320];
    snprintf(message, sizeof(message), "HIDL service %s/%s: this device has no hwservicemanager",
             i != NULL ? i : "?", n != NULL ? n : "?");
    if (__atomic_add_fetch(&told, 1, __ATOMIC_RELAXED) <= 8) fprintf(stderr, "[WL-HIDL] getService: %s\n", message);
    if (i != NULL) (*env)->ReleaseStringUTFChars(env, iface, i);
    if (n != NULL) (*env)->ReleaseStringUTFChars(env, name, n);
    hw_throw(env, "java/util/NoSuchElementException", message);
    return NULL;
}

static void hw_register_service(JNIEnv* env, jobject self, jstring name)
{
    (void) self;
    (void) name;
    hw_throw(env, "android/os/RemoteException", "this device has no hwservicemanager");
}

static void hw_transact(JNIEnv* env, jobject self, jint code, jobject request, jobject reply, jint flags)
{
    (void) self;
    (void) code;
    (void) request;
    (void) reply;
    (void) flags;
    hw_throw(env, "android/os/RemoteException", "this device has no HIDL services");
}

static int bind_each(JNIEnv* env, const char* name, const JNINativeMethod* methods, int count)
{
    jclass cls = (*env)->FindClass(env, name);
    if (cls == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    int bound = 0;
    for (int i = 0; i < count; ++i) {
        if ((*env)->RegisterNatives(env, cls, &methods[i], 1) == 0) {
            ++bound;
        } else {
            (*env)->ExceptionClear(env);
        }
    }
    (*env)->DeleteLocalRef(env, cls);
    return bound;
}

/* HwBinder's natives bound (of 9); *helpers gets those of HwParcel, HwRemoteBinder and HwBlob (of 6). */
int wl_register_hwbinder(JNIEnv* env, int* helpers)
{
    static const JNINativeMethod binder[] = {
        {"native_init", "()J", (void*) hw_native_init},
        {"native_setup", "()V", (void*) hw_setup},
        {"transact", "(ILandroid/os/HwParcel;Landroid/os/HwParcel;I)V", (void*) hw_transact},
        {"registerService", "(Ljava/lang/String;)V", (void*) hw_register_service},
        {"getService", "(Ljava/lang/String;Ljava/lang/String;Z)Landroid/os/IHwBinder;", (void*) hw_get_service},
        {"setTrebleTestingOverride", "(Z)V", (void*) hw_treble_override},
        {"configureRpcThreadpool", "(JZ)V", (void*) hw_configure_threadpool},
        {"joinRpcThreadpool", "()V", (void*) hw_nothing},
        {"native_report_sysprop_change", "()V", (void*) hw_nothing},
    };
    static const JNINativeMethod parcel[] = {
        {"native_init", "()J", (void*) hw_native_init},
        {"native_setup", "(Z)V", (void*) hw_setup_flag},
    };
    static const JNINativeMethod remote[] = {
        {"native_init", "()J", (void*) hw_native_init},
        {"native_setup_empty", "()V", (void*) hw_setup},
    };
    static const JNINativeMethod blob[] = {
        {"native_init", "()J", (void*) hw_native_init},
        {"native_setup", "(I)V", (void*) hw_setup_size},
    };
    *helpers = bind_each(env, "android/os/HwParcel", parcel, 2) + bind_each(env, "android/os/HwRemoteBinder", remote, 2)
               + bind_each(env, "android/os/HwBlob", blob, 2);
    return bind_each(env, "android/os/HwBinder", binder, (int) (sizeof(binder) / sizeof(binder[0])));
}
