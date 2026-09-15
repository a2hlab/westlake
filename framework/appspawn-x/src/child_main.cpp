/**
 * appspawn-x child process implementation.
 *
 * Handles the complete child process lifecycle after fork():
 * 1. Apply OH DAC credentials (UID/GID/groups)
 * 2. Set up filesystem sandbox (mount namespace, bind mounts)
 * 3. Set SELinux security context
 * 4. Configure OH AccessToken for permission enforcement
 * 5. Initialize the Android-OH adapter layer
 * 6. Launch ActivityThread.main() to start the Android app
 */

#include "child_main.h"
#include "appspawnx_runtime.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <dlfcn.h>
#include <signal.h>
#include <sched.h>
#include <ucontext.h>
#include <unistd.h>
#include <thread>
#include <sys/uio.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/capability.h>
#include <unistd.h>
#include <algorithm>

// B.30 (2026-04-29): real adaptation for SELinux + AccessToken so child secon
// transitions out of u:r:appspawn:s0 (which can't query SA 180/501/4607).
// hap_restorecon: HapContext::HapDomainSetcontext routes APL → setcon.
// token_setproc: SetSelfTokenID applies the OH access token from TLV.
#include "hap_restorecon.h"          // selinux_adapter:libhap_restorecon
#include "token_setproc.h"           // access_token:libtokensetproc_shared

namespace appspawnx {

// ART's profiled-JIT loader derives its input as "<dex-location>.prof". Android's
// package manager normally prepares that installer-owned path. AppSpawnX launches
// unchanged APKs directly, while VMRuntime correctly writes the current profile to
// the app-owned code_cache directory. Create only the missing compatibility link,
// before dropping root, so ART sees the normal per-dex profile without granting the
// app write access to its staged APK directory.
static bool wl_prepare_profiled_jit_link(const SpawnMsg& msg) {
    const char* enabled = getenv("APPSPAWNX_PROFILED_JIT");
    if (enabled == nullptr || strcmp(enabled, "1") != 0 || msg.bundleName.empty()) {
        return false;
    }

    std::string codePath = msg.apkPath;
    if (codePath.empty()) {
        const char* launchPath = getenv("ASX_APK_PATH");
        if (launchPath != nullptr) codePath = launchPath;
    }
    if (codePath.empty()) {
        fprintf(stderr, "[CHILD-PROFILE-JIT] skipped: no APK path for %s\n",
                msg.bundleName.c_str());
        return false;
    }

    const std::string profilePath =
            "/data/data/" + msg.bundleName + "/code_cache/primary.prof";
    const std::string dexProfilePath = codePath + ".prof";
    struct stat st {};
    if (lstat(dexProfilePath.c_str(), &st) == 0) {
        if (!S_ISLNK(st.st_mode)) {
            fprintf(stderr,
                    "[CHILD-PROFILE-JIT] refusing existing non-symlink %s\n",
                    dexProfilePath.c_str());
            return false;
        }
        char target[PATH_MAX];
        const ssize_t length = readlink(
                dexProfilePath.c_str(), target, sizeof(target) - 1);
        if (length < 0) {
            fprintf(stderr, "[CHILD-PROFILE-JIT] readlink(%s) failed: %s\n",
                    dexProfilePath.c_str(), strerror(errno));
            return false;
        }
        target[length] = '\0';
        if (profilePath != target) {
            fprintf(stderr,
                    "[CHILD-PROFILE-JIT] refusing mismatched link %s -> %s\n",
                    dexProfilePath.c_str(), target);
            return false;
        }
    } else if (errno != ENOENT) {
        fprintf(stderr, "[CHILD-PROFILE-JIT] lstat(%s) failed: %s\n",
                dexProfilePath.c_str(), strerror(errno));
        return false;
    } else if (symlink(profilePath.c_str(), dexProfilePath.c_str()) != 0) {
        fprintf(stderr, "[CHILD-PROFILE-JIT] symlink(%s -> %s) failed: %s\n",
                dexProfilePath.c_str(), profilePath.c_str(), strerror(errno));
        return false;
    }

    fprintf(stderr, "[CHILD-PROFILE-JIT] ready %s -> %s\n",
            dexProfilePath.c_str(), profilePath.c_str());
    fflush(stderr);
    return true;
}

// AppSpawnX launches an APK directly rather than through Android's installd
// and PackageManagerService.  The framework still calls
// LoadedApk.registerAppInfoToArt(), but the OH manifest bridge can only report
// an app-id (for example 10042) while the specialized process has a full
// multi-user uid (for example 20010042).  LoadedApk correctly rejects that
// mismatched ApplicationInfo as belonging to another process, so ART never
// learns which dex locations to profile.
//
// Supply the installer-owned metadata at the OH spawn boundary, where the
// package, unchanged APK path, and real specialized uid are authoritative.
// Keep the profile in the app-owned code_cache directory so no privileged
// installd service or package-specific filesystem rule is required.  This is
// the same VMRuntime API used by LoadedApk; it does not alter app bytecode or
// relax ART's compilation policy.
static bool wl_register_app_profile(JNIEnv* env, const SpawnMsg& msg) {
    if (env == nullptr || msg.bundleName.empty()) return false;

    const char* forcedJit = getenv("APPSPAWNX_FORCE_JIT");
    if (forcedJit == nullptr || strcmp(forcedJit, "1") != 0) return false;

    std::string codePath = msg.apkPath;
    if (codePath.empty()) {
        const char* launchPath = getenv("ASX_APK_PATH");
        if (launchPath != nullptr) codePath = launchPath;
    }
    if (codePath.empty()) {
        fprintf(stderr, "[CHILD-PROFILE] skipped: no APK path for %s\n",
                msg.bundleName.c_str());
        return false;
    }

    const std::string codeCache =
            "/data/data/" + msg.bundleName + "/code_cache";
    if (mkdir(codeCache.c_str(), 0770) != 0 && errno != EEXIST) {
        fprintf(stderr, "[CHILD-PROFILE] mkdir(%s) failed: %s\n",
                codeCache.c_str(), strerror(errno));
        return false;
    }
    const std::string profilePath = codeCache + "/primary.prof";

    jclass vmRuntime = env->FindClass("dalvik/system/VMRuntime");
    if (vmRuntime == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        fprintf(stderr, "[CHILD-PROFILE] VMRuntime class unavailable\n");
        return false;
    }
    jmethodID registerAppInfo = env->GetStaticMethodID(
            vmRuntime,
            "registerAppInfo",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;I)V");
    if (registerAppInfo == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(vmRuntime);
        fprintf(stderr, "[CHILD-PROFILE] VMRuntime.registerAppInfo unavailable\n");
        return false;
    }

    jclass stringClass = env->FindClass("java/lang/String");
    jstring packageName = env->NewStringUTF(msg.bundleName.c_str());
    jstring currentProfile = env->NewStringUTF(profilePath.c_str());
    jstring referenceProfile = env->NewStringUTF("");
    jstring apkPath = env->NewStringUTF(codePath.c_str());
    jobjectArray codePaths = env->NewObjectArray(1, stringClass, apkPath);

    env->CallStaticVoidMethod(vmRuntime,
                              registerAppInfo,
                              packageName,
                              currentProfile,
                              referenceProfile,
                              codePaths,
                              1 /* primary APK */);
    const bool ok = !env->ExceptionCheck();
    if (!ok) env->ExceptionClear();
    fprintf(stderr,
            "[CHILD-PROFILE] register package=%s apk=%s profile=%s result=%s\n",
            msg.bundleName.c_str(), codePath.c_str(), profilePath.c_str(),
            ok ? "OK" : "EXCEPTION");
    fflush(stderr);

    env->DeleteLocalRef(codePaths);
    env->DeleteLocalRef(apkPath);
    env->DeleteLocalRef(referenceProfile);
    env->DeleteLocalRef(currentProfile);
    env->DeleteLocalRef(packageName);
    env->DeleteLocalRef(stringClass);
    env->DeleteLocalRef(vmRuntime);
    return ok;
}

static bool wl_join_process_cgroup(const char* path) {
    const int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "[WESTLAKE-APP-PRIV] open(%s) failed: %s\n",
                path, strerror(errno));
        return false;
    }
    char pid[32];
    const int length = snprintf(pid, sizeof(pid), "%d\n",
                                static_cast<int>(getpid()));
    const ssize_t written = write(fd, pid, static_cast<size_t>(length));
    const int savedErrno = errno;
    close(fd);
    if (written != length) {
        errno = savedErrno;
        fprintf(stderr, "[WESTLAKE-APP-PRIV] write(%s) failed: %s\n",
                path, strerror(errno));
        return false;
    }
    return true;
}

// Task-profile placement belongs to the privileged spawn phase.  After the
// app UID and SELinux domain are installed, a normal app must not need write
// access to the system-owned cgroup controls.  That access can disappear on
// reboot even when a previous development session happened to allow it.
static int wl_prepare_app_cpu_profile() {
    if (getenv("WL_NO_DROP_UID") != nullptr) return 0;
    if (!wl_join_process_cgroup("/dev/cpuset/top-app/cgroup.procs") ||
        !wl_join_process_cgroup("/dev/cpuctl/top-app/cgroup.procs")) {
        fflush(stderr);
        return -1;
    }
    fprintf(stderr, "[WESTLAKE-APP-PRIV] cpuProfile=top-app before app credentials uid=%d\n",
            static_cast<int>(getuid()));
    return 0;
}

// Split specialization around credential changes: privileged cgroup placement
// and bounding-set removal happen first; the finalizer verifies capabilities
// are gone after UID/GID and SELinux setup. Retain the display-class nice floor
// (-4), without allowing app background workers to claim -8/-16/-20.
static int wl_bounding_dropped = 0;

// Bounding-set removal requires CAP_SETPCAP and must precede setresuid().
// Removing a bounding bit does not remove current effective capabilities, so
// the remaining UID/GID and SELinux specialization can still finish.
static int wl_drop_capability_bounding_set() {
    for (int capability = 0; ; ++capability) {
        errno = 0;
        const int present = prctl(PR_CAPBSET_READ, capability, 0, 0, 0);
        if (present < 0) {
            if (errno == EINVAL) return 0;
            return -1;
        }
        if (present != 0) {
            if (prctl(PR_CAPBSET_DROP, capability, 0, 0, 0) != 0) {
                fprintf(stderr, "[WESTLAKE-APP-PRIV] privileged bounding drop(%d) failed: %s\n",
                        capability, strerror(errno));
                return -1;
            }
            ++wl_bounding_dropped;
        }
    }
}

static int wl_finalize_app_privileges() {
    if (getenv("WL_NO_DROP_UID") != nullptr) {
        fprintf(stderr,
                "[WESTLAKE-APP-PRIV] skipped because WL_NO_DROP_UID is set\n");
        fflush(stderr);
        return 0;
    }

    // This board's v1 cpu controller has shares/uclamp but no CFS bandwidth
    // quota, and top-app's cpuset spans every CPU.  A pathological foreground
    // workload can therefore keep all eight CPUs runnable even after correct
    // cgroup placement.  Reserve the two highest-numbered (and fastest on the
    // validation board) CPUs for OH input/composition.  New ART/app threads
    // inherit this process affinity.  The environment override provides an
    // immediate per-launch rollback or tuning point without another build.
    // musl's sysconf(_SC_NPROCESSORS_ONLN) can report the process/profile
    // capacity rather than the actual schedulable mask on this OH build.  The
    // deployed child consequently saw an eight-CPU mask but skipped the
    // reservation branch.  Derive the count from the kernel affinity mask we
    // are about to modify; this is also the right source if a parent profile
    // intentionally constrains the child before specialization.
    cpu_set_t inheritedAffinity;
    CPU_ZERO(&inheritedAffinity);
    int onlineCpus = 0;
    const bool haveInheritedAffinity =
        sched_getaffinity(0, sizeof(inheritedAffinity), &inheritedAffinity) == 0;
    if (haveInheritedAffinity) {
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
            if (CPU_ISSET(cpu, &inheritedAffinity)) ++onlineCpus;
        }
    }
    if (onlineCpus <= 0) {
        const long reported = sysconf(_SC_NPROCESSORS_ONLN);
        onlineCpus = reported > 0
            ? static_cast<int>(reported > CPU_SETSIZE ? CPU_SETSIZE : reported)
            : 1;
        // If the kernel query itself failed, use sysconf only as a fallback
        // source and construct the conventional contiguous online mask.  Do
        // not pass the still-empty inheritedAffinity to sched_setaffinity.
        for (int cpu = 0; cpu < onlineCpus; ++cpu) {
            CPU_SET(cpu, &inheritedAffinity);
        }
    }
    int reservedCpus = onlineCpus >= 8 ? 2 : 0;
    if (const char* value = getenv("WESTLAKE_APP_CPU_RESERVE");
        value != nullptr && *value != '\0') {
        const long requested = strtol(value, nullptr, 10);
        if (requested >= 0 && requested < onlineCpus) {
            reservedCpus = static_cast<int>(requested);
        }
    }
    const int appCpus = onlineCpus - reservedCpus;
    if (reservedCpus > 0 && appCpus > 0 && onlineCpus <= CPU_SETSIZE) {
        cpu_set_t affinity;
        CPU_ZERO(&affinity);
        // Keep the lowest-numbered inherited CPUs and leave the highest two
        // for OH input/composition.  Never add a CPU that was absent from the
        // inherited mask.
        int kept = 0;
        for (int cpu = 0; cpu < CPU_SETSIZE && kept < appCpus; ++cpu) {
            if (CPU_ISSET(cpu, &inheritedAffinity)) {
                CPU_SET(cpu, &affinity);
                ++kept;
            }
        }
        if (sched_setaffinity(0, sizeof(affinity), &affinity) != 0) {
            fprintf(stderr,
                    "[WESTLAKE-APP-PRIV] sched_setaffinity(0-%d) failed: %s\n",
                    appCpus - 1, strerror(errno));
            fflush(stderr);
            return -1;
        }
        fprintf(stderr,
                "[WESTLAKE-APP-PRIV] cpu affinity=0-%d reserve=%d\n",
                appCpus - 1, reservedCpus);
    }

    struct rlimit niceLimit {};
    niceLimit.rlim_cur = 24;  // Linux minimum nice = 20 - rlim_cur = -4.
    niceLimit.rlim_max = 24;
    if (setrlimit(RLIMIT_NICE, &niceLimit) != 0) {
        fprintf(stderr,
                "[WESTLAKE-APP-PRIV] setrlimit(RLIMIT_NICE=24) failed: %s\n",
                strerror(errno));
        fflush(stderr);
        return -1;
    }

    for (int capability = 0; ; ++capability) {
        errno = 0;
        const int present = prctl(PR_CAPBSET_READ, capability, 0, 0, 0);
        if (present < 0) {
            if (errno == EINVAL) break;  // Past the kernel's last capability.
            fprintf(stderr,
                    "[WESTLAKE-APP-PRIV] PR_CAPBSET_READ(%d) failed: %s\n",
                    capability, strerror(errno));
            fflush(stderr);
            return -1;
        }
        if (present != 0) {
            fprintf(stderr, "[WESTLAKE-APP-PRIV] capability %d remains in bounding set\n",
                    capability);
            return -1;
        }
    }

    __user_cap_header_struct header {};
    header.version = _LINUX_CAPABILITY_VERSION_3;
    header.pid = 0;
    __user_cap_data_struct data[2] {};
    if (syscall(SYS_capset, &header, &data[0]) != 0) {
        fprintf(stderr, "[WESTLAKE-APP-PRIV] capset(zero) failed: %s\n",
                strerror(errno));
        fflush(stderr);
        return -1;
    }
    if (prctl(PR_SET_KEEPCAPS, 0, 0, 0, 0) != 0) {
        fprintf(stderr, "[WESTLAKE-APP-PRIV] PR_SET_KEEPCAPS(0) failed: %s\n",
                strerror(errno));
        fflush(stderr);
        return -1;
    }

    __user_cap_data_struct verify[2] {};
    if (syscall(SYS_capget, &header, &verify[0]) != 0) {
        fprintf(stderr, "[WESTLAKE-APP-PRIV] capget verify failed: %s\n",
                strerror(errno));
        fflush(stderr);
        return -1;
    }
    const unsigned long long effective =
        static_cast<unsigned long long>(verify[0].effective) |
        (static_cast<unsigned long long>(verify[1].effective) << 32);
    const unsigned long long permitted =
        static_cast<unsigned long long>(verify[0].permitted) |
        (static_cast<unsigned long long>(verify[1].permitted) << 32);
    fprintf(stderr,
            "[WESTLAKE-APP-PRIV] finalized uid=%d CapEff=%#llx CapPrm=%#llx "
            "boundingDropped=%d niceFloor=-4 cpuProfile=top-app\n",
            static_cast<int>(getuid()), effective, permitted, wl_bounding_dropped);
    fflush(stderr);
    return (effective == 0 && permitted == 0) ? 0 : -1;
}

