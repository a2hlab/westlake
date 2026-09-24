#!/bin/bash
# build_missing_natives.sh <android-source> <oh-sdk-native> <out-dir>
# Builds libwl_missing_natives.so from framework/javacore-shim/missing_natives.c: platform natives
# the staged runtime declares but never registers, bound onto the boot classes from JNI_OnLoad.
set -e
SRC=$1; SDK=$2; OUT=$3
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p $OUT
$SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fPIC -O2 -Wall -Wextra -Werror \
  -I$SRC/libnativehelper/include_jni -shared -fuse-ld=lld -Wl,-soname,libwl_missing_natives.so -Wl,-z,defs \
  $HERE/framework/javacore-shim/missing_natives.c -lm -lc -o $OUT/libwl_missing_natives.so
echo "built $OUT/libwl_missing_natives.so"
