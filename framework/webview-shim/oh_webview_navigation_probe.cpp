/*
 * WESTLAKE §707/§710 — transparent probes for Chromium's generated navigation JNI
 * boundaries.
 *
 * ART resolves unregistered J.N methods by their exported long JNI names.  Toutiao reaches
 * NavigationControllerImpl.h(), whose 21-argument MAqmDh4t call returns without creating a new
 * committed history entry.  For loadDataWithBaseURL Chromium intentionally puts an empty data:
 * URL in JO.a and the complete encoded document in JO.k (argument 12), so both must be reported.
 * This wrapper verifies the arguments after ART has marshalled them, then forwards them unchanged
 * to the pinned libwebviewchromium.so implementation.  It is a diagnostic boundary shim only: no
 * URL, flag, object, exception, or return value is altered.
 */

#include <jni.h>

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace {

using LoadUrlFn = jobject (*)(
    JNIEnv*, jclass,
    jlong, jobject, jstring, jint, jint, jstring, jint, jint, jstring, jobject,
    jstring, jstring, jstring, jboolean, jboolean, jboolean, jobject, jboolean,
    jboolean, jlong, jlong);

using EvaluateJavaScriptFn = void (*)(
    JNIEnv*, jclass, jlong, jstring, jobject);

extern "C" void wl_ALooper_traceNext(int callbackCount);

LoadUrlFn ResolveRealLoadUrl()
{
    static LoadUrlFn real;
    static bool attempted;
    if (attempted) return real;
    attempted = true;

    void* handle = dlopen(
        "/data/local/tmp/asx/webview-t-lib/libwebviewchromium.so", RTLD_NOW | RTLD_LOCAL);
    if (handle != nullptr) {
        real = reinterpret_cast<LoadUrlFn>(dlsym(handle, "Java_J_N_MAqmDh4t"));
    }
    if (real == nullptr) {
        // The absolute path is stable for the harness, but retain a loader-name fallback for
        // controls that use the same provider from another staging directory.
        handle = dlopen("libwebviewchromium.so", RTLD_NOW | RTLD_LOCAL);
        if (handle != nullptr) {
            real = reinterpret_cast<LoadUrlFn>(dlsym(handle, "Java_J_N_MAqmDh4t"));
        }
    }
    fprintf(stderr, "[WESTLAKE-WEBNAV-707] real=%p\n", reinterpret_cast<void*>(real));
    fflush(stderr);
    return real;
}

EvaluateJavaScriptFn ResolveRealEvaluateJavaScript()
{
    static EvaluateJavaScriptFn real;
    static bool attempted;
    if (attempted) return real;
    attempted = true;

    void* handle = dlopen(
        "/data/local/tmp/asx/webview-t-lib/libwebviewchromium.so", RTLD_NOW | RTLD_LOCAL);
    if (handle != nullptr) {
        real = reinterpret_cast<EvaluateJavaScriptFn>(
            dlsym(handle, "Java_J_N_M0uS2SDH"));
    }
    if (real == nullptr) {
        handle = dlopen("libwebviewchromium.so", RTLD_NOW | RTLD_LOCAL);
        if (handle != nullptr) {
            real = reinterpret_cast<EvaluateJavaScriptFn>(
                dlsym(handle, "Java_J_N_M0uS2SDH"));
        }
    }
    fprintf(stderr, "[WESTLAKE-WEBEVAL-710] real=%p\n", reinterpret_cast<void*>(real));
    fflush(stderr);
    return real;
}

int StringLength(JNIEnv* env, jstring value)
{
    return value != nullptr ? static_cast<int>(env->GetStringLength(value)) : -1;
}

void CopyPrefix(JNIEnv* env, jstring value, char* output, size_t outputSize)
{
    if (outputSize == 0) return;
    output[0] = '\0';
    if (value == nullptr) return;
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) return;
    snprintf(output, outputSize, "%.*s", static_cast<int>(outputSize - 1), chars);
    env->ReleaseStringUTFChars(value, chars);
}

// WESTLAKE §720 diagnostic: preserve the complete JavaScript programs that Toutiao submits
// after its article template becomes ready.  The prefix log cannot distinguish the several
// JSBridge callbacks with the same shape.  This is read-only with respect to Chromium and is
// deliberately limited to non-trivial scripts in the direct-launch diagnostic directory.
void DumpScript(JNIEnv* env, jstring script, int scriptLength)
{
    if (script == nullptr || scriptLength < 500) return;
    const char* chars = env->GetStringUTFChars(script, nullptr);
    if (chars == nullptr) return;

    static unsigned sequence;
    char path[192];
    snprintf(path, sizeof(path),
             "/data/local/tmp/asx/webscript-%d-%03u-len%d.js",
             static_cast<int>(getpid()), ++sequence, scriptLength);
    FILE* output = fopen(path, "wb");
    if (output != nullptr) {
        fwrite(chars, 1, strlen(chars), output);
        fclose(output);
        fprintf(stderr, "[WESTLAKE-WEBDUMP-720] path=%s\n", path);
        fflush(stderr);
    }
    env->ReleaseStringUTFChars(script, chars);
}

