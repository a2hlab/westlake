/*
 * Natives of adapter.core.SocketBinderNative. Reads a Parcel's bytes and object table through
 * libbinder's own accessors, and moves bytes and file descriptors over Unix stream sockets.
 *
 * Android's Parcel keeps binder objects and descriptors as flat_binder_object entries in its data,
 * listed in an offset table. Java can't read those while they exist (Parcel.marshall throws), and
 * sendmsg with SCM_RIGHTS has no public Java form that keeps a descriptor with its frame.
 */
#define _GNU_SOURCE
#include <jni.h>
#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#pragma clang diagnostic ignored "-Wsign-compare" /* musl's CMSG_NXTHDR */

#define MAX_FDS 64
#define CHUNK 16384

typedef const void* (*ParcelData)(const void*);
typedef size_t (*ParcelSize)(const void*);
typedef const uint64_t* (*ParcelObjects)(const void*);

static ParcelData g_data;
static ParcelSize g_size;
static ParcelObjects g_objects;
static ParcelSize g_objects_count;
static jfieldID g_native_ptr;

/* libwestlake_binder on Westlake, libbinder on Android. Already loaded in either case. */
static void* binder_symbol(const char* name)
{
    void* symbol = dlsym(RTLD_DEFAULT, name);
    static const char* const libraries[] = {"libwestlake_binder.so", "libbinder.so"};
    for (size_t i = 0; symbol == NULL && i < sizeof(libraries) / sizeof(libraries[0]); i++) {
        void* library = dlopen(libraries[i], RTLD_NOW);
        if (library != NULL) symbol = dlsym(library, name);
    }
    return symbol;
}

static void throw_named(JNIEnv* env, const char* class_name, const char* message)
{
    jclass c = (*env)->FindClass(env, class_name);
    if (c != NULL) (*env)->ThrowNew(env, c, message);
}

static void throw_errno(JNIEnv* env, const char* what)
{
    char message[128];
    size_t n = strlen(what);
    if (n > 60) n = 60;
    memcpy(message, what, n);
    message[n] = ':';
    message[n + 1] = ' ';
    strncpy(message + n + 2, strerror(errno), sizeof(message) - n - 3);
    message[sizeof(message) - 1] = 0;
    throw_named(env, "java/io/IOException", message);
}

/* The native Parcel behind a Java Parcel, or NULL with an exception pending. */
static const void* native_parcel(JNIEnv* env, jobject parcel)
{
    if (g_data == NULL) {
        g_data = (ParcelData)binder_symbol("_ZNK7android6Parcel4dataEv");
        g_size = (ParcelSize)binder_symbol("_ZNK7android6Parcel8dataSizeEv");
        g_objects = (ParcelObjects)binder_symbol("_ZNK7android6Parcel10ipcObjectsEv");
        g_objects_count = (ParcelSize)binder_symbol("_ZNK7android6Parcel15ipcObjectsCountEv");
    }
    if (g_data == NULL || g_size == NULL || g_objects == NULL || g_objects_count == NULL) {
        throw_named(env, "java/lang/IllegalStateException", "libbinder Parcel accessors not found");
        return NULL;
    }
    if (g_native_ptr == NULL) {
        jclass c = (*env)->GetObjectClass(env, parcel);
        g_native_ptr = (*env)->GetFieldID(env, c, "mNativePtr", "J");
        if (g_native_ptr == NULL) return NULL;
    }
    const void* p = (const void*)(intptr_t)(*env)->GetLongField(env, parcel, g_native_ptr);
    if (p == NULL) throw_named(env, "java/lang/IllegalStateException", "Parcel already recycled");
    return p;
}

JNIEXPORT jbyteArray JNICALL Java_adapter_core_SocketBinderNative_parcelBytes(JNIEnv* env, jclass c, jobject parcel)
{
    (void)c;
    const void* p = native_parcel(env, parcel);
    if (p == NULL) return NULL;
    size_t size = g_size(p);
    jbyteArray out = (*env)->NewByteArray(env, (jsize)size);
    if (out != NULL && size > 0) {
        (*env)->SetByteArrayRegion(env, out, 0, (jsize)size, (const jbyte*)g_data(p));
    }
    return out;
}

