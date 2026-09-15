/*
 * adapter_bridge.cpp
 *
 * JNI Bridge entry point.  Connects Android Java layer with OpenHarmony C++
 * IPC framework.  Per-adapter native methods now live in dedicated cpp files
 * registered via RegisterNatives — see register_* functions below.
 *
 * This file's responsibilities (post 2026-05-19 startReg refactor):
 *   - AdapterBridge singleton (LifecycleAdapter JNI cache, DataShare init)
 *   - JNI_OnLoad: skia codec registration + 7 register_* calls
 *   - Direction-2 classloader cache (adapter_bridge_set_class_loader/load_class)
 *   - AppSpawnXInit.nativeHiLog best-effort RegisterNatives (PathClassLoader
 *     non-BCP class — FindClass may return null at JNI_OnLoad time)
 *   - libmali RTLD_GLOBAL preload constructor
 *
 * Per-class native impls and JNINativeMethod tables:
 *   - adapter/core/OHEnvironment              → oh_environment.cpp
 *   - adapter/activity/ActivityManagerAdapter → activity_manager_adapter.cpp
 *   - adapter/activity/ActivityTaskManagerAdapter
 *                                             → activity_task_manager_adapter.cpp
 *   - adapter/window/WindowManagerAdapter     → window_manager_adapter.cpp
 *   - adapter/window/WindowSessionAdapter     → window_session_adapter.cpp
 *   - adapter/window/InputEventBridge         → input_event_bridge.cpp
 *
 * Historical context (kept for grep traceability):
 *   - 2026-04-22 B.1: original Java_adapter_OHEnvironment_* /
 *     Java_adapter_bridge_* / Java_adapter_client_* exports were patched
 *     with adapter_activity_/adapter_window_/adapter_core_ forwarder shims
 *     after the Java package rename to adapter.{core,activity,window}.
 *   - 2026-05-19 startReg refactor: every Java native is now registered via
 *     RegisterNatives + JNINativeMethod tables in sibling cpp files, mirroring
 *     liboh_android_runtime.so's startReg() dispatch model.  All Java_* dlsym
 *     long-name exports retired in this file.  The "OAT cache" rationale cited
 *     by the original forwarder shims was a 2026-04 misjudgment (ART JNI is
 *     lazy resolve, OAT does not cache dlsym).
 */
#include "adapter_bridge.h"
#include "oh_callback_handler.h"
#include "oh_datashare_client.h"
#include "oh_input_bridge.h"

#include <android/log.h>
#include <jni.h>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <pthread.h>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <cstdint>
#include <cstring>

// initDataShareJniCache lives in framework/contentprovider/jni/oh_datashare_client.cpp
// inside `namespace oh_adapter {}`. Must declare extern in matching namespace
// so the C++ mangled name matches the definition:
//   defined:  _ZN10oh_adapter21initDataShareJniCacheEP7_JNIEnv
//   (global extern would look for _Z21initDataShareJniCacheP7_JNIEnv → mismatch)
namespace oh_adapter { extern bool initDataShareJniCache(JNIEnv* env); }

#define LOG_TAG "OH_JNI_Bridge"
// B.37 (2026-04-29 EOD+2): direct HiLogPrint bypass for child diagnostics.
// __android_log_print → liblog → hilog bridge is broken in fork child, so
// LOGI/LOGE silently drop in child.  Route to HiLogPrint directly.
extern "C" int HiLogPrint(int type, int level, unsigned int domain,
                          const char* tag, const char* fmt, ...)
    __attribute__((__format__(printf, 5, 6)));
#define LOGI(fmt, ...) HiLogPrint(3, 4, 0xD000F00u, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) HiLogPrint(3, 6, 0xD000F00u, LOG_TAG, fmt, ##__VA_ARGS__)

// 2026-05-08 G2.14x: preload the chipset Mali GPU driver before any hwui PLT
// resolution, so the driver is resident as early as it is in an RS process
// (which pulls libmali.so.0 in via DT_NEEDED) rather than being loaded lazily
// by OH's EglWrapperLoader. This was identified as a candidate root cause for
// the EGLDisplay-invalid issue seen in helloworld but not in RS.
//
// 2026-09-11: the preload stays, but it is RTLD_LOCAL now — it was RTLD_GLOBAL,
// which published the whole driver into the process-wide lookup chain. That is
// not just the eglXxx entry points: libGLES_mali.z.so statically links zlib and
// exports 90 of its symbols (deflate*, inflate*, crc32*, adler32*, compress*).
// Publishing them globally let a z_stream initialised by one zlib be handed to
// the driver's private copy, whose deflate_state differs, and Toutiao died
// reproducibly with SIGSEGV in deflateReset+60 on a bd_tracker thread about two
// minutes into every run (cppcrash-17883, cppcrash-19202).
//
// Stock OH never has this problem: the driver lives in the chipsetsdk linker
// namespace and EglWrapperLoader::Load opens libEGL_impl.so (the libmali
// symlink) with RTLD_LOCAL, keeping those symbols private. RTLD_LOCAL restores
// exactly that, while keeping the early-load behaviour the preload exists for.
__attribute__((constructor))
static void preload_libmali_global() {
    void* h = dlopen("libmali.so.0", RTLD_NOW | RTLD_LOCAL);
    if (h == nullptr) {
        LOGE("preload libmali.so.0 failed: %{public}s", dlerror());
    } else {
        LOGI("preload libmali.so.0 -> RTLD_LOCAL OK (handle=%p)", h);
    }
}

