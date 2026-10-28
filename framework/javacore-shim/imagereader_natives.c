/*
 * android.media.ImageReader over an OH consumer surface, bound from libwl_missing_natives'
 * JNI_OnLoad.
 *
 * None of its natives is registered and its class initializer calls nativeClassInit, so the class
 * itself failed: 756 of the 982 HK top-1000 apps reach it (camera previews, QR scanners, frames
 * read back from a decoder or from GL). OH's consumer surface has the same shape as Android's
 * BufferItemConsumer: a producer window to hand out, buffers acquired and released by the consumer,
 * and a callback when a frame is queued. So:
 *
 *   - nativeInit creates the consumer (OH_ConsumerSurface_Create) at the reader's size, with CPU
 *     read usage and the reader's format asked of its producer window, and posts each frame to
 *     ImageReader.postEventFromNative as Android's listener does.
 *   - nativeGetSurface answers a Surface whose native object is that producer window, which is how
 *     this runtime's Surface, ANativeWindow_fromSurface, EGL and MediaCodec all name a window.
 *   - nativeImageSetup acquires a buffer and maps it; the image's planes come from OH's plane
 *     layout (OH_NativeBuffer_MapPlanes); nativeReleaseImage unmaps it and gives it back.
 *
 * Not ported, each answered as AOSP answers when it cannot: detaching an image (ImageWriter's
 * attach path), the GraphicBuffer planes ImageWriter and the camera service use, and a
 * HardwareBuffer for an image (null, which Image.getHardwareBuffer allows). Timestamps are the
 * acquire time on CLOCK_MONOTONIC, not the producer's. A closed reader's consumer is kept, as the
 * runtime's SurfaceTexture keeps its own, because a producer may still hold its window.
 */
#include <dlfcn.h>
#include <jni.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define COUNT(t) ((int) (sizeof(t) / sizeof((t)[0])))

static void ilog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void ilog(const char* fmt, ...)
{
    char line[256];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int) sizeof(line) - 2) n = (int) sizeof(line) - 2;
    line[n++] = '\n';
    (void) write(2, line, (size_t) n);
}

typedef struct OH_NativeImage OH_NativeImage;
typedef struct NativeWindow OHNativeWindow;
typedef struct NativeWindowBuffer OHNativeWindowBuffer;
typedef struct OH_NativeBuffer OH_NativeBuffer;
typedef void (*OnFrameAvailable)(void* context);
typedef struct { void* context; OnFrameAvailable onFrameAvailable; } OnFrameAvailableListener;
typedef struct { int32_t width, height, format, usage, stride; } NativeBufferConfig;
typedef struct { uint64_t offset; uint32_t rowStride, columnStride; } NativeBufferPlane;
typedef struct { uint32_t planeCount; NativeBufferPlane planes[4]; } NativeBufferPlanes;

/* OH's NATIVEBUFFER_PIXEL_FMT_* (buffer_common.h) and usage bits (native_buffer.h). */
enum { OH_FMT_RGB_565 = 3, OH_FMT_RGBX_8888 = 11, OH_FMT_RGBA_8888 = 12, OH_FMT_YCBCR_420_SP = 24,
       OH_FMT_YCRCB_420_SP = 25, OH_FMT_YCBCR_420_P = 28, OH_FMT_YCRCB_420_P = 29 };
enum { OH_USAGE_CPU_READ = 1 << 0, OH_USAGE_MEM_DMA = 1 << 3, OH_USAGE_CPU_READ_OFTEN = 1 << 16 };
/* OH_NativeWindow_NativeWindowHandleOpt codes (external_window.h). */
enum { OH_SET_BUFFER_GEOMETRY = 0, OH_SET_FORMAT = 3, OH_SET_USAGE = 5 };

/* Android's formats: PixelFormat / ImageFormat / HardwareBuffer. */
enum { A_RGBA_8888 = 1, A_RGBX_8888 = 2, A_RGB_565 = 4, A_NV21 = 0x11, A_PRIVATE = 0x22,
       A_YUV_420_888 = 0x23 };
/* ImageReader's ACQUIRE_* codes. */
enum { ACQUIRE_SUCCESS = 0, ACQUIRE_NO_BUFS = 1, ACQUIRE_MAX_IMAGES = 2 };

