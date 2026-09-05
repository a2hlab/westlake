#!/system/bin/sh
set -eu
BASE=/data/local/tmp/asx/ime-network-fix-20260904
expect_md5() {
  test "$(md5sum "$2" | cut -d ' ' -f1)" = "$1" || { echo "HASH_MISMATCH $2"; exit 1; }
}
expect_md5 f62106537ce442659b9a9f7b2b8dbcfe "/data/local/tmp/asx/ime-network-fix-20260904/fw/adapter-mainline-stubs.jar"
expect_md5 bc91be426e74fbbe179465f6ca3337c5 "/data/local/tmp/asx/ime-network-fix-20260904/fw/adapter-runtime-bcp.jar"
expect_md5 67055626a70feaab530aee4a6ba1012d "/data/local/tmp/asx/ime-network-fix-20260904/fw/apache-xml.jar"
expect_md5 af90d638bb8371757567c882a8abb640 "/data/local/tmp/asx/ime-network-fix-20260904/fw/bouncycastle.jar"
expect_md5 46ac0e313e9407b3bf920da2bb3a20e7 "/data/local/tmp/asx/ime-network-fix-20260904/fw/core-icu4j.jar"
expect_md5 2655a054e993aab5547d6089f2ef0969 "/data/local/tmp/asx/ime-network-fix-20260904/fw/core-libart.jar"
expect_md5 366bf7205be4b39e789a8ce908bf1cdb "/data/local/tmp/asx/ime-network-fix-20260904/fw/core-oj.jar"
expect_md5 a39a4525221136f86c082802d4beb507 "/data/local/tmp/asx/ime-network-fix-20260904/fw/framework.jar"
expect_md5 ef8116348e477945f94b2f59ae99c765 "/data/local/tmp/asx/ime-network-fix-20260904/fw/oh-adapter-framework.jar"
expect_md5 096bd79c2498134a9c1ca41fb184a144 "/data/local/tmp/asx/ime-network-fix-20260904/fw/okhttp.jar"
expect_md5 d51f6773efe08666266970d2f7edca0c "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-adapter-mainline-stubs.art"
expect_md5 8869307ac75f139e1a7c46dfb4551bbe "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-adapter-mainline-stubs.oat"
expect_md5 73c12581adb53096c940c56a849f1b9a "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-adapter-mainline-stubs.vdex"
expect_md5 0aaab4f02a006946642105953c08a554 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-adapter-runtime-bcp.art"
expect_md5 5c2ef034bbe50eb82edf72373bf7d6b9 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-adapter-runtime-bcp.oat"
expect_md5 8a9dc6cab12b63ba3949658f0861b044 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-adapter-runtime-bcp.vdex"
expect_md5 0535e91aaaec647b60b5f46666658d67 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-apache-xml.art"
expect_md5 170881f21d63cb6f6e2bdb46be6201a9 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-apache-xml.oat"
expect_md5 66a4503049093f9cb702cc4ff9a3c09e "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-apache-xml.vdex"
expect_md5 71a2487de0414284e45af1d0729cdb00 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-bouncycastle.art"
expect_md5 a9fe34c449e2d169c2b0ddea4fdfee11 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-bouncycastle.oat"
expect_md5 fbea07979472db00ccd59a8f2f45ff1e "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-bouncycastle.vdex"
expect_md5 c2a7f05f174b3e7eaeedb5f7a5598f95 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-core-icu4j.art"
expect_md5 0065d9a4078f3dce6e5bdcf6e8d5cedd "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-core-icu4j.oat"
expect_md5 c07d5aca7685155f28f4f381fcb0b7b8 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-core-icu4j.vdex"
expect_md5 d82924efc05ad6a9e3b834f61843b511 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-core-libart.art"
expect_md5 2b913d07821bf9f0a4b97a2664c18b33 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-core-libart.oat"
expect_md5 e9c23f66d283f4d86ceb1e938ab5c195 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-core-libart.vdex"
expect_md5 cdf54b10452920a52d30819d806be221 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-framework.art"
expect_md5 3dd880e0e4d5e8e6263037d230ad64a8 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-framework.oat"
expect_md5 fd6c043210fdeb25b3ad16f4bc20de5c "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-framework.vdex"
expect_md5 6f060b78522c76bb5034bfa7b6484ea4 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-oh-adapter-framework.art"
expect_md5 5f84292b5a0f92f0eab8f4783193b981 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-oh-adapter-framework.oat"
expect_md5 392dc6f719836f122baf7decbb3d82d8 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-oh-adapter-framework.vdex"
expect_md5 0a519a07fea37916f91a08791a3e843a "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-okhttp.art"
expect_md5 e265df259c103bd77b93af1c0c0bd393 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-okhttp.oat"
expect_md5 81f310163d67e51b0fc1e11f73057cbd "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot-okhttp.vdex"
expect_md5 6a4008310efdd4c9878eba05ed2162fd "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot.art"
expect_md5 a9ec6540003276d680558071242f3f81 "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot.oat"
expect_md5 7dc4bd414985d6276a26e923f047219a "/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904/boot.vdex"
expect_md5 a8cb923655f0439dc54f0c0ef1731d8f "/data/local/tmp/asx/ime-network-fix-20260904/runtime/appspawn-x"
expect_md5 c8b5e7352662109ddcb9ca3b97020894 "/data/local/tmp/asx/ime-network-fix-20260904/runtime/libart.so"
expect_md5 b58ed9c01a93df2601e9c0671afdf2df "/data/local/tmp/asx/ime-network-fix-20260904/runtime/liboh_adapter_bridge.so"
expect_md5 ba4fbceccf2a8981ace6f799d10ea074 "/data/local/tmp/asx/ime-network-fix-20260904/runtime/liboh_ime_helper_capi.so"
expect_md5 8db90ca5e8a6e6d7a27d6cfbf749c2fe "/data/local/tmp/asx/ime-network-fix-20260904/runtime/liboh_network_jni.so"
expect_md5 349ad3c62305dddd612337cff317e1eb "/data/local/tmp/asx/ime-network-fix-20260904/runtime/libwestlake_runtime_boundary.so"
expect_md5 9e157b168383a6fb81fe99fd255526d1 "/data/local/tmp/asx/ime-network-fix-20260904/run-common.sh"
: "${ASX_APK_PATH:=/data/local/tmp/asx/toutiao.apk}"
: "${ASX_LAUNCH_PKG:=com.ss.android.article.news}"
: "${ASX_LAUNCH_ACTIVITY:=com.ss.android.article.news.activity.MainActivity}"
if [ "$ASX_LAUNCH_PKG" = com.ss.android.article.news ]; then
  expect_md5 cc99caaaa3baffbb100118753fa1d130 "$ASX_APK_PATH"