using namespace oh_adapter;

// ==================== AdapterBridge Implementation ====================

AdapterBridge& AdapterBridge::getInstance() {
    static AdapterBridge instance;
    return instance;
}

bool AdapterBridge::initialize(JNIEnv* env) {
    LOGI("AdapterBridge::initialize()");

    // Cache LifecycleAdapter JNI method IDs.  The class moved from
    // adapter.* to adapter.activity.*; FindClass on the legacy path returns
    // null (which propagated as a bare NoClassDefFoundError to the caller).
    jclass clazz = env->FindClass("adapter/activity/LifecycleAdapter");
    if (clazz == nullptr) {
        // Legacy path fallback in case some build still ships the old package
        if (env->ExceptionCheck()) env->ExceptionClear();
        clazz = env->FindClass("adapter/LifecycleAdapter");
    }
    if (clazz == nullptr) {
        LOGE("Failed to find adapter/activity/LifecycleAdapter class");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }
    lifecycle_adapter_class_ = (jclass)env->NewGlobalRef(clazz);
    on_oh_lifecycle_callback_ = env->GetMethodID(clazz, "onOHLifecycleCallback", "(II)V");
    if (on_oh_lifecycle_callback_ == nullptr) {
        LOGE("Failed to find onOHLifecycleCallback method");
        return false;
    }

    // Initialize DataShare JNI cache (MatrixCursor class/method IDs)
    if (!initDataShareJniCache(env)) {
        LOGE("Failed to initialize DataShare JNI cache");
        // Non-fatal: ContentProvider bridge won't work, but other bridges are fine
    }

    return true;
}

void AdapterBridge::shutdown() {
    LOGI("AdapterBridge::shutdown()");
    JNIEnv* env = getEnv();
    if (env && lifecycle_adapter_ref_) {
        env->DeleteGlobalRef(lifecycle_adapter_ref_);
        lifecycle_adapter_ref_ = nullptr;
    }
    if (env && lifecycle_adapter_class_) {
        env->DeleteGlobalRef(lifecycle_adapter_class_);
        lifecycle_adapter_class_ = nullptr;
    }
}

void AdapterBridge::setLifecycleAdapterRef(JNIEnv* env, jobject obj) {
    if (lifecycle_adapter_ref_) {
        env->DeleteGlobalRef(lifecycle_adapter_ref_);
    }
    lifecycle_adapter_ref_ = env->NewGlobalRef(obj);
}

JNIEnv* AdapterBridge::getEnv() {
    JNIEnv* env = nullptr;
    if (jvm_) {
        int status = jvm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            jvm_->AttachCurrentThread(&env, nullptr);
        }
    }
    return env;
}

void AdapterBridge::callbackLifecycleChange(int abilityToken, int ohState) {
    JNIEnv* env = getEnv();
    if (!env || !lifecycle_adapter_ref_) {
        LOGE("Cannot callback: JNI env or LifecycleAdapter ref is null");
        return;
    }

    // Call LifecycleAdapter directly using the stored singleton reference
    env->CallVoidMethod(lifecycle_adapter_ref_, on_oh_lifecycle_callback_,
                        abilityToken, ohState);
}

// Forward decl outside extern "C" so the C++ mangled name matches the
// definition in skia_codec_register.cpp.
namespace adapter { void RegisterAllSkiaCodecs(); }

// ==================== Hilog logging helpers for AppSpawnX side ====================

#define ZYG_LOG_INFO(fmt, ...)  HiLogPrint(3, 4, 0xD000F00u, "AppSpawnX", fmt, ##__VA_ARGS__)
#define ZYG_LOG_ERR(fmt, ...)   HiLogPrint(3, 6, 0xD000F00u, "AppSpawnX", fmt, ##__VA_ARGS__)

#define B32_LOG_TYPE_CORE  3       // OH LOG_CORE
#define B32_LOG_INFO       4       // OH LOG_INFO
#define B32_LOG_DOMAIN     0xD000F00u  // matches APPSPAWNX_LOG_DOMAIN in spawn_msg.h

// ==================== Direction-2 classloader cache ====================
//
// 2026-04-30: cache PathClassLoader for cross-thread access.  Set time: at
// JNI_OnLoad time, FindClass(AppSpawnXInit) often returns null (system CL
// cannot find PathClassLoader classes), so appspawnx_runtime.cpp injects the
// classloader at B.35.A time via dlsym → adapter_bridge_set_class_loader.
// OH IPC native threads (attached, system CL only) use adapter_bridge_load_class
// to load classes that moved into oh-adapter-runtime.jar (e.g.,
// AppSchedulerBridge).

static JavaVM* g_adapter_jvm = nullptr;
static jobject g_adapter_class_loader = nullptr;   // global ref to PathClassLoader
static jmethodID g_load_class_method = nullptr;    // ClassLoader.loadClass(String)

