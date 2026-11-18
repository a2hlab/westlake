#!/usr/bin/env bash
# Build an app-process HEVC Main decoder against an OpenHarmony third_party/ffmpeg checkout.
set -eu
ffmpeg=$(cd "$1" && pwd)
sdk=$(cd "$2" && pwd)
mkdir -p "$3"
out=$(cd "$3" && pwd)
src=$(cd "$(dirname "$0")/../framework/javacore-shim/hevc" && pwd)
mkdir -p "$out/hevc-ffmpeg"
cd "$out/hevc-ffmpeg"
bash "$ffmpeg/configure" --arch=aarch64 --target-os=linux --enable-cross-compile \
    --cc="$sdk/llvm/bin/clang --target=aarch64-linux-ohos" --sysroot="$sdk/sysroot" \
    --ar="$sdk/llvm/bin/llvm-ar" --ranlib="$sdk/llvm/bin/llvm-ranlib" --nm="$sdk/llvm/bin/llvm-nm" \
    --strip="$sdk/llvm/bin/llvm-strip" --disable-everything --disable-programs --disable-doc --disable-debug \
    --disable-network --disable-autodetect --disable-shared --enable-static --enable-pic --enable-decoder=hevc
make -j"${BUILD_JOBS:-4}" libavcodec/libavcodec.a libavutil/libavutil.a
"$sdk/llvm/bin/clang++" --target=aarch64-linux-ohos --sysroot="$sdk/sysroot" -std=c++17 -shared \
    -fPIC -O2 -fvisibility=hidden -Wall -Wextra -Werror -I. -I"$ffmpeg" \
    "$src/decoder.cpp" "$src/frames.cpp" libavcodec/libavcodec.a libavutil/libavutil.a \
    -Wl,--exclude-libs,ALL -Wl,-soname,libwl_hevc.so -Wl,-z,defs -static-libstdc++ \
    -lnative_window -lnative_buffer -lnative_media_core -lnative_media_codecbase -lm -o "$out/libwl_hevc.so"
