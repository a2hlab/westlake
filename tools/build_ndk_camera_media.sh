#!/bin/bash
# build_ndk_camera_media.sh <oh-sdk-native> <out-dir>
# Builds libcamera2ndk.so (framework/camera-ndk-shim) and libmediandk.so (framework/media-ndk-shim):
# the NDK camera and media APIs of a runtime with no camera and no NDK codec behind them, so a
# library that names them in DT_NEEDED loads and is told what is not there.
set -e
SDK=$1; OUT=$2
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p $OUT
for pair in camera-ndk-shim/camera2ndk.c:libcamera2ndk.so media-ndk-shim/mediandk.c:libmediandk.so; do
  src=${pair%%:*}; lib=${pair##*:}
  $SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fPIC -O2 -Wall -Werror \
    -fvisibility=hidden -shared -fuse-ld=lld -Wl,-soname,$lib -Wl,-z,defs \
    $HERE/framework/$src -lc -o $OUT/$lib
  echo "built $OUT/$lib"
done