extern "C" void adapter_bridge_set_class_loader(JavaVM* vm, jobject classLoader,
                                                jmethodID loadClassMethod) {
    g_adapter_jvm = vm;
    if (!classLoader) {
        ZYG_LOG_ERR("[Direction2] adapter_bridge_set_class_loader called with null cl");
        return;
    }
    JNIEnv* env = nullptr;
    if (vm && vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK
        && env != nullptr) {
        if (g_adapter_class_loader) {
            env->DeleteGlobalRef(g_adapter_class_loader);
        }
        g_adapter_class_loader = env->NewGlobalRef(classLoader);
        g_load_class_method = loadClassMethod;
        ZYG_LOG_INFO("[Direction2] adapter_bridge_set_class_loader cached cl=%{public}p loadClass=%{public}p",
            g_adapter_class_loader, g_load_class_method);
    } else {
        ZYG_LOG_ERR("[Direction2] adapter_bridge_set_class_loader: GetEnv failed");
    }
}

extern "C" jclass adapter_bridge_load_class(JNIEnv* env, const char* binaryName) {
    if (!env || !binaryName) return nullptr;
    if (g_adapter_class_loader && g_load_class_method) {
        jstring jName = env->NewStringUTF(binaryName);
        jobject cls = env->CallObjectMethod(g_adapter_class_loader,
                                            g_load_class_method, jName);
        env->DeleteLocalRef(jName);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return nullptr;
        }
        if (cls) return reinterpret_cast<jclass>(cls);
    }
    // Fallback: FindClass with '/' separator
    std::string jniName(binaryName);
    for (char& c : jniName) if (c == '.') c = '/';
    jclass cls = env->FindClass(jniName.c_str());
    if (env->ExceptionCheck()) env->ExceptionClear();
    return cls;
}

// ==================== AppSpawnXInit.nativeHiLog (PathClassLoader, best-effort) ====================
//
// 2026-04-29 B.32: direct HiLogPrint bridge for Java side.  Android Log →
// liblog.so → hilog bridge constructor path is broken in spawned child
// (B.31 verified: zero D002000 entries in child hilog).  This native bypasses
// all of that and calls HiLogPrint directly.  Lets us see Java init
// checkpoints without depending on the liblog.so → hilog bridge state
// surviving fork.  Filter via:
//     hdc shell hilog | grep "AppSpawnXJava"
//
// 2026-04-29 B.34.1: registered via RegisterNatives because AppSpawnXInit
// lives in oh-adapter-runtime.jar (PathClassLoader, non-BCP).  ART's
// auto-dlsym JNI lookup is classloader-scoped: a native method on a
// PathClassLoader class is searched in libraries loaded by the same
// PathClassLoader (or its ancestors via parent delegation).  bridge.so,
// bound to bootstrap, is NOT in PathClassLoader's library set, so any
// Java_com_android_internal_os_AppSpawnXInit_nativeHiLog symbol exported
// here would still auto-dlsym fail.  RegisterNatives bypasses scoping.
//
// Best-effort: if FindClass returns null at JNI_OnLoad (PathClassLoader not
// yet created), registration silently skips and appLog falls back to
// System.err in that window.  appspawnx_runtime.cpp's B.35.A path (PathClass-
// Loader-aware retry) re-registers via dlsym of the symbol below, so the
// impl is exposed with extern "C" linkage as `adapter_appspawnx_native_hilog`
// (same naming style as adapter_bridge_set_class_loader / load_class) — see
// framework/appspawn-x/src/appspawnx_runtime.cpp B.35.A path:
//     dlsym(bridge, "adapter_appspawnx_native_hilog")

// Defined in activity_task_manager_adapter.cpp. Android seeds this service in
// every application process before app code runs; appspawn-x has the same
// stable point immediately after installing its real IServiceManager adapter.
extern bool wl_prime_webview_update_service(JNIEnv* env);
extern bool wl_publish_webview_update_service_after_bind(JNIEnv* env);

// -1: startup has not installed OHServiceManager yet, 0: ready/retry,
// 1: priming (also suppresses recursive System.err -> nativeHiLog calls),
// 2: verified for this forked child.
static std::atomic<int> g_webview_service_prime_state{-1};
static std::atomic<int> g_webview_bind_watcher_pid{0};
static std::atomic<int> g_app_boundary_native_state{0};

static jint install_app_boundary_natives(JNIEnv* env, jobject app_class_loader);

