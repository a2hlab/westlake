/*
 * activity_task_manager_adapter.cpp
 *
 * JNI registration for adapter.activity.ActivityTaskManagerAdapter via
 * RegisterNatives.  Replaces the legacy
 * Java_adapter_bridge_ActivityTaskManagerAdapter_* exports that previously
 * lived in framework/core/jni/adapter_bridge.cpp.
 *
 * Class:  adapter/activity/ActivityTaskManagerAdapter  (BCP - oh-adapter-framework.jar)
 * Registered from adapter_bridge.cpp's JNI_OnLoad via
 *   register_ActivityTaskManagerAdapter(env).
 *
 * 8 natives: 2 generic (GetService + StartAbility) + 6 mission stack ops.
 * The 6 mission natives previously had no adapter_activity_* forwarder and
 * raised UnsatisfiedLinkError on first call from bridgeStartActivityWithStack —
 * see 2026-05-19 helloworld pid 2340 crash (build_patch_log entry).
 */

#include "oh_ability_manager_client.h"

#include <android/log.h>
#include <jni.h>
#include <string>
#include <mutex>
#include <unistd.h>

#define TAG "OH_ATMJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

using namespace oh_adapter;

namespace {

std::string jstr(JNIEnv* env, jstring s) {
    if (!s) return "";
    const char* raw = env->GetStringUTFChars(s, nullptr);
    std::string result(raw);
    env->ReleaseStringUTFChars(s, raw);
    return result;
}

static std::string wl_object_class_name(JNIEnv* env, jobject object) {
    if (object == nullptr) return "<null>";
    jclass objectClass = env->GetObjectClass(object);
    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID getName = classClass != nullptr
        ? env->GetMethodID(classClass, "getName", "()Ljava/lang/String;") : nullptr;
    jstring name = (objectClass != nullptr && getName != nullptr)
        ? static_cast<jstring>(env->CallObjectMethod(objectClass, getName)) : nullptr;
    std::string result = (!env->ExceptionCheck() && name != nullptr)
        ? jstr(env, name) : "<unknown>";
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (name != nullptr) env->DeleteLocalRef(name);
    if (classClass != nullptr) env->DeleteLocalRef(classClass);
    if (objectClass != nullptr) env->DeleteLocalRef(objectClass);
    return result;
}

static std::string jsonQuote(const std::string& value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (unsigned char c : value) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 0x20) {
                    result += "\\u00";
                    result.push_back(kHex[c >> 4]);
                    result.push_back(kHex[c & 0x0f]);
                } else {
                    result.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    result.push_back('"');
    return result;
}

// AppSchedulerBridge.buildIntentFromWant() consumes the OH Want.ToJson shape.
// The JNI caller already received every field from IntentWantConverter, so keep
// that data intact when the BMS-free direct-launch path loops back into Android.
static std::string makeDirectLaunchWantJson(const WantParams& want) {
    std::string result = "{\"bundleName\":" + jsonQuote(want.bundleName)
            + ",\"abilityName\":" + jsonQuote(want.abilityName);
    if (!want.action.empty()) {
        result += ",\"action\":" + jsonQuote(want.action);
    }
    if (!want.uri.empty()) {
        result += ",\"uri\":" + jsonQuote(want.uri);
    }
    if (!want.parametersJson.empty()) {
        // Want.ToJson represents parameters as a JSON-encoded string. Keeping
        // it quoted also makes malformed app data unable to corrupt the outer
        // envelope; AppSchedulerBridge defensively parses the inner object.
        result += ",\"parameters\":" + jsonQuote(want.parametersJson);
    }
    result.push_back('}');
    return result;
}

// WESTLAKE §686 (2026-08-19): make Toutiao's system-WebView wrapper publish a
// real AOSP provider before a BMS-free secondary Activity is launched.
//
// Toutiao installs TTWebProviderWrapper into WebViewFactory.sProviderInstance,
// then obtains the underlying system provider with
//
//   WebViewFactory.getProviderClass()
//   providerClass.getMethod("create", WebViewDelegate.class).invoke(...)
//
// The first half works on this port (the WebView APK and Chromium library are
// both loaded), but this deployed T provider exposes the legacy
// (WebViewDelegate) constructor rather than the static create(WebViewDelegate)
// method Toutiao's O+ branch assumes.  That branch catches NoSuchMethodError and
// returns null.  TTWebProviderWrapper then creates a dynamic WebViewProvider
// proxy whose invocation target is null; NewDetailActivity dies at
// WebView.<init>.  Use the static factory when present and the provider's real
// constructor otherwise, exactly as Toutiao's own pre-O compatibility branch
// does.  This helper is deliberately scoped to the in-process OH activity
// boundary: it is a no-op unless the app class and wrapper instance both exist,
// and it never changes WebViewFactory.sProviderInstance.
static void wl_log_and_clear_webview_exception(JNIEnv* env, const char* where) {
    jthrowable thrown = env->ExceptionOccurred();
    if (thrown == nullptr) return;
    env->ExceptionClear();

    std::string detail("<unprintable>");
    jclass thrownClass = env->GetObjectClass(thrown);
    jmethodID toString = thrownClass != nullptr
        ? env->GetMethodID(thrownClass, "toString", "()Ljava/lang/String;") : nullptr;
    if (!env->ExceptionCheck() && toString != nullptr) {
        jstring text = static_cast<jstring>(env->CallObjectMethod(thrown, toString));
        if (!env->ExceptionCheck() && text != nullptr) {
            detail = jstr(env, text);
            env->DeleteLocalRef(text);
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    fprintf(stderr, "[WESTLAKE-WEBVIEW-PROVIDER] %s threw %s\n",
            where, detail.c_str());
    fflush(stderr);
    if (thrownClass != nullptr) env->DeleteLocalRef(thrownClass);
    env->DeleteLocalRef(thrown);
}

static bool wl_set_webview_supported_cache(
        JNIEnv* env, jclass webViewFactoryClass, bool supported) {
    // The application-ready watcher can arrive after post-bind publication.
    // Serialize this state transition, and never let cache-only priming undo
    // a successfully verified publication. Reset the latch for each fork.
    static std::mutex featureMutex;
    static pid_t featurePid = 0;
    static bool published = false;
    std::lock_guard<std::mutex> lock(featureMutex);
    if (featurePid != getpid()) { featurePid = getpid(); published = false; }
    supported = supported || published;
    jclass booleanClass = env->FindClass("java/lang/Boolean");
    const char* booleanFieldName = supported ? "TRUE" : "FALSE";
    jfieldID booleanField = booleanClass != nullptr
        ? env->GetStaticFieldID(booleanClass, booleanFieldName,
                                "Ljava/lang/Boolean;")
        : nullptr;
    jfieldID supportedField = webViewFactoryClass != nullptr
        ? env->GetStaticFieldID(webViewFactoryClass, "sWebViewSupported",
                                "Ljava/lang/Boolean;")
        : nullptr;
    jobject booleanValue = booleanField != nullptr
        ? env->GetStaticObjectField(booleanClass, booleanField)
        : nullptr;
    if (env->ExceptionCheck() || booleanClass == nullptr ||
            booleanField == nullptr || supportedField == nullptr ||
            booleanValue == nullptr) {
        wl_log_and_clear_webview_exception(
            env, "WebViewFactory.sWebViewSupported lookup");
        if (booleanValue != nullptr) env->DeleteLocalRef(booleanValue);
        if (booleanClass != nullptr) env->DeleteLocalRef(booleanClass);
        return false;
    }

    env->SetStaticObjectField(webViewFactoryClass, supportedField, booleanValue);
    jobject readback = env->GetStaticObjectField(
        webViewFactoryClass, supportedField);
    const bool success = !env->ExceptionCheck() && readback != nullptr &&
                         env->IsSameObject(readback, booleanValue);
    if (success && supported) published = true;
    if (!success && env->ExceptionCheck()) {
        wl_log_and_clear_webview_exception(
            env, "WebViewFactory.sWebViewSupported publish");
    }
    if (readback != nullptr) env->DeleteLocalRef(readback);
    env->DeleteLocalRef(booleanValue);
    env->DeleteLocalRef(booleanClass);
    return success;
}

// Android normally seeds ServiceManager's per-process cache from SystemServer
// before application code can construct a WebView.  The standalone OH child
// installs OHServiceManager, but a direct secondary-Activity launch can reach
// WebViewFactory without a cached webviewupdate binder.  In that state this
// framework's getUpdateServiceUnchecked() returns null and the unmodified
// WebViewFactory immediately dereferences it in waitForAndGetProvider().
//
// Seed the same ServiceManager cache entry Android supplies at bind time.  The
// binder and PackageInfo remain owned by the generic OH service/package
// adapters; this code neither invents provider metadata nor bypasses
// WebViewFactory's package verification and provider-loading path.
static bool wl_prime_webview_update_service_impl(
        JNIEnv* env, bool publishFeatureAfterBind) {
    jclass updateClass = env->FindClass(
        "adapter/core/WebViewUpdateServiceAdapter");
    if (env->ExceptionCheck() || updateClass == nullptr) {
        wl_log_and_clear_webview_exception(env, "WebViewUpdateServiceAdapter lookup");
        return false;
    }

    jmethodID isAvailable = env->GetStaticMethodID(
        updateClass, "isAvailable", "()Z");
    jboolean available = isAvailable != nullptr
        ? env->CallStaticBooleanMethod(updateClass, isAvailable) : JNI_FALSE;
    if (env->ExceptionCheck()) {
        wl_log_and_clear_webview_exception(env, "WebViewUpdateServiceAdapter.isAvailable");
        env->DeleteLocalRef(updateClass);
        return false;
    }
    if (available != JNI_TRUE) {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-PROVIDER] configured provider unavailable\n");
        fflush(stderr);
        env->DeleteLocalRef(updateClass);
        return false;
    }

    jmethodID getInstance = env->GetStaticMethodID(
        updateClass, "getInstance",
        "()Ladapter/core/WebViewUpdateServiceAdapter;");
    jobject updateService = getInstance != nullptr
        ? env->CallStaticObjectMethod(updateClass, getInstance) : nullptr;
    if (env->ExceptionCheck() || updateService == nullptr) {
        wl_log_and_clear_webview_exception(env, "WebViewUpdateServiceAdapter.getInstance");
        env->DeleteLocalRef(updateClass);
        return false;
    }

    jclass serviceManagerClass = env->FindClass("android/os/ServiceManager");
    jclass webViewFactoryClass = env->FindClass("android/webkit/WebViewFactory");
    jfieldID cacheField = serviceManagerClass != nullptr
        ? env->GetStaticFieldID(serviceManagerClass, "sCache", "Ljava/util/Map;")
        : nullptr;
    jfieldID serviceNameField = webViewFactoryClass != nullptr
        ? env->GetStaticFieldID(webViewFactoryClass,
                                "WEBVIEW_UPDATE_SERVICE_NAME",
                                "Ljava/lang/String;")
        : nullptr;
    jmethodID publicGetService = serviceManagerClass != nullptr
        ? env->GetStaticMethodID(serviceManagerClass, "getService",
                                 "(Ljava/lang/String;)Landroid/os/IBinder;")
        : nullptr;
    jobject cache = cacheField != nullptr
        ? env->GetStaticObjectField(serviceManagerClass, cacheField) : nullptr;
    // Resolve put/get on the concrete cache class (normally ArrayMap).  This
    // avoids invoke-interface dispatch here: older ART bridge builds have been
    // observed to return normally from Map.put without changing this object.
    // Always read the entry back before reporting that the cache was seeded.
    jclass cacheClass = cache != nullptr ? env->GetObjectClass(cache) : nullptr;
    jmethodID put = cacheClass != nullptr
        ? env->GetMethodID(cacheClass, "put",
                           "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;")
        : nullptr;
    jmethodID get = cacheClass != nullptr
        ? env->GetMethodID(cacheClass, "get",
                           "(Ljava/lang/Object;)Ljava/lang/Object;")
        : nullptr;
    // Use the exact String object consumed by WebViewFactory. This port has
    // had ART String/ArrayMap dispatch defects where put/get with the same
    // freshly allocated key succeeds but a later equal String misses. A
    // same-object cache check therefore did not prove the framework lookup.
    jstring serviceName = serviceNameField != nullptr
        ? static_cast<jstring>(env->GetStaticObjectField(webViewFactoryClass,
                                                         serviceNameField))
        : nullptr;
    if (env->ExceptionCheck() || cache == nullptr || put == nullptr || get == nullptr ||
            serviceName == nullptr || publicGetService == nullptr) {
        wl_log_and_clear_webview_exception(env, "ServiceManager.sCache lookup");
        if (serviceName != nullptr) env->DeleteLocalRef(serviceName);
        if (cacheClass != nullptr) env->DeleteLocalRef(cacheClass);
        if (cache != nullptr) env->DeleteLocalRef(cache);
        if (webViewFactoryClass != nullptr) env->DeleteLocalRef(webViewFactoryClass);
        if (serviceManagerClass != nullptr) env->DeleteLocalRef(serviceManagerClass);
        env->DeleteLocalRef(updateService);
        env->DeleteLocalRef(updateClass);
        return false;
    }

    jobject previous = env->CallObjectMethod(
        cache, put, serviceName, updateService);
    const char* previousState = previous == nullptr ? "null"
        : (env->IsSameObject(previous, updateService) ? "same" : "other");
    jobject readback = nullptr;
    jobject publicReadback = nullptr;
    bool cacheSuccess = !env->ExceptionCheck();
    if (cacheSuccess) {
        readback = env->CallObjectMethod(cache, get, serviceName);
        cacheSuccess = !env->ExceptionCheck() && readback != nullptr &&
                       env->IsSameObject(readback, updateService);
    }
    if (cacheSuccess) {
        publicReadback = env->CallStaticObjectMethod(
            serviceManagerClass, publicGetService, serviceName);
        cacheSuccess = !env->ExceptionCheck() && publicReadback != nullptr &&
                       env->IsSameObject(publicReadback, updateService);
    }
    if (!cacheSuccess && env->ExceptionCheck()) {
        wl_log_and_clear_webview_exception(env, "ServiceManager.sCache.put");
    }
    if (cacheSuccess) {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-PROVIDER] primed ServiceManager webviewupdate cache public-readback=OK\n");
        fflush(stderr);
    } else {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-PROVIDER] failed ServiceManager webviewupdate cache readback\n");
        fflush(stderr);
    }