static struct {
    int loaded;
    OH_NativeImage* (*create)(void);
    int32_t (*setDefaultUsage)(OH_NativeImage*, uint64_t);
    int32_t (*setDefaultSize)(OH_NativeImage*, int32_t, int32_t);
    OHNativeWindow* (*window)(OH_NativeImage*);
    int32_t (*setListener)(OH_NativeImage*, OnFrameAvailableListener);
    int32_t (*unsetListener)(OH_NativeImage*);
    int32_t (*acquire)(OH_NativeImage*, OHNativeWindowBuffer**, int*);
    int32_t (*release)(OH_NativeImage*, OHNativeWindowBuffer*, int);
    int32_t (*fromWindowBuffer)(OHNativeWindowBuffer*, OH_NativeBuffer**);
    void (*config)(OH_NativeBuffer*, NativeBufferConfig*);
    int32_t (*map)(OH_NativeBuffer*, void**);
    int32_t (*mapPlanes)(OH_NativeBuffer*, void**, NativeBufferPlanes*);
    int32_t (*unmap)(OH_NativeBuffer*);
    int32_t (*windowOpt)(OHNativeWindow*, int, ...);
} oh;

static int load_oh(void)
{
    void *image, *buffer, *window;
    if (oh.loaded) return oh.loaded > 0;
    oh.loaded = -1;
    image = dlopen("libnative_image.so", RTLD_NOW);
    buffer = dlopen("libnative_buffer.so", RTLD_NOW);
    window = dlopen("libnative_window.so", RTLD_NOW);
    if (image == NULL || buffer == NULL || window == NULL) {
        ilog("[WL-IMAGEREADER] OH image libraries unavailable: %s", dlerror());
        return 0;
    }
#define SYM(field, lib, name) *(void**) &oh.field = dlsym(lib, name)
    SYM(create, image, "OH_ConsumerSurface_Create");
    SYM(setDefaultUsage, image, "OH_ConsumerSurface_SetDefaultUsage");
    SYM(setDefaultSize, image, "OH_ConsumerSurface_SetDefaultSize");
    SYM(window, image, "OH_NativeImage_AcquireNativeWindow");
    SYM(setListener, image, "OH_NativeImage_SetOnFrameAvailableListener");
    SYM(unsetListener, image, "OH_NativeImage_UnsetOnFrameAvailableListener");
    SYM(acquire, image, "OH_NativeImage_AcquireNativeWindowBuffer");
    SYM(release, image, "OH_NativeImage_ReleaseNativeWindowBuffer");
    SYM(fromWindowBuffer, buffer, "OH_NativeBuffer_FromNativeWindowBuffer");
    SYM(config, buffer, "OH_NativeBuffer_GetConfig");
    SYM(map, buffer, "OH_NativeBuffer_Map");
    SYM(mapPlanes, buffer, "OH_NativeBuffer_MapPlanes");
    SYM(unmap, buffer, "OH_NativeBuffer_Unmap");
    SYM(windowOpt, window, "OH_NativeWindow_NativeWindowHandleOpt");
#undef SYM
    if (!oh.create || !oh.window || !oh.setListener || !oh.acquire || !oh.release || !oh.fromWindowBuffer
            || !oh.config || !oh.map || !oh.unmap) {
        ilog("[WL-IMAGEREADER] OH consumer surface API incomplete");
        return 0;
    }
    oh.loaded = 1;
    return 1;
}

typedef struct {
    OH_NativeImage* image;
    OHNativeWindow* window;
    jobject weakSelf;            /* global ref to the WeakReference postEventFromNative takes */
    int width, height, maxImages, format;
    int acquired;
    pthread_mutex_t mu;
} Reader;

typedef struct {
    OHNativeWindowBuffer* windowBuffer;
    OH_NativeBuffer* buffer;
    uint8_t* addr;
    NativeBufferConfig config;
    NativeBufferPlanes planes;
    bool hasPlanes;
} Frame;

static JavaVM* g_vm;
static jclass g_readerClass, g_surfaceClass, g_planeClass;
static jfieldID g_nativeContext, g_nativeBuffer, g_timestamp, g_transform, g_scalingMode;
static jmethodID g_postEvent, g_surfaceInit, g_planeInit;

static Reader* reader_of(JNIEnv* env, jobject thiz)
{
    return (Reader*) (intptr_t) (*env)->GetLongField(env, thiz, g_nativeContext);
}

static Frame* frame_of(JNIEnv* env, jobject image)
{
    return (Frame*) (intptr_t) (*env)->GetLongField(env, image, g_nativeBuffer);
}