// WESTLAKE §718 diagnostic: locate the article WebView while this JNI wrapper is running on
// Android's main thread.  This lets one A/B run issue a normal WebView navigation at the exact
// point where late ExecuteJavaScript stalls, separating a dead renderer/frame channel from an
// eval-specific failure.  The traversal and replacement page are temporary diagnostics.
jobject FindWebViewInSubtree(JNIEnv* env, jobject view, jclass webViewClass,
                            jclass viewGroupClass, jmethodID childCount,
                            jmethodID childAt, int depth)
{
    if (view == nullptr || depth > 64) return nullptr;
    if (env->IsInstanceOf(view, webViewClass)) return env->NewLocalRef(view);
    if (!env->IsInstanceOf(view, viewGroupClass)) return nullptr;

    const jint count = env->CallIntMethod(view, childCount);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    for (jint index = count - 1; index >= 0; --index) {
        jobject child = env->CallObjectMethod(view, childAt, index);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return nullptr;
        }
        jobject result = FindWebViewInSubtree(
            env, child, webViewClass, viewGroupClass, childCount, childAt, depth + 1);
        env->DeleteLocalRef(child);
        if (result != nullptr) return result;
    }
    return nullptr;
}

jobject FindVisibleToutiaoWebViewInSubtree(
    JNIEnv* env, jobject view, jclass webViewClass, jclass viewGroupClass,
    jmethodID childCount, jmethodID childAt, jmethodID getClassName,
    jmethodID isShown, int depth)
{
    if (view == nullptr || depth > 64) return nullptr;
    if (env->IsInstanceOf(view, webViewClass)) {
        const jboolean shown = env->CallBooleanMethod(view, isShown);
        if (env->ExceptionCheck()) env->ExceptionClear();
        jclass runtimeClass = env->GetObjectClass(view);
        jstring name = runtimeClass != nullptr
            ? static_cast<jstring>(env->CallObjectMethod(runtimeClass, getClassName)) : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            name = nullptr;
        }
        const char* chars = name != nullptr ? env->GetStringUTFChars(name, nullptr) : nullptr;
        const bool match = shown && chars != nullptr && strstr(chars, "MyWebViewV9") != nullptr;
        if (name != nullptr && chars != nullptr) env->ReleaseStringUTFChars(name, chars);
        if (name != nullptr) env->DeleteLocalRef(name);
        if (runtimeClass != nullptr) env->DeleteLocalRef(runtimeClass);
        if (match) return env->NewLocalRef(view);
    }
    if (!env->IsInstanceOf(view, viewGroupClass)) return nullptr;

    const jint count = env->CallIntMethod(view, childCount);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    for (jint index = count - 1; index >= 0; --index) {
        jobject child = env->CallObjectMethod(view, childAt, index);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return nullptr;
        }
        jobject result = FindVisibleToutiaoWebViewInSubtree(
            env, child, webViewClass, viewGroupClass, childCount, childAt,
            getClassName, isShown, depth + 1);
        env->DeleteLocalRef(child);
        if (result != nullptr) return result;
    }
    return nullptr;
}