    // Toutiao asks CookieManager for an instance while LoadedApk is still
    // constructing its Application.  At that point AppGlobals has not yet
    // published ActivityThread.mInitialApplication, so WebViewFactory's normal
    // feature query observes a null PackageManager and permanently caches
    // Boolean.FALSE in sWebViewSupported.  Do not force TRUE at this early
    // checkpoint: doing so would let provider creation continue without an
    // Application context. Only the explicit post-bind caller requests feature
    // publication, and only after the configured provider's isAvailable()
    // check above succeeded.
    bool featureSuccess = true;
    const char* featureState = "held-unsupported";
    jclass activityThreadClass = nullptr;
    jobject application = nullptr;
    if (!publishFeatureAfterBind) {
        // OH child construction is not yet a valid WebView context. Pin the
        // same cached false that WebViewFactory would compute at this stage,
        // but make its lifetime explicit: the post-bind callback below
        // replaces it with TRUE. This prevents a later CookieManager retry
        // during Application.onCreate from starting Chromium on the bind path.
        featureSuccess = wl_set_webview_supported_cache(
            env, webViewFactoryClass, false);
        if (!featureSuccess) featureState = "hold-failed";
    } else {
        featureState = "application-pending";
        activityThreadClass = env->FindClass("android/app/ActivityThread");
        jmethodID currentApplication = activityThreadClass != nullptr
            ? env->GetStaticMethodID(activityThreadClass, "currentApplication",
                                     "()Landroid/app/Application;")
            : nullptr;
        application = currentApplication != nullptr
            ? env->CallStaticObjectMethod(activityThreadClass, currentApplication)
            : nullptr;
        if (env->ExceptionCheck() || activityThreadClass == nullptr ||
                currentApplication == nullptr) {
            wl_log_and_clear_webview_exception(
                env, "ActivityThread.currentApplication");
            featureSuccess = false;
            featureState = "application-check-failed";
        } else if (application == nullptr) {
            // A publish request is valid only at the explicit post-bind
            // checkpoint. Treat an absent Application as retryable failure;
            // never turn it into an early TRUE feature bit.
            featureSuccess = false;
        } else {
            featureSuccess = wl_set_webview_supported_cache(
                env, webViewFactoryClass, true);
            featureState = featureSuccess ? "published" : "publish-failed";
        }
    }

    fprintf(stderr,
            "[WESTLAKE-WEBVIEW-FEATURE] application=%s feature=%s\n",
            application != nullptr ? "ready" : "pending", featureState);
    fflush(stderr);

    // Cache priming is independently useful before bind. When the post-bind
    // caller requests feature publication, that publication is part of the
    // success invariant and may be retried by the checkpoint path.
    const bool success = cacheSuccess && featureSuccess;
    if (success) {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-PROVIDER] startup service cache=OK previous=%s\n",
                previousState);
        fflush(stderr);
    } else {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-PROVIDER] startup service cache failed\n");
        fflush(stderr);
    }

    if (publicReadback != nullptr) env->DeleteLocalRef(publicReadback);
    if (application != nullptr) env->DeleteLocalRef(application);
    if (activityThreadClass != nullptr) env->DeleteLocalRef(activityThreadClass);
    if (readback != nullptr) env->DeleteLocalRef(readback);
    if (previous != nullptr) env->DeleteLocalRef(previous);
    env->DeleteLocalRef(serviceName);
    env->DeleteLocalRef(cacheClass);
    env->DeleteLocalRef(cache);
    env->DeleteLocalRef(webViewFactoryClass);
    env->DeleteLocalRef(serviceManagerClass);
    env->DeleteLocalRef(updateService);
    env->DeleteLocalRef(updateClass);
    return success;
}

