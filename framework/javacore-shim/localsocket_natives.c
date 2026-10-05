/*
 * android.net.LocalSocketImpl's natives, as frameworks/base/core/jni/android_net_LocalSocketImpl.cpp
 * implements them over libcutils' socket_local_* and android-base's file descriptor passing. The
 * runtime declares them and never registered them: LocalServerSocket's bindLocal threw
 * UnsatisfiedLinkError on a TikTok thread, and CapCut and Element use local sockets too.
 */
#define _GNU_SOURCE   /* struct ucred */
#include <jni.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

/* libcutils' namespaces (cutils/sockets.h). */
enum { NAMESPACE_ABSTRACT = 0, NAMESPACE_RESERVED = 1, NAMESPACE_FILESYSTEM = 2 };
#define RESERVED_SOCKET_PREFIX "/dev/socket/"
#define MAX_RECEIVED_FDS 64

static jfieldID g_inbound;
static jfieldID g_outbound;
static jfieldID g_descriptor;
static jclass g_file_descriptor;
static jmethodID g_file_descriptor_init;
static jclass g_credentials;
static jmethodID g_credentials_init;

static void throw_named(JNIEnv* env, const char* name, const char* message)
{
    jclass c = (*env)->FindClass(env, name);
    if (c != NULL) (*env)->ThrowNew(env, c, message);
}

static void throw_io(JNIEnv* env, int error)
{
    throw_named(env, "java/io/IOException", strerror(error));
}

static int fd_of(JNIEnv* env, jobject descriptor)
{
    return (*env)->GetIntField(env, descriptor, g_descriptor);
}

/* libcutils' socket_make_sockaddr_un. */
static int make_address(const char* name, int namespace_id, struct sockaddr_un* address, socklen_t* length)
{
    size_t name_length = strlen(name);
    memset(address, 0, sizeof(*address));
    switch (namespace_id) {
    case NAMESPACE_ABSTRACT:
        if (name_length + 1 > sizeof(address->sun_path)) return -1;
        address->sun_path[0] = 0;   /* not NUL-terminated (man 7 unix) */
        memcpy(address->sun_path + 1, name, name_length);
        break;
    case NAMESPACE_RESERVED:
        name_length += strlen(RESERVED_SOCKET_PREFIX);
        if (name_length > sizeof(address->sun_path) - 1) return -1;
        strcpy(address->sun_path, RESERVED_SOCKET_PREFIX);
        strcat(address->sun_path, name);
        break;
    case NAMESPACE_FILESYSTEM:
        if (name_length > sizeof(address->sun_path) - 1) return -1;
        strcpy(address->sun_path, name);
        break;
    default:
        return -1;
    }
    address->sun_family = AF_LOCAL;
    *length = (socklen_t) (name_length + offsetof(struct sockaddr_un, sun_path) + 1);
    return 0;
}

static void socket_connect_local(JNIEnv* env, jobject self, jobject descriptor, jstring name, jint namespace_id)
{
    struct sockaddr_un address;
    socklen_t length;
    const char* text;
    int fd, failed;
    (void) self;
    if (name == NULL) {
        throw_named(env, "java/lang/NullPointerException", NULL);
        return;
    }
    fd = fd_of(env, descriptor);
    if ((*env)->ExceptionCheck(env)) return;
    text = (*env)->GetStringUTFChars(env, name, NULL);
    if (text == NULL) return;
    failed = make_address(text, namespace_id, &address, &length) < 0;
    (*env)->ReleaseStringUTFChars(env, name, text);
    if (failed) {
        throw_io(env, EINVAL);
        return;
    }
    if (connect(fd, (struct sockaddr*) &address, length) < 0) throw_io(env, errno);
}

static void socket_bind_local(JNIEnv* env, jobject self, jobject descriptor, jstring name, jint namespace_id)
{
    struct sockaddr_un address;
    socklen_t length;
    const char* text;
    int fd, failed, one = 1;
    (void) self;
    if (name == NULL) {
        throw_named(env, "java/lang/NullPointerException", NULL);
        return;
    }
    fd = fd_of(env, descriptor);
    if ((*env)->ExceptionCheck(env)) return;
    text = (*env)->GetStringUTFChars(env, name, NULL);
    if (text == NULL) return;
    failed = make_address(text, namespace_id, &address, &length) < 0;
    (*env)->ReleaseStringUTFChars(env, name, text);
    if (failed) {
        throw_io(env, EINVAL);
        return;
    }
    if (namespace_id == NAMESPACE_RESERVED || namespace_id == NAMESPACE_FILESYSTEM) {
        unlink(address.sun_path);   /* a stale socket file; ENOENT is fine */
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(fd, (struct sockaddr*) &address, length) < 0) throw_io(env, errno);
}

/*
 * Reads into buffer, handing descriptors that came with the data to the socket's
 * inboundFileDescriptors. The length read, 0 at the end of the stream, -1 with an exception thrown.
 */