// Android's top-app profile gives the UI thread latency priority over the
// application's own background pools.  This standalone launcher creates
// ActivityThread on a dedicated pthread after specialization, so no framework
// task-profile call ever applies that distinction: the UI looper remains at
// nice 0 while hundreds of app workers compete at the same priority.  Use the
// bounded -4 floor installed above for only the ActivityThread pthread.  The
// launcher joiner and all ART/app background threads keep their inherited nice
// value, and the process remains inside the top-app cgroups/cpuset.
static void wl_prioritize_activity_thread() {
    errno = 0;
    if (setpriority(PRIO_PROCESS, 0, -4) != 0) {
        fprintf(stderr,
                "[WESTLAKE-APP-PRIV] ActivityThread setpriority(-4) failed: %s\n",
                strerror(errno));
        fflush(stderr);
        return;
    }
    errno = 0;
    const int priority = getpriority(PRIO_PROCESS, 0);
    fprintf(stderr,
            "[WESTLAKE-APP-PRIV] ActivityThread nice=%d\n",
            errno == 0 ? priority : 0);
    fflush(stderr);
}

// ── §528: create the JIT that this libart never creates ────────────────────────────────────────
// The child runs with ZERO compiled code: no jit-code-cache mapping, no AOT mapping, only [vdso].
// Everything is interpreted, which is why startup takes minutes, the UI can deadlock, and
// timing-sensitive paths behave nothing like a real device.
//
// It is not a configuration problem — ART accepts -Xusejit:true / -Xjitthreshold:1 — and it is not
// a missing compiler: JitCompiler::CompileMethod and OptimizingCompiler::JitCompile are statically
// linked into libart (371 JIT symbols). The JIT is simply never CREATED, and falls through BOTH
// paths that would create it:
//
//   1. runtime.cc:  if (UseJitCompilation() || GetSaveProfilingInfo()) { if (!IsZygote()) CreateJit(); }
//      appspawn-x runs ART *as a zygote*, so IsZygote() is true and this is skipped.
//   2. Post-fork, AOSP creates it inside Runtime::InitNonZygoteOrPostFork — which this libart
//      deliberately SKIPS ("Skipping InitNonZygoteOrPostFork (standalone build)") to avoid starting
//      the SignalCatcher thread.
//
// Confirmed by markers: "[RT] Creating JIT..." appears 0 times in the parent AND the child, while
// "Skipping InitNonZygoteOrPostFork" appears once. Nothing ever tried, which is why ART never logged
// a failure — its own "Failed to create JIT Code Cache: " could not fire.
//
// libart is NOT rebuildable here, but Runtime::CreateJit() is an exported symbol
// (_ZN3art7Runtime9CreateJitEv, type T), as is Runtime::Current(). So do exactly what the skipped
// init would have done, from outside, at the point where the child's ART state is consistent —
// after ZygoteHooks.postForkChild/postForkCommon.
//
// ⚠️Env-gated (APPSPAWNX_FORCE_JIT=1) so it is trivially reversible: this calls into ART internals
// at a point AOSP would not, and a bad interaction must be switchable off without a rebuild.

// ArtMethod::access_flags_ immediately follows its 32-bit declaring-class
// reference in ART image version 118. The standalone runtime converts a few
// managed boot methods into JNI methods. For such a conversion, ART's
// kAccSkipAccessChecks bit (managed meaning) must not survive as
// kAccFastNative (native meaning); both use 0x00080000. An imageless runtime
// naturally lacks that verifier bit, while an image-backed runtime otherwise
// treats the converted synchronized TimeZone method as FastNative and corrupts
// its monitor protocol.
//
// Keep this compatibility repair narrow and fail closed: accept only a direct,
// aligned jmethodID whose flags already say native+fast. Opaque JNI IDs and
// unexpected ART layouts are logged and left untouched.
static bool wl_clear_converted_fast_native(jmethodID method, const char* name) {
    constexpr uintptr_t kMinimumDirectMethodAddress = 0x10000u;
    constexpr size_t kArtMethodAccessFlagsOffset = sizeof(uint32_t);
    constexpr uint32_t kAccNative = 0x00000100u;
    constexpr uint32_t kAccFastNative = 0x00080000u;

    const uintptr_t raw = reinterpret_cast<uintptr_t>(method);
    if (raw < kMinimumDirectMethodAddress ||
        (raw & (alignof(uint32_t) - 1u)) != 0u) {
        fprintf(stderr,
                "[CHILD-TZ-JNI] %s method id is not a direct ArtMethod: %p\n",
                name,
                reinterpret_cast<void*>(raw));
        fflush(stderr);
        return false;
    }

    auto* accessFlags = reinterpret_cast<uint32_t*>(
            raw + kArtMethodAccessFlagsOffset);
    const uint32_t before = __atomic_load_n(accessFlags, __ATOMIC_ACQUIRE);
    if ((before & kAccNative) == 0u) {
        fprintf(stderr,
                "[CHILD-TZ-JNI] %s unexpected non-native flags=0x%x method=%p\n",
                name,
                before,
                reinterpret_cast<void*>(raw));
        fflush(stderr);
        return false;
    }
    if ((before & kAccFastNative) == 0u) {
        fprintf(stderr,
                "[CHILD-TZ-JNI] %s flags already safe=0x%x method=%p\n",
                name,
                before,
                reinterpret_cast<void*>(raw));
        fflush(stderr);
        return true;
    }

    const uint32_t after = __atomic_and_fetch(
            accessFlags, ~kAccFastNative, __ATOMIC_RELEASE);
    fprintf(stderr,
            "[CHILD-TZ-JNI] %s cleared converted FastNative flags 0x%x -> 0x%x method=%p\n",
            name,
            before,
            after,
            reinterpret_cast<void*>(raw));
    fflush(stderr);
    return (after & kAccFastNative) == 0u;
}

// A boot image may restore oat/native quick entrypoints during the zygote
// post-fork hooks after the standalone runtime converted java.util.TimeZone's
// getDefault methods to its portable native implementation. Sanitize the
// converted access flags and re-register in the specialized child so ART
// republishes the generic-JNI entrypoint with the synchronized-method monitor
// protocol. Keep this opt-in until a boot-image candidate passes the full app
// and control matrix.
static bool wl_rebind_portable_timezone(JNIEnv* env) {
    const char* enabled = getenv("WESTLAKE_CHILD_REBIND_PORTABLE_TIMEZONE");
    if (enabled == nullptr || strcmp(enabled, "1") != 0 || env == nullptr) {
        return false;
    }

    using GetDefaultFn = jobject (*)(JNIEnv*, jclass);
    dlerror();
    auto getDefault = reinterpret_cast<GetDefaultFn>(
            dlsym(RTLD_DEFAULT, "Westlake_TimeZone_getDefault"));
    const char* symbolError = dlerror();
    if (getDefault == nullptr) {
        fprintf(stderr,
                "[CHILD-TZ-JNI] portable TimeZone implementation unavailable: %s\n",
                symbolError != nullptr ? symbolError : "symbol not found");
        fflush(stderr);
        return false;
    }

    jclass cls = env->FindClass("java/util/TimeZone");
    if (cls == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        fprintf(stderr, "[CHILD-TZ-JNI] java/util/TimeZone unavailable\n");
        fflush(stderr);
        return false;
    }
    JNINativeMethod methods[] = {
        {const_cast<char*>("getDefault"),
         const_cast<char*>("()Ljava/util/TimeZone;"),
         reinterpret_cast<void*>(getDefault)},
        {const_cast<char*>("getDefaultRef"),
         const_cast<char*>("()Ljava/util/TimeZone;"),
         reinterpret_cast<void*>(getDefault)},
    };

    bool flagsSafe = true;
    for (const JNINativeMethod& method : methods) {
        jmethodID id = env->GetStaticMethodID(cls, method.name, method.signature);
        if (id == nullptr || env->ExceptionCheck()) {
            env->ExceptionClear();
            fprintf(stderr,
                    "[CHILD-TZ-JNI] GetStaticMethodID failed for %s%s\n",
                    method.name,
                    method.signature);
            fflush(stderr);
            flagsSafe = false;
            continue;
        }
        flagsSafe = wl_clear_converted_fast_native(id, method.name) && flagsSafe;
    }
    if (!flagsSafe) {
        env->DeleteLocalRef(cls);
        fprintf(stderr,
                "[CHILD-TZ-JNI] refusing rebind because access flags were not safely repaired\n");
        fflush(stderr);
        return false;
    }

    const jint rc = env->RegisterNatives(cls, methods, 2);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
    fprintf(stderr,
            "[CHILD-TZ-JNI] RegisterNatives TimeZone.getDefault/getDefaultRef rc=%d\n",
            static_cast<int>(rc));
    fflush(stderr);
    return rc == JNI_OK;
}

