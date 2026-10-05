/*
 * android.database.sqlite.SQLiteConnection.nativeOpen / nativeRegisterLocalizedCollators -- wrapped,
 * not replaced.
 *
 * Android registers the UNICODE collation on every connection, and LOCALIZED and PHONEBOOK for the
 * system locale, all over ICU (external/sqlite/android/sqlite3_android.cpp, UTF-8 storage). The
 * runtime's liboh_android_runtime.so (which cannot be rebuilt here) registers no UNICODE, and its
 * LOCALIZED and PHONEBOOK compare with strcoll, which musl implements as byte order in every locale.
 * Fossify Notes reads its notes with "ORDER BY title COLLATE UNICODE", and each such query failed
 * on its worker threads ("no such collation sequence: UNICODE").
 *
 * After the runtime's nativeOpen returns its connection (AOSP's SQLiteConnection, whose first field
 * is the sqlite3 handle), UNICODE is registered with ICU's root collator; after its
 * nativeRegisterLocalizedCollators, LOCALIZED and PHONEBOOK are registered again with the locale's
 * collator at primary strength, replacing the byte-order ones. The phone-number and file functions
 * AOSP registers alongside are still not provided. The registered implementations are taken from
 * ART's method record and wrapped only if they lie in liboh_android_runtime.so (see canvas_natives.c).
 */
#include <dlfcn.h>
#include <jni.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WL_SQLITE_OK 0
#define WL_SQLITE_UTF8 1
#define WL_UCOL_PRIMARY 0

typedef jlong (*OpenFn)(JNIEnv*, jclass, jstring, jint, jstring, jboolean, jboolean, jint, jint);
typedef void (*LocalizedFn)(JNIEnv*, jclass, jlong, jstring);
typedef int (*CreateCollationFn)(void*, const char*, int, void*, int (*)(void*, int, const void*, int, const void*),
                                 void (*)(void*));
typedef void* (*UcolOpenFn)(const char*, int*);
typedef void (*UcolCloseFn)(void*);
typedef void (*UcolSetStrengthFn)(void*, int);
typedef int (*UcolStrcollUtf8Fn)(const void*, const char*, int32_t, const char*, int32_t, int*);

static OpenFn g_open;
static LocalizedFn g_localized;
static CreateCollationFn g_create_collation;
static UcolOpenFn g_ucol_open;
static UcolCloseFn g_ucol_close;
static UcolSetStrengthFn g_ucol_set_strength;
static UcolStrcollUtf8Fn g_ucol_strcoll_utf8;

/* AOSP's collate8: ICU's order of two UTF-8 strings. */
static int wl_collate8(void* collator, int left_length, const void* left, int right_length, const void* right)
{
    int status = 0;
    int result = g_ucol_strcoll_utf8(collator, (const char*) left, left_length, (const char*) right,
                                     right_length, &status);
    return status > 0 ? 0 : result;   /* UCOL_LESS -1, UCOL_EQUAL 0, UCOL_GREATER 1 */
}

static void wl_collator_close(void* collator)
{
    g_ucol_close(collator);
}

/* One collation over ICU's collator for a locale (NULL: the root); 0 when registered. */
static int wl_register_collation(void* db, const char* name, const char* locale, int primary)
{
    int status = 0;
    void* collator = g_ucol_open(locale, &status);
    if (status > 0 || collator == NULL) return -1;
    if (primary) g_ucol_set_strength(collator, WL_UCOL_PRIMARY);
    if (g_create_collation(db, name, WL_SQLITE_UTF8, collator, wl_collate8, wl_collator_close) != WL_SQLITE_OK) {
        g_ucol_close(collator);
        return -1;
    }
    return 0;
}

static jlong wl_open(JNIEnv* env, jclass clazz, jstring path, jint flags, jstring label, jboolean trace,
                     jboolean profile, jint lookaside_size, jint lookaside_count)
{
    jlong connection = g_open(env, clazz, path, flags, label, trace, profile, lookaside_size, lookaside_count);
    void* db = connection != 0 ? *(void* const*) (intptr_t) connection : NULL;
    if (db != NULL && wl_register_collation(db, "UNICODE", NULL, 0) != 0) {
        static int logged;
        if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) {
            fprintf(stderr, "[WL-SQLITE] no ICU collator: UNICODE not registered\n");
        }
    }
    return connection;
}

