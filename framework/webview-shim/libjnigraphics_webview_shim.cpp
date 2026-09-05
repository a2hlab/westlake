#include <android/bitmap.h>

#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

// Android's libjnigraphics reaches directly into libhwui's native Bitmap. The
// OH host must keep that AOSP C++ ABI local, so libjnigraphics cannot safely
// link to or dlsym the private GraphicsJNI/Skia types. Use Bitmap's public Java
// pixel-buffer contract instead: expose a temporary native buffer while locked,
// then copy it back on unlock. This keeps the compatibility boundary in one
// generic NDK shim and works for application codecs such as HEIF without
// exporting libhwui's C++ ABI process-wide.

struct LockedBitmap {
    jobject bitmap;
    void *pixels;
    jlong byte_count;
};

constexpr size_t kMaxLockedBitmaps = 64;
LockedBitmap g_locked[kMaxLockedBitmaps] = {};
pthread_mutex_t g_locked_mutex = PTHREAD_MUTEX_INITIALIZER;

bool get_int(JNIEnv *env, jobject bitmap, const char *name, jint *value)
{
    jclass cls = env->GetObjectClass(bitmap);
    if (cls == nullptr) {
        return false;
    }
    jmethodID method = env->GetMethodID(cls, name, "()I");
    if (method == nullptr) {
        env->DeleteLocalRef(cls);
        return false;
    }
    *value = env->CallIntMethod(bitmap, method);
    env->DeleteLocalRef(cls);
    return !env->ExceptionCheck();
}

AndroidBitmapFormat get_format(JNIEnv *env, jobject bitmap)
{
    jclass bitmap_cls = env->GetObjectClass(bitmap);
    if (bitmap_cls == nullptr) {
        return ANDROID_BITMAP_FORMAT_NONE;
    }
    jmethodID get_config = env->GetMethodID(
            bitmap_cls, "getConfig", "()Landroid/graphics/Bitmap$Config;");
    jobject config = get_config != nullptr
            ? env->CallObjectMethod(bitmap, get_config) : nullptr;
    env->DeleteLocalRef(bitmap_cls);
    if (config == nullptr || env->ExceptionCheck()) {
        return ANDROID_BITMAP_FORMAT_NONE;
    }

    jclass config_cls = env->GetObjectClass(config);
    jmethodID to_string = config_cls != nullptr
            ? env->GetMethodID(config_cls, "toString", "()Ljava/lang/String;")
            : nullptr;
    jstring value = to_string != nullptr
            ? static_cast<jstring>(env->CallObjectMethod(config, to_string))
            : nullptr;
    AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_NONE;
    if (value != nullptr && !env->ExceptionCheck()) {
        const char *text = env->GetStringUTFChars(value, nullptr);
        if (text != nullptr) {
            if (strcmp(text, "ARGB_8888") == 0) {
                format = ANDROID_BITMAP_FORMAT_RGBA_8888;
            } else if (strcmp(text, "RGB_565") == 0) {
                format = ANDROID_BITMAP_FORMAT_RGB_565;
            } else if (strcmp(text, "ARGB_4444") == 0) {
                format = ANDROID_BITMAP_FORMAT_RGBA_4444;
            } else if (strcmp(text, "ALPHA_8") == 0) {
                format = ANDROID_BITMAP_FORMAT_A_8;
            } else if (strcmp(text, "RGBA_F16") == 0) {
                format = ANDROID_BITMAP_FORMAT_RGBA_F16;
            } else if (strcmp(text, "RGBA_1010102") == 0) {
                format = ANDROID_BITMAP_FORMAT_RGBA_1010102;
            }
            env->ReleaseStringUTFChars(value, text);
        }
    }
    if (value != nullptr) {
        env->DeleteLocalRef(value);
    }
    if (config_cls != nullptr) {
        env->DeleteLocalRef(config_cls);
    }
    env->DeleteLocalRef(config);
    return format;
}

bool copy_pixels(JNIEnv *env, jobject bitmap, void *pixels, jlong byte_count,
                 const char *method_name)
{
    jobject buffer = env->NewDirectByteBuffer(pixels, byte_count);
    jclass bitmap_cls = env->GetObjectClass(bitmap);
    jmethodID method = bitmap_cls != nullptr
            ? env->GetMethodID(bitmap_cls, method_name, "(Ljava/nio/Buffer;)V")
            : nullptr;
    if (buffer == nullptr || method == nullptr || env->ExceptionCheck()) {
        if (buffer != nullptr) {
            env->DeleteLocalRef(buffer);
        }
        if (bitmap_cls != nullptr) {
            env->DeleteLocalRef(bitmap_cls);
        }
        return false;
    }
    env->CallVoidMethod(bitmap, method, buffer);
    env->DeleteLocalRef(buffer);
    env->DeleteLocalRef(bitmap_cls);
    return !env->ExceptionCheck();
}

}  // namespace

