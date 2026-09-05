#!/system/bin/sh
# arm64 zygote launcher — MANUAL only (deliberately NOT an init service, to avoid
# boot-loop risk on this board). Run via: hdc shell "sh /data/local/tmp/asx/run_asx.sh &"
setenforce 0 2>/dev/null
mkdir -p /data/service/el1/public/appspawnx 2>/dev/null
chmod 0777 /data/service/el1/public/appspawnx 2>/dev/null
# WESTLAKE §732: Chromium obtains the Android font root through the bionic
# boundary shim. Keep its complete adapter font configuration on writable
# staging storage while reusing the board's actual OH font files.
mkdir -p /data/local/tmp/asx/android-root/etc 2>/dev/null
cp /data/local/tmp/asx/fonts.xml \
  /data/local/tmp/asx/android-root/etc/fonts.xml 2>/dev/null
# The framework's SystemFonts hard-codes /system/etc/fonts.xml. The board's
# read-only system image carries the older Latin-only adapter configuration,
# so bind the complete boundary config before ART initializes Typeface. This
# is a generic runtime mount and can be removed once the system image includes
# src/framework/android-runtime/etc/fonts.xml.
if [ -f /data/local/tmp/asx/fonts.xml ] && \
   ! cmp -s /data/local/tmp/asx/fonts.xml /system/android/etc/fonts.xml; then
  mount --bind /data/local/tmp/asx/fonts.xml \
    /system/android/etc/fonts.xml 2>/dev/null
fi
if [ ! -e /data/local/tmp/asx/android-root/fonts ]; then
  ln -s /system/fonts /data/local/tmp/asx/android-root/fonts 2>/dev/null
fi
# 2026-07-21: musl defaults threads to a 128KB stack; ART then reports
# "StackOverflowError: stack size 124KB" in the forked child (Typeface.<clinit>
# font mmap path).  Raise the process stack rlimit before exec.
ulimit -s 8192 2>/dev/null
# Android zygote children inherit the filesystem root as their working directory and
# java.io.tmpdir/user.dir must not point at the launcher staging directory.  Keeping the
# bridge process in /data/local/tmp/asx made relative sentinel paths such as "null" resolve
# against persistent staging data.  Once an unrelated cache created asx/null, release apps
# could mistake it for a valid downloaded resource directory (Toutiao then generated
# file://null/... WebView asset URLs).  All launcher inputs below are absolute, so restore
# Android's process-directory contract before appspawn-x forks application children. A test APK
# with an offline coverage agent may opt into a writable directory (for example, Material Catalog
# writes a relative jacoco.exec); the production Toutiao default remains Android's root directory.
ASX_WORKDIR=${ASX_WORKDIR:-/}
cd "$ASX_WORKDIR" || exit 1
export APPSPAWNX_DPROPS=user.dir="$ASX_WORKDIR"
# Android installs each package in its own code directory (normally with a
# base.apk and optional split APK siblings).  ASX development artifacts share
# one very large staging directory; exposing that directory as sourceDir makes
# PackageInfoBuilder's legitimate split resolver enumerate every unrelated
# artifact on each package query.  Present the unchanged APK and native-library
# tree through a package-owned layout so direct launch observes Android's
# filesystem contract without copying either payload.
TOUTIAO_APK_SOURCE=${ASX_APK_PATH:-/data/local/tmp/asx/toutiao.apk}
TOUTIAO_INSTALL_DIR=/data/local/tmp/asx/apps/com.ss.android.article.news
mkdir -p "$TOUTIAO_INSTALL_DIR" 2>/dev/null
if [ "$TOUTIAO_APK_SOURCE" != "$TOUTIAO_INSTALL_DIR/base.apk" ]; then
  ln -sfn "$TOUTIAO_APK_SOURCE" "$TOUTIAO_INSTALL_DIR/base.apk" 2>/dev/null
