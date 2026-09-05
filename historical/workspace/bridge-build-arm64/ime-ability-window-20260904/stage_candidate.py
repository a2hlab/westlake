#!/usr/bin/env python3
import json
import os
import re
import hashlib
from pathlib import Path
import shutil
import subprocess
import device
ROOT = device.ROOT
NAME = os.environ.get('CANDIDATE', 'candidate04')
assert re.fullmatch(r'candidate[0-9]{2,}', NAME)
BASE = device.REMOTE + '/' + NAME
OUT = ROOT / NAME
BOOT = '/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-ability-window-20260904-' + NAME
active = device.shell("for p in /proc/[0-9]*; do if grep -q -F '" + BASE +
    "/runtime/libart.so' \"$p/maps\" 2>/dev/null; then echo ACTIVE_PID ${p##*/}; fi; done")
if 'ACTIVE_PID' in active: raise RuntimeError('Refusing mapped candidate overwrite: ' + active)
manifest = json.loads((OUT / 'manifest.json').read_text())
subprocess.run(['bash', str(ROOT / 'build_owned_z.sh')], check=True)
shutil.copy2(ROOT / 'libowned_window_z.so', OUT / 'runtime/libowned_window_z.so')
manifest['runtime/libowned_window_z.so'] = hashlib.sha256((OUT / 'runtime/libowned_window_z.so').read_bytes()).hexdigest()
(OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
subprocess.run(['bash', str(ROOT / 'build_owned_input.sh')], check=True)
shutil.copy2(ROOT / 'liboh_owned_input.so', OUT / 'runtime/liboh_owned_input.so')
manifest['runtime/liboh_owned_input.so'] = hashlib.sha256((OUT / 'runtime/liboh_owned_input.so').read_bytes()).hexdigest()
(OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
common = (ROOT.parent / 'ime-network-fix-20260904/run-common.sh').read_text()
# All common shared assets are already provisioned. Do not change global
# SELinux mode, permissions, system mounts, baseline files, or APK symlinks.
begin = common.index('setenforce 0')
end = common.index('ulimit -s', begin)
common = common[:begin] + common[end:]
common = common.replace('export LD_PRELOAD=', 'export LD_PRELOAD=$RUNTIME/liboh_owned_input.so:$RUNTIME/libowned_window_z.so:')
(OUT / 'run-common.sh').write_text(common)
checks = []
paths = []
for name, sha in manifest.items():
    if name.startswith('boot/') and not name.endswith(('.art', '.oat', '.vdex')): continue
    target = BOOT + '/' + Path(name).name if name.startswith('boot/') else BASE + '/' + name
    checks.append(f'expect_sha {sha} {target}')
    paths.append((OUT / name, target))
runner = '''#!/system/bin/sh
set -eu
BASE=BASE_PLACEHOLDER
expect_sha() { test "$(sha256sum "$2" | cut -d ' ' -f1)" = "$1" || { echo HASH_MISMATCH "$2"; exit 1; }; }
''' + '\n'.join(checks) + '''
if [ "${VALIDATE_ONLY:-0}" = 1 ]; then echo HOST_CANDIDATE_INPUTS_OK; exit 0; fi
: "${WL_PARENT_ID:?A live ability window ID is required}"
export WL_SUB_WINDOW=1 WL_FOCUSABLE=1 WL_SESSION_BUNDLE=org.westlake.imehost
export WL_ABILITY_OWNED_WINDOWS=1 WL_MMI=1
export ASX_SOCKET_NAME=SOCKET_PLACEHOLDER
export ASX_FW_DIR=$BASE/fw ASX_RUNTIME=$BASE/runtime
export ASX_FW_IMAGE=BOOT_PLACEHOLDER
export WESTLAKE_USE_BC_DIGESTS=1
export WESTLAKE_IME_HELPER_PATH=$BASE/runtime/liboh_ime_helper_capi.so
export WESTLAKE_NET_HELPER_PATH=$BASE/runtime/liboh_network_jni.so
export WESTLAKE_CHILD_REBIND_PORTABLE_TIMEZONE=1
export WESTLAKE_CHILD_DEOPT_BOOT=0 WESTLAKE_CHILD_SWITCH_BOOT=0
export WESTLAKE_JIT_APP_ONLY=1 WESTLAKE_JIT_BOOT_PARSER=1 WESTLAKE_JIT_BOOT_NATIVE_STUBS=1
export APPSPAWNX_JIT_THRESHOLD=2000 WL_TOUCH_ENQUEUE=1
exec sh "$BASE/run-common.sh"
'''
(OUT / 'run.sh').write_text(runner.replace('BASE_PLACEHOLDER', BASE).replace('BOOT_PLACEHOLDER', BOOT)
    .replace('SOCKET_PLACEHOLDER', 'AppSpawnXImeHost' + NAME.removeprefix('candidate')))
sdk = Path('/home/dspfac/ohos-sdk-6.1/linux/native')
subprocess.run([str(sdk / 'llvm/bin/clang'), '--target=aarch64-linux-ohos',
    '--sysroot=' + str(sdk / 'sysroot'), '-Wall', '-Wextra', '-Werror', '-O2',
    str(ROOT / 'host_spawn.c'), '-o', str(OUT / 'host-spawn')], check=True)
paths += [(OUT / name, BASE + '/' + name) for name in ('run.sh', 'run-common.sh', 'host-spawn')]
subprocess.run([str(sdk / 'llvm/bin/clang'), '--target=aarch64-linux-ohos',
    '--sysroot=' + str(sdk / 'sysroot'), '-Wall', '-Wextra', '-Werror', '-O2',
    str(ROOT / 'private_data_launch.c'), '-o', str(OUT / 'private-data-launch')], check=True)
paths.append((OUT / 'private-data-launch', BASE + '/private-data-launch'))
device.shell(f'mkdir -p {BASE}/fw {BASE}/runtime {BOOT}')
for path, target in paths:
    device.send(str(path.relative_to(ROOT)), target)
result = device.shell(f'chmod 755 {BASE}/runtime/appspawn-x {BASE}/host-spawn {BASE}/private-data-launch; VALIDATE_ONLY=1 sh {BASE}/run.sh')
assert 'HOST_CANDIDATE_INPUTS_OK' in result, result
print(result)