// Resolve the Java WebView back to the exact native WebContents used by Chromium's generated
// evaluateJavaScript JNI.  Toutiao replaces WebView.mProvider with a java.lang.reflect.Proxy,
// but that proxy deliberately exposes WebViewProviderProxy.RealGetter; use that public proxy
// contract instead of guessing at the invocation-handler fields.  Everything below is read-only.
jlong NativeWebContentsForWebView(JNIEnv* env, jobject webView)
{
    jclass webViewClass = env->FindClass("android/webkit/WebView");
    if (env->ExceptionCheck() || webViewClass == nullptr) {
        env->ExceptionClear();
        return 0;
    }
    jfieldID providerField = env->GetFieldID(
        webViewClass, "mProvider", "Landroid/webkit/WebViewProvider;");
    if (env->ExceptionCheck() || providerField == nullptr) {
        env->ExceptionClear();
        return 0;
    }
    jobject provider = env->GetObjectField(webView, providerField);
    if (env->ExceptionCheck() || provider == nullptr) {
        env->ExceptionClear();
        return 0;
    }

    jobject realProvider = nullptr;
    jclass providerClass = env->GetObjectClass(provider);
    jmethodID getReal = providerClass != nullptr
        ? env->GetMethodID(providerClass, "getRealWebViewProvider",
                           "()Landroid/webkit/WebViewProvider;")
        : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (getReal != nullptr) {
        realProvider = env->CallObjectMethod(provider, getReal);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            realProvider = nullptr;
        }
    }
    jobject providerToRead = realProvider != nullptr ? realProvider : provider;
    jclass realClass = env->GetObjectClass(providerToRead);
    jfieldID awField = realClass != nullptr
        ? env->GetFieldID(realClass, "f", "Lorg/chromium/android_webview/AwContents;")
        : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();
    jobject awContents = awField != nullptr
        ? env->GetObjectField(providerToRead, awField) : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        awContents = nullptr;
    }

    jobject webContents = nullptr;
    if (awContents != nullptr) {
        jclass awClass = env->GetObjectClass(awContents);
        jfieldID webContentsField = awClass != nullptr
            ? env->GetFieldID(awClass, "i", "Lorg/chromium/content_public/browser/WebContents;")
            : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (webContentsField != nullptr) {
            webContents = env->GetObjectField(awContents, webContentsField);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                webContents = nullptr;
            }
        }
        if (awClass != nullptr) env->DeleteLocalRef(awClass);
    }

    jlong nativeWebContents = 0;
    if (webContents != nullptr) {
        jclass contentsClass = env->GetObjectClass(webContents);
        jfieldID nativeField = contentsClass != nullptr
            ? env->GetFieldID(contentsClass, "b", "J") : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (nativeField != nullptr) {
            nativeWebContents = env->GetLongField(webContents, nativeField);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                nativeWebContents = 0;
            }
        }
        if (contentsClass != nullptr) env->DeleteLocalRef(contentsClass);
    }

    if (webContents != nullptr) env->DeleteLocalRef(webContents);
    if (awContents != nullptr) env->DeleteLocalRef(awContents);
    if (realClass != nullptr) env->DeleteLocalRef(realClass);
    if (realProvider != nullptr) env->DeleteLocalRef(realProvider);
    if (providerClass != nullptr) env->DeleteLocalRef(providerClass);
    env->DeleteLocalRef(provider);
    return nativeWebContents;
}

jobject FindWebViewForNativeInSubtree(
    JNIEnv* env, jobject view, jclass webViewClass, jclass viewGroupClass,
    jmethodID childCount, jmethodID childAt, jlong targetNative, int depth)
{
    if (view == nullptr || depth > 64) return nullptr;
    if (env->IsInstanceOf(view, webViewClass) &&
        NativeWebContentsForWebView(env, view) == targetNative) {
        return env->NewLocalRef(view);
    }
    if (!env->IsInstanceOf(view, viewGroupClass)) return nullptr;
    const jint count = env->CallIntMethod(view, childCount);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    for (jint index = count - 1; index >= 0; --index) {
        jobject child = env->CallObjectMethod(view, childAt, index);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return nullptr;
        }
        jobject result = FindWebViewForNativeInSubtree(
            env, child, webViewClass, viewGroupClass, childCount, childAt,
            targetNative, depth + 1);
        env->DeleteLocalRef(child);
        if (result != nullptr) return result;
    }
    return nullptr;
}