// Compatibility path for validated libart builds that predate the
// WestlakeEnsureChildJit wrapper but export the two upstream Runtime methods.
// Do not edit private Runtime/JitOptions fields: -Xusejit:true must already be
// visible through Runtime::UseJitCompilation(), and CreateJit() owns all state.
static int wl_create_jit_after_fork_compat() {
    using CurrentFn = void* (*)();
    using UseJitFn = bool (*)(const void*);
    using CreateJitFn = void (*)(void*);

    dlerror();
    auto current = reinterpret_cast<CurrentFn>(
        dlsym(RTLD_DEFAULT, "_ZN3art7Runtime7CurrentEv"));
    auto useJit = reinterpret_cast<UseJitFn>(
        dlsym(RTLD_DEFAULT, "_ZNK3art7Runtime17UseJitCompilationEv"));
    auto createJit = reinterpret_cast<CreateJitFn>(
        dlsym(RTLD_DEFAULT, "_ZN3art7Runtime9CreateJitEv"));
    const char* symbolError = dlerror();
    if (current == nullptr || useJit == nullptr || createJit == nullptr) {
        LOGE("[CHILD-JIT] compatibility symbols unavailable current=%p use=%p create=%p: %s",
             reinterpret_cast<void*>(current), reinterpret_cast<void*>(useJit),
             reinterpret_cast<void*>(createJit),
             symbolError != nullptr ? symbolError : "unknown dlsym error");
        return -1;
    }

    void* runtime = current();
    if (runtime == nullptr) {
        LOGE("[CHILD-JIT] Runtime::Current returned null");
        return -2;
    }

    // This validated libart predates the source-level child helper and clears
    // JitOptions::use_jit_compilation_ while starting as a zygote. Recover the
    // option only after proving the exact CreateJit machine-code layout that
    // names Runtime::jit_options_ (+656) and its first boolean field (+0).
    // Refuse all other binaries rather than writing through a guessed ABI.
    uint32_t loadJitOptions = 0;
    uint32_t loadUseJit = 0;
    uint32_t loadSaveProfile = 0;
    memcpy(&loadJitOptions,
           reinterpret_cast<const unsigned char*>(createJit) + 0x14,
           sizeof(loadJitOptions));
    memcpy(&loadUseJit,
           reinterpret_cast<const unsigned char*>(createJit) + 0x1c,
           sizeof(loadUseJit));
    memcpy(&loadSaveProfile,
           reinterpret_cast<const unsigned char*>(createJit) + 0x24,
           sizeof(loadSaveProfile));
    // Clang may keep the profile-saver boolean in either w8 or w9. Both
    // encodings read byte offset 48 from the already-validated JitOptions
    // pointer in x8; the destination register does not change the private
    // Runtime/JitOptions layout being guarded here.
    const bool knownSaveProfileLoad =
        loadSaveProfile == 0x3940c108u || loadSaveProfile == 0x3940c109u;
    if (loadJitOptions != 0xf9414808u || loadUseJit != 0x39400109u ||
        !knownSaveProfileLoad) {
        LOGE("[CHILD-JIT] refusing unknown CreateJit ABI insns=%08x/%08x/%08x",
             loadJitOptions, loadUseJit, loadSaveProfile);
        return -3;
    }

    constexpr size_t kJitOptionsOffset = 656;
    constexpr size_t kJitOffset = 640;
    constexpr size_t kJitCodeCacheOffset = 648;
    void* jitOptions = *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(runtime) + kJitOptionsOffset);
    if (jitOptions == nullptr ||
        (reinterpret_cast<uintptr_t>(jitOptions) & (alignof(void*) - 1)) != 0) {
        LOGE("[CHILD-JIT] refusing implausible jit_options_=%p", jitOptions);
        return -4;
    }

    unsigned char* useJitOption = reinterpret_cast<unsigned char*>(jitOptions);
    // CreateJit's third checked instruction reads ProfileSaverOptions::enabled_
    // at JitOptions+48.  The standalone zygote clears both booleans before the
    // fork; restore the two VM options together in the specialized child.
    // All other ProfileSaverOptions fields retain their values parsed from
    // -Xjitsaveprofilinginfo.
    unsigned char* saveProfileOption = useJitOption + 48;
    if (*useJitOption > 1 || *saveProfileOption > 1) {
        LOGE("[CHILD-JIT] refusing invalid JIT options use=%u save_profile=%u",
             static_cast<unsigned>(*useJitOption),
             static_cast<unsigned>(*saveProfileOption));
        return -5;
    }

    void* existingJit = *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(runtime) + kJitOffset);
    if (existingJit == nullptr) {
        if (*useJitOption == 0) {
            *useJitOption = 1;
            LOGI("[CHILD-JIT] restored child use_jit_compilation_ after zygote startup");
        }
        if (*saveProfileOption == 0) {
            *saveProfileOption = 1;
            LOGI("[CHILD-JIT] restored child profile saver after zygote startup");
        }
        createJit(runtime);
    }

    void* jit = *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(runtime) + kJitOffset);
    void* codeCache = *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(runtime) + kJitCodeCacheOffset);

    bool codeCacheMapped = false;
    FILE* maps = fopen("/proc/self/maps", "r");
    if (maps != nullptr) {
        char line[512];
        while (fgets(line, sizeof(line), maps) != nullptr) {
            if (strstr(line, "jit-code-cache") != nullptr ||
                strstr(line, "/memfd:jit-cache") != nullptr) {
                codeCacheMapped = true;
                break;
            }
        }
        fclose(maps);
    }
    if (jit == nullptr || codeCache == nullptr || !useJit(runtime)) {
        LOGE("[CHILD-JIT] incomplete JIT state jit=%p cache=%p enabled=%d",
             jit, codeCache, useJit(runtime));
        return -7;
    }
    if (!codeCacheMapped) {
        // OH's procfs does not consistently expose anonymous VMA names. The
        // two Runtime-owned objects plus UseJitCompilation() are the primary
        // in-process oracle; keep the missing label as diagnostics only.
        LOGW("[CHILD-JIT] jit-code-cache VMA label unavailable in OH procfs");
    }

    LOGI("[CHILD-JIT] application JIT active through Runtime::CreateJit compatibility path jit=%p cache=%p",
         jit, codeCache);
    return 0;
}

// §578 headless JIT load test. Runs com.android.internal.os.WlJitBench.bench (pure arithmetic, NO
// atomics/coroutines/objects) on a dedicated big-stack thread AFTER the JIT is enabled. Purpose:
// isolate "can the JIT compile AND execute compiled code under sustained load" from noice's own
// coroutine/atomics path (where it dies on a tap). If this SURVIVES + compiles, the JIT engine works
// and noice's crash is workload-specific; if it dies, compiled-execution itself is broken.
// Gated by APPSPAWNX_JIT_BENCH=1. Runs WITHOUT any UI tap so noice stays idle-alive alongside it.
static void* wl_jit_bench_thread(void*) {
    JavaVM* vm = nullptr; jsize n = 0;
    if (JNI_GetCreatedJavaVMs(&vm, 1, &n) != JNI_OK || vm == nullptr || n < 1) {
        fprintf(stderr, "[JIT-BENCH] no JVM\n"); fflush(stderr); return nullptr;
    }
    JNIEnv* e = nullptr;
    JavaVMAttachArgs aa{JNI_VERSION_1_6, "wl-jit-bench", nullptr};
    if (vm->AttachCurrentThread(&e, &aa) != JNI_OK || e == nullptr) {
        fprintf(stderr, "[JIT-BENCH] attach FAILED\n"); fflush(stderr); return nullptr;
    }
    jclass cls = e->FindClass("com/android/internal/os/WlJitBench");
    if (cls == nullptr) { if (e->ExceptionCheck()) e->ExceptionClear();
        fprintf(stderr, "[JIT-BENCH] FindClass FAILED\n"); fflush(stderr); vm->DetachCurrentThread(); return nullptr; }
    jmethodID m = e->GetStaticMethodID(cls, "bench", "(II)J");
    if (m == nullptr) { if (e->ExceptionCheck()) e->ExceptionClear();
        fprintf(stderr, "[JIT-BENCH] GetStaticMethodID FAILED\n"); fflush(stderr); vm->DetachCurrentThread(); return nullptr; }
    int rounds = 400, iters = 2000000;
    if (const char* r = getenv("APPSPAWNX_JIT_BENCH_ROUNDS"); r && *r) rounds = atoi(r);
    if (const char* it = getenv("APPSPAWNX_JIT_BENCH_ITERS"); it && *it) iters = atoi(it);
    // §579b: optionally mark bench() itself NON-COMPILABLE (kAccCompileDontBother=0x02000000,
    // access_flags_ at ArtMethod+4; jmethodID IS the ArtMethod*), so the JNI CallStatic always
    // invokes INTERPRETED bench, which then calls COMPILED run() in its loop — the realistic
    // "interpreter calls a hot compiled leaf" pattern. Tests whether the SOE is specific to the
    // JNI-invoke-of-a-COMPILED-callee stack check.
    if (const char* nb = getenv("APPSPAWNX_JIT_BENCH_NOCOMPILE_OUTER"); nb && strcmp(nb, "1") == 0) {
        uint32_t* af = reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(m) + 4);
        *af |= 0x02000000u;
        fprintf(stderr, "[JIT-BENCH] bench() marked non-compilable (outer stays interpreted)\n");
        fflush(stderr);
    }
    {   // §578d: log this thread's real stack geometry so we can compare against SP at the fault
        pthread_attr_t ga; void* sb = nullptr; size_t sz = 0;
        if (pthread_getattr_np(pthread_self(), &ga) == 0) {
            pthread_attr_getstack(&ga, &sb, &sz); pthread_attr_destroy(&ga);
        }
        int local;
        fprintf(stderr, "[JIT-BENCH] stack base=%p size=%zu (lo=%p hi=%p) &local≈SP=%p\n",
                sb, sz, sb, (void*)((char*)sb + sz), (void*)&local);
        fflush(stderr);
    }
    fprintf(stderr, "[JIT-BENCH] START rounds=%d itersPerRound=%d (run/step/mul should get hot)\n",
            rounds, iters); fflush(stderr);
    // §578c PAUSE mode: run ONE round (triggers compilation via the hot internal loop), then park
    // forever WITHOUT re-entering the compiled method. This keeps the process alive with the buggy
    // compiled code sitting in the code cache, so /proc/<pid>/mem can be dumped and the prologue
    // disassembled. Round 1's normal compiled-entry is what crashes, so we never do it here.
    if (const char* pz = getenv("APPSPAWNX_JIT_BENCH_PAUSE"); pz && strcmp(pz, "1") == 0) {
        jlong v = e->CallStaticLongMethod(cls, m, 1, iters);
        if (e->ExceptionCheck()) { e->ExceptionClear();
            fprintf(stderr, "[JIT-BENCH] PAUSE: round0 threw (unexpected)\n"); fflush(stderr); }
        else fprintf(stderr, "[JIT-BENCH] PAUSE: round0 done checksum=%lld — compiled code now in cache\n",
                     (long long) v);
        fflush(stderr);
        // §578c: dump the JIT code-cache mapping to a file IN-PROCESS (survives noice's later death)
        // so the compiled prologue can be disassembled offline. Find the r-x jit-code-cache range in
        // /proc/self/maps and copy it out.
        FILE* mp = fopen("/proc/self/maps", "r");
        if (mp) {
            char line[512];
            while (fgets(line, sizeof line, mp)) {
                if (strstr(line, "jit-code-cache") && strstr(line, "r-x")) {
                    unsigned long lo = 0, hi = 0;
                    if (sscanf(line, "%lx-%lx", &lo, &hi) == 2 && hi > lo) {
                        size_t len = hi - lo; if (len > (4u<<20)) len = 4u<<20;  // cap 4MB
                        fprintf(stderr, "[JIT-BENCH] code-cache r-x %lx-%lx (%zu bytes) -> /data/local/tmp/jitcode.bin\n",
                                lo, hi, len); fflush(stderr);
                        FILE* out = fopen("/data/local/tmp/jitcode.bin", "wb");
                        if (out) { fwrite((const void*) lo, 1, len, out); fclose(out);
                            fprintf(stderr, "[JIT-BENCH] dumped code cache base=0x%lx\n", lo); }
                        break;
                    }
                }
            }
            fclose(mp);
        }
        fprintf(stderr, "[JIT-BENCH] PARKING\n"); fflush(stderr);
        for (;;) sleep(3600);
    }
    for (int r = 0; r < rounds; r++) {
        if (r < 3) { int local; fprintf(stderr, "[JIT-BENCH] round %d pre-call &local≈SP=%p\n", r, (void*)&local); fflush(stderr); }
        jlong v = e->CallStaticLongMethod(cls, m, 1, iters);   // one internal run() per round
        if (e->ExceptionCheck()) {
            // printStackTrace is a fork-safe noop here, so name the throwable directly.
            jthrowable ex = e->ExceptionOccurred();
            e->ExceptionClear();
            const char* cn = "?"; const char* msg = "?";
            jstring jmsgs = nullptr;
            if (ex != nullptr) {
                jclass exc = e->GetObjectClass(ex);
                jmethodID gname = e->GetMethodID(e->FindClass("java/lang/Class"), "getName", "()Ljava/lang/String;");
                jstring jn = exc && gname ? (jstring) e->CallObjectMethod(exc, gname) : nullptr;
                if (jn) cn = e->GetStringUTFChars(jn, nullptr);
                jmethodID gmsg = e->GetMethodID(e->FindClass("java/lang/Throwable"), "getMessage", "()Ljava/lang/String;");
                jmsgs = gmsg ? (jstring) e->CallObjectMethod(ex, gmsg) : nullptr;
                if (e->ExceptionCheck()) e->ExceptionClear();
                if (jmsgs) msg = e->GetStringUTFChars(jmsgs, nullptr);
            }
            fprintf(stderr, "[JIT-BENCH] EXCEPTION at round %d: %s: %s\n", r, cn, msg);
            fflush(stderr);
            // §578e: name the throw site — where does the SOE actually originate?
            if (ex != nullptr) {
                jclass thc = e->FindClass("java/lang/Throwable");
                jmethodID gst = e->GetMethodID(thc, "getStackTrace", "()[Ljava/lang/StackTraceElement;");
                jobjectArray fr = gst ? (jobjectArray) e->CallObjectMethod(ex, gst) : nullptr;
                if (e->ExceptionCheck()) e->ExceptionClear();
                if (fr) {
                    jsize fn = e->GetArrayLength(fr); if (fn > 20) fn = 20;
                    jclass stec = e->FindClass("java/lang/StackTraceElement");
                    jmethodID gc = e->GetMethodID(stec, "getClassName", "()Ljava/lang/String;");
                    jmethodID gm = e->GetMethodID(stec, "getMethodName", "()Ljava/lang/String;");
                    fprintf(stderr, "[JIT-BENCH]   SOE depth=%d, top %d frames:\n", (int) e->GetArrayLength(fr), (int) fn);
                    for (jsize i = 0; i < fn; ++i) {
                        jobject st = e->GetObjectArrayElement(fr, i);
                        jstring jc2 = (jstring) e->CallObjectMethod(st, gc);
                        jstring jm2 = (jstring) e->CallObjectMethod(st, gm);
                        const char* c2 = jc2 ? e->GetStringUTFChars(jc2, nullptr) : "?";
                        const char* m2 = jm2 ? e->GetStringUTFChars(jm2, nullptr) : "?";
                        fprintf(stderr, "[JIT-BENCH]     at %s.%s\n", c2, m2); fflush(stderr);
                        if (jc2) { e->ReleaseStringUTFChars(jc2, c2); e->DeleteLocalRef(jc2); }
                        if (jm2) { e->ReleaseStringUTFChars(jm2, m2); e->DeleteLocalRef(jm2); }
                        e->DeleteLocalRef(st);
                    }
                }
            }
            vm->DetachCurrentThread(); return nullptr;
        }
        if ((r % 40) == 0) { fprintf(stderr, "[JIT-BENCH] round %d checksum=%lld\n", r, (long long) v); fflush(stderr); }
    }
    fprintf(stderr, "[JIT-BENCH] DONE all %d rounds — SURVIVED heavy compute load\n", rounds);
    fflush(stderr);
    vm->DetachCurrentThread();
    return nullptr;
}
static void wl_jit_bench_start() {
    const char* v = getenv("APPSPAWNX_JIT_BENCH");
    if (v == nullptr || strcmp(v, "1") != 0) return;
    size_t mb = 64;
    if (const char* s = getenv("APPSPAWNX_JIT_BENCH_STACK_MB"); s && *s) {
        long m = strtol(s, nullptr, 10); if (m >= 1 && m <= 512) mb = (size_t) m;
    }
    pthread_attr_t attr; pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, mb * 1024 * 1024);
    pthread_t t;
    if (pthread_create(&t, &attr, wl_jit_bench_thread, nullptr) == 0) {
        pthread_detach(t);
        fprintf(stderr, "[JIT-BENCH] thread launched (stack=%zuMB)\n", mb); fflush(stderr);
    } else {
        fprintf(stderr, "[JIT-BENCH] pthread_create FAILED\n"); fflush(stderr);
    }
    pthread_attr_destroy(&attr);
}

