#!/bin/bash
# build_audioeffect_jni.sh <android-source> <oh-sdk-native> <out-dir>
# Builds libaudioeffect_jni.so from framework/audioeffect-shim: the audio-effect natives of a
# device with no effect engine, so android.media.audiofx classes load and report "not supported".
set -e
SRC=$1; SDK=$2; OUT=$3
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p $OUT
$SDK/llvm/bin/clang --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fPIC -O2 -Wall -Werror \
  -I$SRC/libnativehelper/include_jni -shared -fuse-ld=lld -Wl,-soname,libaudioeffect_jni.so -Wl,-z,defs \
  $HERE/framework/audioeffect-shim/audioeffect_jni.c -lc -o $OUT/libaudioeffect_jni.so
echo "built $OUT/libaudioeffect_jni.so"
