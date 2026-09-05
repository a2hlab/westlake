#!/system/bin/sh
set -eu

ASX=/data/local/tmp/asx
SOCKET_NAME=${ASX_SOCKET_NAME:-AppSpawnXJitAotCandidate}
FW=${ASX_FW_DIR:-$ASX/fw}
FW_IMAGE=${ASX_FW_IMAGE:-$ASX/fwbootimg-v118-clean3-content-log-space6-20260902}
RUNTIME=${ASX_RUNTIME:-$ASX/out-jit-aot-candidate-20260902}
: "${ASX_USE_BOOT_IMAGE:=1}"

setenforce 0 2>/dev/null || true
mkdir -p /data/service/el1/public/appspawnx 2>/dev/null || true
chmod 0777 /data/service/el1/public/appspawnx 2>/dev/null || true
mkdir -p $ASX/android-root/etc 2>/dev/null || true
cp $ASX/fonts.xml $ASX/android-root/etc/fonts.xml 2>/dev/null || true
if [ -f $ASX/fonts.xml ] && ! cmp -s $ASX/fonts.xml /system/android/etc/fonts.xml; then
  mount --bind $ASX/fonts.xml /system/android/etc/fonts.xml 2>/dev/null || true
fi
if [ ! -e $ASX/android-root/fonts ]; then
  ln -s /system/fonts $ASX/android-root/fonts 2>/dev/null || true
fi
ulimit -s 8192 2>/dev/null || true
# Match the production launcher contract: Android children normally inherit
# `/`, while test APKs with offline coverage agents can explicitly select a
# writable working directory for relative output such as jacoco.exec.
ASX_WORKDIR=${ASX_WORKDIR:-/}
cd "$ASX_WORKDIR" || exit 1
export APPSPAWNX_DPROPS=user.dir="$ASX_WORKDIR"
: "${APPSPAWNX_FORCE_JIT:=1}"
: "${APPSPAWNX_JIT_THRESHOLD:=10000}"
export APPSPAWNX_FORCE_JIT APPSPAWNX_JIT_THRESHOLD
if [ "$APPSPAWNX_FORCE_JIT" = "1" ]; then
  : "${WESTLAKE_ENABLE_JIT:=1}"
  : "${WESTLAKE_CHILD_JIT:=1}"
  export WESTLAKE_ENABLE_JIT WESTLAKE_CHILD_JIT
else
  # Preserve a real interpreter control. Merely omitting -Xusejit is not
  # sufficient on this standalone zygote because the child-only creation path
  # is independently gated by WESTLAKE_CHILD_JIT.
  unset WESTLAKE_ENABLE_JIT WESTLAKE_CHILD_JIT
fi
: "${WESTLAKE_CHILD_DEOPT_BOOT:=1}"
: "${WESTLAKE_CHILD_SWITCH_BOOT:=1}"
if [ "$WESTLAKE_CHILD_DEOPT_BOOT" != "0" ]; then
  export WESTLAKE_CHILD_DEOPT_BOOT
else
  unset WESTLAKE_CHILD_DEOPT_BOOT
fi
if [ "$WESTLAKE_CHILD_SWITCH_BOOT" != "0" ]; then
  export WESTLAKE_CHILD_SWITCH_BOOT
else
  unset WESTLAKE_CHILD_SWITCH_BOOT
fi
: "${WESTLAKE_JIT_APP_ONLY:=1}"
: "${WESTLAKE_JIT_PEERLESS:=1}"
: "${WESTLAKE_JIT_BOOT_PARSER:=1}"
: "${WESTLAKE_APP_CPU_RESERVE:=0}"
export WESTLAKE_JIT_APP_ONLY WESTLAKE_JIT_PEERLESS WESTLAKE_JIT_BOOT_PARSER
export WESTLAKE_APP_CPU_RESERVE
export ASX_DIAG_THROWABLE=1
# Keep the historical fast-dev default for existing runners, but allow a
# candidate to enable ART verification. Android 15 nterp requires verified
# methods and silently falls back to the C++ switch interpreter when
# -Xverify:none is forced.
: "${APPSPAWNX_FAST_DEV:=1}"
export APPSPAWNX_FAST_DEV
if [ "${ASX_ENABLE_DAEMONS:-0}" = "1" ]; then
  unset ASX_NO_DAEMON_INIT ASX_NO_START_DAEMONS ASX_NATIVE_PREFORK
else
  export ASX_NO_DAEMON_INIT=1 ASX_NO_START_DAEMONS=1
  export ASX_NATIVE_PREFORK=1
