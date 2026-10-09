#!/bin/bash
# build_bionic_shim.sh <oh-sdk-native> <out-dir> [description]
# Builds libwebview_bionic_shim.so: the bionic compatibility layer (webview_bionic_shim.c), the
# Android OpenSL ES adapter, AAudio over OHAudio, the DT_ANDROID_RELR loader, libandroid's
# AConfiguration, sensor and input calls and Bionic's fts; the libaaudio.so staged beside it for
# DT_NEEDED; and artifacts.json for a launch's --bionic-shim input.
set -e
SDK=$1; O=$2; DESC=${3:-bionic shim}
W=$(cd "$(dirname "$0")/.." && pwd)/framework/webview-shim
RT=$(ls -d $SDK/llvm/lib/clang/*/lib/aarch64-linux-ohos | head -1); CRT=$SDK/sysroot/usr/lib/aarch64-linux-ohos
rm -rf $O && mkdir -p $O
CC="$SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fPIC -O2 -Wall -Wextra -Werror"
$CC -c $W/webview_bionic_shim.c -o $O/s.o
$CC -c $W/opensles_android_adapter.c -o $O/sl.o
# AOSP's AAudio headers as the app sees them; OH's libohaudio is opened at the first stream.
$CC -isystem $W/../../third_party/aaudio -c $W/aaudio_ohaudio.c -o $O/aa.o
$CC -c $W/android_relocs.c -o $O/ar.o
# libandroid's AConfiguration, sensor and input calls, for callers in every namespace (JNI for the first).
$CC -I$W/../../third_party/jni -c $W/android_ndk_config.c -o $O/nc.o
# Bionic's fts(3), which musl lacks (OpenBSD's code: its fts_set leaves a parameter unused).
$CC -Wno-unused-parameter -c $W/bionic_fts.c -o $O/ft.o
$SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fPIC -c $W/webview_setjmp_arm64.S -o $O/j.o
# -nostdlib with explicit crt objects: the shim must not pull a second libc++ or its init order.
# libOpenSLES stays NEEDED (--no-as-needed): an app's dlopen of libOpenSLES.so is answered with this
# library, whose lookups then fall through to OH's.
$SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fuse-ld=lld -nostdlib -shared -Wl,-z,defs \
  -Wl,--build-id=sha1 -Wl,-soname,libwebview_bionic_shim.so -Wl,--version-script=$W/webview_bionic_shim.map \
  $CRT/crti.o $RT/clang_rt.crtbegin.o $O/s.o $O/sl.o $O/aa.o $O/ar.o $O/nc.o $O/ft.o $O/j.o -Wl,--no-as-needed -lOpenSLES -Wl,--as-needed \
  -ldl -lc $RT/libclang_rt.builtins.a $RT/clang_rt.crtend.o $CRT/crtn.o -o $O/libwebview_bionic_shim.so
# libaaudio.so: a name for DT_NEEDED with no code of its own (libaaudio_stub.c).
$CC -c $W/libaaudio_stub.c -o $O/as.o
$SDK/llvm/bin/clang --target=aarch64-linux-ohos -fuse-ld=lld -nostdlib -shared -Wl,--build-id=sha1 \
  -Wl,-soname,libaaudio.so $O/as.o -o $O/libaaudio.so
rm -f $O/*.o
python3 - "$O" "$DESC" <<'PY'
import hashlib, json, pathlib, sys
o = pathlib.Path(sys.argv[1])
names = ["libwebview_bionic_shim.so", "libaaudio.so"]
data = {name: (o / name).read_bytes() for name in names}
(o / "artifacts.json").write_text(json.dumps({"schema": "a2hlab-source-bionic-shim/v1", "source": sys.argv[2],
 "compiler": "OH SDK llvm clang", "errors": [],
 "artifacts": {name: {"sha256": hashlib.sha256(b).hexdigest(), "bytes": len(b)} for name, b in data.items()}},
 indent=1) + "\n")
print("built", o.name, len(data["libwebview_bionic_shim.so"]), "bytes")
PY