jobject FindTopmostWebView(JNIEnv* env, jlong targetNative = 0)
{
    jclass webViewClass = env->FindClass("android/webkit/WebView");
    jclass viewGroupClass = env->FindClass("android/view/ViewGroup");
    jclass windowManagerGlobalClass = env->FindClass("android/view/WindowManagerGlobal");
    jclass viewRootClass = env->FindClass("android/view/ViewRootImpl");
    jclass listClass = env->FindClass("java/util/ArrayList");
    jclass classClass = env->FindClass("java/lang/Class");
    jclass viewClass = env->FindClass("android/view/View");
    if (env->ExceptionCheck() || webViewClass == nullptr || viewGroupClass == nullptr ||
        windowManagerGlobalClass == nullptr || viewRootClass == nullptr || listClass == nullptr ||
        classClass == nullptr || viewClass == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }

    jmethodID childCount = env->GetMethodID(viewGroupClass, "getChildCount", "()I");
    jmethodID childAt = env->GetMethodID(
        viewGroupClass, "getChildAt", "(I)Landroid/view/View;");
    jmethodID getInstance = env->GetStaticMethodID(
        windowManagerGlobalClass, "getInstance", "()Landroid/view/WindowManagerGlobal;");
    jfieldID rootsField = env->GetFieldID(
        windowManagerGlobalClass, "mRoots", "Ljava/util/ArrayList;");
    jfieldID viewField = env->GetFieldID(viewRootClass, "mView", "Landroid/view/View;");
    jmethodID size = env->GetMethodID(listClass, "size", "()I");
    jmethodID get = env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");
    jmethodID getClassName = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
    jmethodID isShown = env->GetMethodID(viewClass, "isShown", "()Z");
    if (env->ExceptionCheck() || childCount == nullptr || childAt == nullptr ||
        getInstance == nullptr || rootsField == nullptr || viewField == nullptr ||
        size == nullptr || get == nullptr || getClassName == nullptr || isShown == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }

    jobject manager = env->CallStaticObjectMethod(windowManagerGlobalClass, getInstance);
    jobject roots = manager != nullptr ? env->GetObjectField(manager, rootsField) : nullptr;
    if (env->ExceptionCheck() || roots == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }
    const jint count = env->CallIntMethod(roots, size);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    // Prefer identity over z-order. This maps the exact native argument of M0uS2SDH back to its
    // owning Java WebView, even when Toutiao has multiple visible article activities.
    if (targetNative != 0) {
        for (jint index = count - 1; index >= 0; --index) {
            jobject root = env->CallObjectMethod(roots, get, index);
            jobject view = root != nullptr ? env->GetObjectField(root, viewField) : nullptr;
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                if (root != nullptr) env->DeleteLocalRef(root);
                continue;
            }
            jobject result = FindWebViewForNativeInSubtree(
                env, view, webViewClass, viewGroupClass, childCount, childAt,
                targetNative, 0);
            if (view != nullptr) env->DeleteLocalRef(view);
            if (root != nullptr) env->DeleteLocalRef(root);
            if (result != nullptr) return result;
        }
    }
    // Prefer the visible Toutiao article WebView. Feed/preload WebViews can coexist in another
    // root and were previously selected by z-order, producing a valid but irrelevant setting.
    for (jint index = count - 1; index >= 0; --index) {
        jobject root = env->CallObjectMethod(roots, get, index);
        jobject view = root != nullptr ? env->GetObjectField(root, viewField) : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            if (root != nullptr) env->DeleteLocalRef(root);
            continue;
        }
        jobject result = FindVisibleToutiaoWebViewInSubtree(
            env, view, webViewClass, viewGroupClass, childCount, childAt,
            getClassName, isShown, 0);
        if (view != nullptr) env->DeleteLocalRef(view);
        if (root != nullptr) env->DeleteLocalRef(root);
        if (result != nullptr) return result;
    }
    for (jint index = count - 1; index >= 0; --index) {
        jobject root = env->CallObjectMethod(roots, get, index);
        jobject view = root != nullptr ? env->GetObjectField(root, viewField) : nullptr;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            if (root != nullptr) env->DeleteLocalRef(root);
            continue;
        }
        jobject result = FindWebViewInSubtree(
            env, view, webViewClass, viewGroupClass, childCount, childAt, 0);
        if (view != nullptr) env->DeleteLocalRef(view);
        if (root != nullptr) env->DeleteLocalRef(root);
        if (result != nullptr) return result;
    }
    return nullptr;
}

