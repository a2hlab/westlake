#!/usr/bin/env python3
"""Build isolated framework overlays from pinned dex inputs and source classes.

Disassembly/reassembly preserves every unrelated class; no APK is rewritten.
The Bouncy Castle capability patch leaves Conscrypt preference and all digest
methods intact. The explicit OHOS fallback flag is required on Android-branded
runtimes without Conscrypt; other configurations retain the original check.
"""
import hashlib
import shutil
import subprocess
import zipfile
from pathlib import Path

from fetch_inputs import ROOT, PINS

BRIDGE = Path('/home/dspfac/bridge-build')
ARM = ROOT.parent
SMALI = (BRIDGE / 'framework-patch-tools-rescue/cp.txt').read_text().strip() + ':' + str(
    ARM / 'fullrun-20260824-final/fw-inputasync-build-7.nVPsud/smali-3.0.3.jar') + ':' + str(
    ARM / 'tools/smali-2.5.2/*')

def run(*args):
    subprocess.run([str(a) for a in args], check=True, cwd=ROOT)

def decode(source, target):
    run('java', '-cp', SMALI, 'com.android.tools.smali.baksmali.Main', 'disassemble', '-o', target, source)

def assemble(tree, jar):
    dex = jar.with_suffix('.dex')
    run('java', '-cp', SMALI, 'com.android.tools.smali.smali.Main', 'assemble', '-a', '30', '-o', dex, tree)
    with zipfile.ZipFile(jar, 'w', zipfile.ZIP_DEFLATED) as out:
        # Boot inputs may be multidex: the deployed adapter's classes2.dex
        # owns its JSSE/socket helpers. Preserve every non-replaced entry.
        with zipfile.ZipFile(ROOT / 'original-fw' / jar.name) as original:
            for info in original.infolist():
                if info.filename != 'classes.dex':
                    out.writestr(info, original.read(info.filename))
        out.write(dex, 'classes.dex')
    with zipfile.ZipFile(jar) as output, zipfile.ZipFile(ROOT / 'original-fw' / jar.name) as original:
        assert set(output.namelist()) == set(original.namelist())
        for name in original.namelist():
            if name != 'classes.dex':
                assert output.read(name) == original.read(name), name

def main():
    for key, expected in PINS.items():
        p = ROOT / 'original-fw' / (key.replace('_', '-') + '.jar')
        assert hashlib.md5(p.read_bytes()).hexdigest() == expected, p
    fw = ROOT / 'fw'
    fw.mkdir(exist_ok=True)
    for p in (ROOT / 'original-fw').glob('*.jar'):
        if not (fw / p.name).exists():
            shutil.copy2(p, fw / p.name)
    classes = ROOT / 'java-classes'
    classes.mkdir(exist_ok=True)
    sources = [BRIDGE / 'src/framework/window/java/OhImeBridge.java',
               BRIDGE / 'src/framework/window/java/InputMethodManagerAdapter.java']
    sources += sorted((BRIDGE / 'src/framework/appspawn-x/java/adapter/security').glob('*.java'))
    cp = str(ARM / 'bcp-build/cp/framework-classes.jar')
    run('javac', '--release', '8', '-classpath', cp, '-d', classes, *sources)
    program = ROOT / 'java-overlay.jar'
    run('jar', 'cf', program, '-C', classes, '.')
    dexdir = ROOT / 'java-dex'
    dexdir.mkdir(exist_ok=True)
    run('/home/dspfac/android-sdk/build-tools/34.0.0/d8', '--min-api', '30', '--output', dexdir, program)
    overlay = ROOT / 'java-smali'
    decode(dexdir / 'classes.dex', overlay)
    for basename, package in [('oh-adapter-framework', 'window'), ('adapter-runtime-bcp', 'security')]:
        tree = ROOT / (basename + '-smali')
        decode(ROOT / 'original-fw' / (basename + '.jar'), tree)
        for p in (overlay / 'adapter' / package).glob('*.smali'):
            shutil.copy2(p, tree / 'adapter' / package / p.name)
        assemble(tree, fw / (basename + '.jar'))
    tree = ROOT / 'bc-smali'
    decode(ROOT / 'original-fw/bouncycastle.jar', tree)
    factory = tree / 'com/android/org/bouncycastle/crypto/digests/AndroidDigestFactory.smali'
    source = factory.read_text()
    needle = '    :cond_31\n    new-instance v0, Ljava/lang/AssertionError;'
    assert source.count(needle) == 1, 'Unrecognized Bouncy Castle capability guard'
    replacement = '''    :cond_31
    # OHOS platform backend selection, not a certificate-validation bypass.
    const-string v0, "WESTLAKE_USE_BC_DIGESTS"
    invoke-static {v0}, Ljava/lang/System;->getenv(Ljava/lang/String;)Ljava/lang/String;
    move-result-object v0
    const-string v1, "1"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v0
    if-eqz v0, :oh_conscrypt_required
    const/4 v0, 0x0
    sput-object v0, Lcom/android/org/bouncycastle/crypto/digests/AndroidDigestFactory;->CONSCRYPT:Lcom/android/org/bouncycastle/crypto/digests/AndroidDigestFactoryInterface;
    return-void
    :oh_conscrypt_required
    new-instance v0, Ljava/lang/AssertionError;'''
    factory.write_text(source.replace(needle, replacement))
    assemble(tree, fw / 'bouncycastle.jar')
    tree = ROOT / 'okhttp-smali'
    decode(ROOT / 'original-fw/okhttp.jar', tree)
    platform = tree / 'com/android/okhttp/internal/Platform.smali'
    source = platform.read_text()
    needle = '''    .catch Ljava/lang/UnsupportedOperationException; {:try_start_0 .. :try_end_4} :catch_5

    return-object v0'''
    assert source.count(needle) == 1, 'Unrecognized ALPN adapter'
    platform.write_text(source.replace(needle, '''    .catch Ljava/lang/UnsupportedOperationException; {:try_start_0 .. :try_end_4} :catch_5

    # SSLSocket uses an empty string for no ALPN. OkHttp uses null to
    # select HTTP/1.1. Do not invent a negotiated protocol or alter TLS.
    if-eqz v0, :oh_alpn_done
    invoke-virtual {v0}, Ljava/lang/String;->isEmpty()Z
    move-result v1
    if-eqz v1, :oh_alpn_done
    const/4 v0, 0x0
    :oh_alpn_done
    return-object v0'''))
    assemble(tree, fw / 'okhttp.jar')
    tree = ROOT / 'core-libart-smali'
    decode(ROOT / 'original-fw/core-libart.jar', tree)
    constants = tree / 'android/system/OsConstants.smali'
    source = constants.read_text()
    needle = '''    invoke-static {}, Landroid/system/OsConstants;->placeholder()I

    move-result v0

    sput v0, Landroid/system/OsConstants;->IP_MULTICAST_ALL:I'''
    assert source.count(needle) == 1, 'Unrecognized platform socket constant'
    constants.write_text(source.replace(needle, '''    # Linux/OHOS UAPI IP_MULTICAST_ALL, from the platform SDK netinet/in.h.
    # The deployed partial native initializer omits this field.
    const/16 v0, 0x31
    sput v0, Landroid/system/OsConstants;->IP_MULTICAST_ALL:I'''))
    assemble(tree, fw / 'core-libart.jar')
    print('Built isolated Java inputs:', fw)

if __name__ == '__main__':
    main()
