#!/usr/bin/env python3
"""Stage an isolated, hash-pinned image/runtime; never replace the baseline."""
import concurrent.futures
import hashlib
import shutil
import sys
from pathlib import Path

import device

ROOT = device.ROOT
BASE = device.REMOTE
BOOT = '/data/local/tmp/asx/fwbootimg-v118-explicitfault-objectfieldoffset-verify-nojavatimezone-v4-20260903/ime-network-fix-20260904'

def main():
    # Never truncate an image/library mapped by a running repair process.
    # Other baselines use different paths and are deliberately out of scope.
    guard = BASE + '/runtime/' + ('liboh_ime_helper_capi.so' if '--helper-only' in sys.argv else 'libart.so')
    active = device.shell("for p in /proc/[0-9]*; do if grep -q -F '" + guard
            + "' \"$p/maps\" 2>/dev/null; then echo IME_NET_ACTIVE_PID ${p##*/}; fi; done")
    if 'IME_NET_ACTIVE_PID' in active:
        raise RuntimeError('Archive and stop the identified repair processes before staging:\n' + active)
    runtime = ROOT / 'runtime'
    runtime.mkdir(exist_ok=True)
    pinned = ROOT.parent / 'out-jit-review-fixes-20260904'
    pins = {'libart.so': 'c8b5e7352662109ddcb9ca3b97020894',
            'appspawn-x': 'a8cb923655f0439dc54f0c0ef1731d8f',
            'liboh_adapter_bridge.so': 'b58ed9c01a93df2601e9c0671afdf2df',
            'libwestlake_runtime_boundary.so': '349ad3c62305dddd612337cff317e1eb'}
    for name, digest in pins.items():
        p = pinned / name
        assert hashlib.md5(p.read_bytes()).hexdigest() == digest, p
        shutil.copy2(p, runtime / name)
    helper = ROOT / 'liboh_ime_helper_capi.so'
    assert helper.read_bytes()[18:20] == b'\xb7\x00', 'Helper must be AArch64 ELF'
    shutil.copy2(helper, runtime / helper.name)
    shutil.copy2(ROOT / 'liboh_network_jni.so', runtime / 'liboh_network_jni.so')
    common = (ROOT.parent / 'run-jit-aot-candidate-20260902.sh').read_text()
    begin = common.index('TOUTIAO_INSTALL_DIR=')
    end = common.index('export ASX_LAUNCH_PKG ASX_APK_PATH ASX_LAUNCH_ACTIVITY', begin)
    # Generic direct-launch path: use the selected unchanged APK as-is. Avoid
    # rewriting Toutiao's install symlink when running an independent control.
    common = common[:begin] + 'test -f "$ASX_APK_PATH"\n' + common[end:]
    (ROOT / 'run-common.sh').write_text(common)
    paths = sorted((ROOT / 'fw').glob('*.jar'))
    paths += sorted(p for p in (ROOT / 'boot').glob('boot*') if p.suffix in ('.art', '.oat', '.vdex'))
    assert len(paths) == 40, 'Ten boot class path inputs and ten image triplets required'
    paths += sorted(runtime.glob('*')) + [ROOT / 'run-common.sh']
    checks = []
    for p in paths:
        rel = p.relative_to(ROOT).as_posix()
        digest = hashlib.md5(p.read_bytes()).hexdigest()
        target = BOOT + '/' + p.name if p.parent.name == 'boot' else BASE + '/' + rel
        checks.append(f'expect_md5 {digest} "{target}"')
    runner = '''#!/system/bin/sh
set -eu
BASE=/data/local/tmp/asx/ime-network-fix-20260904
expect_md5() {
  test "$(md5sum "$2" | cut -d ' ' -f1)" = "$1" || { echo "HASH_MISMATCH $2"; exit 1; }
}
''' + '\n'.join(checks) + '''
: "${ASX_APK_PATH:=/data/local/tmp/asx/toutiao.apk}"
: "${ASX_LAUNCH_PKG:=com.ss.android.article.news}"
: "${ASX_LAUNCH_ACTIVITY:=com.ss.android.article.news.activity.MainActivity}"
if [ "$ASX_LAUNCH_PKG" = com.ss.android.article.news ]; then
  expect_md5 cc99caaaa3baffbb100118753fa1d130 "$ASX_APK_PATH"
fi
export ASX_APK_PATH ASX_LAUNCH_PKG ASX_LAUNCH_ACTIVITY
export ASX_SOCKET_NAME=${ASX_SOCKET_NAME:-AppSpawnXImeNet}
export ASX_FW_DIR=$BASE/fw ASX_RUNTIME=$BASE/runtime
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
'''
    runner = runner.replace('export WESTLAKE_USE_BC_DIGESTS=1',
            'export ASX_FW_IMAGE=' + BOOT + '\nexport WESTLAKE_USE_BC_DIGESTS=1')
    (ROOT / 'run.sh').write_text(runner)
    paths.append(ROOT / 'run.sh')
    device.shell(f'mkdir -p {BASE}/fw {BOOT} {BASE}/runtime')
    def send(p):
        rel = p.relative_to(ROOT).as_posix()
        # Windows HDC treats forward-slash host subpaths as directory sends.
        # Use Windows separators for the host side, POSIX for the device side.
        target = BOOT + '/' + p.name if p.parent.name == 'boot' else BASE + '/' + rel
        device.run(['file', 'send', rel.replace('/', '\\'), target])
    send_paths = paths
    if '--helper-only' in sys.argv:
        send_paths = [runtime / helper.name, ROOT / 'run.sh']
    with concurrent.futures.ThreadPoolExecutor(3) as pool:
        list(pool.map(send, send_paths))
    result = device.shell(f'chmod 755 {BASE}/runtime/appspawn-x; VALIDATE_ONLY=1 sh {BASE}/run.sh')
    assert 'IME_NET_INPUTS_OK' in result and 'HASH_MISMATCH' not in result, result
    print('Staged', len(send_paths), 'files; device-verified', len(paths), 'inputs', flush=True)

if __name__ == '__main__':
    main()
