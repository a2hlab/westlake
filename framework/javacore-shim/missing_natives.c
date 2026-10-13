/*
 * Platform natives the staged runtime declares but never registers.
 *
 * Two kinds so far, and they arrive the same way: a method is native in the boot classpath, the
 * library that would register it is not built here, and the first app to reach the method dies on
 * UnsatisfiedLinkError with nothing earlier to warn of it.
 *
 *   java.lang.Math and StrictMath -- the runtime's own openjdk stub registers most of these from a
 *   built-in table, but not all: rint is missing from Math, IEEEremainder and hypot from
 *   StrictMath. ART intrinsifies them when it compiles a method, so nothing noticed until an app
 *   reached one in the interpreter first -- Wikipedia, on Math.rint, in its first activity. Each
 *   is a libm call with the same meaning, so these are forwarders, not implementations.
 *
 *   jdk.internal.misc.VM.getNanoTimeAdjustment -- the wall clock behind java.time.Instant.now()
 *   and Clock.systemUTC(). Feeder's view model reads it before its first frame. It is a
 *   @CriticalNative, so it is bound with the critical-native calling convention.
 *
 *   java.lang.ProcessEnvironment.environ -- behind System.getenv(); Briar reads its environment
 *   while its Application is created. Ported from libcore's ProcessEnvironment_md.c.
 *
 *   android.view.SurfaceControl.nativeSetDesiredHdrHeadroom -- registered by
 *   liboh_android_runtime.so, whose object tree no longer builds. LocalSend's Flutter engine
 *   reaches it through SurfaceView.performSurfaceTransaction before its first frame.
 *
 *   android.view.SurfaceControl.nativeAddTransactionCompletedListener -- never registered. A
 *   relaunched activity keeps its window, and ViewRootImpl asks to report the rebuilt window's
 *   next draw, adding a completed listener as it does: the missing native killed the main thread
 *   of every activity that called recreate().
 *
 *   android.app.ActivityThread.nPurgePendingResources / nInitZygoteChildHeapProfiling -- the
 *   framework calls the first itself, from its low-memory purge, so no app code names it and any
 *   app can reach it once the system is short of memory: RedReader died on it before its first
 *   screen. AOSP's version asks the allocator to release free memory and ignores the result;
 *   OH's musl has no M_PURGE, so here it does nothing, which is the same outcome for the app. The
 *   second sets up heap profiling for zygote children, which is not configured here.
 *
 *   android.database.sqlite.SQLiteGlobal.nativeReleaseMemory -- the same kind: ActivityThread's
 *   low-memory handler calls it for every app. AOSP returns the bytes SQLite freed; this answers
 *   0, nothing released, which callers only log. Found by the harness's lifecycle-natives check,
 *   before any app died on it.
 *
 *   android.os.SystemProperties.native_get(String, String) -- wrapped, not replaced. Its table (in
 *   liboh_android_runtime.so, which cannot be rebuilt here) has no dalvik.vm heap sizes, so
 *   ActivityManager.getMemoryClass() and getLargeMemoryClass() answered 0: CNN's Application built
 *   an LruCache of that size and threw "maxSize <= 0"; Glide sizes its caches the same way. The
 *   registered implementation is taken from ART's method record (a jmethodID is the ArtMethod;
 *   its JNI entry point, data_, is at offset 16 in this ART) and used only if it lies in
 *   liboh_android_runtime.so; the wrapper returns it unchanged unless it answers nothing for
 *   one of these keys, then the value an 8 GB Android device reports. Same heap size as the
 *   bionic shim's table.
 *
 *   java.io.UnixFileSystem.setPermission0 / setReadOnly0 -- File.setWritable, setReadable,
 *   setExecutable and setReadOnly returned false on every file and directory (Os.chmod on the same
 *   paths works): Facebook and Messenger (SoLoader making lib-compressed read-only) and CapCut died
 *   on it. Replaced with libcore's own logic (UnixFileSystem_md.c): stat, add or clear the owner or
 *   everyone bits, chmod.
 *
 *   java.io.UnixFileSystem.getSpace0 -- File.getTotalSpace, getFreeSpace and getUsableSpace. The
 *   runtime's openjdk stub registers a version that answers 0 for every path, so any size an app
 *   derives from them is 0: CNN sizes its OkHttp cache at half of /data's usable space and its
 *   Application died on "maxSize <= 0". libcore's logic again: statvfs, the fragment size times
 *   the block count asked for.
 *
 *   The loading thread's Java priority -- not a native, a value. libart.so's PaletteSchedGetPriority
 *   is a weak stub that returns success without writing a priority, so ART gives every thread it
 *   attaches, the main thread included, Java priority 0 (Android's main thread has 5), and every
 *   Java thread created from them inherits 0. Code that saves a thread's priority and restores it
 *   calls Thread.setPriority(0), which throws "Priority out of range: 0": Instagram's Application
 *   constructor and CapCut's ByteDance thread pool died on it. This library loads on the main
 *   thread before the app starts, so it sets that thread to NORM_PRIORITY when it reads 0; threads
 *   created from it then inherit 5. Threads ART attaches later (render, binder) still read 0 --
 *   fixing those needs the palette in libart.
 *
 *   android.text.AndroidCharacter -- see text_natives.c.
 *
 *   android.media.AudioRecord -- see audiorecord_natives.c.
 *
 *   android.media -- MediaPlayer, and the AudioTrack and AudioSystem natives the AudioTrack shim
 *   leaves out: see media_natives.c.
 *
 * Bound with RegisterNatives rather than left to be found by name, which is what lets this library
 * supply a method belonging to a boot class without being the library that owns it.
 */