// Continue WebViewFactory's upstream provider-loading sequence from the
// selected OH update-service object.  This is used only when the factory's
// getUpdateServiceUnchecked() loses the local Stub object in bytecode
// Stub.asInterface dispatch.  Provider selection, PackageInfo construction,
// APK code/resources, and Chromium initialization still come from the generic
// WebView service/package boundary; no app URL or content is substituted.
static jclass wl_get_webview_provider_class_direct(
        JNIEnv* env, jobject application, jclass factoryClass) {
    auto fail = [env](const char* where) -> jclass {
        if (env->ExceptionCheck()) {
            wl_log_and_clear_webview_exception(env, where);
        } else {
            fprintf(stderr, "[WESTLAKE-WEBVIEW-PROVIDER] %s returned null\n", where);
            fflush(stderr);
        }
        return nullptr;
    };

    jclass updateClass = env->FindClass("adapter/core/WebViewUpdateServiceAdapter");
    jmethodID getInstance = updateClass != nullptr
        ? env->GetStaticMethodID(updateClass, "getInstance",
                                 "()Ladapter/core/WebViewUpdateServiceAdapter;")
        : nullptr;
    jobject updateService = getInstance != nullptr
        ? env->CallStaticObjectMethod(updateClass, getInstance) : nullptr;
    if (env->ExceptionCheck() || updateService == nullptr) {
        return fail("direct WebViewUpdateServiceAdapter.getInstance");
    }

    jmethodID waitForProvider = env->GetMethodID(
        updateClass, "waitForAndGetProvider",
        "()Landroid/webkit/WebViewProviderResponse;");
    jobject response = waitForProvider != nullptr
        ? env->CallObjectMethod(updateService, waitForProvider) : nullptr;
    if (env->ExceptionCheck() || response == nullptr) {
        return fail("direct waitForAndGetProvider");
    }

    jclass responseClass = env->FindClass("android/webkit/WebViewProviderResponse");
    jfieldID statusField = responseClass != nullptr
        ? env->GetFieldID(responseClass, "status", "I") : nullptr;
    jfieldID packageInfoField = responseClass != nullptr
        ? env->GetFieldID(responseClass, "packageInfo",
                          "Landroid/content/pm/PackageInfo;") : nullptr;
    jint status = statusField != nullptr ? env->GetIntField(response, statusField) : -1;
    jobject packageInfo = packageInfoField != nullptr
        ? env->GetObjectField(response, packageInfoField) : nullptr;
    if (env->ExceptionCheck() || status != 0 || packageInfo == nullptr) {
        return fail("direct WebViewProviderResponse");
    }

    jclass packageInfoClass = env->FindClass("android/content/pm/PackageInfo");
    jfieldID applicationInfoField = packageInfoClass != nullptr
        ? env->GetFieldID(packageInfoClass, "applicationInfo",
                          "Landroid/content/pm/ApplicationInfo;") : nullptr;
    jobject applicationInfo = applicationInfoField != nullptr
        ? env->GetObjectField(packageInfo, applicationInfoField) : nullptr;
    if (env->ExceptionCheck() || applicationInfo == nullptr) {
        return fail("direct provider ApplicationInfo");
    }

    jclass applicationClass = env->GetObjectClass(application);
    jmethodID createApplicationContext = applicationClass != nullptr
        ? env->GetMethodID(applicationClass, "createApplicationContext",
                           "(Landroid/content/pm/ApplicationInfo;I)Landroid/content/Context;")
        : nullptr;
    jobject webViewContext = createApplicationContext != nullptr
        ? env->CallObjectMethod(application, createApplicationContext,
                                applicationInfo, 3 /* INCLUDE_CODE | IGNORE_SECURITY */)
        : nullptr;
    if (env->ExceptionCheck() || webViewContext == nullptr) {
        return fail("direct createApplicationContext");
    }

    jfieldID loadedPackageInfo = env->GetStaticFieldID(
        factoryClass, "sPackageInfo", "Landroid/content/pm/PackageInfo;");
    if (env->ExceptionCheck() || loadedPackageInfo == nullptr) {
        return fail("WebViewFactory.sPackageInfo lookup");
    }
    env->SetStaticObjectField(factoryClass, loadedPackageInfo, packageInfo);
    if (env->ExceptionCheck()) {
        return fail("WebViewFactory.sPackageInfo publish");
    }

    // Match WebViewFactory.getProviderClass(): make the provider's resource
    // table visible to the initial app before initializing its Java class.
    jclass contextClass = env->FindClass("android/content/Context");
    jmethodID getAssets = contextClass != nullptr
        ? env->GetMethodID(contextClass, "getAssets",
                           "()Landroid/content/res/AssetManager;") : nullptr;
    jobject assets = getAssets != nullptr
        ? env->CallObjectMethod(application, getAssets) : nullptr;
    jclass applicationInfoClass = env->FindClass("android/content/pm/ApplicationInfo");
    jfieldID sourceDirField = applicationInfoClass != nullptr
        ? env->GetFieldID(applicationInfoClass, "sourceDir", "Ljava/lang/String;") : nullptr;
    jstring sourceDir = sourceDirField != nullptr
        ? static_cast<jstring>(env->GetObjectField(applicationInfo, sourceDirField)) : nullptr;
    jclass assetManagerClass = env->FindClass("android/content/res/AssetManager");
    jmethodID addSharedAsset = assetManagerClass != nullptr
        ? env->GetMethodID(assetManagerClass, "addAssetPathAsSharedLibrary",
                           "(Ljava/lang/String;)I") : nullptr;
    jint assetCookie = (assets != nullptr && sourceDir != nullptr && addSharedAsset != nullptr)
        ? env->CallIntMethod(assets, addSharedAsset, sourceDir) : 0;
    if (env->ExceptionCheck() || assetCookie == 0) {
        return fail("provider addAssetPathAsSharedLibrary");
    }

    jmethodID getClassLoader = contextClass != nullptr
        ? env->GetMethodID(contextClass, "getClassLoader",
                           "()Ljava/lang/ClassLoader;") : nullptr;
    jobject classLoader = getClassLoader != nullptr
        ? env->CallObjectMethod(webViewContext, getClassLoader) : nullptr;
    if (env->ExceptionCheck() || classLoader == nullptr) {
        return fail("provider Context.getClassLoader");
    }

    // Upstream ignores the integer load result and lets provider startup load
    // without a shared RELRO when zygote reservation is unavailable.  Preserve
    // that behavior, but surface exceptions and continue to class loading.
    jmethodID getLibrary = env->GetStaticMethodID(
        factoryClass, "getWebViewLibrary",
        "(Landroid/content/pm/ApplicationInfo;)Ljava/lang/String;");
    jstring library = getLibrary != nullptr
        ? static_cast<jstring>(env->CallStaticObjectMethod(
              factoryClass, getLibrary, applicationInfo)) : nullptr;
    jclass loaderClass = env->FindClass("android/webkit/WebViewLibraryLoader");
    jmethodID loadNative = loaderClass != nullptr
        ? env->GetStaticMethodID(loaderClass, "loadNativeLibrary",
                                 "(Ljava/lang/ClassLoader;Ljava/lang/String;)I")
        : nullptr;
    jint loadResult = -1;
    if (!env->ExceptionCheck() && loadNative != nullptr && library != nullptr) {
        loadResult = env->CallStaticIntMethod(loaderClass, loadNative, classLoader, library);
    }
    if (env->ExceptionCheck()) {
        wl_log_and_clear_webview_exception(env, "WebViewLibraryLoader.loadNativeLibrary");
    }

    jmethodID getProviderClass = env->GetStaticMethodID(
        factoryClass, "getWebViewProviderClass",
        "(Ljava/lang/ClassLoader;)Ljava/lang/Class;");
    jclass providerClass = getProviderClass != nullptr
        ? static_cast<jclass>(env->CallStaticObjectMethod(
              factoryClass, getProviderClass, classLoader)) : nullptr;
    if (env->ExceptionCheck() || providerClass == nullptr) {
        return fail("WebViewFactory.getWebViewProviderClass direct");
    }

    fprintf(stderr,
            "[WESTLAKE-WEBVIEW-PROVIDER] direct provider class ready asset=%d native=%d\n",
            assetCookie, loadResult);
    fflush(stderr);
    return providerClass;
}

static jclass wl_load_webview_provider_compat_class(
        JNIEnv* env, jclass providerClass) {
    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID getClassLoader = classClass != nullptr
        ? env->GetMethodID(classClass, "getClassLoader",
                           "()Ljava/lang/ClassLoader;") : nullptr;
    jobject providerLoader = getClassLoader != nullptr
        ? env->CallObjectMethod(providerClass, getClassLoader) : nullptr;
    if (env->ExceptionCheck() || providerLoader == nullptr) {
        wl_log_and_clear_webview_exception(env, "provider ClassLoader lookup");
        return nullptr;
    }

    jclass loaderClass = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = loaderClass != nullptr
        ? env->GetMethodID(loaderClass, "loadClass",
                           "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    jstring compatName = env->NewStringUTF(
        "com.android.webview.chromium.WebViewChromiumFactoryProviderCompat");
    jclass compatClass = (loadClass != nullptr && compatName != nullptr)
        ? static_cast<jclass>(env->CallObjectMethod(
              providerLoader, loadClass, compatName))
        : nullptr;
    if (compatName != nullptr) env->DeleteLocalRef(compatName);
    if (env->ExceptionCheck() || compatClass == nullptr) {
        // Older, unpatched WebView packages remain usable for normal WebViews.
        if (env->ExceptionCheck()) env->ExceptionClear();
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-PROVIDER] compat provider class unavailable\n");
        fflush(stderr);
        compatClass = nullptr;
    } else {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-PROVIDER] compat provider class ready\n");
        fflush(stderr);
    }
    if (providerLoader != nullptr) env->DeleteLocalRef(providerLoader);
    if (loaderClass != nullptr) env->DeleteLocalRef(loaderClass);
    if (classClass != nullptr) env->DeleteLocalRef(classClass);
    return compatClass;
}

