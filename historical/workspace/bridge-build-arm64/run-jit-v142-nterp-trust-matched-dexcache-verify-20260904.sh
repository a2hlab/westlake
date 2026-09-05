#!/system/bin/sh
set -eu

ASX=/data/local/tmp/asx
RUNTIME=$ASX/out-jit-perf-v142-nterp-trust-matched-dexcache-pinned-20260904
BOOT=$ASX/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903
FW=$ASX/fw
export ASX_FW_DIR=$FW
CACHE_DIR=/data/data/com.ss.android.article.news/cache/oat_primary/arm64

expect_md5() {
    test "$(md5sum "$2" | cut -d ' ' -f1)" = "$1"
}

expect_md5 20e48b90b6b01468f155ed7c48097e70 "$RUNTIME/libart.so"
expect_md5 a8cb923655f0439dc54f0c0ef1731d8f "$RUNTIME/appspawn-x"
expect_md5 52ece63117854d2704ad9cdea3cdab5a "$RUNTIME/liboh_adapter_bridge.so"
expect_md5 d8b3902969be014b58a659f95426535e "$RUNTIME/libwestlake_runtime_boundary.so"

# The nterp-only fast path is valid only for this complete, matched boot-image
# and boot-class-path set.  Keep all generic ClassLinker validation enabled in
# the rest of libart.
expect_md5 a98d64c8bb47a526deb7fd3be4dcd912 "$BOOT/boot.art"
expect_md5 03eba5dde8d120c0442520648d34e57b "$BOOT/boot.oat"
expect_md5 7dc4bd414985d6276a26e923f047219a "$BOOT/boot.vdex"
expect_md5 b330e3b8c92ac2879288241971f7f757 "$BOOT/boot-core-libart.art"
expect_md5 a01bd59081c07acc64838d34fdc84e29 "$BOOT/boot-core-libart.oat"
expect_md5 aa55ee91ad7b49d3d18888b79a63b8da "$BOOT/boot-core-libart.vdex"
expect_md5 261741106cd1e1352b9abe6fa50d4818 "$BOOT/boot-core-icu4j.art"
expect_md5 c92ecf7057044030dd6de98066a0afaf "$BOOT/boot-core-icu4j.oat"
expect_md5 c07d5aca7685155f28f4f381fcb0b7b8 "$BOOT/boot-core-icu4j.vdex"
expect_md5 100e9762b7d10cd5eb5191d570fa6c82 "$BOOT/boot-okhttp.art"
expect_md5 def1f523bf254ea30515721bb336f192 "$BOOT/boot-okhttp.oat"
expect_md5 edf6f77aff780c355122b54dddd2ce00 "$BOOT/boot-okhttp.vdex"
expect_md5 000ed70b43462a3f88cd0a1e6a4827fa "$BOOT/boot-bouncycastle.art"
expect_md5 0e1b4a13ce6a1abcffd0ce55388be3a7 "$BOOT/boot-bouncycastle.oat"
expect_md5 7bfc63e2df3f55a7edd49d578ac3e8bc "$BOOT/boot-bouncycastle.vdex"
expect_md5 a7223cd47dc31300248001b1e0f734eb "$BOOT/boot-apache-xml.art"
expect_md5 d4ce7e570700752ce71319c687fbfaf5 "$BOOT/boot-apache-xml.oat"
expect_md5 66a4503049093f9cb702cc4ff9a3c09e "$BOOT/boot-apache-xml.vdex"
expect_md5 15389b2b0083059f2b1f7b2f31bafa0a "$BOOT/boot-adapter-mainline-stubs.art"
expect_md5 c680dab4714feef960a097492717d635 "$BOOT/boot-adapter-mainline-stubs.oat"
expect_md5 73c12581adb53096c940c56a849f1b9a "$BOOT/boot-adapter-mainline-stubs.vdex"
expect_md5 e8102f65280ebac2a615807fec4f1eb1 "$BOOT/boot-framework.art"
expect_md5 97ce50d6d813878d8354b2aa812431b9 "$BOOT/boot-framework.oat"
expect_md5 fd6c043210fdeb25b3ad16f4bc20de5c "$BOOT/boot-framework.vdex"
expect_md5 1aee1e444c99da5872f319f8d13158a9 "$BOOT/boot-adapter-runtime-bcp.art"
expect_md5 74a129119247e072445e771d0d71bc2a "$BOOT/boot-adapter-runtime-bcp.oat"
expect_md5 110fef226f39350277b2ab7ba58699df "$BOOT/boot-adapter-runtime-bcp.vdex"
expect_md5 e7089782e456df0fb391cc55926cc006 "$BOOT/boot-oh-adapter-framework.art"
expect_md5 de3c8da4edd3ae3b0c2b579369f17ea9 "$BOOT/boot-oh-adapter-framework.oat"
expect_md5 b0477a19a318f71e580d065eb2cd063f "$BOOT/boot-oh-adapter-framework.vdex"

