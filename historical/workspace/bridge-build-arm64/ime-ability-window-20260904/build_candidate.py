#!/usr/bin/env python3
"""Overlay only the OH focus bridge, keeping the accepted network inputs intact."""
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import zipfile
ROOT = Path(__file__).resolve().parent
PREVIOUS = ROOT.parent / 'ime-network-fix-20260904'
BRIDGE = Path('/home/dspfac/bridge-build')
REMOTE = '/data/local/tmp/asx/ime-ability-window-20260904'
NAME = os.environ.get('CANDIDATE', 'candidate04')
assert re.fullmatch(r'candidate[0-9]{2,}', NAME)
OUT = ROOT / NAME
if (OUT / 'manifest.json').exists():
    raise RuntimeError('Preserve built evidence; set CANDIDATE to a new candidateNN directory')
OUT.mkdir(exist_ok=True)
def run(*args, **kwargs): subprocess.run([str(x) for x in args], check=True, **kwargs)
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
for part in ('fw', 'runtime'):
    (OUT / part).mkdir(exist_ok=True)
    for p in (PREVIOUS / part).iterdir():
        if p.is_file(): shutil.copy2(p, OUT / part / p.name)
classes = OUT / 'java-classes'
classes.mkdir(exist_ok=True)
run('javac', '--release', '8', '-classpath', ROOT.parent / 'bcp-build/cp/framework-classes.jar',
    '-d', classes, BRIDGE / 'src/framework/window/java/SessionStageBridge.java',
    BRIDGE / 'src/framework/window/java/WindowCallbackBridge.java',
    BRIDGE / 'src/framework/window/java/OhImeBridge.java')
# WindowCallbackBridge is compiled for type checking, but its deployed bytecode
# already implements the focus latch. Do not replace unrelated callback classes.
with zipfile.ZipFile(OUT / 'overlay.jar', 'w') as overlay:
    for p in (classes / 'adapter/window').glob('*.class'):
        if p.name.startswith(('SessionStageBridge', 'OhImeBridge')):
            overlay.write(p, str(p.relative_to(classes)))
(OUT / 'dex').mkdir(exist_ok=True)
run('/home/dspfac/android-sdk/build-tools/34.0.0/d8', '--min-api', '30', '--output', OUT / 'dex', OUT / 'overlay.jar')
smali = (BRIDGE / 'framework-patch-tools-rescue/cp.txt').read_text().strip() + ':' + str(
    ROOT.parent / 'fullrun-20260824-final/fw-inputasync-build-7.nVPsud/smali-3.0.3.jar') + ':' + str(
    ROOT.parent / 'tools/smali-2.5.2/*')
for source, target in [(OUT / 'dex/classes.dex', OUT / 'overlay-smali'),
                       (PREVIOUS / 'fw/oh-adapter-framework.jar', OUT / 'adapter-smali')]:
    run('java', '-cp', smali, 'com.android.tools.smali.baksmali.Main', 'disassemble', '-o', target, source)
for p in (OUT / 'overlay-smali/adapter/window').glob('*.smali'):
    shutil.copy2(p, OUT / 'adapter-smali/adapter/window' / p.name)
run('java', '-cp', smali, 'com.android.tools.smali.smali.Main', 'assemble', '-a', '30',
    '-o', OUT / 'adapter.dex', OUT / 'adapter-smali')
with zipfile.ZipFile(PREVIOUS / 'fw/oh-adapter-framework.jar') as old:
    with zipfile.ZipFile(OUT / 'fw/oh-adapter-framework.jar', 'w', zipfile.ZIP_DEFLATED) as new:
        for info in old.infolist():
            new.writestr(info, (OUT / 'adapter.dex').read_bytes() if info.filename == 'classes.dex' else old.read(info.filename))
    with zipfile.ZipFile(OUT / 'fw/oh-adapter-framework.jar') as new:
        assert old.namelist() == new.namelist()
        for name in old.namelist():
            if name != 'classes.dex': assert old.read(name) == new.read(name)
# The deployed boot path also defines SessionStageBridge in apache-xml's fourth
# DEX. ART resolves that earlier definition. Overlay both definitions while
# byte-preserving all other ZIP entries, rather than silently patching dead code.
with zipfile.ZipFile(PREVIOUS / 'fw/apache-xml.jar') as old:
    original = old.read('classes4.dex')
    (OUT / 'apache-classes4.dex').write_bytes(original)
    run('java', '-cp', smali, 'com.android.tools.smali.baksmali.Main', 'disassemble',
        '-o', OUT / 'apache-smali', OUT / 'apache-classes4.dex')
    assert (OUT / 'apache-smali/adapter/window/SessionStageBridge.smali').exists()
    shutil.copy2(OUT / 'overlay-smali/adapter/window/SessionStageBridge.smali',
        OUT / 'apache-smali/adapter/window/SessionStageBridge.smali')
    run('java', '-cp', smali, 'com.android.tools.smali.smali.Main', 'assemble', '-a', '30',
        '-o', OUT / 'apache-patched.dex', OUT / 'apache-smali')
    with zipfile.ZipFile(OUT / 'fw/apache-xml.jar', 'w', zipfile.ZIP_DEFLATED) as new:
        for info in old.infolist():
            new.writestr(info, (OUT / 'apache-patched.dex').read_bytes() if info.filename == 'classes4.dex' else old.read(info.filename))
env = os.environ.copy()
env.update(WESTLAKE_USE_BC_DIGESTS='1', FW_OVERRIDE=str(OUT / 'fw'), OUT_OVERRIDE=str(OUT / 'boot'),
    DEVICE_FW_DIR_OVERRIDE=REMOTE + '/' + NAME + '/fw', EXPECTED_BCP_MD5_OVERRIDE=hashlib.md5(
        (OUT / 'fw/adapter-runtime-bcp.jar').read_bytes()).hexdigest())
run('bash', ROOT.parent / 'build_fwbootimg_v63_westlake_provider_first_20260904.sh', env=env)
sdk = Path('/home/dspfac/ohos-sdk-6.1/linux/native')
run(sdk / 'llvm/bin/clang++', '--target=aarch64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot'),
    '-shared', '-fPIC', '-O2', '-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-I' + str(BRIDGE / 'aosp/libnativehelper/include_jni'), '-Wl,-z,defs',
    '-Wl,-soname,liboh_ime_helper_capi.so', BRIDGE / 'src/framework/window/jni/oh_ime_helper_capi.cpp',
    '-lohinputmethod', '-ldl', '-o', OUT / 'runtime/liboh_ime_helper_capi.so')
manifest = {str(p.relative_to(OUT)): digest(p) for part in ('fw', 'runtime', 'boot')
    for p in (OUT / part).iterdir() if p.is_file()}
(OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('CANDIDATE_BUILD_OK')
