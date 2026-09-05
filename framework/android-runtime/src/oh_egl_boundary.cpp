// EGL optional-damage fallback for the pinned HWUI bridge. No fake extension
// advertisement, ignored EGL errors, or global software-rendering fallback.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <map>
#include <string>
#include <unistd.h>
namespace {
std::mutex mutex;
struct Capability { int mode; int swaps = 0; };
std::map<EGLDisplay, Capability> capabilities;
template<class T> T original(const char* name) {
    auto fn = reinterpret_cast<T>(dlsym(RTLD_NEXT, name));
    if (!fn) { fprintf(stderr,"[OH_EGL] missing core symbol %s\n",name); std::abort(); }
    return fn;
}
bool has(const char* text, const char* token) {
    if (!text) return false;
    size_t size = strlen(token);
    for (const char* p=text; (p=strstr(p,token)); p+=size)
        if ((p==text || p[-1]==' ') && (p[size]==0 || p[size]==' ')) return true;
    return false;
}
bool forced() {
    const char* value=getenv("WESTLAKE_EGL_TEST_NO_DAMAGE");
    return value && strcmp(value,"1")==0;
}
template<class T> T driver(const char* name) {
    static void* library=dlopen("libEGL.so",RTLD_NOW|RTLD_LOCAL);
    return library ? reinterpret_cast<T>(dlsym(library,name)) : nullptr;
}
}
extern "C" EGLAPI const char* EGLAPIENTRY eglQueryString(EGLDisplay display,EGLint name) {
    auto query=original<decltype(&eglQueryString)>("eglQueryString");
    const char* result=query(display,name);
    if (name!=EGL_EXTENSIONS || !forced() || !result) return result;
    static thread_local bool logged=false;
    if (!logged) { logged=true; fprintf(stderr,"[OH_EGL] TEST removing damage extensions pid=%d\n",getpid()); }
    // Explicit isolated fault-injection control: remove only the two optional
    // damage capabilities, never add an extension the driver lacks.
    static thread_local std::string filtered;
    filtered.clear();
    const char* cursor=result;
    while (*cursor) {
        while (*cursor==' ') ++cursor;
        const char* end=strchr(cursor,' '); if (!end) end=cursor+strlen(cursor);
        std::string token(cursor,end-cursor);
        if (token!="EGL_KHR_swap_buffers_with_damage" && token!="EGL_EXT_swap_buffers_with_damage") {
            if (!filtered.empty()) filtered+=' '; filtered+=token;
        }
        cursor=end;
    }
    return filtered.c_str();
}
extern "C" EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay display,EGLint* major,EGLint* minor) {
    EGLBoolean result=original<decltype(&eglInitialize)>("eglInitialize")(display,major,minor);
    if (result) { std::lock_guard<std::mutex> lock(mutex); capabilities.erase(display); }
    fprintf(stderr,"[OH_EGL] initialize pid=%d display=%p ok=%u\n",getpid(),display,result);
    return result;
}
extern "C" EGLAPI EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay display) {
    EGLBoolean result=original<decltype(&eglTerminate)>("eglTerminate")(display);
    if (result) { std::lock_guard<std::mutex> lock(mutex); capabilities.erase(display); }
    fprintf(stderr,"[OH_EGL] terminate pid=%d display=%p ok=%u\n",getpid(),display,result);
    return result;
}
extern "C" EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffersWithDamageKHR(
        EGLDisplay display,EGLSurface surface,EGLint* rects,EGLint count) {
    int mode;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto item=capabilities.find(display);
        if (item==capabilities.end()) {
            const char* names=original<decltype(&eglQueryString)>("eglQueryString")(display,EGL_EXTENSIONS);
            mode=forced() ? 0 : has(names,"EGL_KHR_swap_buffers_with_damage") ? 1
                    : has(names,"EGL_EXT_swap_buffers_with_damage") ? 2 : 0;
            item=capabilities.emplace(display,Capability{mode,0}).first;
            fprintf(stderr,"[OH_EGL] swap mode=%s query=%s pid=%d\n",mode==1?"KHR":mode==2?"EXT":"full",names?"ok":"null",getpid());
        }
        mode=item->second.mode;
        ++item->second.swaps;
    }
    using Swap=EGLBoolean (*)(EGLDisplay,EGLSurface,EGLint*,EGLint);
    const char* name=mode==1 ? "eglSwapBuffersWithDamageKHR" : "eglSwapBuffersWithDamageEXT";
    auto damage=mode ? driver<Swap>(name) : nullptr;
    if (damage) return damage(display,surface,rects,count);
    auto swap=driver<decltype(&eglSwapBuffers)>("eglSwapBuffers");
    if (!swap) { fprintf(stderr,"[OH_EGL] missing core driver swap\n"); std::abort(); }
    return swap(display,surface);
}