// ── §525b: name the JAVA method behind an interpreter SEGV, WITHOUT trusting libart ─────────────
// First cut called art::ArtMethod::PrettyMethod(x0) directly. It fired on the first real crash and
// produced NOTHING but the register line: no method name, and not even the "not exported" fallback —
// so PrettyMethod itself faulted on the pointer. That is informative in its own right (the ArtMethod
// is likely CORRUPT, not merely unresolvable, which fits §436 being memory corruption rather than a
// lookup bug), but it yields no site.
//
// So: never dereference anything directly, and never call into libart. Every read goes through
// process_vm_readv on our own pid, which returns EFAULT instead of raising a nested fault. Dump all
// registers, then decode any that plausibly point at an ArtMethod.
//
// ArtMethod layout (ART, Android 12+):
//   +0x00 u32 declaring_class_      (GcRoot, compressed reference)
//   +0x04 u32 access_flags_
//   +0x08 u32 dex_method_index_
//   +0x0c u16 method_index_
//   +0x0e u16 hotness_count_ / imt_index_
//   +0x10 ptr data_ ; +0x18 ptr entry_point_from_quick_compiled_code_
// dex_method_index_ + declaring_class_ are enough to identify the call target offline.
static volatile int wl_naming_in_progress = 0;

// Read remote memory without ever faulting: EFAULT comes back as a return value.
static bool wl_safe_read(unsigned long src, void* dst, size_t n) {
    struct iovec l; l.iov_base = dst;            l.iov_len = n;
    struct iovec r; r.iov_base = (void*) src;    r.iov_len = n;
    return process_vm_readv(getpid(), &l, 1, &r, 1, 0) == (ssize_t) n;
}

static void wl_dump_artmethod(const char* what, unsigned long p) {
    unsigned char raw[32];
    if (!wl_safe_read(p, raw, sizeof raw)) {
        char b[128];
        int n = snprintf(b, sizeof b, "[WESTLAKE-525]   %s=%#lx UNREADABLE\n", what, p);
        if (n > 0) { ssize_t w = write(2, b, (size_t) n); (void) w; }
        return;
    }
    unsigned int declaring, access, dexIdx; unsigned short mIdx, hotness;
    unsigned long data, quick;
    memcpy(&declaring, raw + 0x00, 4); memcpy(&access,  raw + 0x04, 4);
    memcpy(&dexIdx,    raw + 0x08, 4); memcpy(&mIdx,    raw + 0x0c, 2);
    memcpy(&hotness,   raw + 0x0e, 2);
    memcpy(&data,      raw + 0x10, 8); memcpy(&quick,   raw + 0x18, 8);
    Dl_info di;
    const char* data_lib = "?";
    unsigned long data_off = 0;
    if (data != 0 && dladdr((void*) data, &di) != 0 && di.dli_fname != nullptr) {
        data_lib = strrchr(di.dli_fname, '/');
        data_lib = data_lib != nullptr ? data_lib + 1 : di.dli_fname;
        data_off = data - (unsigned long) di.dli_fbase;
    }
    char b[420];
    int n = snprintf(b, sizeof b,
        "[WESTLAKE-525]   %s=%#lx declaring_class=%#x access=%#x dex_method_idx=%u "
        "method_idx=%u hotness=%u data=%#lx (%s+%#lx) quick=%#lx\n",
        what, p, declaring, access, dexIdx, mIdx, hotness,
        data, data_lib, data_off, quick);
    if (n > 0) { ssize_t w = write(2, b, (size_t) n); (void) w; }
}

static void wl_name_java_method(void* ctx) {
    ucontext_t* uc = (ucontext_t*) ctx;
    if (uc == nullptr || wl_naming_in_progress) return;
    wl_naming_in_progress = 1;

    // All registers: the first cut assumed x0 still held called_method, but the fault happens partway
    // into DoCall where x0..x3 may already be scratch (the observed x2=0x8, x3=0x2b are not pointers).
    for (int i = 0; i < 31; i += 4) {
        char b[200];
        int n = snprintf(b, sizeof b, "[WESTLAKE-525] x%-2d=%#-18lx x%-2d=%#-18lx x%-2d=%#-18lx x%-2d=%#lx\n",
            i,   (unsigned long) uc->uc_mcontext.regs[i],
            i+1, (unsigned long) (i+1 < 31 ? uc->uc_mcontext.regs[i+1] : 0),
            i+2, (unsigned long) (i+2 < 31 ? uc->uc_mcontext.regs[i+2] : 0),
            i+3, (unsigned long) (i+3 < 31 ? uc->uc_mcontext.regs[i+3] : 0));
        if (n > 0) { ssize_t w = write(2, b, (size_t) n); (void) w; }
    }
    {
        char b[160];
        int n = snprintf(b, sizeof b, "[WESTLAKE-525] sp=%#lx pc=%#lx\n",
            (unsigned long) uc->uc_mcontext.sp, (unsigned long) uc->uc_mcontext.pc);
        if (n > 0) { ssize_t w = write(2, b, (size_t) n); (void) w; }
    }

    // Decode every register that could plausibly be an ArtMethod: 4-byte aligned, above the null
    // page, and readable. ArtMethods are small and dense, so a few false positives are cheap.
    for (int i = 0; i < 31; ++i) {
        unsigned long v = (unsigned long) uc->uc_mcontext.regs[i];
        if (v < 0x10000 || (v & 3) != 0) continue;
        unsigned int probe;
        if (!wl_safe_read(v + 8, &probe, 4)) continue;   // dex_method_index_ must at least be readable
        char tag[8]; snprintf(tag, sizeof tag, "x%d", i);
        wl_dump_artmethod(tag, v);
    }

    // A native implementation reached through art_quick_generic_jni_trampoline no longer has the
    // ArtMethod in an argument register.  The trampoline keeps its callee-save frame in x28 and
    // stores the ArtMethod at frame[0].  Decode that indirection (and a small bounded stack window)
    // only for native-address faults; doing this for managed implicit-null checks would add noise
    // and perturb the hot exception path.
    if ((unsigned long) uc->uc_mcontext.pc >= (1ul << 36)) {
        unsigned long bases[3] = {
            (unsigned long) uc->uc_mcontext.regs[28],
            (unsigned long) uc->uc_mcontext.sp,
            (unsigned long) uc->uc_mcontext.regs[29],
        };
        for (int bi = 0; bi < 3; ++bi) {
            for (unsigned long off = 0; off < 768; off += sizeof(unsigned long)) {
                unsigned long candidate = 0;
                if (!wl_safe_read(bases[bi] + off, &candidate, sizeof(candidate)) ||
                    candidate < 0x10000 || (candidate & 3) != 0 || candidate >= (1ul << 48)) {
                    continue;
                }
                unsigned char raw[16];
                if (!wl_safe_read(candidate, raw, sizeof(raw))) continue;
                unsigned int declaring = 0, access = 0, dex_idx = 0;
                memcpy(&declaring, raw + 0, 4);
                memcpy(&access, raw + 4, 4);
                memcpy(&dex_idx, raw + 8, 4);
                if (declaring < 0x10000 || declaring >= 0x80000000 ||
                    (access & 0x100u) == 0 || dex_idx >= 200000u) {
                    continue;
                }
                char tag[32];
                snprintf(tag, sizeof(tag), "stack%d+%#lx", bi, off);
                wl_dump_artmethod(tag, candidate);
            }
        }
    }
    wl_naming_in_progress = 0;
}



