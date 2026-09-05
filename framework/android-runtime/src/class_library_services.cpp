/*
 * Android/OpenJDK class-library services supplied at the OH boundary.
 *
 * These are ordinary platform natives. Keeping them in the framework bridge
 * lets ART retain stock interpreter, JIT and per-ClassLoader JNI dispatch.
 */

#include <jni.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <malloc.h>
#include <sys/inotify.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace android {
namespace {

void Throw(JNIEnv* env, const char* class_name, const char* message) {
    jclass clazz = env->FindClass(class_name);
    if (clazz != nullptr) {
        env->ThrowNew(clazz, message != nullptr ? message : "native operation failed");
        env->DeleteLocalRef(clazz);
    }
}

int RegisterOptional(JNIEnv* env,
                     jclass clazz,
                     const char* name,
                     const char* signature,
                     void* function) {
    const JNINativeMethod method = {
        const_cast<char*>(name), const_cast<char*>(signature), function};
    const int result = env->RegisterNatives(clazz, &method, 1);
    if (result != JNI_OK && env->ExceptionCheck()) env->ExceptionClear();
    return result == JNI_OK ? 1 : 0;
}

// ---- java.lang.System ------------------------------------------------------

// These methods are @CriticalNative in Android's core-oj. Their implementation
// therefore uses the direct critical-native ABI (no JNIEnv/jclass arguments).
// Register them after the VM is fully started so JIT direct calls never need
// the lazy critical-native dlsym trampoline.
jlong System_nano_time() {
    timespec now{};
    return clock_gettime(CLOCK_MONOTONIC, &now) == 0
            ? static_cast<jlong>(now.tv_sec) * 1000000000LL + now.tv_nsec : 0;
}

jlong System_current_time_millis() {
    timespec now{};
    return clock_gettime(CLOCK_REALTIME, &now) == 0
            ? static_cast<jlong>(now.tv_sec) * 1000LL + now.tv_nsec / 1000000LL : 0;
}

// ---- android.os.FileObserver ------------------------------------------------

jmethodID g_file_observer_on_event;

jint FileObserver_init(JNIEnv*, jobject) {
    return static_cast<jint>(inotify_init1(IN_CLOEXEC));
}

void FileObserver_observe(JNIEnv* env, jobject observer, jint fd) {
    alignas(inotify_event) char buffer[4096];
    for (;;) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (count == 0) return;

        size_t offset = 0;
        while (offset + sizeof(inotify_event) <= static_cast<size_t>(count)) {
            const auto* event = reinterpret_cast<const inotify_event*>(buffer + offset);
            const size_t event_size = sizeof(inotify_event) + event->len;
            if (offset + event_size > static_cast<size_t>(count)) break;
            jstring path = event->len != 0 ? env->NewStringUTF(event->name) : nullptr;
            env->CallVoidMethod(observer,
                                g_file_observer_on_event,
                                static_cast<jint>(event->wd),
                                static_cast<jint>(event->mask),
                                path);
            if (path != nullptr) env->DeleteLocalRef(path);
            if (env->ExceptionCheck()) env->ExceptionClear();
            offset += event_size;
        }
    }
}

void FileObserver_start_watching(JNIEnv* env,
                                 jobject,
                                 jint fd,
                                 jobjectArray paths,
                                 jint mask,
                                 jintArray watch_descriptors) {
    if (fd < 0 || paths == nullptr || watch_descriptors == nullptr) return;
    const jsize count = std::min(env->GetArrayLength(paths),
                                env->GetArrayLength(watch_descriptors));
    std::vector<jint> descriptors(static_cast<size_t>(count), -1);
    for (jsize i = 0; i < count; ++i) {
        auto path = static_cast<jstring>(env->GetObjectArrayElement(paths, i));
        const char* chars = path != nullptr
                ? env->GetStringUTFChars(path, nullptr) : nullptr;
        if (chars != nullptr) {
            descriptors[static_cast<size_t>(i)] = inotify_add_watch(fd, chars, mask);
            env->ReleaseStringUTFChars(path, chars);
        }
        if (path != nullptr) env->DeleteLocalRef(path);
    }
    if (count != 0) {
        env->SetIntArrayRegion(watch_descriptors, 0, count, descriptors.data());
    }
}

