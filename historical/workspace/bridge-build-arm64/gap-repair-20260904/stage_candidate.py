"""Stage a new immutable candidate with pinned framework and boot inputs."""
import os,json,re,shutil
from pathlib import Path
import device
ROOT=device.ROOT;NAME=os.environ.get('CANDIDATE','candidate01');assert re.fullmatch('candidate[0-9]+',NAME)
OUT=ROOT/NAME;REMOTE='/data/local/tmp/asx/gap-repair-20260904/'+NAME
BOOT='/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/gap-repair-20260904-'+NAME
assert 'EXISTS' not in device.shell('test -e '+REMOTE+'/run.sh && echo EXISTS'),'Candidate already staged; choose a new name'
baseline=ROOT.parent/'ime-ability-window-20260904/candidate04'
common=(baseline/'run-common.sh').read_text()
if (OUT/'runtime/liboh_webview_startup_order.so').exists():
    common=common.replace('export LD_PRELOAD=','export LD_PRELOAD=$RUNTIME/liboh_webview_startup_order.so:')
if (OUT/'runtime/liboh_egl_boundary.so').exists():
    common=common.replace('export LD_PRELOAD=','export LD_PRELOAD=$RUNTIME/liboh_egl_boundary.so:')
if (OUT/'runtime/liboh_ime_geometry.so').exists():
    common=common.replace('export LD_PRELOAD=','export LD_PRELOAD=$RUNTIME/liboh_ime_geometry.so:')
(OUT/'run-common.sh').write_text(common)
manifest=json.loads((OUT/'manifest.json').read_text());checks=[];paths=[]
for name,sha in manifest.items():
    if name.startswith('boot/') and not name.endswith(('.art','.oat','.vdex')):continue
    target=BOOT+'/'+Path(name).name if name.startswith('boot/') else REMOTE+'/'+name
    paths.append((name,target));checks.append('expect_sha '+sha+' '+target)
runner='''#!/system/bin/sh
set -eu
BASE=REMOTE
expect_sha() { test "$(sha256sum "$2" | cut -d ' ' -f1)" = "$1" || { echo HASH_MISMATCH "$2"; exit 1; }; }
CHECKS
if [ "${VALIDATE_ONLY:-0}" = 1 ]; then echo GAP_INPUTS_OK; exit 0; fi
: "${WL_PARENT_ID:?A live ability window ID is required}"
export WL_SUB_WINDOW=1 WL_FOCUSABLE=1 WL_SESSION_BUNDLE=org.westlake.imehost
export WL_ABILITY_OWNED_WINDOWS=1 WL_MMI=1
: "${ASX_SOCKET_NAME:=AppSpawnXGapCandidate01}"
export ASX_SOCKET_NAME
export ASX_FW_DIR=$BASE/fw ASX_RUNTIME=$BASE/runtime ASX_FW_IMAGE=BOOTDIR
export WESTLAKE_USE_BC_DIGESTS=1 WESTLAKE_WEBVIEW_SOFTWARE_FALLBACK=0
export WESTLAKE_HWUI_HELPER_PATH=$BASE/runtime/liboh_hwui_functor.so
export WESTLAKE_IME_HELPER_PATH=$BASE/runtime/liboh_ime_helper_capi.so
export WESTLAKE_IME_METADATA_PATH=$BASE/runtime/liboh_ime_metadata.so
export WESTLAKE_NET_HELPER_PATH=$BASE/runtime/liboh_network_jni.so
export WESTLAKE_TLS_HELPER_PATH=$BASE/runtime/liboh_tls_boundary.so
export WESTLAKE_PERMISSION_HELPER_PATH=$BASE/runtime/liboh_permission_queries.so
export WESTLAKE_CHILD_REBIND_PORTABLE_TIMEZONE=1
export WESTLAKE_CHILD_DEOPT_BOOT=0 WESTLAKE_CHILD_SWITCH_BOOT=0
export WESTLAKE_JIT_APP_ONLY=1 WESTLAKE_JIT_BOOT_PARSER=1 WESTLAKE_JIT_BOOT_NATIVE_STUBS=1
export APPSPAWNX_JIT_THRESHOLD=2000 WL_TOUCH_ENQUEUE=1
exec sh "$BASE/run-common.sh"
'''
(OUT/'run.sh').write_text(runner.replace('REMOTE',REMOTE).replace('BOOTDIR',BOOT).replace('CHECKS','\n'.join(checks)))
paths += [(name,REMOTE+'/'+name) for name in ('run.sh','run-common.sh')]
device.shell('mkdir -p '+REMOTE+'/fw '+REMOTE+'/runtime '+BOOT)
for name,target in paths:device.send(NAME+'/'+name,target)
result=device.shell('chmod 755 '+REMOTE+'/runtime/appspawn-x; VALIDATE_ONLY=1 sh '+REMOTE+'/run.sh')
assert 'GAP_INPUTS_OK' in result,result
print(result)