// ---------------------------------------------------------------------------
// run  –  child process main entry point (does not return)
// ---------------------------------------------------------------------------
[[noreturn]] void ChildMain::run(const SpawnMsg& msg, AppSpawnXRuntime* runtime) {
    pid_t myPid = getpid();
    LOGI("Child process started, pid=%d uid=%d bundle=%s",
         myPid, msg.uid, msg.bundleName.c_str());

    // Diagnostic (kept until HelloWorld UI works, per memory
    // feedback_keep_cp_instrumentation.md): redirect native fd 1/2 to a
    // per-pid file so libart's LOG(FATAL)/CHECK output (otherwise
    // discarded into the inherited /dev/null fd 2) becomes readable
    // post-mortem.  Try multiple paths because appspawn:s0 SELinux
    // domain may deny write to /data/local/tmp; /data/service/el1/public/appspawnx
    // is created by appspawn_x.cfg specifically for our use.
    LOGI("[CHILD] entering stderr redirect probe");
    {
        const char* candPaths[] = {
            "/data/service/el1/public/appspawnx",
            "/data/misc/appspawnx",
            "/data/local/tmp",
            "/data/log",
        };
        int errFd = -1;
        char chosenPath[128] = {0};
        for (const char* dir : candPaths) {
            snprintf(chosenPath, sizeof(chosenPath),
                     "%s/adapter_child_%d.stderr", dir, myPid);
            errFd = open(chosenPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (errFd >= 0) {
                LOGI("[CHILD] opened %s fd=%d", chosenPath, errFd);
                break;
            }
            LOGW("[CHILD] open(%s) failed: errno=%d %s",
                 chosenPath, errno, strerror(errno));
        }
        if (errFd >= 0) {
            dup2(errFd, 2);
            dup2(errFd, 1);
            if (errFd != 1 && errFd != 2) close(errFd);
            setvbuf(stderr, nullptr, _IOLBF, 0);
            LOGI("[CHILD] native stderr redirected to %s", chosenPath);
            // WESTLAKE 2026-07-22 (§145): OH's libdfx/musl-sigchain handler converts faults into a
            // clean exit(1) and may write no cppcrash, so a fault can masquerade as a normal exit
            // (this misled the investigation twice). Install our own handler FIRST so every fatal
            // signal is reported with its address before anything can swallow it, then re-raise.
            if (getenv("ASX_CHILD_FATAL_SIGNAL_DIAG") != nullptr) {
                struct sigaction wl_sa;
                memset(&wl_sa, 0, sizeof(wl_sa));
                wl_sa.sa_flags = SA_SIGINFO;
                wl_sa.sa_sigaction = [](int sig, siginfo_t* si, void*) {
                    char buf[192];
                    int n = snprintf(buf, sizeof(buf),
                                     "\n[WESTLAKE-FATALSIG] signal=%d code=%d addr=%p pid=%d\n",
                                     sig, si != nullptr ? si->si_code : 0,
                                     si != nullptr ? si->si_addr : nullptr, (int)getpid());
                    ssize_t ignored = write(2, buf, n); (void)ignored;
                    signal(sig, SIG_DFL);
                    raise(sig);
                };
                for (int wl_s : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) {
                    sigaction(wl_s, &wl_sa, nullptr);
                }
                fprintf(stderr, "[WESTLAKE-FATALSIG] handlers installed\n"); fflush(stderr);
                // WESTLAKE §330: the sigaction() handlers above NEVER FIRE — OHOS musl INTERCEPTS
                // sigaction(), so ART/sigchain remains the real handler and our report is skipped
                // (proven in-process in §324: install returned rc=0 with prev=libart, yet ~26k
                // faults/s flowed past unseen). musl exports the same sigchain API ART uses, and
                // "special" handlers run BEFORE the user sigaction — register there too so a fatal
                // signal is ALWAYS reported with its address/PC before anything can swallow it.
                // Returns false = report only, never claim the fault, chain unchanged.
                {
                    struct wl_sig_action {
                        bool (*sca_sigaction)(int, siginfo_t*, void*);
                        sigset_t sca_mask;
                        int sca_flags;
                    };
                    typedef void (*add_ssh_t)(int, struct wl_sig_action*);
                    add_ssh_t wl_add = (add_ssh_t) dlsym(RTLD_DEFAULT, "add_special_signal_handler");
                    if (wl_add != nullptr) {
                        static struct wl_sig_action wl_acts[2];
                        int wl_i = 0;
                        for (int wl_s : {SIGSEGV, SIGBUS}) {
                            struct wl_sig_action* a = &wl_acts[wl_i++];
                            memset(a, 0, sizeof(*a));
                            sigemptyset(&a->sca_mask);
                            a->sca_flags = 0;
                            a->sca_sigaction = [](int sig, siginfo_t* si, void* ctx) -> bool {
                                static volatile int wl_n = 0;
                                // Keep enough samples to include late lifecycle faults. Toutiao
                                // legitimately triggers several managed implicit-null checks
                                // before its window is shown; a cap of six hid the terminal wake
                                // fault that followed them.
                                if (wl_n < 2048) {
                                    int k = wl_n++;
                                    unsigned long pc = 0, lr = 0;
                                    ucontext_t* uc = (ucontext_t*) ctx;
                                    if (uc != nullptr) {
                                        pc = (unsigned long) uc->uc_mcontext.pc;
                                        lr = (unsigned long) uc->uc_mcontext.regs[30];
                                    }
                                    // §330b: resolve PC to a library+offset with dladdr — the child
                                    // dies too fast to snapshot /proc/<pid>/maps from a shell, and raw
                                    // ASLR addresses are useless across runs.
                                    Dl_info wl_di;
                                    const char* wl_lib = "?"; unsigned long wl_off = 0;
                                    if (dladdr((void*) pc, &wl_di) != 0 && wl_di.dli_fname != nullptr) {
                                        wl_lib = wl_di.dli_fname;
                                        const char* sl = strrchr(wl_lib, '/');
                                        if (sl != nullptr) wl_lib = sl + 1;
                                        wl_off = pc - (unsigned long) wl_di.dli_fbase;
                                    }
                                    char b[320];
                                    int n2 = snprintf(b, sizeof b,
                                        "[WESTLAKE-CHILDSEGV] #%d sig=%d code=%d addr=%p pc=%#lx (%s+%#lx) sym=%s tid=%d\n",
                                        k, sig, si ? si->si_code : -1, si ? si->si_addr : nullptr,
                                        pc, wl_lib, wl_off,
                                        (dladdr((void*) pc, &wl_di) != 0 && wl_di.dli_sname) ? wl_di.dli_sname : "-",
                                        (int) gettid());
                                    if (n2 > 0) { ssize_t w = write(2, b, (size_t) n2); (void) w; }
                                    // §525: turn "somewhere in an interface call" into a named site
                                    if (sig == 11) wl_name_java_method(ctx);
                                    // frame-pointer walk (aarch64: [fp]=caller fp, [fp+8]=ret addr)
                                    if (uc != nullptr) {
                                        unsigned long fp = (unsigned long) uc->uc_mcontext.regs[29];
                                        for (int d = 0; d < 12 && fp != 0 && (fp & 7) == 0; ++d) {
                                            unsigned long nx = *(unsigned long*) fp;
                                            unsigned long ra = *(unsigned long*) (fp + 8);
                                            if (ra == 0) break;
                                            Dl_info fdi;
                                            const char* flib = "?"; unsigned long foff = 0;
                                            const char* fsym = "-";
                                            if (dladdr((void*) ra, &fdi) != 0 && fdi.dli_fname != nullptr) {
                                                flib = fdi.dli_fname;
                                                const char* s2 = strrchr(flib, '/');
                                                if (s2 != nullptr) flib = s2 + 1;
                                                foff = ra - (unsigned long) fdi.dli_fbase;
                                                if (fdi.dli_sname != nullptr) fsym = fdi.dli_sname;
                                            }
                                            char fb[256];
                                            int fl = snprintf(fb, sizeof fb,
                                                    "[WESTLAKE-CHILDSEGV]   fr%02d ra=%#lx (%s+%#lx) %s\n",
                                                    d, ra, flib, foff, fsym);
                                            if (fl > 0) { ssize_t w2 = write(2, fb, (size_t) fl); (void) w2; }
                                            if (nx <= fp) break;
                                            fp = nx;
                                        }
                                    }
                                }
                                return false;
                            };
                            wl_add(wl_s, a);
                        }
                        const char* m = "[WESTLAKE-FATALSIG] sigchain special handlers registered\n";
                        ssize_t w = write(2, m, strlen(m)); (void) w;
                    } else {
                        const char* m = "[WESTLAKE-FATALSIG] add_special_signal_handler NOT FOUND\n";
                        ssize_t w = write(2, m, strlen(m)); (void) w;
                    }
                }
            }
        } else {
            LOGE("[CHILD] all stderr redirect paths failed; ART abort msgs lost");
        }
    }

    // Prepare the installer-owned profiled-JIT input while the fork child still
    // has permission to modify the staged APK directory. This is a no-op unless
    // APPSPAWNX_PROFILED_JIT=1 was explicitly selected for the parent runtime.
    wl_prepare_profiled_jit_link(msg);

    // 2026-05-02 G2.14n+: mark this process as the child so that the lazy
    // Typeface init path in libhwui's typeface_minimal_stub (calling
    // oh_create_default_skia_handle in liboh_hwui_shim.so) is allowed to do
    // real Skia work.  In the parent process, this flag stays false and the
    // shim refuses to call SkFontMgr_New_OHOS — which would spawn IPC worker
    // threads that ZygoteHooks.preFork()'s waitUntilAllThreadsStopped()
    // would hang on forever.
    //
    // Resolved via dlsym (not direct link) to avoid creating a load-time
    // dependency from appspawn-x → liboh_hwui_shim.so (the shim is loaded
    // transitively via libhwui via liboh_adapter_bridge.so).
    {
        using MarkFn = void (*)(void);
        // liboh_hwui_shim is intentionally RTLD_LOCAL: exporting the OH/Skia
        // boundary into the process-global scope would let unrelated APK DSOs
        // bind to it. RTLD_DEFAULT therefore cannot see the marker on OH musl.
        // Resolve it from the owning DSO while preserving that isolation.
        void* owner = dlopen("liboh_hwui_shim.so", RTLD_NOLOAD | RTLD_NOW);
        if (owner == nullptr) {
            owner = dlopen("liboh_hwui_shim.so", RTLD_NOW | RTLD_LOCAL);
        }
        MarkFn fn = reinterpret_cast<MarkFn>(owner != nullptr
            ? dlsym(owner, "oh_typeface_mark_child")
            : dlsym(RTLD_DEFAULT, "oh_typeface_mark_child"));
        if (fn) {
            fn();
            LOGI("[CHILD] oh_typeface_mark_child() invoked");
        } else {
            const char* error = dlerror();
            LOGW("[CHILD] oh_typeface_mark_child unavailable — Typeface "
                 "real-init won't fire (font rendering will degrade): %s",
                 error != nullptr ? error : "unknown loader error");
        }
    }

    // G2.14h (2026-05-01): apply AccessToken FIRST, while still in appspawn:s0
    // domain AND still root uid. SetSelfTokenID requires either uid==0 or
    // CAP_SYS_RESOURCE; both are dropped by applyDac, both are restricted by
    // applySELinux's transition to normal_hap:s0. OH's stock appspawn applies
    // token first (see appspawn_service.c init child sequence).
    //
    // Step 1: Apply OH AccessToken (must run while uid==0 + appspawn:s0)
    int ret = applyAccessToken(msg);
    if (ret != 0) {
        LOGE("applyAccessToken failed, ret=%d – aborting child", ret);
        _exit(13);
    }

    // Join the task profile before DAC/SELinux specialization; all later ART
    // and application threads inherit it without app access to cgroup files.
    ret = wl_prepare_app_cpu_profile();
    if (ret != 0) {
        LOGE("wl_prepare_app_cpu_profile failed, ret=%d – aborting child", ret);
        _exit(15);
    }

    // Step 2: Apply DAC credentials (must be done early, before sandbox)
    ret = applyDac(msg);
    if (ret != 0) {
        LOGE("applyDac failed, ret=%d – aborting child", ret);
        _exit(10);
    }

    // Step 3: Set up filesystem sandbox
    ret = applySandbox(msg);
    if (ret != 0) {
        LOGE("applySandbox failed, ret=%d – aborting child", ret);
        _exit(11);
    }

    // Step 4: Set SELinux context
    ret = applySELinux(msg);
    if (ret != 0) {
        LOGE("applySELinux failed, ret=%d – aborting child", ret);
        _exit(12);
    }

    // Step 4.25: the OH token and SELinux transition are complete, so the
    // Android app no longer needs the privileged capability state inherited
    // from appspawn.  Do this before postForkChild restarts ART daemon threads
    // so every subsequently-created app/WebView worker inherits the bounded
    // priority and capability state.
    ret = wl_finalize_app_privileges();
    if (ret != 0) {
        LOGE("wl_finalize_app_privileges failed, ret=%d – aborting child", ret);
        _exit(15);
    }

    // Step 4.5 (B.33 2026-04-29): ZygoteHooks.postForkChild + postForkCommon.
    // Order matters: must run AFTER applySELinux/applyAccessToken (steps 3-4)
    // because Daemons.startPostZygoteFork inside postForkCommon spawns ART
    // daemon threads, and setcon in applySELinux requires the process to be
    // single-threaded (kernel returns EPERM otherwise → -SELINUX_SET_CONTEXT_ERROR
    // rc=-7).  But MUST run BEFORE any env->CallStaticVoidMethod in initChild,
    // since without postForkChild the child's ART state is inconsistent (locks
    // held by dead parent daemon TIDs) and Java calls deadlock in epoll_wait
    // on dead daemon notifications.
    if (runtime->zygotePostForkChild() != 0) {
        LOGW("zygotePostForkChild non-zero — daemon threads may be missing in child");
    }
    // 2026-07-09 BISECT: the child dies DURING CallStaticVoidMethod(initChild) (no
    // [CM-CK] return), right after a Throwable.printStackTrace — smells like a daemon
    // thread restarted here exit_group'ing. Gate off the daemon-restart to test: if
    // the child then reaches J_initChild_TOP, the restarted daemon is the killer.
    // postForkCommon is REQUIRED (skipping it → child dies at J_initChild_TOP, the
    // B.33 epoll_wait/dead-daemon-lock issue). WITH it, the child reaches deep into
    // initChild (installActivityManagerStub). It is NOT the fork-path killer.
    if (runtime->zygotePostForkCommon() != 0) {
        LOGW("zygotePostForkCommon non-zero — daemon restart may be incomplete");
    }

    // Replace child boot entrypoints immediately after ART's post-fork repair,
    // but do not create compiler workers yet. Child-only JNI/framework
    // registrations are restored below and must exist before boot callers can
    // be compiled.
    if (const char* childJit = getenv("WESTLAKE_CHILD_JIT");
        childJit != nullptr && strcmp(childJit, "1") == 0) {
        using PrepareChildRuntime = int (*)();
        auto prepare = reinterpret_cast<PrepareChildRuntime>(
                dlsym(RTLD_DEFAULT, "WestlakePrepareChildRuntime"));
        if (prepare == nullptr) {
            LOGW("[CHILD-JIT] WestlakePrepareChildRuntime unavailable; using validated libart compatibility path");
        } else {
            int prepareRc = prepare();
            if (prepareRc != 0) {
                LOGE("[CHILD-JIT] child runtime preparation failed rc=%d", prepareRc);
                _exit(17);
            }
            LOGI("[CHILD-JIT] child boot execution policy prepared");
        }
    }

    // Framework compatibility services that own Java worker threads are
    // deliberately absent from the AppSpawn parent.  Starting them during
    // AndroidRuntime::startReg() leaves non-ART threads alive at preFork and
    // stock Runtime::PreZygoteFork correctly aborts.  Resolve the boundary
    // entry point only after specialization and the canonical ART post-fork
    // hooks, so every worker belongs exclusively to this app child.
    {
        using InstallPostForkServices = void (*)(JNIEnv*);
        dlerror();
        auto install = reinterpret_cast<InstallPostForkServices>(
                dlsym(RTLD_DEFAULT, "wl_install_post_fork_framework_services"));
        const char* installError = dlerror();
        if (install != nullptr) {
            install(runtime->getJNIEnv());
            LOGI("[CHILD] post-fork framework services installed");
        } else if (runtime->installPostForkFrameworkServices() == 0) {
            LOGI("[CHILD] post-fork framework services installed by appspawn fallback");
        } else {
            LOGW("[CHILD] post-fork framework services failed; bridge installer unavailable: %s",
                 installError != nullptr ? installError : "unknown dlsym error");
        }
    }

    // ART's stock ZygoteHooks.postForkChild path above owns JIT creation and
    // post-fork code-cache state. The optional benchmark observes that state;
    // it no longer edits Runtime/JitOptions fields or invokes CreateJit by ABI.
    if (const char* hb = getenv("APPSPAWNX_HEADLESS_BENCH"); hb && strcmp(hb, "1") == 0) {
        fprintf(stderr, "[JIT-BENCH] HEADLESS: using post-fork JIT, SKIPPING ActivityThread\n");
        fflush(stderr);
        usleep(4000 * 1000);          // let the runtime settle
        wl_jit_bench_start();
        for (;;) pause();             // keep the process alive for the bench thread
    }
    wl_jit_bench_start();

    // 2026-07-09: restart the sigchain re-assert thread in the forked CHILD, now that
    // postForkChild/setcon are done (child may be multi-threaded). This keeps ART's
    // fault handler on top of OHOS libdfx so the child's framework null/suspend-check
    // SEGVs are recovered by ART instead of libdfx exit(1)-ing (the initChild death).
    // Must run AFTER setcon (single-threaded requirement) — hence here, not in atfork.
    // Give the CHILD the SAME SIG_DFL {SEGV,BUS,ABRT,external} reclaimer that keeps
    // the PARENT alive (the killer is a SEGV that only manifests as a death when a
    // handler is installed; SIG_DFL neutralizes it). The re-assert thread doesn't
    // survive fork, so start a fresh one here in the forked app process.
    // 2026-07-09 EXPERIMENT: child SIG_DFL reclaimer DISABLED. It SIG_DFL'd SEGV/BUS
    // (breaking ART's inherited implicit-null-check fault recovery) and raced ART's
    // handler — a suspected cause of the variable-timing death. Let ART's inherited
    // fault handler stand alone in the child.
    if (getenv("ASX_CHILD_RECLAIMER")) {
        pthread_t childReclaimer;
        if (pthread_create(&childReclaimer, nullptr, [](void*) -> void* {
            for (;;) { signal(SIGSEGV, SIG_DFL); signal(SIGBUS, SIG_DFL); signal(SIGABRT, SIG_DFL);
                signal(SIGTERM, SIG_DFL); signal(SIGXCPU, SIG_DFL); usleep(20000); }
            return nullptr;
        }, nullptr) == 0) { pthread_detach(childReclaimer);
            fprintf(stderr, "[CHILD] SIG_DFL reclaimer started\n"); fflush(stderr); }
    } else {
        // Keep ART's fault handler IN FRONT of OHOS libdfx (which else intercepts the
        // framework's implicit-null-check SEGVs and exit(1)s the child). If ART's
        // NullPointerHandler gets the SEGV and the arm64 pc-edit survives sigreturn,
        // the null check is recovered and the child survives past installActivityManagerStub.
        void (*startReassert)() =
            reinterpret_cast<void(*)()>(dlsym(RTLD_DEFAULT, "SigchainStartReassert"));
        if (startReassert) { startReassert(); fprintf(stderr, "[CHILD] sigchain re-assert (ART front-of-chain)\n"); }
        else fprintf(stderr, "[CHILD] SigchainStartReassert not found\n");
        fflush(stderr);
    }

    // Step 5: Post-fork runtime initialization (OH IPC setup)
    runtime->onChildInit();

    // Step 6: Initialize adapter layer (OHEnvironment.initialize)
    JNIEnv* env = runtime->getJNIEnv();
    if (!env) {
        LOGE("JNIEnv is null after onChildInit – aborting child");
        _exit(14);
    }

    // 2026-07-09: re-run AndroidRuntime::startReg in the CHILD. The child fork-inherits
    // MOST of the parent's framework-native registrations, but LOSES some (notably
    // android.os.Binder.clearCallingIdentity + adapter natives) → ActivityThread.main
    // dies with UnsatisfiedLinkError deep in the launch. Re-registering here (dlsym the
    // same exported startReg the parent uses) restores them. RegisterNatives overwrites,
    // so this is idempotent for the ones already inherited.
    {
        // 2026-07-11: MUST use RTLD_NOLOAD (bridge already loaded via System.loadLibrary)
        // to fetch the EXISTING handle. Plain RTLD_NOW forces a fresh full-resolution
        // pass that FAILS on arm64 (some global-scope symbol is unresolved under NOW) →
        // dlopen returns null → startReg is silently skipped → MessageQueue/ApkAssets
        // natives never get registered → ActivityThread.main dies with UnsatisfiedLinkError.
        // The parent uses the same RTLD_NOLOAD|RTLD_NOW trick (appspawnx_runtime.cpp:636).
        void* libRt = dlopen("liboh_adapter_bridge.so", RTLD_NOLOAD | RTLD_NOW);
        if (!libRt) libRt = dlopen("liboh_adapter_bridge.so", RTLD_NOLOAD | RTLD_LAZY);
        if (!libRt) libRt = dlopen(nullptr, RTLD_LAZY);
        fprintf(stderr, "[CHILD] startReg re-register: libRt=%p (%s)\n",
                libRt, libRt ? "ok" : dlerror()); fflush(stderr);
        if (libRt) {
            using StartReg_t = int (*)(JNIEnv*);
            auto childStartReg = reinterpret_cast<StartReg_t>(
                dlsym(libRt, "_ZN7android14AndroidRuntime8startRegEP7_JNIEnv"));
            if (childStartReg) {
                int rc = childStartReg(env);
                if (env->ExceptionCheck()) env->ExceptionClear();
                fprintf(stderr, "[CHILD] re-ran startReg in child, rc=%d\n", rc); fflush(stderr);
            } else {
                fprintf(stderr, "[CHILD] startReg symbol not found for child re-register\n");
                fflush(stderr);
            }
        }
    }

    // startReg above is the last post-fork registration pass before adapter
    // initialization and application Java. Repair portable runtime natives
    // here, after any zygote/oat entrypoint reset and before JIT workers can
    // compile their callers.
    wl_rebind_portable_timezone(env);

    ret = initAdapterLayer(env, runtime);
    if (ret != 0) {
        LOGW("initAdapterLayer failed, ret=%d – continuing anyway", ret);
        // Non-fatal: the app may work without the adapter in some cases
    }

    // All child-only native registrations and OH adapter initialization now
    // exist. Start JIT workers only at this point, before application Java is
    // launched, so compiled boot callers cannot observe null native targets.
    if (const char* childJit = getenv("WESTLAKE_CHILD_JIT");
        childJit != nullptr && strcmp(childJit, "1") == 0) {
        using EnsureChildJit = int (*)();
        auto ensureJit = reinterpret_cast<EnsureChildJit>(
                dlsym(RTLD_DEFAULT, "WestlakeEnsureChildJit"));
        int jitRc = ensureJit != nullptr
            ? ensureJit()
            : wl_create_jit_after_fork_compat();
        if (jitRc != 0) {
            LOGE("[CHILD-JIT] JIT creation failed rc=%d", jitRc);
            _exit(19);
        }
        if (ensureJit != nullptr) {
            LOGI("[CHILD-JIT] application JIT active after child registrations");
        }

        // Direct APK launches bypass the Android package-installer service
        // that normally supplies ART's profile metadata. Register only after
        // the post-fork JIT exists, and before application bytecode starts.
        wl_register_app_profile(env, msg);
    }

    // Step 7: Set process name for debugging (shows in ps output)
    if (!msg.procName.empty()) {
        prctl(PR_SET_NAME, msg.procName.c_str(), 0, 0, 0);
    }

    // Step 8: Launch the Android ActivityThread event loop
    LOGI("Launching ActivityThread for %s", msg.procName.c_str());
    // WESTLAKE (arm64 board, 2026-07-21) — run it on a BIG-STACK pthread.
    //
    // musl reports only __default_stacksize (128 KB) from pthread_getattr_np() for the
    // INITIAL thread, regardless of the real 8 MB rlimit stack.  ART's Thread::InitStackHwm
    // believes it, so anything deep on the child's main thread dies with
    //   "java.lang.StackOverflowError: stack size 124KB"   (128 KB - guard)
    // — observed killing android.graphics.Typeface.<clinit> on its font-mmap path
    // (MappedByteBuffer/DirectByteBuffer), which then leaves sDefaultTypeface null and
    // fails ensureBindApplication.  Patching the ELF PT_GNU_STACK and `ulimit -s` both
    // failed to move it (OHOS musl doesn't raise __default_stacksize from either).
    // A pthread created with an explicit stacksize DOES get an honest attr, so run the
    // whole ActivityThread/Looper on one.  It never returns, so just join it.
    {
        struct LaunchCtx {
            const SpawnMsg* msg; AppSpawnXRuntime* runtime;
        };
        static LaunchCtx ctx{&msg, runtime};   // enclosing fn is static: no `this`
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        // §565: THIS is the stack ART reports as "stack size 8182KB" when the JIT starts
        // executing compiled frames -- it is an explicitly-sized pthread, not the process main
        // thread, which is why -Xss and RLIMIT_STACK (16 MB here) both had no effect.
        // ★codex's reading is the key one: ART turns the guard-page fault into StackOverflowError
        // by writing art_quick_throw_stack_overflow into the saved PC, so a CLEAN Java exception is
        // proof the handler and sigreturn worked -- the overflow is REAL, not spurious. Compiled
        // frames simply need more stack than the interpreter did.
        size_t javaStackMb = 8;
        if (const char* v = getenv("APPSPAWNX_JAVA_STACK_MB"); v && *v) {
            long m = strtol(v, nullptr, 10);
            if (m >= 1 && m <= 512) javaStackMb = (size_t)m;
        }
        fprintf(stderr, "[CM-BIGSTACK] java thread stack = %zu MB\n", javaStackMb); fflush(stderr);
        pthread_attr_setstacksize(&attr, javaStackMb * 1024 * 1024);
        pthread_t javaTid;
        int prc = pthread_create(&javaTid, &attr, [](void* a) -> void* {
            auto* c = static_cast<LaunchCtx*>(a);
            wl_prioritize_activity_thread();
            JNIEnv* tenv = nullptr;
            JavaVM* vm = c->runtime->getJavaVM();
            if (vm != nullptr && vm->AttachCurrentThread(&tenv, nullptr) != JNI_OK) {
                fprintf(stderr, "[CM-BIGSTACK] AttachCurrentThread FAILED\n"); fflush(stderr);
                return nullptr;
            }
            size_t ss = 0; pthread_attr_t ga;
            if (pthread_getattr_np(pthread_self(), &ga) == 0) {
                pthread_attr_getstacksize(&ga, &ss); pthread_attr_destroy(&ga);
            }
            fprintf(stderr, "[CM-BIGSTACK] ActivityThread on dedicated pthread, stack=%zu bytes\n", ss);
            fflush(stderr);
            ChildMain::launchActivityThread(tenv, *c->msg, c->runtime);
            return nullptr;
        }, &ctx);
        pthread_attr_destroy(&attr);
        if (prc == 0) {
            // The fork child starts out attached to ART and therefore registered as a
            // runnable mutator.  Once ActivityThread has moved to javaTid, this thread
            // does nothing but block forever in pthread_join().  Leaving it attached
            // makes a stop-the-world GC wait for a checkpoint that this native joiner
            // can never run; a cold Toutiao launch then deadlocks in
            // Heap::WaitForGcToCompleteLocked while registering an app dex file.
            // Detaching is the JNI-defined way to remove this otherwise-unused thread
            // from ART's mutator list.  No JNI state is used after a successful spawn.
            JavaVM* vm = runtime->getJavaVM();
            jint detachRc = vm != nullptr ? vm->DetachCurrentThread() : JNI_ERR;
            fprintf(stderr,
                    "[CM-BIGSTACK] launcher DetachCurrentThread before join rc=%d\n",
                    static_cast<int>(detachRc));
            fflush(stderr);
            pthread_join(javaTid, nullptr);
        } else {
            fprintf(stderr, "[CM-BIGSTACK] pthread_create failed (%d) — falling back to main thread\n", prc);
            fflush(stderr);
            wl_prioritize_activity_thread();
            launchActivityThread(env, msg, runtime);
        }
    }

    // Should never reach here – launchActivityThread enters an infinite loop
    LOGE("launchActivityThread returned unexpectedly – exiting");
    fprintf(stderr, "[CM-EXIT] launchActivityThread RETURNED — child_main:_exit(1)\n"); fflush(stderr);
    _exit(1);
}

// ---------------------------------------------------------------------------
// applyDac  –  set UID, GID, and supplementary groups
// ---------------------------------------------------------------------------
int ChildMain::applyDac(const SpawnMsg& msg) {
    LOGI("Applying DAC: uid=%d gid=%d gids_count=%zu",
         msg.uid, msg.gid, msg.gids.size());

    // [NET-FIX 2026-07-04] Grant internet at the spawn boundary, the OHOS-native
    // way. Adapter (Android) apps never enter OHOS's permission system — they
    // install from an APK with no OHOS bundle config, so their access-token holds
    // no ohos.permission.INTERNET, and native appspawn/AMS therefore DENY them
    // (every socket()/getaddrinfo -> EPERM, crashing networked apps). Android apps
    // universally expect INTERNET, so grant it here for adapter children exactly as
    // native appspawn does for an OHOS app that holds INTERNET: add the inet
    // supplementary gids, grant access to OH's dnsproxyd socket through the
    // netsys_socket gid, and (still fully root, below) set the netsys eBPF
    // oh_sock_permission_map[uid]=1 — the actual per-uid socket gate.  Without
    // netsys_socket, a capability-free child gets -EACCES from
    // NetSysGetResolvConf and musl collapses every hostname to EAI_NONAME even
    // though raw native sockets work.
    std::vector<gid_t> gidArray(msg.gids.begin(), msg.gids.end());
    for (gid_t g : {static_cast<gid_t>(1097) /*OH netsys_socket*/,
                    static_cast<gid_t>(3003) /*AID_INET*/,
                    static_cast<gid_t>(3004) /*AID_NET_RAW*/}) {
        if (std::find(gidArray.begin(), gidArray.end(), g) == gidArray.end()) {
            gidArray.push_back(g);
        }
    }
    if (setgroups(gidArray.size(), gidArray.data()) < 0) {
        LOGE("setgroups(%zu groups) failed: %s", gidArray.size(), strerror(errno));
        return -1;
    }
    LOGD("Set %zu supplementary groups (incl netsys_socket 1097, inet 3003/3004)",
         gidArray.size());

    // Grant the netsys socket-permission map for this uid while still fully root
    // (before setresgid/setresuid). The eBPF map is the real socket gate and the
    // grant persists for the process's lifetime. The child is single-threaded here
    // (ART daemon threads restart later, in zygotePostForkChild) and no seccomp is
    // applied until applySandbox, so fork+exec of the existing bpfgrant tool is
    // safe. Best-effort: a failure only means the app falls back to its offline UI.
    if (msg.uid >= 0) {
        pid_t gp = fork();
        if (gp == 0) {
            char uidStr[16];
            snprintf(uidStr, sizeof(uidStr), "%d", msg.uid);
            execl("/system/bin/bpfgrant", "bpfgrant", uidStr, "oh_sock_permission_map",
                  static_cast<char*>(nullptr));
            _exit(127);
        } else if (gp > 0) {
            int st = 0;
            waitpid(gp, &st, 0);
            LOGI("[NET-FIX] granted oh_sock_permission_map[%d]=1 at spawn (rc=%d)",
                 msg.uid, st);
        } else {
            LOGW("[NET-FIX] fork for bpfgrant failed: %s — internet may be denied",
                 strerror(errno));
        }
    }

    // WESTLAKE §364: optionally KEEP the privileged token.
    // Measured chain: our SSM sub-session is only mounted under an app WindowScene, and the only
    // WindowScene we can borrow belongs to noice's ArkTS ability — which AMS then kills with
    // `LIFECYCLE_TIMEOUT / ability:EntryAbility foreground timeout` about 5s after we create the
    // session (proven: the identical run with sessions disabled produces no sysfreeze at all), and
    // when it dies its WindowScene and every node under it — ours included — is destroyed.
    // A privileged caller does not need to borrow anything: SSM's CheckSystemWindowPermission and
    // RS's CheckCreateNodeAndSurface both branch on IsSystemCalling()/isNonSystemAppCalling, so with
    // a native token we can ask for an APP_MAIN_WINDOW that owns its own WindowScene.
    // Dev-board bring-up aid, opt-in only.
    if (getenv("WL_NO_DROP_UID") != nullptr) {
        LOGI("[WESTLAKE-§364] WL_NO_DROP_UID set - keeping privileged uid/gid (uid=%d)", getuid());
        fprintf(stderr, "[WESTLAKE-KEEPUID] staying uid=%d gid=%d\n", getuid(), getgid());
        fflush(stderr);
        return 0;
    }

    // Any privileged helper exec is complete. Drop the bounding set while
    // CAP_SETPCAP is still effective, before setresuid clears capabilities.
    if (wl_drop_capability_bounding_set() != 0) return -1;

    // Set primary GID (must be done before setuid to avoid permission issues)
    if (msg.gid >= 0) {
        if (setresgid(msg.gid, msg.gid, msg.gid) < 0) {
            LOGE("setresgid(%d) failed: %s", msg.gid, strerror(errno));
            return -1;
        }
        LOGD("Set GID to %d", msg.gid);
    }

    // Set UID (this drops root privileges – do this last)
    if (msg.uid >= 0) {
        if (setresuid(msg.uid, msg.uid, msg.uid) < 0) {
            LOGE("setresuid(%d) failed: %s", msg.uid, strerror(errno));
            return -1;
        }
        LOGD("Set UID to %d", msg.uid);
    }

    // Verify we actually dropped root
    if (msg.uid > 0 && getuid() == 0) {
        LOGE("Failed to drop root – uid is still 0");
        return -1;
    }

    // Disable ability to regain root via setuid binaries
    if (msg.uid > 0) {
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
            LOGW("prctl(PR_SET_NO_NEW_PRIVS) failed: %s", strerror(errno));
            // Non-fatal
        }
    }

    LOGI("DAC applied: running as uid=%d gid=%d", getuid(), getgid());
    return 0;
}