void FileObserver_stop_watching(JNIEnv* env, jobject, jint fd, jintArray watches) {
    if (fd < 0 || watches == nullptr) return;
    const jsize count = env->GetArrayLength(watches);
    std::vector<jint> descriptors(static_cast<size_t>(count));
    if (count != 0) env->GetIntArrayRegion(watches, 0, count, descriptors.data());
    for (jint descriptor : descriptors) {
        if (descriptor >= 0) inotify_rm_watch(fd, descriptor);
    }
}

// ---- sun.nio.ch.IOUtil ------------------------------------------------------

jfieldID g_file_descriptor_field;

int FileDescriptorValue(JNIEnv* env, jobject file_descriptor) {
    return file_descriptor != nullptr && g_file_descriptor_field != nullptr
            ? env->GetIntField(file_descriptor, g_file_descriptor_field) : -1;
}

jint IOUtil_iov_max(JNIEnv*, jclass) {
    const long value = sysconf(_SC_IOV_MAX);
    return static_cast<jint>(value > 0 ? std::min<long>(value, INT_MAX) : 16);
}

jint IOUtil_fd_limit(JNIEnv* env, jclass) {
    rlimit limit{};
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        Throw(env, "java/io/IOException", "getrlimit failed");
        return -1;
    }
    return static_cast<jint>(std::min<rlim_t>(limit.rlim_max, INT_MAX));
}

jboolean IOUtil_drain(JNIEnv* env, jclass, jint fd) {
    char buffer[128];
    bool read_any = false;
    for (;;) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            read_any = true;
            if (count == static_cast<ssize_t>(sizeof(buffer))) continue;
        } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            Throw(env, "java/io/IOException", "drain failed");
        }
        return read_any ? JNI_TRUE : JNI_FALSE;
    }
}

int ConfigureBlocking(int fd, bool blocking) {
    const int flags = fcntl(fd, F_GETFL);
    if (flags < 0) return -1;
    const int new_flags = blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK;
    return flags == new_flags ? 0 : fcntl(fd, F_SETFL, new_flags);
}

jlong IOUtil_make_pipe(JNIEnv* env, jobject, jboolean blocking) {
    int fds[2];
    if (pipe(fds) != 0) {
        Throw(env, "java/io/IOException", "pipe failed");
        return 0;
    }
    if (blocking == JNI_FALSE &&
        (ConfigureBlocking(fds[0], false) != 0 ||
         ConfigureBlocking(fds[1], false) != 0)) {
        close(fds[0]);
        close(fds[1]);
        Throw(env, "java/io/IOException", "configure pipe failed");
        return 0;
    }
    return (static_cast<jlong>(fds[0]) << 32) |
           static_cast<uint32_t>(fds[1]);
}

void IOUtil_configure_blocking(JNIEnv* env, jclass, jobject descriptor, jboolean blocking) {
    if (ConfigureBlocking(FileDescriptorValue(env, descriptor), blocking == JNI_TRUE) != 0) {
        Throw(env, "java/io/IOException", "configure blocking failed");
    }
}

void IOUtil_set_fd(JNIEnv* env, jclass, jobject descriptor, jint value) {
    if (descriptor != nullptr && g_file_descriptor_field != nullptr) {
        env->SetIntField(descriptor, g_file_descriptor_field, value);
    }
}

jint IOUtil_get_fd(JNIEnv* env, jclass, jobject descriptor) {
    return FileDescriptorValue(env, descriptor);
}

jboolean IOUtil_random_bytes(JNIEnv* env, jclass, jbyteArray bytes) {
    if (bytes == nullptr) return JNI_FALSE;
    const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return JNI_FALSE;
    const jsize size = env->GetArrayLength(bytes);
    std::vector<jbyte> data(static_cast<size_t>(size));
    size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t count = read(fd, data.data() + offset, data.size() - offset);
        if (count > 0) offset += static_cast<size_t>(count);
        else if (count < 0 && errno == EINTR) continue;
        else break;
    }
    close(fd);
    if (offset != data.size()) return JNI_FALSE;
    if (size != 0) env->SetByteArrayRegion(bytes, 0, size, data.data());
    return env->ExceptionCheck() ? JNI_FALSE : JNI_TRUE;
}

