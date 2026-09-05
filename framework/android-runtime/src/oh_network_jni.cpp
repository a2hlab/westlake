/* Missing libcore datagram operations at the OHOS POSIX boundary.
 * No ART internals, Bionic structures, or application-specific behavior.
 * This extension registers only natives absent from the pinned runtime.
 */
#include <jni.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include <cstddef>
#include <cstdio>
#include <vector>
#include <memory>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include "oh_async_close.h"

namespace {
jclass bytesClass, inetClass, inet6Class, errorClass;
jfieldID fdField, holderField, addressField, portField, hostnameField;
jmethodID getAddress, scopeId, byAddress, byAddress6, errorCtor;
jclass unixClass, socketErrorClass;
jfieldID unixPath;
jfieldID secondsField, microsField, groupIndexField, groupAddressField;
jfieldID pollFdField, pollEventsField, pollReventsField;
using oh_network::CloseMonitor;
bool initialized = false;

void error(JNIEnv* env, const char* operation, int number) {
    if (env->ExceptionCheck()) return;
    jstring name = env->NewStringUTF(operation);
    if (!name) return;
    jobject failure = env->NewObject(errorClass, errorCtor, name, number);
    if (failure) { env->Throw(static_cast<jthrowable>(failure)); env->DeleteLocalRef(failure); }
    env->DeleteLocalRef(name);
}

int descriptor(JNIEnv* env, jobject fd) {
    if (!fd) { error(env, "socket", EBADF); return -1; }
    return env->GetIntField(fd, fdField);
}

bool closed(JNIEnv* env, jobject file, int fd, const CloseMonitor& monitor) {
    if (monitor.signalled() || descriptor(env, file) != fd || fd < 0) {
        if (!env->ExceptionCheck()) env->ThrowNew(socketErrorClass, "Socket closed");
        return true;
    }
    return env->ExceptionCheck();
}

bool unixAddress(JNIEnv* env, jobject value, sockaddr_un& out, socklen_t& length) {
    if (!value || !env->IsInstanceOf(value, unixClass)) {
        error(env, "sockaddr", EAFNOSUPPORT); return false;
    }
    auto path = static_cast<jbyteArray>(env->GetObjectField(value, unixPath));
    if (!path) { error(env, "sockaddr", EINVAL); return false; }
    const jsize count = env->GetArrayLength(path);
    if (count < 0 || static_cast<size_t>(count) > sizeof(out.sun_path)) {
        env->DeleteLocalRef(path); error(env, "sockaddr", ENAMETOOLONG); return false;
    }
    out.sun_family = AF_UNIX;
    env->GetByteArrayRegion(path, 0, count, reinterpret_cast<jbyte*>(out.sun_path));
    env->DeleteLocalRef(path);
    length = offsetof(sockaddr_un, sun_path) + count; // Abstract names include embedded NULs.
    return !env->ExceptionCheck();
}

void bindUnix(JNIEnv* env, jobject, jobject file, jobject target) {
    sockaddr_un addr{}; socklen_t length;
    int fd = descriptor(env, file);
    if (env->ExceptionCheck() || !unixAddress(env, target, addr, length)) return;
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), length)) error(env, "bind", errno);
}

