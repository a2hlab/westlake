#!/usr/bin/env bash
set -euo pipefail
root=/home/dspfac/bridge-build-arm64/ime-ability-window-20260904
sdk=/home/dspfac/ohos-sdk-6.1/linux/native
"$sdk/llvm/bin/clang++" --target=aarch64-linux-ohos --sysroot="$sdk/sysroot" \
  -shared -fPIC -O2 -Wall -Wextra -Werror -Wl,-z,defs \
  -Wl,-soname,libowned_window_z.so "$root/owned_window_z.cpp" -ldl \
  -o "$root/libowned_window_z.so"
