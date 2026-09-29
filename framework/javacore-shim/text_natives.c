/*
 * android.text.AndroidCharacter natives, ported from AOSP core/jni/android_text_AndroidCharacter.cpp.
 *
 * None of the five was registered. Text layout asks for mirroring and directionality of every
 * bidirectional run, so an app that lays out such text dies on the first one: Discord (React
 * Native) threw on AndroidCharacter.getMirror before its first screen. The answers come from the
 * runtime's own ICU (libicuuc.so, Android's build), found by its versioned entry points at load, so
 * this matches what Android's libandroid_runtime computes from the same library.
 */
#include <jni.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define PROPERTY_UNDEFINED (-1)
#define JAVA_LANG_CHARACTER_MAX_DIRECTIONALITY 18
#define UCHAR_EAST_ASIAN_WIDTH 0x1004
#define U_UNASSIGNED 0

static struct {
    int (*charDirection)(int);
    int (*charType)(int);
    int (*charMirror)(int);
    int (*getIntPropertyValue)(int, int);
    int (*getIntPropertyMaxValue)(int);
} icu;

/* AOSP's table: ICU UCharDirection -> java.lang.Character.DIRECTIONALITY_*. */
static const int directionality_map[JAVA_LANG_CHARACTER_MAX_DIRECTIONALITY + 1] = {
    0, 1, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 2, 16, 17, 18, 8, 9,
};

static void* icu_symbol(void* lib, const char* name)
{
    char versioned[64];
    void* sym;
    int v;
    for (v = 80; v >= 60; v--) {
        snprintf(versioned, sizeof(versioned), "%s_%d", name, v);
        if ((sym = dlsym(lib, versioned)) != NULL) return sym;
    }
    return dlsym(lib, name);
}

static int load_icu(void)
{
    void* lib = dlopen("libicuuc.so", RTLD_NOW);
    if (lib == NULL) return 0;
    *(void**) &icu.charDirection = icu_symbol(lib, "u_charDirection");
    *(void**) &icu.charType = icu_symbol(lib, "u_charType");
    *(void**) &icu.charMirror = icu_symbol(lib, "u_charMirror");
    *(void**) &icu.getIntPropertyValue = icu_symbol(lib, "u_getIntPropertyValue");
    *(void**) &icu.getIntPropertyMaxValue = icu_symbol(lib, "u_getIntPropertyMaxValue");
    return icu.charDirection && icu.charType && icu.charMirror && icu.getIntPropertyValue
           && icu.getIntPropertyMaxValue;
}

static void throw_aioobe(JNIEnv* env)
{
    jclass c = (*env)->FindClass(env, "java/lang/ArrayIndexOutOfBoundsException");
    if (c != NULL) (*env)->ThrowNew(env, c, NULL);
}

static int direction_of(int c)
{
    int dir = icu.charDirection(c);
    if (dir < 0 || dir > JAVA_LANG_CHARACTER_MAX_DIRECTIONALITY || icu.charType(c) == U_UNASSIGNED) {
        return PROPERTY_UNDEFINED;
    }
    return directionality_map[dir];
}

static void getDirectionalities(JNIEnv* env, jclass cls, jcharArray srcArray, jbyteArray destArray, jint count)
{
    jchar* src;
    jbyte* dest;
    int i;
    (void) cls;
    if ((*env)->GetArrayLength(env, srcArray) < count || (*env)->GetArrayLength(env, destArray) < count) {
        throw_aioobe(env);
        return;
    }
    src = (*env)->GetCharArrayElements(env, srcArray, NULL);
    dest = (*env)->GetByteArrayElements(env, destArray, NULL);
    if (src == NULL || dest == NULL) goto out;
    for (i = 0; i < count; i++) {
        if (src[i] >= 0xD800 && src[i] <= 0xDBFF && i + 1 < count && src[i + 1] >= 0xDC00 && src[i + 1] <= 0xDFFF) {
            int dir = direction_of(0x10000 + ((src[i] - 0xD800) << 10) + (src[i + 1] & 0x3FF));
            dest[i++] = (jbyte) dir;
            dest[i] = (jbyte) dir;
        } else {
            dest[i] = (jbyte) direction_of(src[i]);
        }
    }
out:
    if (src != NULL) (*env)->ReleaseCharArrayElements(env, srcArray, src, JNI_ABORT);
    if (dest != NULL) (*env)->ReleaseByteArrayElements(env, destArray, dest, 0);
}

