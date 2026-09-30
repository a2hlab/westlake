// OH port: the subset of AOSP's android/graphics/bitmap.h (graphics::Bitmap) that
// android/opengl/util.cpp uses, over the NDK AndroidBitmap_* API of the runtime's
// libjnigraphics.so. Resolved at run time, so this library gains no link-time dependency.
#pragma once
#include <dlfcn.h>
#include <jni.h>
#include <stdint.h>

enum AndroidBitmapFormat {
    ANDROID_BITMAP_FORMAT_NONE = 0,
    ANDROID_BITMAP_FORMAT_RGBA_8888 = 1,
    ANDROID_BITMAP_FORMAT_RGB_565 = 4,
    ANDROID_BITMAP_FORMAT_RGBA_4444 = 7,
    ANDROID_BITMAP_FORMAT_A_8 = 8,
    ANDROID_BITMAP_FORMAT_RGBA_F16 = 9,
    ANDROID_BITMAP_FORMAT_RGBA_1010102 = 10,
};

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    int32_t format;
    uint32_t flags;
} AndroidBitmapInfo;

namespace graphics {

class Bitmap {
  public:
    Bitmap(JNIEnv* env, jobject bitmap) : mEnv(env), mBitmap(bitmap) {
        Api& api = getApi();
        mValid = bitmap != nullptr && api.getInfo != nullptr && api.getInfo(env, bitmap, &mInfo) == 0;
    }
    ~Bitmap() {
        if (mPixels != nullptr) getApi().unlockPixels(mEnv, mBitmap);
    }
    bool isValid() const { return mValid; }
    AndroidBitmapInfo getInfo() const { return mInfo; }
    void* getPixels() {
        Api& api = getApi();
        if (mValid && mPixels == nullptr && api.lockPixels != nullptr &&
            api.lockPixels(mEnv, mBitmap, &mPixels) != 0) {
            mPixels = nullptr;
        }
        return mPixels;
    }

  private:
    struct Api {
        int (*getInfo)(JNIEnv*, jobject, AndroidBitmapInfo*);
        int (*lockPixels)(JNIEnv*, jobject, void**);
        int (*unlockPixels)(JNIEnv*, jobject);
    };
    static Api& getApi() {
        static Api api = [] {
            void* lib = dlopen("libjnigraphics.so", RTLD_NOW);
            auto find = [lib](const char* name) {
                void* fn = dlsym(RTLD_DEFAULT, name);
                return fn != nullptr || lib == nullptr ? fn : dlsym(lib, name);
            };
            Api a;
            a.getInfo = reinterpret_cast<int (*)(JNIEnv*, jobject, AndroidBitmapInfo*)>(find("AndroidBitmap_getInfo"));
            a.lockPixels = reinterpret_cast<int (*)(JNIEnv*, jobject, void**)>(find("AndroidBitmap_lockPixels"));
            a.unlockPixels = reinterpret_cast<int (*)(JNIEnv*, jobject)>(find("AndroidBitmap_unlockPixels"));
            return a;
        }();
        return api;
    }
    JNIEnv* mEnv;
    jobject mBitmap;
    AndroidBitmapInfo mInfo = {};
    void* mPixels = nullptr;
    bool mValid = false;
};

}  // namespace graphics
