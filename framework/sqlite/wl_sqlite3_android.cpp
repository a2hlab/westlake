/* WESTLAKE: the collations Android's SQLiteConnection registers on every connection, as
 * external/sqlite/android/sqlite3_android.cpp registers them for UTF-8 storage (SQLiteConnection's
 * UTF16_STORAGE is 0): UNICODE with ICU's root collator, LOCALIZED and PHONEBOOK with the system
 * locale's at primary strength. The phone-number and file functions AOSP registers alongside are
 * not provided. */
#include "sqlite3_android.h"
#include <string>
#include <cstring>
#include <clocale>
#include <stdio.h>
#include <unicode/ucol.h>
#include <unicode/uiter.h>

/* Byte order with a stable tiebreak: the comparator when ICU gives no collator. musl's strcoll
 * compares bytes in every locale. */
static int wl_localized_cmp(void*, int lLen, const void* lhs, int rLen, const void* rhs) {
    std::string a(static_cast<const char*>(lhs), static_cast<size_t>(lLen));
    std::string b(static_cast<const char*>(rhs), static_cast<size_t>(rLen));
    int r = strcoll(a.c_str(), b.c_str());
    if (r != 0) return (r < 0) ? -1 : 1;
    // Stable tiebreak so equal collation keys still order deterministically.
    if (a == b) return 0;
    return (a < b) ? -1 : 1;
}

/* AOSP's collate8: ICU's order of two UTF-8 strings. */
static int wl_icu_cmp8(void* collator, int lLen, const void* lhs, int rLen, const void* rhs) {
    UCharIterator left, right;
    UErrorCode status = U_ZERO_ERROR;
    uiter_setUTF8(&left, static_cast<const char*>(lhs), lLen);
    uiter_setUTF8(&right, static_cast<const char*>(rhs), rLen);
    UCollationResult result = ucol_strcollIter(static_cast<UCollator*>(collator), &left, &right, &status);
    return result == UCOL_LESS ? -1 : result == UCOL_GREATER ? 1 : 0;
}

static void wl_icu_close(void* collator) {
    ucol_close(static_cast<UCollator*>(collator));
}

/* One collation over ICU's collator for a locale (null: the root), or over byte order if ICU has
 * none to give. */
static int wl_register_collation(sqlite3* handle, const char* name, const char* locale, bool primary) {
    UErrorCode status = U_ZERO_ERROR;
    UCollator* collator = ucol_open(locale, &status);
    if (U_SUCCESS(status) && primary) ucol_setAttribute(collator, UCOL_STRENGTH, UCOL_PRIMARY, &status);
    if (U_FAILURE(status) || collator == nullptr) {
        if (collator != nullptr) ucol_close(collator);
        fprintf(stderr, "[WESTLAKE-COLLATE] %s: no ICU collator for '%s' (%d); byte order\n", name,
                locale != nullptr ? locale : "root", status);
        return sqlite3_create_collation(handle, name, SQLITE_UTF8, nullptr, wl_localized_cmp);
    }
    return sqlite3_create_collation_v2(handle, name, SQLITE_UTF8, collator, wl_icu_cmp8, wl_icu_close);
}

/* WESTLAKE 2026-10-05: this registered nothing, and Android registers UNICODE on every connection.
 * Fossify Notes reads its notes with "ORDER BY title COLLATE UNICODE"; each such query failed on the
 * app's worker threads ("no such collation sequence: UNICODE"). */
extern "C" int register_android_functions(sqlite3* handle, int utf16Storage) {
    (void)utf16Storage;
    if (handle == nullptr) return SQLITE_OK;
    return wl_register_collation(handle, "UNICODE", nullptr, false);
}

/* WESTLAKE 2026-07-22 (§158): Room runs "REINDEX LOCALIZED" when it opens its database. With no
 * collation of that name registered, sqlite3 fails with
 *   "unable to identify the object to be reindexed (code 1 SQLITE_ERROR)"
 * that SQLiteException lands on a Kotlin coroutine, the port's no-op ThreadGroup.uncaughtException
 * discards it, the coroutine dies, MainActivity.onCreate never returns, the Looper never dispatches
 * the next Activity launch, and no window is ever created. LOCALIZED (and PHONEBOOK) now order as
 * AOSP's do, by ICU for the system locale at primary strength.
 */
extern "C" int register_localized_collators(sqlite3* handle, const char* systemLocale,
                                            int utf16Storage) {
    (void)utf16Storage;
    if (handle == nullptr) return SQLITE_OK;
    int rc = wl_register_collation(handle, "LOCALIZED", systemLocale, true);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[WESTLAKE-COLLATE] LOCALIZED registration failed rc=%d\n", rc);
        fflush(stderr);
        return rc;
    }
    wl_register_collation(handle, "PHONEBOOK", systemLocale, true);
    fprintf(stderr, "[WESTLAKE-COLLATE] LOCALIZED + PHONEBOOK registered\n");
    fflush(stderr);
    return SQLITE_OK;
}