fi
ln -sfn /data/local/tmp/asx/lib "$TOUTIAO_INSTALL_DIR/lib" 2>/dev/null
export ASX_APK_PATH="$TOUTIAO_INSTALL_DIR/base.apk"
# WESTLAKE §796: the generic ARM64 quick-entrypoint restore and mini-debug-info failures are
# fixed and validated across Noice, Material Catalog, and Toutiao. Enable ART's normal JIT by
# default while retaining an immediate per-launch rollback with APPSPAWNX_FORCE_JIT=0.
export APPSPAWNX_FORCE_JIT=${APPSPAWNX_FORCE_JIT:-1}
# The parent is a real zygote, so ART intentionally defers compiler creation
# during Runtime::Start(). Enable the existing child-only creation path after
# post-fork repair and native registration; otherwise -Xusejit:true alone still
# leaves every application method interpreted.
if [ "$APPSPAWNX_FORCE_JIT" = "1" ]; then
  export WESTLAKE_CHILD_JIT=1
else
  unset WESTLAKE_CHILD_JIT
fi
# Keep platform/bridge bytecode on the interpreter path where this standalone
# port's generic Android-compatibility repairs run.  App classes retain normal
# JIT sampling and compilation.  Set to 0 for an explicit all-class JIT probe.
export WESTLAKE_JIT_APP_ONLY=${WESTLAKE_JIT_APP_ONLY:-1}
# PackageInfoBuilder and other generic platform adapters parse large OH bundle
# payloads through the boot-classpath org.json implementation during cold
# start.  Keep the compatibility boundary for all other boot/framework code,
# but allow ART's existing source-defined JSONTokener/JSONObject/JSONArray
# exception to JIT.  Controlled Toutiao runs reached the first laid-out view
# and visible surface in 114 s twice, versus 170-210 s with this disabled.
export WESTLAKE_JIT_BOOT_PARSER=${WESTLAKE_JIT_BOOT_PARSER:-1}
# Let application children use the board's full heterogeneous CPU set. The
# former default reserved CPUs 6-7 even though CPU 7 is the only capacity-1024
# core; use WESTLAKE_APP_CPU_RESERVE=2 for an immediate scheduling rollback.
export WESTLAKE_APP_CPU_RESERVE=${WESTLAKE_APP_CPU_RESERVE:-0}
export WESTLAKE_TDUMP=1
export WESTLAKE_NPE_DIAG=1 WESTLAKE_REINIT=1
# Toutiao has a supported Java-network fallback for devices where its bundled
# Chromium/Cronet engine repeatedly fails to boot.  The OH boundary fixes now
# let the native engine load feed media reliably, so preserve Android's normal
# native path by default and retain Java fallback only as an explicit control.
# Preserve the first pre-change preference file so returning from that control
# cannot leave a persisted chromium_boot_failures override behind.
TOUTIAO_NET_PREF=/data/data/com.ss.android.article.news/shared_prefs/ss_app_config.xml
TOUTIAO_NET_PREF_BACKUP=/data/local/tmp/asx/ss_app_config.xml.pre-westlake-java-net
if [ "${WL_TOUTIAO_FORCE_JAVA_NET:-0}" != "0" ] && [ -f "$TOUTIAO_NET_PREF" ]; then
  if [ ! -f "$TOUTIAO_NET_PREF_BACKUP" ]; then
    if [ -f /data/local/tmp/asx/ss_app_config.xml.pre-cronet-fallback-20260827 ]; then
      cp -p /data/local/tmp/asx/ss_app_config.xml.pre-cronet-fallback-20260827 \
        "$TOUTIAO_NET_PREF_BACKUP" 2>/dev/null
    else
      cp -p "$TOUTIAO_NET_PREF" "$TOUTIAO_NET_PREF_BACKUP" 2>/dev/null
    fi
  fi
  sed -i 's/name="chromium_boot_failures" value="[0-9][0-9]*"/name="chromium_boot_failures" value="6"/' \
    "$TOUTIAO_NET_PREF" 2>/dev/null
