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
 *   android.media -- MediaPlayer, and the AudioTrack and AudioSystem natives the AudioTrack shim
 *   leaves out: see media_natives.c.
 *
 * Bound with RegisterNatives rather than left to be found by name, which is what lets this library
 * supply a method belonging to a boot class without being the library that owns it.
 */
#include <jni.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
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

#define MEM(Name, Sig) { #Name, Sig, (void*) wl_##Name }
static const JNINativeMethod kMemory[] = {
    MEM(peekByteArray, "(J[BII)V"), MEM(pokeByteArray, "(J[BII)V"),
    MEM(peekCharArray, "(J[CIIZ)V"), MEM(pokeCharArray, "(J[CIIZ)V"),
    MEM(peekShortArray, "(J[SIIZ)V"), MEM(pokeShortArray, "(J[SIIZ)V"),
    MEM(peekIntArray, "(J[IIIZ)V"), MEM(pokeIntArray, "(J[IIIZ)V"),
    MEM(peekLongArray, "(J[JIIZ)V"), MEM(pokeLongArray, "(J[JIIZ)V"),
    MEM(peekFloatArray, "(J[FIIZ)V"), MEM(pokeFloatArray, "(J[FIIZ)V"),
    MEM(peekDoubleArray, "(J[DIIZ)V"), MEM(pokeDoubleArray, "(J[DIIZ)V"),
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

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv* env = NULL;
    (void) reserved;
    if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_6) != JNI_OK || env == NULL) {
        note("[WL-NATIVES] no JNIEnv in JNI_OnLoad\n");
        return JNI_VERSION_1_6;
    }
    {
        char line[192];
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
        snprintf(line, sizeof(line),
                 "[WL-NATIVES] bound Math=%d/%d StrictMath=%d/%d SurfaceControl=%d/%d VM=%d/%d environ=%d Memory=%d/%d ActivityThread=%d/%d SQLiteGlobal=%d\n",
                 m, (int) (sizeof(kMath) / sizeof(kMath[0])),
                 s, (int) (sizeof(kStrictMath) / sizeof(kStrictMath[0])),
                 v, (int) (sizeof(kSurfaceControl) / sizeof(kSurfaceControl[0])),
                 t, (int) (sizeof(kVm) / sizeof(kVm[0])), e, mem, (int) (sizeof(kMemory) / sizeof(kMemory[0])),
                 at, (int) (sizeof(kActivityThread) / sizeof(kActivityThread[0])), sq);
        note(line);
    }
    wl_register_media(env, vm);
    return JNI_VERSION_1_6;
}
