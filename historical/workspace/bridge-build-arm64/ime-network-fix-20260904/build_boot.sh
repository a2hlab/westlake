#!/usr/bin/env bash
set -euo pipefail
root=/home/dspfac/bridge-build-arm64/ime-network-fix-20260904
export WESTLAKE_USE_BC_DIGESTS=1
FW_OVERRIDE="$root/fw" OUT_OVERRIDE="$root/boot" \
DEVICE_FW_DIR_OVERRIDE=/data/local/tmp/asx/ime-network-fix-20260904/fw \
EXPECTED_BCP_MD5_OVERRIDE="$(md5sum "$root/fw/adapter-runtime-bcp.jar" | cut -d ' ' -f1)" \
bash /home/dspfac/bridge-build-arm64/build_fwbootimg_v63_westlake_provider_first_20260904.sh