// ---- java.lang.UNIXProcess --------------------------------------------------

std::vector<char> CopyBytes(JNIEnv* env, jbyteArray source) {
    if (source == nullptr) return {};
    const jsize length = env->GetArrayLength(source);
    std::vector<char> result(static_cast<size_t>(length) + 1, '\0');
    if (length != 0) {
        env->GetByteArrayRegion(source, 0, length,
                                reinterpret_cast<jbyte*>(result.data()));
    }
    return result;
}

std::vector<char*> SplitBlock(std::vector<char>* block, int count) {
    std::vector<char*> result;
    if (block == nullptr || block->empty()) return result;
    char* current = block->data();
    char* end = current + block->size();
    for (int i = 0; i < count && current < end; ++i) {
        result.push_back(current);
        current += strnlen(current, static_cast<size_t>(end - current)) + 1;
    }
    return result;
}

jint UNIXProcess_fork_and_exec(JNIEnv* env,
                               jobject,
                               jbyteArray program_bytes,
                               jbyteArray argument_bytes,
                               jint argument_count,
                               jbyteArray environment_bytes,
                               jint environment_count,
                               jbyteArray directory_bytes,
                               jintArray descriptors_array,
                               jboolean redirect_error_stream) {
    auto program = CopyBytes(env, program_bytes);
    auto arguments = CopyBytes(env, argument_bytes);
    auto environment = CopyBytes(env, environment_bytes);
    auto directory = CopyBytes(env, directory_bytes);
    if (program.empty() || descriptors_array == nullptr ||
        env->GetArrayLength(descriptors_array) < 3) {
        Throw(env, "java/io/IOException", "invalid process arguments");
        return -1;
    }

    std::vector<char*> argv;
    argv.push_back(program.data());
    auto argument_list = SplitBlock(&arguments, argument_count);
    argv.insert(argv.end(), argument_list.begin(), argument_list.end());
    argv.push_back(nullptr);
    auto envp = SplitBlock(&environment, environment_count);
    envp.push_back(nullptr);

    jint descriptors[3];
    env->GetIntArrayRegion(descriptors_array, 0, 3, descriptors);
    int child_fds[3] = {descriptors[0], descriptors[1], descriptors[2]};
    int parent_fds[3] = {-1, -1, -1};
    for (int i = 0; i < 3; ++i) {
        if (descriptors[i] != -1) continue;
        int pipe_fds[2];
        if (pipe2(pipe_fds, O_CLOEXEC) != 0) {
            Throw(env, "java/io/IOException", "process pipe failed");
            return -1;
        }
        if (i == 0) {
            child_fds[i] = pipe_fds[0];
            parent_fds[i] = pipe_fds[1];
        } else {
            parent_fds[i] = pipe_fds[0];
            child_fds[i] = pipe_fds[1];
        }
    }

    const pid_t pid = fork();
    if (pid < 0) {
        for (int fd : parent_fds) if (fd >= 0) close(fd);
        for (int i = 0; i < 3; ++i) {
            if (descriptors[i] == -1 && child_fds[i] >= 0) close(child_fds[i]);
        }
        Throw(env, "java/io/IOException", "fork failed");
        return -1;
    }
    if (pid == 0) {
        if (!directory.empty() && directory[0] != '\0') chdir(directory.data());
        for (int i = 0; i < 3; ++i) {
            if (child_fds[i] >= 0 && child_fds[i] != i) dup2(child_fds[i], i);
        }
        if (redirect_error_stream == JNI_TRUE) dup2(STDOUT_FILENO, STDERR_FILENO);
        for (int fd : parent_fds) if (fd >= 0) close(fd);
        for (int i = 0; i < 3; ++i) if (child_fds[i] > STDERR_FILENO) close(child_fds[i]);
        if (environment_count > 0) execve(program.data(), argv.data(), envp.data());
        else execv(program.data(), argv.data());
        _exit(127);
    }

    for (int i = 0; i < 3; ++i) {
        if (descriptors[i] == -1 && child_fds[i] >= 0) close(child_fds[i]);
        descriptors[i] = parent_fds[i];
    }
    env->SetIntArrayRegion(descriptors_array, 0, 3, descriptors);
    return static_cast<jint>(pid);
}