static void wl_register_localized(JNIEnv* env, jclass clazz, jlong connection, jstring locale)
{
    const char* name;
    void* db;
    g_localized(env, clazz, connection, locale);
    if ((*env)->ExceptionCheck(env) || connection == 0 || locale == NULL) return;
    db = *(void* const*) (intptr_t) connection;
    name = (*env)->GetStringUTFChars(env, locale, NULL);
    if (db != NULL && name != NULL) {
        wl_register_collation(db, "LOCALIZED", name, 1);
        wl_register_collation(db, "PHONEBOOK", name, 1);
    }
    if (name != NULL) (*env)->ReleaseStringUTFChars(env, locale, name);
}

/* ICU's C API carries its version in every name (ucol_open_75). */
static void* icu_symbol(void* library, const char* base, int version)
{
    char name[64];
    snprintf(name, sizeof(name), "%s_%d", base, version);
    return dlsym(library, name);
}

static int find_icu(void)
{
    void* i18n = dlopen("libicui18n.so", RTLD_NOW | RTLD_NOLOAD);
    int version;
    if (i18n == NULL) return 0;
    for (version = 90; version >= 60 && g_ucol_open == NULL; version--) {
        g_ucol_open = (UcolOpenFn) icu_symbol(i18n, "ucol_open", version);
        if (g_ucol_open != NULL) {
            g_ucol_close = (UcolCloseFn) icu_symbol(i18n, "ucol_close", version);
            g_ucol_set_strength = (UcolSetStrengthFn) icu_symbol(i18n, "ucol_setStrength", version);
            g_ucol_strcoll_utf8 = (UcolStrcollUtf8Fn) icu_symbol(i18n, "ucol_strcollUTF8", version);
        }
    }
    return g_ucol_open != NULL && g_ucol_close != NULL && g_ucol_set_strength != NULL && g_ucol_strcoll_utf8 != NULL;
}

/* The registered native of a static method, if liboh_android_runtime.so's. */
static void* runtime_native(JNIEnv* env, jclass c, const char* name, const char* signature)
{
    jmethodID m = (*env)->GetStaticMethodID(env, c, name, signature);
    void* entry;
    Dl_info info;
    if (m == NULL) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    entry = *(void* const*) ((const char*) m + 16);   /* ArtMethod::ptr_sized_fields_.data_ */
    if (entry == NULL || dladdr(entry, &info) == 0 || info.dli_fname == NULL
            || strstr(info.dli_fname, "liboh_android_runtime") == NULL) {
        return NULL;
    }
    return entry;
}

/* Returns how many of the two are wrapped. */
int wl_register_sqlite_collations(JNIEnv* env)
{
    static const JNINativeMethod open_method = {
        "nativeOpen", "(Ljava/lang/String;ILjava/lang/String;ZZII)J", (void*) wl_open };
    static const JNINativeMethod localized_method = {
        "nativeRegisterLocalizedCollators", "(JLjava/lang/String;)V", (void*) wl_register_localized };
    void* runtime = dlopen("liboh_android_runtime.so", RTLD_NOW | RTLD_NOLOAD);
    jclass c;
    int wrapped = 0;
    if (runtime == NULL || !find_icu()) return 0;
    g_create_collation = (CreateCollationFn) dlsym(runtime, "sqlite3_create_collation_v2");
    if (g_create_collation == NULL) return 0;
    c = (*env)->FindClass(env, "android/database/sqlite/SQLiteConnection");
    if (c == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    g_open = (OpenFn) runtime_native(env, c, open_method.name, open_method.signature);
    if (g_open != NULL && (*env)->RegisterNatives(env, c, &open_method, 1) == JNI_OK) wrapped++;
    else (*env)->ExceptionClear(env);
    g_localized = (LocalizedFn) runtime_native(env, c, localized_method.name, localized_method.signature);
    if (g_localized != NULL && (*env)->RegisterNatives(env, c, &localized_method, 1) == JNI_OK) wrapped++;
    else (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, c);
    return wrapped;
}