bool address(JNIEnv* env, int fd, jobject value, int port, sockaddr_storage& out, socklen_t& length, bool verbatim = false) {
    if (!value || port < 0 || port > 65535) { error(env, "sockaddr", EINVAL); return false; }
    auto array = static_cast<jbyteArray>(env->CallObjectMethod(value, getAddress));
    if (!array || env->ExceptionCheck()) return false;
    const int n = env->GetArrayLength(array);
    unsigned char raw[16]{};
    if (n != 4 && n != 16) { env->DeleteLocalRef(array); error(env, "sockaddr", EINVAL); return false; }
    env->GetByteArrayRegion(array, 0, n, reinterpret_cast<jbyte*>(raw));
    env->DeleteLocalRef(array);
    if (env->ExceptionCheck()) return false;
    int domain = 0;
    socklen_t size = sizeof(domain);
    if (verbatim) domain = n == 4 ? AF_INET : AF_INET6;
    else if (getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &domain, &size)) {
        error(env, "getsockopt", errno); return false;
    }
    memset(&out, 0, sizeof(out));
    if (domain == AF_INET && n == 4) {
        auto* a = reinterpret_cast<sockaddr_in*>(&out);
        a->sin_family = AF_INET; a->sin_port = htons(port);
        memcpy(&a->sin_addr, raw, 4); length = sizeof(*a);
    } else if (domain == AF_INET6) {
        auto* a = reinterpret_cast<sockaddr_in6*>(&out);
        a->sin6_family = AF_INET6; a->sin6_port = htons(port);
        if (n == 4) {
            // IPv4 ANY maps to IPv6 ANY when binding a dual-stack socket.
            if (raw[0] || raw[1] || raw[2] || raw[3]) {
                a->sin6_addr.s6_addr[10] = a->sin6_addr.s6_addr[11] = 0xff;
                memcpy(a->sin6_addr.s6_addr + 12, raw, 4);
            }
        } else {
            memcpy(&a->sin6_addr, raw, 16);
            a->sin6_scope_id = env->CallIntMethod(value, scopeId);
        }
        length = sizeof(*a);
    } else { error(env, "sockaddr", EAFNOSUPPORT); return false; }
    return !env->ExceptionCheck();
}

void setTimeval(JNIEnv* env, jobject, jobject file, jint level, jint option, jobject time) {
    if (!time) { error(env, "setsockopt", EINVAL); return; }
    timeval value{};
    value.tv_sec = env->GetLongField(time, secondsField);
    value.tv_usec = env->GetLongField(time, microsField);
    const int fd = descriptor(env, file);
    if (env->ExceptionCheck()) return;
    if (setsockopt(fd, level, option, &value, sizeof(value))) error(env, "setsockopt", errno);
}

void setInterface(JNIEnv* env, jobject, jobject file, jint level, jint option, jint index) {
    ip_mreqn value{}; value.imr_ifindex = index;
    const int fd = descriptor(env, file);
    if (!env->ExceptionCheck() && setsockopt(fd, level, option, &value, sizeof(value))) {
        int savedError = errno;
        fprintf(stderr, "[OH_NETWORK] multicast interface level=%d option=%d index=%d errno=%d\n", level, option, index, savedError);
        error(env, "setsockopt", savedError);
    }
}

void setByte(JNIEnv* env, jobject, jobject file, jint level, jint option, jint byteValue) {
    const unsigned char value = static_cast<unsigned char>(byteValue);
    const int fd = descriptor(env, file);
    if (!env->ExceptionCheck() && setsockopt(fd, level, option, &value, sizeof(value))) error(env, "setsockopt", errno);
}

void setGroup(JNIEnv* env, jobject, jobject file, jint level, jint option, jobject group) {
    if (!group) { error(env, "setsockopt", EINVAL); return; }
    group_req value{};
    value.gr_interface = env->GetIntField(group, groupIndexField);
    jobject host = env->GetObjectField(group, groupAddressField);
    socklen_t length = 0;
    const int fd = descriptor(env, file);
    bool ok = !env->ExceptionCheck() && address(env, fd, host, 0, value.gr_group, length, true);
    if (host) env->DeleteLocalRef(host);
    if (ok && setsockopt(fd, level, option, &value, sizeof(value))) error(env, "setsockopt", errno);
}

