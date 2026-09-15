// Android's FontManagerService normally serializes the immutable font map and
// the application deserializes it. This process has no Android system_server;
// perform the same framework calls before forking, using only the staged fonts.
#include <jni.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

int initializeSourceFontMap(JNIEnv* env) {
    const char* root = getenv("WESTLAKE_RUNTIME_ROOT");
    if (!root || !*root) return 0;
    if (access((std::string(root) + "/etc/fonts.xml").c_str(), R_OK)) {
        fprintf(stderr, "SOURCE_FONT_MAP_FAILED missing staged fonts.xml\n");
        return -1;
    }
    if (env->PushLocalFrame(8)) return -1;
    auto initialize = [&]() -> bool {
        jclass typeface = env->FindClass("android/graphics/Typeface");
        if (!typeface) return false;
        auto load = env->GetStaticMethodID(typeface, "loadPreinstalledSystemFontMap", "()V");
        if (!load) return false;
        env->CallStaticVoidMethod(typeface, load);
        if (env->ExceptionCheck()) return false;
        auto get = env->GetStaticMethodID(typeface, "getSystemFontMap", "()Ljava/util/Map;");
        if (!get) return false;
        jobject map = env->CallStaticObjectMethod(typeface, get);
        if (!map || env->ExceptionCheck()) return false;
        auto serialize = env->GetStaticMethodID(typeface, "serializeFontMap",
                "(Ljava/util/Map;)Landroid/os/SharedMemory;");
        if (!serialize) return false;
        jobject memory = env->CallStaticObjectMethod(typeface, serialize, map);
        if (!memory || env->ExceptionCheck()) return false;
        auto set = env->GetStaticMethodID(typeface, "setSystemFontMap", "(Landroid/os/SharedMemory;)V");
        if (!set) return false;
        env->CallStaticVoidMethod(typeface, set, memory);
        // Typeface retains the read-only mapping and its SharedMemory owner.
        return !env->ExceptionCheck();
    };
    bool ok = initialize();
    if (env->ExceptionCheck()) env->ExceptionDescribe();
    env->PopLocalFrame(nullptr);
    fprintf(stderr, "SOURCE_FONT_MAP_%s\n", ok ? "PASS" : "FAILED");
    return ok ? 0 : -1;
}
