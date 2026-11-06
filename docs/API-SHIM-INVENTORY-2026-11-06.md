# Westlake API shim inventory (2026-11-06)

How much Westlake implements itself to run Android apps on OpenHarmony, counted from this repository
and the most recent local build of each library. Counts are approximate (about ±10%): they come from
scanning sources and dynamic symbol tables, not from a hand-kept list.

## Summary

About **3,300 Android-facing entry points** are Westlake's own, and behind them Westlake calls about
**630 distinct OpenHarmony functions**. Westlake's own code in `framework/` and `native/` is about
156,000 lines.

Most of Android is not shimmed. The framework's Java, ART, hwui, Skia, Minikin and resources run as
AOSP 15 code (`android-15.0.0_r1`). The shims sit only where Android would reach its kernel, its own
system services or hardware. One Android call often needs several OpenHarmony calls, so the two
numbers do not map one to one.

| Layer | Who calls it | Westlake's shims |
|---|---|---|
| 1. System services (Java, Binder interfaces) | the framework on the app's behalf | 14 full adapters with 1,012 methods, plus 38 services answered by lightweight stand-ins: about **52 services** |
| 2. JNI natives | framework Java calling native code | about **1,300** native methods |
| 3. App native ABI | an app's own `.so` files | about **985** functions |
| 4. Mainline module stubs | framework and apps, for modules with no service here | **115** classes |

## 1. System services

Full adapters (classes extending an `I*.Stub`). "Implemented" means the method has its own logic;
"default" means it returns null, 0, false or empty on purpose, which callers accept.

| Interface | Methods | Implemented | Default |
|---|---:|---:|---:|
| IActivityManager | 265 | 109 | 156 |
| IPackageManager | 219 | 28 | 191 |
| IWindowManager | 152 | 51 | 101 |
| IActivityTaskManager | 93 | 48 | 45 |
| IActivityClientController | 81 | 20 | 61 |
| IDisplayManager | 59 | 13 | 46 |
| IWindowSession | 46 | 27 | 19 |
| IInputMethodManager | 36 | 23 | 13 |
| IServiceManager | 14 | 3 | 11 |
| IJobCallback | 11 | 7 | 4 |
| IPermissionManager | 11 | 10 | 1 |
| IApplicationThread | 10 | 4 | 6 |
| IWebViewUpdateService | 10 | 5 | 5 |
| IAppTask | 5 | 3 | 2 |
| **Total** | **1,012** | **351** | **661** |

Lightweight stand-ins (`core/java/LocalServiceBinders.java`, `ServiceAnswers.java`) answer 38 more
services by name, among them accounts, alarm, audio, clipboard, notification, power, Wi-Fi, battery,
device idle, thermal, usage stats, storage stats, device policy, role, wallpaper, UI mode, VPN, NSD,
USB, media router, locale, Bluetooth and package installer. In the shared Play services process the
account manager is a real store (`LocalAccounts`).

## 2. JNI natives

Registered `JNINativeMethod` entries plus exported `Java_*` functions, by directory:

| Directory | Registered | `Java_*` |
|---|---:|---:|
| `framework/android-runtime` | 567 | 118 |
| `framework/javacore-shim` | 247 | 65 |
| `framework/opengl-jni` | 79 | 0 |
| `framework/core/jni` | 47 | 11 |
| `framework/audioeffect-shim` | 33 | 0 |
| `framework/window/jni` | 26 | 37 |
| `framework/surface/jni` | 21 | 0 |
| `framework/package-manager/jni` | 0 | 17 |
| `framework/contentprovider/jni` | 0 | 9 |
| `framework/activity/jni` | 6 | 6 |
| other | 0 | 6 |
| **Total** | **1,026** | **269** |

## 3. App native ABI

Functions exported to an app's native code (defined dynamic symbols):

| Library | Functions | Covers |
|---|---:|---|
| Bionic shim (`libwebview_bionic_shim`, `libwestlake_bionic`) | 506 | Android's libc on OpenHarmony's musl |
| `libwl_missing_natives` | 154 | natives the framework expects and no library provides |
| `libmediandk` | 123 | media codecs, extractor, formats |
| `libandroid` | 75 | native window, assets, looper, input |
| `libjnigraphics` | 59 | bitmap access from native code |
| `libcamera2ndk` | 49 | camera |
| `libandroid_native_network_compat` | 13 | network ABI translation |
| `libsync` | 6 | sync fences |
| **Total** | **985** | |

Khronos APIs (EGL, GLES, Vulkan, OpenSL ES) are the same on both sides and pass through to
OpenHarmony's own libraries, with small fixes (GLES 1, GLES 3 lookup in the GLES 2 library, OpenSL ES
ABI).

## 4. Mainline module stubs

`framework/mainline-stubs`: 115 classes standing in for Mainline modules that have no service here
(networking and IKE, Wi-Fi, Bluetooth, NFC, UWB, health connect, app search, ad services, SDK sandbox,
Conscrypt names, phone numbers and others), so framework code that names them loads.

## OpenHarmony functions called

- **About 320 public NDK functions** (`OH_*`) referenced in source, across 30 libraries. The largest:
  native drawing 60, OHAudio 37, AVPlayer 26, codec base 24, native image 19, media core 18, video
  decoder 14, input method 13, native window 13, audio codec 13, native buffer 12, UDMF 9, pixel map 8,
  net connection 8.
- **About 370 linked directly**, mostly platform C++ interfaces rather than the public NDK: Skia and
  render service, window and display management, ability manager and Want, common events, input
  method. The adapter bridge library (`liboh_adapter_bridge`) alone links 257.
- Many shims load OpenHarmony libraries at run time (`dlopen`/`dlsym`) rather than linking them, so
  the source references are the fuller count. The union of the two is about 630.

## How these were counted

- Services: classes extending `I*.Stub`; a method counts as default when its body, without logging, is
  empty or a single default return. Stand-ins: interface names proxied in `core/java`.
- JNI: lines opening a `{ "name", "(signature)" ...}` entry, and `Java_*` definitions, in tracked
  C/C++ sources.
- App ABI and directly linked OpenHarmony functions: `llvm-nm -D` on the built libraries. Defined
  `T`/`W` symbols are exports; undefined symbols are matched against the exports of the OpenHarmony
  SDK's sysroot libraries and the board's system libraries (libc, libm, libdl, C++ runtime and
  Khronos libraries left out).
- `OH_*` references: identifiers in tracked `framework/` and `native/` sources that are exported by
  an OpenHarmony library (constants and type names are left out that way).
