#!/bin/bash
# build_bionic_shim.sh <oh-sdk-native> <out-dir> [description]
# Builds libwebview_bionic_shim.so: the bionic compatibility layer (webview_bionic_shim.c), the
# Android OpenSL ES adapter, AAudio over OHAudio, the DT_ANDROID_RELR loader, libandroid's
# AConfiguration, sensor and input calls and Bionic's fts; the libaaudio.so, libnativewindow.so and
# libOpenMAXAL.so staged beside it for DT_NEEDED; the Android namespace's libandroid.so; and
# artifacts.json for a launch's --bionic-shim input.
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
# libc_table.S from libc_table.txt: each listed libc or libdl function as name@WL_LIBC_TABLE, a
# trampoline through a slot that westlake_libc_table_resolve fills with musl's function on the first
# call (the lazy path keeps the argument registers, x8 and q0-q7). Only code that looks the name up
# in this library's symbol table itself reaches one: no ordinary import binds to a non-default version.
grep -v -e '^#' -e '^$' $W/libc_table.txt | awk '
BEGIN {
  print "\t.text\n\t.p2align 2\n\t.type\twestlake_libc_table_lazy, %function\nwestlake_libc_table_lazy:"
  print "\tstp\tx29, x30, [sp, #-224]!\n\tmov\tx29, sp\n\tstp\tx0, x1, [sp, #16]\n\tstp\tx2, x3, [sp, #32]"
  print "\tstp\tx4, x5, [sp, #48]\n\tstp\tx6, x7, [sp, #64]\n\tstp\tx8, x17, [sp, #80]\n\tstp\tq0, q1, [sp, #96]"
  print "\tstp\tq2, q3, [sp, #128]\n\tstp\tq4, q5, [sp, #160]\n\tstp\tq6, q7, [sp, #192]"
  print "\tmov\tx0, x17\n\tmov\tx1, x30\n\tbl\twestlake_libc_table_resolve\n\tmov\tx16, x0"
  print "\tldp\tq6, q7, [sp, #192]\n\tldp\tq4, q5, [sp, #160]\n\tldp\tq2, q3, [sp, #128]\n\tldp\tq0, q1, [sp, #96]"
  print "\tldp\tx8, x17, [sp, #80]\n\tldp\tx6, x7, [sp, #64]\n\tldp\tx4, x5, [sp, #48]\n\tldp\tx2, x3, [sp, #32]"
  print "\tldp\tx0, x1, [sp, #16]\n\tldp\tx29, x30, [sp], #224\n\tbr\tx16"
  print "\t.size\twestlake_libc_table_lazy, .-westlake_libc_table_lazy"
}
{
  name[n] = $1
  printf "\t.p2align 2\n\t.globl\twestlake_libc_table_%s\n\t.type\twestlake_libc_table_%s, %%function\n", $1, $1
  printf "westlake_libc_table_%s:\n\tadrp\tx17, westlake_libc_table_slots+%d\n", $1, 8 * n
  printf "\tadd\tx17, x17, :lo12:westlake_libc_table_slots+%d\n\tldr\tx16, [x17]\n", 8 * n
  printf "\tcbz\tx16, westlake_libc_table_lazy\n\tbr\tx16\n"
  printf "\t.size\twestlake_libc_table_%s, .-westlake_libc_table_%s\n", $1, $1
  printf "\t.symver\twestlake_libc_table_%s, %s@WL_LIBC_TABLE\n", $1, $1
  n++
}
END {
  print "\t.bss\n\t.p2align 3\n\t.globl\twestlake_libc_table_slots\n\t.hidden\twestlake_libc_table_slots"
  printf "westlake_libc_table_slots:\n\t.zero\t%d\n", 8 * n
  print "\t.section\t.rodata.str1.1,\"aMS\",@progbits,1"
  for (i = 0; i < n; i++) printf ".Lwestlake_libc_table_name%d:\n\t.asciz\t\"%s\"\n", i, name[i]
  print "\t.section\t.data.rel.ro,\"aw\"\n\t.p2align 3\n\t.globl\twestlake_libc_table_names\n\t.hidden\twestlake_libc_table_names"
  print "westlake_libc_table_names:"
  for (i = 0; i < n; i++) printf "\t.xword\t.Lwestlake_libc_table_name%d\n", i
  print "\t.globl\twestlake_libc_table_size\n\t.hidden\twestlake_libc_table_size"
  printf "westlake_libc_table_size:\n\t.xword\t%d\n", n
}' > $O/libc_table.S
$SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fPIC -c $O/libc_table.S -o $O/lt.o
# -nostdlib with explicit crt objects: the shim must not pull a second libc++ or its init order.
# libOpenSLES stays NEEDED (--no-as-needed): an app's dlopen of libOpenSLES.so is answered with this
# library, whose lookups then fall through to OH's.
$SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fuse-ld=lld -nostdlib -shared -Wl,-z,defs \
  -Wl,--build-id=sha1 -Wl,-soname,libwebview_bionic_shim.so -Wl,--version-script=$W/webview_bionic_shim.map \
  $CRT/crti.o $RT/clang_rt.crtbegin.o $O/s.o $O/sl.o $O/aa.o $O/ar.o $O/nc.o $O/ft.o $O/j.o $O/lt.o -Wl,--no-as-needed -lOpenSLES -Wl,--as-needed \
  -ldl -lc $RT/libclang_rt.builtins.a $RT/clang_rt.crtend.o $CRT/crtn.o -o $O/libwebview_bionic_shim.so