expect_md5 366bf7205be4b39e789a8ce908bf1cdb "$FW/core-oj.jar"
expect_md5 f4cf9889b154ed75bbe18c930db08a92 "$FW/core-libart.jar"
expect_md5 46ac0e313e9407b3bf920da2bb3a20e7 "$FW/core-icu4j.jar"
expect_md5 24d148391606995267bdae292ed590a1 "$FW/okhttp.jar"
expect_md5 85dd99f8ac82c08cfacb4e530d3bb22b "$FW/bouncycastle.jar"
expect_md5 67055626a70feaab530aee4a6ba1012d "$FW/apache-xml.jar"
expect_md5 f62106537ce442659b9a9f7b2b8dbcfe "$FW/adapter-mainline-stubs.jar"
expect_md5 a39a4525221136f86c082802d4beb507 "$FW/framework.jar"
expect_md5 9c5e98061a50f946e82c7064e73326ea "$FW/adapter-runtime-bcp.jar"
expect_md5 180010c554b036e92c56b11b33ee301b "$FW/oh-adapter-framework.jar"

expect_md5 185c54b844f54a6656ff089032cf2ab2 "$CACHE_DIR/base.art"
expect_md5 03de8178cf4b87c463ab04e63963d412 "$ASX/apps/com.ss.android.article.news/oat/arm64/base.odex"
expect_md5 28b52582e0ccfe856d7c7f4aba7eb6e6 "$ASX/apps/com.ss.android.article.news/oat/arm64/base.vdex"

# Validate the effective app/provider paths and the exact delegated launcher,
# not just the ART/boot inputs. Overrides must have the same locked contents.
: "${ASX_APK_PATH:=$ASX/toutiao.apk}"
: "${ASX_WEBVIEW_APK:=$ASX/webview-t.apk}"
expect_md5 cc99caaaa3baffbb100118753fa1d130 "$ASX_APK_PATH"
expect_md5 ff5f17baa9e43ffaf8e1a39725f5fcde "$ASX_WEBVIEW_APK"
expect_md5 7885bdcda807bba1d3e00fceaf322d7d "$ASX/run-jit-aot-candidate-20260902.sh"
export ASX_APK_PATH ASX_WEBVIEW_APK
export ASX_SOCKET_NAME=AppSpawnX
export ASX_FW_IMAGE=$BOOT
export ASX_RUNTIME=$RUNTIME
export WESTLAKE_CHILD_REBIND_PORTABLE_TIMEZONE=1
export WESTLAKE_CHILD_DEOPT_BOOT=0
export WESTLAKE_CHILD_SWITCH_BOOT=0
export WESTLAKE_JIT_APP_ONLY=1
export WESTLAKE_JIT_BOOT_PARSER=1
export WESTLAKE_JIT_BOOT_NATIVE_STUBS=1
export APPSPAWNX_JIT_THRESHOLD=2000

exec sh "$ASX/run-jit-aot-candidate-20260902.sh" \
    > "$ASX/asx.err" 2>&1