jint pollSockets(JNIEnv* env, jobject, jobjectArray values, jint timeout) {
    if (!values) { error(env, "poll", EINVAL); return -1; }
    const jsize count = env->GetArrayLength(values);
    if (count > 65536) { error(env, "poll", EINVAL); return -1; }
    std::vector<pollfd> descriptors(count);
    std::vector<std::unique_ptr<CloseMonitor>> monitors;
    monitors.reserve(count);
    for (jsize i = 0; i < count; ++i) {
        jobject value = env->GetObjectArrayElement(values, i);
        jobject file = value ? env->GetObjectField(value, pollFdField) : nullptr;
        descriptors[i].fd = file ? descriptor(env, file) : -1;
        // IoBridge.release$ invalidates the Java descriptor before signalling.
        // A reader arriving after that signal must fail immediately; treating
        // the closed object as an ignored POSIX -1 slot loses the close and
        // waits the entire receive timeout on an empty poll set.
        if (file && descriptors[i].fd < 0) {
            env->DeleteLocalRef(file); env->DeleteLocalRef(value);
            error(env, "poll", EBADF); return -1;
        }
        descriptors[i].events = value ? env->GetShortField(value, pollEventsField) : 0;
        if (value) env->SetShortField(value, pollReventsField, 0);
        if (descriptors[i].fd >= 0) {
            monitors.emplace_back(new CloseMonitor(descriptors[i].fd));
            if (descriptor(env, file) != descriptors[i].fd || monitors.back()->signalled()) {
                env->DeleteLocalRef(file);env->DeleteLocalRef(value);error(env, "poll", EBADF);return -1;
            }
        }
        if (file) env->DeleteLocalRef(file);
        if (value) env->DeleteLocalRef(value);
        if (env->ExceptionCheck()) return -1;
    }
    // Return EINTR instead of retrying and restarting the timeout. IoBridge
    // translates it into SocketException for an asynchronously closed socket.
    int result = CloseMonitor::pollInterruptibly(descriptors.data(), count, timeout);
    int savedError = errno;
    for (const auto& monitor : monitors) if (monitor->signalled()) { error(env, "poll", EBADF); return -1; }
    if (result < 0) { error(env, "poll", savedError); return -1; }
    for (jsize i = 0; i < count; ++i) {
        jobject value = env->GetObjectArrayElement(values, i);
        if (value) { env->SetShortField(value, pollReventsField, descriptors[i].revents);env->DeleteLocalRef(value); }
    }
    return env->ExceptionCheck() ? -1 : result;
}

void bindSocket(JNIEnv* env, jobject, jobject file, jobject host, jint port) {
    const int fd = descriptor(env, file);
    sockaddr_storage addr{}; socklen_t length = 0;
    if (env->ExceptionCheck() || !address(env, fd, host, port, addr, length)) return;
    int result;
    do { result = bind(fd, reinterpret_cast<sockaddr*>(&addr), length); } while (result < 0 && errno == EINTR);
    if (result < 0) error(env, "bind", errno);
}

struct Buffer {
    JNIEnv* env; jobject object; jint offset; jint count; bool array; void* data = nullptr;
    Buffer(JNIEnv* e, jobject o, jint start, jint n, bool sending)
        : env(e), object(o), offset(start), count(n), array(o && e->IsInstanceOf(o, bytesClass)) {
        jlong capacity = !o ? -1 : array ? e->GetArrayLength(static_cast<jbyteArray>(o))
                                       : e->GetDirectBufferCapacity(o);
        if (start < 0 || n < 0 || capacity < start || capacity - start < n) {
            error(e, "buffer", EINVAL); return;
        }
        if (array) {
            data = malloc(n ? n : 1);
            if (!data) { error(e, "buffer", ENOMEM); return; }
            if (sending && n) e->GetByteArrayRegion(static_cast<jbyteArray>(o), start, n, static_cast<jbyte*>(data));
        } else {
            void* base = e->GetDirectBufferAddress(o);
            if (base) data = static_cast<char*>(base) + start;
            else error(e, "buffer", EINVAL);
        }
    }
    void received(int n) {
        if (array && n > 0) env->SetByteArrayRegion(static_cast<jbyteArray>(object), offset, n, static_cast<jbyte*>(data));
    }
    ~Buffer() { if (array) free(data); }
};

