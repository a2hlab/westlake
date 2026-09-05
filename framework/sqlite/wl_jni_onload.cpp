/* Entry point for libwlsqlite.so.
 *
 * The adapter declares android.database's JNI entry points in
 * liboh_android_runtime.so but ships no implementation behind them: that library
 * exports register_android_database_CursorWindow yet contains zero sqlite3_*
 * symbols, and no libsqlite*.so exists anywhere on the board.  In-process probes
 * confirmed both halves are dead:
 *
 *   UnsatisfiedLinkError: No implementation found for
 *     android.database.sqlite.SQLiteConnection.nativeOpen(...)
 *   UnsatisfiedLinkError: No implementation found for
 *     android.database.CursorWindow.nativeCreate(...)
 *
 * With no database engine, AppLog/BdInstall cannot create its event store, so
 * device_register is never sent, device_id and iid stay empty, and the feed is
 * never requested -- which is why the app renders an empty list despite 47/47
 * successful TLS handshakes and ~30 business APIs returning 200.
 *
 * This library supplies the missing half and attaches it with RegisterNatives.
 * That mechanism was verified first, with a single stub method, before any of
 * this was written: five earlier injection routes for the ALooper shim all
 * failed because they needed the shim's symbols to be *globally* visible so a
 * later dlopen could relocate against them, and the sealed.child namespace
 * forbids that.  RegisterNatives has no such requirement -- it attaches function
 * pointers to a class by explicit call, regardless of namespace or classloader --
 * and the stub duly turned "No implementation found" into its own exception.
 *
 * Load order matters: this must be loaded before the app opens its first
 * database.  AMR does that from bindApplication, the same hook already used for
 * the TLS shim.
 */

#include <jni.h>

#include <stdio.h>

#include "android_runtime/AndroidRuntime.h"

namespace android {
int register_android_database_SQLiteConnection(JNIEnv* env);
int register_android_database_CursorWindow(JNIEnv* env);
}  // namespace android

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || env == nullptr) {
        fprintf(stderr, "[WL-SQLITE] JNI_OnLoad: GetEnv failed\n");
        fflush(stderr);
        return JNI_ERR;
    }

    // The sqlite trace/profile/progress hooks call AndroidRuntime::getJNIEnv()
    // on whatever thread is driving the statement, so the VM has to be reachable
    // from a global before any of them can fire.
    android::AndroidRuntime::setJavaVM(vm);

    // CursorWindow first: SQLiteConnection's nativeExecuteForCursorWindow writes
    // result rows into a window, so registering the connection while the window
    // is still unimplemented would leave a half-working pair.  Both halves are
    // ours here -- the adapter exports none of android::CursorWindow's accessors
    // -- so there is no foreign buffer implementation to interoperate with.
    int cwRc = android::register_android_database_CursorWindow(env);
    if (env->ExceptionCheck()) env->ExceptionClear();

    int connRc = android::register_android_database_SQLiteConnection(env);
    if (env->ExceptionCheck()) env->ExceptionClear();

    fprintf(stderr, "[WL-SQLITE] JNI_OnLoad: CursorWindow=%s SQLiteConnection=%s\n",
            cwRc >= 0 ? "OK" : "FAILED",
            connRc == JNI_OK ? "OK" : "FAILED");
    fflush(stderr);

    // Return success even on a partial bind: a database that fails later with a
    // specific missing method is far easier to diagnose from the log above than
    // a bare UnsatisfiedLinkError on System.loadLibrary with nothing attached.
    return JNI_VERSION_1_6;
}
