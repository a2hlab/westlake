#!/bin/bash
# build_glesv1_cm.sh <oh-sdk-native> <out-dir>
# Builds libGLESv1_CM.so (framework/gles1-shim): no code of its own, DT_NEEDED on the board's
# driver libGLES_mali.z.so, which exports the GLES 1.x entry points. The driver is named through a
# link-time stub with its soname, so the build needs nothing from the board.
set -e
SDK=$1; OUT=$2
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p $OUT/stub
CC="$SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fPIC -O2 -Wall -Werror -shared -fuse-ld=lld"
echo "" | $CC -x c - -Wl,-soname,libGLES_mali.z.so -o $OUT/stub/libGLES_mali.z.so
$CC $HERE/framework/gles1-shim/glesv1_cm.c -Wl,-soname,libGLESv1_CM.so -Wl,--no-as-needed \
  $OUT/stub/libGLES_mali.z.so -o $OUT/libGLESv1_CM.so
rm -rf $OUT/stub
echo "built $OUT/libGLESv1_CM.so"
