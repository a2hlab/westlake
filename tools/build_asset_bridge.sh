#!/bin/bash
# build_asset_bridge.sh <workspace> <runtime-dir> <out-dir>
# Builds libwestlake_asset_bridge.so from framework/webview-shim/android_asset_manager_bridge.cpp:
# the wl_AAsset* entry points the WebView shim's libandroid.so resolves for code in the isolated
# Android namespace (React Native loads its bundle this way; Godot, SDL and Emacs read assets).
# It links against the runtime's own liboh_android_runtime.so and libandroidfw.so, so when the
# namespace inherits it from the parent it reaches the one initialized AssetManager2, never a copy.
# <workspace> is the build workspace (toolchains/, android-source/); <runtime-dir> the native runtime.
set -e
W=$1; RT=$2; OUT=$3
HERE=$(cd "$(dirname "$0")/.." && pwd)
SDK=$W/toolchains/ohos-sdk/native; PLATFORM=$W/toolchains/clang-15; A=$W/android-source
mkdir -p $OUT
# The include set the native builder compiles libandroidfw.so with: native/libraries.json's global
# list plus libandroidfw's own, rooted at the workspace.
INCLUDES=$(python3 -c "
import json,sys
d=json.load(open('$HERE/native/libraries.json'))
paths=d['includes']+d['libraries']['libandroidfw.so'].get('includes',[])
print(' '.join('-I$W/'+p for p in dict.fromkeys(paths)))")
$SDK/llvm/bin/clang++ --target=aarch64-linux-ohos --sysroot=$SDK/sysroot -fPIC -O2 -std=gnu++20 -nostdinc++ \
  -isystem $PLATFORM/include/libcxx-ohos/include/c++/v1 \
  -ftrivial-auto-var-init=zero -enable-trivial-auto-var-init-zero-knowing-it-will-be-removed-from-clang \
  -D_GNU_SOURCE -DNDEBUG -Wall -Werror -include $HERE/native/include/ohos_port.h \
  -I$HERE/framework/android-runtime/src $INCLUDES \
  -shared -fuse-ld=lld -nostdlib++ -Wl,-soname,libwestlake_asset_bridge.so -Wl,-z,defs \
  $HERE/framework/webview-shim/android_asset_manager_bridge.cpp \
  -L$RT -loh_android_runtime -landroidfw -lutils -llog $PLATFORM/lib/aarch64-linux-ohos/libc++.so \
  -o $OUT/libwestlake_asset_bridge.so
echo "built $OUT/libwestlake_asset_bridge.so"
