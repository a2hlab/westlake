# framework/sqlite — `libwlsqlite.so`

The SQLite engine plus AOSP's `android.database` JNI boundary, built from
source in this repository.

## Why

The adapter's `framework.jar` carries AOSP's `android.database.sqlite` Java
layer with none of its natives implemented, so the first
`openOrCreateDatabase()` throws `UnsatisfiedLinkError` and takes `Application`
start-up with it. The board's own `libsqlite.z.so` is not a substitute: it
exports three `sqlite3_*` symbols, none of them usable here.

## What is where

| Path | Contents |
| --- | --- |
| `android_database_*.cpp/.h` | AOSP's JNI boundary, unmodified |
| `wl_CursorWindow.cpp`, `wl_sqlite3_android.cpp` | Westlake's replacements for the parts that assume ashmem and an ICU-backed collator |
| `wl_compat.cpp`, `wl_jni_onload.cpp` | The compatibility bodies and the single entry point |
| `compat/` | The seventeen platform headers this tree does not otherwise have |
| `inc/android_runtime/Log.h` | stderr logging, so nothing links `liblog` |
| `wlsqlite.map` | Export list: `JNI_OnLoad` and nothing else |
| `../../third_party/sqlite/` | SQLite 3.45.1 amalgamation, unmodified |

## Building

Two ABIs. The choice is not cosmetic — see the header comment in
`CMakeLists.txt` for the full reasoning.

**Deployable (bionic).** The adapter's app process is served by the
Android-side linker namespace, so the artifact that can actually be loaded there
must be bionic-linked:

```sh
cmake -S framework/sqlite -B <build> \
      -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30
cmake --build <build>
```

or, equivalently, through the repository's own driver:

```sh
python3 tools/build_sqlite.py --ndk <ndk> --api 30 --out <new dir>
```

**Verification (OHOS/musl).** Builds against the pinned OpenHarmony native SDK.
This is what the manifest workspace can run entirely offline, and passing it
proves the source closes: every symbol resolves, no platform header is missing.
It is not the artifact to deploy into the app process.

```sh
python3 tools/build_sqlite.py \
    --sdk <workspace>/toolchains/ohos-sdk/native --out <new dir>
```

An Android NDK is not part of the content-locked toolchain set, so the operator
supplies it and `build_sqlite.py` records its identity in `artifacts.json`
rather than assuming one.

## Verified

Verification build, 2026-09-05, against the manifest-pinned
`ohos-sdk 6.1.0.31` native SDK (`tree_sha256 2b7a2854…88f4f`):

```
CC sqlite3.c … android_database_CursorWindow.cpp        (8 translation units)
BUILT libwlsqlite.so
  sha256 fc2d5d0f011817c82739d0d548f55ac2127442a3ed4130aaf9a117558e2f0a46
  ELF 64-bit LSB shared object, ARM aarch64, BuildID[sha1]=73bbed5f…
  NEEDED: libc.so                       (no liblog, no libc++_shared)
  defined dynamic FUNC symbols: JNI_OnLoad, and only JNI_OnLoad
```

`--no-undefined` is on, so that link succeeding *is* the closure check: an
unresolved symbol would mean a missing compat shim, and we want to hear about it
here rather than at the first database open on a board.

`build_sqlite.py` asserts the single-export property itself and fails the build
if anything else becomes visible — `-fvisibility=hidden` governs only our own
translation units, and the static libc++ archive was compiled with default
visibility, so without the version script roughly 1500 `std::` symbols leak out.

Not verified on hardware: this workstation has no board access.