// WESTLAKE §731: Android WebView's hardware path records a WebView functor into HWUI. The
// current OH graphics boundary can render ordinary Android display-list operations, but it does
// not yet execute that functor into the host RSSurfaceNode: the renderer DOM and scroll state are
// live while the WebView rectangle remains white. Keep Android and Chromium unchanged and make
// the compatibility decision at this libjnigraphics/OH boundary. LAYER_TYPE_SOFTWARE asks the
// stock WebView provider to use its normal software Canvas implementation until the OH functor
// bridge is complete.
//
// This hook runs on the same Android UI thread as WebView.evaluateJavascript. Resolve by native
// WebContents so multiple/preloaded WebViews do not change the wrong peer. It is deliberately
// app-agnostic and idempotent.
void EnsureSoftwareWebViewLayer(JNIEnv* env, jlong nativeWebContents)
{
    jobject webView = FindTopmostWebView(env, nativeWebContents);
    jclass webViewClass = env->FindClass("android/webkit/WebView");
    if (env->ExceptionCheck() || webView == nullptr || webViewClass == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (webView != nullptr) env->DeleteLocalRef(webView);
        if (webViewClass != nullptr) env->DeleteLocalRef(webViewClass);
        return;
    }

    jmethodID getLayerType = env->GetMethodID(webViewClass, "getLayerType", "()I");
    jmethodID setLayerType = env->GetMethodID(
        webViewClass, "setLayerType", "(ILandroid/graphics/Paint;)V");
    if (env->ExceptionCheck() || getLayerType == nullptr || setLayerType == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(webViewClass);
        env->DeleteLocalRef(webView);
        return;
    }

    const jint oldLayerType = env->CallIntMethod(webView, getLayerType);
    if (!env->ExceptionCheck() && oldLayerType != 1 /* View.LAYER_TYPE_SOFTWARE */) {
        env->CallVoidMethod(webView, setLayerType, 1, nullptr);
        fprintf(stderr,
                "[WESTLAKE-WEBLAYER-731] native=0x%llx layer=%d->1 exception=%d\n",
                static_cast<unsigned long long>(nativeWebContents),
                static_cast<int>(oldLayerType), env->ExceptionCheck() ? 1 : 0);
        fflush(stderr);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(webViewClass);
    env->DeleteLocalRef(webView);
}

// WESTLAKE §721 read-only lifecycle differential. Toutiao's article containers disable
// WebSettings JavaScript in onPause(), while their onResume() only resumes the WebView. A
// resumed/attached WebView can therefore accept evaluateJavascript at JNI and silently return a
// null result without executing it. Report the public state of the topmost WebView at the exact
// signed setContent submission; do not alter settings or lifecycle here.
void ProbeTopmostWebViewState(JNIEnv* env, jlong targetNative)
{
    jobject webView = FindTopmostWebView(env, targetNative);
    jclass webViewClass = env->FindClass("android/webkit/WebView");
    jclass webSettingsClass = env->FindClass("android/webkit/WebSettings");
    jclass viewClass = env->FindClass("android/view/View");
    if (env->ExceptionCheck() || webView == nullptr || webViewClass == nullptr ||
        webSettingsClass == nullptr || viewClass == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        fprintf(stderr, "[WESTLAKE-WEBSTATE-721] unavailable webView=%p\n", webView);
        fflush(stderr);
        if (webView != nullptr) env->DeleteLocalRef(webView);
        return;
    }

    jmethodID getSettings = env->GetMethodID(
        webViewClass, "getSettings", "()Landroid/webkit/WebSettings;");
    jmethodID getJavaScriptEnabled = env->GetMethodID(
        webSettingsClass, "getJavaScriptEnabled", "()Z");
    jmethodID getDomStorageEnabled = env->GetMethodID(
        webSettingsClass, "getDomStorageEnabled", "()Z");
    jmethodID isPaused = env->GetMethodID(webViewClass, "isPaused", "()Z");
    jmethodID getContentHeight = env->GetMethodID(webViewClass, "getContentHeight", "()I");
    jmethodID getLayerType = env->GetMethodID(viewClass, "getLayerType", "()I");
    jmethodID isAttached = env->GetMethodID(viewClass, "isAttachedToWindow", "()Z");
    jmethodID isShown = env->GetMethodID(viewClass, "isShown", "()Z");
    if (env->ExceptionCheck() || getSettings == nullptr || getJavaScriptEnabled == nullptr) {
        env->ExceptionClear();
        fprintf(stderr, "[WESTLAKE-WEBSTATE-721] method lookup failed\n");
        fflush(stderr);
        env->DeleteLocalRef(webView);
        return;
    }

    jobject settings = env->CallObjectMethod(webView, getSettings);
    const int javascript = settings != nullptr
        ? env->CallBooleanMethod(settings, getJavaScriptEnabled) : -1;
    if (env->ExceptionCheck()) env->ExceptionClear();
    const int domStorage = settings != nullptr && getDomStorageEnabled != nullptr
        ? env->CallBooleanMethod(settings, getDomStorageEnabled) : -1;
    if (env->ExceptionCheck()) env->ExceptionClear();
    const int paused = isPaused != nullptr ? env->CallBooleanMethod(webView, isPaused) : -1;
    if (env->ExceptionCheck()) env->ExceptionClear();
    const int contentHeight = getContentHeight != nullptr
        ? env->CallIntMethod(webView, getContentHeight) : -1;
    if (env->ExceptionCheck()) env->ExceptionClear();
    const int layerType = getLayerType != nullptr
        ? env->CallIntMethod(webView, getLayerType) : -1;
    if (env->ExceptionCheck()) env->ExceptionClear();
    const int attached = isAttached != nullptr
        ? env->CallBooleanMethod(webView, isAttached) : -1;
    if (env->ExceptionCheck()) env->ExceptionClear();
    const int shown = isShown != nullptr ? env->CallBooleanMethod(webView, isShown) : -1;
    if (env->ExceptionCheck()) env->ExceptionClear();
    const jlong resolvedNative = NativeWebContentsForWebView(env, webView);

    fprintf(stderr,
            "[WESTLAKE-WEBSTATE-721] webView=%p targetNative=0x%llx"
            " resolvedNative=0x%llx exact=%d javascript=%d domStorage=%d"
            " paused=%d contentHeight=%d layerType=%d attached=%d shown=%d\n",
            webView, static_cast<unsigned long long>(targetNative),
            static_cast<unsigned long long>(resolvedNative),
            resolvedNative == targetNative ? 1 : 0,
            javascript, domStorage, paused, contentHeight, layerType, attached, shown);
    fflush(stderr);
    if (settings != nullptr) env->DeleteLocalRef(settings);
    env->DeleteLocalRef(webView);
}

// WESTLAKE §719 diagnostic: use Chromium's own generated JNI accessors to report the exact
// browser-side WebContents visibility and primary RenderFrameHost state at the failed transition.
// The provider's release dex retains three no-argument frame booleans (isRenderFrameLive,
// signalCloseWatcherIfActive, and areInputEventsIgnored) as b/e/i after R8; reporting all three
// avoids guessing their obfuscated ordering.  This is read-only and must be removed with the
// surrounding article diagnostics.
void ProbePrimaryRenderFrame(JNIEnv* env, jlong nativeWebContents)
{
    using GetVisibilityFn = jint (*)(JNIEnv*, jclass, jlong);
    using GetMainFrameFn = jobject (*)(JNIEnv*, jclass, jlong);

    void* handle = dlopen(
        "/data/local/tmp/asx/webview-t-lib/libwebviewchromium.so", RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) handle = dlopen("libwebviewchromium.so", RTLD_NOW | RTLD_LOCAL);
    GetVisibilityFn getVisibility = handle != nullptr
        ? reinterpret_cast<GetVisibilityFn>(dlsym(handle, "Java_J_N_MOzDgqoz"))
        : nullptr;
    GetMainFrameFn getMainFrame = handle != nullptr
        ? reinterpret_cast<GetMainFrameFn>(dlsym(handle, "Java_J_N_MjidYpBx"))
        : nullptr;
    if (getVisibility == nullptr || getMainFrame == nullptr) {
        fprintf(stderr,
                "[WESTLAKE-WEBFRAME-719] missing visibility=%p mainFrame=%p\n",
                reinterpret_cast<void*>(getVisibility),
                reinterpret_cast<void*>(getMainFrame));
        fflush(stderr);
        return;
    }

    const jint visibility = getVisibility(env, nullptr, nativeWebContents);
    jobject frame = getMainFrame(env, nullptr, nativeWebContents);
    if (env->ExceptionCheck() || frame == nullptr) {
        fprintf(stderr,
                "[WESTLAKE-WEBFRAME-719] visibility=%d frame=%p exception=%d\n",
                static_cast<int>(visibility), frame, env->ExceptionCheck() ? 1 : 0);
        fflush(stderr);
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }

    jclass frameClass = env->GetObjectClass(frame);
    jfieldID nativeField = env->GetFieldID(frameClass, "a", "J");
    jmethodID stateB = env->GetMethodID(frameClass, "b", "()Z");
    jmethodID stateE = env->GetMethodID(frameClass, "e", "()Z");
    jmethodID stateI = env->GetMethodID(frameClass, "i", "()Z");
    jmethodID lastUrl = env->GetMethodID(frameClass, "j", "()Lorg/chromium/url/GURL;");
    if (env->ExceptionCheck()) env->ExceptionClear();
    const jlong nativeFrame = nativeField != nullptr
        ? env->GetLongField(frame, nativeField) : 0;
    const int b = stateB != nullptr ? env->CallBooleanMethod(frame, stateB) : -1;
    if (env->ExceptionCheck()) env->ExceptionClear();
    const int e = stateE != nullptr ? env->CallBooleanMethod(frame, stateE) : -1;
    if (env->ExceptionCheck()) env->ExceptionClear();
    const int i = stateI != nullptr ? env->CallBooleanMethod(frame, stateI) : -1;
    if (env->ExceptionCheck()) env->ExceptionClear();

    char urlText[256] = {0};
    jobject gurl = lastUrl != nullptr ? env->CallObjectMethod(frame, lastUrl) : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        gurl = nullptr;
    }
    if (gurl != nullptr) {
        jclass objectClass = env->FindClass("java/lang/Object");
        jmethodID toString = objectClass != nullptr
            ? env->GetMethodID(objectClass, "toString", "()Ljava/lang/String;") : nullptr;
        jstring text = toString != nullptr
            ? static_cast<jstring>(env->CallObjectMethod(gurl, toString)) : nullptr;
        if (!env->ExceptionCheck() && text != nullptr) {
            CopyPrefix(env, text, urlText, sizeof(urlText));
            env->DeleteLocalRef(text);
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(gurl);
    }

    fprintf(stderr,
            "[WESTLAKE-WEBFRAME-719] visibility=%d frame=%p native=0x%llx"
            " stateB=%d stateE=%d stateI=%d lastUrl=\"%s\"\n",
            static_cast<int>(visibility), frame,
            static_cast<unsigned long long>(nativeFrame), b, e, i, urlText);
    fflush(stderr);
    env->DeleteLocalRef(frame);
}

}  // namespace