static void throw_named(JNIEnv* env, const char* cls, const char* message)
{
    jclass c = (*env)->FindClass(env, cls);
    if (c != NULL) (*env)->ThrowNew(env, c, message);
}

/* A frame was queued: ImageReader.postEventFromNative(weakSelf), from OH's listener thread. */
static void on_frame(void* context)
{
    Reader* r = context;
    JNIEnv* env = NULL;
    bool attached = false;
    if (g_vm == NULL || r->weakSelf == NULL) return;
    if ((*g_vm)->GetEnv(g_vm, (void**) &env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return;
        attached = true;
    }
    (*env)->CallStaticVoidMethod(env, g_readerClass, g_postEvent, r->weakSelf);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}

static int oh_format_for(int halFormat)
{
    switch (halFormat) {
    case A_RGBA_8888: return OH_FMT_RGBA_8888;
    case A_RGBX_8888: return OH_FMT_RGBX_8888;
    case A_RGB_565: return OH_FMT_RGB_565;
    case A_YUV_420_888: return OH_FMT_YCBCR_420_SP;
    case A_NV21: return OH_FMT_YCRCB_420_SP;
    default: return -1;          /* PRIVATE and the rest: the producer's own format */
    }
}

static bool oh_format_is_yuv(int format)
{
    return format == OH_FMT_YCBCR_420_SP || format == OH_FMT_YCRCB_420_SP
           || format == OH_FMT_YCBCR_420_P || format == OH_FMT_YCRCB_420_P;
}

static int bytes_per_pixel(int format)
{
    return format == OH_FMT_RGB_565 ? 2 : 4;
}

static void ir_class_init(JNIEnv* env, jclass clazz)
{
    jclass image = (*env)->FindClass(env, "android/media/ImageReader$SurfaceImage");
    jclass plane = (*env)->FindClass(env, "android/media/ImageReader$SurfaceImage$SurfacePlane");
    jclass surface = (*env)->FindClass(env, "android/view/Surface");
    if (image == NULL || plane == NULL || surface == NULL) return;
    g_readerClass = (*env)->NewGlobalRef(env, clazz);
    g_planeClass = (*env)->NewGlobalRef(env, plane);
    g_surfaceClass = (*env)->NewGlobalRef(env, surface);
    g_nativeContext = (*env)->GetFieldID(env, clazz, "mNativeContext", "J");
    g_postEvent = (*env)->GetStaticMethodID(env, clazz, "postEventFromNative", "(Ljava/lang/Object;)V");
    g_nativeBuffer = (*env)->GetFieldID(env, image, "mNativeBuffer", "J");
    g_timestamp = (*env)->GetFieldID(env, image, "mTimestamp", "J");
    g_transform = (*env)->GetFieldID(env, image, "mTransform", "I");
    g_scalingMode = (*env)->GetFieldID(env, image, "mScalingMode", "I");
    g_planeInit = (*env)->GetMethodID(env, plane, "<init>",
                                      "(Landroid/media/ImageReader$SurfaceImage;IILjava/nio/ByteBuffer;)V");
    g_surfaceInit = (*env)->GetMethodID(env, surface, "<init>", "(J)V");
}

static void ir_init(JNIEnv* env, jobject thiz, jobject weakSelf, jint w, jint h, jint maxImages,
                    jlong usage, jint halFormat, jint dataSpace)
{
    Reader* r;
    OnFrameAvailableListener listener;
    int format = oh_format_for(halFormat);
    (void) usage;
    (void) dataSpace;
    if (!load_oh()) {
        throw_named(env, "java/lang/UnsupportedOperationException", "no consumer surface on this device");
        return;
    }
    r = calloc(1, sizeof(*r));
    if (r == NULL) {
        throw_named(env, "java/lang/OutOfMemoryError", "ImageReader");
        return;
    }
    pthread_mutex_init(&r->mu, NULL);
    r->width = w;
    r->height = h;
    r->maxImages = maxImages;
    r->format = halFormat;
    r->image = oh.create();
    r->window = r->image != NULL ? oh.window(r->image) : NULL;
    if (r->window == NULL) {
        ilog("[WL-IMAGEREADER] no consumer surface for %dx%d format %#x", w, h, halFormat);
        free(r);
        throw_named(env, "java/lang/IllegalStateException", "Failed to create a consumer surface");
        return;
    }
    if (oh.setDefaultSize != NULL) oh.setDefaultSize(r->image, w, h);
    if (oh.setDefaultUsage != NULL)
        oh.setDefaultUsage(r->image, OH_USAGE_CPU_READ | OH_USAGE_CPU_READ_OFTEN | OH_USAGE_MEM_DMA);
    if (oh.windowOpt != NULL) {
        oh.windowOpt(r->window, OH_SET_BUFFER_GEOMETRY, (int32_t) w, (int32_t) h);
        oh.windowOpt(r->window, OH_SET_USAGE,
                     (uint64_t) (OH_USAGE_CPU_READ | OH_USAGE_CPU_READ_OFTEN | OH_USAGE_MEM_DMA));
        if (format >= 0) oh.windowOpt(r->window, OH_SET_FORMAT, (int32_t) format);
    }
    r->weakSelf = (*env)->NewGlobalRef(env, weakSelf);
    listener.context = r;
    listener.onFrameAvailable = on_frame;
    oh.setListener(r->image, listener);
    (*env)->SetLongField(env, thiz, g_nativeContext, (jlong) (intptr_t) r);
    ilog("[WL-IMAGEREADER] reader %dx%d format %#x max %d -> window %p", w, h, halFormat, maxImages,
         (void*) r->window);
}

static void ir_close(JNIEnv* env, jobject thiz)
{
    Reader* r = reader_of(env, thiz);
    if (r == NULL) return;
    (*env)->SetLongField(env, thiz, g_nativeContext, 0);
    if (oh.unsetListener != NULL) oh.unsetListener(r->image);
    if (r->weakSelf != NULL) (*env)->DeleteGlobalRef(env, r->weakSelf);
    r->weakSelf = NULL;
    /* The consumer and its window stay: a producer may still hold the window (see the top). */
}

static jobject ir_get_surface(JNIEnv* env, jobject thiz)
{
    Reader* r = reader_of(env, thiz);
    if (r == NULL || g_surfaceInit == NULL) return NULL;
    return (*env)->NewObject(env, g_surfaceClass, g_surfaceInit, (jlong) (intptr_t) r->window);
}

static jint ir_image_setup(JNIEnv* env, jobject thiz, jobject image)
{
    Reader* r = reader_of(env, thiz);
    OHNativeWindowBuffer* windowBuffer = NULL;
    int fence = -1;
    Frame* f;
    void* addr = NULL;
    struct timespec now;
    if (r == NULL) return ACQUIRE_NO_BUFS;
    pthread_mutex_lock(&r->mu);
    if (r->acquired >= r->maxImages) {
        pthread_mutex_unlock(&r->mu);
        return ACQUIRE_MAX_IMAGES;
    }
    if (oh.acquire(r->image, &windowBuffer, &fence) != 0 || windowBuffer == NULL) {
        pthread_mutex_unlock(&r->mu);
        return ACQUIRE_NO_BUFS;
    }
    r->acquired++;
    pthread_mutex_unlock(&r->mu);
    if (fence >= 0) {
        struct pollfd p = { fence, POLLIN, 0 };
        (void) poll(&p, 1, 1000);
        close(fence);
    }
    f = calloc(1, sizeof(*f));
    if (f == NULL) goto fail;
    f->windowBuffer = windowBuffer;
    if (oh.fromWindowBuffer(windowBuffer, &f->buffer) != 0 || f->buffer == NULL) goto fail;
    oh.config(f->buffer, &f->config);
    if (oh_format_is_yuv(f->config.format) && oh.mapPlanes != NULL
            && oh.mapPlanes(f->buffer, &addr, &f->planes) == 0 && f->planes.planeCount >= 3) {
        f->hasPlanes = true;
    } else if (oh.map(f->buffer, &addr) != 0) {
        addr = NULL;
    }
    if (addr == NULL) goto fail;
    f->addr = addr;
    if ((r->format == A_YUV_420_888 || r->format == A_NV21) != oh_format_is_yuv(f->config.format)
            && r->format != A_PRIVATE) {
        char message[160];
        snprintf(message, sizeof(message),
                 "The producer output buffer format %#x doesn't match the ImageReader's configured buffer format %#x",
                 f->config.format, r->format);
        oh.unmap(f->buffer);
        oh.release(r->image, windowBuffer, -1);
        pthread_mutex_lock(&r->mu);
        r->acquired--;
        pthread_mutex_unlock(&r->mu);
        free(f);
        throw_named(env, "java/lang/UnsupportedOperationException", message);
        return ACQUIRE_NO_BUFS;
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    (*env)->SetLongField(env, image, g_nativeBuffer, (jlong) (intptr_t) f);
    (*env)->SetLongField(env, image, g_timestamp, (jlong) now.tv_sec * 1000000000LL + now.tv_nsec);
    (*env)->SetIntField(env, image, g_transform, 0);
    (*env)->SetIntField(env, image, g_scalingMode, 0);
    return ACQUIRE_SUCCESS;
fail:
    ilog("[WL-IMAGEREADER] could not map an acquired buffer");
    oh.release(r->image, windowBuffer, -1);
    pthread_mutex_lock(&r->mu);
    r->acquired--;
    pthread_mutex_unlock(&r->mu);
    free(f);
    return ACQUIRE_NO_BUFS;
}

static void ir_release_image(JNIEnv* env, jobject thiz, jobject image)
{
    Reader* r = reader_of(env, thiz);
    Frame* f = frame_of(env, image);
    if (f == NULL) return;
    (*env)->SetLongField(env, image, g_nativeBuffer, 0);
    if (f->buffer != NULL) oh.unmap(f->buffer);
    if (r != NULL) {
        oh.release(r->image, f->windowBuffer, -1);
        pthread_mutex_lock(&r->mu);
        if (r->acquired > 0) r->acquired--;
        pthread_mutex_unlock(&r->mu);
    }
    free(f);
}

static jint ir_detach_image(JNIEnv* env, jobject thiz, jobject image, jboolean throwIseOnly)
{
    (void) thiz;
    (void) image;
    (void) throwIseOnly;
    throw_named(env, "java/lang/IllegalStateException", "Image detach is not supported on this device");
    return -1;
}

static void ir_discard_free_buffers(JNIEnv* env, jobject thiz)
{
    (void) env;
    (void) thiz;
}

static jobjectArray ir_create_image_planes(JNIEnv* env, jclass clazz, jint numPlanes, jobject buffer,
                                           jint fenceFd, jint format, jint l, jint t, jint r, jint b)
{
    (void) clazz; (void) numPlanes; (void) buffer; (void) fenceFd; (void) format;
    (void) l; (void) t; (void) r; (void) b;
    throw_named(env, "java/lang/UnsupportedOperationException", "GraphicBuffer planes are not supported");
    return NULL;
}

static void ir_unlock_graphic_buffer(JNIEnv* env, jclass clazz, jobject buffer)
{
    (void) env; (void) clazz; (void) buffer;
}

static jobject new_plane(JNIEnv* env, jobject image, uint8_t* data, jlong size, int rowStride, int pixelStride)
{
    jobject bytes = (*env)->NewDirectByteBuffer(env, data, size);
    if (bytes == NULL) return NULL;
    return (*env)->NewObject(env, g_planeClass, g_planeInit, image, rowStride, pixelStride, bytes);
}

static jobjectArray si_create_planes(JNIEnv* env, jobject image, jint numPlanes, jint halFormat, jlong usage)
{
    Frame* f = frame_of(env, image);
    jobjectArray out;
    int w, h, stride;
    (void) halFormat;
    (void) usage;
    if (f == NULL) {
        throw_named(env, "java/lang/IllegalStateException", "Image is not valid");
        return NULL;
    }
    out = (*env)->NewObjectArray(env, numPlanes, g_planeClass, NULL);
    if (out == NULL || numPlanes <= 0) return out;
    w = f->config.width;
    h = f->config.height;
    stride = f->config.stride > 0 ? f->config.stride : w;
    if (oh_format_is_yuv(f->config.format)) {
        /* Y, U, V, as YUV_420_888 names them: OH's planes come in that order whatever the layout. */
        uint64_t offsets[3];
        int rowStrides[3], pixelStrides[3];
        if (f->hasPlanes) {
            for (int i = 0; i < 3; i++) {
                offsets[i] = f->planes.planes[i].offset;
                rowStrides[i] = (int) f->planes.planes[i].rowStride;
                pixelStrides[i] = (int) f->planes.planes[i].columnStride;
            }
        } else {
            /* Semi-planar without a plane map: Y, then interleaved chroma after the luma rows. */
            uint64_t chroma = (uint64_t) stride * (uint64_t) h;
            bool crFirst = f->config.format == OH_FMT_YCRCB_420_SP;
            offsets[0] = 0;
            offsets[1] = chroma + (crFirst ? 1 : 0);
            offsets[2] = chroma + (crFirst ? 0 : 1);
            rowStrides[0] = rowStrides[1] = rowStrides[2] = stride;
            pixelStrides[0] = 1;
            pixelStrides[1] = pixelStrides[2] = 2;
        }
        for (int i = 0; i < numPlanes && i < 3; i++) {
            int pw = i == 0 ? w : (w + 1) / 2, ph = i == 0 ? h : (h + 1) / 2;
            jlong size = (jlong) rowStrides[i] * (ph - 1) + (jlong) pixelStrides[i] * (pw - 1) + 1;
            jobject plane = new_plane(env, image, f->addr + offsets[i], size, rowStrides[i], pixelStrides[i]);
            if (plane == NULL) return NULL;
            (*env)->SetObjectArrayElement(env, out, i, plane);
            (*env)->DeleteLocalRef(env, plane);
        }
    } else {
        int bpp = bytes_per_pixel(f->config.format);
        jobject plane = new_plane(env, image, f->addr, (jlong) stride * h, stride, bpp);
        if (plane == NULL) return NULL;
        (*env)->SetObjectArrayElement(env, out, 0, plane);
        (*env)->DeleteLocalRef(env, plane);
    }
    return out;
}

static jint si_width(JNIEnv* env, jobject image)
{
    Frame* f = frame_of(env, image);
    return f != NULL ? f->config.width : 0;
}

static jint si_height(JNIEnv* env, jobject image)
{
    Frame* f = frame_of(env, image);
    return f != NULL ? f->config.height : 0;
}

static jint si_format(JNIEnv* env, jobject image, jint readerFormat)
{
    (void) env;
    (void) image;
    return readerFormat;
}

static jint si_fence(JNIEnv* env, jobject image)
{
    (void) env;
    (void) image;
    return -1;
}

static jobject si_hardware_buffer(JNIEnv* env, jobject image)
{
    (void) env;
    (void) image;
    return NULL;
}

static const JNINativeMethod kReader[] = {
    { "nativeClassInit", "()V", (void*) ir_class_init },
    { "nativeInit", "(Ljava/lang/Object;IIIJII)V", (void*) ir_init },
    { "nativeClose", "()V", (void*) ir_close },
    { "nativeReleaseImage", "(Landroid/media/Image;)V", (void*) ir_release_image },
    { "nativeGetSurface", "()Landroid/view/Surface;", (void*) ir_get_surface },
    { "nativeDetachImage", "(Landroid/media/Image;Z)I", (void*) ir_detach_image },
    { "nativeDiscardFreeBuffers", "()V", (void*) ir_discard_free_buffers },
    { "nativeImageSetup", "(Landroid/media/Image;)I", (void*) ir_image_setup },
    { "nativeCreateImagePlanes", "(ILandroid/graphics/GraphicBuffer;IIIIII)[Landroid/media/ImageReader$ImagePlane;",
      (void*) ir_create_image_planes },
    { "nativeUnlockGraphicBuffer", "(Landroid/graphics/GraphicBuffer;)V", (void*) ir_unlock_graphic_buffer },
};

static const JNINativeMethod kSurfaceImage[] = {
    { "nativeCreatePlanes", "(IIJ)[Landroid/media/ImageReader$SurfaceImage$SurfacePlane;",
      (void*) si_create_planes },
    { "nativeGetWidth", "()I", (void*) si_width },
    { "nativeGetHeight", "()I", (void*) si_height },
    { "nativeGetFormat", "(I)I", (void*) si_format },
    { "nativeGetFenceFd", "()I", (void*) si_fence },
    { "nativeGetHardwareBuffer", "()Landroid/hardware/HardwareBuffer;", (void*) si_hardware_buffer },
};

/*
 * PublicFormatUtils: the public ImageFormat <-> HAL format and dataspace map of AOSP's
 * ui/PublicFormat.cpp, which ImageReader's constructor calls before nativeInit. Formats that are
 * not a blob, depth or raw-depth pass through unchanged, as they do there.
 */
enum {
    PF_RAW_SENSOR = 0x20, PF_JPEG = 0x100, PF_DEPTH_POINT_CLOUD = 0x101, PF_RAW_DEPTH = 0x1002,
    PF_RAW_DEPTH10 = 0x1003, PF_JPEG_R = 0x1005, PF_YUV_420_888 = 0x23, PF_NV21 = 0x11,
    PF_YV12 = 0x32315659, PF_DEPTH16 = 0x44363159, PF_HEIC = 0x48454946, PF_DEPTH_JPEG = 0x69656963,
    HAL_RAW16 = 0x20, HAL_BLOB = 0x21, HAL_RAW10 = 0x25, HAL_Y16 = 0x20363159,
    DS_UNKNOWN = 0, DS_ARBITRARY = 1, DS_JFIF = 0x101, DS_DEPTH = 0x1000, DS_DYNAMIC_DEPTH = 0x1002,
    DS_HEIF = 0x1003, DS_JPEG_R = 0x1005, DS_V0_JFIF = 0x8C20000,
};

static jint pf_hal_format(JNIEnv* env, jclass clazz, jint format)
{
    (void) env; (void) clazz;
    switch (format) {
    case PF_JPEG: case PF_DEPTH_POINT_CLOUD: case PF_DEPTH_JPEG: case PF_HEIC: case PF_JPEG_R:
        return HAL_BLOB;
    case PF_DEPTH16: return HAL_Y16;
    case PF_RAW_DEPTH: return HAL_RAW16;
    case PF_RAW_DEPTH10: return HAL_RAW10;
    default: return format;
    }
}

static jint pf_hal_dataspace(JNIEnv* env, jclass clazz, jint format)
{
    (void) env; (void) clazz;
    switch (format) {
    case PF_JPEG: case PF_YUV_420_888: case PF_NV21: case PF_YV12: return DS_V0_JFIF;
    case PF_DEPTH_POINT_CLOUD: case PF_DEPTH16: case PF_RAW_DEPTH: case PF_RAW_DEPTH10: return DS_DEPTH;
    case PF_RAW_SENSOR: return DS_ARBITRARY;
    case PF_DEPTH_JPEG: return DS_DYNAMIC_DEPTH;
    case PF_HEIC: return DS_HEIF;
    case PF_JPEG_R: return DS_JPEG_R;
    default: return DS_UNKNOWN;
    }
}

static jint pf_public_format(JNIEnv* env, jclass clazz, jint hal, jint dataspace)
{
    (void) env; (void) clazz;
    switch (hal) {
    case HAL_BLOB:
        switch (dataspace) {
        case DS_DEPTH: return PF_DEPTH_POINT_CLOUD;
        case DS_V0_JFIF: case DS_JFIF: return PF_JPEG;
        case DS_HEIF: return PF_HEIC;
        case DS_DYNAMIC_DEPTH: return PF_DEPTH_JPEG;
        case DS_JPEG_R: return PF_JPEG_R;
        default: return 0;
        }
    case HAL_Y16: return dataspace == DS_DEPTH ? PF_DEPTH16 : hal;
    case HAL_RAW16: return dataspace == DS_DEPTH ? PF_RAW_DEPTH : hal;
    case HAL_RAW10: return dataspace == DS_DEPTH ? PF_RAW_DEPTH10 : hal;
    default: return hal;
    }
}

static const JNINativeMethod kPublicFormatUtils[] = {
    { "nativeGetHalFormat", "(I)I", (void*) pf_hal_format },
    { "nativeGetHalDataspace", "(I)I", (void*) pf_hal_dataspace },
    { "nativeGetPublicFormat", "(II)I", (void*) pf_public_format },
};

/* Registers both classes and PublicFormatUtils; the class initializer's nativeClassInit then runs on
 * first use. */
int wl_register_image_reader(JNIEnv* env)
{
    jclass reader, image, formats;
    int n = 0;
    (*env)->GetJavaVM(env, &g_vm);
    reader = (*env)->FindClass(env, "android/media/ImageReader");
    if (reader == NULL || (*env)->RegisterNatives(env, reader, kReader, COUNT(kReader)) != 0) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        return 0;
    }
    n += COUNT(kReader);
    image = (*env)->FindClass(env, "android/media/ImageReader$SurfaceImage");
    if (image != NULL && (*env)->RegisterNatives(env, image, kSurfaceImage, COUNT(kSurfaceImage)) == 0) {
        n += COUNT(kSurfaceImage);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    formats = (*env)->FindClass(env, "android/media/PublicFormatUtils");
    if (formats != NULL
            && (*env)->RegisterNatives(env, formats, kPublicFormatUtils, COUNT(kPublicFormatUtils)) == 0) {
        n += COUNT(kPublicFormatUtils);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    return n;
}