#define _GNU_SOURCE
#define _GNU_SOURCE /* dladdr */
#include <dlfcn.h>
#include <errno.h>
#include <jni.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/xattr.h>
#include <grp.h>
#include <poll.h>
JNIEXPORT jdouble JNICALL Java_java_lang_Math_acos(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return acos(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_asin(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return asin(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_atan(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return atan(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_cbrt(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return cbrt(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_ceil(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return ceil(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_cos(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return cos(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_cosh(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return cosh(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_exp(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return exp(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_expm1(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return expm1(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_floor(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return floor(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_log(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return log(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_log10(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return log10(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_log1p(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return log1p(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_rint(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return rint(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_sin(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return sin(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_sinh(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return sinh(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_sqrt(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return sqrt(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_tan(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return tan(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_tanh(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return tanh(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_IEEEremainder(JNIEnv* env, jclass c, jdouble a, jdouble b)
{ (void)env; (void)c; return remainder(a, b); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_atan2(JNIEnv* env, jclass c, jdouble a, jdouble b)
{ (void)env; (void)c; return atan2(a, b); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_hypot(JNIEnv* env, jclass c, jdouble a, jdouble b)
{ (void)env; (void)c; return hypot(a, b); }
JNIEXPORT jdouble JNICALL Java_java_lang_Math_pow(JNIEnv* env, jclass c, jdouble a, jdouble b)
{ (void)env; (void)c; return pow(a, b); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_acos(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return acos(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_asin(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return asin(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_atan(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return atan(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_cbrt(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return cbrt(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_cos(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return cos(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_cosh(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return cosh(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_exp(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return exp(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_expm1(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return expm1(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_log(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return log(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_log10(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return log10(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_log1p(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return log1p(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_sin(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return sin(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_sinh(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return sinh(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_sqrt(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return sqrt(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_tan(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return tan(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_tanh(JNIEnv* env, jclass c, jdouble a)
{ (void)env; (void)c; return tanh(a); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_IEEEremainder(JNIEnv* env, jclass c, jdouble a, jdouble b)
{ (void)env; (void)c; return remainder(a, b); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_atan2(JNIEnv* env, jclass c, jdouble a, jdouble b)
{ (void)env; (void)c; return atan2(a, b); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_hypot(JNIEnv* env, jclass c, jdouble a, jdouble b)
{ (void)env; (void)c; return hypot(a, b); }
JNIEXPORT jdouble JNICALL Java_java_lang_StrictMath_pow(JNIEnv* env, jclass c, jdouble a, jdouble b)
{ (void)env; (void)c; return pow(a, b); }

/*
 * Binding the methods here rather than leaving them to be found by name.
 *
 * ART looks a native method up by searching only the libraries loaded by the class loader that
 * owns the declaring class.  java.lang.Math is a boot class, so this library is loaded with a null
 * loader -- and it still was not searched: the load reports success, the symbol is exported, and
 * the lookup misses anyway.  RegisterNatives does not search at all, it binds the entry directly,
 * so it is right regardless of why the search fails.
 *
 * Each method is registered on its own and a failure is reported and stepped over, because a name
 * that is not declared native in this runtime's libcore would otherwise fail the whole table and
 * leave every other method unbound.
 */
/*
 * Setting a desired HDR headroom is a hint: it asks the compositor to reserve dynamic range for a
 * surface. This board composites without it, so doing nothing is the honest answer rather than a
 * missing one -- the app gets standard range, which is what it would get anyway. The signature is
 * the AOSP one, (long, long, float): transaction, surface control, headroom.
 */
JNIEXPORT void JNICALL Java_android_view_SurfaceControl_nativeSetDesiredHdrHeadroom(
        JNIEnv* env, jclass c, jlong transaction, jlong surface, jfloat headroom)
{ (void)env; (void)c; (void)transaction; (void)surface; (void)headroom; }

/*
 * A completed listener hears when a transaction reached the screen, with its present fence. No
 * compositor here reports that, so the listener is accepted and never called. Its framework caller,
 * ViewRootImpl, uses it only to time an app's first presented frame, which then goes unreported.
 */
JNIEXPORT void JNICALL Java_android_view_SurfaceControl_nativeAddTransactionCompletedListener(
        JNIEnv* env, jclass c, jlong transaction, jobject listener)
{ (void)env; (void)c; (void)transaction; (void)listener; }

/*
 * OpenJDK's contract: the current wall-clock time as nanoseconds since offsetInSeconds, or -1 when
 * that difference does not fit (more than 2^32 seconds either way); Instant.now() then re-reads
 * with a nearer offset.
 *
 * libcore declares it @CriticalNative: ART calls it with the Java arguments only, no JNIEnv and no
 * jclass. With the ordinary JNI signature the offset was read from the wrong register, the answer
 * was -1 for every offset, and Clock.currentInstant threw "Offset ... is not in range" (Tasks.org).
 */
static jlong wl_getNanoTimeAdjustment(jlong offset)
{
    const jlong max_diff = 0x0100000000LL;
    struct timespec ts;
    jlong diff;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return -1;
    diff = (jlong) ts.tv_sec - offset;
    if (diff >= max_diff || diff <= -max_diff) return -1;
    return diff * 1000000000LL + (jlong) ts.tv_nsec;
}

/* The process environment as alternating name and value byte arrays; entries without '=' are
 * skipped, as libcore's own implementation does. */
extern char** environ;
static jobjectArray wl_environ(JNIEnv* env, jclass c)
{
    jsize count = 0, i, j;
    jobjectArray result;
    jclass bytes = (*env)->FindClass(env, "[B");
    (void)c;
    if (bytes == NULL) return NULL;
    for (i = 0; environ[i]; i++) if (strchr(environ[i], '=') != NULL) count++;
    result = (*env)->NewObjectArray(env, 2 * count, bytes, NULL);
    if (result == NULL) return NULL;
    for (i = 0, j = 0; environ[i]; i++) {
        const char* end = strchr(environ[i], '=');
        jbyteArray name, value;
        jsize nameLength, valueLength;
        if (end == NULL) continue;
        nameLength = (jsize) (end - environ[i]);
        valueLength = (jsize) strlen(end + 1);
        name = (*env)->NewByteArray(env, nameLength);
        if (name == NULL) return NULL;
        value = (*env)->NewByteArray(env, valueLength);
        if (value == NULL) return NULL;
        (*env)->SetByteArrayRegion(env, name, 0, nameLength, (const jbyte*) environ[i]);
        (*env)->SetByteArrayRegion(env, value, 0, valueLength, (const jbyte*) (end + 1));
        (*env)->SetObjectArrayElement(env, result, 2 * j, name);
        (*env)->SetObjectArrayElement(env, result, 2 * j + 1, value);
        (*env)->DeleteLocalRef(env, name);
        (*env)->DeleteLocalRef(env, value);
        j++;
    }
    return result;
}

static const JNINativeMethod kProcessEnvironment[] = {
    { "environ", "()[[B", (void*) wl_environ },
};

static const JNINativeMethod kVm[] = {
    { "getNanoTimeAdjustment", "(J)J", (void*) wl_getNanoTimeAdjustment },
};

static void wl_purgePendingResources(JNIEnv* env, jclass c) { (void) env; (void) c; }
static void wl_initZygoteChildHeapProfiling(JNIEnv* env, jclass c) { (void) env; (void) c; }

typedef jstring (*PropGetFn)(JNIEnv*, jclass, jstring, jstring);
static PropGetFn g_prop_get;

static const struct { const char* key; const char* value; } kHeapProps[] = {
    { "dalvik.vm.heapgrowthlimit", "256m" },
    { "dalvik.vm.heapsize", "512m" },
};

static jstring wl_prop_get(JNIEnv* env, jclass c, jstring key, jstring def)
{
    jstring answer = g_prop_get(env, c, key, def);
    const char* k;
    const char* a;
    int missing;
    size_t i;
    if ((*env)->ExceptionCheck(env) || key == NULL) return answer;
    /* Missing means empty, or the caller's own default handed back: getLargeMemoryClass asks for
     * dalvik.vm.heapsize with "16m" as the default and got 16 back. */
    a = answer != NULL ? (*env)->GetStringUTFChars(env, answer, NULL) : NULL;
    missing = a == NULL || a[0] == '\0';
    if (!missing && def != NULL) {
        const char* d = (*env)->GetStringUTFChars(env, def, NULL);
        if (d != NULL) {
            missing = strcmp(a, d) == 0;
            (*env)->ReleaseStringUTFChars(env, def, d);
        }
    }
    if (a != NULL) (*env)->ReleaseStringUTFChars(env, answer, a);
    if (!missing) return answer;
    k = (*env)->GetStringUTFChars(env, key, NULL);
    if (k == NULL) return answer;
    for (i = 0; i < sizeof(kHeapProps) / sizeof(kHeapProps[0]); i++) {
        if (strcmp(k, kHeapProps[i].key) == 0) {
            (*env)->ReleaseStringUTFChars(env, key, k);
            return (*env)->NewStringUTF(env, kHeapProps[i].value);
        }
    }
    (*env)->ReleaseStringUTFChars(env, key, k);
    return answer;
}

/* Wraps SystemProperties.native_get(String, String) if its registered implementation can be found
 * and is liboh_android_runtime's; otherwise leaves it alone. Returns 1 when wrapped. */
static int wrap_system_properties(JNIEnv* env)
{
    static const JNINativeMethod wrapped = {
        "native_get", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", (void*) wl_prop_get };
    jclass c = (*env)->FindClass(env, "android/os/SystemProperties");
    jmethodID m;
    void* entry;
    Dl_info info;
    int ok = 0;
    if (c == NULL) { (*env)->ExceptionClear(env); return 0; }
    m = (*env)->GetStaticMethodID(env, c, wrapped.name, wrapped.signature);
    if (m == NULL) {
        (*env)->ExceptionClear(env);
    } else {
        entry = *(void* const*) ((const char*) m + 16);   /* ArtMethod::ptr_sized_fields_.data_ */
        if (entry != NULL && dladdr(entry, &info) != 0 && info.dli_fname != NULL
                && strstr(info.dli_fname, "liboh_android_runtime") != NULL) {
            g_prop_get = (PropGetFn) entry;
            ok = (*env)->RegisterNatives(env, c, &wrapped, 1) == JNI_OK;
            if (!ok) (*env)->ExceptionClear(env);
        }
    }
    (*env)->DeleteLocalRef(env, c);
    return ok;
}

static jint wl_sqliteReleaseMemory(JNIEnv* env, jclass c) { (void) env; (void) c; return 0; }

static const JNINativeMethod kSqliteGlobal[] = {
    { "nativeReleaseMemory", "()I", (void*) wl_sqliteReleaseMemory },
};

static const JNINativeMethod kActivityThread[] = {
    { "nPurgePendingResources", "()V", (void*) wl_purgePendingResources },
    { "nInitZygoteChildHeapProfiling", "()V", (void*) wl_initZygoteChildHeapProfiling },
};

static const JNINativeMethod kSurfaceControl[] = {
    { "nativeSetDesiredHdrHeadroom", "(JJF)V",
      (void*) Java_android_view_SurfaceControl_nativeSetDesiredHdrHeadroom },
    { "nativeAddTransactionCompletedListener", "(JLjava/util/function/Consumer;)V",
      (void*) Java_android_view_SurfaceControl_nativeAddTransactionCompletedListener },
};

#define M(name, sig)      { #name, sig, (void*) Java_java_lang_Math_##name }
#define SM(name, sig)     { #name, sig, (void*) Java_java_lang_StrictMath_##name }

static const JNINativeMethod kMath[] = {
    M(acos, "(D)D"), M(asin, "(D)D"), M(atan, "(D)D"), M(cbrt, "(D)D"), M(ceil, "(D)D"),
    M(cos, "(D)D"), M(cosh, "(D)D"), M(exp, "(D)D"), M(expm1, "(D)D"), M(floor, "(D)D"),
    M(log, "(D)D"), M(log10, "(D)D"), M(log1p, "(D)D"), M(rint, "(D)D"), M(sin, "(D)D"),
    M(sinh, "(D)D"), M(sqrt, "(D)D"), M(tan, "(D)D"), M(tanh, "(D)D"),
    M(IEEEremainder, "(DD)D"), M(atan2, "(DD)D"), M(hypot, "(DD)D"), M(pow, "(DD)D"),
};

static const JNINativeMethod kStrictMath[] = {
    SM(acos, "(D)D"), SM(asin, "(D)D"), SM(atan, "(D)D"), SM(cbrt, "(D)D"), SM(cos, "(D)D"),
    SM(cosh, "(D)D"), SM(exp, "(D)D"), SM(expm1, "(D)D"), SM(log, "(D)D"), SM(log10, "(D)D"),
    SM(log1p, "(D)D"), SM(sin, "(D)D"), SM(sinh, "(D)D"), SM(sqrt, "(D)D"), SM(tan, "(D)D"),
    SM(tanh, "(D)D"), SM(IEEEremainder, "(DD)D"), SM(atan2, "(DD)D"), SM(hypot, "(DD)D"),
    SM(pow, "(DD)D"),
};

/*
 * libcore.io.Memory's bulk copies between native memory and Java arrays. NIO's direct buffers
 * use them for every bulk put/get (FloatBuffer.put(float[]) and the like), and this runtime's
 * libcore registers none: libGDX's Mindustry died in FloatBuffer.put on pokeFloatArray, Unciv on
 * pokeShortArray. As AOSP's libcore_io_Memory.cpp: a plain copy, or a per-element byte swap
 * when the buffer's order is not the platform's.
 */
#define WL_MEMORY_ARRAY(Name, T, JArray, Get, Set, Swap)                                           \
    static void wl_peek##Name##Array(JNIEnv* env, jclass c, jlong address, JArray dst,           \
                                     jint offset, jint count, jboolean swap) {                   \
        const T* src = (const T*) (intptr_t) address;                                            \
        (void) c;                                                                                  \
        if (!swap) {                                                                               \
            (*env)->Set(env, dst, offset, count, src);                                             \
            return;                                                                                \
        }                                                                                          \
        for (jint i = 0; i < count; i++) {                                                         \
            T value;                                                                               \
            memcpy(&value, &src[i], sizeof value);                                                 \
            value = Swap(value);                                                                   \
            (*env)->Set(env, dst, offset + i, 1, &value);                                          \
        }                                                                                          \
    }                                                                                              \
    static void wl_poke##Name##Array(JNIEnv* env, jclass c, jlong address, JArray src,           \
                                     jint offset, jint count, jboolean swap) {                   \
        T* dst = (T*) (intptr_t) address;                                                          \
        (void) c;                                                                                  \
        if (!swap) {                                                                               \
            (*env)->Get(env, src, offset, count, dst);                                             \
            return;                                                                                \
        }                                                                                          \
        for (jint i = 0; i < count; i++) {                                                         \
            T value;                                                                               \
            (*env)->Get(env, src, offset + i, 1, &value);                                          \
            value = Swap(value);                                                                   \
            memcpy(&dst[i], &value, sizeof value);                                                 \
        }                                                                                          \
    }

static inline jchar wl_swap16c(jchar v) { return (jchar) __builtin_bswap16(v); }
static inline jshort wl_swap16(jshort v) { return (jshort) __builtin_bswap16((uint16_t) v); }
static inline jint wl_swap32(jint v) { return (jint) __builtin_bswap32((uint32_t) v); }
static inline jlong wl_swap64(jlong v) { return (jlong) __builtin_bswap64((uint64_t) v); }
static inline jfloat wl_swapf(jfloat v) {
    uint32_t bits;
    memcpy(&bits, &v, sizeof bits);
    bits = __builtin_bswap32(bits);
    memcpy(&v, &bits, sizeof v);
    return v;
}
static inline jdouble wl_swapd(jdouble v) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    bits = __builtin_bswap64(bits);
    memcpy(&v, &bits, sizeof v);
    return v;
}

WL_MEMORY_ARRAY(Char, jchar, jcharArray, GetCharArrayRegion, SetCharArrayRegion, wl_swap16c)
WL_MEMORY_ARRAY(Short, jshort, jshortArray, GetShortArrayRegion, SetShortArrayRegion, wl_swap16)
WL_MEMORY_ARRAY(Int, jint, jintArray, GetIntArrayRegion, SetIntArrayRegion, wl_swap32)
WL_MEMORY_ARRAY(Long, jlong, jlongArray, GetLongArrayRegion, SetLongArrayRegion, wl_swap64)
WL_MEMORY_ARRAY(Float, jfloat, jfloatArray, GetFloatArrayRegion, SetFloatArrayRegion, wl_swapf)
WL_MEMORY_ARRAY(Double, jdouble, jdoubleArray, GetDoubleArrayRegion, SetDoubleArrayRegion, wl_swapd)

static void wl_peekByteArray(JNIEnv* env, jclass c, jlong address, jbyteArray dst, jint offset, jint count) {
    (void) c;
    (*env)->SetByteArrayRegion(env, dst, offset, count, (const jbyte*) (intptr_t) address);
}
static void wl_pokeByteArray(JNIEnv* env, jclass c, jlong address, jbyteArray src, jint offset, jint count) {
    (void) c;
    (*env)->GetByteArrayRegion(env, src, offset, count, (jbyte*) (intptr_t) address);
}

/*
 * The bulk copies between a byte[] and an array of wider elements, with an optional per-element
 * byte swap: java.nio.HeapByteBuffer's typed bulk gets and puts (a CharBuffer, ShortBuffer,
 * IntBuffer... view over a byte[]). Instagram's start-up hit unsafeBulkGet; Snapchat's r83 log has
 * it too. As AOSP's libcore_io_Memory.cpp: the element-typed side's offset counts elements, the
 * byte[] side's counts bytes.
 */
static void wl_bulk_copy(jbyte* dst, const jbyte* src, jint byteCount, jint sizeofElement, jboolean swap)
{
    if (sizeofElement <= 1 || !swap) {
        memmove(dst, src, (size_t) byteCount);
        return;
    }
    for (jint i = 0; i + sizeofElement <= byteCount; i += sizeofElement) {
        for (jint k = 0; k < sizeofElement; k++) dst[i + k] = src[i + sizeofElement - 1 - k];
    }
}

static void wl_unsafeBulkGet(JNIEnv* env, jclass c, jobject dstObject, jint dstOffset, jint byteCount,
                             jbyteArray srcArray, jint srcOffset, jint sizeofElement, jboolean swap)
{
    (void) c;
    jbyte* src = (jbyte*) (*env)->GetPrimitiveArrayCritical(env, srcArray, NULL);
    if (src == NULL) return;
    jbyte* dst = (jbyte*) (*env)->GetPrimitiveArrayCritical(env, (jarray) dstObject, NULL);
    if (dst != NULL) {
        wl_bulk_copy(dst + (size_t) dstOffset * (size_t) sizeofElement, src + srcOffset, byteCount, sizeofElement, swap);
        (*env)->ReleasePrimitiveArrayCritical(env, (jarray) dstObject, dst, 0);
    }
    (*env)->ReleasePrimitiveArrayCritical(env, srcArray, src, JNI_ABORT);
}

static void wl_unsafeBulkPut(JNIEnv* env, jclass c, jbyteArray dstArray, jint dstOffset, jint byteCount,
                             jobject srcObject, jint srcOffset, jint sizeofElement, jboolean swap)
{
    (void) c;
    jbyte* src = (jbyte*) (*env)->GetPrimitiveArrayCritical(env, (jarray) srcObject, NULL);
    if (src == NULL) return;
    jbyte* dst = (jbyte*) (*env)->GetPrimitiveArrayCritical(env, dstArray, NULL);
    if (dst != NULL) {
        wl_bulk_copy(dst + dstOffset, src + (size_t) srcOffset * (size_t) sizeofElement, byteCount, sizeofElement, swap);
        (*env)->ReleasePrimitiveArrayCritical(env, dstArray, dst, 0);
    }
    (*env)->ReleasePrimitiveArrayCritical(env, (jarray) srcObject, src, JNI_ABORT);
}

#define MEM(Name, Sig) { #Name, Sig, (void*) wl_##Name }
static const JNINativeMethod kMemory[] = {
    MEM(peekByteArray, "(J[BII)V"), MEM(pokeByteArray, "(J[BII)V"),
    MEM(peekCharArray, "(J[CIIZ)V"), MEM(pokeCharArray, "(J[CIIZ)V"),
    MEM(peekShortArray, "(J[SIIZ)V"), MEM(pokeShortArray, "(J[SIIZ)V"),
    MEM(peekIntArray, "(J[IIIZ)V"), MEM(pokeIntArray, "(J[IIIZ)V"),
    MEM(peekLongArray, "(J[JIIZ)V"), MEM(pokeLongArray, "(J[JIIZ)V"),
    MEM(peekFloatArray, "(J[FIIZ)V"), MEM(pokeFloatArray, "(J[FIIZ)V"),
    MEM(peekDoubleArray, "(J[DIIZ)V"), MEM(pokeDoubleArray, "(J[DIIZ)V"),
    MEM(unsafeBulkGet, "(Ljava/lang/Object;II[BIIZ)V"), MEM(unsafeBulkPut, "([BIILjava/lang/Object;IIZ)V"),
};

static void note(const char* text)
{
    size_t n = 0;
    while (text[n] != '\0') n++;
    (void) write(2, text, n);
}

static int bind(JNIEnv* env, const char* cls, const JNINativeMethod* table, int count)
{
    jclass c = (*env)->FindClass(env, cls);
    int bound = 0;
    int i;
    if (c == NULL) {
        (*env)->ExceptionClear(env);
        note("[WL-NATIVES] class not found\n");
        return 0;
    }
    for (i = 0; i < count; i++) {
        if ((*env)->RegisterNatives(env, c, &table[i], 1) == JNI_OK) {
            bound++;
        } else {
            /* Not declared native in this runtime's libcore: that method needs no binding. */
            (*env)->ExceptionClear(env);
        }
    }
    (*env)->DeleteLocalRef(env, c);
    return bound;
}

void wl_register_media(JNIEnv* env, JavaVM* vm);
int wl_register_text(JNIEnv* env);
int wl_register_audiorecord(JNIEnv* env);
int wl_register_audio_ports(JNIEnv* env);
int wl_register_expat(JNIEnv* env);
int wl_register_ojluni_nio(JNIEnv* env);
int wl_register_localsocket(JNIEnv* env);
int wl_register_surface_canvas(JNIEnv* env);
int wl_register_sqlite_collations(JNIEnv* env);
int wl_register_notification_switch(JNIEnv* env);

static int file_path(JNIEnv* env, jobject file, char* out, size_t size)
{
    jclass c = (*env)->GetObjectClass(env, file);
    jmethodID getPath = (*env)->GetMethodID(env, c, "getPath", "()Ljava/lang/String;");
    jstring path;
    const char* chars;
    (*env)->DeleteLocalRef(env, c);
    if (getPath == NULL) { (*env)->ExceptionClear(env); return 0; }
    path = (jstring) (*env)->CallObjectMethod(env, file, getPath);
    if (path == NULL || (*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
    chars = (*env)->GetStringUTFChars(env, path, NULL);
    if (chars == NULL) { (*env)->DeleteLocalRef(env, path); return 0; }
    snprintf(out, size, "%s", chars);
    (*env)->ReleaseStringUTFChars(env, path, chars);
    (*env)->DeleteLocalRef(env, path);
    return out[0] != '\0';
}

/* java.io.FileSystem's ACCESS_READ, ACCESS_WRITE, ACCESS_EXECUTE. */
static jboolean wl_setPermission0(JNIEnv* env, jobject self, jobject file, jint access, jboolean enable,
                                  jboolean owner_only)
{
    char path[4096];
    struct stat st;
    mode_t bits = 0;
    (void) self;
    if (!file_path(env, file, path, sizeof(path)) || stat(path, &st) != 0) return JNI_FALSE;
    if (access & 4) bits |= owner_only ? S_IRUSR : (S_IRUSR | S_IRGRP | S_IROTH);
    if (access & 2) bits |= owner_only ? S_IWUSR : (S_IWUSR | S_IWGRP | S_IWOTH);
    if (access & 1) bits |= owner_only ? S_IXUSR : (S_IXUSR | S_IXGRP | S_IXOTH);
    return chmod(path, enable ? (st.st_mode | bits) : (st.st_mode & ~bits)) == 0 ? JNI_TRUE : JNI_FALSE;
}

static jboolean wl_setReadOnly0(JNIEnv* env, jobject self, jobject file)
{
    char path[4096];
    struct stat st;
    (void) self;
    if (!file_path(env, file, path, sizeof(path)) || stat(path, &st) != 0) return JNI_FALSE;
    return chmod(path, st.st_mode & ~(S_IWUSR | S_IWGRP | S_IWOTH)) == 0 ? JNI_TRUE : JNI_FALSE;
}

/*
 * File.setLastModified failed like the permission natives above (Facebook: "could not set modtime of
 * .../dex/regen_stamp"). As OpenJDK does: the access time is kept, the modification time set to the
 * given milliseconds.
 */
static jboolean wl_setLastModifiedTime0(JNIEnv* env, jobject self, jobject file, jlong time)
{
    char path[4096];
    struct timespec times[2];
    (void) self;
    if (!file_path(env, file, path, sizeof(path))) return JNI_FALSE;
    times[0].tv_sec = 0;
    times[0].tv_nsec = UTIME_OMIT;
    times[1].tv_sec = (time_t) (time / 1000);
    times[1].tv_nsec = (long) (time % 1000) * 1000000L;
    return utimensat(AT_FDCWD, path, times, 0) == 0 ? JNI_TRUE : JNI_FALSE;
}

static jlong wl_getLastModifiedTime0(JNIEnv* env, jobject self, jobject file)
{
    char path[4096];
    struct stat st;
    (void) self;
    if (!file_path(env, file, path, sizeof(path)) || stat(path, &st) != 0) return 0;
    return (jlong) st.st_mtim.tv_sec * 1000 + st.st_mtim.tv_nsec / 1000000;
}

/* java.io.FileSystem's SPACE_TOTAL, SPACE_FREE, SPACE_USABLE. */
static jlong wl_getSpace0(JNIEnv* env, jobject self, jobject file, jint which)
{
    char path[4096];
    struct statvfs st;
    (void) self;
    if (!file_path(env, file, path, sizeof(path)) || statvfs(path, &st) != 0) return 0;
    switch (which) {
    case 0: return (jlong) st.f_frsize * (jlong) st.f_blocks;
    case 1: return (jlong) st.f_frsize * (jlong) st.f_bfree;
    case 2: return (jlong) st.f_frsize * (jlong) st.f_bavail;
    default: return 0;
    }
}

static const JNINativeMethod kUnixFileSystem[] = {
    { "setPermission0", "(Ljava/io/File;IZZ)Z", (void*) wl_setPermission0 },
    { "setReadOnly0", "(Ljava/io/File;)Z", (void*) wl_setReadOnly0 },
    { "setLastModifiedTime0", "(Ljava/io/File;J)Z", (void*) wl_setLastModifiedTime0 },
    { "getLastModifiedTime0", "(Ljava/io/File;)J", (void*) wl_getLastModifiedTime0 },
    { "getSpace0", "(Ljava/io/File;I)J", (void*) wl_getSpace0 },
};

/*
 * Platform natives the staged runtime leaves unregistered, found by r73 ("No implementation found"):
 * sun.nio.ch.Net's poll constants (Mindustry, Unciv), Process.getGidForName (NYTimes),
 * android.os.SELinux's context queries (Termux), Camera._getNumberOfCameras (Open Camera) and
 * CamcorderProfile (Snapchat). Values are this kernel's; there is no camera, so none is reported.
 */
static jshort wl_pollin(JNIEnv* e, jclass c) { (void) e; (void) c; return POLLIN; }
static jshort wl_pollout(JNIEnv* e, jclass c) { (void) e; (void) c; return POLLOUT; }
static jshort wl_pollerr(JNIEnv* e, jclass c) { (void) e; (void) c; return POLLERR; }
static jshort wl_pollhup(JNIEnv* e, jclass c) { (void) e; (void) c; return POLLHUP; }
static jshort wl_pollnval(JNIEnv* e, jclass c) { (void) e; (void) c; return POLLNVAL; }
static jshort wl_pollconn(JNIEnv* e, jclass c) { (void) e; (void) c; return POLLOUT; }

static const JNINativeMethod kNet[] = {
    { "pollinValue", "()S", (void*) wl_pollin },
    { "polloutValue", "()S", (void*) wl_pollout },
    { "pollerrValue", "()S", (void*) wl_pollerr },
    { "pollhupValue", "()S", (void*) wl_pollhup },
    { "pollnvalValue", "()S", (void*) wl_pollnval },
    { "pollconnValue", "()S", (void*) wl_pollconn },
};

static jint wl_getGidForName(JNIEnv* env, jclass c, jstring name)
{
    const char* chars;
    struct group* g;
    jint gid = -1;
    (void) c;
    if (name == NULL) return -1;
    chars = (*env)->GetStringUTFChars(env, name, NULL);
    if (chars == NULL) return -1;
    g = getgrnam(chars);
    if (g != NULL) gid = (jint) g->gr_gid;
    (*env)->ReleaseStringUTFChars(env, name, chars);
    return gid;
}

/*
 * Process.readProcFile and parseProcLine, as android_util_Process.cpp implements them: fields of a
 * /proc file split by the caller's format words. WhatsApp reads its /proc stats with readProcFile,
 * which the runtime declares but never registered (UnsatisfiedLinkError on the calling thread).
 */
enum {
    WL_PROC_TERM_MASK = 0xff, WL_PROC_COMBINE = 0x100, WL_PROC_PARENS = 0x200, WL_PROC_QUOTES = 0x400,
    WL_PROC_CHAR = 0x800, WL_PROC_OUT_STRING = 0x1000, WL_PROC_OUT_LONG = 0x2000, WL_PROC_OUT_FLOAT = 0x4000,
};

static jboolean wl_parse_proc_line(JNIEnv* env, char* buffer, jint startIndex, jint endIndex, jintArray format,
                                   jobjectArray outStrings, jlongArray outLongs, jfloatArray outFloats)
{
    const jsize NF = (*env)->GetArrayLength(env, format);
    const jsize NS = outStrings ? (*env)->GetArrayLength(env, outStrings) : 0;
    const jsize NL = outLongs ? (*env)->GetArrayLength(env, outLongs) : 0;
    const jsize NR = outFloats ? (*env)->GetArrayLength(env, outFloats) : 0;
    jint* formatData = (*env)->GetIntArrayElements(env, format, NULL);
    jlong* longsData = outLongs ? (*env)->GetLongArrayElements(env, outLongs, NULL) : NULL;
    jfloat* floatsData = outFloats ? (*env)->GetFloatArrayElements(env, outFloats, NULL) : NULL;
    jsize i = startIndex, di = 0, fi;
    jboolean res = JNI_TRUE;
    if (formatData == NULL || (NL > 0 && longsData == NULL) || (NR > 0 && floatsData == NULL)) {
        if (formatData != NULL) (*env)->ReleaseIntArrayElements(env, format, formatData, 0);
        if (longsData != NULL) (*env)->ReleaseLongArrayElements(env, outLongs, longsData, 0);
        if (floatsData != NULL) (*env)->ReleaseFloatArrayElements(env, outFloats, floatsData, 0);
        jclass oom = (*env)->FindClass(env, "java/lang/OutOfMemoryError");
        if (oom != NULL) (*env)->ThrowNew(env, oom, NULL);
        return JNI_FALSE;
    }
    for (fi = 0; fi < NF; fi++) {
        jint mode = formatData[fi];
        if ((mode & WL_PROC_PARENS) != 0) {
            i++;
        } else if ((mode & WL_PROC_QUOTES) != 0) {
            if (i < endIndex && buffer[i] == '"') i++;
            else mode &= ~WL_PROC_QUOTES;
        }
        const char term = (char) (mode & WL_PROC_TERM_MASK);
        const jsize start = i;
        jsize end = -1;
        if (i >= endIndex) {
            res = JNI_FALSE;
            break;
        }
        if ((mode & WL_PROC_PARENS) != 0) {
            while (i < endIndex && buffer[i] != ')') i++;
            end = i;
            i++;
        } else if ((mode & WL_PROC_QUOTES) != 0) {
            while (i < endIndex && buffer[i] != '"') i++;
            end = i;
            i++;
        }
        while (i < endIndex && buffer[i] != term) i++;
        if (end < 0) end = i;
        if (i < endIndex) {
            i++;
            if ((mode & WL_PROC_COMBINE) != 0) {
                while (i < endIndex && buffer[i] == term) i++;
            }
        }
        if ((mode & (WL_PROC_OUT_FLOAT | WL_PROC_OUT_LONG | WL_PROC_OUT_STRING)) != 0) {
            char c = buffer[end];
            buffer[end] = 0;
            if ((mode & WL_PROC_OUT_FLOAT) != 0 && di < NR) floatsData[di] = strtof(buffer + start, NULL);
            if ((mode & WL_PROC_OUT_LONG) != 0 && di < NL) {
                longsData[di] = (mode & WL_PROC_CHAR) != 0 ? buffer[start] : strtoll(buffer + start, NULL, 10);
            }
            if ((mode & WL_PROC_OUT_STRING) != 0 && di < NS) {
                jstring text = (*env)->NewStringUTF(env, buffer + start);
                (*env)->SetObjectArrayElement(env, outStrings, di, text);
                if (text != NULL) (*env)->DeleteLocalRef(env, text);
            }
            buffer[end] = c;
            di++;
        }
    }
    (*env)->ReleaseIntArrayElements(env, format, formatData, 0);
    if (longsData != NULL) (*env)->ReleaseLongArrayElements(env, outLongs, longsData, 0);
    if (floatsData != NULL) (*env)->ReleaseFloatArrayElements(env, outFloats, floatsData, 0);
    return res;
}

static jboolean wl_parseProcLine(JNIEnv* env, jclass c, jbyteArray buffer, jint startIndex, jint endIndex,
                                 jintArray format, jobjectArray outStrings, jlongArray outLongs, jfloatArray outFloats)
{
    jbyte* bytes = (*env)->GetByteArrayElements(env, buffer, NULL);
    jboolean result;
    (void) c;
    if (bytes == NULL) return JNI_FALSE;
    result = wl_parse_proc_line(env, (char*) bytes, startIndex, endIndex, format, outStrings, outLongs, outFloats);
    (*env)->ReleaseByteArrayElements(env, buffer, bytes, 0);
    return result;
}

static jboolean wl_readProcFile(JNIEnv* env, jclass c, jstring file, jintArray format, jobjectArray outStrings,
                                jlongArray outLongs, jfloatArray outFloats)
{
    const char* path;
    char stack[4096];
    char* data = stack;
    size_t size = sizeof(stack);
    ssize_t count;
    jboolean result;
    int fd;
    (void) c;
    if (file == NULL || format == NULL) {
        jclass npe = (*env)->FindClass(env, "java/lang/NullPointerException");
        if (npe != NULL) (*env)->ThrowNew(env, npe, NULL);
        return JNI_FALSE;
    }
    path = (*env)->GetStringUTFChars(env, file, NULL);
    if (path == NULL) return JNI_FALSE;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    (*env)->ReleaseStringUTFChars(env, file, path);
    if (fd < 0) return JNI_FALSE;
    for (;;) {
        count = pread(fd, data, size, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 || (size_t) count < size) break;
        /* As large as the whole file: grow until a read comes back short. */
        if (data != stack) free(data);
        size *= 2;
        data = malloc(size);
        if (data == NULL) {
            close(fd);
            return JNI_FALSE;
        }
    }
    close(fd);
    result = count < 0 ? JNI_FALSE
                       : wl_parse_proc_line(env, data, 0, (jint) count, format, outStrings, outLongs, outFloats);
    if (data != stack) free(data);
    return result;
}

static const JNINativeMethod kProcess[] = {
    { "getGidForName", "(Ljava/lang/String;)I", (void*) wl_getGidForName },
    { "readProcFile", "(Ljava/lang/String;[I[Ljava/lang/String;[J[F)Z", (void*) wl_readProcFile },
    { "parseProcLine", "([BII[I[Ljava/lang/String;[J[F)Z", (void*) wl_parseProcLine },
};

/*
 * No performance hint service: Android's PerformanceHintManager.create() throws
 * ServiceNotFoundException when nativeAcquireManager answers 0, and getSystemService answers null.
 * Unbound, the call threw UnsatisfiedLinkError past that check (Messenger).
 */
static jlong wl_acquireHintManager(JNIEnv* e, jclass c) { (void) e; (void) c; return 0; }

static const JNINativeMethod kPerformanceHint[] = {
    { "nativeAcquireManager", "()J", (void*) wl_acquireHintManager },
};

static jstring context_from(JNIEnv* env, const char* path)
{
    char buffer[256];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n = fd >= 0 ? read(fd, buffer, sizeof(buffer) - 1) : -1;
    if (fd >= 0) close(fd);
    if (n <= 0) return NULL;
    buffer[n] = '\0';
    while (n > 0 && (buffer[n - 1] == '\n' || buffer[n - 1] == '\0')) buffer[--n] = '\0';
    return (*env)->NewStringUTF(env, buffer);
}

static jstring wl_selinux_getContext(JNIEnv* env, jclass c) { (void) c; return context_from(env, "/proc/self/attr/current"); }

static jstring wl_selinux_getPidContext(JNIEnv* env, jclass c, jint pid)
{
    char path[64];
    (void) c;
    snprintf(path, sizeof(path), "/proc/%d/attr/current", (int) pid);
    return context_from(env, path);
}

static jstring wl_selinux_getFileContext(JNIEnv* env, jclass c, jstring path)
{
    char buffer[256];
    const char* chars;
    ssize_t n;
    (void) c;
    if (path == NULL) return NULL;
    chars = (*env)->GetStringUTFChars(env, path, NULL);
    if (chars == NULL) return NULL;
    n = getxattr(chars, "security.selinux", buffer, sizeof(buffer) - 1);
    (*env)->ReleaseStringUTFChars(env, path, chars);
    if (n <= 0) return NULL;
    buffer[n] = '\0';
    return (*env)->NewStringUTF(env, buffer);
}

static jboolean wl_selinux_enabled(JNIEnv* env, jclass c)
{
    (void) env; (void) c;
    return access("/sys/fs/selinux/enforce", F_OK) == 0 ? JNI_TRUE : JNI_FALSE;
}

static jboolean wl_selinux_enforced(JNIEnv* env, jclass c)
{
    char value = '0';
    int fd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
    (void) env; (void) c;
    if (fd >= 0) { if (read(fd, &value, 1) != 1) value = '0'; close(fd); }
    return value == '1' ? JNI_TRUE : JNI_FALSE;
}

static const JNINativeMethod kSELinux[] = {
    { "getContext", "()Ljava/lang/String;", (void*) wl_selinux_getContext },
    { "getPidContext", "(I)Ljava/lang/String;", (void*) wl_selinux_getPidContext },
    { "getFileContext", "(Ljava/lang/String;)Ljava/lang/String;", (void*) wl_selinux_getFileContext },
    { "isSELinuxEnabled", "()Z", (void*) wl_selinux_enabled },
    { "isSELinuxEnforced", "()Z", (void*) wl_selinux_enforced },
};

static jint wl_camera_count(JNIEnv* e, jclass c, jint device, jint policy)
{
    (void) e; (void) c; (void) device; (void) policy;
    return 0;
}

/*
 * With no camera listed, every id is unknown. Android's JNI answers getCameraInfo for an unknown id
 * with RuntimeException("Unknown camera ID"), and a connection with -EACCES, which Camera.open turns
 * into "Fail to connect to camera service". Open Camera asks for camera 0's facing without checking
 * the count; the missing native threw UnsatisfiedLinkError, which no caller catches.
 */
static void wl_camera_info(JNIEnv* e, jclass c, jint id, jint rotation, jint slow, jint policy, jobject info)
{
    (void) c; (void) id; (void) rotation; (void) slow; (void) policy; (void) info;
    jclass failure = (*e)->FindClass(e, "java/lang/RuntimeException");
    if (failure != NULL) (*e)->ThrowNew(e, failure, "Unknown camera ID");
}

static jint wl_camera_setup(JNIEnv* e, jobject thiz, jobject self, jint id, jstring package, jint rotation,
                            jboolean slow, jint client, jint policy)
{
    (void) e; (void) thiz; (void) self; (void) id; (void) package; (void) rotation; (void) slow; (void) client;
    (void) policy;
    return -EACCES;
}

/* A camera whose setup failed holds nothing; its finalizer still releases it (Open Camera). */
static void wl_camera_release(JNIEnv* e, jobject thiz) { (void) e; (void) thiz; }

static const JNINativeMethod kCamera[] = {
    { "_getNumberOfCameras", "(II)I", (void*) wl_camera_count },
    { "_getCameraInfo", "(IIIILandroid/hardware/Camera$CameraInfo;)V", (void*) wl_camera_info },
    { "native_setup", "(Ljava/lang/Object;ILjava/lang/String;IZII)I", (void*) wl_camera_setup },
    { "native_release", "()V", (void*) wl_camera_release },
};

static void wl_camcorder_init(JNIEnv* e, jclass c) { (void) e; (void) c; }
static jobject wl_camcorder_profile(JNIEnv* e, jclass c, jint id, jint q) { (void) e; (void) c; (void) id; (void) q; return NULL; }
static jobject wl_camcorder_profiles(JNIEnv* e, jclass c, jint id, jint q, jboolean a) { (void) e; (void) c; (void) id; (void) q; (void) a; return NULL; }
static jboolean wl_camcorder_has(JNIEnv* e, jclass c, jint id, jint q) { (void) e; (void) c; (void) id; (void) q; return JNI_FALSE; }

static const JNINativeMethod kCamcorderProfile[] = {
    { "native_init", "()V", (void*) wl_camcorder_init },
    { "native_get_camcorder_profile", "(II)Landroid/media/CamcorderProfile;", (void*) wl_camcorder_profile },
    { "native_get_camcorder_profiles", "(IIZ)Landroid/media/EncoderProfiles;", (void*) wl_camcorder_profiles },
    { "native_has_camcorder_profile", "(II)Z", (void*) wl_camcorder_has },
};

/* Returns the priority it found (before any change). */
static int normalize_thread_priority(JNIEnv* env)
{
    jclass thread = (*env)->FindClass(env, "java/lang/Thread");
    jmethodID current, get, set;
    jobject self;
    int priority = -1;
    if (thread == NULL) { (*env)->ExceptionClear(env); return -1; }
    current = (*env)->GetStaticMethodID(env, thread, "currentThread", "()Ljava/lang/Thread;");
    get = (*env)->GetMethodID(env, thread, "getPriority", "()I");
    set = (*env)->GetMethodID(env, thread, "setPriority", "(I)V");
    if (current != NULL && get != NULL && set != NULL
            && (self = (*env)->CallStaticObjectMethod(env, thread, current)) != NULL) {
        priority = (*env)->CallIntMethod(env, self, get);
        if (priority < 1) {
            (*env)->CallVoidMethod(env, self, set, 5 /* Thread.NORM_PRIORITY */);
        }
        (*env)->DeleteLocalRef(env, self);
    }
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, thread);
    return priority;
}

/*
 * Failed JNI member lookups, logged. A native library that caches a method or field ID for a
 * member this runtime lacks aborts later under CheckJNI with only "mid == null" (Facebook and
 * Messenger in com.facebook.proxygen.EventBase.init()), naming neither. ART lets a table replace
 * every thread's JNI functions (JNIEnvExt::SetTableOverride, as heap profilers use); the one
 * installed here is the active table with the four ID lookups wrapped. A failed lookup leaves
 * NoSuchMethodError/NoSuchFieldError pending, so the wrapper sets it aside, names the class, and
 * puts it back before returning.
 */
static struct JNINativeInterface g_lookup_table;
static jmethodID (*g_get_method_id)(JNIEnv*, jclass, const char*, const char*);
static jmethodID (*g_get_static_method_id)(JNIEnv*, jclass, const char*, const char*);
static jfieldID (*g_get_field_id)(JNIEnv*, jclass, const char*, const char*);
static jfieldID (*g_get_static_field_id)(JNIEnv*, jclass, const char*, const char*);
static int g_lookup_failures;

static void log_failed_lookup(JNIEnv* env, const char* what, jclass c, const char* name, const char* sig)
{
    char line[512], class_name[256] = "?";
    jthrowable pending;
    if (__atomic_fetch_add(&g_lookup_failures, 1, __ATOMIC_RELAXED) >= 100) return;
    pending = (*env)->ExceptionOccurred(env);
    (*env)->ExceptionClear(env);
    if (c != NULL) {
        jclass class_class = (*env)->GetObjectClass(env, c);
        jmethodID get_name = g_get_method_id(env, class_class, "getName", "()Ljava/lang/String;");
        jstring text = get_name != NULL ? (jstring) (*env)->CallObjectMethod(env, c, get_name) : NULL;
        const char* chars = text != NULL ? (*env)->GetStringUTFChars(env, text, NULL) : NULL;
        if (chars != NULL) {
            snprintf(class_name, sizeof(class_name), "%s", chars);
            (*env)->ReleaseStringUTFChars(env, text, chars);
        }
        (*env)->ExceptionClear(env);
    }
    snprintf(line, sizeof(line), "[WL-JNI] %s failed: %s.%s %s\n", what, class_name, name != NULL ? name : "?",
             sig != NULL ? sig : "?");
    note(line);
    if (pending != NULL) (*env)->Throw(env, pending);
}

static jmethodID wl_GetMethodID(JNIEnv* env, jclass c, const char* name, const char* sig)
{
    jmethodID id = g_get_method_id(env, c, name, sig);
    if (id == NULL) log_failed_lookup(env, "GetMethodID", c, name, sig);
    return id;
}

static jmethodID wl_GetStaticMethodID(JNIEnv* env, jclass c, const char* name, const char* sig)
{
    jmethodID id = g_get_static_method_id(env, c, name, sig);
    if (id == NULL) log_failed_lookup(env, "GetStaticMethodID", c, name, sig);
    return id;
}

static jfieldID wl_GetFieldID(JNIEnv* env, jclass c, const char* name, const char* sig)
{
    jfieldID id = g_get_field_id(env, c, name, sig);
    if (id == NULL) log_failed_lookup(env, "GetFieldID", c, name, sig);
    return id;
}

static jfieldID wl_GetStaticFieldID(JNIEnv* env, jclass c, const char* name, const char* sig)
{
    jfieldID id = g_get_static_field_id(env, c, name, sig);
    if (id == NULL) log_failed_lookup(env, "GetStaticFieldID", c, name, sig);
    return id;
}

static int install_lookup_logging(JNIEnv* env, JavaVM* vm)
{
    typedef void (*SetTableOverrideFn)(const struct JNINativeInterface*);
    static const char kSymbol[] = "_ZN3art9JNIEnvExt16SetTableOverrideEPK18JNINativeInterface";
    SetTableOverrideFn set_override = (SetTableOverrideFn) dlsym(RTLD_DEFAULT, kSymbol);
    Dl_info art;
    /* libart is not in this library's global scope; reach it through the VM's own code. */
    if (set_override == NULL && dladdr((void*) (*vm)->GetEnv, &art) != 0 && art.dli_fname != NULL) {
        void* handle = dlopen(art.dli_fname, RTLD_NOW | RTLD_NOLOAD);
        if (handle != NULL) set_override = (SetTableOverrideFn) dlsym(handle, kSymbol);
    }
    if (set_override == NULL || g_get_method_id != NULL) return 0;
    g_lookup_table = **env;
    g_get_method_id = g_lookup_table.GetMethodID;
    g_get_static_method_id = g_lookup_table.GetStaticMethodID;
    g_get_field_id = g_lookup_table.GetFieldID;
    g_get_static_field_id = g_lookup_table.GetStaticFieldID;
    g_lookup_table.GetMethodID = wl_GetMethodID;
    g_lookup_table.GetStaticMethodID = wl_GetStaticMethodID;
    g_lookup_table.GetFieldID = wl_GetFieldID;
    g_lookup_table.GetStaticFieldID = wl_GetStaticFieldID;
    set_override(&g_lookup_table);
    return 1;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv* env = NULL;
    (void) reserved;
    if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_6) != JNI_OK || env == NULL) {
        note("[WL-NATIVES] no JNIEnv in JNI_OnLoad\n");
        return JNI_VERSION_1_6;
    }
    {
        char line[512];
        int m = bind(env, "java/lang/Math", kMath, (int) (sizeof(kMath) / sizeof(kMath[0])));
        int s = bind(env, "java/lang/StrictMath", kStrictMath,
                     (int) (sizeof(kStrictMath) / sizeof(kStrictMath[0])));
        int v = bind(env, "android/view/SurfaceControl", kSurfaceControl,
                     (int) (sizeof(kSurfaceControl) / sizeof(kSurfaceControl[0])));
        int t = bind(env, "jdk/internal/misc/VM", kVm, (int) (sizeof(kVm) / sizeof(kVm[0])));
        int e = bind(env, "java/lang/ProcessEnvironment", kProcessEnvironment,
                     (int) (sizeof(kProcessEnvironment) / sizeof(kProcessEnvironment[0])));
        int mem = bind(env, "libcore/io/Memory", kMemory, (int) (sizeof(kMemory) / sizeof(kMemory[0])));
        int at = bind(env, "android/app/ActivityThread", kActivityThread,
                      (int) (sizeof(kActivityThread) / sizeof(kActivityThread[0])));
        int sq = bind(env, "android/database/sqlite/SQLiteGlobal", kSqliteGlobal,
                      (int) (sizeof(kSqliteGlobal) / sizeof(kSqliteGlobal[0])));
        int sp = wrap_system_properties(env);
        int pr = normalize_thread_priority(env);
        int fs = bind(env, "java/io/UnixFileSystem", kUnixFileSystem,
                      (int) (sizeof(kUnixFileSystem) / sizeof(kUnixFileSystem[0])));
        int tx = wl_register_text(env);
        int ar = wl_register_audiorecord(env);
        int ap = wl_register_audio_ports(env);
        int xp = wl_register_expat(env);
        int jl = install_lookup_logging(env, vm);
        int mb = wl_register_ojluni_nio(env);
        int ls = wl_register_localsocket(env);
        int cv = wl_register_surface_canvas(env);
        int sc = wl_register_sqlite_collations(env);
        int ns = wl_register_notification_switch(env);
        int more = bind(env, "sun/nio/ch/Net", kNet, (int) (sizeof(kNet) / sizeof(kNet[0])))
                   + bind(env, "android/os/Process", kProcess, (int) (sizeof(kProcess) / sizeof(kProcess[0])))
                   + bind(env, "android/os/PerformanceHintManager", kPerformanceHint,
                          (int) (sizeof(kPerformanceHint) / sizeof(kPerformanceHint[0])))
                   + bind(env, "android/os/SELinux", kSELinux, (int) (sizeof(kSELinux) / sizeof(kSELinux[0])))
                   + bind(env, "android/hardware/Camera", kCamera, (int) (sizeof(kCamera) / sizeof(kCamera[0])))
                   + bind(env, "android/media/CamcorderProfile", kCamcorderProfile,
                          (int) (sizeof(kCamcorderProfile) / sizeof(kCamcorderProfile[0])));
        snprintf(line, sizeof(line),
                 "[WL-NATIVES] bound Math=%d/%d StrictMath=%d/%d SurfaceControl=%d/%d VM=%d/%d environ=%d Memory=%d/%d ActivityThread=%d/%d SQLiteGlobal=%d SystemProperties-wrapped=%d thread-priority-was=%d UnixFileSystem=%d AndroidCharacter=%d AudioRecord=%d AudioPorts=%d ExpatParser=%d jni-lookup-log=%d ojluni-nio=%d LocalSocketImpl=%d Surface-canvas=%d SQLite-collations=%d/2 NotificationSwitch=%d more=%d\n",
                 m, (int) (sizeof(kMath) / sizeof(kMath[0])),
                 s, (int) (sizeof(kStrictMath) / sizeof(kStrictMath[0])),
                 v, (int) (sizeof(kSurfaceControl) / sizeof(kSurfaceControl[0])),
                 t, (int) (sizeof(kVm) / sizeof(kVm[0])), e, mem, (int) (sizeof(kMemory) / sizeof(kMemory[0])),
                 at, (int) (sizeof(kActivityThread) / sizeof(kActivityThread[0])), sq, sp, pr, fs, tx, ar, ap, xp, jl, mb, ls, cv, sc, ns, more);
        note(line);
    }
    wl_register_media(env, vm);
    return JNI_VERSION_1_6;
}