JNIEXPORT jintArray JNICALL Java_adapter_core_SocketBinderNative_parcelObjects(JNIEnv* env, jclass c, jobject parcel)
{
    (void)c;
    const void* p = native_parcel(env, parcel);
    if (p == NULL) return NULL;
    size_t count = g_objects_count(p);
    size_t size = g_size(p);
    const uint64_t* offsets = g_objects(p);
    const char* data = (const char*)g_data(p);
    jintArray out = (*env)->NewIntArray(env, (jsize)(count * 2));
    if (out == NULL) return NULL;
    for (size_t i = 0; i < count; i++) {
        if (offsets[i] + sizeof(uint32_t) > size) {
            throw_named(env, "java/lang/IllegalStateException", "Parcel object outside its data");
            return NULL;
        }
        uint32_t type;
        memcpy(&type, data + offsets[i], sizeof(type));
        jint pair[2] = {(jint)offsets[i], (jint)type};
        (*env)->SetIntArrayRegion(env, out, (jsize)(i * 2), 2, pair);
    }
    return out;
}

JNIEXPORT jintArray JNICALL Java_adapter_core_SocketBinderNative_socketPair(JNIEnv* env, jclass c)
{
    (void)c;
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0) {
        throw_errno(env, "socketpair");
        return NULL;
    }
    jintArray out = (*env)->NewIntArray(env, 2);
    if (out != NULL) (*env)->SetIntArrayRegion(env, out, 0, 2, (const jint*)fds);
    return out;
}

/* An abstract address: a leading NUL, then the name. Returns the address length, or 0 if too long. */
static socklen_t abstract_address(JNIEnv* env, jstring name, struct sockaddr_un* address)
{
    memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    jsize n = (*env)->GetStringUTFLength(env, name);
    if ((size_t)n + 1 > sizeof(address->sun_path)) {
        throw_named(env, "java/io/IOException", "socket name too long");
        return 0;
    }
    (*env)->GetStringUTFRegion(env, name, 0, (*env)->GetStringLength(env, name), address->sun_path + 1);
    return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + (size_t)n);
}

JNIEXPORT jint JNICALL Java_adapter_core_SocketBinderNative_listen(JNIEnv* env, jclass c, jstring name)
{
    (void)c;
    struct sockaddr_un address;
    socklen_t length = abstract_address(env, name, &address);
    if (length == 0) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || bind(fd, (struct sockaddr*)&address, length) != 0 || listen(fd, 16) != 0) {
        throw_errno(env, "listen");
        if (fd >= 0) close(fd);
        return -1;
    }
    return fd;
}

JNIEXPORT jint JNICALL Java_adapter_core_SocketBinderNative_accept(JNIEnv* env, jclass c, jint fd)
{
    (void)c;
    int client;
    do {
        client = accept4(fd, NULL, NULL, SOCK_CLOEXEC);
    } while (client < 0 && errno == EINTR);
    if (client < 0) throw_errno(env, "accept");
    return client;
}

JNIEXPORT jint JNICALL Java_adapter_core_SocketBinderNative_connect(JNIEnv* env, jclass c, jstring name)
{
    (void)c;
    struct sockaddr_un address;
    socklen_t length = abstract_address(env, name, &address);
    if (length == 0) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, (struct sockaddr*)&address, length) != 0) {
        throw_errno(env, "connect");
        if (fd >= 0) close(fd);
        return -1;
    }
    return fd;
}