extern "C" __attribute__((visibility("default"))) jobject Java_J_N_MAqmDh4t(
    JNIEnv* env, jclass clazz,
    jlong native_controller, jobject controller, jstring url, jint arg3, jint arg4,
    jstring arg5, jint arg6, jint arg7, jstring arg8, jobject arg9, jstring arg10,
    jstring arg11, jstring arg12, jboolean arg13, jboolean arg14, jboolean arg15,
    jobject arg16, jboolean arg17, jboolean arg18, jlong arg19, jlong arg20)
{
    char urlPrefix[112];
    char arg5Prefix[112];
    char arg8Prefix[112];
    char arg10Prefix[112];
    char arg11Prefix[112];
    char arg12Value[768];
    CopyPrefix(env, url, urlPrefix, sizeof(urlPrefix));
    CopyPrefix(env, arg5, arg5Prefix, sizeof(arg5Prefix));
    CopyPrefix(env, arg8, arg8Prefix, sizeof(arg8Prefix));
    CopyPrefix(env, arg10, arg10Prefix, sizeof(arg10Prefix));
    CopyPrefix(env, arg11, arg11Prefix, sizeof(arg11Prefix));
    CopyPrefix(env, arg12, arg12Value, sizeof(arg12Value));
    fprintf(stderr,
            "[WESTLAKE-WEBNAV-707] enter native=0x%llx controller=%p"
            " urlLen=%d url=\"%s\" a3=%d a4=%d a5Len=%d a5=\"%s\""
            " a6=%d a7=%d a8Len=%d a8=\"%s\" a9=%p"
            " a10Len=%d a10=\"%s\" a11Len=%d a11=\"%s\""
            " a12Len=%d a12=\"%s\" b13=%d b14=%d b15=%d"
            " a16=%p b17=%d b18=%d a19=%lld a20=%lld\n",
            static_cast<unsigned long long>(native_controller), controller,
            StringLength(env, url), urlPrefix, static_cast<int>(arg3), static_cast<int>(arg4),
            StringLength(env, arg5), arg5Prefix, static_cast<int>(arg6), static_cast<int>(arg7),
            StringLength(env, arg8), arg8Prefix, arg9, StringLength(env, arg10), arg10Prefix,
            StringLength(env, arg11), arg11Prefix, StringLength(env, arg12), arg12Value,
            static_cast<int>(arg13),
            static_cast<int>(arg14), static_cast<int>(arg15), arg16, static_cast<int>(arg17),
            static_cast<int>(arg18), static_cast<long long>(arg19),
            static_cast<long long>(arg20));
    fflush(stderr);

    LoadUrlFn real = ResolveRealLoadUrl();
    if (real == nullptr) {
        fprintf(stderr, "[WESTLAKE-WEBNAV-707] missing real implementation\n");
        fflush(stderr);
        return nullptr;
    }
    jobject result = real(
        env, clazz, native_controller, controller, url, arg3, arg4, arg5, arg6, arg7,
        arg8, arg9, arg10, arg11, arg12, arg13, arg14, arg15, arg16, arg17, arg18,
        arg19, arg20);
    fprintf(stderr, "[WESTLAKE-WEBNAV-707] exit result=%p exception=%d\n",
            result, env->ExceptionCheck() ? 1 : 0);
    fflush(stderr);
    return result;
}