static jobject find_bound_app_class_loader(JNIEnv* env, jclass activity_thread) {
    jmethodID current_thread = env->GetStaticMethodID(
            activity_thread, "currentActivityThread", "()Landroid/app/ActivityThread;");
    if (current_thread == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }
    jobject thread = env->CallStaticObjectMethod(activity_thread, current_thread);
    if (thread == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (thread != nullptr) env->DeleteLocalRef(thread);
        return nullptr;
    }

    jfieldID bound_field = env->GetFieldID(
            activity_thread, "mBoundApplication",
            "Landroid/app/ActivityThread$AppBindData;");
    jobject bound = bound_field != nullptr ? env->GetObjectField(thread, bound_field) : nullptr;
    env->DeleteLocalRef(thread);
    if (bound == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (bound != nullptr) env->DeleteLocalRef(bound);
        return nullptr;
    }

    jclass bound_class = env->GetObjectClass(bound);
    jfieldID info_field = bound_class != nullptr
            ? env->GetFieldID(bound_class, "info", "Landroid/app/LoadedApk;")
            : nullptr;
    jobject loaded_apk = info_field != nullptr ? env->GetObjectField(bound, info_field) : nullptr;
    if (bound_class != nullptr) env->DeleteLocalRef(bound_class);
    env->DeleteLocalRef(bound);
    if (loaded_apk == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (loaded_apk != nullptr) env->DeleteLocalRef(loaded_apk);
        return nullptr;
    }

    jclass loaded_apk_class = env->GetObjectClass(loaded_apk);
    jmethodID get_class_loader = loaded_apk_class != nullptr
            ? env->GetMethodID(loaded_apk_class, "getClassLoader", "()Ljava/lang/ClassLoader;")
            : nullptr;
    jobject class_loader = get_class_loader != nullptr
            ? env->CallObjectMethod(loaded_apk, get_class_loader)
            : nullptr;
    if (loaded_apk_class != nullptr) env->DeleteLocalRef(loaded_apk_class);
    env->DeleteLocalRef(loaded_apk);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (class_loader != nullptr) env->DeleteLocalRef(class_loader);
        return nullptr;
    }
    return class_loader;
}

static void* webview_bind_watcher(void*) {
    JavaVM* vm = g_adapter_jvm;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm == nullptr ||
            (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK &&
             (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || env == nullptr))) {
        fprintf(stderr, "[WESTLAKE-WEBVIEW-STARTUP] bind watcher attach failed\n");
        fflush(stderr);
        return nullptr;
    }
    attached = true;

    // Republish only the ServiceManager cache after Android exposes its
    // Application. This watcher deliberately does not publish the WebView
    // feature bit: mInitialApplication is stored before Application.onCreate,
    // and enabling WebView there moves Chromium initialization into the cold
    // bind path. The explicit post-bind checkpoint handles feature publication.
    for (int attempt = 0; attempt < 15000; ++attempt) {
        jclass activityThread = env->FindClass("android/app/ActivityThread");
        jobject appClassLoader = activityThread != nullptr
            ? find_bound_app_class_loader(env, activityThread)
            : nullptr;
        if (appClassLoader != nullptr &&
                g_app_boundary_native_state.load(std::memory_order_acquire) != 2) {
            const jint installed = install_app_boundary_natives(env, appClassLoader);
            g_app_boundary_native_state.store(installed != 0 ? 2 : 1,
                                              std::memory_order_release);
            if (installed != 0) {
                fprintf(stderr,
                        "[APP-BOUNDARY] app ClassLoader natives installed=%d attempt=%d\n",
                        installed, attempt);
                fflush(stderr);
            }
        }
        if (appClassLoader != nullptr) env->DeleteLocalRef(appClassLoader);
        jmethodID currentApplication = activityThread != nullptr
            ? env->GetStaticMethodID(activityThread, "currentApplication",
                                     "()Landroid/app/Application;")
            : nullptr;
        jobject application = currentApplication != nullptr
            ? env->CallStaticObjectMethod(activityThread, currentApplication)
            : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (activityThread != nullptr) env->DeleteLocalRef(activityThread);

        if (application != nullptr) {
            env->DeleteLocalRef(application);
            const bool success = wl_prime_webview_update_service(env);
            g_webview_service_prime_state.store(success ? 2 : 0,
                                                std::memory_order_release);
            fprintf(stderr,
                    "[WESTLAKE-WEBVIEW-STARTUP] application-ready cache-republish=%s attempt=%d\n",
                    success ? "OK" : "RETRY", attempt);
            fflush(stderr);
            if (success) break;
        }
        usleep(attempt < 2000 ? 1000 : 20000);
    }

    if (attached) vm->DetachCurrentThread();
    return nullptr;
}

extern "C" void adapter_start_webview_bind_watcher() {
    const int current_pid = static_cast<int>(getpid());
    const int previous_pid = g_webview_bind_watcher_pid.exchange(
        current_pid, std::memory_order_acq_rel);
    if (previous_pid == current_pid) return;
    pthread_t thread;
    const int rc = pthread_create(&thread, nullptr, webview_bind_watcher, nullptr);
    if (rc == 0) {
        pthread_detach(thread);
    } else {
        g_webview_bind_watcher_pid.store(0, std::memory_order_release);
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-STARTUP] bind watcher pthread_create failed rc=%d\n",
                rc);
        fflush(stderr);
    }
}