# libaaudio.so: a name for DT_NEEDED with no code of its own (libaaudio_stub.c).
$CC -c $W/libaaudio_stub.c -o $O/as.o
$SDK/llvm/bin/clang --target=aarch64-linux-ohos -fuse-ld=lld -nostdlib -shared -Wl,--build-id=sha1 \
  -Wl,-soname,libaaudio.so $O/as.o -o $O/libaaudio.so
# libOpenMAXAL.so: the NDK's OpenMAX AL with no engine (libopenmaxal.c), for the libraries that name it
# in DT_NEEDED; its version script is AOSP's export list, version LIBOPENMAXAL.
$CC -c $W/libopenmaxal.c -o $O/xa.o
$SDK/llvm/bin/clang --target=aarch64-linux-ohos -fuse-ld=lld -nostdlib -shared -Wl,--build-id=sha1 -Wl,-z,defs \
  -Wl,-soname,libOpenMAXAL.so -Wl,--version-script=$W/libopenmaxal.map $O/xa.o -o $O/libOpenMAXAL.so
# libnativewindow.so: the same, and its version script declares the LIBNATIVEWINDOW its importers
# name in DT_VERNEED (the shim's own exports carry that version, where the definitions are).
$CC -c $W/libnativewindow_stub.c -o $O/nw.o
$SDK/llvm/bin/clang --target=aarch64-linux-ohos -fuse-ld=lld -nostdlib -shared -Wl,--build-id=sha1 \
  -Wl,-soname,libnativewindow.so -Wl,--version-script=$W/libnativewindow_stub.map $O/nw.o \
  -o $O/libnativewindow.so
# libandroid.so: the Android namespace's libandroid (libandroid_webview_shim.c), replacing the
# WebView input's captured copy. It links OH's libsurface.z.so, which the SDK does not ship, so a
# link-time stub names its imports; the device's library answers them at load.
for f in GetBufferHandleFromNative NativeWindowCancelBuffer NativeWindowFlushBuffer NativeWindowHandleOpt \
    NativeWindowRequestBuffer OH_NativeBufferFromNativeWindowBuffer OH_NativeBuffer_Map OH_NativeBuffer_MapWaitFence; do
  echo "void $f(void) {}"
done > $O/surface_stub.c
mkdir -p $O/stub
$SDK/llvm/bin/clang --target=aarch64-linux-ohos -fuse-ld=lld -nostdlib -shared -Wl,-soname,libsurface.z.so \
  $O/surface_stub.c -o $O/stub/libsurface.z.so
$CC -I$W/../../third_party/jni -c $W/libandroid_webview_shim.c -o $O/la.o
$SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fuse-ld=lld -shared -Wl,--build-id=sha1 \
  -Wl,-soname,libandroid.so $O/la.o -L$O/stub -lsurface.z -ldl -o $O/libandroid.so
rm -rf $O/stub $O/surface_stub.c
rm -f $O/*.o
python3 - "$O" "$DESC" <<'PY'
import hashlib, json, pathlib, sys
o = pathlib.Path(sys.argv[1])
names = ["libwebview_bionic_shim.so", "libaaudio.so", "libnativewindow.so", "libandroid.so", "libOpenMAXAL.so"]
data = {name: (o / name).read_bytes() for name in names}
(o / "artifacts.json").write_text(json.dumps({"schema": "a2hlab-source-bionic-shim/v1", "source": sys.argv[2],
 "compiler": "OH SDK llvm clang", "errors": [],
 "artifacts": {name: {"sha256": hashlib.sha256(b).hexdigest(), "bytes": len(b)} for name, b in data.items()}},
 indent=1) + "\n")
print("built", o.name, len(data["libwebview_bionic_shim.so"]), "bytes")
PY