extern "C" int AndroidBitmap_getInfo(JNIEnv *env, jobject bitmap,
                                     AndroidBitmapInfo *info)
{
    if (env == nullptr || bitmap == nullptr) {
        return ANDROID_BITMAP_RESULT_BAD_PARAMETER;
    }
    jint width = 0;
    jint height = 0;
    jint stride = 0;
    if (!get_int(env, bitmap, "getWidth", &width) ||
        !get_int(env, bitmap, "getHeight", &height) ||
        !get_int(env, bitmap, "getRowBytes", &stride)) {
        return ANDROID_BITMAP_RESULT_JNI_EXCEPTION;
    }
    if (info != nullptr) {
        info->width = static_cast<uint32_t>(width);
        info->height = static_cast<uint32_t>(height);
        info->stride = static_cast<uint32_t>(stride);
        info->format = get_format(env, bitmap);
        info->flags = ANDROID_BITMAP_FLAGS_ALPHA_PREMUL;
    }
    fprintf(stderr, "[WESTLAKE-JNIGRAPHICS-824] getInfo %dx%d stride=%d format=%d\n",
            width, height, stride, info != nullptr ? info->format : 0);
    return env->ExceptionCheck()
            ? ANDROID_BITMAP_RESULT_JNI_EXCEPTION
            : ANDROID_BITMAP_RESULT_SUCCESS;
}

extern "C" int AndroidBitmap_lockPixels(JNIEnv *env, jobject bitmap,
                                        void **addr_ptr)
{
    if (env == nullptr || bitmap == nullptr || addr_ptr == nullptr) {
        return ANDROID_BITMAP_RESULT_BAD_PARAMETER;
    }
    jint height = 0;
    jint stride = 0;
    if (!get_int(env, bitmap, "getHeight", &height) ||
        !get_int(env, bitmap, "getRowBytes", &stride) ||
        height <= 0 || stride <= 0) {
        return ANDROID_BITMAP_RESULT_JNI_EXCEPTION;
    }
    const jlong byte_count = static_cast<jlong>(height) * stride;
    void *pixels = calloc(1, static_cast<size_t>(byte_count));
    if (pixels == nullptr) {
        return ANDROID_BITMAP_RESULT_ALLOCATION_FAILED;
    }

    // Preserve existing pixels for native clients that update only a region.
    if (!copy_pixels(env, bitmap, pixels, byte_count, "copyPixelsToBuffer")) {
        // A fresh mutable Bitmap may not yet expose readable pixels. The codec
        // is still allowed to fill the zeroed buffer, so discard that exception.
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    pthread_mutex_lock(&g_locked_mutex);
    size_t slot = kMaxLockedBitmaps;
    for (size_t i = 0; i < kMaxLockedBitmaps; ++i) {
        if (g_locked[i].bitmap == nullptr) {
            slot = i;
            break;
        }
    }
    if (slot == kMaxLockedBitmaps) {
        pthread_mutex_unlock(&g_locked_mutex);
        free(pixels);
        return ANDROID_BITMAP_RESULT_ALLOCATION_FAILED;
    }
    g_locked[slot].bitmap = env->NewGlobalRef(bitmap);
    g_locked[slot].pixels = pixels;
    g_locked[slot].byte_count = byte_count;
    pthread_mutex_unlock(&g_locked_mutex);
    if (g_locked[slot].bitmap == nullptr) {
        free(pixels);
        return ANDROID_BITMAP_RESULT_JNI_EXCEPTION;
    }

    *addr_ptr = pixels;
    fprintf(stderr, "[WESTLAKE-JNIGRAPHICS-824] lockPixels bytes=%lld pixels=%p\n",
            static_cast<long long>(byte_count), pixels);
    return ANDROID_BITMAP_RESULT_SUCCESS;
}

extern "C" int AndroidBitmap_unlockPixels(JNIEnv *env, jobject bitmap)
{
    if (env == nullptr || bitmap == nullptr) {
        return ANDROID_BITMAP_RESULT_BAD_PARAMETER;
    }

    LockedBitmap locked = {};
    pthread_mutex_lock(&g_locked_mutex);
    for (size_t i = 0; i < kMaxLockedBitmaps; ++i) {
        if (g_locked[i].bitmap != nullptr &&
            env->IsSameObject(g_locked[i].bitmap, bitmap)) {
            locked = g_locked[i];
            g_locked[i] = {};
            break;
        }
    }
    pthread_mutex_unlock(&g_locked_mutex);
    if (locked.bitmap == nullptr) {
        return ANDROID_BITMAP_RESULT_BAD_PARAMETER;
    }

    const bool copied = copy_pixels(env, bitmap, locked.pixels,
                                    locked.byte_count, "copyPixelsFromBuffer");
    env->DeleteGlobalRef(locked.bitmap);
    free(locked.pixels);
    fprintf(stderr, "[WESTLAKE-JNIGRAPHICS-824] unlockPixels copied=%d\n",
            copied ? 1 : 0);
    return copied ? ANDROID_BITMAP_RESULT_SUCCESS
                  : ANDROID_BITMAP_RESULT_JNI_EXCEPTION;
}
