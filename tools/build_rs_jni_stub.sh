#!/bin/bash
# build_rs_jni_stub.sh <android-source> <oh-sdk-native> <out-dir>
# Builds librs_jni.so from framework/javacore-shim/rs_jni_stub.c: RenderScript's JNI library as "not available"
# (RenderScript.create() throws RSDriverException instead of the class failing to initialize). The runtime
# stages it where android.renderscript.RenderScript's System.loadLibrary("rs_jni") looks.
set -e
SRC=$1; SDK=$2; OUT=$3
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p $OUT
$SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fPIC -O2 -Wall -Wextra -Werror \
  -I$SRC/libnativehelper/include_jni -shared -fuse-ld=lld -Wl,-soname,librs_jni.so -Wl,-z,defs \
  $HERE/framework/javacore-shim/rs_jni_stub.c -lc -o $OUT/librs_jni.so
echo "built $OUT/librs_jni.so"