static void maybe_prime_webview_service(JNIEnv* env, const char* message) {
    if (message == nullptr) return;
    const bool service_manager_ready =
        std::strstr(message, "J_after_installServiceManagerAdapter") != nullptr;
    const bool application_bound =
        std::strstr(message, "[B43-BIND] handleBindApplication returned OK") != nullptr ||
        std::strstr(message, "[DIRECT-LAUNCH] bind done sBindAppDone=true") != nullptr;
    if (service_manager_ready || application_bound) {
        g_webview_service_prime_state.store(0, std::memory_order_release);
    }
    if ((std::strstr(message, "J_after_") == nullptr && !application_bound) ||
            g_webview_service_prime_state.load(std::memory_order_acquire) < 0 ||
            g_webview_service_prime_state.load(std::memory_order_acquire) == 2) {
        return;
    }

    int expected = 0;
    if (!g_webview_service_prime_state.compare_exchange_strong(
            expected, 1, std::memory_order_acq_rel)) {
        return;
    }

    // Before bind, only seed the service cache and pin WebView unsupported so
    // Application.onCreate() cannot start Chromium without a usable
    // Application context. At the explicit post-bind checkpoint, publish the
    // feature as well: Toutiao asks for WebView while constructing its first
    // Activity, before a ViewRoot window exists.
    const bool success = application_bound
        ? wl_publish_webview_update_service_after_bind(env)
        : wl_prime_webview_update_service(env);
    g_webview_service_prime_state.store(success ? 2 : 0,
                                        std::memory_order_release);
    fprintf(stderr,
            "[WESTLAKE-WEBVIEW-STARTUP] checkpoint=%s result=%s\n",
            message, success ? "OK" : "RETRY");
    fflush(stderr);
    if (service_manager_ready) adapter_start_webview_bind_watcher();
}

extern "C" JNIEXPORT void JNICALL adapter_appspawnx_native_hilog(
        JNIEnv* env, jclass /*clazz*/, jstring jtag, jstring jmsg) {
    const char* tag = jtag ? env->GetStringUTFChars(jtag, nullptr) : "Java";
    const char* msg = jmsg ? env->GetStringUTFChars(jmsg, nullptr) : "(null)";
    HiLogPrint(B32_LOG_TYPE_CORE, B32_LOG_INFO, B32_LOG_DOMAIN,
               tag ? tag : "Java", "%{public}s", msg ? msg : "(null)");
    maybe_prime_webview_service(env, msg);
    if (jtag && tag) env->ReleaseStringUTFChars(jtag, tag);
    if (jmsg && msg) env->ReleaseStringUTFChars(jmsg, msg);
}

namespace {

int register_AppSpawnXInit(JNIEnv* env) {
    jclass cls = env->FindClass("com/android/internal/os/AppSpawnXInit");
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (cls == nullptr) {
        ZYG_LOG_ERR("[B34.1] FindClass(AppSpawnXInit) returned null at JNI_OnLoad "
                    "— appLog will fall back to stderr until later resolution");
        return JNI_ERR;
    }
    JNINativeMethod m = {
        const_cast<char*>("nativeHiLog"),
        const_cast<char*>("(Ljava/lang/String;Ljava/lang/String;)V"),
        reinterpret_cast<void*>(adapter_appspawnx_native_hilog)
    };
    jint rc = env->RegisterNatives(cls, &m, 1);
    env->DeleteLocalRef(cls);
    if (rc != JNI_OK) {
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        ZYG_LOG_ERR("[B34.1] RegisterNatives(AppSpawnXInit.nativeHiLog) failed rc=%{public}d", rc);
    } else {
        ZYG_LOG_INFO("[B34.1] RegisterNatives(AppSpawnXInit.nativeHiLog) OK");
    }
    return rc;
}

}  // namespace

// Forward decl for the 6 BCP-class register functions defined in sibling cpp files
extern int register_OHEnvironment(JNIEnv*);
extern int register_ActivityManagerAdapter(JNIEnv*);
extern int register_ActivityTaskManagerAdapter(JNIEnv*);
extern int register_WindowManagerAdapter(JNIEnv*);
extern int register_WindowSessionAdapter(JNIEnv*);
extern int register_InputEventBridge(JNIEnv*);
extern int register_InputMethodBridge(JNIEnv*);
extern int register_DisplayManagerAdapter(JNIEnv*);
extern int register_InputManagerAdapter(JNIEnv*);

extern "C" jstring
Java_adapter_packagemanager_PackageManagerAdapter_nativeGetBundleInfo(
        JNIEnv*, jclass, jstring, jint);
extern "C" jstring
Java_adapter_packagemanager_PackageManagerAdapter_nativeGetApplicationInfo(
        JNIEnv*, jclass, jstring, jint);
extern "C" jstring
Java_adapter_packagemanager_PackageManagerAdapter_nativeGetAllBundleInfos(
        JNIEnv*, jclass, jint);
extern "C" jstring
Java_adapter_packagemanager_PackageManagerAdapter_nativeQueryAbilityInfos(
        JNIEnv*, jclass, jstring, jint);
extern "C" jint
Java_adapter_packagemanager_PackageManagerAdapter_nativeGetUidByBundleName(
        JNIEnv*, jclass, jstring);
extern "C" jint
Java_adapter_packagemanager_PackageManagerAdapter_nativeCheckPermission(
        JNIEnv*, jclass, jstring, jstring);
extern "C" jstring
Java_adapter_packagemanager_PackageManagerAdapter_nativeParseApkManifestJson(
        JNIEnv*, jclass, jstring);