// ---------------------------------------------------------------------------
// applySandbox  –  create mount namespace and set up filesystem isolation
// ---------------------------------------------------------------------------
int ChildMain::applySandbox(const SpawnMsg& msg) {
    // Skip sandbox if NO_SANDBOX flag is set (for debugging)
    if (msg.hasFlag(StartFlags::NO_SANDBOX)) {
        LOGW("Sandbox disabled by NO_SANDBOX flag");
        return 0;
    }

    LOGI("Setting up sandbox for %s", msg.bundleName.c_str());

    // TODO: Implement Android-specific sandbox mounts:
    //
    // 1. Create new mount namespace:
    //    unshare(CLONE_NEWNS)
    //
    // 2. Make root mount private to prevent propagation:
    //    mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL)
    //
    // 3. Bind mount APK directory for app code access:
    //    mount("/data/app/<bundleName>/", "<sandbox>/app/", NULL, MS_BIND, NULL)
    //
    // 4. Bind mount app data directory:
    //    mount("/data/data/<bundleName>/", "<sandbox>/data/", NULL, MS_BIND, NULL)
    //
    // 5. Mount tmpfs for /dev and create minimal device nodes
    //
    // 6. Bind mount shared libraries:
    //    mount("/system/lib64/", "<sandbox>/system/lib64/", NULL, MS_BIND | MS_RDONLY, NULL)
    //
    // 7. Apply OH sandbox profile from JSON config
    //
    // In production, this links against OH appspawn sandbox library:
    //   - SetAppSandboxProperty(msg)
    //   - AppSpawnSandboxCfg_Parse(configPath)

    LOGD("Sandbox setup: TODO – filesystem isolation not yet implemented");
    LOGD("  APK path: %s", msg.apkPath.c_str());
    LOGD("  Native libs: %s", msg.nativeLibPaths.c_str());
    LOGD("  Bundle data: /data/data/%s/", msg.bundleName.c_str());

    return 0;
}