elif [ -f "$TOUTIAO_NET_PREF_BACKUP" ]; then
  # A Java-fallback control persists its boot-failure counter in app data.
  # Restore the content-locked pre-control state before a native launch rather
  # than letting a previous diagnostic silently select Java networking.
  cp -p "$TOUTIAO_NET_PREF_BACKUP" "$TOUTIAO_NET_PREF" 2>/dev/null
fi
# Diagnostic only: the fork-safe Throwable hook prints the pending exception
# class without invoking fragile PrintStream internals.
export ASX_DIAG_THROWABLE=1
# The parent deliberately keeps Java daemons stopped between spawns. Calling
# Java ZygoteHooks.preFork() in that state throws from Daemons.stop(); its
# matching postForkCommon() then starts a ReferenceQueueDaemon in the parent,
# so ART aborts the next fork because ThreadList size is 2. Use the stock ART
# native fork hooks for the daemon-free parent. Children still take the full
# Java postForkChild/postForkCommon path and start their normal runtime daemons.
export APPSPAWNX_FAST_DEV=1 ASX_NO_DAEMON_INIT=1 ASX_NO_START_DAEMONS=1 ASX_NATIVE_PREFORK=1 ANDROID_ROOT=/system ICU_DATA=/data/local/tmp/asx
# ART enables its trusted-OAT-only policy for every -Xzygote runtime. The
# adapter's boot image is deliberately staged outside the immutable Android
# /system tree, so opt in only that canonical directory; application OATs and
# every other data path remain subject to ART's normal zygote trust rule.
export WESTLAKE_TRUSTED_OAT_DIR=${WESTLAKE_TRUSTED_OAT_DIR:-/data/local/tmp/asx/fw}
if [ "${WL_WEBVIEW_JPEG_SIMD_DISABLE:-0}" != "0" ]; then
  export JSIMD_FORCENONE=1