extern "C" jstring
Java_adapter_packagemanager_PackageManagerAdapter_nativeGetOhSystemProperty(
        JNIEnv*, jclass, jstring, jstring);

static int register_PackageManagerAdapter(JNIEnv* env) {
    jclass clazz = env->FindClass("adapter/packagemanager/PackageManagerAdapter");
    if (clazz == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        ZYG_LOG_ERR("RegisterNatives(PackageManagerAdapter): class not found");
        return JNI_ERR;
    }
    const JNINativeMethod methods[] = {
        {const_cast<char*>("nativeGetBundleInfo"),
         const_cast<char*>("(Ljava/lang/String;I)Ljava/lang/String;"),
         reinterpret_cast<void*>(Java_adapter_packagemanager_PackageManagerAdapter_nativeGetBundleInfo)},
        {const_cast<char*>("nativeGetApplicationInfo"),
         const_cast<char*>("(Ljava/lang/String;I)Ljava/lang/String;"),
         reinterpret_cast<void*>(Java_adapter_packagemanager_PackageManagerAdapter_nativeGetApplicationInfo)},
        {const_cast<char*>("nativeGetAllBundleInfos"),
         const_cast<char*>("(I)Ljava/lang/String;"),
         reinterpret_cast<void*>(Java_adapter_packagemanager_PackageManagerAdapter_nativeGetAllBundleInfos)},
        {const_cast<char*>("nativeQueryAbilityInfos"),
         const_cast<char*>("(Ljava/lang/String;I)Ljava/lang/String;"),
         reinterpret_cast<void*>(Java_adapter_packagemanager_PackageManagerAdapter_nativeQueryAbilityInfos)},
        {const_cast<char*>("nativeGetUidByBundleName"),
         const_cast<char*>("(Ljava/lang/String;)I"),
         reinterpret_cast<void*>(Java_adapter_packagemanager_PackageManagerAdapter_nativeGetUidByBundleName)},
        {const_cast<char*>("nativeCheckPermission"),
         const_cast<char*>("(Ljava/lang/String;Ljava/lang/String;)I"),
         reinterpret_cast<void*>(Java_adapter_packagemanager_PackageManagerAdapter_nativeCheckPermission)},
        {const_cast<char*>("nativeParseApkManifestJson"),
         const_cast<char*>("(Ljava/lang/String;)Ljava/lang/String;"),
         reinterpret_cast<void*>(Java_adapter_packagemanager_PackageManagerAdapter_nativeParseApkManifestJson)},
        {const_cast<char*>("nativeGetOhSystemProperty"),
         const_cast<char*>("(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"),
         reinterpret_cast<void*>(Java_adapter_packagemanager_PackageManagerAdapter_nativeGetOhSystemProperty)},
    };
    const jint rc = env->RegisterNatives(
            clazz, methods, sizeof(methods) / sizeof(methods[0]));
    env->DeleteLocalRef(clazz);
    if (rc != JNI_OK) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        ZYG_LOG_ERR("RegisterNatives(PackageManagerAdapter) failed rc=%{public}d", rc);
    } else {
        ZYG_LOG_INFO("RegisterNatives(PackageManagerAdapter) OK (%{public}zu methods)",
                     sizeof(methods) / sizeof(methods[0]));
    }
    return rc;
}