static void wl_publish_toutiao_system_provider_state(
        JNIEnv* env, jclass wrapperClass, jobject wrapper,
        jobject appClassLoader, jmethodID loadClass, jobject provider) {
    // DedicatedWebView probes the optional three-argument createWebView method
    // through the wrapper's TT-provider slot even when Toutiao selected
    // SYSTEMWEBVIEW.  Publish the compatibility provider in that slot while
    // keeping the wrapper's load result explicitly on SYSTEMWEBVIEW.
    jfieldID ttProviderField = env->GetFieldID(
        wrapperClass, "a", "Landroid/webkit/WebViewFactoryProvider;");
    if (env->ExceptionCheck() || ttProviderField == nullptr) {
        wl_log_and_clear_webview_exception(env, "TTWebProviderWrapper.a lookup");
        return;
    }

    jstring loadResultName = env->NewStringUTF("X.TJ0");
    jclass loadResultClass = (loadClass != nullptr && loadResultName != nullptr)
        ? static_cast<jclass>(env->CallObjectMethod(
              appClassLoader, loadClass, loadResultName))
        : nullptr;
    if (loadResultName != nullptr) env->DeleteLocalRef(loadResultName);
    if (env->ExceptionCheck() || loadResultClass == nullptr) {
        wl_log_and_clear_webview_exception(env, "system WebView load result lookup");
        return;
    }
    jmethodID loadResultCtor = env->GetMethodID(loadResultClass, "<init>", "()V");
    jobject loadResult = loadResultCtor != nullptr
        ? env->NewObject(loadResultClass, loadResultCtor) : nullptr;
    jfieldID loadResultField = env->GetFieldID(
        wrapperClass, "h", "LX/TJ0;");
    if (env->ExceptionCheck() || loadResult == nullptr || loadResultField == nullptr) {
        wl_log_and_clear_webview_exception(env, "system WebView load result create");
        if (loadResultClass != nullptr) env->DeleteLocalRef(loadResultClass);
        return;
    }

    // TJ0.i() broadcasts load-result changes to this wrapper while holding the
    // TJ0 class monitor.  Once the standalone bridge has selected the usable
    // system provider, a later TTWebView result can race activity launch and
    // replace both fields below with an unavailable TT glue proxy.  Remove
    // this wrapper from that observer queue under the same monitor before the
    // final publication.  This preserves the app's chosen SYSTEMWEBVIEW state
    // across the gap between scheduling a secondary Activity and constructing
    // its DedicatedWebView.
    jboolean observerRemoved = JNI_FALSE;
    bool loadResultMonitorHeld = env->MonitorEnter(loadResultClass) == JNI_OK;
    if (loadResultMonitorHeld) {
        jfieldID observerQueueField = env->GetStaticFieldID(
            loadResultClass, "e", "Ljava/util/concurrent/ConcurrentLinkedQueue;");
        jobject observerQueue = (!env->ExceptionCheck() && observerQueueField != nullptr)
            ? env->GetStaticObjectField(loadResultClass, observerQueueField) : nullptr;
        jclass observerQueueClass = observerQueue != nullptr
            ? env->GetObjectClass(observerQueue) : nullptr;
        jmethodID removeObserver = observerQueueClass != nullptr
            ? env->GetMethodID(observerQueueClass, "remove", "(Ljava/lang/Object;)Z")
            : nullptr;
        if (!env->ExceptionCheck() && removeObserver != nullptr) {
            observerRemoved = env->CallBooleanMethod(
                observerQueue, removeObserver, wrapper);
        }
        if (env->ExceptionCheck()) {
            wl_log_and_clear_webview_exception(
                env, "system WebView observer removal");
        }
        if (observerQueueClass != nullptr) env->DeleteLocalRef(observerQueueClass);
        if (observerQueue != nullptr) env->DeleteLocalRef(observerQueue);
    }

    env->SetObjectField(wrapper, loadResultField, loadResult);
    env->SetObjectField(wrapper, ttProviderField, provider);
    if (env->ExceptionCheck()) {
        wl_log_and_clear_webview_exception(env, "Toutiao system provider state publish");
    } else {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-PROVIDER] published system load result and dedicated compat provider observerRemoved=%d\n",
                observerRemoved == JNI_TRUE ? 1 : 0);
        fflush(stderr);
    }
    if (loadResultMonitorHeld) env->MonitorExit(loadResultClass);
    env->DeleteLocalRef(loadResult);
    env->DeleteLocalRef(loadResultClass);
}

static void wl_stabilize_direct_launch_webview_provider(JNIEnv* env) {
    jclass activityThreadClass = env->FindClass("android/app/ActivityThread");
    jmethodID currentApplication = activityThreadClass != nullptr
        ? env->GetStaticMethodID(activityThreadClass, "currentApplication",
                                 "()Landroid/app/Application;") : nullptr;
    jobject application = currentApplication != nullptr
        ? env->CallStaticObjectMethod(activityThreadClass, currentApplication) : nullptr;
    if (env->ExceptionCheck() || application == nullptr) {
        wl_log_and_clear_webview_exception(env, "currentApplication");
        return;
    }

    jclass applicationClass = env->GetObjectClass(application);
    jmethodID getClassLoader = applicationClass != nullptr
        ? env->GetMethodID(applicationClass, "getClassLoader",
                           "()Ljava/lang/ClassLoader;") : nullptr;
    jobject classLoader = getClassLoader != nullptr
        ? env->CallObjectMethod(application, getClassLoader) : nullptr;
    if (env->ExceptionCheck() || classLoader == nullptr) {
        wl_log_and_clear_webview_exception(env, "Application.getClassLoader");
        return;
    }

    jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = classLoaderClass != nullptr
        ? env->GetMethodID(classLoaderClass, "loadClass",
                           "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    jstring wrapperName = env->NewStringUTF(
        "com.bytedance.lynx.webview.glue.TTWebProviderWrapper");
    jclass wrapperClass = (loadClass != nullptr && wrapperName != nullptr)
        ? static_cast<jclass>(env->CallObjectMethod(classLoader, loadClass, wrapperName))
        : nullptr;
    if (wrapperName != nullptr) env->DeleteLocalRef(wrapperName);
    if (env->ExceptionCheck() || wrapperClass == nullptr) {
        // Expected for noice, Material Catalog, and every non-Toutiao app.
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }

    jfieldID systemProviderField = env->GetStaticFieldID(
        wrapperClass, "i", "Landroid/webkit/WebViewFactoryProvider;");
    if (env->ExceptionCheck() || systemProviderField == nullptr) {
        wl_log_and_clear_webview_exception(env, "TTWebProviderWrapper.i lookup");
        return;
    }
    jclass factoryClass = env->FindClass("android/webkit/WebViewFactory");
    jfieldID providerInstanceField = factoryClass != nullptr
        ? env->GetStaticFieldID(factoryClass, "sProviderInstance",
                                "Landroid/webkit/WebViewFactoryProvider;") : nullptr;
    jobject wrapper = providerInstanceField != nullptr
        ? env->GetStaticObjectField(factoryClass, providerInstanceField) : nullptr;
    if (env->ExceptionCheck() || wrapper == nullptr ||
            !env->IsInstanceOf(wrapper, wrapperClass)) {
        wl_log_and_clear_webview_exception(env, "WebViewFactory wrapper lookup");
        fprintf(stderr, "[WESTLAKE-WEBVIEW-PROVIDER] no installed Toutiao wrapper\n");
        fflush(stderr);
        return;
    }

    // TTWebProviderWrapper.getSystemProvider() synchronizes on this same object.
    // Taking its monitor prevents another startup thread from racing a second
    // provider creation while we perform and publish the direct JNI call.
    if (env->MonitorEnter(wrapper) != JNI_OK) {
        fprintf(stderr, "[WESTLAKE-WEBVIEW-PROVIDER] wrapper MonitorEnter failed\n");
        fflush(stderr);
        return;
    }

    jobject existing = env->GetStaticObjectField(wrapperClass, systemProviderField);
    if (existing != nullptr) {
        // A non-null system-provider cache is not sufficient.  Toutiao's O+
        // SysProviderCreator assumes a static create(WebViewDelegate) factory,
        // while this T provider uses the constructor form.  DedicatedWebView
        // also requires a declared three-argument createWebView overload.  Reuse
        // the cache only when it is already the compatibility subclass shipped
        // in the WebView APK; otherwise construct that subclass below and
        // replace the incompatible cached instance.
        jclass existingClass = env->GetObjectClass(existing);
        jmethodID dedicatedCreate = existingClass != nullptr
            ? env->GetMethodID(
                  existingClass, "createWebView",
                  "(Landroid/webkit/WebView;Landroid/webkit/WebView$PrivateAccess;Z)"
                  "Landroid/webkit/WebViewProvider;")
            : nullptr;
        if (env->ExceptionCheck()) {
            // The unadapted ForT provider does not expose this optional
            // ByteDance contract.  Clear only that method-lookup failure so
            // the constructor compatibility path below remains available.
            env->ExceptionClear();
            dedicatedCreate = nullptr;
        }
        const bool compatible = dedicatedCreate != nullptr;
        const std::string existingClassName = wl_object_class_name(env, existing);
        if (compatible) {
            wl_publish_toutiao_system_provider_state(
                env, wrapperClass, wrapper, classLoader, loadClass, existing);
            env->MonitorExit(wrapper);
            fprintf(stderr,
                    "[WESTLAKE-WEBVIEW-PROVIDER] repaired compatible initialized provider state class=%s\n",
                    existingClassName.c_str());
            fflush(stderr);
            env->DeleteLocalRef(existingClass);
            env->DeleteLocalRef(existing);
            return;
        }
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-PROVIDER] replacing incompatible initialized provider class=%s\n",
                existingClassName.c_str());
        fflush(stderr);
        if (existingClass != nullptr) env->DeleteLocalRef(existingClass);
        env->DeleteLocalRef(existing);
    }

    wl_prime_webview_update_service_impl(env, true);

    jobject provider = nullptr;
    jmethodID getProviderClass = env->GetStaticMethodID(
        factoryClass, "getProviderClass", "()Ljava/lang/Class;");
    jclass providerClass = getProviderClass != nullptr
        ? static_cast<jclass>(env->CallStaticObjectMethod(factoryClass, getProviderClass))
        : nullptr;
    if (env->ExceptionCheck() || providerClass == nullptr) {
        wl_log_and_clear_webview_exception(env, "WebViewFactory.getProviderClass");
        providerClass = wl_get_webview_provider_class_direct(
            env, application, factoryClass);
    } else {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-PROVIDER] stock getProviderClass succeeded\n");
        fflush(stderr);
    }
    if (providerClass != nullptr) {
        jclass compatProviderClass =
            wl_load_webview_provider_compat_class(env, providerClass);
        jclass implementationClass = compatProviderClass != nullptr
            ? compatProviderClass : providerClass;
        jclass delegateClass = env->FindClass("android/webkit/WebViewDelegate");
        jmethodID delegateCtor = delegateClass != nullptr
            ? env->GetMethodID(delegateClass, "<init>", "()V") : nullptr;
        jobject delegate = delegateCtor != nullptr
            ? env->NewObject(delegateClass, delegateCtor) : nullptr;
        // The compatibility subclass must be constructed directly so the
        // object keeps its declared three-argument createWebView overload.
        jmethodID create = compatProviderClass == nullptr
            ? env->GetStaticMethodID(
                  providerClass, "create",
                  "(Landroid/webkit/WebViewDelegate;)Landroid/webkit/WebViewFactoryProvider;")
            : nullptr;
        if (env->ExceptionCheck()) {
            // WebViewChromiumFactoryProviderForT on this board uses the
            // constructor form. GetStaticMethodID reports NoSuchMethodError;
            // clear only that lookup failure and try the stock constructor.
            env->ExceptionClear();
            create = nullptr;
        }
        if (delegate != nullptr && create != nullptr) {
            provider = env->CallStaticObjectMethod(providerClass, create, delegate);
            if (env->ExceptionCheck()) {
                // Some provider builds retain the static factory for ABI
                // compatibility but reject it at runtime.  That is not a
                // reason to abandon the stock constructor contract below.
                // Clear the factory failure before looking up/invoking the
                // constructor; otherwise JNI leaves the thread in an
                // exception-pending state and every fallback operation fails.
                wl_log_and_clear_webview_exception(
                    env, "providerClass.create; trying constructor");
                provider = nullptr;
            }
        }
        if (delegate != nullptr && provider == nullptr) {
            jmethodID providerCtor = env->GetMethodID(
                implementationClass, "<init>",
                "(Landroid/webkit/WebViewDelegate;)V");
            if (env->ExceptionCheck()) {
                wl_log_and_clear_webview_exception(
                    env, "provider constructor lookup");
            } else if (providerCtor != nullptr) {
                provider = env->NewObject(
                    implementationClass, providerCtor, delegate);
                if (env->ExceptionCheck()) {
                    wl_log_and_clear_webview_exception(
                        env, "provider constructor invocation");
                    provider = nullptr;
                } else if (provider != nullptr) {
                    fprintf(stderr,
                            "[WESTLAKE-WEBVIEW-PROVIDER] used provider constructor fallback\n");
                    fflush(stderr);
                }
            }
        }
        if (compatProviderClass != nullptr) {
            env->DeleteLocalRef(compatProviderClass);
        }
        if (delegate != nullptr) env->DeleteLocalRef(delegate);
        if (delegateClass != nullptr) env->DeleteLocalRef(delegateClass);
    }

    if (provider != nullptr) {
        env->SetStaticObjectField(wrapperClass, systemProviderField, provider);
        if (env->ExceptionCheck()) {
            wl_log_and_clear_webview_exception(env, "TTWebProviderWrapper.i publish");
        } else {
            wl_publish_toutiao_system_provider_state(
                env, wrapperClass, wrapper, classLoader, loadClass, provider);
            fprintf(stderr,
                    "[WESTLAKE-WEBVIEW-PROVIDER] stock provider created and published\n");
            fflush(stderr);
        }
        env->DeleteLocalRef(provider);
    } else {
        fprintf(stderr,
                "[WESTLAKE-WEBVIEW-PROVIDER] stock provider creation returned null\n");
        fflush(stderr);
    }
    env->MonitorExit(wrapper);
}