fi
# WESTLAKE §761: opt-in Android native ABI namespace. ART routes only the
# launcher-selected basenames; all other framework, WebView, and app native
# loads keep their established default path. Direct targets use their complete
# Android NDK dependency graph. The anchor groups compatibility DSOs with the
# player because OH does not expose prior RTLD_GLOBAL objects to a later
# independent dlopen the way Android's linker does.
# Keep Android NDK C++ DSOs in the app namespace.  In the default OH namespace,
# the already-loaded OH libc++_shared.so (std::__n1) wins the SONAME collision
# over the APK's libc++_shared.so (std::__ndk1).  libbdheif then fails relocation
# before JNI_OnLoad and all Fresco HEIF decode calls appear as missing natives.
# Keep the image-decoder and core Lynx dependency graphs isolated from OH
# libc++. liblynx and its QuickJS dependency are Android NDK libraries
# (std::__ndk1); loading them in the default OH namespace binds the
# already-loaded OH libc++ SONAME first and fails relocation before JNI_OnLoad.
# The security service is part of core template initialization and has the same
# validated NDK C++ boundary. Optional modules such as Markdown remain on their
# existing default path until independently validated. Cronet stays in the
# Android namespace because its JNI_OnLoad registration must be visible to ART.
export WESTLAKE_ANDROID_NATIVE_TARGETS=libkeva.so:libvcbasekit.so:libttheif_dec.so:libbdheif.so:libsscronet.so:liblynxsecurity.so:liblynx.so:libnapi.so:libquick.so
export WESTLAKE_ANDROID_NATIVE_ANCHOR_TARGET=libkeva.so
export WESTLAKE_ANDROID_NATIVE_ANCHOR=/data/local/tmp/asx/nsprobe/libwestlake_native_bootstrap_anchor.so
export WESTLAKE_ANDROID_NATIVE_NEXT_ANCHOR_TARGET=libttmplayer.so
export WESTLAKE_ANDROID_NATIVE_NEXT_ANCHOR=/data/local/tmp/asx/nsprobe/libwestlake_player_anchor.so
# Resolve the APK's own libc++ before the compatibility directory inside this
# isolated namespace. The staged nsprobe libc++ covers the media/image graph
# but lacks codecvt<char16_t>, which libnapi (and therefore Lynx) requires.
# ABI comparison of the selected media, image, Cronet, QuickJS, and Lynx graphs
# shows that the APK libc++ satisfies their complete std::__ndk1 import set.
# This ordering is namespace-local; appspawn-x still uses the OH platform
# libc++ selected by the separate, libs-last LD_LIBRARY_PATH below.
export WESTLAKE_ANDROID_NATIVE_SEARCH_PATH=/data/local/tmp/asx/lib/arm64:/data/local/tmp/asx/nsprobe:/data/local/tmp/asx/webview-t-lib:/data/local/tmp/asx:/system/lib64/ndk:/system/lib64/platformsdk:/system/lib64/chipset-sdk-sp:/system/lib64
# The isolated image/media namespace still inherits its normal platform ABI.
export WESTLAKE_ANDROID_NATIVE_INHERIT=libc.so:libdl.so:libm.so:libz.so:libbionic_abi_shim.so
# Bionic and OH/musl reverse the two pointer fields in struct addrinfo.
# Cronet and Android WebView both perform DNS in Android-built DSOs, so select
# both at the boundary without altering OH-native callers.
export WESTLAKE_ANDROID_NATIVE_NET_TARGETS=libmffmpeg.so:libttmplayer.so:libsscronet.so:libwebviewchromium.so
# WESTLAKE §770 diagnostic: confirm whether the software video decoder binds,
# locks, and posts the TextureView's OH NativeWindow after network startup.
export WESTLAKE_TRACE_ANDROID_NATIVE_WINDOW=${WL_TRACE_ANDROID_NATIVE_WINDOW:-0}
export LD_LIBRARY_PATH=/data/local/tmp/asx:/system/lib64:/system/lib64/platformsdk:/system/lib64/chipset-sdk:/system/lib64/chipset-sdk-sp:/system/lib64/ndk:/system/lib64/module/data:/system/lib64/module:/data/local/tmp/asx/lib/arm64:/data/local/tmp/asx/webview-t-lib
# Android retained a tiny legacy libstdc++.so after moving to libc++. Some APK
# DSOs (including libEncryptor) have a DT_NEEDED entry for the old SONAME but
# import no C++ symbols from it. The deployment stages our compatibility DSO
# beside the extracted APK libraries, where dependent-library lookup can see it,
# without binding Android NDK C++ code to OH's libc++ implementation.
# WESTLAKE diagnostic: observe only calls originating in the Android player
# DSOs, forwarding their POSIX network operations unchanged to OH libc.
WESTLAKE_PRELOAD=/data/local/tmp/asx/nsprobe/libbionic_stdio_shim.so:/data/local/tmp/asx/libandroid_native_network_compat.so:/data/local/tmp/asx/liblog_shim.so:/data/local/tmp/asx/libbionic_abi_shim.so:/data/local/tmp/asx/libwl636.so:/data/local/tmp/asx/webview-t-lib/libwebview_bionic_shim.so
# Diagnostic only: preserve the normal preload set by default, but allow a
# controlled run to identify deliberate exit/kill paths that do not reach the
# child fatal-signal handler.
if [ "${WL_EXIT_TRACE:-0}" = "1" ]; then
  WESTLAKE_PRELOAD=/data/local/tmp/asx/libwestlake_exit_trace.so:$WESTLAKE_PRELOAD
fi
export LD_PRELOAD=$WESTLAKE_PRELOAD
if [ "${WL_PTRACE_TRACE:-0}" = "1" ]; then
  exec /data/local/tmp/asx/fatal_signal_tracer \
    /data/local/tmp/asx/appspawn-x \
    --sandbox-config /data/local/tmp/asx/appspawn_x_sandbox.json \
    > /data/local/tmp/asx/asx.err 2>&1
fi
exec /data/local/tmp/asx/appspawn-x --sandbox-config /data/local/tmp/asx/appspawn_x_sandbox.json > /data/local/tmp/asx/asx.err 2>&1