fi
export ANDROID_ROOT=/system
export ICU_DATA=$ASX
export BOOTCLASSPATH=$FW/core-oj.jar:$FW/core-libart.jar:$FW/core-icu4j.jar:$FW/okhttp.jar:$FW/bouncycastle.jar:$FW/apache-xml.jar:$FW/adapter-mainline-stubs.jar:$FW/framework.jar:$FW/adapter-runtime-bcp.jar:$FW/oh-adapter-framework.jar
export DEX2OATBOOTCLASSPATH=$BOOTCLASSPATH
if [ "$ASX_USE_BOOT_IMAGE" != "0" ]; then
  export WESTLAKE_TRUSTED_OAT_DIR=$FW_IMAGE
  export ANDROID_BOOT_IMAGE=$FW_IMAGE/boot.art
else
  unset WESTLAKE_TRUSTED_OAT_DIR ANDROID_BOOT_IMAGE
fi

export ASX_KEEP_THEME=1
export ASX_DIRECT_LAUNCH=1
: "${ASX_LAUNCH_PKG:=com.ss.android.article.news}"
: "${ASX_APK_PATH:=$ASX/toutiao.apk}"
: "${ASX_LAUNCH_ACTIVITY:=com.ss.android.article.news.activity.MainActivity}"
: "${ASX_WEBVIEW_APK:=$ASX/webview-t.apk}"
: "${ASX_WEBVIEW_LIB_DIR:=$ASX/webview-t-lib}"
: "${ASX_WEBVIEW_DATA_DIR:=$ASX/webview-t-data}"
test -f "$ASX_APK_PATH"
export ASX_LAUNCH_PKG ASX_APK_PATH ASX_LAUNCH_ACTIVITY
export ASX_WEBVIEW_APK ASX_WEBVIEW_LIB_DIR ASX_WEBVIEW_DATA_DIR

export WESTLAKE_ANDROID_NATIVE_TARGETS=libkeva.so:libvcbasekit.so:libttheif_dec.so:libbdheif.so:libsscronet.so:liblynxsecurity.so:liblynx.so:libnapi.so:libquick.so
export WESTLAKE_ANDROID_NATIVE_ANCHOR_TARGET=libkeva.so
export WESTLAKE_ANDROID_NATIVE_ANCHOR=$ASX/nsprobe/libwestlake_native_bootstrap_anchor.so
export WESTLAKE_ANDROID_NATIVE_NEXT_ANCHOR_TARGET=libttmplayer.so
export WESTLAKE_ANDROID_NATIVE_NEXT_ANCHOR=$ASX/nsprobe/libwestlake_player_anchor.so
export WESTLAKE_ANDROID_NATIVE_SEARCH_PATH=$ASX/lib/arm64:$ASX/nsprobe:$ASX/webview-t-lib:$ASX:/system/lib64/ndk:/system/lib64/platformsdk:/system/lib64/chipset-sdk-sp:/system/lib64
export WESTLAKE_ANDROID_NATIVE_INHERIT=libc.so:libdl.so:libm.so:libz.so:libbionic_abi_shim.so
export WESTLAKE_ANDROID_NATIVE_NET_TARGETS=libmffmpeg.so:libttmplayer.so:libsscronet.so:libwebviewchromium.so
export LD_LIBRARY_PATH=$RUNTIME:$ASX:/system/lib64:/system/lib64/platformsdk:/system/lib64/chipset-sdk:/system/lib64/chipset-sdk-sp:/system/lib64/ndk:/system/lib64/module/data:/system/lib64/module:$ASX/lib/arm64:$ASX/webview-t-lib
export LD_PRELOAD=$ASX/nsprobe/libbionic_stdio_shim.so:$ASX/libandroid_native_network_compat.so:$ASX/liblog_shim.so:$ASX/libbionic_abi_shim.so:$ASX/libwl636.so:$ASX/webview-t-lib/libwebview_bionic_shim.so

if [ "${WL_PTRACE_TRACE:-0}" = "1" ]; then
  TRACE_BIN=${ASX_FATAL_SIGNAL_TRACER:-$ASX/fatal_signal_tracer}
  exec $TRACE_BIN $RUNTIME/appspawn-x \
      --socket-name $SOCKET_NAME \
      --sandbox-config $ASX/appspawn_x_sandbox.json
fi

exec $RUNTIME/appspawn-x \
    --socket-name $SOCKET_NAME \
    --sandbox-config $ASX/appspawn_x_sandbox.json