// -------- generic (2) --------

jlong nativeGetOHAbilityManagerService_impl(JNIEnv*, jclass) {
    return (jlong)&OHAbilityManagerClient::getInstance();
}

// WESTLAKE §736 (2026-08-19): restore the AOSP main-Looper invariant at the
// BMS-free OH -> Android secondary-Activity boundary.
//
// ActivityThread.main() prepares the process main Looper normally, and the
// direct-launch watcher observes it before launching the first Activity.  A
// later Toutiao launch nevertheless measured Looper.getMainLooper() == null
// while constructing its video Fragment, even though the lifecycle callback
// was executing on the live Android UI Looper.  AOSP keeps that process-wide
// reference in Looper.sMainLooper (guarded by Looper.class); it does not create
// a second Looper.  Preserve those semantics here: only in the existing
// ASX_DIRECT_LAUNCH path, and only when the static reference is null while the
// current launch thread already owns a Looper, publish that exact Looper under
// the same class monitor.  If this boundary is ever reached off a Looper
// thread, log the state and leave it untouched rather than blessing the wrong
// thread as main.
static void wl_restore_direct_launch_main_looper(JNIEnv* env) {
    jclass looperClass = env->FindClass("android/os/Looper");
    if (looperClass == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        fprintf(stderr, "[WESTLAKE-MAINLOOPER-736] Looper class unavailable\n");
        fflush(stderr);
        return;
    }

    jmethodID getMainLooper = env->GetStaticMethodID(
        looperClass, "getMainLooper", "()Landroid/os/Looper;");
    jmethodID myLooper = env->GetStaticMethodID(
        looperClass, "myLooper", "()Landroid/os/Looper;");
    if (getMainLooper == nullptr || myLooper == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        fprintf(stderr, "[WESTLAKE-MAINLOOPER-736] Looper methods unavailable\n");
        fflush(stderr);
        env->DeleteLocalRef(looperClass);
        return;
    }

    jobject main = env->CallStaticObjectMethod(looperClass, getMainLooper);
    jobject mine = env->CallStaticObjectMethod(looperClass, myLooper);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        fprintf(stderr, "[WESTLAKE-MAINLOOPER-736] Looper state query threw\n");
        fflush(stderr);
        if (main != nullptr) env->DeleteLocalRef(main);
        if (mine != nullptr) env->DeleteLocalRef(mine);
        env->DeleteLocalRef(looperClass);
        return;
    }

    std::string threadName("<unknown>");
    jclass threadClass = env->FindClass("java/lang/Thread");
    jmethodID currentThread = threadClass != nullptr
        ? env->GetStaticMethodID(threadClass, "currentThread", "()Ljava/lang/Thread;")
        : nullptr;
    jmethodID getName = threadClass != nullptr
        ? env->GetMethodID(threadClass, "getName", "()Ljava/lang/String;") : nullptr;
    jobject thread = currentThread != nullptr
        ? env->CallStaticObjectMethod(threadClass, currentThread) : nullptr;
    jstring name = (thread != nullptr && getName != nullptr)
        ? static_cast<jstring>(env->CallObjectMethod(thread, getName)) : nullptr;
    if (!env->ExceptionCheck() && name != nullptr) threadName = jstr(env, name);
    if (env->ExceptionCheck()) env->ExceptionClear();

    const bool sameBefore = main != nullptr && mine != nullptr
        && env->IsSameObject(main, mine);
    bool restored = false;
    if (main == nullptr && mine != nullptr) {
        jfieldID mainField = env->GetStaticFieldID(
            looperClass, "sMainLooper", "Landroid/os/Looper;");
        if (mainField != nullptr && !env->ExceptionCheck()
                && env->MonitorEnter(looperClass) == JNI_OK) {
            jobject racedMain = env->GetStaticObjectField(looperClass, mainField);
            if (racedMain == nullptr && !env->ExceptionCheck()) {
                env->SetStaticObjectField(looperClass, mainField, mine);
                restored = !env->ExceptionCheck();
            }
            if (racedMain != nullptr) env->DeleteLocalRef(racedMain);
            env->MonitorExit(looperClass);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    jobject after = env->CallStaticObjectMethod(looperClass, getMainLooper);
    if (env->ExceptionCheck()) env->ExceptionClear();
    const bool sameAfter = after != nullptr && mine != nullptr
        && env->IsSameObject(after, mine);
    fprintf(stderr,
            "[WESTLAKE-MAINLOOPER-736] thread=%s main=%p mine=%p "
            "sameBefore=%d restored=%d after=%p sameAfter=%d\n",
            threadName.c_str(), main, mine, sameBefore ? 1 : 0,
            restored ? 1 : 0, after, sameAfter ? 1 : 0);
    fflush(stderr);

    if (after != nullptr) env->DeleteLocalRef(after);
    if (name != nullptr) env->DeleteLocalRef(name);
    if (thread != nullptr) env->DeleteLocalRef(thread);
    if (threadClass != nullptr) env->DeleteLocalRef(threadClass);
    if (main != nullptr) env->DeleteLocalRef(main);
    if (mine != nullptr) env->DeleteLocalRef(mine);
    env->DeleteLocalRef(looperClass);
}

// WESTLAKE 2026-07-22: drive a DL2-launched Activity to onResume.
// Mirrors AppSchedulerBridge.directResume(int), which is private and hardcoded to recordId 1.
// Without this the Activity is created but never resumed, so handleResumeActivity -> addView
// never runs and the screen stays black. All-JNI so no adapter jar / boot image rebuild.
static void wl_dl2_resume(JNIEnv* env, int rec) {
    jclass atCls = env->FindClass("android/app/ActivityThread");
    jclass regCls = env->FindClass("adapter/core/OhTokenRegistry");
    if (atCls == nullptr || regCls == nullptr) { env->ExceptionClear();
        fprintf(stderr, "[WESTLAKE-DL2RESUME] class lookup failed\n"); fflush(stderr); return; }
    jmethodID curAT = env->GetStaticMethodID(atCls, "currentActivityThread",
                                             "()Landroid/app/ActivityThread;");
    jmethodID getAppThread = env->GetMethodID(atCls, "getApplicationThread",
                                             "()Landroid/app/ActivityThread$ApplicationThread;");
    jmethodID findTok = env->GetStaticMethodID(regCls, "findByRecordId",
                                               "(I)Landroid/os/IBinder;");
    if (curAT == nullptr || getAppThread == nullptr || findTok == nullptr) { env->ExceptionClear();
        fprintf(stderr, "[WESTLAKE-DL2RESUME] method lookup failed\n"); fflush(stderr); return; }
    jobject at = env->CallStaticObjectMethod(atCls, curAT);
    jobject appThread = (at != nullptr) ? env->CallObjectMethod(at, getAppThread) : nullptr;
    jobject token = env->CallStaticObjectMethod(regCls, findTok, (jint) rec);
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    if (appThread == nullptr || token == nullptr) {
        fprintf(stderr, "[WESTLAKE-DL2RESUME] no appThread/token for recordId=%d\n", rec);
        fflush(stderr); return;
    }
    jclass txCls  = env->FindClass("android/app/servertransaction/ClientTransaction");
    jclass resCls = env->FindClass("android/app/servertransaction/ResumeActivityItem");
    if (txCls == nullptr || resCls == nullptr) { env->ExceptionClear();
        fprintf(stderr, "[WESTLAKE-DL2RESUME] transaction classes missing\n"); fflush(stderr); return; }
    // Android 15 moved the activity token out of ClientTransaction and into each
    // item: obtain() takes only the client, and ResumeActivityItem/ActivityResultItem
    // take the token as their first argument. GetStaticMethodID raises
    // NoSuchMethodError for a signature that does not exist rather than returning
    // null, so keeping the pre-15 shapes here aborted the runtime.
    jmethodID txObtain = env->GetStaticMethodID(txCls, "obtain",
        "(Landroid/app/IApplicationThread;)"
        "Landroid/app/servertransaction/ClientTransaction;");
    jmethodID resObtain = env->GetStaticMethodID(resCls, "obtain",
        "(Landroid/os/IBinder;ZZ)Landroid/app/servertransaction/ResumeActivityItem;");
    if (txObtain == nullptr || resObtain == nullptr) { env->ExceptionClear();
        fprintf(stderr, "[WESTLAKE-DL2RESUME] obtain() lookup failed\n"); fflush(stderr); return; }
    jobject tx  = env->CallStaticObjectMethod(txCls, txObtain, appThread);
    jobject res = env->CallStaticObjectMethod(resCls, resObtain, token, JNI_TRUE, JNI_FALSE);
    jmethodID setLifecycle = env->GetMethodID(txCls, "setLifecycleStateRequest",
        "(Landroid/app/servertransaction/ActivityLifecycleItem;)V");
    if (tx == nullptr || res == nullptr || setLifecycle == nullptr) { env->ExceptionClear();
        fprintf(stderr, "[WESTLAKE-DL2RESUME] transaction build failed\n"); fflush(stderr); return; }

    // WESTLAKE §683 (2026-08-19): keep the direct-launch resume transaction
    // structurally compatible with the AOSP transactions seen by app hooks.
    //
    // A lifecycle-only ClientTransaction legitimately has a null callbacks
    // list. Toutiao installs a Handler/Instrumentation transaction observer
    // which assumes getCallbacks() is non-null; its List.size()/iterator()
    // throws before TransactionExecutor reaches ResumeActivityItem, leaving a
    // successfully-created second Activity without a ViewRootImpl. Real AMS
    // launches commonly carry callbacks (results/new-intents/config changes)
    // alongside the lifecycle request. Add the standard ActivityResultItem
    // with an empty result list: TransactionExecutor executes it as a no-op,
    // while observers see a valid, non-empty callback list. This is confined
    // to the BMS-free in-process boundary path; normal OH lifecycle delivery
    // and the Android framework remain unchanged.
    {
        jclass arrayListCls = env->FindClass("java/util/ArrayList");
        jclass resultCls = env->FindClass(
            "android/app/servertransaction/ActivityResultItem");
        jmethodID listCtor = arrayListCls != nullptr
            ? env->GetMethodID(arrayListCls, "<init>", "()V") : nullptr;
        jmethodID resultObtain = resultCls != nullptr
            ? env->GetStaticMethodID(resultCls, "obtain",
                "(Landroid/os/IBinder;Ljava/util/List;)"
                "Landroid/app/servertransaction/ActivityResultItem;")
            : nullptr;
        jmethodID addCallback = env->GetMethodID(txCls, "addCallback",
            "(Landroid/app/servertransaction/ClientTransactionItem;)V");
        if (listCtor != nullptr && resultObtain != nullptr && addCallback != nullptr) {
            jobject emptyResults = env->NewObject(arrayListCls, listCtor);
            jobject resultItem = emptyResults != nullptr
                ? env->CallStaticObjectMethod(resultCls, resultObtain, token, emptyResults)
                : nullptr;
            if (!env->ExceptionCheck() && resultItem != nullptr) {
                env->CallVoidMethod(tx, addCallback, resultItem);
            }
            if (!env->ExceptionCheck()) {
                fprintf(stderr,
                        "[WESTLAKE-DL2RESUME] added empty ActivityResultItem callback "
                        "for recordId=%d\n", rec);
                fflush(stderr);
            }
            if (resultItem != nullptr) env->DeleteLocalRef(resultItem);
            if (emptyResults != nullptr) env->DeleteLocalRef(emptyResults);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            fprintf(stderr,
                    "[WESTLAKE-DL2RESUME] ActivityResultItem callback unavailable; "
                    "continuing lifecycle-only for recordId=%d\n", rec);
            fflush(stderr);
        }
        if (resultCls != nullptr) env->DeleteLocalRef(resultCls);
        if (arrayListCls != nullptr) env->DeleteLocalRef(arrayListCls);
    }
    env->CallVoidMethod(tx, setLifecycle, res);
    jclass appThreadCls = env->GetObjectClass(appThread);
    jmethodID sched = env->GetMethodID(appThreadCls, "scheduleTransaction",
        "(Landroid/app/servertransaction/ClientTransaction;)V");
    if (sched == nullptr) { env->ExceptionClear();
        fprintf(stderr, "[WESTLAKE-DL2RESUME] scheduleTransaction missing\n"); fflush(stderr); return; }
    // WESTLAKE §788: in direct-launch mode this function is already running on
    // Android's main thread (the startActivity listener was delivered through
    // the native MessageQueue fd).  Merely calling ApplicationThread.schedule-
    // Transaction posts ResumeActivityItem and then the makeVisible() block
    // below races ahead of it.  TikTokActivity exposed the race deterministically:
    // WindowManagerGlobal.addView received a null decor and threw "view must not
    // be null".  Execute synchronously under the same boundary flag used by the
    // launch transaction, so handleResumeActivity has created/attached the decor
    // before visibility is requested.
    // Do not recursively execute a resume transaction while startActivity is
    // already running on Android's main Looper.  BrowserActivity performs
    // WebView/Cookie work from onResume which posts back to that Looper; a
    // nested executeTransaction() can therefore wait for work that cannot run
    // until the outer native startAbility callback returns.  Queueing through
    // ApplicationThread is the normal AOSP path and handleResumeActivity()
    // owns both addView() and makeVisible().  Keep the synchronous fallback
    // only for callers that genuinely enter this boundary off the UI Looper.
    bool onMainLooper = false;
    {
        jclass looperCls = env->FindClass("android/os/Looper");
        jmethodID getMain = looperCls != nullptr
            ? env->GetStaticMethodID(looperCls, "getMainLooper", "()Landroid/os/Looper;")
            : nullptr;
        jmethodID getMine = looperCls != nullptr
            ? env->GetStaticMethodID(looperCls, "myLooper", "()Landroid/os/Looper;")
            : nullptr;
        jobject mainLooper = (getMain != nullptr && !env->ExceptionCheck())
            ? env->CallStaticObjectMethod(looperCls, getMain) : nullptr;
        jobject myLooper = (getMine != nullptr && !env->ExceptionCheck())
            ? env->CallStaticObjectMethod(looperCls, getMine) : nullptr;
        if (!env->ExceptionCheck() && mainLooper != nullptr && myLooper != nullptr) {
            onMainLooper = env->IsSameObject(mainLooper, myLooper);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (myLooper != nullptr) env->DeleteLocalRef(myLooper);
        if (mainLooper != nullptr) env->DeleteLocalRef(mainLooper);
        if (looperCls != nullptr) env->DeleteLocalRef(looperCls);
    }
    bool resumedSynchronously = false;
    if (getenv("WL_SYNC_TRANSACTION") != nullptr && at != nullptr && !onMainLooper) {
        jmethodID execute = env->GetMethodID(atCls, "executeTransaction",
            "(Landroid/app/servertransaction/ClientTransaction;)V");
        if (execute != nullptr && !env->ExceptionCheck()) {
            env->CallVoidMethod(at, execute, tx);
            resumedSynchronously = !env->ExceptionCheck();
        }
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    }
    if (!resumedSynchronously) {
        env->CallVoidMethod(appThread, sched, tx);
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear();
            fprintf(stderr, "[WESTLAKE-DL2RESUME] scheduleTransaction threw\n"); fflush(stderr); return; }
    }
    fprintf(stderr,
            "[WESTLAKE-DL2RESUME] resume transaction %s for recordId=%d "
            "onMainLooper=%d\n",
            resumedSynchronously ? "executed synchronously" : "scheduled", rec,
            onMainLooper ? 1 : 0);
    fflush(stderr);

    if (!resumedSynchronously) {
        // ActivityThread.handleResumeActivity() will add the window and call
        // Activity.makeVisible() when the queued transaction runs.  Calling
        // makeVisible here would race ahead of that transaction.
        return;
    }

    // WESTLAKE §245: MAKE THE DECOR VISIBLE.
    // §244 measured `[OH_WSA-relayout] ... visibility=8` == View.GONE, so ViewRootImpl skips surface
    // acquisition and performDraw() entirely -- hwui is never asked for a buffer and the (correctly
    // composited, §243) surface stays BLACK. AOSP flips this in
    // ActivityThread.handleResumeActivity via `r.activity.makeVisible()`; this in-process DL2 resume
    // path schedules ResumeActivityItem but never performs that step. Do it here.
    // `Activity.makeVisible()` is package-private, so call it reflectively; fall back to
    // getWindow().getDecorView().setVisibility(View.VISIBLE) if it is unavailable.
    {
        jclass atCls2 = env->FindClass("android/app/ActivityThread");
        jmethodID curAT2 = (atCls2 != nullptr)
            ? env->GetStaticMethodID(atCls2, "currentActivityThread",
                                     "()Landroid/app/ActivityThread;") : nullptr;
        jmethodID getAct = (atCls2 != nullptr)
            ? env->GetMethodID(atCls2, "getActivity",
                               "(Landroid/os/IBinder;)Landroid/app/Activity;") : nullptr;
        jclass regCls2 = env->FindClass("adapter/core/OhTokenRegistry");
        jmethodID findTok2 = (regCls2 != nullptr)
            ? env->GetStaticMethodID(regCls2, "findByRecordId", "(I)Landroid/os/IBinder;") : nullptr;
        if (env->ExceptionCheck()) { env->ExceptionClear(); }
        jobject act = nullptr;
        if (curAT2 != nullptr && getAct != nullptr && findTok2 != nullptr) {
            jobject at2  = env->CallStaticObjectMethod(atCls2, curAT2);
            jobject tok2 = env->CallStaticObjectMethod(regCls2, findTok2, (jint) rec);
            if (at2 != nullptr && tok2 != nullptr) {
                act = env->CallObjectMethod(at2, getAct, tok2);
            }
            if (env->ExceptionCheck()) { env->ExceptionClear(); }
        }
        if (act != nullptr) {
            jclass actCls = env->GetObjectClass(act);

            // WESTLAKE 2026-08-26: A direct secondary launch can reach the
            // resume transaction with Activity.mDecor still null.  Stock AMS
            // normally arranges the ActivityClientRecord/window state before
            // this point; our BMS-free in-process transaction does not always
            // do so.  Calling Activity.makeVisible() with a null mDecor reaches
            // WindowManagerGlobal.addView(null, ...) and throws
            // "view must not be null".  Recover the Activity's own DecorView
            // from its Window and publish it in the standard framework field;
            // makeVisible() can then perform the normal one-time addView.
            // Keep record 1 untouched because its validated primary launch
            // already establishes mDecor through ActivityThread.
            if (rec > 1 && actCls != nullptr) {
                jfieldID decorField = env->GetFieldID(
                    actCls, "mDecor", "Landroid/view/View;");
                jobject currentDecor = decorField != nullptr
                    ? env->GetObjectField(act, decorField) : nullptr;
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    currentDecor = nullptr;
                }
                if (currentDecor == nullptr) {
                    jmethodID getWindow = env->GetMethodID(
                        actCls, "getWindow", "()Landroid/view/Window;");
                    jobject window = getWindow != nullptr
                        ? env->CallObjectMethod(act, getWindow) : nullptr;
                    jclass windowCls = window != nullptr
                        ? env->GetObjectClass(window) : nullptr;
                    jmethodID getDecorView = windowCls != nullptr
                        ? env->GetMethodID(windowCls, "getDecorView",
                            "()Landroid/view/View;") : nullptr;
                    jobject windowDecor = (window != nullptr && getDecorView != nullptr)
                        ? env->CallObjectMethod(window, getDecorView) : nullptr;
                    if (!env->ExceptionCheck() && decorField != nullptr
                            && windowDecor != nullptr) {
                        env->SetObjectField(act, decorField, windowDecor);
                        fprintf(stderr,
                                "[WESTLAKE-DL2RESUME] recovered mDecor from Window "
                                "for recordId=%d\n", rec);
                        fflush(stderr);
                    }
                    if (env->ExceptionCheck()) {
                        env->ExceptionDescribe();
                        env->ExceptionClear();
                    }
                    if (windowDecor != nullptr) env->DeleteLocalRef(windowDecor);
                    if (windowCls != nullptr) env->DeleteLocalRef(windowCls);
                    if (window != nullptr) env->DeleteLocalRef(window);
                }
                if (currentDecor != nullptr) env->DeleteLocalRef(currentDecor);
            }
            jmethodID mkVis = env->GetMethodID(actCls, "makeVisible", "()V");
            if (mkVis != nullptr) {
                env->CallVoidMethod(act, mkVis);
                if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
                fprintf(stderr, "[WESTLAKE-DL2RESUME] makeVisible() called for recordId=%d\n", rec);
            } else {
                env->ExceptionClear();
                fprintf(stderr, "[WESTLAKE-DL2RESUME] makeVisible not found for recordId=%d\n", rec);
            }
            fflush(stderr);
        } else {
            fprintf(stderr, "[WESTLAKE-DL2RESUME] no Activity for recordId=%d (cannot makeVisible)\n",
                    rec);
            fflush(stderr);
        }
    }
}