jint sendDatagram(JNIEnv* env, jobject, jobject file, jobject object, jint offset, jint count,
          jint flags, jobject host, jint port) {
    const int fd = descriptor(env, file);
    if (env->ExceptionCheck()) return -1;
    sockaddr_storage addr{}; socklen_t length = 0;
    if (host && !address(env, fd, host, port, addr, length)) return -1;
    Buffer buffer(env, object, offset, count, true);
    if (!buffer.data || env->ExceptionCheck()) return -1;
    CloseMonitor monitor(fd);
    if (closed(env, file, fd, monitor)) return -1;
    ssize_t result;
    do { result = sendto(fd, buffer.data, count, flags | MSG_NOSIGNAL,
                        host ? reinterpret_cast<sockaddr*>(&addr) : nullptr, length); }
    while (result < 0 && errno == EINTR && !monitor.signalled());
    int savedError = errno;
    if (closed(env, file, fd, monitor)) return -1;
    if (result < 0) error(env, "sendto", savedError);
    return result;
}

jint receive(JNIEnv* env, jobject, jobject file, jobject object, jint offset, jint count,
             jint flags, jobject source) {
    const int fd = descriptor(env, file);
    if (env->ExceptionCheck()) return -1;
    Buffer buffer(env, object, offset, count, false);
    if (!buffer.data || env->ExceptionCheck()) return -1;
    CloseMonitor monitor(fd);
    if (closed(env, file, fd, monitor)) return -1;
    sockaddr_storage addr{}; socklen_t length = sizeof(addr);
    ssize_t result;
    do { result = recvfrom(fd, buffer.data, count, flags, reinterpret_cast<sockaddr*>(&addr), &length); }
    while (result < 0 && errno == EINTR && !monitor.signalled());
    int savedError = errno;
    if (closed(env, file, fd, monitor)) return -1;
    if (result < 0) { error(env, "recvfrom", savedError); return -1; }
    // MSG_TRUNC can report wire length greater than the supplied buffer.
    buffer.received(result > count ? count : result);
    if (source && !env->ExceptionCheck()) {
        const bool ipv6 = addr.ss_family == AF_INET6;
        if (!ipv6 && addr.ss_family != AF_INET) { error(env, "recvfrom", EAFNOSUPPORT); return -1; }
        auto* a4 = reinterpret_cast<sockaddr_in*>(&addr);
        auto* a6 = reinterpret_cast<sockaddr_in6*>(&addr);
        jbyteArray raw = env->NewByteArray(ipv6 ? 16 : 4);
        if (!raw) return -1;
        env->SetByteArrayRegion(raw, 0, ipv6 ? 16 : 4,
            reinterpret_cast<jbyte*>(ipv6 ? static_cast<void*>(&a6->sin6_addr) : static_cast<void*>(&a4->sin_addr)));
        jobject host = ipv6 && a6->sin6_scope_id
            ? env->CallStaticObjectMethod(inet6Class, byAddress6, nullptr, raw, a6->sin6_scope_id)
            : env->CallStaticObjectMethod(inetClass, byAddress, raw);
        jobject holder = env->GetObjectField(source, holderField);
        if (holder && host && !env->ExceptionCheck()) {
            env->SetObjectField(holder, addressField, host);
            env->SetObjectField(holder, hostnameField, nullptr);
            env->SetIntField(holder, portField, ntohs(ipv6 ? a6->sin6_port : a4->sin_port));
        }
        if (holder) env->DeleteLocalRef(holder);
        if (host) env->DeleteLocalRef(host);
        env->DeleteLocalRef(raw);
    }
    return env->ExceptionCheck() ? -1 : result;
}