// ---------------------------------------------------------------------------
// applySELinux  –  set the SELinux security context for the process
// ---------------------------------------------------------------------------
int ChildMain::applySELinux(const SpawnMsg& msg) {
    // B.30 (2026-04-29): real adaptation via OH HapContext::HapDomainSetcontext.
    //
    // Prior B.29 attempt failed because child stayed in u:r:appspawn:s0 (parent
    // domain) and couldn't query OH SA 180 (DeviceInfo) / SA 501 (BMS) /
    // SA 4607 (WindowSession) — all selinux denied.  AMS LIFECYCLE_HALF_TIMEOUT
    // 5s killed the child.
    //
    // OH's libhap_restorecon resolves apl + packageName + hapFlags + uid →
    // app domain context (e.g. u:r:normal_hap:s0:c<uid>) via the same lookup
    // table OH native appspawn uses (sehap_contexts file).  After setcon the
    // child has SA query permissions appropriate for its APL.

    // Default APL if TLV didn't supply one (text-format spawn fallback).
    std::string apl = msg.apl;
    if (apl.empty()) {
        apl = "normal";
    }

    ::HapDomainInfo info;
    info.apl = apl;
    info.packageName = msg.bundleName;
    info.hapFlags = msg.hapFlags;
    info.uid = static_cast<uint32_t>(msg.uid);

    LOGI("applySELinux: apl=%s pkg=%s hapFlags=%llu uid=%u",
         info.apl.c_str(), info.packageName.c_str(),
         static_cast<unsigned long long>(info.hapFlags), info.uid);

    ::HapContext hapContext;
    int rc = hapContext.HapDomainSetcontext(info);
    if (rc != 0) {
        LOGE("HapDomainSetcontext failed rc=%{public}d errno=%{public}d (apl=%{public}s pkg=%{public}s) — child remains in %{public}s",
             rc, errno, info.apl.c_str(), info.packageName.c_str(), "u:r:appspawn:s0");
        return rc;
    }
    LOGI("applySELinux: child secon transitioned successfully (apl=%s)",
         info.apl.c_str());
    return 0;
}

// ---------------------------------------------------------------------------
// applyAccessToken  –  set OH AccessToken for permission enforcement
// ---------------------------------------------------------------------------
int ChildMain::applyAccessToken(const SpawnMsg& msg) {
    // B.30 (2026-04-29): real adaptation via OH SetSelfTokenID
    // (libtokensetproc_shared).  AMS ships the AccessTokenIdEx via TLV;
    // applying it lets OH SAs (BMS / DeviceInfo / WindowSession) authorize
    // this process for permissions granted to bundleName at install time.
    //
    // Without this, OH IPC checks (samgr->GetSystemAbility) succeed past
    // SELinux but fail at AccessToken layer with "permission denied" errors.

    // G2.14h (2026-05-01) — when TLV has 0, query AccessTokenKit directly.
    //
    // Why TLV has 0 even after libbms v3 patch (which calls AllocHapToken at install
    // and writes accessTokenId into both top-level applicationInfo and
    // InnerBundleUserInfo for userId 0/100):
    //   AppMS::StartProcess at app_mgr_service_inner.cpp:4814 OVERRIDES the
    //   bundleInfo.applicationInfo.accessTokenId/Ex with the result of
    //   AccessTokenKit::GetHapTokenIDEx(GetUserIdByUid(uid), bundleInfo.name, appIndex).
    //   Empirically that GetHapTokenIDEx call returns 0 for our HelloWorld even
    //   though `atm dump -t -b com.example.helloworld` shows the token registered
    //   at install time. Cause not yet pinned (caller-permission? IPC race?), but
    //   the kit call from CHILD context returns the correct registered token, so
    //   we use that as a self-heal path.
    //
    // History:
    //   2026-04-29 B.31 probe verified: OH AppMS DID send OH_TLV_ACCESS_TOKEN_INFO,
    //   but the value is genuinely 0 (libapk_installer never calls
    //   AccessTokenKit::AllocHapToken at install time).
    //
    // Earlier (pre-G2.14g) behavior was to early-return when 0, leaving the
    // child with appspawn-x's INHERITED system token. This caused
    // JudgeSelfCalled mismatch in OH AMS:
    //   * IPCSkeleton::GetCallingTokenID() returns child's inherited (non-zero) token
    //   * abilityRecord->GetApplicationInfo().accessTokenId stored = 0 (from BMS)
    //   * 非零 != 0 → CHECK_PERMISSION_FAILED (rc=2097177)
    //   * Symptom: AbilityTransitionDone(state=FOREGROUND) rejected → AMS times out
    //     ability load → 30s LIFECYCLE_TIMEOUT kills HelloWorld.
    //
    // Fix: explicitly SetSelfTokenID(0) so child's calling token matches
    // AMS-stored 0. JudgeSelfCalled then sees callingTokenId == tokenID
    // (both 0) and returns true.
    //
    // Long-term P4 fix (still unaddressed): integrate AccessTokenKit into
    // libapk_installer so both BMS and child have a real non-zero HAP token.
    // For HelloWorld baseline, "both 0" is acceptable — JudgeSelfCalled
    // passes; OH services that gate by specific permissions still deny but
    // the lifecycle path is unblocked.
    if (msg.accessTokenIdEx == 0) {
        LOGW("applyAccessToken: accessTokenIdEx==0 in TLV — kernel will reject SetSelfTokenID; skipping "
             "(child keeps parent's inherited token; libbms v3 + AppMS bundleInfo override patch should fix)");
        return 0;
    }

    LOGI("applyAccessToken: id=0x%llx apl=%s",
         static_cast<unsigned long long>(msg.accessTokenIdEx),
         msg.apl.c_str());

    int rc = SetSelfTokenID(msg.accessTokenIdEx);
    if (rc != 0) {
        LOGE("SetSelfTokenID(0x%llx) failed rc=%d",
             static_cast<unsigned long long>(msg.accessTokenIdEx), rc);
        return 0;
    }
    LOGI("applyAccessToken: SetSelfTokenID(0x%llx) OK",
         static_cast<unsigned long long>(msg.accessTokenIdEx));
    return 0;
}

// ---------------------------------------------------------------------------
// initAdapterLayer  –  initialize the Android-OH adapter bridge
// ---------------------------------------------------------------------------
int ChildMain::initAdapterLayer(JNIEnv* env, AppSpawnXRuntime* runtime) {
    LOGI("Initializing adapter layer (OHEnvironment)");

    // Find OHEnvironment via the PathClassLoader that loaded it in the parent.
    // Using env->FindClass here would go through the bootstrap classloader,
    // producing a *different* class object whose <clinit> would re-attempt
    // System.loadLibrary("oh_adapter_bridge") — but the .so is already bound
    // to the parent's PathClassLoader, triggering UnsatisfiedLinkError:
    //   "already opened by ClassLoader 0x1cf; can't open in ClassLoader 0(null)"
    // Going through the cached PathClassLoader returns the same class the
    // parent already initialized (inherited via fork); <clinit> does not re-run.
    jclass ohEnvClass = runtime
        ? runtime->loadClassViaPath(env, "adapter.core.OHEnvironment")
        : env->FindClass("adapter/core/OHEnvironment");
    if (!ohEnvClass) {
        LOGW("OHEnvironment class not found – adapter layer not in classpath");
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        return -1;
    }

    // Get the static initialize() method
    jmethodID initMethod = env->GetStaticMethodID(
        ohEnvClass, "initialize", "()V");
    if (!initMethod) {
        LOGE("OHEnvironment.initialize() method not found");
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        env->DeleteLocalRef(ohEnvClass);
        return -1;
    }

    // Call OHEnvironment.initialize()
    // This loads liboh_adapter_bridge.so, sets up the OH service connections,
    // and registers the adapter service stubs
    env->CallStaticVoidMethod(ohEnvClass, initMethod);

    if (env->ExceptionCheck()) {
        LOGE("Exception during OHEnvironment.initialize():");
        env->ExceptionDescribe();
        env->ExceptionClear();
        env->DeleteLocalRef(ohEnvClass);
        return -1;
    }

    env->DeleteLocalRef(ohEnvClass);
    LOGI("Adapter layer initialized successfully");
    return 0;
}

// ---------------------------------------------------------------------------
// launchActivityThread  –  enter the Android app event loop
// ---------------------------------------------------------------------------
static bool populateChildSecureRandomServices(JNIEnv* env, jobject provider) {
    if (provider == nullptr) return false;

    jclass providerBase = env->FindClass("java/security/Provider");
    jclass serviceClass = env->FindClass("java/security/Provider$Service");
    jmethodID serviceCtor = serviceClass != nullptr
            ? env->GetMethodID(
                    serviceClass,
                    "<init>",
                    "(Ljava/security/Provider;Ljava/lang/String;Ljava/lang/String;"
                    "Ljava/lang/String;Ljava/util/List;Ljava/util/Map;)V")
            : nullptr;
    jmethodID putService = providerBase != nullptr
            ? env->GetMethodID(
                    providerBase,
                    "putService",
                    "(Ljava/security/Provider$Service;)V")
            : nullptr;
    const char* algorithms[] = {
        "SHA1PRNG", "NativePRNG", "NativePRNGNonBlocking", "DRBG"
    };
    for (const char* algorithmName : algorithms) {
        if (serviceCtor == nullptr || putService == nullptr || env->ExceptionCheck()) break;
        jstring type = env->NewStringUTF("SecureRandom");
        jstring algorithm = env->NewStringUTF(algorithmName);
        jstring implementation =
                env->NewStringUTF("adapter.compat.WestlakeSecureRandomSpi");
        jobject service = env->NewObject(
                serviceClass,
                serviceCtor,
                provider,
                type,
                algorithm,
                implementation,
                nullptr,
                nullptr);
        if (service != nullptr && !env->ExceptionCheck()) {
            env->CallVoidMethod(provider, putService, service);
        }
        if (service != nullptr) env->DeleteLocalRef(service);
        env->DeleteLocalRef(implementation);
        env->DeleteLocalRef(algorithm);
        env->DeleteLocalRef(type);
    }
    if (serviceClass != nullptr) env->DeleteLocalRef(serviceClass);
    if (providerBase != nullptr) env->DeleteLocalRef(providerBase);
    return !env->ExceptionCheck() && serviceCtor != nullptr && putService != nullptr;
}

static bool installChildSecurityProvider(JNIEnv* env, AppSpawnXRuntime* runtime) {
    // Source runtimes carry the platform Conscrypt provider on their boot class
    // path and install OhTrustBridge during preload. WestlakeJsseProvider is a
    // captured-stack replacement and is intentionally absent from source
    // adapter jars, so do not turn that expected absence into a child-startup
    // error on every launch.
    if (getenv("WESTLAKE_RUNTIME_ROOT") != nullptr) {
        fprintf(stderr,
                "[CHILD-SECURITY] source runtime uses Conscrypt/OhTrustBridge; "
                "legacy WestlakeJSSE skipped\n");
        return true;
    }

    jclass providerClass = runtime != nullptr
            ? runtime->loadClassViaPath(env, "adapter.compat.WestlakeJsseProvider")
            : env->FindClass("adapter/compat/WestlakeJsseProvider");
    if (providerClass == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        fprintf(stderr, "[CHILD-SECURITY] WestlakeJsseProvider class unavailable\n");
        return false;
    }

    jmethodID providerCtor = env->GetMethodID(providerClass, "<init>", "()V");
    jobject provider = providerCtor != nullptr
            ? env->NewObject(providerClass, providerCtor)
            : nullptr;
    // Provider.put(String,String) relies on lazy legacy-property parsing.
    // The verify-only boot image can leave that cache empty, so publish the
    // SecureRandom services through Provider.putService(), the canonical JCA
    // service map, before installing the provider.
    if (provider != nullptr && !env->ExceptionCheck()) {
        populateChildSecureRandomServices(env, provider);
    }
    jclass securityClass = env->FindClass("java/security/Security");
    jmethodID getProvider = securityClass != nullptr
            ? env->GetStaticMethodID(
                    securityClass,
                    "getProvider",
                    "(Ljava/lang/String;)Ljava/security/Provider;")
            : nullptr;
    jmethodID addProvider = securityClass != nullptr
            ? env->GetStaticMethodID(
                    securityClass, "addProvider", "(Ljava/security/Provider;)I")
            : nullptr;
    if (provider == nullptr || getProvider == nullptr || addProvider == nullptr ||
            env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (securityClass != nullptr) env->DeleteLocalRef(securityClass);
        if (provider != nullptr) env->DeleteLocalRef(provider);
        env->DeleteLocalRef(providerClass);
        fprintf(stderr, "[CHILD-SECURITY] provider construction failed\n");
        return false;
    }

    jstring providerName = env->NewStringUTF("WestlakeJSSE");
    jobject existingProvider = env->CallStaticObjectMethod(
            securityClass, getProvider, providerName);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        existingProvider = nullptr;
    }

    // The parent deliberately installs this provider before fork. Removing it
    // in the child needlessly rebuilds ProviderList and can traverse unloaded
    // lazy provider slots. Reuse the inherited object and refresh its canonical
    // service map; add only when no inherited provider exists.
    const bool reusedExisting = existingProvider != nullptr;
    jint position = -1;
    if (reusedExisting) {
        populateChildSecureRandomServices(env, existingProvider);
    } else if (!env->ExceptionCheck()) {
        position = env->CallStaticIntMethod(securityClass, addProvider, provider);
    }
    env->DeleteLocalRef(providerName);

    bool success = !env->ExceptionCheck() && (reusedExisting || position > 0);
    if (success) {
        jclass secureRandom = env->FindClass("java/security/SecureRandom");
        jmethodID getInstance = secureRandom != nullptr
                ? env->GetStaticMethodID(
                        secureRandom,
                        "getInstance",
                        "(Ljava/lang/String;)Ljava/security/SecureRandom;")
                : nullptr;
        jstring algorithm = env->NewStringUTF("SHA1PRNG");
        jobject random = getInstance != nullptr
                ? env->CallStaticObjectMethod(secureRandom, getInstance, algorithm)
                : nullptr;
        success = random != nullptr && !env->ExceptionCheck();
        if (random != nullptr) env->DeleteLocalRef(random);
        env->DeleteLocalRef(algorithm);
        if (secureRandom != nullptr) env->DeleteLocalRef(secureRandom);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    if (existingProvider != nullptr) env->DeleteLocalRef(existingProvider);
    env->DeleteLocalRef(securityClass);
    env->DeleteLocalRef(provider);
    env->DeleteLocalRef(providerClass);
    fprintf(stderr,
            "[CHILD-SECURITY] provider reused=%d position=%d SecureRandom.SHA1PRNG=%s\n",
            reusedExisting ? 1 : 0,
            static_cast<int>(position),
            success ? "OK" : "FAILED");
    fflush(stderr);
    return success;
}