JNIEXPORT jint JNICALL Java_adapter_core_SocketBinderNative_send(JNIEnv* env, jclass c, jint fd, jbyteArray buf,
                                                                 jint off, jint len, jintArray fds)
{
    (void)c;
    char data[CHUNK];
    size_t n = len < CHUNK ? (size_t)len : CHUNK;
    (*env)->GetByteArrayRegion(env, buf, off, (jsize)n, (jbyte*)data);
    if ((*env)->ExceptionCheck(env)) return -1;
    struct iovec iov = {data, n};
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    char control[CMSG_SPACE(sizeof(int) * MAX_FDS)];
    jsize count = fds == NULL ? 0 : (*env)->GetArrayLength(env, fds);
    if (count > MAX_FDS) {
        throw_named(env, "java/io/IOException", "too many descriptors");
        return -1;
    }
    if (count > 0) {
        memset(control, 0, sizeof(control));
        message.msg_control = control;
        message.msg_controllen = CMSG_SPACE(sizeof(int) * (size_t)count);
        struct cmsghdr* header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(sizeof(int) * (size_t)count);
        (*env)->GetIntArrayRegion(env, fds, 0, count, (jint*)CMSG_DATA(header));
    }
    ssize_t sent;
    do {
        sent = sendmsg(fd, &message, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    if (sent < 0) {
        throw_errno(env, "send");
        return -1;
    }
    return (jint)sent;
}

JNIEXPORT jint JNICALL Java_adapter_core_SocketBinderNative_recv(JNIEnv* env, jclass c, jint fd, jbyteArray buf,
                                                                 jint off, jint len, jintArray fds)
{
    (void)c;
    char data[CHUNK];
    struct iovec iov = {data, len < CHUNK ? (size_t)len : CHUNK};
    char control[CMSG_SPACE(sizeof(int) * MAX_FDS)];
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    ssize_t got;
    do {
        got = recvmsg(fd, &message, MSG_CMSG_CLOEXEC);
    } while (got < 0 && errno == EINTR);
    if (got < 0) {
        throw_errno(env, "recv");
        return -1;
    }
    jint count = 0;
    jsize room = (*env)->GetArrayLength(env, fds) - 1;
    for (struct cmsghdr* header = CMSG_FIRSTHDR(&message); header != NULL; header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS) continue;
        size_t arrived = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        const int* received = (const int*)CMSG_DATA(header);
        for (size_t i = 0; i < arrived; i++) {
            if (count < room) {
                jint value = received[i];
                (*env)->SetIntArrayRegion(env, fds, 1 + count, 1, &value);
                count++;
            } else {
                close(received[i]);
            }
        }
    }
    (*env)->SetIntArrayRegion(env, fds, 0, 1, &count);
    if (got > 0) (*env)->SetByteArrayRegion(env, buf, off, (jsize)got, (const jbyte*)data);
    return (jint)got;
}

JNIEXPORT jint JNICALL Java_adapter_core_SocketBinderNative_peerPid(JNIEnv* env, jclass c, jint fd)
{
    (void)c;
    struct ucred credentials;
    socklen_t length = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) {
        throw_errno(env, "peer credentials");
        return -1;
    }
    return credentials.pid;
}

/*
 * Who Binder.getCallingUid answers for. Every Westlake process shares one OH uid and no call here
 * arrives through the driver, so libbinder's answer would always be this process. Set while serving
 * a socket channel's call; otherwise this thread answers for itself, as a local call does.
 * Binder's natives are @CriticalNative, so they take no JNIEnv and must stay this simple.
 */
static __thread jint t_calling_uid;
static __thread jint t_calling_pid;

static jint calling_uid(void)
{
    return t_calling_uid != 0 ? t_calling_uid : (jint)getuid();
}

static jint calling_pid(void)
{
    return t_calling_uid != 0 ? t_calling_pid : (jint)getpid();
}

/*
 * The token's layout is libbinder's: uid in the high half, pid in the low, and bit 30 of the low
 * half set once an identity has been cleared. Looper compares tokens across messages and reports any
 * change, and its first token came from libbinder, so this must match bit for bit.
 */
#define EXPLICIT_IDENTITY 0x40000000u
static __thread uint32_t t_explicit;

static jlong identity_token(void)
{
    /* A thread that answers for itself counts as cleared: its Looper took its first token from
     * libbinder before these natives replaced it, cleared, and this must compare equal to that. */
    uint32_t flag = t_calling_uid != 0 ? t_explicit : EXPLICIT_IDENTITY;
    return (jlong)(((uint64_t)(uint32_t)calling_uid() << 32) | ((uint32_t)calling_pid() | flag));
}

