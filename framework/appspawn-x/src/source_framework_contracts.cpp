// Exercise public Java APIs after source framework registration. These checks
// validate actual JNI calls and ownership; a successful RegisterNatives alone
// cannot establish either property.
#include <jni.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <dlfcn.h>
#include <cerrno>
#include <sys/socket.h>
#include <pthread.h>
#include <unistd.h>
#include <android/bitmap.h>
#include <android/imagedecoder.h>
#include <android/looper.h>

extern "C" int* __errno();

struct ErrnoContract {
    int* mainSlot;
    bool passed;
};

static void* checkThreadErrno(void* argument) {
    auto* state = static_cast<ErrnoContract*>(argument);
    int* slot = __errno();
    errno = 0;
    int result = ::close(-1);
    state->passed = slot == &errno && slot != state->mainSlot
            && result == -1 && *slot == EBADF;
    return nullptr;
}

static int checkLooperCallback(int fd, int events, void* data) {
    char value = 0;
    if ((events & ALOOPER_EVENT_INPUT) && ::read(fd, &value, 1) == 1 && value == 'Q') {
        ++*static_cast<int*>(data);
    }
    return 1;
}

#define REQUIRE(expr) do { if (!(expr) || env->ExceptionCheck()) { \
    fprintf(stderr, "SOURCE_FRAMEWORK_CONTRACT_FAIL line=%d: %s\n", __LINE__, #expr); \
    if (env->ExceptionCheck()) env->ExceptionDescribe(); \
    env->PopLocalFrame(nullptr); return -1; } } while (0)

