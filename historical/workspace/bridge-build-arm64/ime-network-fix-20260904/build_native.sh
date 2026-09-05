#!/usr/bin/env bash
set -euo pipefail
cd /home/dspfac/bridge-build-arm64/ime-network-fix-20260904
sdk=/home/dspfac/ohos-sdk-6.1/linux/native
"$sdk/llvm/bin/clang++" --target=aarch64-linux-ohos --sysroot="$sdk/sysroot" \
  -shared -fPIC -O2 -std=c++17 \
  -I/home/dspfac/bridge-build/aosp/libnativehelper/include_jni \
  -Wl,-z,defs -Wl,-soname,liboh_ime_helper_capi.so \
  /home/dspfac/bridge-build/src/framework/window/jni/oh_ime_helper_capi.cpp \
  -lohinputmethod -ldl -o liboh_ime_helper_capi.so
file liboh_ime_helper_capi.so
"$sdk/llvm/bin/clang++" --target=aarch64-linux-ohos --sysroot="$sdk/sysroot" \
  -shared -fPIC -O2 -std=c++17 -Wall -Wextra -Werror \
  -I/home/dspfac/bridge-build/aosp/libnativehelper/include_jni \
  -Wl,-z,defs -Wl,-soname,liboh_network_jni.so \
  /home/dspfac/bridge-build/src/framework/android-runtime/src/oh_network_jni.cpp \
  -o liboh_network_jni.so
file liboh_network_jni.so