static void set_identity(jlong token)
{
    t_calling_uid = (jint)((uint64_t)token >> 32);
    t_calling_pid = (jint)((uint32_t)token & ~EXPLICIT_IDENTITY);
    t_explicit = (uint32_t)token & EXPLICIT_IDENTITY;
}

/*
 * Binder.isDirectlyHandlingTransaction: true while this thread serves a socket channel's call, as
 * libbinder answers while a thread executes a transaction from the driver (its serving stack). Nested
 * calls on a waiting thread count up and down. AttributionSource refuses to be unparceled outside a
 * transaction (Android 12 and later), so every content provider call served over a channel threw.
 */
static __thread int t_serving;

static jboolean directly_handling(void)
{
    return t_serving > 0 ? JNI_TRUE : JNI_FALSE;
}

static jlong critical_clear_identity(void)
{
    jlong old = identity_token();
    t_calling_uid = 0;
    t_calling_pid = 0;
    t_explicit = EXPLICIT_IDENTITY;
    return old;
}

JNIEXPORT void JNICALL Java_adapter_core_SocketBinderNative_installCallingIdentity(JNIEnv* env, jclass c)
{
    (void)c;
    static const JNINativeMethod methods[] = {
        {"getCallingUid", "()I", (void*)calling_uid},
        {"getCallingPid", "()I", (void*)calling_pid},
        {"clearCallingIdentity", "()J", (void*)critical_clear_identity},
        {"restoreCallingIdentity", "(J)V", (void*)set_identity},
    };
    /* Apart, so that a framework without it (before Android 14) keeps the identity natives above. */
    static const JNINativeMethod serving[] = {
        {"isDirectlyHandlingTransactionNative", "()Z", (void*)directly_handling},
    };
    jclass binder = (*env)->FindClass(env, "android/os/Binder");
    if (binder != NULL) {
        (*env)->RegisterNatives(env, binder, methods, sizeof(methods) / sizeof(methods[0]));
        if ((*env)->RegisterNatives(env, binder, serving, 1) != JNI_OK) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, binder);
    }
}

JNIEXPORT jlong JNICALL Java_adapter_core_SocketBinderNative_setCallingIdentity(JNIEnv* env, jclass c, jint uid, jint pid)
{
    (void)env;
    (void)c;
    jlong old = identity_token();
    t_calling_uid = uid;
    t_calling_pid = pid;
    t_explicit = 0;
    t_serving++;
    return old;
}

JNIEXPORT void JNICALL Java_adapter_core_SocketBinderNative_restoreCallingIdentity(JNIEnv* env, jclass c, jlong identity)
{
    (void)env;
    (void)c;
    set_identity(identity);
    if (t_serving > 0) t_serving--;
}

JNIEXPORT void JNICALL Java_adapter_core_SocketBinderNative_shutdown(JNIEnv* env, jclass c, jint fd)
{
    (void)env;
    (void)c;
    shutdown(fd, SHUT_RDWR);
}

JNIEXPORT void JNICALL Java_adapter_core_SocketBinderNative_close(JNIEnv* env, jclass c, jint fd)
{
    (void)env;
    (void)c;
    close(fd);
}

/*
 * CursorWindow's parcel natives. liboh_android_runtime's hand CursorWindow::writeToParcel and
 * createFromParcel a null Parcel. Nothing reached them while every provider ran in the process that
 * asked it: a provider's rows cross in a CursorWindow only to a caller in another process. Providers
 * of the Play services process, queried by the Play Store's process over SocketBinder, are the first,
 * and the provider's process died on the first query. These pass the Parcel behind the Java one to the
 * same CursorWindow methods, which liboh_android_runtime exports.
 */
typedef int32_t (*CursorWindowWrite)(void* window, const void* parcel);
typedef int32_t (*CursorWindowCreate)(const void* parcel, void** window);
static CursorWindowWrite g_cursor_write;
static CursorWindowCreate g_cursor_create;

