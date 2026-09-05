/* Narrow transition shim for the pinned bridge's exported startup hooks.
 * Serialize cache priming and post-bind publication; a late background refresh
 * must not withdraw a capability already published for this child. Delegate
 * provider availability, app-context and package verification unchanged.
 */
#include <jni.h>
#include <dlfcn.h>
#include <mutex>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
namespace {
std::recursive_mutex mutex;
bool published = false;
bool networkRegistered = false;
bool hardwareInstalled = false;
bool tlsInstalled = false;
pid_t owner = 0;
using Hook = bool (*)(JNIEnv*);
bool call(JNIEnv* env, bool afterBind) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (owner != getpid()) { owner = getpid(); published = false; networkRegistered = false; hardwareInstalled = false; tlsInstalled = false; }
    if (afterBind && !tlsInstalled) {
        const char* path = getenv("WESTLAKE_TLS_HELPER_PATH");
        if (path && *path) {
            void* helper = dlopen(path, RTLD_NOW);
            auto install = helper ? reinterpret_cast<int (*)(JNIEnv*)>(dlsym(helper, "westlake_tls_child_register")) : nullptr;
            if (!install || !install(env)) {
                fprintf(stderr, "[OH_WEBVIEW_ORDER] TLS chain helper registration failed\n");
                return false;
            }
        }
        tlsInstalled = true;
    }
    if (afterBind && !networkRegistered) {
        const char* path = getenv("WESTLAKE_NET_HELPER_PATH");
        void* helper = path ? dlopen(path, RTLD_NOW | RTLD_NOLOAD) : nullptr;
        auto install = helper ? reinterpret_cast<int (*)(JNIEnv*)>(dlsym(helper, "westlake_network_child_register")) : nullptr;
        // Older baseline helpers have no re-registration hook. Leave those
        // configurations alone; only the new candidate exports it.
        bool ok = !install || install(env);
        if (helper) dlclose(helper);
        if (!ok) { fprintf(stderr, "[OH_WEBVIEW_ORDER] network boundary registration failed\n"); return false; }
        networkRegistered = true;
    }
    if (afterBind && !hardwareInstalled) {
        const char* path = getenv("WESTLAKE_HWUI_HELPER_PATH");
        if (path && *path) {
            // Retain the DSO: RecordingCanvas now holds its native entry point.
            void* helper = dlopen(path, RTLD_NOW);
            auto install = helper ? reinterpret_cast<int (*)(JNIEnv*)>(dlsym(helper, "westlake_install_webview_functor")) : nullptr;
            if (!install || !install(env)) {
                fprintf(stderr, "[OH_WEBVIEW_ORDER] hardware helper failed: %s\n", dlerror());
                return false;
            }
        }
        hardwareInstalled = true;
    }
    const bool publish = afterBind || published;
    const char* name = publish ? "_Z44wl_publish_webview_update_service_after_bindP7_JNIEnv"
                              : "_Z31wl_prime_webview_update_serviceP7_JNIEnv";
    auto original = reinterpret_cast<Hook>(dlsym(RTLD_NEXT, name));
    if (!original) { fprintf(stderr, "[OH_WEBVIEW_ORDER] missing original startup hook\n"); return false; }
    bool ok = original(env);
    if (ok && publish) {
        published = true;
        fprintf(stderr, "[OH_WEBVIEW_ORDER] published capability retained pid=%d\n", owner);
    }
    return ok;
}
}
bool wl_prime_webview_update_service(JNIEnv* env) { return call(env, false); }
bool wl_publish_webview_update_service_after_bind(JNIEnv* env) { return call(env, true); }
