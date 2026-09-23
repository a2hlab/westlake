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
 *   android.view.SurfaceControl.nativeSetDesiredHdrHeadroom -- registered by
 *   liboh_android_runtime.so, whose object tree no longer builds. LocalSend's Flutter engine
 *   reaches it through SurfaceView.performSurfaceTransaction before its first frame.
 *
 * Bound with RegisterNatives rather than left to be found by name, which is what lets this library
 * supply a method belonging to a boot class without being the library that owns it.
 */
#include <jni.h>
#include <math.h>
#include <stdio.h>
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

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv* env = NULL;
    (void) reserved;
    if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_6) != JNI_OK || env == NULL) {
        note("[WL-NATIVES] no JNIEnv in JNI_OnLoad\n");
        return JNI_VERSION_1_6;
    }
    {
        char line[128];
        int m = bind(env, "java/lang/Math", kMath, (int) (sizeof(kMath) / sizeof(kMath[0])));
        int s = bind(env, "java/lang/StrictMath", kStrictMath,
                     (int) (sizeof(kStrictMath) / sizeof(kStrictMath[0])));
        int v = bind(env, "android/view/SurfaceControl", kSurfaceControl,
                     (int) (sizeof(kSurfaceControl) / sizeof(kSurfaceControl[0])));
        snprintf(line, sizeof(line),
                 "[WL-NATIVES] bound Math=%d/%d StrictMath=%d/%d SurfaceControl=%d/%d\n",
                 m, (int) (sizeof(kMath) / sizeof(kMath[0])),
                 s, (int) (sizeof(kStrictMath) / sizeof(kStrictMath[0])),
                 v, (int) (sizeof(kSurfaceControl) / sizeof(kSurfaceControl[0])));
        note(line);
    }
    return JNI_VERSION_1_6;
}
