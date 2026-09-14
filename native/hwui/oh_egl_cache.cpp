// Keep the host EGL driver's own persistent shader cache. Android supplies a
// filename; OH's EGL wrapper accepts its existing parent cache directory.
#include <private/EGL/cache.h>
#include <string>
namespace android {
void egl_set_cache_filename(const char* filename) {
    if (!filename || !*filename) return;
    std::string path(filename);
    auto separator = path.find_last_of('/');
    OHOS::EglSetCacheDir(separator == std::string::npos ? "." :
                        separator == 0 ? "/" : path.substr(0, separator));
}
}