jint nativeStartAbility_impl(JNIEnv* env, jclass,
                             jstring bundleName, jstring abilityName,
                             jstring action, jstring uri, jstring extraJson,
                             jlong callerOhTokenAddr) {
    WantParams want;
    want.bundleName = jstr(env, bundleName);
    want.abilityName = jstr(env, abilityName);
    want.action = jstr(env, action);
    want.uri = jstr(env, uri);
    want.parametersJson = jstr(env, extraJson);

    LOGI("nativeStartAbility: bundle=%s, ability=%s, action=%s, callerOhToken=0x%llx",
         want.bundleName.c_str(), want.abilityName.c_str(), want.action.c_str(),
         static_cast<unsigned long long>(callerOhTokenAddr));

    // 2026-06-04 LINK-OPEN GUARD: an implicit ACTION_VIEW (ohos.want.action.viewData
    // with no explicit bundle) is an Android startActivity(ACTION_VIEW, url) — e.g.
    // tapping a license hyperlink on noice's "About this sound" page. This device
    // has no browser/handler, so OH StartAbility returns ERR_IMPLICIT_START_ABILITY_FAIL
    // (2097199) AND the failed implicit start pushes the caller (noice) to the
    // BACKGROUND → looks like a crash (launcher comes forward). Since the link can
    // never open anyway, no-op it and report success so noice keeps foreground.
    if (want.bundleName.empty() && want.action == "ohos.want.action.viewData") {
        LOGI("nativeStartAbility: suppressing implicit viewData link-open (no handler; "
             "keeps noice foreground) uri=%s", want.uri.c_str());
        return 0;  // pretend success to the AOSP caller; do not yield foreground
    }

    // WESTLAKE (2026-07-22) DIRECT-LAUNCH SECOND ACTIVITY:
    // In DIRECT-LAUNCH mode the bundle is never registered with OH BMS, so routing our OWN
    // Activities through OH StartAbility fails (observed: returns 2097152) and nothing is drawn.
    // noice's MainActivity immediately starts AppIntroActivity on first run, so this blocked the
    // first frame. Launch our own package's activities IN-PROCESS via the same
    // AppSchedulerBridge.nativeOnScheduleLaunchAbility(...) call that directLaunchNoBms() uses
    // for MainActivity.
    // WESTLAKE §231 (2026-07-22) — REAL OH StartAbility WORKS NOW, but is REVERTED (see below).
    // With the signed HAP installed (§230) the old justification for this bypass is obsolete, and
    // the real path succeeds IF the ability name is mapped to one OH actually knows:
    //     ...noice.activity.AppIntroActivity -> 2097152   (OH cannot resolve ANDROID class names)
    //     EntryAbility                        -> 0        (the only ability our HAP declares)
    // i.e. `OHAbilityManagerClient::startAbilityWithCaller(want /*abilityName="EntryAbility"*/, tok)`
    // returns 0 and AMS creates a real ability record.
    // WHY REVERTED: AMS then spawns ITS OWN process for that ability, so the session/token belong to
    // that pid, not to this appspawn-x child -- the V7 window call still got ERR_INVALID_STATE -- and
    // the AMS callback into this process made startup fail with
    //     [INITCHILD-FAIL] NoClassDefFoundError: Class not found using the boot class loader
    // (regressing the clean §216/§230 state of missingview=0 / initfail=0). No window was gained.
    // Re-enable only together with the process-identity fix (§231 notes: run the Android app INSIDE
    // the process AMS starts for EntryAbility, or adopt that ability token as ohTokenAddr).
    {
        const char* dl  = getenv("ASX_DIRECT_LAUNCH");
        const char* pkg = getenv("ASX_LAUNCH_PKG");
        if (dl != nullptr && pkg != nullptr && !want.abilityName.empty() &&
            (want.bundleName == pkg || want.bundleName.empty())) {
            wl_restore_direct_launch_main_looper(env);
            // §686: the target Activity may instantiate WebView in onCreate.
            // Stabilize Toutiao's underlying stock provider before scheduling
            // that lifecycle transaction; non-Toutiao apps take the no-op path.
            wl_stabilize_direct_launch_webview_provider(env);
            jclass cls = env->FindClass("adapter/activity/AppSchedulerBridge");
            jmethodID m = (cls != nullptr)
                ? env->GetStaticMethodID(cls, "nativeOnScheduleLaunchAbility",
                      "(Ljava/lang/Object;Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;"
                      "Ljava/lang/String;J)V")
                : nullptr;
            if (m != nullptr) {
                static int wl_record = 1;
                const int rec = ++wl_record;
                const std::string wantJson = makeDirectLaunchWantJson(want);
                // WESTLAKE 2026-08-25: a BMS-free startActivity has no OH AMS
                // transaction driver.  Scheduling LaunchActivityItem and immediately
                // scheduling ResumeActivityItem left the later makeVisible() probe
                // racing before ActivityThread had created the Activity.  The observed
                // result was deterministic: `no Activity ... cannot makeVisible`, a
                // ViewRoot relayout at visibility=GONE, and a fully-populated Chromium
                // DOM whose document.visibilityState remained hidden.
                //
                // AppSchedulerBridge already owns a synchronous transaction path for
                // this exact standalone boundary.  Enable it for in-process secondary
                // activities so the launch transaction completes before wl_dl2_resume()
                // looks up the Activity and calls makeVisible().  This is generic to
                // direct-launch mode; normal OH/Android activity routing is untouched.
                setenv("WL_SYNC_TRANSACTION", "1", 1);
                jstring jp = env->NewStringUTF(pkg);
                jstring ja = env->NewStringUTF(want.abilityName.c_str());
                jstring jw = env->NewStringUTF(wantJson.c_str());
                env->CallStaticVoidMethod(cls, m, nullptr, jp, ja, (jint) rec,
                                          nullptr, jw, (jlong) 0);
                if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
                fprintf(stderr,
                        "[WESTLAKE-DL2] in-process launch of %s (recordId=%d, "
                        "action=%s, uriBytes=%zu, extrasBytes=%zu, wantBytes=%zu)\n",
                        want.abilityName.c_str(), rec, want.action.c_str(), want.uri.size(),
                        want.parametersJson.size(), wantJson.size());
                fflush(stderr);
                env->DeleteLocalRef(jw);
                env->DeleteLocalRef(ja);
                env->DeleteLocalRef(jp);
                // 2026-07-22: the launch alone leaves the Activity CREATED BUT NOT VISIBLE —
                // AOSP adds the window (wm.addView(decor, l)) in handleResumeActivity, so
                // without a resume there is no ViewRootImpl, no traversal and no frame.
                // AppSchedulerBridge.directResume() exists for exactly this but is private and
                // hardcoded to recordId 1 (correct only for the first, direct-launched Activity).
                // Replicate it here for THIS launch's recordId — no adapter-jar/boot-image work.
                wl_dl2_resume(env, rec);
                return 0;
            }
            env->ExceptionClear();
            fprintf(stderr, "[WESTLAKE-DL2] could not resolve AppSchedulerBridge; "
                            "falling back to OH StartAbility\n");
            fflush(stderr);
        }
    }

    return OHAbilityManagerClient::getInstance().startAbilityWithCaller(
            want, callerOhTokenAddr);
}

