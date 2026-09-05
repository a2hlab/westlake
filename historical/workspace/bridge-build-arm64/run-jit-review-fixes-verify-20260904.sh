#!/system/bin/sh
set -eu

ASX=/data/local/tmp/asx
RUNTIME=$ASX/out-jit-review-fixes-20260904
BOOT=$ASX/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903
FW=$ASX/fw
export ASX_FW_DIR=$FW
CACHE_DIR=/data/data/com.ss.android.article.news/cache/oat_primary/arm64

expect_md5() {
    test "$(md5sum "$2" | cut -d ' ' -f1)" = "$1"
}

# Isolated review candidate; v142 and the default AppSpawnX service stay intact.
expect_md5 c8b5e7352662109ddcb9ca3b97020894 "$RUNTIME/libart.so"
expect_md5 a8cb923655f0439dc54f0c0ef1731d8f "$RUNTIME/appspawn-x"
expect_md5 b58ed9c01a93df2601e9c0671afdf2df "$RUNTIME/liboh_adapter_bridge.so"
expect_md5 349ad3c62305dddd612337cff317e1eb "$RUNTIME/libwestlake_runtime_boundary.so"

# ResolveMethodForNterp trusts only DexFile identities copied from this fully
# validated boot image. Lock every image/oat/vdex and boot-class-path input.
for pair in \
    a98d64c8bb47a526deb7fd3be4dcd912:boot.art \
    03eba5dde8d120c0442520648d34e57b:boot.oat \
    7dc4bd414985d6276a26e923f047219a:boot.vdex \
    b330e3b8c92ac2879288241971f7f757:boot-core-libart.art \
    a01bd59081c07acc64838d34fdc84e29:boot-core-libart.oat \
    aa55ee91ad7b49d3d18888b79a63b8da:boot-core-libart.vdex \
    261741106cd1e1352b9abe6fa50d4818:boot-core-icu4j.art \
    c92ecf7057044030dd6de98066a0afaf:boot-core-icu4j.oat \
    c07d5aca7685155f28f4f381fcb0b7b8:boot-core-icu4j.vdex \
    100e9762b7d10cd5eb5191d570fa6c82:boot-okhttp.art \
    def1f523bf254ea30515721bb336f192:boot-okhttp.oat \
    edf6f77aff780c355122b54dddd2ce00:boot-okhttp.vdex \
    000ed70b43462a3f88cd0a1e6a4827fa:boot-bouncycastle.art \
    0e1b4a13ce6a1abcffd0ce55388be3a7:boot-bouncycastle.oat \
    7bfc63e2df3f55a7edd49d578ac3e8bc:boot-bouncycastle.vdex \
    a7223cd47dc31300248001b1e0f734eb:boot-apache-xml.art \
    d4ce7e570700752ce71319c687fbfaf5:boot-apache-xml.oat \
    66a4503049093f9cb702cc4ff9a3c09e:boot-apache-xml.vdex \
    15389b2b0083059f2b1f7b2f31bafa0a:boot-adapter-mainline-stubs.art \
    c680dab4714feef960a097492717d635:boot-adapter-mainline-stubs.oat \
    73c12581adb53096c940c56a849f1b9a:boot-adapter-mainline-stubs.vdex \
    e8102f65280ebac2a615807fec4f1eb1:boot-framework.art \
    97ce50d6d813878d8354b2aa812431b9:boot-framework.oat \
    fd6c043210fdeb25b3ad16f4bc20de5c:boot-framework.vdex \
    1aee1e444c99da5872f319f8d13158a9:boot-adapter-runtime-bcp.art \
    74a129119247e072445e771d0d71bc2a:boot-adapter-runtime-bcp.oat \
    110fef226f39350277b2ab7ba58699df:boot-adapter-runtime-bcp.vdex \
    e7089782e456df0fb391cc55926cc006:boot-oh-adapter-framework.art \
    de3c8da4edd3ae3b0c2b579369f17ea9:boot-oh-adapter-framework.oat \
    b0477a19a318f71e580d065eb2cd063f:boot-oh-adapter-framework.vdex
do
    expect_md5 "${pair%%:*}" "$BOOT/${pair#*:}"
done

for pair in \
    366bf7205be4b39e789a8ce908bf1cdb:core-oj.jar \
    f4cf9889b154ed75bbe18c930db08a92:core-libart.jar \
    46ac0e313e9407b3bf920da2bb3a20e7:core-icu4j.jar \
    24d148391606995267bdae292ed590a1:okhttp.jar \
    85dd99f8ac82c08cfacb4e530d3bb22b:bouncycastle.jar \
    67055626a70feaab530aee4a6ba1012d:apache-xml.jar \
    f62106537ce442659b9a9f7b2b8dbcfe:adapter-mainline-stubs.jar \
    a39a4525221136f86c082802d4beb507:framework.jar \
    9c5e98061a50f946e82c7064e73326ea:adapter-runtime-bcp.jar \
    180010c554b036e92c56b11b33ee301b:oh-adapter-framework.jar
do
    expect_md5 "${pair%%:*}" "$FW/${pair#*:}"
done

# App OAT is not trusted by the nterp shortcut, but remains locked so this is
# an adjacent runtime comparison rather than an input-change experiment.
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
export ASX_SOCKET_NAME=${ASX_SOCKET_NAME:-AppSpawnXJitReview}
export ASX_FW_IMAGE=$BOOT
export ASX_RUNTIME=$RUNTIME
export WESTLAKE_CHILD_REBIND_PORTABLE_TIMEZONE=1
export WESTLAKE_CHILD_DEOPT_BOOT=0
export WESTLAKE_CHILD_SWITCH_BOOT=0
export WESTLAKE_JIT_APP_ONLY=1
export WESTLAKE_JIT_BOOT_PARSER=1
export WESTLAKE_JIT_BOOT_NATIVE_STUBS=1
export APPSPAWNX_JIT_THRESHOLD=2000

if [ "${WL_JIT_REVIEW_VALIDATE_ONLY:-0}" = "1" ]; then
    echo "JIT_REVIEW_INPUTS_OK runtime=$RUNTIME framework=$ASX_FW_DIR"
    exit 0
fi
exec sh "$ASX/run-jit-aot-candidate-20260902.sh" > "$RUNTIME/parent.log" 2>&1
