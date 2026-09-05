/*
 * Android legacy libstdc++ compatibility DSO for the OH native namespace.
 *
 * Android kept a small libstdc++.so compatibility library after moving the
 * platform and NDK to libc++.  Some APK libraries still retain a DT_NEEDED
 * entry for that old SONAME even though they import no symbols from it.  OHOS
 * does not ship the SONAME, so its dynamic loader rejects those otherwise
 * valid APK libraries before JNI_OnLoad can register their methods.
 *
 * The Android app namespace also contains the APK's libc++_shared.so, which
 * provides the real C++ ABI.  This intentionally empty DSO only satisfies the
 * legacy dependency name; it must not redirect Android code to OH's libc++.
 */

__attribute__((visibility("default")))
void westlake_android_libstdcxx_compat_anchor(void) {}