// -------- mission stack (6) --------

jint nativeStartAbilityInMission_impl(JNIEnv* env, jclass,
                                      jstring bundleName, jstring abilityName,
                                      jstring action, jstring uri, jstring extraJson,
                                      jint missionId) {
    WantParams want;
    want.bundleName = jstr(env, bundleName);
    want.abilityName = jstr(env, abilityName);
    want.action = jstr(env, action);
    want.uri = jstr(env, uri);
    want.parametersJson = jstr(env, extraJson);

    LOGI("nativeStartAbilityInMission: bundle=%s, ability=%s, missionId=%d",
         want.bundleName.c_str(), want.abilityName.c_str(), missionId);

    return OHAbilityManagerClient::getInstance().startAbilityInMission(want, missionId);
}

jint nativeCleanMission_impl(JNIEnv*, jclass, jint missionId) {
    return OHAbilityManagerClient::getInstance().cleanMission(missionId);
}

jint nativeMoveMissionToFront_impl(JNIEnv*, jclass, jint missionId) {
    return OHAbilityManagerClient::getInstance().moveMissionToFront(missionId);
}

jboolean nativeIsTopAbility_impl(JNIEnv* env, jclass,
                                 jint missionId, jstring abilityName) {
    std::string name = jstr(env, abilityName);
    return (jboolean)OHAbilityManagerClient::getInstance().isTopAbility(missionId, name);
}