extern "C" __attribute__((visibility("default"))) void Java_J_N_M0uS2SDH(
    JNIEnv* env, jclass clazz, jlong native_web_contents, jstring script, jobject callback)
{
    const int scriptLength = StringLength(env, script);
    char prefix[256];
    CopyPrefix(env, script, prefix, sizeof(prefix));
    const bool isSetContent = strstr(prefix, "typeof setContent") != nullptr;
    fprintf(stderr,
            "[WESTLAKE-WEBEVAL-710] enter native=0x%llx scriptLen=%d"
            " script=\"%s\" callback=%p\n",
            static_cast<unsigned long long>(native_web_contents), scriptLength,
            prefix, callback);
    fflush(stderr);
    EnsureSoftwareWebViewLayer(env, native_web_contents);
    DumpScript(env, script, scriptLength);
    if (!env->ExceptionCheck() && isSetContent) {
        ProbeTopmostWebViewState(env, native_web_contents);
        ProbePrimaryRenderFrame(env, native_web_contents);
    }

    EvaluateJavaScriptFn real = ResolveRealEvaluateJavaScript();
    if (real == nullptr) {
        fprintf(stderr, "[WESTLAKE-WEBEVAL-710] missing real implementation\n");
        fflush(stderr);
        return;
    }

// Historical mutation probes (§711/§713/§717/§718) are retained below only for provenance.
// Disable them as a group: §720 must observe Toutiao's unmodified WebView contract.
#if 0
    // WESTLAKE §717 sequencing diagnostic. A 45,918-byte ByteDance bootstrap runs after the
    // template is ready but before every article setContent call. The renderer channel is live
    // before it and unobservable afterward. Suppress only this bootstrap for one A/B run, then
    // let the signed §713 probe test the same frame at the normal article-injection point.
    if (!env->ExceptionCheck() && scriptLength > 40000) {
        fprintf(stderr,
                "[WESTLAKE-WEBEVAL-717] suppressed pre-article bootstrap len=%d\n",
                scriptLength);
        fflush(stderr);
        return;
    }

    // WESTLAKE §718 one-variable A/B diagnostic. At this point the article template has
    // executed and reported setContentReady. Replace the first late eval with a stock WebView
    // navigation to a tiny document, on the same Android main thread. If the title/height change,
    // navigation and renderer scheduling remain healthy and only late eval is broken; if they do
    // not, the post-template main-frame channel is broadly inert. Suppress retry evals so they
    // cannot race or mutate the diagnostic page. Remove after this single A/B result.
    if (!env->ExceptionCheck() && strstr(prefix, "typeof setContent") != nullptr) {
        static bool submittedNavigation;
        if (!submittedNavigation) {
            submittedNavigation = true;
            ProbePrimaryRenderFrame(env, native_web_contents);
            jobject webView = FindTopmostWebView(env);
            jclass webViewClass = env->FindClass("android/webkit/WebView");
            jmethodID loadData = webViewClass != nullptr
                ? env->GetMethodID(webViewClass, "loadDataWithBaseURL",
                    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
                    "Ljava/lang/String;Ljava/lang/String;)V")
                : nullptr;
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (webView != nullptr && loadData != nullptr) {
                jstring base = env->NewStringUTF("https://westlake.invalid/718/");
                jstring data = env->NewStringUTF(
                    "<!doctype html><html><head><title>WL718_NAV</title></head>"
                    "<body style='min-height:2400px'><h1>WL718 navigation executed</h1>"
                    "<p>post-template renderer navigation is alive</p></body></html>");
                jstring mime = env->NewStringUTF("text/html");
                jstring encoding = env->NewStringUTF("UTF-8");
                jstring history = env->NewStringUTF("https://westlake.invalid/718/history");
                env->CallVoidMethod(webView, loadData, base, data, mime, encoding, history);
                fprintf(stderr,
                        "[WESTLAKE-WEBNAV-718] submitted static page exception=%d\n",
                        env->ExceptionCheck() ? 1 : 0);
                fflush(stderr);
                env->DeleteLocalRef(base);
                env->DeleteLocalRef(data);
                env->DeleteLocalRef(mime);
                env->DeleteLocalRef(encoding);
                env->DeleteLocalRef(history);
                env->DeleteLocalRef(webView);
            } else {
                fprintf(stderr,
                        "[WESTLAKE-WEBNAV-718] unable to locate WebView=%p loadData=%p\n",
                        webView, reinterpret_cast<void*>(loadData));
                fflush(stderr);
                if (webView != nullptr) env->DeleteLocalRef(webView);
            }
        }
        return;
    }

    // WESTLAKE §713 one-variable A/B diagnostic. At this point the article template has
    // executed and reported setContentReady, but the subsequent 14 KB setContent program is
    // followed by a permanently empty DOM. Replace only that program (not later setExtra or
    // bridge traffic) with a tiny mutation at the identical WebContents/timing boundary. This
    // separates a wedged late-evaluation transport from the content program itself. Remove after
    // the A/B result is captured.
    if (!env->ExceptionCheck() && strstr(prefix, "typeof setContent") != nullptr) {
        jstring simpleMutation = env->NewStringUTF(
            "javascript:(function(){document.title='WL713_SIMPLE';"
            "document.body.style.minHeight='2400px';"
            "document.body.innerHTML='<h1>WL713 simple mutation executed</h1>'"
            "+'<p>late WebContents evaluation transport is alive</p>';"
            "window.location.href='bytedance://wl716executed';})()");
        if (simpleMutation != nullptr) {
            wl_ALooper_traceNext(20);
            real(env, clazz, native_web_contents, simpleMutation, callback);
            env->DeleteLocalRef(simpleMutation);
        }
        fprintf(stderr,
                "[WESTLAKE-WEBEVAL-713] replaced article program exception=%d\n",
                env->ExceptionCheck() ? 1 : 0);
        fflush(stderr);
        return;
    }
#endif
    real(env, clazz, native_web_contents, script, callback);
    fprintf(stderr, "[WESTLAKE-WEBEVAL-710] exit exception=%d\n",
            env->ExceptionCheck() ? 1 : 0);
    fflush(stderr);

#if 0
    // WESTLAKE §711 diagnostic: Toutiao's complete setContent() program reaches this boundary
    // but Android continues to report contentHeight=1.  Submit a second, independent expression
    // after that large program and expose its DOM observations through WebView.getTitle().  This
    // distinguishes failed JavaScript execution from a populated DOM whose layout/compositor
    // state never reaches the Android WebView peer.  It is deliberately gated to the one large
    // article injection and must be removed after the distinction is measured.
    if (!env->ExceptionCheck() && scriptLength > 5000) {
        jstring domProbe = env->NewStringUTF(
            "javascript:(function(){document.title='WL711:'+document.body.scrollHeight+':'"
            "+document.body.innerHTML.length+':'+(typeof setContent)})()");
        if (domProbe != nullptr) {
            real(env, clazz, native_web_contents, domProbe, nullptr);
            env->DeleteLocalRef(domProbe);
            fprintf(stderr, "[WESTLAKE-WEBEVAL-711] DOM title probe submitted exception=%d\n",
                    env->ExceptionCheck() ? 1 : 0);
            fflush(stderr);
        }
    }
#endif
}
