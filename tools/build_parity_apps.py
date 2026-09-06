#!/usr/bin/env python3
"""Build deterministic unsigned parity APKs from the checked-in control sources."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def build(sdk, output, variant):
    sdk = sdk.resolve()
    output = output.absolute()
    if output.exists():
        raise ValueError('Output must be a new directory')
    source = ROOT / 'tests/parity/apps' / variant
    api = sdk / 'platforms/android-34/android.jar'
    bt = sdk / 'build-tools/34.0.0'
    required = [api, bt / 'aapt2', bt / 'd8', bt / 'lib/d8.jar']
    locked = json.loads((ROOT / 'tests/parity/android-sdk.lock.json').read_text())['files']
    for path in required:
        if not path.is_file():
            raise ValueError('Missing Android SDK input: ' + str(path))
        if sha(path) != locked[str(path.relative_to(sdk))]['sha256']:
            raise ValueError('Android SDK input differs from content lock: ' + str(path))
    output.mkdir(parents=True)
    classes = output / 'classes'
    classes.mkdir()
    env = dict(os.environ, SOURCE_DATE_EPOCH='1772755200', TZ='UTC', LC_ALL='C.UTF-8')
    commands = []
    def run(*args):
        cmd = [str(x) for x in args]
        commands.append(cmd)
        subprocess.run(cmd, check=True, env=env)
    run('javac', '--release', '8', '-parameters', '-encoding', 'UTF-8',
        '-classpath', api, '-d', classes, *sorted(source.glob('*.java')))
    run(bt / 'd8', '--min-api', '28', '--lib', api, '--output', output,
        *sorted(classes.rglob('*.class')))
    run(bt / 'aapt2', 'link', '-I', api, '--manifest', source / 'AndroidManifest.xml',
        '--min-sdk-version', '28', '--target-sdk-version', '28', '-o', output / 'resources.apk')
    with zipfile.ZipFile(output / 'resources.apk') as resource:
        entries = {name: resource.read(name) for name in resource.namelist()}
    entries['classes.dex'] = (output / 'classes.dex').read_bytes()
    for cert in sorted((ROOT / 'tests/parity/cert-fixtures').glob('*.pem')):
        data = cert.read_bytes()
        if b'PRIVATE KEY' in data:
            raise ValueError('Private keys cannot be APK fixtures')
        entries['assets/certs/' + cert.name] = data
    unsigned = output / 'control-unsigned.apk'
    with zipfile.ZipFile(unsigned, 'w', compression=zipfile.ZIP_STORED) as archive:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    report = dict(variant=variant, signed=False, commands=commands,
                  javac=subprocess.check_output(['javac', '-version'], stderr=subprocess.STDOUT, text=True).strip(),
                  sdk_inputs={str(p.relative_to(sdk)): sha(p) for p in required},
                  sources={str(p.relative_to(ROOT)): sha(p) for p in sorted(source.iterdir()) if p.is_file()},
                  artifacts={unsigned.name: dict(sha256=sha(unsigned), bytes=unsigned.stat().st_size),
                             'classes.dex': dict(sha256=sha(output / 'classes.dex'), bytes=(output / 'classes.dex').stat().st_size)})
    (output / 'artifacts.json').write_text(json.dumps(report, indent=2) + '\n')
    print('UNSIGNED_PARITY_APK_BUILT', variant, sha(unsigned))
    return report

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--variant', choices=['control22', 'control24', 'control26'], default='control24')
    args = parser.parse_args()
    build(args.sdk, args.out, args.variant)
