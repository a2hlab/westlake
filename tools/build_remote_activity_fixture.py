#!/usr/bin/env python3
"""Build two source-only activity contract fixtures; never connect them to a real GMS endpoint."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def build(args):
    out = args.out.absolute()
    out.mkdir(parents=True, exist_ok=False)
    source = ROOT / 'tests/remote-activity'
    api = args.sdk / 'platforms/android-34/android.jar'
    bt = args.sdk / 'build-tools/34.0.0'
    env = dict(os.environ, JAVA_HOME=str(args.jdk), PATH=str(args.jdk / 'bin') + os.pathsep + os.environ['PATH'])
    def run(*command):
        subprocess.run(list(map(str, command)), env=env, check=True)
    classes = out / 'classes'
    classes.mkdir()
    run(args.jdk / 'bin/javac', '--release', '8', '-classpath', api, '-d', classes, source / 'Fixture.java')
    run(sys.executable, ROOT / 'tools/strip_method_parameters.py', classes)
    run(bt / 'd8', '--min-api', '28', '--lib', api, '--output', out, *sorted(classes.rglob('*.class')))
    hashes = {}
    for role in ['host', 'client']:
        unsigned = out / (role + '-unsigned.apk')
        run(bt / 'aapt2', 'link', '-I', api, '--manifest', source / (role + '.xml'), '-o', unsigned)
        with zipfile.ZipFile(unsigned, 'a') as archive:
            archive.write(out / 'classes.dex', 'classes.dex')
        aligned = out / (role + '-aligned.apk')
        run(bt / 'zipalign', '-p', '4', unsigned, aligned)
        apk = out / (role + '.apk')
        run(bt / 'apksigner', 'sign', '--ks', args.signing_key, '--ks-pass', 'pass:android', '--out', apk, aligned)
        run(bt / 'apksigner', 'verify', apk)
        hashes[role] = hashlib.sha256(apk.read_bytes()).hexdigest()
    (out / 'artifacts.json').write_text(json.dumps({'schema': 'westlake-remote-activity-fixture-v1',
        'fixture_only': True, 'device_tested': False, 'apk_sha256': hashes,
        'sources': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in sorted(source.glob('*')) if p.is_file()}}, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ['sdk', 'jdk', 'signing-key', 'out']:
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    for key in ['sdk', 'jdk', 'signing_key']:
        setattr(args, key, getattr(args, key).resolve())
    build(args)