jint sendUnix(JNIEnv* env, jobject, jobject file, jobject object, jint offset,
              jint count, jint flags, jobject target) {
    int fd = descriptor(env, file);
    sockaddr_un addr{}; socklen_t length = 0;
    if (env->ExceptionCheck() || (target && !unixAddress(env, target, addr, length))) return -1;
    Buffer buffer(env, object, offset, count, true);
    if (!buffer.data || env->ExceptionCheck()) return -1;
    CloseMonitor monitor(fd);
    if (closed(env, file, fd, monitor)) return -1;
    ssize_t result;
    do { result = sendto(fd, buffer.data, count, flags | MSG_NOSIGNAL,
            target ? reinterpret_cast<sockaddr*>(&addr) : nullptr, length); }
    while (result < 0 && errno == EINTR && !monitor.signalled());
    int savedError = errno;
    if (closed(env, file, fd, monitor)) return -1;
    if (result < 0) error(env, "sendto", savedError);
    return result;
}

void signalClosed(JNIEnv* env, jclass, jobject file) {
    int fd = descriptor(env, file);
    if (fd >= 0 && !env->ExceptionCheck()) CloseMonitor::signal(fd);
}

// libcore's direct-buffer read/write paths use these same Object natives.
// The original OH stubs unconditionally cast Object to byte[], corrupting
// direct buffers and pinning Java arrays across blocking calls.
jint stream(JNIEnv* env, jobject file, jobject object, jint offset, jint count, bool sending) {
    const int fd = descriptor(env, file);
    if (env->ExceptionCheck()) return -1;
    Buffer buffer(env, object, offset, count, sending);
    if (!buffer.data || env->ExceptionCheck()) return -1;
    CloseMonitor monitor(fd);
    if (closed(env, file, fd, monitor)) return -1;
    ssize_t result;
    do { result = sending ? write(fd, buffer.data, count) : read(fd, buffer.data, count); }
    while (result < 0 && errno == EINTR && !monitor.signalled());
    int savedError = errno;
    if (closed(env, file, fd, monitor)) return -1;
    if (result < 0) error(env, sending ? "write" : "read", savedError);
    else if (!sending) buffer.received(result);
    return env->ExceptionCheck() ? -1 : result;
}
jint readBytes(JNIEnv* e, jobject, jobject fd, jobject b, jint off, jint n) { return stream(e, fd, b, off, n, false); }
jint writeBytes(JNIEnv* e, jobject, jobject fd, jobject b, jint off, jint n) { return stream(e, fd, b, off, n, true); }

jclass global(JNIEnv* env, const char* name) {
    jclass local = env->FindClass(name);
    if (!local) return nullptr;
    auto result = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local); return result;
}
}