jint UNIXProcess_wait(JNIEnv*, jobject, jint pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return errno == ECHILD ? 0 : -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 0x80 + WTERMSIG(status);
    return status;
}

void UNIXProcess_destroy(JNIEnv*, jclass, jint pid) {
    if (pid > 0) kill(pid, SIGTERM);
}

void UNIXProcess_init_ids(JNIEnv*, jclass) {}

// ---- android.os.Debug -------------------------------------------------------

void Debug_get_memory_info(JNIEnv*, jclass, jobject) {
    // MemoryInfo is zero-initialized by Java. Zero is a valid measurement for
    // every category unavailable from OH's procfs accounting boundary.
}

jboolean Debug_get_memory_info_pid(JNIEnv*, jclass, jint pid, jobject) {
    return pid == getpid() ? JNI_TRUE : JNI_FALSE;
}

jlong Debug_native_heap_size(JNIEnv*, jclass) {
    const struct mallinfo2 info = mallinfo2();
    return static_cast<jlong>(info.arena + info.hblkhd);
}

jlong Debug_native_heap_allocated(JNIEnv*, jclass) {
    return static_cast<jlong>(mallinfo2().uordblks);
}

jlong Debug_native_heap_free(JNIEnv*, jclass) {
    return static_cast<jlong>(mallinfo2().fordblks);
}

// ---- sun.misc.Unsafe -------------------------------------------------------

jobject Unsafe_allocate_instance(JNIEnv* env, jobject, jclass clazz) {
    if (clazz == nullptr) {
        Throw(env, "java/lang/NullPointerException", "clazz == null");
        return nullptr;
    }
    return env->AllocObject(clazz);
}

}  // namespace

