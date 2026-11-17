#!/usr/bin/env python3
"""Build the local-account periodic sync fixture, without Google SDK dependencies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ['sdk', 'jdk', 'signing-key', 'out']:
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    out = args.out.absolute()
    if out.exists():
        parser.error('--out must be new')
    out.mkdir(parents=True)
    src = ROOT / 'tests/periodic-sync'
    tools = args.sdk / 'build-tools/34.0.0'
    api = args.sdk / 'platforms/android-34/android.jar'
    env = dict(os.environ, JAVA_HOME=str(args.jdk), PATH=str(args.jdk / 'bin') + os.pathsep + os.environ['PATH'])
    def run(*command):
        subprocess.run(list(map(str, command)), env=env, check=True)
    run(tools / 'aapt2', 'compile', '--dir', src / 'res', '-o', out / 'res.zip')
    generated = out / 'generated'
    generated.mkdir()
    run(tools / 'aapt2', 'link', '-I', api, '--manifest', src / 'AndroidManifest.xml',
        '--java', generated, '-R', out / 'res.zip', '-o', out / 'unsigned.apk')
    classes = out / 'classes'
    classes.mkdir()
    run(args.jdk / 'bin/javac', '--release', '8', '-encoding', 'UTF-8', '-classpath', api,
        '-d', classes, *sorted(src.glob('*.java')), *sorted(generated.rglob('*.java')))
    run(sys.executable, ROOT / 'tools/strip_method_parameters.py', classes)
    dex = out / 'dex'
    dex.mkdir()
    run(tools / 'd8', '--min-api', '22', '--lib', api, '--output', dex, *sorted(classes.rglob('*.class')))
    with zipfile.ZipFile(out / 'unsigned.apk', 'a') as archive:
        for path in dex.glob('*.dex'):
            archive.write(path, path.name)
    run(tools / 'zipalign', '-p', '4', out / 'unsigned.apk', out / 'aligned.apk')
    run(tools / 'apksigner', 'sign', '--ks', args.signing_key, '--ks-pass', 'pass:android',
        '--out', out / 'periodic-sync-probe.apk', out / 'aligned.apk')
    run(tools / 'apksigner', 'verify', out / 'periodic-sync-probe.apk')
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    report = {'schema': 1, 'apkSha256': sha(out / 'periodic-sync-probe.apk'),
              'sourceSha256': {str(p.relative_to(ROOT)): sha(p) for p in sorted(src.rglob('*')) if p.is_file()},
              'deviceTested': False, 'googleAccountOrNetworkRequired': False}
    (out / 'artifacts.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PERIODIC_SYNC_PROBE_BUILT', out)

if __name__ == '__main__':
    main()