jint nativeClearAbilitiesAbove_impl(JNIEnv* env, jclass,
                                    jint missionId, jstring abilityName) {
    std::string name = jstr(env, abilityName);
    return OHAbilityManagerClient::getInstance().clearAbilitiesAbove(missionId, name);
}

jint nativeGetMissionIdForBundle_impl(JNIEnv* env, jclass, jstring bundleName) {
    std::string bundle = jstr(env, bundleName);
    return OHAbilityManagerClient::getInstance().getMissionIdForBundle(bundle);
}

const JNINativeMethod kMethods[] = {
    {"nativeGetOHAbilityManagerService", "()J",
        (void*)nativeGetOHAbilityManagerService_impl},
    {"nativeStartAbility",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Ljava/lang/String;Ljava/lang/String;J)I",
        (void*)nativeStartAbility_impl},
    {"nativeStartAbilityInMission",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Ljava/lang/String;Ljava/lang/String;I)I",
        (void*)nativeStartAbilityInMission_impl},
    {"nativeCleanMission",         "(I)I",
        (void*)nativeCleanMission_impl},
    {"nativeMoveMissionToFront",   "(I)I",
        (void*)nativeMoveMissionToFront_impl},
    {"nativeIsTopAbility",
        "(ILjava/lang/String;)Z",
        (void*)nativeIsTopAbility_impl},
    {"nativeClearAbilitiesAbove",
        "(ILjava/lang/String;)I",
        (void*)nativeClearAbilitiesAbove_impl},
    {"nativeGetMissionIdForBundle",
        "(Ljava/lang/String;)I",
        (void*)nativeGetMissionIdForBundle_impl},
};

}  // namespace

// Process-start hook used by adapter_bridge.cpp at the stable appspawn-x
// checkpoint. Keep the implementation shared with the secondary-activity
// boundary so both paths enforce identical cache and provider verification.
bool wl_prime_webview_update_service(JNIEnv* env) {
    return wl_prime_webview_update_service_impl(env, false);
}

// Publish at the explicit post-bind checkpoint. Application.onCreate() has
// returned, so ActivityThread has a usable Application context, while this is
// still early enough for Activity creation code that asks for WebView.
bool wl_publish_webview_update_service_after_bind(JNIEnv* env) {
    return wl_prime_webview_update_service_impl(env, true);
}

int register_ActivityTaskManagerAdapter(JNIEnv* env) {
    jclass clazz = env->FindClass("adapter/activity/ActivityTaskManagerAdapter");
    if (!clazz) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        LOGE("register_ActivityTaskManagerAdapter: FindClass returned null");
        return JNI_ERR;
    }
    jint rc = env->RegisterNatives(clazz, kMethods,
                                   sizeof(kMethods) / sizeof(kMethods[0]));
    env->DeleteLocalRef(clazz);
    if (rc != JNI_OK) {
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        LOGE("register_ActivityTaskManagerAdapter: RegisterNatives failed rc=%d", (int)rc);
    } else {
        LOGI("register_ActivityTaskManagerAdapter: OK 8 methods");
    }
    return rc;
}