int runSourceFrameworkContracts(JNIEnv* env) {
    if (env->PushLocalFrame(64)) return -1;
    ErrnoContract errnoState{__errno(), false};
    REQUIRE(errnoState.mainSlot == &errno);
    auto versionedLookup = reinterpret_cast<void* (*)(void*, const char*, const char*)>(
            dlsym(RTLD_DEFAULT, "dlvsym")); REQUIRE(versionedLookup);
    REQUIRE(versionedLookup(RTLD_DEFAULT, "__errno", "LIBC") ==
            reinterpret_cast<void*>(&__errno));
    errno = E2BIG;
    pthread_t errnoThread;
    REQUIRE(pthread_create(&errnoThread, nullptr, checkThreadErrno, &errnoState) == 0);
    REQUIRE(pthread_join(errnoThread, nullptr) == 0);
    REQUIRE(errnoState.passed && *__errno() == E2BIG);
    fprintf(stderr, "SOURCE_BIONIC_ERRNO_PASS host_slot=1 thread_isolation=1 syscall_error=EBADF\n");
    auto* legacyStreams = static_cast<char*>(versionedLookup(RTLD_DEFAULT, "__sF", "LIBC"));
    REQUIRE(legacyStreams);
    auto abiWrite = reinterpret_cast<size_t (*)(const void*, size_t, size_t, FILE*)>(
            versionedLookup(RTLD_DEFAULT, "fwrite", "LIBC")); REQUIRE(abiWrite);
    auto abiFlush = reinterpret_cast<int (*)(FILE*)>(
            versionedLookup(RTLD_DEFAULT, "fflush", "LIBC")); REQUIRE(abiFlush);
    int streamPipe[2]; REQUIRE(::pipe(streamPipe) == 0);
    int savedStderr = ::dup(STDERR_FILENO); REQUIRE(savedStderr >= 0);
    REQUIRE(::fflush(stderr) == 0);
    REQUIRE(::dup2(streamPipe[1], STDERR_FILENO) == STDERR_FILENO);
    ::close(streamPipe[1]);
    // Bionic's LP64 legacy FILE ABI uses 152-byte objects. Compare its stderr
    // sentinel and the host's real FILE through the same versioned entry point.
    FILE* legacyStderr = reinterpret_cast<FILE*>(legacyStreams + 2 * 152);
    bool streamsOkay = abiWrite("ANDROID", 1, 7, legacyStderr) == 7
            && abiFlush(legacyStderr) == 0
            && abiWrite("HOST", 1, 4, stderr) == 4 && abiFlush(stderr) == 0;
    int restoredStderr = ::dup2(savedStderr, STDERR_FILENO);
    ::close(savedStderr);
    REQUIRE(restoredStderr == STDERR_FILENO && streamsOkay);
    char streamText[12]{};
    REQUIRE(::read(streamPipe[0], streamText, sizeof(streamText)) == 11);
    REQUIRE(::close(streamPipe[0]) == 0 && std::strcmp(streamText, "ANDROIDHOST") == 0);
    fprintf(stderr, "SOURCE_BIONIC_STDIO_PASS legacy_stream=1 host_stream=1 exact_bytes=11\n");
    // Prepare NDK first, then Java. Java's native poll must dispatch the NDK
    // callback, and destroying its wrapper must preserve the acquired looper.
    ALooper* nativeLooper = ALooper_prepare(0); REQUIRE(nativeLooper);
    ALooper_acquire(nativeLooper);
    jclass queueClass = env->FindClass("android/os/MessageQueue"); REQUIRE(queueClass);
    auto queueCtor = env->GetMethodID(queueClass, "<init>", "(Z)V"); REQUIRE(queueCtor);
    jobject queue = env->NewObject(queueClass, queueCtor, JNI_TRUE); REQUIRE(queue);
    auto queuePointer = env->GetFieldID(queueClass, "mPtr", "J"); REQUIRE(queuePointer);
    auto queuePoll = env->GetMethodID(queueClass, "nativePollOnce", "(JI)V"); REQUIRE(queuePoll);
    auto queueDispose = env->GetMethodID(queueClass, "dispose", "()V"); REQUIRE(queueDispose);
    REQUIRE(ALooper_forThread() == nativeLooper);
    int pipeFds[2]; REQUIRE(::pipe(pipeFds) == 0);
    int callbacks = 0;
    REQUIRE(ALooper_addFd(nativeLooper, pipeFds[0], ALOOPER_POLL_CALLBACK,
                         ALOOPER_EVENT_INPUT, checkLooperCallback, &callbacks) == 1);
    REQUIRE(::write(pipeFds[1], "Q", 1) == 1);
    env->CallVoidMethod(queue, queuePoll, env->GetLongField(queue, queuePointer), 0);
    REQUIRE(!env->ExceptionCheck() && callbacks == 1);
    env->CallVoidMethod(queue, queueDispose); REQUIRE(!env->ExceptionCheck());
    REQUIRE(::write(pipeFds[1], "Q", 1) == 1);
    REQUIRE(ALooper_pollOnce(0, nullptr, nullptr, nullptr) == ALOOPER_POLL_CALLBACK);
    REQUIRE(callbacks == 2);
    REQUIRE(ALooper_removeFd(nativeLooper, pipeFds[0]) == 1);
    REQUIRE(::close(pipeFds[0]) == 0 && ::close(pipeFds[1]) == 0);
    ALooper_release(nativeLooper);
    fprintf(stderr, "SOURCE_SHARED_LOOPER_PASS java_dispatch=1 native_dispatch=1 ownership_preserved=1\n");
    jclass surfaceControl = env->FindClass("android/view/SurfaceControl"); REQUIRE(surfaceControl);
    auto overlayQuery = env->GetStaticMethodID(surfaceControl, "getOverlaySupport",
            "()Landroid/hardware/OverlayProperties;"); REQUIRE(overlayQuery);
    jobject overlay = env->CallStaticObjectMethod(surfaceControl, overlayQuery); REQUIRE(overlay);
    jclass overlayClass = env->GetObjectClass(overlay); REQUIRE(overlayClass);
    auto combination = env->GetMethodID(overlayClass, "isCombinationSupported", "(II)Z"); REQUIRE(combination);
    auto mixed = env->GetMethodID(overlayClass, "isMixedColorSpacesSupported", "()Z"); REQUIRE(mixed);
    jclass dataspace = env->FindClass("android/hardware/DataSpace"); REQUIRE(dataspace);
    auto srgbField = env->GetStaticFieldID(dataspace, "DATASPACE_SRGB", "I"); REQUIRE(srgbField);
    jint srgb = env->GetStaticIntField(dataspace, srgbField);
    REQUIRE(!env->CallBooleanMethod(overlay, combination, srgb, 1));
    REQUIRE(!env->CallBooleanMethod(overlay, mixed));
    auto defaultOverlay = env->GetStaticMethodID(overlayClass, "getDefault",
            "()Landroid/hardware/OverlayProperties;"); REQUIRE(defaultOverlay);
    jobject virtualOverlay = env->CallStaticObjectMethod(overlayClass, defaultOverlay); REQUIRE(virtualOverlay);
    REQUIRE(env->CallBooleanMethod(virtualOverlay, combination, srgb, 1));
    REQUIRE(env->CallBooleanMethod(virtualOverlay, mixed));
    jclass displayAdapter = env->FindClass("adapter/window/DisplayManagerAdapter");
    REQUIRE(displayAdapter);
    auto displayAdapterInstance = env->GetStaticMethodID(displayAdapter, "getInstance",
            "()Ladapter/window/DisplayManagerAdapter;");
    REQUIRE(displayAdapterInstance);
    jobject displayService = env->CallStaticObjectMethod(displayAdapter, displayAdapterInstance);
    REQUIRE(displayService);
    auto displayOverlayQuery = env->GetMethodID(displayAdapter, "getOverlaySupport",
            "()Landroid/hardware/OverlayProperties;");
    REQUIRE(displayOverlayQuery);
    jobject displayOverlay = env->CallObjectMethod(displayService, displayOverlayQuery);
    REQUIRE(displayOverlay && !env->CallBooleanMethod(displayOverlay, mixed));
    jclass transaction = env->FindClass("android/view/SurfaceControl$Transaction"); REQUIRE(transaction);
    auto transactionCtor = env->GetMethodID(transaction, "<init>", "()V"); REQUIRE(transactionCtor);
    auto transactionApply = env->GetMethodID(transaction, "apply", "()V"); REQUIRE(transactionApply);
    auto transactionClose = env->GetMethodID(transaction, "close", "()V"); REQUIRE(transactionClose);
    jobject emptyTransaction = env->NewObject(transaction, transactionCtor); REQUIRE(emptyTransaction);
    env->CallVoidMethod(emptyTransaction, transactionApply); REQUIRE(!env->ExceptionCheck());
    env->CallVoidMethod(emptyTransaction, transactionClose); REQUIRE(!env->ExceptionCheck());
    fprintf(stderr, "SOURCE_SURFACE_CONTROL_PASS overlay_query=1 display_service=1 virtual_default=1 apply_close=1\n");
    // Exercise the Android 15 BLASTBufferQueue API surface that ViewRootImpl
    // uses to turn a relayout SurfaceControl into a renderable Surface. This
    // catches signature drift between the pinned framework JAR and the OH
    // transaction boundary before an application attempts its first frame.
    jclass blast = env->FindClass("android/graphics/BLASTBufferQueue"); REQUIRE(blast);
    auto blastCtor = env->GetMethodID(blast, "<init>", "(Ljava/lang/String;Z)V"); REQUIRE(blastCtor);
    auto blastCreateSurface = env->GetMethodID(blast, "createSurface", "()Landroid/view/Surface;");
    REQUIRE(blastCreateSurface);
    auto blastClear = env->GetMethodID(blast, "clearSyncTransaction", "()V"); REQUIRE(blastClear);
    auto blastStop = env->GetMethodID(blast, "stopContinuousSyncTransaction", "()V"); REQUIRE(blastStop);
    auto blastLastFrame = env->GetMethodID(blast, "getLastAcquiredFrameNum", "()J"); REQUIRE(blastLastFrame);
    auto blastGather = env->GetMethodID(blast, "gatherPendingTransactions",
            "(J)Landroid/view/SurfaceControl$Transaction;"); REQUIRE(blastGather);
    auto blastSetHang = env->GetMethodID(blast, "setTransactionHangCallback",
            "(Landroid/graphics/BLASTBufferQueue$TransactionHangCallback;)V"); REQUIRE(blastSetHang);
    auto blastDestroy = env->GetMethodID(blast, "destroy", "()V"); REQUIRE(blastDestroy);
    jstring blastName = env->NewStringUTF("source-contract"); REQUIRE(blastName);
    jobject blastQueue = env->NewObject(blast, blastCtor, blastName, JNI_FALSE); REQUIRE(blastQueue);
    jobject blastSurface = env->CallObjectMethod(blastQueue, blastCreateSurface); REQUIRE(blastSurface);
    env->CallVoidMethod(blastQueue, blastClear); REQUIRE(!env->ExceptionCheck());
    env->CallVoidMethod(blastQueue, blastStop); REQUIRE(!env->ExceptionCheck());
    REQUIRE(env->CallLongMethod(blastQueue, blastLastFrame) == 0);
    jobject gathered = env->CallObjectMethod(blastQueue, blastGather, jlong(7)); REQUIRE(gathered);
    env->CallVoidMethod(gathered, transactionApply); REQUIRE(!env->ExceptionCheck());
    env->CallVoidMethod(gathered, transactionClose); REQUIRE(!env->ExceptionCheck());
    env->CallVoidMethod(blastQueue, blastSetHang, nullptr); REQUIRE(!env->ExceptionCheck());
    env->CallVoidMethod(blastQueue, blastDestroy); REQUIRE(!env->ExceptionCheck());
    fprintf(stderr, "SOURCE_BLAST_BUFFER_QUEUE_PASS create=1 surface=1 pending_transaction=1 destroy=1\n");
    jclass incremental = env->FindClass("android/os/incremental/IncrementalManager");
    REQUIRE(incremental);
    auto incrementalEnabled = env->GetStaticMethodID(incremental, "nativeIsEnabled", "()Z");
    auto incrementalV2 = env->GetStaticMethodID(incremental, "nativeIsV2Available", "()Z");
    auto incrementalPath = env->GetStaticMethodID(
            incremental, "nativeIsIncrementalPath", "(Ljava/lang/String;)Z");
    auto incrementalFd = env->GetStaticMethodID(incremental, "nativeIsIncrementalFd", "(I)Z");
    auto incrementalSignature = env->GetStaticMethodID(
            incremental, "nativeUnsafeGetFileSignature", "(Ljava/lang/String;)[B");
    REQUIRE(incrementalEnabled && incrementalV2 && incrementalPath && incrementalFd
            && incrementalSignature);
    jstring ordinaryPath = env->NewStringUTF("/data/local/tmp/a2hlab-ordinary.apk");
    REQUIRE(ordinaryPath);
    REQUIRE(!env->CallStaticBooleanMethod(incremental, incrementalEnabled));
    REQUIRE(!env->CallStaticBooleanMethod(incremental, incrementalV2));
    REQUIRE(!env->CallStaticBooleanMethod(incremental, incrementalPath, ordinaryPath));
    REQUIRE(!env->CallStaticBooleanMethod(incremental, incrementalFd, -1));
    REQUIRE(env->CallStaticObjectMethod(incremental, incrementalSignature, ordinaryPath) == nullptr);
    REQUIRE(!env->ExceptionCheck());
    fprintf(stderr, "SOURCE_INCREMENTAL_MANAGER_PASS enabled=0 v2=0 ordinary_path=0 signature=null\n");
    // Exercise the original connectivity module JNI and the OH accounting
    // boundary. An unsupported tag must neither invent success nor close the
    // socket, and an invalid descriptor must retain Android's EBADF result.
    jclass traffic = env->FindClass("android/net/TrafficStats"); REQUIRE(traffic);
    auto tag = env->GetStaticMethodID(traffic, "native_tagSocketFd", "(Ljava/io/FileDescriptor;II)I"); REQUIRE(tag);
    auto untag = env->GetStaticMethodID(traffic, "native_untagSocketFd", "(Ljava/io/FileDescriptor;)I"); REQUIRE(untag);
    jclass descriptor = env->FindClass("java/io/FileDescriptor"); REQUIRE(descriptor);
    auto descriptorCtor = env->GetMethodID(descriptor, "<init>", "()V"); REQUIRE(descriptorCtor);
    auto descriptorField = env->GetFieldID(descriptor, "descriptor", "I"); REQUIRE(descriptorField);
    jobject fdObject = env->NewObject(descriptor, descriptorCtor); REQUIRE(fdObject);
    REQUIRE(env->CallStaticIntMethod(traffic, tag, fdObject, 42, -1) == -EBADF);
    REQUIRE(env->CallStaticIntMethod(traffic, untag, fdObject) == -EBADF);
    int socketFd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0); REQUIRE(socketFd >= 0);
    env->SetIntField(fdObject, descriptorField, socketFd); REQUIRE(!env->ExceptionCheck());
    REQUIRE(env->CallStaticIntMethod(traffic, tag, fdObject, 42, -1) == -EOPNOTSUPP);
    REQUIRE(env->CallStaticIntMethod(traffic, untag, fdObject) == -EOPNOTSUPP);
    int socketType = 0;
    socklen_t socketTypeLength = sizeof(socketType);
    REQUIRE(getsockopt(socketFd, SOL_SOCKET, SO_TYPE, &socketType, &socketTypeLength) == 0);
    REQUIRE(socketType == SOCK_DGRAM);
    REQUIRE(::close(socketFd) == 0);
    env->SetIntField(fdObject, descriptorField, -1); REQUIRE(!env->ExceptionCheck());
    fprintf(stderr, "SOURCE_TRAFFIC_STATS_JNI_PASS invalid_fd=EBADF accounting=EOPNOTSUPP socket_preserved=1\n");
    jclass parcel = env->FindClass("android/os/Parcel"); REQUIRE(parcel);
    auto obtain = env->GetStaticMethodID(parcel, "obtain", "()Landroid/os/Parcel;"); REQUIRE(obtain);
    auto recycle = env->GetMethodID(parcel, "recycle", "()V"); REQUIRE(recycle);
    auto position = env->GetMethodID(parcel, "setDataPosition", "(I)V"); REQUIRE(position);
    auto writeInt = env->GetMethodID(parcel, "writeInt", "(I)V"); REQUIRE(writeInt);
    auto readInt = env->GetMethodID(parcel, "readInt", "()I"); REQUIRE(readInt);
    auto writeLong = env->GetMethodID(parcel, "writeLong", "(J)V"); REQUIRE(writeLong);
    auto readLong = env->GetMethodID(parcel, "readLong", "()J"); REQUIRE(readLong);
    auto writeString = env->GetMethodID(parcel, "writeString", "(Ljava/lang/String;)V"); REQUIRE(writeString);
    auto readString = env->GetMethodID(parcel, "readString", "()Ljava/lang/String;"); REQUIRE(readString);
    jobject p = env->CallStaticObjectMethod(parcel, obtain); REQUIRE(p);
    jstring original = env->NewStringUTF("source-你好"); REQUIRE(original);
    env->CallVoidMethod(p, writeInt, 0x12345678); REQUIRE(!env->ExceptionCheck());
    env->CallVoidMethod(p, writeLong, jlong(0x1234567812345678)); REQUIRE(!env->ExceptionCheck());
    env->CallVoidMethod(p, writeString, original); REQUIRE(!env->ExceptionCheck());
    env->CallVoidMethod(p, position, 0); REQUIRE(!env->ExceptionCheck());
    REQUIRE(env->CallIntMethod(p, readInt) == 0x12345678);
    REQUIRE(env->CallLongMethod(p, readLong) == jlong(0x1234567812345678));
    jstring restored = static_cast<jstring>(env->CallObjectMethod(p, readString)); REQUIRE(restored);
    jclass stringClass = env->FindClass("java/lang/String"); REQUIRE(stringClass);
    auto equals = env->GetMethodID(stringClass, "equals", "(Ljava/lang/Object;)Z"); REQUIRE(equals);
    REQUIRE(env->CallBooleanMethod(original, equals, restored));
    env->CallVoidMethod(p, recycle); REQUIRE(!env->ExceptionCheck());
    fprintf(stderr, "SOURCE_PARCEL_JAVA_ROUNDTRIP_PASS\n");

    jclass hb = env->FindClass("android/hardware/HardwareBuffer"); REQUIRE(hb);
    auto create = env->GetStaticMethodID(hb, "create", "(IIIIJ)Landroid/hardware/HardwareBuffer;"); REQUIRE(create);
    auto width = env->GetMethodID(hb, "getWidth", "()I"); REQUIRE(width);
    auto height = env->GetMethodID(hb, "getHeight", "()I"); REQUIRE(height);
    auto format = env->GetMethodID(hb, "getFormat", "()I"); REQUIRE(format);
    auto id = env->GetMethodID(hb, "getId", "()J"); REQUIRE(id);
    auto close = env->GetMethodID(hb, "close", "()V"); REQUIRE(close);
    auto write = env->GetMethodID(hb, "writeToParcel", "(Landroid/os/Parcel;I)V"); REQUIRE(write);
    jobject allocation = env->CallStaticObjectMethod(hb, create, 7, 5, 1, 1, jlong(0x33)); REQUIRE(allocation);
    REQUIRE(env->CallIntMethod(allocation, width) == 7);
    REQUIRE(env->CallIntMethod(allocation, height) == 5);
    REQUIRE(env->CallIntMethod(allocation, format) == 1);
    jlong allocationId = env->CallLongMethod(allocation, id); REQUIRE(allocationId != 0);
    p = env->CallStaticObjectMethod(parcel, obtain); REQUIRE(p);
    env->CallVoidMethod(allocation, write, p, 0); REQUIRE(!env->ExceptionCheck());
    env->CallVoidMethod(p, position, 0); REQUIRE(!env->ExceptionCheck());
    auto creatorField = env->GetStaticFieldID(hb, "CREATOR", "Landroid/os/Parcelable$Creator;"); REQUIRE(creatorField);
    jobject creator = env->GetStaticObjectField(hb, creatorField); REQUIRE(creator);
    jclass creatorClass = env->GetObjectClass(creator); REQUIRE(creatorClass);
    auto fromParcel = env->GetMethodID(creatorClass, "createFromParcel", "(Landroid/os/Parcel;)Ljava/lang/Object;"); REQUIRE(fromParcel);
    jobject copy = env->CallObjectMethod(creator, fromParcel, p); REQUIRE(copy);
    env->CallVoidMethod(p, recycle); REQUIRE(!env->ExceptionCheck());
    env->CallVoidMethod(allocation, close); REQUIRE(!env->ExceptionCheck());
    REQUIRE(env->CallIntMethod(copy, width) == 7);
    REQUIRE(env->CallLongMethod(copy, id) == allocationId);
    env->CallVoidMethod(copy, close); REQUIRE(!env->ExceptionCheck());
    fprintf(stderr, "SOURCE_HARDWARE_BUFFER_JAVA_PARCEL_PASS\n");

    jclass bitmap = env->FindClass("android/graphics/Bitmap"); REQUIRE(bitmap);
    jclass config = env->FindClass("android/graphics/Bitmap$Config"); REQUIRE(config);
    auto argb = env->GetStaticFieldID(config, "ARGB_8888", "Landroid/graphics/Bitmap$Config;"); REQUIRE(argb);
    jobject argbConfig = env->GetStaticObjectField(config, argb); REQUIRE(argbConfig);
    auto makeBitmap = env->GetStaticMethodID(bitmap, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;"); REQUIRE(makeBitmap);
    jobject pixels = env->CallStaticObjectMethod(bitmap, makeBitmap, 2, 2, argbConfig); REQUIRE(pixels);
    jclass canvas = env->FindClass("android/graphics/Canvas"); REQUIRE(canvas);
    auto constructor = env->GetMethodID(canvas, "<init>", "(Landroid/graphics/Bitmap;)V"); REQUIRE(constructor);
    jobject drawing = env->NewObject(canvas, constructor, pixels); REQUIRE(drawing);
    auto color = env->GetMethodID(canvas, "drawColor", "(I)V"); REQUIRE(color);
    env->CallVoidMethod(drawing, color, jint(0xff123456)); REQUIRE(!env->ExceptionCheck());
    auto pixel = env->GetMethodID(bitmap, "getPixel", "(II)I"); REQUIRE(pixel);
    REQUIRE(env->CallIntMethod(pixels, pixel, 1, 1) == jint(0xff123456));
    fprintf(stderr, "SOURCE_SOFTWARE_CANVAS_PIXEL_PASS\n");
    if (getenv("WESTLAKE_SOURCE_NDK_GRAPHICS_CONTRACTS")) {
        void* library = dlopen("libjnigraphics.so", RTLD_NOW | RTLD_LOCAL);
        if (!library) fprintf(stderr, "SOURCE_NDK_GRAPHICS_LOAD_FAILED %s\n", dlerror());
        REQUIRE(library);
#define NDK(name) auto name##Fn = reinterpret_cast<decltype(&name)>(dlsym(library, #name)); REQUIRE(name##Fn)
        NDK(AndroidBitmap_getInfo);
        NDK(AndroidBitmap_getDataSpace);
        NDK(AndroidBitmap_lockPixels);
        NDK(AndroidBitmap_unlockPixels);
        NDK(AndroidBitmap_compress);
        NDK(AImageDecoder_createFromBuffer);
        NDK(AImageDecoder_decodeImage);
        NDK(AImageDecoder_delete);
#undef NDK
        AndroidBitmapInfo info{};
        REQUIRE(AndroidBitmap_getInfoFn(env, pixels, &info) == ANDROID_BITMAP_RESULT_SUCCESS);
        REQUIRE(info.width == 2 && info.height == 2 && info.stride >= 8 &&
                info.format == ANDROID_BITMAP_FORMAT_RGBA_8888);
        void* address = nullptr;
        REQUIRE(AndroidBitmap_lockPixelsFn(env, pixels, &address) == ANDROID_BITMAP_RESULT_SUCCESS);
        REQUIRE(address);
        auto* rgba = static_cast<unsigned char*>(address);
        REQUIRE(rgba[0] == 0x12 && rgba[1] == 0x34 && rgba[2] == 0x56 && rgba[3] == 0xff);
        rgba[0] = 0xaa; rgba[1] = 0xbb; rgba[2] = 0xcc;
        std::vector<unsigned char> png;
        auto savePng = [](void* context, const void* bytes, size_t length) -> bool {
            auto& out = *static_cast<std::vector<unsigned char>*>(context);
            const auto* start = static_cast<const unsigned char*>(bytes);
            out.insert(out.end(), start, start + length);
            return true;
        };
        const int32_t dataSpace = AndroidBitmap_getDataSpaceFn(env, pixels);
        REQUIRE(dataSpace != 0);
        REQUIRE(AndroidBitmap_compressFn(&info, dataSpace, address, ANDROID_BITMAP_COMPRESS_FORMAT_PNG,
                                         100, &png, savePng) == ANDROID_BITMAP_RESULT_SUCCESS);
        REQUIRE(AndroidBitmap_unlockPixelsFn(env, pixels) == ANDROID_BITMAP_RESULT_SUCCESS);
        REQUIRE(env->CallIntMethod(pixels, pixel, 0, 0) == jint(0xffaabbcc));
        AImageDecoder* decoder = nullptr;
        REQUIRE(AImageDecoder_createFromBufferFn(png.data(), png.size(), &decoder) ==
                ANDROID_IMAGE_DECODER_SUCCESS);
        REQUIRE(decoder);
        unsigned char decoded[16]{};
        int result = AImageDecoder_decodeImageFn(decoder, decoded, 8, sizeof(decoded));
        AImageDecoder_deleteFn(decoder);
        REQUIRE(result == ANDROID_IMAGE_DECODER_SUCCESS);
        const unsigned char expected[] = {0xaa, 0xbb, 0xcc, 0xff, 0x12, 0x34, 0x56, 0xff,
                                         0x12, 0x34, 0x56, 0xff, 0x12, 0x34, 0x56, 0xff};
        REQUIRE(std::memcmp(decoded, expected, sizeof(expected)) == 0);
        // Exercise the separately built JPEG and full WebP providers as well.
        // Lossless WebP must preserve exact pixels; JPEG uses uniform RGB to
        // test quantization without conflating it with chroma subsampling.
        info.stride = 8;
        for (int encoding : {ANDROID_BITMAP_COMPRESS_FORMAT_WEBP_LOSSLESS,
                             ANDROID_BITMAP_COMPRESS_FORMAT_JPEG}) {
            unsigned char source[16];
            std::memcpy(source, expected, sizeof(source));
            if (encoding == ANDROID_BITMAP_COMPRESS_FORMAT_JPEG) {
                for (int i = 0; i < 4; ++i) {
                    source[4*i] = 0x33; source[4*i+1] = 0x77;
                    source[4*i+2] = 0xbb; source[4*i+3] = 0xff;
                }
            }
            std::vector<unsigned char> encoded;
            REQUIRE(AndroidBitmap_compressFn(&info, dataSpace, source, encoding,
                    100, &encoded, savePng) == ANDROID_BITMAP_RESULT_SUCCESS);
            decoder = nullptr;
            REQUIRE(AImageDecoder_createFromBufferFn(encoded.data(), encoded.size(), &decoder) ==
                    ANDROID_IMAGE_DECODER_SUCCESS);
            REQUIRE(decoder);
            result = AImageDecoder_decodeImageFn(decoder, decoded, 8, sizeof(decoded));
            AImageDecoder_deleteFn(decoder);
            REQUIRE(result == ANDROID_IMAGE_DECODER_SUCCESS);
            for (int i = 0; i < 16; ++i) {
                int tolerance = encoding == ANDROID_BITMAP_COMPRESS_FORMAT_JPEG && i % 4 != 3 ? 3 : 0;
                REQUIRE(std::abs(int(decoded[i]) - int(source[i])) <= tolerance);
            }
            fprintf(stderr, "SOURCE_NDK_CODEC_PASS format=%d bytes=%zu\n", encoding, encoded.size());
        }
        // One opaque black GIF pixel. LZW codes are clear(4), black(0), end(5).
        const unsigned char gif[] = {
            'G','I','F','8','9','a', 1,0,1,0,0x80,0,0,
            0,0,0, 255,255,255, 0x2c,0,0,0,0,1,0,1,0,0,2,2,0x44,1,0,0x3b};
        decoder = nullptr;
        REQUIRE(AImageDecoder_createFromBufferFn(gif, sizeof(gif), &decoder) ==
                ANDROID_IMAGE_DECODER_SUCCESS);
        REQUIRE(decoder);
        unsigned char gifPixel[4]{};
        result = AImageDecoder_decodeImageFn(decoder, gifPixel, 4, sizeof(gifPixel));
        AImageDecoder_deleteFn(decoder);
        REQUIRE(result == ANDROID_IMAGE_DECODER_SUCCESS);
        REQUIRE(gifPixel[0] == 0 && gifPixel[1] == 0 && gifPixel[2] == 0 && gifPixel[3] == 255);
        fprintf(stderr, "SOURCE_NDK_GIF_PASS\n");
        dlclose(library);
        fprintf(stderr, "SOURCE_NDK_GRAPHICS_PASS bitmap_pixels=4 png_bytes=%zu codecs=4\n", png.size());
    }
    if (getenv("WESTLAKE_SOURCE_FONT_CONTRACTS")) {
        jclass typeface = env->FindClass("android/graphics/Typeface"); REQUIRE(typeface);
        jclass fonts = env->FindClass("android/graphics/fonts/SystemFonts"); REQUIRE(fonts);
        auto available = env->GetStaticMethodID(fonts, "getAvailableFonts", "()Ljava/util/Set;"); REQUIRE(available);
        jobject fontSet = env->CallStaticObjectMethod(fonts, available); REQUIRE(fontSet);
        jclass set = env->FindClass("java/util/Set"); REQUIRE(set);
        auto toArray = env->GetMethodID(set, "toArray", "()[Ljava/lang/Object;"); REQUIRE(toArray);
        auto array = static_cast<jobjectArray>(env->CallObjectMethod(fontSet, toArray)); REQUIRE(array);
        jclass font = env->FindClass("android/graphics/fonts/Font"); REQUIRE(font);
        auto file = env->GetMethodID(font, "getFile", "()Ljava/io/File;"); REQUIRE(file);
        jclass fileClass = env->FindClass("java/io/File"); REQUIRE(fileClass);
        auto canonical = env->GetMethodID(fileClass, "getCanonicalPath", "()Ljava/lang/String;"); REQUIRE(canonical);
        const char* root = getenv("WESTLAKE_RUNTIME_ROOT"); REQUIRE(root && *root);
        const std::string prefix = std::string(root) + "/fonts/";
        jsize count = env->GetArrayLength(array);
        fprintf(stderr, "SOURCE_FONT_AVAILABLE_COUNT %d\n", count);
        REQUIRE(count >= 100);
        for (jsize i = 0; i < count; ++i) {
            jobject f = env->GetObjectArrayElement(array, i); REQUIRE(f);
            jobject path = env->CallObjectMethod(f, file); REQUIRE(path);
            auto name = static_cast<jstring>(env->CallObjectMethod(path, canonical)); REQUIRE(name);
            const char* text = env->GetStringUTFChars(name, nullptr); REQUIRE(text);
            bool isolated = std::strncmp(text, prefix.c_str(), prefix.size()) == 0;
            if (!isolated) fprintf(stderr, "SOURCE_FONT_OUTSIDE_STAGE %s\n", text);
            env->ReleaseStringUTFChars(name, text);
            env->DeleteLocalRef(name); env->DeleteLocalRef(path); env->DeleteLocalRef(f);
            REQUIRE(isolated);
        }
        jclass paint = env->FindClass("android/graphics/Paint"); REQUIRE(paint);
        auto paintCtor = env->GetMethodID(paint, "<init>", "()V"); REQUIRE(paintCtor);
        jobject pen = env->NewObject(paint, paintCtor); REQUIRE(pen);
        auto hasGlyph = env->GetMethodID(paint, "hasGlyph", "(Ljava/lang/String;)Z"); REQUIRE(hasGlyph);
        jstring chinese = env->NewStringUTF("你"); REQUIRE(chinese);
        REQUIRE(env->CallBooleanMethod(pen, hasGlyph, chinese));
        auto textSize = env->GetMethodID(paint, "setTextSize", "(F)V"); REQUIRE(textSize);
        env->CallVoidMethod(pen, textSize, 32.0f); REQUIRE(!env->ExceptionCheck());
        auto ink = env->GetMethodID(paint, "setColor", "(I)V"); REQUIRE(ink);
        env->CallVoidMethod(pen, ink, jint(0xff000000)); REQUIRE(!env->ExceptionCheck());
        jobject textPixels = env->CallStaticObjectMethod(bitmap, makeBitmap, 64, 48, argbConfig); REQUIRE(textPixels);
        jobject textCanvas = env->NewObject(canvas, constructor, textPixels); REQUIRE(textCanvas);
        env->CallVoidMethod(textCanvas, color, jint(0xffffffff)); REQUIRE(!env->ExceptionCheck());
        auto drawText = env->GetMethodID(canvas, "drawText", "(Ljava/lang/String;FFLandroid/graphics/Paint;)V"); REQUIRE(drawText);
        env->CallVoidMethod(textCanvas, drawText, chinese, 4.0f, 36.0f, pen); REQUIRE(!env->ExceptionCheck());
        int marked = 0;
        for (int y = 0; y < 48; ++y) for (int x = 0; x < 64; ++x) {
            jint value = env->CallIntMethod(textPixels, pixel, x, y); REQUIRE(!env->ExceptionCheck());
            if (value != jint(0xffffffff)) ++marked;
        }
        REQUIRE(marked > 20 && marked < 64 * 48);
        fprintf(stderr, "SOURCE_FONT_ISOLATION_PASS fonts=%d chinese_ink_pixels=%d\n", count, marked);
    }
    fprintf(stderr, "SOURCE_FRAMEWORK_CONTRACTS_PASS\n");
    env->PopLocalFrame(nullptr);
    return 0;
}