fi
export ASX_APK_PATH ASX_LAUNCH_PKG ASX_LAUNCH_ACTIVITY
export ASX_SOCKET_NAME=${ASX_SOCKET_NAME:-AppSpawnXImeNet}
export ASX_FW_DIR=$BASE/fw ASX_RUNTIME=$BASE/runtime
export ASX_FW_IMAGE=/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904
export WESTLAKE_USE_BC_DIGESTS=1
export WESTLAKE_IME_HELPER_PATH=$BASE/runtime/liboh_ime_helper_capi.so
export WESTLAKE_NET_HELPER_PATH=$BASE/runtime/liboh_network_jni.so
export WESTLAKE_CHILD_REBIND_PORTABLE_TIMEZONE=1
export WESTLAKE_CHILD_DEOPT_BOOT=0 WESTLAKE_CHILD_SWITCH_BOOT=0
export WESTLAKE_JIT_APP_ONLY=1 WESTLAKE_JIT_BOOT_PARSER=1 WESTLAKE_JIT_BOOT_NATIVE_STUBS=1
export APPSPAWNX_JIT_THRESHOLD=2000 WL_TOUCH_ENQUEUE=1
export WESTLAKE_TRACE_ANDROID_NATIVE_NET=1
if [ "${VALIDATE_ONLY:-0}" = 1 ]; then echo IME_NET_INPUTS_OK; exit 0; fi
exec sh "$BASE/run-common.sh"
