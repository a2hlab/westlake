#!/usr/bin/env bash
set -euo pipefail
root=/home/dspfac/bridge-build-arm64/ime-ability-window-20260904
oh=/home/dspfac/openharmony
src=/home/dspfac/ohos-6.1-src
llvm=$oh/prebuilts/clang/ohos/linux-x86_64/llvm
sdk=/home/dspfac/ohos-sdk-6.1/linux/native
lib=/home/dspfac/bridge-build-arm64/out/board_libs
"$llvm/bin/clang++" --target=aarch64-linux-ohos --sysroot="$sdk/sysroot" \
  -nostdinc++ -isystem "$llvm/include/libcxx-ohos/include/c++/v1" \
  -I"$oh/base/notification/eventhandler/interfaces/inner_api" \
  -I"$oh/base/notification/eventhandler/frameworks/eventhandler/include" \
  -I"$src/foundation/multimodalinput/input/interfaces/native/innerkits/event/include" \
  -I"$src/foundation/multimodalinput/input/interfaces/native/innerkits/proxy/include" \
  -I"$src/foundation/multimodalinput/input/interfaces/native/innerkits/common/include" \
  -I"$src/foundation/multimodalinput/input/util/common/include" \
  -I"$src/commonlibrary/c_utils/base/include" \
  -I"$src/foundation/communication/ipc/interfaces/innerkits/ipc_core/include" \
  -DWESTLAKE_PINNED_INPUT_ENTRY=1 \
  -shared -fPIC -O2 -std=c++17 -fno-rtti -Wall -Wextra -Werror -Wno-unused-parameter \
  -Wl,-z,defs -Wl,-soname,liboh_owned_input.so \
  /home/dspfac/bridge-build/src/framework/window/jni/oh_owned_input_bridge.cpp \
  -L"$lib" -lmmi-client.z -leventhandler.z -lc++ -ldl \
  -o "$root/liboh_owned_input.so"