// ---- Audio native stubs (Unity/AudioTrack init hits unimplemented AudioSystem/AudioTrack
// natives → UnsatisfiedLinkError thrown through libunity's native audio init, which can leave a
// libunity pthread mutex locked → UnityPlayer ctor deadlocks). Provide sane-default stubs so
// audio init doesn't throw. Static methods: (JNIEnv*, jclass[, args]). ----
namespace {
jint AS_i_void(JNIEnv*, jclass) { return 0; }
jint AS_maxChan(JNIEnv*, jclass) { return 8; }       // native_getMaxChannelCount (FCC_8)
jint AS_maxRate(JNIEnv*, jclass) { return 192000; }  // native_getMaxSampleRate
jint AS_minRate(JNIEnv*, jclass) { return 4000; }    // native_getMinSampleRate
jint AS_primRate(JNIEnv*, jclass) { return 48000; }  // getPrimaryOutputSamplingRate
jint AS_primFrames(JNIEnv*, jclass) { return 1024; } // getPrimaryOutputFrameCount
jint AS_outLatency(JNIEnv*, jclass, jint) { return 0; }              // getOutputLatency(I)
jint AS_deviceConnectionState(JNIEnv*, jclass, jint, jstring) {
    // This boundary has no Android audio-policy inventory.  Report the queried
    // route as unavailable, matching the empty AudioPort/AudioPatch lists
    // instead of throwing during player initialization.
    return 0;  // AudioSystem.DEVICE_STATE_UNAVAILABLE
}
jint AT_minBuf(JNIEnv*, jclass, jint, jint, jint) { return 8192; }   // native_get_min_buff_size(III)
jint AT_outRate(JNIEnv*, jclass, jint) { return 48000; }            // native_get_output_sample_rate(I)
jint AS_newSessionId(JNIEnv*, jclass) { static int s = 1000; return ++s; }  // AudioSystem.newAudioSessionId() -> unique >0
static void reg(JNIEnv* env, const char* cls, const char* name, const char* sig, void* fn) {
    jclass c = env->FindClass(cls);
    if (!c) { if (env->ExceptionCheck()) env->ExceptionClear(); return; }
    JNINativeMethod m = { const_cast<char*>(name), const_cast<char*>(sig), fn };
    env->RegisterNatives(c, &m, 1);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(c);
}
}  // namespace
// [FIX-AUDIO 2026-06-30] real AudioTrack -> OH_AudioRenderer shim (oh_audiotrack_shim.cpp)
extern "C" int register_AudioTrack_shim(JNIEnv* env);
// [FIX-AUDIO 2026-07-02] MediaCodec (MP3 decode) -> OH_AudioCodec shim (oh_mediacodec_shim.cpp)
extern "C" int register_MediaCodec_shim(JNIEnv* env);
static int register_Audio_fixup(JNIEnv* env) {
    reg(env, "android/media/AudioSystem", "native_getMaxChannelCount", "()I", (void*)AS_maxChan);
    reg(env, "android/media/AudioSystem", "native_getMaxSampleRate", "()I", (void*)AS_maxRate);
    reg(env, "android/media/AudioSystem", "native_getMinSampleRate", "()I", (void*)AS_minRate);
    reg(env, "android/media/AudioSystem", "getPrimaryOutputSamplingRate", "()I", (void*)AS_primRate);
    reg(env, "android/media/AudioSystem", "getPrimaryOutputFrameCount", "()I", (void*)AS_primFrames);
    reg(env, "android/media/AudioSystem", "getOutputLatency", "(I)I", (void*)AS_outLatency);
    reg(env, "android/media/AudioSystem", "getDeviceConnectionState", "(ILjava/lang/String;)I",
        (void*)AS_deviceConnectionState);
    reg(env, "android/media/AudioSystem", "newAudioSessionId", "()I", (void*)AS_newSessionId);
    reg(env, "android/media/AudioTrack", "native_get_min_buff_size", "(III)I", (void*)AT_minBuf);
    reg(env, "android/media/AudioTrack", "native_get_output_sample_rate", "(I)I", (void*)AT_outRate);
    ZYG_LOG_INFO("[AUDIO-FIX] AudioSystem/AudioTrack native stubs registered");
    register_AudioTrack_shim(env);   // overrides min_buff_size stub + adds setup/start/write/...
    return 0;
}

// ==================== JNI_OnLoad ====================

// [PROXY 2026-07-01] Opt-in: if /data/local/tmp/oh_proxy exists (contents
// "host:port"), point this app's JVM HTTP(S) proxy at it so ExoPlayer's
// DefaultHttpDataSource (HttpURLConnection) tunnels to the host CONNECT proxy
// (hdc rport). The proxy resolves hostnames host-side, so no device DNS is
// needed. Gated by the file so normal boots (no proxy running) are unaffected.
static void oh_set_proxy_props(JNIEnv* env) {
    FILE* f = fopen("/data/local/tmp/oh_proxy", "r");
    if (!f) return;
    char host[64] = {0}, port[16] = {0};
    int n = fscanf(f, "%63[^:]:%15s", host, port);
    fclose(f);
    if (n != 2) return;
    jclass sys = env->FindClass("java/lang/System");
    if (!sys) { if (env->ExceptionCheck()) env->ExceptionClear(); return; }
    jmethodID setProp = env->GetStaticMethodID(sys, "setProperty",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!setProp) { if (env->ExceptionCheck()) env->ExceptionClear(); return; }
    const char* hkeys[] = {"https.proxyHost", "http.proxyHost"};
    const char* pkeys[] = {"https.proxyPort", "http.proxyPort"};
    jstring jhost = env->NewStringUTF(host);
    jstring jport = env->NewStringUTF(port);
    for (int i = 0; i < 2; i++) {
        jstring k = env->NewStringUTF(hkeys[i]);
        jobject r = env->CallStaticObjectMethod(sys, setProp, k, jhost);
        if (r) env->DeleteLocalRef(r);
        env->DeleteLocalRef(k);
        jstring pk = env->NewStringUTF(pkeys[i]);
        jobject r2 = env->CallStaticObjectMethod(sys, setProp, pk, jport);
        if (r2) env->DeleteLocalRef(r2);
        env->DeleteLocalRef(pk);
    }
    env->DeleteLocalRef(jhost); env->DeleteLocalRef(jport);
    if (env->ExceptionCheck()) env->ExceptionClear();
    ZYG_LOG_INFO("[PROXY] JVM http(s) proxy -> %s:%s", host, port);
}