extern "C" int westlake_network_child_register(JNIEnv* env);

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    // Complete the platform constant omitted by the partial libcore native
    // initializer, before UnixSocketAddress caches it in its own initializer.
    jclass constants = env->FindClass("android/system/OsConstants");
    if (!constants) return JNI_ERR;
    jfieldID pathMax = env->GetStaticFieldID(constants, "UNIX_PATH_MAX", "I");
    if (!pathMax) return JNI_ERR;
    sockaddr_un unixLayout{};
    env->SetStaticIntField(constants, pathMax, sizeof(unixLayout.sun_path));
    // Linux/OHOS UAPI constants omitted by the same partial initializer.
    // Values come from the target sysroot, never Android numeric assumptions.
    struct Constant { const char* name; int value; } missing[] = {
        {"IP_MULTICAST_IF", IP_MULTICAST_IF}, {"IP_MULTICAST_TTL", IP_MULTICAST_TTL},
        {"IP_MULTICAST_LOOP", IP_MULTICAST_LOOP}, {"IPV6_MULTICAST_IF", IPV6_MULTICAST_IF},
        {"IPV6_MULTICAST_HOPS", IPV6_MULTICAST_HOPS}, {"IPV6_MULTICAST_LOOP", IPV6_MULTICAST_LOOP},
        {"MCAST_JOIN_GROUP", MCAST_JOIN_GROUP}, {"MCAST_LEAVE_GROUP", MCAST_LEAVE_GROUP},
        {"MSG_TRUNC", MSG_TRUNC}, {"MSG_PEEK", MSG_PEEK},
        {"ENOPROTOOPT", ENOPROTOOPT}, {"ECONNREFUSED", ECONNREFUSED},
        {"ENETUNREACH", ENETUNREACH}, {"EHOSTUNREACH", EHOSTUNREACH}
    };
    for (const auto& item : missing) {
        jfieldID field = env->GetStaticFieldID(constants, item.name, "I");
        if (!field) { fprintf(stderr, "[OH_NETWORK] missing constant field %s\n", item.name); return JNI_ERR; }
        env->SetStaticIntField(constants, field, item.value);
    }
    env->DeleteLocalRef(constants);
    bytesClass = global(env, "[B"); inetClass = global(env, "java/net/InetAddress");
    inet6Class = global(env, "java/net/Inet6Address"); errorClass = global(env, "android/system/ErrnoException");
    unixClass = global(env, "android/system/UnixSocketAddress");
    socketErrorClass = global(env, "java/net/SocketException");
    if (!bytesClass || !inetClass || !inet6Class || !errorClass || !unixClass || !socketErrorClass) return JNI_ERR;
    unixPath = env->GetFieldID(unixClass, "sun_path", "[B");
    jclass timevalClass = env->FindClass("android/system/StructTimeval");
    jclass groupClass = env->FindClass("android/system/StructGroupReq");
    jclass pollClass = env->FindClass("android/system/StructPollfd");
    if (!timevalClass || !groupClass || !pollClass) return JNI_ERR;
    secondsField = env->GetFieldID(timevalClass, "tv_sec", "J");
    microsField = env->GetFieldID(timevalClass, "tv_usec", "J");
    groupIndexField = env->GetFieldID(groupClass, "gr_interface", "I");
    groupAddressField = env->GetFieldID(groupClass, "gr_group", "Ljava/net/InetAddress;");
    pollFdField = env->GetFieldID(pollClass, "fd", "Ljava/io/FileDescriptor;");
    pollEventsField = env->GetFieldID(pollClass, "events", "S");
    pollReventsField = env->GetFieldID(pollClass, "revents", "S");
    jclass fdClass = env->FindClass("java/io/FileDescriptor");
    jclass socketClass = env->FindClass("java/net/InetSocketAddress");
    jclass holderClass = env->FindClass("java/net/InetSocketAddress$InetSocketAddressHolder");
    if (!fdClass || !socketClass || !holderClass) return JNI_ERR;
    fdField = env->GetFieldID(fdClass, "descriptor", "I");
    holderField = env->GetFieldID(socketClass, "holder", "Ljava/net/InetSocketAddress$InetSocketAddressHolder;");
    addressField = env->GetFieldID(holderClass, "addr", "Ljava/net/InetAddress;");
    hostnameField = env->GetFieldID(holderClass, "hostname", "Ljava/lang/String;");
    portField = env->GetFieldID(holderClass, "port", "I");
    getAddress = env->GetMethodID(inetClass, "getAddress", "()[B");
    scopeId = env->GetMethodID(inet6Class, "getScopeId", "()I");
    byAddress = env->GetStaticMethodID(inetClass, "getByAddress", "([B)Ljava/net/InetAddress;");
    byAddress6 = env->GetStaticMethodID(inet6Class, "getByAddress", "(Ljava/lang/String;[BI)Ljava/net/Inet6Address;");
    errorCtor = env->GetMethodID(errorClass, "<init>", "(Ljava/lang/String;I)V");
    if (env->ExceptionCheck()) return JNI_ERR;
    initialized = true;
    return westlake_network_child_register(env) ? JNI_VERSION_1_6 : JNI_ERR;
}