void ChildMain::launchActivityThread(JNIEnv* env, const SpawnMsg& msg,
                                     AppSpawnXRuntime* runtime) {
    // Security.addProvider() in the zygote may update a JAR-verification
    // thread-local provider list on the preload worker.  Re-establish the
    // class-library provider on the actual child Java thread so stock ART and
    // SecureRandom see the same process contract after fork.
    installChildSecurityProvider(env, runtime);

    // Determine the target class to launch
    std::string targetClass = msg.targetClass;
    if (targetClass.empty()) {
        targetClass = "android.app.ActivityThread";
    }

    LOGI("Launching target class: %s (proc=%s, sdkVersion=%d)",
         targetClass.c_str(), msg.procName.c_str(), msg.targetSdkVersion);

    // Call AppSpawnXInit.initChild(procName, targetClass, targetSdkVersion)
    // This Java method performs:
    //   1. Set the process name (Process.setArgV0)
    //   2. Call RuntimeInit.commonInit() for thread handlers, timezone, etc.
    //   3. Find the target class and invoke its main() method
    //   4. For ActivityThread, this enters Looper.loop() which blocks forever

    // Prefer the parent-cached global ref (resolved via PathClassLoader at
    // runtime startup). Fall back to runtime->loadClassViaPath (also uses
    // the PathClassLoader) or plain FindClass if everything else failed.
    jclass initClass = nullptr;
    jclass cachedGlobal = runtime ? runtime->getAppSpawnXInitClass() : nullptr;
    if (cachedGlobal) {
        // Global ref is valid across fork; use it directly (no local ref needed
        // for invoking static methods, but we'll keep the pattern consistent).
        initClass = cachedGlobal;
    } else if (runtime) {
        initClass = runtime->loadClassViaPath(
            env, "com.android.internal.os.AppSpawnXInit");
    } else {
        initClass = env->FindClass("com/android/internal/os/AppSpawnXInit");
    }
    bool initClassIsLocal = (initClass != cachedGlobal);
    if (!initClass) {
        LOGE("AppSpawnXInit class not found – cannot launch ActivityThread");
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }

        // Fallback: try to launch ActivityThread.main() directly
        LOGW("Attempting direct ActivityThread.main() launch as fallback");

        // Convert dotted class name to JNI format (replace . with /)
        std::string jniClassName = targetClass;
        for (char& c : jniClassName) {
            if (c == '.') c = '/';
        }

        jclass targetJniClass = env->FindClass(jniClassName.c_str());
        if (!targetJniClass) {
            LOGE("Cannot find class %s", jniClassName.c_str());
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
            return;
        }

        jmethodID mainMethod = env->GetStaticMethodID(
            targetJniClass, "main", "([Ljava/lang/String;)V");
        if (!mainMethod) {
            LOGE("Cannot find main([String) in %s", jniClassName.c_str());
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
            env->DeleteLocalRef(targetJniClass);
            return;
        }

        // Create empty String[] for main() argument
        jclass stringClass = env->FindClass("java/lang/String");
        jobjectArray emptyArgs = env->NewObjectArray(0, stringClass, nullptr);

        LOGI("Calling %s.main() directly", targetClass.c_str());
        env->CallStaticVoidMethod(targetJniClass, mainMethod, emptyArgs);

        if (env->ExceptionCheck()) {
            LOGE("Exception in %s.main():", targetClass.c_str());
            env->ExceptionDescribe();
            env->ExceptionClear();
        }

        env->DeleteLocalRef(emptyArgs);
        env->DeleteLocalRef(stringClass);
        env->DeleteLocalRef(targetJniClass);
        return;
    }

    // Use AppSpawnXInit.initChild for proper initialization
    jmethodID initChildMethod = env->GetStaticMethodID(
        initClass, "initChild",
        "(Ljava/lang/String;Ljava/lang/String;I)V");
    if (!initChildMethod) {
        LOGE("AppSpawnXInit.initChild() method not found");
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        if (initClassIsLocal) env->DeleteLocalRef(initClass);
        return;
    }

    // 2026-07-09 DIRECT-LAUNCH: arm the BMS/AMS-free launch path for noice by
    // exporting env that AppSpawnXInit.initChild reads to post directLaunchNoBms.
    // apkPath uses msg.apkPath when the spawn carried it, else the staged default.
    if (msg.bundleName == "com.github.ashutoshgngwr.noice") {
        const char* apk = !msg.apkPath.empty() ? msg.apkPath.c_str()
                                               : "/data/local/tmp/asx/noice.apk";
        setenv("ASX_APK_PATH", apk, 1);
        setenv("ASX_LAUNCH_PKG", msg.bundleName.c_str(), 1);
        setenv("ASX_LAUNCH_ACTIVITY",
               "com.github.ashutoshgngwr.noice.activity.MainActivity", 1);
        setenv("ASX_DIRECT_LAUNCH", "1", 1);
        LOGI("[DIRECT-LAUNCH] armed for noice: apk=%s", apk);
    }

    // Convert arguments to Java strings
    jstring jProcName = env->NewStringUTF(msg.procName.c_str());
    jstring jTargetClass = env->NewStringUTF(targetClass.c_str());

    LOGI("Calling AppSpawnXInit.initChild(\"%s\", \"%s\", %d)",
         msg.procName.c_str(), targetClass.c_str(), msg.targetSdkVersion);
    LOGI("[CHILD_CK] CK_BEFORE_initChild_call (about to enter Java)");

    // 2026-07-09: force ART's fault handler to the front of the signal chain right
    // before the first post-fork Java call, in case libdfx re-installed its handler
    // during the child's init (postForkChild/onChildInit/adapter) and the 20ms
    // re-assert hasn't re-claimed it yet. This is the boundary where the child dies.
    {
        void (*ensureFront)(int) =
            reinterpret_cast<void(*)(int)>(dlsym(RTLD_DEFAULT, "EnsureFrontOfChain"));
        if (ensureFront) { ensureFront(SIGSEGV); ensureFront(SIGBUS); }
        fprintf(stderr, "[CHILD] EnsureFrontOfChain(SEGV/BUS) before initChild: %s\n",
                ensureFront ? "done" : "symbol-missing"); fflush(stderr);
    }
    // 2026-07-09 DIAG: is the initChild-entry death a SEGV or a clean exit? Force
    // SIG_DFL for SEGV/BUS just for this boundary — if a crash dump appears, it's a
    // fault (and names the PC); if the death stays clean (exit, no dump), it's a
    // different mechanism (ART clean-exit / abort). REMOVE after diagnosis.
    if (getenv("ASX_CHILD_SIGDFL_DIAG")) {
        signal(SIGSEGV, SIG_DFL);
        signal(SIGBUS, SIG_DFL);
        fprintf(stderr, "[CHILD] SIG_DFL SEGV/BUS diag armed before initChild\n"); fflush(stderr);
    }

    env->CallStaticVoidMethod(initClass, initChildMethod,
                              jProcName, jTargetClass,
                              static_cast<jint>(msg.targetSdkVersion));

    // 2026-04-29 B.31: stderr was /dev/null under init service so we never
    // saw whether initChild returned.  Switched to LOGI/LOGE (hilog) so we
    // know if Java side returned, threw, or hung in Looper.loop().
    LOGI("[CHILD_CK] CK_AFTER_initChild_call (Java returned!)");
    fprintf(stderr, "[CM-CK] initChild CallStaticVoidMethod RETURNED (exc=%d) — "
            "ActivityThread.main/Looper.loop exited or threw\n",
            (int)env->ExceptionCheck()); fflush(stderr);
    if (env->ExceptionCheck()) {
        LOGE("[CHILD_CK] Exception in AppSpawnXInit.initChild():");
        // 2026-05-02 G2.14r: ExceptionDescribe() writes to ART's stderr which
        // appspawn-x maps to /dev/null under "console":0 cfg.  Manually fetch
        // exception class name + message via JNI and log to hilog.
        jthrowable thr = env->ExceptionOccurred();
        env->ExceptionClear();
        if (thr != nullptr) {
            jclass thrClass = env->GetObjectClass(thr);
            jmethodID getMsg = env->GetMethodID(thrClass, "getMessage", "()Ljava/lang/String;");
            jclass classClass = env->GetObjectClass(thrClass);
            jmethodID getName = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
            jstring jClassName = (jstring)env->CallObjectMethod(thrClass, getName);
            jstring jMsg = getMsg ? (jstring)env->CallObjectMethod(thr, getMsg) : nullptr;
            if (env->ExceptionCheck()) env->ExceptionClear();
            const char* className = jClassName ? env->GetStringUTFChars(jClassName, nullptr) : "<no class>";
            const char* msg = jMsg ? env->GetStringUTFChars(jMsg, nullptr) : "<no message>";
            LOGE("[CHILD_CK] Exception type: %{public}s", className);
            LOGE("[CHILD_CK] Exception message: %{public}s", msg);
            fprintf(stderr, "[CM-EXC] initChild threw: %s: %s\n", className, msg); fflush(stderr);
            if (jClassName) env->ReleaseStringUTFChars(jClassName, className);
            if (jMsg) env->ReleaseStringUTFChars(jMsg, msg);
            // dump first 16 stack frames
            jmethodID getStackTrace = env->GetMethodID(thrClass, "getStackTrace",
                                                      "()[Ljava/lang/StackTraceElement;");
            if (getStackTrace) {
                jobjectArray frames = (jobjectArray)env->CallObjectMethod(thr, getStackTrace);
                if (env->ExceptionCheck()) env->ExceptionClear();
                if (frames) {
                    // §569: print class+method DIRECTLY instead of StackTraceElement.toString().
                    // toString() returns "" on this port, so every frame logged as a bare "at " with
                    // no text — which read as "the trace is only 5 frames deep" and sent a whole
                    // analysis round chasing a shallow-stack theory for the JIT's StackOverflowError.
                    // The frames were there all along; only the formatting was missing.
                    // Cap raised 16 -> 64: naming a RECURSION needs enough frames to see the repeat.
                    const jsize total = env->GetArrayLength(frames);
                    jsize n = total;
                    if (n > 64) n = 64;
                    jclass steClass = env->FindClass("java/lang/StackTraceElement");
                    jmethodID getCls = env->GetMethodID(steClass, "getClassName", "()Ljava/lang/String;");
                    jmethodID getMth = env->GetMethodID(steClass, "getMethodName", "()Ljava/lang/String;");
                    // §572: use fprintf(stderr), NOT LOGE. LOGE("%{public}s", ...) DROPS its string
                    // arguments in this build — every frame logged as a bare "at " with no name, which
                    // wasted two analysis rounds. stderr is the child log the harness reads anyway.
                    fprintf(stderr, "[CHILD_CK]   (stack depth = %d frames, showing %d)\n",
                            (int) total, (int) n);
                    fflush(stderr);
                    for (jsize i = 0; i < n; ++i) {
                        jobject ste = env->GetObjectArrayElement(frames, i);
                        jstring jc = (jstring) env->CallObjectMethod(ste, getCls);
                        jstring jm = (jstring) env->CallObjectMethod(ste, getMth);
                        const char* cs = jc ? env->GetStringUTFChars(jc, nullptr) : nullptr;
                        const char* ms = jm ? env->GetStringUTFChars(jm, nullptr) : nullptr;
                        fprintf(stderr, "[CHILD_CK]   at %s.%s\n", cs ? cs : "?", ms ? ms : "?");
                        fflush(stderr);
                        if (cs) env->ReleaseStringUTFChars(jc, cs);
                        if (ms) env->ReleaseStringUTFChars(jm, ms);
                        if (jc) env->DeleteLocalRef(jc);
                        if (jm) env->DeleteLocalRef(jm);
                        env->DeleteLocalRef(ste);
                    }
                    env->DeleteLocalRef(frames);
                }
            }
            env->DeleteLocalRef(thr);
        }
        LOGE("[CHILD_CK] Exception cleared — child will exit");
    } else {
        LOGE("[CHILD_CK] AppSpawnXInit.initChild() returned WITHOUT exception (event loop exited unexpectedly)");
    }

    env->DeleteLocalRef(jProcName);
    env->DeleteLocalRef(jTargetClass);
    if (initClassIsLocal) env->DeleteLocalRef(initClass);
}

} // namespace appspawnx