// Toutiao's metasec library normally registers ms.bd.c.m.a from JNI_OnLoad.
// On this boundary that library's environment-integrity path aborts before the
// registration completes.  Its APK contains the matching Java dispatcher in
// ms.bd.c.b.a with the same signature; forward to that implementation so the
// caller retains the app's operation-specific return types and exceptions.
extern "C" JNIEXPORT jobject JNICALL Java_ms_bd_c_m_a(
        JNIEnv* env, jclass, jint op, jint arg, jlong value,
        jstring text, jobject payload) {
    jclass dispatcher = env->FindClass("ms/bd/c/b");
    if (dispatcher == nullptr) {
        ZYG_LOG_ERR("[METASEC-BRIDGE] missing Java dispatcher for op=%{public}d", op);
        return nullptr;
    }

    jmethodID dispatch = env->GetStaticMethodID(
        dispatcher, "a",
        "(IIJLjava/lang/String;Ljava/lang/Object;)Ljava/lang/Object;");
    if (dispatch == nullptr) {
        ZYG_LOG_ERR("[METASEC-BRIDGE] missing dispatch method for op=%{public}d", op);
        env->DeleteLocalRef(dispatcher);
        return nullptr;
    }

    jobject result = env->CallStaticObjectMethod(
        dispatcher, dispatch, op, arg, value, text, payload);
    env->DeleteLocalRef(dispatcher);
    ZYG_LOG_INFO("[METASEC-BRIDGE] dispatched op=%{public}d arg=%{public}d exception=%{public}d",
                 op, arg, env->ExceptionCheck() ? 1 : 0);
    return result;
}

// Called at the framework's pre-bind point with the real APK ClassLoader.
// RegisterNatives is the canonical way for a boot-owned boundary library to
// provide an implementation to a class owned by an application ClassLoader;
// no ART-wide or process-global JNI fallback is needed.
static jint install_app_boundary_natives(JNIEnv* env, jobject appClassLoader) {
    if (appClassLoader == nullptr) return 0;
    const char* configured = std::getenv("WESTLAKE_APP_BOUNDARY_NATIVES");
    if (configured == nullptr || std::strstr(configured, "metasec") == nullptr) return 0;

    jclass loaderClass = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = loaderClass != nullptr
            ? env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;")
            : nullptr;
    if (loadClass == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (loaderClass != nullptr) env->DeleteLocalRef(loaderClass);
        return 0;
    }

    jstring name = env->NewStringUTF("ms.bd.c.m");
    auto target = reinterpret_cast<jclass>(
            env->CallObjectMethod(appClassLoader, loadClass, name));
    env->DeleteLocalRef(name);
    env->DeleteLocalRef(loaderClass);
    if (target == nullptr || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (target != nullptr) env->DeleteLocalRef(target);
        return 0;
    }

    const JNINativeMethod method = {
        const_cast<char*>("a"),
        const_cast<char*>("(IIJLjava/lang/String;Ljava/lang/Object;)Ljava/lang/Object;"),
        reinterpret_cast<void*>(Java_ms_bd_c_m_a),
    };
    const jint result = env->RegisterNatives(target, &method, 1);
    env->DeleteLocalRef(target);
    if (result != JNI_OK && env->ExceptionCheck()) env->ExceptionClear();
    return result == JNI_OK ? 1 : 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_adapter_activity_AppSchedulerBridge_nativeInstallAppBoundaryNatives(
        JNIEnv* env, jclass, jobject appClassLoader) {
    return install_app_boundary_natives(env, appClassLoader);
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    ZYG_LOG_INFO("[B34.1] JNI_OnLoad: oh_adapter_bridge entered");
    AdapterBridge::getInstance().setJavaVM(vm);
    // The bridge can start its control/input thread before OHEnvironment.init
    // runs. Publish the VM at library load as well so a post-fork child can
    // attach that thread for view-tree and input dispatch immediately.
    OHInputBridge::getInstance().setJavaVM(vm);

    // Bridge to OH libskia_canvaskit's SkCodecs::Register — without this the
    // 4-arg SkCodec::MakeFromStream wrapper sees an empty decoder list and
    // returns kUnimplemented for every PNG/JPEG/WEBP load (HelloWorld
    // setContentView -> ImageDecoder failure root cause).
    #if !defined(__aarch64__)
    adapter::RegisterAllSkiaCodecs();
    #else
    { const char m[]="[JNIONLOAD] skipped skia\n"; write(2,m,sizeof(m)-1); }
    #endif

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    // Best-effort: AppSpawnXInit is a PathClassLoader class (non-BCP). If it's
    // not loaded yet, register_AppSpawnXInit logs and returns; appLog falls
    // back to System.err in that window.  Logged but not fatal.
    register_AppSpawnXInit(env);

    // BCP classes: all of these live in oh-adapter-framework.jar which is
    // loaded into the boot classpath before this library; FindClass must
    // succeed.  Any rc != JNI_OK is a programmer error (signature mismatch
    // most likely) and logged as ERROR inside each register_* function.
    register_OHEnvironment(env);
    register_ActivityManagerAdapter(env);
    register_ActivityTaskManagerAdapter(env);
    register_WindowManagerAdapter(env);
    register_WindowSessionAdapter(env);
    register_InputEventBridge(env);
    register_InputMethodBridge(env);
    if (register_PackageManagerAdapter(env) != JNI_OK) return JNI_ERR;
    if (register_DisplayManagerAdapter(env) != JNI_OK) return JNI_ERR;
    if (register_InputManagerAdapter(env) != JNI_OK) return JNI_ERR;
    register_Audio_fixup(env);
    register_MediaCodec_shim(env);

    oh_set_proxy_props(env);

    return JNI_VERSION_1_6;
}
