#!/bin/sh
set -eu

OH=/home/dspfac/openharmony
SDK=/home/dspfac/ohos-sdk-6.1/linux/native
CC="$OH/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang"
OUT=/home/dspfac/bridge-build-arm64/player-gl-shim

"$CC" --target=aarch64-linux-ohos --sysroot="$SDK/sysroot" \
  -O2 -fPIC -shared \
  -Wl,-soname,libGLESv2.so \
  -Wl,--no-as-needed -L"$OUT" -l:libGLESv3.so -Wl,--as-needed \
  -ldl \
  -o "$OUT/libGLESv2.trace.so" \
  /home/dspfac/bridge-build/src/framework/player-gl-shim/player_gl_trace.c

"$CC" --target=aarch64-linux-ohos --sysroot="$SDK/sysroot" \
  -O2 -fPIC -shared \
  -Wl,-soname,libEGL.so \
  -ldl -lnative_window \
  -o "$OUT/libEGL.trace.so" \
  /home/dspfac/bridge-build/src/framework/player-gl-shim/player_egl_trace.c

readelf -d "$OUT/libGLESv2.trace.so"
readelf -d "$OUT/libEGL.trace.so"