static ssize_t read_all(JNIEnv* env, jobject self, int fd, void* buffer, size_t length)
{
    char control[CMSG_SPACE(sizeof(int) * MAX_RECEIVED_FDS)] __attribute__((aligned(8)));
    struct iovec data = { buffer, length };
    struct msghdr message;
    struct cmsghdr* header;
    int received[MAX_RECEIVED_FDS];
    size_t count = 0;
    ssize_t result;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &data;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    do {
        result = recvmsg(fd, &message, MSG_CMSG_CLOEXEC);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        if (errno == EPIPE) return 0;   /* the end of the stream */
        throw_io(env, errno);
        return -1;
    }
    /* musl's CMSG_NXTHDR compares a signed and an unsigned length. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
    for (header = CMSG_FIRSTHDR(&message); header != NULL; header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS) continue;
        size_t n = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        int* fds = (int*) CMSG_DATA(header);
        for (size_t i = 0; i < n; i++) {
            if (count < MAX_RECEIVED_FDS) received[count++] = fds[i];
            else close(fds[i]);
        }
    }
#pragma GCC diagnostic pop
    if (count > 0) {
        jobjectArray array = (*env)->NewObjectArray(env, (jsize) count, g_file_descriptor, NULL);
        for (size_t i = 0; array != NULL && i < count; i++) {
            jobject object = (*env)->NewObject(env, g_file_descriptor, g_file_descriptor_init);
            if (object == NULL) break;
            (*env)->SetIntField(env, object, g_descriptor, received[i]);
            received[i] = -1;   /* the FileDescriptor owns it now */
            (*env)->SetObjectArrayElement(env, array, (jsize) i, object);
            (*env)->DeleteLocalRef(env, object);
        }
        for (size_t i = 0; i < count; i++) {
            if (received[i] >= 0) close(received[i]);
        }
        if ((*env)->ExceptionCheck(env)) return -1;
        (*env)->SetObjectField(env, self, g_inbound, array);
    }
    return result;
}

/* Writes all of buffer, with the socket's outboundFileDescriptors. 0, or -1 with an exception thrown. */
static int write_all(JNIEnv* env, jobject self, int fd, const void* buffer, size_t length)
{
    jobjectArray outbound = (jobjectArray) (*env)->GetObjectField(env, self, g_outbound);
    jsize count = outbound != NULL ? (*env)->GetArrayLength(env, outbound) : 0;
    const unsigned char* bytes = buffer;
    struct iovec data = { (void*) buffer, length };
    struct msghdr message;
    char* control = NULL;
    ssize_t result;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &data;
    message.msg_iovlen = 1;
    if (count > 0) {
        control = calloc(1, CMSG_SPACE(sizeof(int) * (size_t) count));
        if (control == NULL) {
            throw_io(env, ENOMEM);
            return -1;
        }
        message.msg_control = control;
        message.msg_controllen = CMSG_SPACE(sizeof(int) * (size_t) count);
        struct cmsghdr* header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(sizeof(int) * (size_t) count);
        int* fds = (int*) CMSG_DATA(header);
        for (jsize i = 0; i < count; i++) {
            jobject object = (*env)->GetObjectArrayElement(env, outbound, i);
            fds[i] = object != NULL ? fd_of(env, object) : -1;
            if (object != NULL) (*env)->DeleteLocalRef(env, object);
        }
    }
    do {
        result = sendmsg(fd, &message, MSG_NOSIGNAL);
    } while (result < 0 && errno == EINTR);
    free(control);
    while (result >= 0 && (size_t) result != length) {
        bytes += result;
        length -= (size_t) result;
        result = send(fd, bytes, length, MSG_NOSIGNAL);
    }
    if (result < 0) {
        throw_io(env, errno);
        return -1;
    }
    return 0;
}

static jint socket_read(JNIEnv* env, jobject self, jobject descriptor)
{
    unsigned char byte;
    ssize_t result;
    int fd;
    if (descriptor == NULL) {
        throw_named(env, "java/lang/NullPointerException", NULL);
        return -1;
    }
    fd = fd_of(env, descriptor);
    if ((*env)->ExceptionCheck(env)) return 0;
    result = read_all(env, self, fd, &byte, 1);
    if (result < 0) return 0;
    return result == 0 ? -1 : (jint) byte;
}

static jint socket_readba(JNIEnv* env, jobject self, jbyteArray buffer, jint offset, jint length, jobject descriptor)
{
    jbyte* bytes;
    ssize_t result;
    int fd;
    if (descriptor == NULL || buffer == NULL) {
        throw_named(env, "java/lang/NullPointerException", NULL);
        return -1;
    }
    if (offset < 0 || length < 0 || offset + length > (*env)->GetArrayLength(env, buffer)) {
        throw_named(env, "java/lang/ArrayIndexOutOfBoundsException", NULL);
        return -1;
    }
    if (length == 0) return 0;
    fd = fd_of(env, descriptor);
    if ((*env)->ExceptionCheck(env)) return -1;
    bytes = (*env)->GetByteArrayElements(env, buffer, NULL);
    if (bytes == NULL) return -1;
    result = read_all(env, self, fd, bytes + offset, (size_t) length);
    (*env)->ReleaseByteArrayElements(env, buffer, bytes, 0);
    return (jint) (result == 0 ? -1 : result);
}

