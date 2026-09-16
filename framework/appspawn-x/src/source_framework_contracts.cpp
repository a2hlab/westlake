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
#include <network/netmanager/net_connection.h>

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

    // The device-captured WebView NDK shim looks these private backend symbols
    // up through liboh_adapter_bridge.so.  Verify both transitive symbol
    // ownership and real Java-MessageQueue dispatch, not merely that the
    // implementation exists in an unlinked source file.
    void* adapterBridge = dlopen("liboh_adapter_bridge.so", RTLD_NOW | RTLD_NOLOAD);
    if (adapterBridge == nullptr) {
        adapterBridge = dlopen("liboh_adapter_bridge.so", RTLD_NOW | RTLD_GLOBAL);
    }
    REQUIRE(adapterBridge);
    auto backendPrepare = reinterpret_cast<void* (*)(int)>(
            dlsym(adapterBridge, "wl_ALooper_prepare"));
    auto backendAddFd = reinterpret_cast<int (*)(void*, int, int, int,
            ALooper_callbackFunc, void*)>(dlsym(adapterBridge, "wl_ALooper_addFd"));
    auto backendRemoveFd = reinterpret_cast<int (*)(void*, int)>(
            dlsym(adapterBridge, "wl_ALooper_removeFd"));
    auto backendWake = reinterpret_cast<void (*)(void*)>(
            dlsym(adapterBridge, "wl_ALooper_wake"));
    REQUIRE(backendPrepare && backendAddFd && backendRemoveFd && backendWake);
    void* backendLooper = backendPrepare(0); REQUIRE(backendLooper);
    jobject backendQueue = env->NewObject(queueClass, queueCtor, JNI_TRUE);
    REQUIRE(backendQueue);
    int backendPipe[2]; REQUIRE(::pipe(backendPipe) == 0);
    int backendCallbacks = 0;
    REQUIRE(backendAddFd(backendLooper, backendPipe[0], ALOOPER_POLL_CALLBACK,
                         ALOOPER_EVENT_INPUT, checkLooperCallback,
                         &backendCallbacks) == 1);
    REQUIRE(::write(backendPipe[1], "Q", 1) == 1);
    env->CallVoidMethod(backendQueue, queuePoll,
                        env->GetLongField(backendQueue, queuePointer), 0);
    REQUIRE(!env->ExceptionCheck() && backendCallbacks == 1);
    backendWake(backendLooper);
    REQUIRE(backendRemoveFd(backendLooper, backendPipe[0]) == 1);
    REQUIRE(::close(backendPipe[0]) == 0 && ::close(backendPipe[1]) == 0);
    env->CallVoidMethod(backendQueue, queueDispose); REQUIRE(!env->ExceptionCheck());
    fprintf(stderr, "SOURCE_WEBVIEW_ALOOPER_BACKEND_PASS symbols=4 java_dispatch=1\n");
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

    // Android's ConnectivityService resolves per-network queries against the
    // supplied handle. An unknown handle must not inherit the default network's
    // info, capabilities, or link properties merely because it is connected.
    jclass connectivityAdapter = env->FindClass("adapter/core/ConnectivityManagerAdapter");
    REQUIRE(connectivityAdapter);
    auto connectivityBinder = env->GetStaticMethodID(connectivityAdapter, "getBinder",
            "()Landroid/os/IBinder;"); REQUIRE(connectivityBinder);
    jobject binder = env->CallStaticObjectMethod(connectivityAdapter, connectivityBinder);
    REQUIRE(binder);
    jclass connectivityStub = env->FindClass("android/net/IConnectivityManager$Stub");
    REQUIRE(connectivityStub);
    auto asConnectivity = env->GetStaticMethodID(connectivityStub, "asInterface",
            "(Landroid/os/IBinder;)Landroid/net/IConnectivityManager;");
    REQUIRE(asConnectivity);
    jobject connectivity = env->CallStaticObjectMethod(connectivityStub, asConnectivity, binder);
    REQUIRE(connectivity);
    jclass connectivityInterface = env->FindClass("android/net/IConnectivityManager");
    REQUIRE(connectivityInterface);
    auto activeNetwork = env->GetMethodID(connectivityInterface, "getActiveNetwork",
            "()Landroid/net/Network;"); REQUIRE(activeNetwork);
    auto networkInfoForUid = env->GetMethodID(connectivityInterface, "getNetworkInfoForUid",
            "(Landroid/net/Network;IZ)Landroid/net/NetworkInfo;"); REQUIRE(networkInfoForUid);
    auto networkCapabilities = env->GetMethodID(connectivityInterface, "getNetworkCapabilities",
            "(Landroid/net/Network;Ljava/lang/String;Ljava/lang/String;)Landroid/net/NetworkCapabilities;");
    REQUIRE(networkCapabilities);
    auto networkLinks = env->GetMethodID(connectivityInterface, "getLinkProperties",
            "(Landroid/net/Network;)Landroid/net/LinkProperties;"); REQUIRE(networkLinks);
    auto networkInfoForType = env->GetMethodID(connectivityInterface, "getNetworkInfo",
            "(I)Landroid/net/NetworkInfo;"); REQUIRE(networkInfoForType);
    jobject currentNetwork = env->CallObjectMethod(connectivity, activeNetwork); REQUIRE(currentNetwork);
    jclass networkClass = env->FindClass("android/net/Network"); REQUIRE(networkClass);
    auto networkId = env->GetFieldID(networkClass, "netId", "I"); REQUIRE(networkId);
    auto networkCtor = env->GetMethodID(networkClass, "<init>", "(I)V"); REQUIRE(networkCtor);
    jint currentId = env->GetIntField(currentNetwork, networkId); REQUIRE(currentId > 0);
    jobject unknownNetwork = env->NewObject(networkClass, networkCtor, currentId + 12345);
    REQUIRE(unknownNetwork);
    jobject currentInfo = env->CallObjectMethod(connectivity, networkInfoForUid,
            currentNetwork, jint(0), JNI_FALSE); REQUIRE(currentInfo);
    REQUIRE(env->CallObjectMethod(connectivity, networkInfoForUid,
            unknownNetwork, jint(0), JNI_FALSE) == nullptr);
    jclass networkInfoClass = env->FindClass("android/net/NetworkInfo"); REQUIRE(networkInfoClass);
    auto networkType = env->GetMethodID(networkInfoClass, "getType", "()I"); REQUIRE(networkType);
    jint currentType = env->CallIntMethod(currentInfo, networkType); REQUIRE(currentType >= 0);
    REQUIRE(env->CallObjectMethod(connectivity, networkInfoForType, currentType));
    REQUIRE(env->CallObjectMethod(connectivity, networkInfoForType, jint(-1)) == nullptr);
    jstring callingPackage = env->NewStringUTF("org.westlake.sourcehost"); REQUIRE(callingPackage);
    jobject currentCapabilities = env->CallObjectMethod(connectivity, networkCapabilities,
            currentNetwork, callingPackage, nullptr);
    REQUIRE(currentCapabilities);
    REQUIRE(env->CallObjectMethod(connectivity, networkCapabilities,
            unknownNetwork, callingPackage, nullptr) == nullptr);
    jobject currentLinks = env->CallObjectMethod(connectivity, networkLinks, currentNetwork);
    REQUIRE(currentLinks);
    REQUIRE(env->CallObjectMethod(connectivity, networkLinks, unknownNetwork) == nullptr);
    fprintf(stderr, "SOURCE_CONNECTIVITY_HANDLE_PASS current=%d unknown=null type_filter=1\n",
            currentId);
    jclass linkPropertiesClass = env->FindClass("android/net/LinkProperties");
    REQUIRE(linkPropertiesClass);
    auto getDnsServers = env->GetMethodID(linkPropertiesClass, "getDnsServers",
            "()Ljava/util/List;"); REQUIRE(getDnsServers);
    auto getLinkAddresses = env->GetMethodID(linkPropertiesClass, "getLinkAddresses",
            "()Ljava/util/List;"); REQUIRE(getLinkAddresses);
    auto getRoutes = env->GetMethodID(linkPropertiesClass, "getRoutes",
            "()Ljava/util/List;"); REQUIRE(getRoutes);
    jobject dnsServers = env->CallObjectMethod(currentLinks, getDnsServers); REQUIRE(dnsServers);
    jobject linkAddresses = env->CallObjectMethod(currentLinks, getLinkAddresses);
    REQUIRE(linkAddresses);
    jobject routes = env->CallObjectMethod(currentLinks, getRoutes); REQUIRE(routes);
    jclass listClass = env->FindClass("java/util/List"); REQUIRE(listClass);
    auto listSize = env->GetMethodID(listClass, "size", "()I"); REQUIRE(listSize);
    jint dnsCount = env->CallIntMethod(dnsServers, listSize); REQUIRE(dnsCount > 0);
    jint linkCount = env->CallIntMethod(linkAddresses, listSize); REQUIRE(linkCount > 0);
    jint routeCount = env->CallIntMethod(routes, listSize); REQUIRE(routeCount > 0);
    jclass objectClass = env->FindClass("java/lang/Object"); REQUIRE(objectClass);
    auto objectToString = env->GetMethodID(objectClass, "toString",
            "()Ljava/lang/String;"); REQUIRE(objectToString);
    jstring dnsText = static_cast<jstring>(env->CallObjectMethod(dnsServers, objectToString));
    REQUIRE(dnsText);
    jstring linkText = static_cast<jstring>(env->CallObjectMethod(linkAddresses, objectToString));
    REQUIRE(linkText);
    jstring routeText = static_cast<jstring>(env->CallObjectMethod(routes, objectToString));
    REQUIRE(routeText);
    const char *dnsChars = env->GetStringUTFChars(dnsText, nullptr); REQUIRE(dnsChars);
    const char *linkChars = env->GetStringUTFChars(linkText, nullptr); REQUIRE(linkChars);
    const char *routeChars = env->GetStringUTFChars(routeText, nullptr); REQUIRE(routeChars);
    fprintf(stderr, "SOURCE_CONNECTIVITY_LINK_VALUES_PASS dns=%d addresses=%d servers=%s links=%s\n",
            dnsCount, linkCount, dnsChars, linkChars);
    fprintf(stderr, "SOURCE_CONNECTIVITY_ROUTES_PASS routes=%d values=%s\n",
            routeCount, routeChars);
    env->ReleaseStringUTFChars(routeText, routeChars);
    env->ReleaseStringUTFChars(linkText, linkChars);
    env->ReleaseStringUTFChars(dnsText, dnsChars);

    // Compare the Android proxy getter with the proxy carried by current
    // LinkProperties. Also exercise a configured host/port/exclusion mapping
    // without changing the board's actual network or global proxy settings.
    auto networkProxy = env->GetMethodID(connectivityInterface, "getProxyForNetwork",
            "(Landroid/net/Network;)Landroid/net/ProxyInfo;"); REQUIRE(networkProxy);
    auto linkProxyGetter = env->GetMethodID(linkPropertiesClass, "getHttpProxy",
            "()Landroid/net/ProxyInfo;"); REQUIRE(linkProxyGetter);
    jobject currentProxy = env->CallObjectMethod(connectivity, networkProxy, currentNetwork);
    jobject defaultProxy = env->CallObjectMethod(connectivity, networkProxy, nullptr);
    jobject linkProxy = env->CallObjectMethod(currentLinks, linkProxyGetter);
    REQUIRE(!env->ExceptionCheck());
    REQUIRE((currentProxy == nullptr) == (defaultProxy == nullptr));
    REQUIRE((currentProxy == nullptr) == (linkProxy == nullptr));
    jclass proxyClass = env->FindClass("android/net/ProxyInfo"); REQUIRE(proxyClass);
    auto proxyEquals = env->GetMethodID(proxyClass, "equals", "(Ljava/lang/Object;)Z");
    REQUIRE(proxyEquals);
    if (currentProxy != nullptr) {
        REQUIRE(env->CallBooleanMethod(currentProxy, proxyEquals, defaultProxy));
        REQUIRE(env->CallBooleanMethod(currentProxy, proxyEquals, linkProxy));
    }
    auto buildProxy = env->GetStaticMethodID(connectivityAdapter, "buildProxyInfo",
            "(Ljava/lang/String;I[Ljava/lang/String;)Landroid/net/ProxyInfo;");
    REQUIRE(buildProxy);
    jclass proxyStringClass = env->FindClass("java/lang/String"); REQUIRE(proxyStringClass);
    jobjectArray exclusions = env->NewObjectArray(1, proxyStringClass, nullptr);
    REQUIRE(exclusions);
    jstring exclusion = env->NewStringUTF("*.internal.example.com"); REQUIRE(exclusion);
    env->SetObjectArrayElement(exclusions, 0, exclusion); REQUIRE(!env->ExceptionCheck());
    jstring host = env->NewStringUTF("proxy.example.com"); REQUIRE(host);
    jobject configuredProxy = env->CallStaticObjectMethod(connectivityAdapter, buildProxy,
            host, jint(8080), exclusions); REQUIRE(configuredProxy);
    auto getProxyHost = env->GetMethodID(proxyClass, "getHost", "()Ljava/lang/String;");
    auto getProxyPort = env->GetMethodID(proxyClass, "getPort", "()I");
    auto getProxyExclusions = env->GetMethodID(proxyClass, "getExclusionList",
            "()[Ljava/lang/String;");
    REQUIRE(getProxyHost && getProxyPort && getProxyExclusions);
    jstring returnedHost = static_cast<jstring>(env->CallObjectMethod(configuredProxy,
            getProxyHost)); REQUIRE(returnedHost);
    const char *returnedHostChars = env->GetStringUTFChars(returnedHost, nullptr);
    REQUIRE(returnedHostChars);
    REQUIRE(strcmp(returnedHostChars, "proxy.example.com") == 0);
    env->ReleaseStringUTFChars(returnedHost, returnedHostChars);
    REQUIRE(env->CallIntMethod(configuredProxy, getProxyPort) == 8080);
    jobjectArray returnedExclusions = static_cast<jobjectArray>(env->CallObjectMethod(
            configuredProxy, getProxyExclusions)); REQUIRE(returnedExclusions);
    REQUIRE(env->GetArrayLength(returnedExclusions) == 1);
    fprintf(stderr, "SOURCE_CONNECTIVITY_PROXY_PASS configured=%d default_match=1 link_match=1 synthetic=1\n",
            currentProxy != nullptr);

    // Exercise the public OH application-proxy API as a reversible A/B. This
    // proves the native snapshot, Java ProxyInfo mapping, default-network
    // getter, and LinkProperties view all carry the same configured values.
    void* netConnection = dlopen("libnet_connection.so", RTLD_NOW | RTLD_LOCAL);
    REQUIRE(netConnection);
    auto getDefaultHttpProxy = reinterpret_cast<int32_t (*)(NetConn_HttpProxy*)>(
            dlsym(netConnection, "OH_NetConn_GetDefaultHttpProxy"));
    auto setAppHttpProxy = reinterpret_cast<int32_t (*)(NetConn_HttpProxy*)>(
            dlsym(netConnection, "OH_NetConn_SetAppHttpProxy"));
    REQUIRE(getDefaultHttpProxy && setAppHttpProxy);
    // NetConn_HttpProxy embeds the full exclusion table and is too large for
    // Appspawn's preload-thread stack. Keep all three snapshots on the heap.
    auto* proxyStorage = static_cast<NetConn_HttpProxy*>(
            calloc(3, sizeof(NetConn_HttpProxy))); REQUIRE(proxyStorage);
    NetConn_HttpProxy* previousProxy = &proxyStorage[0];
    NetConn_HttpProxy* testProxy = &proxyStorage[1];
    NetConn_HttpProxy* observedProxy = &proxyStorage[2];
    REQUIRE(getDefaultHttpProxy(previousProxy) == 0);
    std::strncpy(testProxy->host, "127.0.0.1", sizeof(testProxy->host) - 1);
    testProxy->port = 18080;
    testProxy->exclusionListSize = 1;
    std::strncpy(testProxy->exclusionList[0], "localhost",
            sizeof(testProxy->exclusionList[0]) - 1);
    REQUIRE(setAppHttpProxy(testProxy) == 0);
    REQUIRE(getDefaultHttpProxy(observedProxy) == 0);
    REQUIRE(strcmp(observedProxy->host, testProxy->host) == 0 &&
            observedProxy->port == testProxy->port && observedProxy->exclusionListSize == 1 &&
            strcmp(observedProxy->exclusionList[0], testProxy->exclusionList[0]) == 0);
    // OhConnectivityState intentionally caches a snapshot for 250 ms.
    usleep(300000);
    jobject configuredBoardProxy = env->CallObjectMethod(connectivity, networkProxy,
            currentNetwork); REQUIRE(configuredBoardProxy);
    jobject configuredDefaultProxy = env->CallObjectMethod(connectivity, networkProxy, nullptr);
    REQUIRE(configuredDefaultProxy);
    jobject configuredLinks = env->CallObjectMethod(connectivity, networkLinks, currentNetwork);
    REQUIRE(configuredLinks);
    jobject configuredLinkProxy = env->CallObjectMethod(configuredLinks, linkProxyGetter);
    REQUIRE(configuredLinkProxy);
    jstring boardProxyHost = static_cast<jstring>(env->CallObjectMethod(configuredBoardProxy,
            getProxyHost)); REQUIRE(boardProxyHost);
    const char* boardProxyHostChars = env->GetStringUTFChars(boardProxyHost, nullptr);
    REQUIRE(boardProxyHostChars);
    bool boardProxyHostMatches = strcmp(boardProxyHostChars, testProxy->host) == 0;
    env->ReleaseStringUTFChars(boardProxyHost, boardProxyHostChars);
    REQUIRE(boardProxyHostMatches);
    REQUIRE(env->CallIntMethod(configuredBoardProxy, getProxyPort) == testProxy->port);
    REQUIRE(env->CallBooleanMethod(configuredBoardProxy, proxyEquals,
            configuredDefaultProxy));
    REQUIRE(env->CallBooleanMethod(configuredBoardProxy, proxyEquals, configuredLinkProxy));
    jobjectArray boardExclusions = static_cast<jobjectArray>(env->CallObjectMethod(
            configuredBoardProxy, getProxyExclusions)); REQUIRE(boardExclusions);
    REQUIRE(env->GetArrayLength(boardExclusions) == 1);
    jstring boardExclusion = static_cast<jstring>(env->GetObjectArrayElement(
            boardExclusions, 0)); REQUIRE(boardExclusion);
    const char* boardExclusionChars = env->GetStringUTFChars(boardExclusion, nullptr);
    REQUIRE(boardExclusionChars);
    bool boardExclusionMatches = strcmp(boardExclusionChars,
            testProxy->exclusionList[0]) == 0;
    env->ReleaseStringUTFChars(boardExclusion, boardExclusionChars);
    REQUIRE(boardExclusionMatches);
    REQUIRE(setAppHttpProxy(previousProxy) == 0);
    memset(observedProxy, 0, sizeof(*observedProxy));
    REQUIRE(getDefaultHttpProxy(observedProxy) == 0);
    REQUIRE(strcmp(observedProxy->host, previousProxy->host) == 0 &&
            observedProxy->port == previousProxy->port &&
            observedProxy->exclusionListSize == previousProxy->exclusionListSize);
    free(proxyStorage);
    REQUIRE(dlclose(netConnection) == 0);
    fprintf(stderr, "SOURCE_CONNECTIVITY_CONFIGURED_PROXY_PASS host=127.0.0.1 port=18080 exclusions=1 default_match=1 link_match=1 restored=1\n");

    // The framework service must return a defensive, caller-authorized copy of
    // supplied capabilities.  Give the copy a sensitive SSID so this verifies
    // actual redaction rather than merely accepting already-empty board state.
    jclass capabilitiesClass = env->FindClass("android/net/NetworkCapabilities");
    REQUIRE(capabilitiesClass);
    auto capabilitiesCopy = env->GetMethodID(capabilitiesClass, "<init>",
            "(Landroid/net/NetworkCapabilities;)V"); REQUIRE(capabilitiesCopy);
    auto setSsid = env->GetMethodID(capabilitiesClass, "setSSID",
            "(Ljava/lang/String;)Landroid/net/NetworkCapabilities;"); REQUIRE(setSsid);
    auto getSsid = env->GetMethodID(capabilitiesClass, "getSsid", "()Ljava/lang/String;");
    REQUIRE(getSsid);
    jobject sensitiveCapabilities = env->NewObject(capabilitiesClass, capabilitiesCopy,
            currentCapabilities); REQUIRE(sensitiveCapabilities);
    jstring privateSsid = env->NewStringUTF("source-contract-private-ssid");
    REQUIRE(privateSsid);
    REQUIRE(env->CallObjectMethod(sensitiveCapabilities, setSsid, privateSsid));
    auto redactedCapabilities = env->GetMethodID(connectivityInterface,
            "getRedactedNetworkCapabilitiesForPackage",
            "(Landroid/net/NetworkCapabilities;ILjava/lang/String;Ljava/lang/String;)"
            "Landroid/net/NetworkCapabilities;");
    REQUIRE(redactedCapabilities);
    jclass binderClass = env->FindClass("android/os/Binder"); REQUIRE(binderClass);
    auto callingUidMethod = env->GetStaticMethodID(binderClass, "getCallingUid", "()I");
    REQUIRE(callingUidMethod);
    jint callingUid = env->CallStaticIntMethod(binderClass, callingUidMethod);
    REQUIRE(!env->ExceptionCheck());
    jobject redacted = env->CallObjectMethod(connectivity, redactedCapabilities,
            sensitiveCapabilities, callingUid, callingPackage, nullptr);
    REQUIRE(redacted && !env->IsSameObject(redacted, sensitiveCapabilities));
    REQUIRE(env->CallObjectMethod(redacted, getSsid) == nullptr);
    jstring preservedSsid = static_cast<jstring>(env->CallObjectMethod(
            sensitiveCapabilities, getSsid)); REQUIRE(preservedSsid);
    jclass redactionStringClass = env->FindClass("java/lang/String");
    REQUIRE(redactionStringClass);
    auto redactionStringEquals = env->GetMethodID(redactionStringClass, "equals",
            "(Ljava/lang/Object;)Z"); REQUIRE(redactionStringEquals);
    REQUIRE(env->CallBooleanMethod(privateSsid, redactionStringEquals, preservedSsid));
    jobject deniedRedaction = env->CallObjectMethod(connectivity, redactedCapabilities,
            sensitiveCapabilities, callingUid + 1, callingPackage, nullptr);
    if (deniedRedaction != nullptr || !env->ExceptionCheck()) {
        fprintf(stderr, "SOURCE_FRAMEWORK_CONTRACT_FAIL line=%d: foreign UID redaction allowed\n",
                __LINE__);
        env->PopLocalFrame(nullptr);
        return -1;
    }
    jthrowable denied = env->ExceptionOccurred();
    env->ExceptionClear();
    REQUIRE(denied);
    jclass securityException = env->FindClass("java/lang/SecurityException");
    REQUIRE(securityException && env->IsInstanceOf(denied, securityException));
    fprintf(stderr, "SOURCE_CONNECTIVITY_REDACTION_PASS copy=1 ssid_cleared=1 source_preserved=1 foreign_uid_denied=1 uid=%d\n",
            callingUid);

    // Keep callback messages queued on this contract thread so their public
    // ConnectivityManager callback codes can be inspected without a test-only
    // adapter hook. An unsatisfiable cellular request must time out with
    // CALLBACK_UNAVAIL, while releasing the same kind of request must suppress
    // every subsequent callback.
    auto hasTransport = env->GetMethodID(capabilitiesClass, "hasTransport", "(I)Z");
    auto emptyCapabilities = env->GetMethodID(capabilitiesClass, "<init>", "()V");
    auto addTransport = env->GetMethodID(capabilitiesClass, "addTransportType",
            "(I)Landroid/net/NetworkCapabilities;");
    REQUIRE(hasTransport && emptyCapabilities && addTransport);
    auto cellularField = env->GetStaticFieldID(capabilitiesClass, "TRANSPORT_CELLULAR", "I");
    REQUIRE(cellularField);
    jint cellular = env->GetStaticIntField(capabilitiesClass, cellularField);
    REQUIRE(!env->CallBooleanMethod(currentCapabilities, hasTransport, cellular));
    jobject impossibleCapabilities = env->NewObject(capabilitiesClass, emptyCapabilities);
    REQUIRE(impossibleCapabilities);
    REQUIRE(env->CallObjectMethod(impossibleCapabilities, addTransport, cellular));

    jclass looperClass = env->FindClass("android/os/Looper"); REQUIRE(looperClass);
    auto myLooper = env->GetStaticMethodID(looperClass, "myLooper", "()Landroid/os/Looper;");
    auto prepareLooper = env->GetStaticMethodID(looperClass, "prepare", "()V");
    REQUIRE(myLooper && prepareLooper);
    jobject callbackLooper = env->CallStaticObjectMethod(looperClass, myLooper);
    REQUIRE(!env->ExceptionCheck());
    if (callbackLooper == nullptr) {
        env->CallStaticVoidMethod(looperClass, prepareLooper); REQUIRE(!env->ExceptionCheck());
        callbackLooper = env->CallStaticObjectMethod(looperClass, myLooper);
    }
    REQUIRE(callbackLooper);
    jclass handlerClass = env->FindClass("android/os/Handler"); REQUIRE(handlerClass);
    auto handlerCtor = env->GetMethodID(handlerClass, "<init>", "(Landroid/os/Looper;)V");
    auto hasMessages = env->GetMethodID(handlerClass, "hasMessages", "(I)Z");
    auto removeMessages = env->GetMethodID(handlerClass, "removeCallbacksAndMessages",
            "(Ljava/lang/Object;)V");
    REQUIRE(handlerCtor && hasMessages && removeMessages);
    jobject callbackHandler = env->NewObject(handlerClass, handlerCtor, callbackLooper);
    REQUIRE(callbackHandler);
    jclass messengerClass = env->FindClass("android/os/Messenger"); REQUIRE(messengerClass);
    auto messengerCtor = env->GetMethodID(messengerClass, "<init>",
            "(Landroid/os/Handler;)V"); REQUIRE(messengerCtor);
    jobject callbackMessenger = env->NewObject(messengerClass, messengerCtor, callbackHandler);
    REQUIRE(callbackMessenger);
    jclass requestTypeClass = env->FindClass("android/net/NetworkRequest$Type");
    REQUIRE(requestTypeClass);
    auto requestTypeField = env->GetStaticFieldID(requestTypeClass, "REQUEST",
            "Landroid/net/NetworkRequest$Type;"); REQUIRE(requestTypeField);
    jobject requestType = env->GetStaticObjectField(requestTypeClass, requestTypeField);
    REQUIRE(requestType);
    jclass enumClass = env->FindClass("java/lang/Enum"); REQUIRE(enumClass);
    auto enumOrdinal = env->GetMethodID(enumClass, "ordinal", "()I"); REQUIRE(enumOrdinal);
    jint requestOrdinal = env->CallIntMethod(requestType, enumOrdinal);
    REQUIRE(!env->ExceptionCheck());
    auto requestNetwork = env->GetMethodID(connectivityInterface, "requestNetwork",
            "(ILandroid/net/NetworkCapabilities;ILandroid/os/Messenger;ILandroid/os/IBinder;"
            "IILjava/lang/String;Ljava/lang/String;)Landroid/net/NetworkRequest;");
    auto releaseNetwork = env->GetMethodID(connectivityInterface, "releaseNetworkRequest",
            "(Landroid/net/NetworkRequest;)V");
    REQUIRE(requestNetwork && releaseNetwork);
    constexpr jint callbackAvailable = 2;
    constexpr jint callbackLost = 4;
    constexpr jint callbackUnavailable = 5;
    constexpr jint callbackCapabilitiesChanged = 6;
    constexpr jint callbackLinkPropertiesChanged = 7;
    jobject timeoutRequest = env->CallObjectMethod(connectivity, requestNetwork,
            callingUid, impossibleCapabilities, requestOrdinal, callbackMessenger,
            jint(250), nullptr, jint(-1), jint(0), callingPackage, nullptr);
    REQUIRE(timeoutRequest);
    usleep(1400000);
    REQUIRE(env->CallBooleanMethod(callbackHandler, hasMessages, callbackUnavailable));
    REQUIRE(!env->CallBooleanMethod(callbackHandler, hasMessages, callbackAvailable));
    env->CallVoidMethod(callbackHandler, removeMessages, nullptr); REQUIRE(!env->ExceptionCheck());
    // The adapter logs registered=false here, independently confirming that
    // timeout removed the request before this public release call.
    env->CallVoidMethod(connectivity, releaseNetwork, timeoutRequest);
    REQUIRE(!env->ExceptionCheck());

    jobject releasedRequest = env->CallObjectMethod(connectivity, requestNetwork,
            callingUid, impossibleCapabilities, requestOrdinal, callbackMessenger,
            jint(0), nullptr, jint(-1), jint(0), callingPackage, nullptr);
    REQUIRE(releasedRequest);
    env->CallVoidMethod(connectivity, releaseNetwork, releasedRequest);
    REQUIRE(!env->ExceptionCheck());
    usleep(1300000);
    REQUIRE(!env->CallBooleanMethod(callbackHandler, hasMessages, callbackAvailable));
    REQUIRE(!env->CallBooleanMethod(callbackHandler, hasMessages, callbackLost));
    REQUIRE(!env->CallBooleanMethod(callbackHandler, hasMessages, callbackUnavailable));
    REQUIRE(!env->CallBooleanMethod(callbackHandler, hasMessages,
            callbackCapabilitiesChanged));
    REQUIRE(!env->CallBooleanMethod(callbackHandler, hasMessages,
            callbackLinkPropertiesChanged));
    fprintf(stderr, "SOURCE_CONNECTIVITY_CALLBACKS_PASS unavailable=1 timeout_removed=1 release_suppressed=1 request_type=%d\n",
            requestOrdinal);

    // SoundPool is a JNI-loaded Android library, but its compressed-audio
    // implementation belongs at the OH boundary. Prove both halves: the
    // source-built shim can create the public OH AVPlayer backend, and the
    // unchanged Java API can load and play an actual finite WAV descriptor.
    void* soundpoolLibrary = dlopen("libsoundpool.so", RTLD_NOW | RTLD_LOCAL);
    REQUIRE(soundpoolLibrary);
    auto soundpoolBackendProbe = reinterpret_cast<int (*)()>(
            dlsym(soundpoolLibrary, "oh_soundpool_backend_probe"));
    REQUIRE(soundpoolBackendProbe && soundpoolBackendProbe() == 1);
    REQUIRE(dlclose(soundpoolLibrary) == 0);
    fprintf(stderr, "SOURCE_SOUNDPOOL_BACKEND_PASS avplayer_create=1 release=1\n");

    char soundPath[] = "/data/local/tmp/westlake-soundpool-XXXXXX";
    int soundFd = mkstemp(soundPath); REQUIRE(soundFd >= 0);
    constexpr uint32_t sampleRate = 44100;
    constexpr uint32_t sampleBytes = sampleRate * 2;
    std::vector<unsigned char> wav(44 + sampleBytes, 0);
    auto put16 = [&wav](size_t offset, uint16_t value) {
        wav[offset] = static_cast<unsigned char>(value);
        wav[offset + 1] = static_cast<unsigned char>(value >> 8);
    };
    auto put32 = [&wav](size_t offset, uint32_t value) {
        wav[offset] = static_cast<unsigned char>(value);
        wav[offset + 1] = static_cast<unsigned char>(value >> 8);
        wav[offset + 2] = static_cast<unsigned char>(value >> 16);
        wav[offset + 3] = static_cast<unsigned char>(value >> 24);
    };
    std::memcpy(wav.data(), "RIFF", 4);
    put32(4, static_cast<uint32_t>(wav.size() - 8));
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 1);
    put32(24, sampleRate);
    put32(28, sampleRate * 2);
    put16(32, 2);
    put16(34, 16);
    std::memcpy(wav.data() + 36, "data", 4);
    put32(40, sampleBytes);
    size_t written = 0;
    while (written < wav.size()) {
        ssize_t count = ::write(soundFd, wav.data() + written, wav.size() - written);
        REQUIRE(count > 0);
        written += static_cast<size_t>(count);
    }
    REQUIRE(::lseek(soundFd, 0, SEEK_SET) == 0);
    env->SetIntField(fdObject, descriptorField, soundFd); REQUIRE(!env->ExceptionCheck());
    jclass soundpool = env->FindClass("android/media/SoundPool"); REQUIRE(soundpool);
    auto soundpoolSetup = env->GetMethodID(soundpool, "native_setup",
            "(ILjava/lang/Object;Ljava/lang/String;)I"); REQUIRE(soundpoolSetup);
    auto soundpoolLoad = env->GetMethodID(soundpool, "load", "(Ljava/io/FileDescriptor;JJI)I");
    auto soundpoolPlay = env->GetMethodID(soundpool, "_play", "(IFFIIFI)I");
    auto soundpoolPause = env->GetMethodID(soundpool, "pause", "(I)V");
    auto soundpoolResume = env->GetMethodID(soundpool, "resume", "(I)V");
    auto soundpoolStop = env->GetMethodID(soundpool, "stop", "(I)V");
    auto soundpoolVolume = env->GetMethodID(soundpool, "_setVolume", "(IFF)V");
    auto soundpoolUnload = env->GetMethodID(soundpool, "unload", "(I)Z");
    auto soundpoolRelease = env->GetMethodID(soundpool, "native_release", "()V");
    REQUIRE(soundpoolLoad && soundpoolPlay && soundpoolPause && soundpoolResume
            && soundpoolStop && soundpoolVolume && soundpoolUnload && soundpoolRelease);
    jclass attributesBuilder = env->FindClass("android/media/AudioAttributes$Builder");
    REQUIRE(attributesBuilder);
    auto attributesBuilderCtor = env->GetMethodID(attributesBuilder, "<init>", "()V");
    auto attributesBuild = env->GetMethodID(attributesBuilder, "build",
            "()Landroid/media/AudioAttributes;");
    REQUIRE(attributesBuilderCtor && attributesBuild);
    jobject attributesBuilderObject = env->NewObject(attributesBuilder, attributesBuilderCtor);
    REQUIRE(attributesBuilderObject);
    jobject attributes = env->CallObjectMethod(attributesBuilderObject, attributesBuild);
    REQUIRE(attributes);
    jobject soundpoolObject = env->AllocObject(soundpool); REQUIRE(soundpoolObject);
    jstring sourcePackage = env->NewStringUTF("source.contract"); REQUIRE(sourcePackage);
    REQUIRE(env->CallIntMethod(soundpoolObject, soundpoolSetup, 1, attributes, sourcePackage) == 0);
    jint sampleId = env->CallIntMethod(soundpoolObject, soundpoolLoad, fdObject,
            jlong(0), jlong(wav.size()), 1);
    REQUIRE(sampleId > 0 && !env->ExceptionCheck());
    REQUIRE(::close(soundFd) == 0);
    soundFd = -1;
    REQUIRE(::unlink(soundPath) == 0);
    env->SetIntField(fdObject, descriptorField, -1); REQUIRE(!env->ExceptionCheck());
    jint streamId = 0;
    int loadPolls = 0;
    for (; loadPolls < 100 && streamId == 0; ++loadPolls) {
        usleep(50000);
        streamId = env->CallIntMethod(soundpoolObject, soundpoolPlay,
                sampleId, 1.0f, 1.0f, 1, 0, 1.0f, 0);
        REQUIRE(!env->ExceptionCheck());
    }
    REQUIRE(streamId > 0);
    usleep(300000);
    env->CallVoidMethod(soundpoolObject, soundpoolVolume, streamId, 0.25f, 0.25f);
    env->CallVoidMethod(soundpoolObject, soundpoolPause, streamId);
    REQUIRE(!env->ExceptionCheck());
    usleep(50000);
    env->CallVoidMethod(soundpoolObject, soundpoolResume, streamId);
    REQUIRE(!env->ExceptionCheck());
    usleep(50000);
    env->CallVoidMethod(soundpoolObject, soundpoolStop, streamId);
    REQUIRE(!env->ExceptionCheck());
    REQUIRE(env->CallBooleanMethod(soundpoolObject, soundpoolUnload, sampleId));
    env->CallVoidMethod(soundpoolObject, soundpoolRelease); REQUIRE(!env->ExceptionCheck());
    fprintf(stderr, "SOURCE_SOUNDPOOL_JAVA_PASS load=1 play=1 controls=1 polls=%d bytes=%zu\n",
            loadPolls, wav.size());

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
