#!/usr/bin/env bash
set -euo pipefail
root=/home/dspfac/bridge-build-arm64/chinese-ime-20260904
sdk=/home/dspfac/ohos-sdk-6.1/linux/native
src=$root/upstream/entry/src/main/cpp
mkdir -p "$root/out"
sources=("$src"/share/*.cpp)
"$sdk/llvm/bin/clang++" --target=aarch64-linux-ohos --sysroot="$sdk/sysroot" \
  -shared -fPIC -O2 -std=c++17 -I"$src/include" \
  "$root/pinyin_napi.cpp" "${sources[@]}" -lace_napi.z -Wl,-z,defs \
  -Wl,-soname,libwestlake_pinyin.so -o "$root/out/libwestlake_pinyin.so"
# Same engine and dictionary, independently testable on the build host.
c++ -shared -fPIC -O2 -std=c++17 -I"$src/include" "${sources[@]}" -o "$root/out/libpinyin_host.so"