static int width_of(int c, int max)
{
    int width = icu.getIntPropertyValue(c, UCHAR_EAST_ASIAN_WIDTH);
    return (width < 0 || width > max) ? PROPERTY_UNDEFINED : width;
}

static jint getEastAsianWidth(JNIEnv* env, jclass cls, jchar input)
{
    (void) env; (void) cls;
    return width_of(input, icu.getIntPropertyMaxValue(UCHAR_EAST_ASIAN_WIDTH));
}

static void getEastAsianWidths(JNIEnv* env, jclass cls, jcharArray srcArray, jint start, jint count,
                               jbyteArray destArray)
{
    jchar* src;
    jbyte* dest;
    int i, max;
    (void) cls;
    if (start < 0 || start > start + count || (*env)->GetArrayLength(env, srcArray) < start + count
            || (*env)->GetArrayLength(env, destArray) < count) {
        throw_aioobe(env);
        return;
    }
    src = (*env)->GetCharArrayElements(env, srcArray, NULL);
    dest = (*env)->GetByteArrayElements(env, destArray, NULL);
    if (src == NULL || dest == NULL) goto out;
    max = icu.getIntPropertyMaxValue(UCHAR_EAST_ASIAN_WIDTH);
    for (i = 0; i < count; i++) {
        const int s = start + i;
        if (src[s] >= 0xD800 && src[s] <= 0xDBFF && i + 1 < count && src[s + 1] >= 0xDC00 && src[s + 1] <= 0xDFFF) {
            int width = width_of(0x10000 + ((src[s] - 0xD800) << 10) + (src[s + 1] & 0x3FF), max);
            dest[i++] = (jbyte) width;
            dest[i] = (jbyte) width;
        } else {
            dest[i] = (jbyte) width_of(src[s], max);
        }
    }
out:
    if (src != NULL) (*env)->ReleaseCharArrayElements(env, srcArray, src, JNI_ABORT);
    if (dest != NULL) (*env)->ReleaseByteArrayElements(env, destArray, dest, 0);
}

static jboolean mirror(JNIEnv* env, jclass cls, jcharArray charArray, jint start, jint count)
{
    jchar* data;
    jboolean changed = JNI_FALSE;
    int i;
    (void) cls;
    if (start < 0 || start > start + count || (*env)->GetArrayLength(env, charArray) < start + count) {
        throw_aioobe(env);
        return JNI_FALSE;
    }
    data = (*env)->GetCharArrayElements(env, charArray, NULL);
    if (data == NULL) return JNI_FALSE;
    for (i = start; i < start + count; i++) {
        int c2 = icu.charMirror(data[i]);   /* as AOSP: surrogates are never mirrored */
        if (c2 != data[i]) {
            data[i] = (jchar) c2;
            changed = JNI_TRUE;
        }
    }
    (*env)->ReleaseCharArrayElements(env, charArray, data, 0);
    return changed;
}

static jchar getMirror(JNIEnv* env, jclass cls, jchar c)
{
    (void) env; (void) cls;
    return (jchar) icu.charMirror(c);
}

static const JNINativeMethod kAndroidCharacter[] = {
    { "getDirectionalities", "([C[BI)V", (void*) getDirectionalities },
    { "getEastAsianWidths", "([CII[B)V", (void*) getEastAsianWidths },
    { "getEastAsianWidth", "(C)I", (void*) getEastAsianWidth },
    { "mirror", "([CII)Z", (void*) mirror },
    { "getMirror", "(C)C", (void*) getMirror },
};

/* Returns how many were bound (0 when ICU is not found: the natives then stay unbound, as before). */
int wl_register_text(JNIEnv* env)
{
    jclass c;
    int bound = 0, i;
    if (!load_icu()) return 0;
    c = (*env)->FindClass(env, "android/text/AndroidCharacter");
    if (c == NULL) { (*env)->ExceptionClear(env); return 0; }
    for (i = 0; i < (int) (sizeof(kAndroidCharacter) / sizeof(kAndroidCharacter[0])); i++) {
        if ((*env)->RegisterNatives(env, c, &kAndroidCharacter[i], 1) == JNI_OK) bound++;
        else (*env)->ExceptionClear(env);
    }
    (*env)->DeleteLocalRef(env, c);
    return bound;
}