static void socket_write(JNIEnv* env, jobject self, jint byte, jobject descriptor)
{
    unsigned char value = (unsigned char) byte;
    int fd;
    if (descriptor == NULL) {
        throw_named(env, "java/lang/NullPointerException", NULL);
        return;
    }
    fd = fd_of(env, descriptor);
    if ((*env)->ExceptionCheck(env)) return;
    write_all(env, self, fd, &value, 1);
}

static void socket_writeba(JNIEnv* env, jobject self, jbyteArray buffer, jint offset, jint length, jobject descriptor)
{
    jbyte* bytes;
    int fd;
    if (descriptor == NULL || buffer == NULL) {
        throw_named(env, "java/lang/NullPointerException", NULL);
        return;
    }
    if (offset < 0 || length < 0 || offset + length > (*env)->GetArrayLength(env, buffer)) {
        throw_named(env, "java/lang/ArrayIndexOutOfBoundsException", NULL);
        return;
    }
    fd = fd_of(env, descriptor);
    if ((*env)->ExceptionCheck(env)) return;
    bytes = (*env)->GetByteArrayElements(env, buffer, NULL);
    if (bytes == NULL) return;
    write_all(env, self, fd, bytes + offset, (size_t) length);
    (*env)->ReleaseByteArrayElements(env, buffer, bytes, JNI_ABORT);
}

static jobject socket_get_peer_credentials(JNIEnv* env, jobject self, jobject descriptor)
{
    struct ucred credentials;
    socklen_t size = sizeof(credentials);
    int fd;
    (void) self;
    if (descriptor == NULL) {
        throw_named(env, "java/lang/NullPointerException", NULL);
        return NULL;
    }
    fd = fd_of(env, descriptor);
    if ((*env)->ExceptionCheck(env)) return NULL;
    memset(&credentials, 0, sizeof(credentials));
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) < 0) {
        throw_io(env, errno);
        return NULL;
    }
    if (size == 0) return NULL;
    return (*env)->NewObject(env, g_credentials, g_credentials_init, (jint) credentials.pid,
                             (jint) credentials.uid, (jint) credentials.gid);
}

static const JNINativeMethod kLocalSocketImpl[] = {
    { "connectLocal", "(Ljava/io/FileDescriptor;Ljava/lang/String;I)V", (void*) socket_connect_local },
    { "bindLocal", "(Ljava/io/FileDescriptor;Ljava/lang/String;I)V", (void*) socket_bind_local },
    { "read_native", "(Ljava/io/FileDescriptor;)I", (void*) socket_read },
    { "readba_native", "([BIILjava/io/FileDescriptor;)I", (void*) socket_readba },
    { "writeba_native", "([BIILjava/io/FileDescriptor;)V", (void*) socket_writeba },
    { "write_native", "(ILjava/io/FileDescriptor;)V", (void*) socket_write },
    { "getPeerCredentials_native", "(Ljava/io/FileDescriptor;)Landroid/net/Credentials;",
      (void*) socket_get_peer_credentials },
};

static jclass global_class(JNIEnv* env, const char* name)
{
    jclass local = (*env)->FindClass(env, name);
    jclass global = local != NULL ? (*env)->NewGlobalRef(env, local) : NULL;
    if (local != NULL) (*env)->DeleteLocalRef(env, local);
    return global;
}

/* The classes and fields first, then the natives one by one. Returns how many were bound. */
int wl_register_localsocket(JNIEnv* env)
{
    jclass impl = (*env)->FindClass(env, "android/net/LocalSocketImpl");
    int bound = 0;
    if (impl == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    g_inbound = (*env)->GetFieldID(env, impl, "inboundFileDescriptors", "[Ljava/io/FileDescriptor;");
    g_outbound = g_inbound != NULL
            ? (*env)->GetFieldID(env, impl, "outboundFileDescriptors", "[Ljava/io/FileDescriptor;") : NULL;
    g_file_descriptor = g_outbound != NULL ? global_class(env, "java/io/FileDescriptor") : NULL;
    g_descriptor = g_file_descriptor != NULL ? (*env)->GetFieldID(env, g_file_descriptor, "descriptor", "I") : NULL;
    g_file_descriptor_init = g_descriptor != NULL ? (*env)->GetMethodID(env, g_file_descriptor, "<init>", "()V") : NULL;
    g_credentials = g_file_descriptor_init != NULL ? global_class(env, "android/net/Credentials") : NULL;
    g_credentials_init = g_credentials != NULL ? (*env)->GetMethodID(env, g_credentials, "<init>", "(III)V") : NULL;
    if (g_credentials_init == NULL) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, impl);
        return 0;
    }
    for (int i = 0; i < (int) (sizeof(kLocalSocketImpl) / sizeof(kLocalSocketImpl[0])); i++) {
        if ((*env)->RegisterNatives(env, impl, &kLocalSocketImpl[i], 1) == JNI_OK) bound++;
        else (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, impl);
    return bound;
}