// AndroidRuntime installs a second libcore native table in each child. The
// startup boundary invokes this AFTER that registration and BEFORE app code;
// prefork-only registration would silently restore the old poll/read stubs.
extern "C" int westlake_network_child_register(JNIEnv* env) {
    if (!initialized || !fdField || !bytesClass || env->ExceptionCheck()) {
        fprintf(stderr, "[OH_NETWORK] refusing registration before successful JNI initialization\n");
        return 0;
    }
    if (!CloseMonitor::initialize()) {
        fprintf(stderr, "[OH_NETWORK] close monitor initialization failed: %d\n", errno);
        return 0;
    }
    jclass monitorClass = env->FindClass("libcore/io/AsynchronousCloseMonitor");
    JNINativeMethod monitorMethods[] = {
        {const_cast<char*>("signalBlockedThreads"), const_cast<char*>("(Ljava/io/FileDescriptor;)V"), reinterpret_cast<void*>(signalClosed)},
    };
    if (!monitorClass || env->RegisterNatives(monitorClass, monitorMethods, 1) != JNI_OK) return 0;
    jclass linuxClass = env->FindClass("libcore/io/Linux");
    JNINativeMethod methods[] = {
        {const_cast<char*>("bind"), const_cast<char*>("(Ljava/io/FileDescriptor;Ljava/net/InetAddress;I)V"), reinterpret_cast<void*>(bindSocket)},
        {const_cast<char*>("sendtoBytes"), const_cast<char*>("(Ljava/io/FileDescriptor;Ljava/lang/Object;IIILjava/net/InetAddress;I)I"), reinterpret_cast<void*>(sendDatagram)},
        {const_cast<char*>("recvfromBytes"), const_cast<char*>("(Ljava/io/FileDescriptor;Ljava/lang/Object;IIILjava/net/InetSocketAddress;)I"), reinterpret_cast<void*>(receive)},
        {const_cast<char*>("bind"), const_cast<char*>("(Ljava/io/FileDescriptor;Ljava/net/SocketAddress;)V"), reinterpret_cast<void*>(bindUnix)},
        {const_cast<char*>("sendtoBytes"), const_cast<char*>("(Ljava/io/FileDescriptor;Ljava/lang/Object;IIILjava/net/SocketAddress;)I"), reinterpret_cast<void*>(sendUnix)},
        {const_cast<char*>("readBytes"), const_cast<char*>("(Ljava/io/FileDescriptor;Ljava/lang/Object;II)I"), reinterpret_cast<void*>(readBytes)},
        {const_cast<char*>("writeBytes"), const_cast<char*>("(Ljava/io/FileDescriptor;Ljava/lang/Object;II)I"), reinterpret_cast<void*>(writeBytes)},
        {const_cast<char*>("setsockoptTimeval"), const_cast<char*>("(Ljava/io/FileDescriptor;IILandroid/system/StructTimeval;)V"), reinterpret_cast<void*>(setTimeval)},
        {const_cast<char*>("setsockoptIpMreqn"), const_cast<char*>("(Ljava/io/FileDescriptor;III)V"), reinterpret_cast<void*>(setInterface)},
        {const_cast<char*>("setsockoptByte"), const_cast<char*>("(Ljava/io/FileDescriptor;III)V"), reinterpret_cast<void*>(setByte)},
        {const_cast<char*>("setsockoptGroupReq"), const_cast<char*>("(Ljava/io/FileDescriptor;IILandroid/system/StructGroupReq;)V"), reinterpret_cast<void*>(setGroup)},
        {const_cast<char*>("poll"), const_cast<char*>("([Landroid/system/StructPollfd;I)I"), reinterpret_cast<void*>(pollSockets)},
    };
    int ok = linuxClass && env->RegisterNatives(linuxClass, methods, sizeof(methods)/sizeof(methods[0])) == JNI_OK;
    if (monitorClass) env->DeleteLocalRef(monitorClass);
    if (linuxClass) env->DeleteLocalRef(linuxClass);
    fprintf(stderr, "[OH_NETWORK] boundary native registration=%d pid=%d\n", ok, getpid());
    return ok;
}