static void* runtime_symbol(const char* name)
{
    void* symbol = dlsym(RTLD_DEFAULT, name);
    if (symbol == NULL) {
        void* library = dlopen("liboh_android_runtime.so", RTLD_NOW);
        if (library != NULL) symbol = dlsym(library, name);
    }
    return symbol;
}

static void cursor_window_write(JNIEnv* env, jclass c, jlong window, jobject parcel)
{
    (void)c;
    const void* p = native_parcel(env, parcel);
    if (p == NULL) return;
    if (window == 0 || g_cursor_write == NULL) {
        throw_named(env, "java/lang/IllegalStateException", "CursorWindow cannot be written");
        return;
    }
    int32_t status = g_cursor_write((void*)(intptr_t)window, p);
    if (status != 0) throw_named(env, "java/lang/RuntimeException", "Could not write CursorWindow to Parcel");
}

static jlong cursor_window_create(JNIEnv* env, jclass c, jobject parcel)
{
    (void)c;
    const void* p = native_parcel(env, parcel);
    if (p == NULL) return 0;
    void* window = NULL;
    int32_t status = g_cursor_create != NULL ? g_cursor_create(p, &window) : -1;
    if (status != 0 || window == NULL) {
        throw_named(env, "android/database/CursorWindowAllocationException", "Could not create CursorWindow from Parcel");
        return 0;
    }
    return (jlong)(intptr_t)window;
}

/* Over liboh_android_runtime's registrations, when its CursorWindow methods are there to call. */
static void register_cursor_window(JNIEnv* env)
{
    g_cursor_write = (CursorWindowWrite)runtime_symbol("_ZN7android12CursorWindow13writeToParcelEPNS_6ParcelE");
    g_cursor_create = (CursorWindowCreate)runtime_symbol("_ZN7android12CursorWindow16createFromParcelEPNS_6ParcelEPPS0_");
    if (g_cursor_write == NULL || g_cursor_create == NULL) return;
    static const JNINativeMethod methods[] = {
        {"nativeWriteToParcel", "(JLandroid/os/Parcel;)V", (void*)cursor_window_write},
        {"nativeCreateFromParcel", "(Landroid/os/Parcel;)J", (void*)cursor_window_create},
    };
    jclass window = (*env)->FindClass(env, "android/database/CursorWindow");
    if (window == NULL) {
        (*env)->ExceptionClear(env);
        return;
    }
    if ((*env)->RegisterNatives(env, window, methods, 2) != JNI_OK) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, window);
}

#define NATIVE(name, signature) {#name, signature, (void*)Java_adapter_core_SocketBinderNative_##name}

/* Binds onto the boot class from JNI_OnLoad; a framework without the class just skips it. */
int wl_register_socket_binder(JNIEnv* env)
{
    static const JNINativeMethod methods[] = {
        NATIVE(parcelBytes, "(Landroid/os/Parcel;)[B"),
        NATIVE(parcelObjects, "(Landroid/os/Parcel;)[I"),
        NATIVE(socketPair, "()[I"),
        NATIVE(listen, "(Ljava/lang/String;)I"),
        NATIVE(accept, "(I)I"),
        NATIVE(connect, "(Ljava/lang/String;)I"),
        NATIVE(send, "(I[BII[I)I"),
        NATIVE(recv, "(I[BII[I)I"),
        NATIVE(peerPid, "(I)I"),
        NATIVE(installCallingIdentity, "()V"),
        NATIVE(setCallingIdentity, "(II)J"),
        NATIVE(restoreCallingIdentity, "(J)V"),
        NATIVE(shutdown, "(I)V"),
        NATIVE(close, "(I)V"),
    };
    jclass c = (*env)->FindClass(env, "adapter/core/SocketBinderNative");
    if (c == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    int rc = (*env)->RegisterNatives(env, c, methods, sizeof(methods) / sizeof(methods[0]));
    if (rc != 0) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, c);
    register_cursor_window(env);
    return rc == 0;
}
