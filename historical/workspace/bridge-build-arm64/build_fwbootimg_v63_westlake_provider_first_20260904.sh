#!/bin/bash
set -euo pipefail

readonly ROOT=/home/dspfac/bridge-build-arm64
readonly D2O=/home/dspfac/art-latest/build-host-v118-explicit-fault-20260903/bin/dex2oat.objectfieldoffset-v1
readonly ART=/home/dspfac/art-latest
readonly FW=${FW_OVERRIDE:-$ROOT/fwjars-v63-westlake-provider-first-20260904}
readonly OUT=${OUT_OVERRIDE:-$ROOT/fwbootimg-v118-objectfieldoffset-verify-v63-westlake-provider-first-20260904}
readonly PRELOADED=$ROOT/jit-profile-20260903/preloaded-classes-no-java-timezone-v1
readonly EXPECTED_BCP_MD5=${EXPECTED_BCP_MD5_OVERRIDE:-2f6ca05e76004acf463f973b4174f03d}
readonly DEV=${DEVICE_FW_DIR_OVERRIDE:-/data/local/tmp/asx/fw-v63-westlake-provider-first-20260904}

actual=$(md5sum "$FW/adapter-runtime-bcp.jar" | awk '{print $1}')
if [[ "$actual" != "$EXPECTED_BCP_MD5" ]]; then
  echo "refusing adapter-runtime-bcp.jar md5 $actual; expected $EXPECTED_BCP_MD5" >&2
  exit 1
fi

jars=(
  core-oj core-libart core-icu4j okhttp bouncycastle apache-xml
  adapter-mainline-stubs framework adapter-runtime-bcp oh-adapter-framework
)
dex_args=()
for jar in "${jars[@]}"; do
  dex_args+=(--dex-file="$FW/$jar.jar" --dex-location="$DEV/$jar.jar")
done

mkdir -p "$OUT"
ANDROID_ROOT="$ART" timeout 1200 "$D2O" "${dex_args[@]}" \
  --oat-file="$OUT/boot.oat" \
  --image="$OUT/boot.art" \
  --instruction-set=arm64 \
  --compiler-filter=verify \
  --preloaded-classes="$PRELOADED" \
  --base=0x70000000 \
  --inline-max-code-units=0 \
  --android-root="$ART" \
  --runtime-arg -Xverify:none \
  --runtime-arg -Xmx256m \
  -j1 2>"$OUT/d2o.err"

md5sum "$OUT"/boot* > "$OUT/md5sums.txt"
echo "built v63 verify boot image: $OUT"
cat "$OUT/md5sums.txt"