int register_westlake_class_library_services(JNIEnv* env) {
    int required_failures = 0;

    jclass system = env->FindClass("java/lang/System");
    if (system != nullptr) {
        int count = 0;
        count += RegisterOptional(env, system, "nanoTime", "()J",
                                  reinterpret_cast<void*>(System_nano_time));
        count += RegisterOptional(env, system, "currentTimeMillis", "()J",
                                  reinterpret_cast<void*>(System_current_time_millis));
        if (count != 2) ++required_failures;
        env->DeleteLocalRef(system);
    } else {
        ++required_failures;
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    jclass observer = env->FindClass("android/os/FileObserver$ObserverThread");
    if (observer != nullptr) {
        g_file_observer_on_event = env->GetMethodID(
                observer, "onEvent", "(IILjava/lang/String;)V");
        const JNINativeMethod methods[] = {
            {const_cast<char*>("init"), const_cast<char*>("()I"),
             reinterpret_cast<void*>(FileObserver_init)},
            {const_cast<char*>("observe"), const_cast<char*>("(I)V"),
             reinterpret_cast<void*>(FileObserver_observe)},
            {const_cast<char*>("startWatching"),
             const_cast<char*>("(I[Ljava/lang/String;I[I)V"),
             reinterpret_cast<void*>(FileObserver_start_watching)},
            {const_cast<char*>("stopWatching"), const_cast<char*>("(I[I)V"),
             reinterpret_cast<void*>(FileObserver_stop_watching)},
        };
        if (g_file_observer_on_event == nullptr ||
            env->RegisterNatives(observer, methods, 4) != JNI_OK) {
            ++required_failures;
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        env->DeleteLocalRef(observer);
    } else {
        ++required_failures;
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    jclass descriptor = env->FindClass("java/io/FileDescriptor");
    if (descriptor != nullptr) {
        g_file_descriptor_field = env->GetFieldID(descriptor, "descriptor", "I");
        if (g_file_descriptor_field == nullptr && env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(descriptor);
    }
    jclass io_util = env->FindClass("sun/nio/ch/IOUtil");
    if (io_util != nullptr && g_file_descriptor_field != nullptr) {
        int count = 0;
        count += RegisterOptional(env, io_util, "iovMax", "()I",
                                  reinterpret_cast<void*>(IOUtil_iov_max));
        count += RegisterOptional(env, io_util, "fdLimit", "()I",
                                  reinterpret_cast<void*>(IOUtil_fd_limit));
        count += RegisterOptional(env, io_util, "drain", "(I)Z",
                                  reinterpret_cast<void*>(IOUtil_drain));
        count += RegisterOptional(env, io_util, "makePipe", "(Z)J",
                                  reinterpret_cast<void*>(IOUtil_make_pipe));
        count += RegisterOptional(env, io_util, "configureBlocking",
                                  "(Ljava/io/FileDescriptor;Z)V",
                                  reinterpret_cast<void*>(IOUtil_configure_blocking));
        count += RegisterOptional(env, io_util, "setfdVal",
                                  "(Ljava/io/FileDescriptor;I)V",
                                  reinterpret_cast<void*>(IOUtil_set_fd));
        count += RegisterOptional(env, io_util, "fdVal",
                                  "(Ljava/io/FileDescriptor;)I",
                                  reinterpret_cast<void*>(IOUtil_get_fd));
        count += RegisterOptional(env, io_util, "randomBytes", "([B)Z",
                                  reinterpret_cast<void*>(IOUtil_random_bytes));
        if (count == 0) ++required_failures;
        env->DeleteLocalRef(io_util);
    } else {
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    jclass unix_process = env->FindClass("java/lang/UNIXProcess");
    if (unix_process != nullptr) {
        const JNINativeMethod methods[] = {
            {const_cast<char*>("destroyProcess"), const_cast<char*>("(I)V"),
             reinterpret_cast<void*>(UNIXProcess_destroy)},
            {const_cast<char*>("forkAndExec"),
             const_cast<char*>("([B[BI[BI[B[IZ)I"),
             reinterpret_cast<void*>(UNIXProcess_fork_and_exec)},
            {const_cast<char*>("waitForProcessExit"), const_cast<char*>("(I)I"),
             reinterpret_cast<void*>(UNIXProcess_wait)},
            {const_cast<char*>("initIDs"), const_cast<char*>("()V"),
             reinterpret_cast<void*>(UNIXProcess_init_ids)},
        };
        if (env->RegisterNatives(unix_process, methods, 4) != JNI_OK) {
            ++required_failures;
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        env->DeleteLocalRef(unix_process);
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    jclass debug = env->FindClass("android/os/Debug");
    if (debug != nullptr) {
        RegisterOptional(env, debug, "getMemoryInfo", "(Landroid/os/Debug$MemoryInfo;)V",
                         reinterpret_cast<void*>(Debug_get_memory_info));
        RegisterOptional(env, debug, "getMemoryInfo", "(ILandroid/os/Debug$MemoryInfo;)Z",
                         reinterpret_cast<void*>(Debug_get_memory_info_pid));
        RegisterOptional(env, debug, "getNativeHeapSize", "()J",
                         reinterpret_cast<void*>(Debug_native_heap_size));
        RegisterOptional(env, debug, "getNativeHeapAllocatedSize", "()J",
                         reinterpret_cast<void*>(Debug_native_heap_allocated));
        RegisterOptional(env, debug, "getNativeHeapFreeSize", "()J",
                         reinterpret_cast<void*>(Debug_native_heap_free));
        env->DeleteLocalRef(debug);
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    jclass unsafe = env->FindClass("sun/misc/Unsafe");
    if (unsafe != nullptr) {
        RegisterOptional(env, unsafe, "allocateInstance",
                         "(Ljava/lang/Class;)Ljava/lang/Object;",
                         reinterpret_cast<void*>(Unsafe_allocate_instance));
        env->DeleteLocalRef(unsafe);
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    return required_failures == 0 ? 0 : -1;
}

}  // namespace android
