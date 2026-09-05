"""Freeze the tested native repairs; inherit candidate02's exact boot image."""
from pathlib import Path
import json,hashlib,shutil
import device
ROOT=device.ROOT;BASE=ROOT/'candidate02';OUT=ROOT/'candidate03'
assert not (OUT/'manifest.json').exists(),'Preserve frozen candidate'
assert 'EXISTS' not in device.shell('test -e /data/local/tmp/asx/gap-repair-20260904/candidate03/run.sh && echo EXISTS')
(OUT/'runtime').mkdir(parents=True,exist_ok=True)
for p in (BASE/'runtime').iterdir():
    if p.is_file():shutil.copy2(p,OUT/'runtime'/p.name)
for source,name in [('liboh_network_jni_v4.so','liboh_network_jni.so'),('liboh_webview_startup_order.so','liboh_webview_startup_order.so'),('liboh_hwui_functor.so','liboh_hwui_functor.so'),('baseline-libhwui.so','libhwui.so')]:
    shutil.copy2(ROOT/source,OUT/'runtime'/name)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest={str(p.relative_to(OUT)):sha(p) for p in (OUT/'runtime').iterdir() if p.is_file()}
remote='/data/local/tmp/asx/gap-repair-20260904/candidate03'
fw='/data/local/tmp/asx/gap-repair-20260904/candidate02/fw'
boot='/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/gap-repair-20260904-candidate02'
checks=[]
for name,digest in json.loads((BASE/'manifest.json').read_text()).items():
    if name.startswith('fw/'):target=fw+'/'+Path(name).name
    elif name.startswith('boot/') and name.endswith(('.art','.oat','.vdex')):target=boot+'/'+Path(name).name
    else:continue
    checks.append('expect_sha '+digest+' '+target)
for name,digest in manifest.items():checks.append('expect_sha '+digest+' '+remote+'/'+name)
common=(BASE/'run-common.sh').read_text();(OUT/'run-common.sh').write_text(common)
runner='''#!/system/bin/sh
set -eu
BASE=REMOTE
expect_sha() { test "$(sha256sum "$2" | cut -d ' ' -f1)" = "$1" || { echo HASH_MISMATCH "$2"; exit 1; }; }
CHECKS
if [ "${VALIDATE_ONLY:-0}" = 1 ]; then echo GAP_INPUTS_OK; exit 0; fi
: "${WL_PARENT_ID:?A live ability window ID is required}"
: "${ASX_SOCKET_NAME:=AppSpawnXGapCandidate03}"
: "${WESTLAKE_WEBVIEW_SOFTWARE_FALLBACK:=0}"
export WL_SUB_WINDOW=1 WL_FOCUSABLE=1 WL_SESSION_BUNDLE=org.westlake.imehost
export WL_ABILITY_OWNED_WINDOWS=1 WL_MMI=1 ASX_SOCKET_NAME
export ASX_FW_DIR=FWDIR ASX_RUNTIME=$BASE/runtime ASX_FW_IMAGE=BOOTDIR
export WESTLAKE_USE_BC_DIGESTS=1 WESTLAKE_WEBVIEW_SOFTWARE_FALLBACK
export WESTLAKE_HWUI_HELPER_PATH=$BASE/runtime/liboh_hwui_functor.so
export WESTLAKE_IME_HELPER_PATH=$BASE/runtime/liboh_ime_helper_capi.so
export WESTLAKE_NET_HELPER_PATH=$BASE/runtime/liboh_network_jni.so
export WESTLAKE_CHILD_REBIND_PORTABLE_TIMEZONE=1
export WESTLAKE_CHILD_DEOPT_BOOT=0 WESTLAKE_CHILD_SWITCH_BOOT=0
export WESTLAKE_JIT_APP_ONLY=1 WESTLAKE_JIT_BOOT_PARSER=1 WESTLAKE_JIT_BOOT_NATIVE_STUBS=1
export APPSPAWNX_JIT_THRESHOLD=2000 WL_TOUCH_ENQUEUE=1
exec sh "$BASE/run-common.sh"
'''
(OUT/'run.sh').write_text(runner.replace('REMOTE',remote).replace('FWDIR',fw).replace('BOOTDIR',boot).replace('CHECKS','\n'.join(checks)))
manifest.update({name:sha(OUT/name) for name in ('run.sh','run-common.sh')})
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(OUT/'inherited.json').write_text(json.dumps({'framework_boot':'candidate02','manifest_sha256':sha(BASE/'manifest.json'),'hwui_baseline_md5':hashlib.md5((OUT/'runtime/libhwui.so').read_bytes()).hexdigest(),'control_apk_sha256':sha(ROOT/'control-out/control.apk')},indent=2)+'\n')
device.shell('mkdir -p '+remote+'/runtime')
for name in manifest:device.send('candidate03/'+name,remote+'/'+name)
result=device.shell('chmod 755 '+remote+'/runtime/appspawn-x; VALIDATE_ONLY=1 sh '+remote+'/run.sh')
assert 'GAP_INPUTS_OK' in result,result
print('FROZEN_CANDIDATE03_OK',result)
