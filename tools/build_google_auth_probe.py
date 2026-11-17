#!/usr/bin/env python3
"""Build the official GoogleAuthUtil SDK probe from hash-pinned Maven artifacts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(args):
    out = args.out.absolute()
    out.mkdir(parents=True, exist_ok=False)
    source = ROOT / 'tests/google-auth'
    lock = json.loads((source / 'sdk-dependencies.json').read_text())
    api = args.sdk / 'platforms/android-34/android.jar'
    bt = args.sdk / 'build-tools/34.0.0'
    env = dict(os.environ, JAVA_HOME=str(args.jdk), PATH=str(args.jdk / 'bin') + os.pathsep + os.environ['PATH'])

    def run(*command):
        subprocess.run(list(map(str, command)), env=env, check=True)

    jars, resources, namespaces = [], [], []
    for artifact in lock['artifacts']:
        path = args.dependencies / artifact['file']
        if sha(path) != artifact['sha256']:
            raise ValueError('SDK artifact hash mismatch: ' + path.name)
        if artifact['type'] == 'jar':
            jars.append(path)
            continue
        dest = out / 'libraries' / (artifact['artifact'] + '-' + artifact['version'])
        dest.mkdir(parents=True)
        with zipfile.ZipFile(path) as archive:
            for name in archive.namelist():
                if name in ['classes.jar', 'AndroidManifest.xml'] or name.startswith('res/'):
                    if '..' in Path(name).parts or Path(name).is_absolute():
                        raise ValueError('Unsafe SDK archive member')
                    archive.extract(name, dest)
        jars.append(dest / 'classes.jar')
        namespaces.append(ET.parse(dest / 'AndroidManifest.xml').getroot().attrib['package'])
        if (dest / 'res').is_dir():
            compiled = dest / 'resources.zip'
            run(bt / 'aapt2', 'compile', '--dir', dest / 'res', '-o', compiled)
            resources += ['-R', compiled]
    generated = out / 'generated'
    generated.mkdir()
    unsigned = out / 'unsigned.apk'
    run(bt / 'aapt2', 'link', '-I', api, '--manifest', source / 'AndroidManifest.xml',
        '--auto-add-overlay', '--java', generated, '--extra-packages', ':'.join(sorted(set(namespaces))),
        '-o', unsigned, *resources)
    classes = out / 'classes'
    classes.mkdir()
    sources = sorted(source.glob('*.java'))
    run(args.jdk / 'bin/javac', '--release', '8', '-encoding', 'UTF-8',
        '-classpath', os.pathsep.join(map(str, [api, *jars])), '-d', classes,
        *sources, *sorted(generated.rglob('*.java')))
    run(sys.executable, ROOT / 'tools/strip_method_parameters.py', classes)
    dex = out / 'dex'
    dex.mkdir()
    run(bt / 'd8', '--min-api', '28', '--lib', api, '--output', dex,
        *sorted(classes.rglob('*.class')), *jars)
    with zipfile.ZipFile(unsigned, 'a') as archive:
        for path in sorted(dex.glob('classes*.dex')):
            archive.write(path, path.name)
    run(bt / 'zipalign', '-p', '4', unsigned, out / 'aligned.apk')
    apk = out / 'google-auth-probe.apk'
    # This accepts only the throwaway development key created by the bootstrap probe builder.
    run(bt / 'apksigner', 'sign', '--ks', args.signing_key, '--ks-pass', 'pass:android',
        '--out', apk, out / 'aligned.apk')
    run(bt / 'apksigner', 'verify', apk)
    report = {'schema': 'westlake-official-auth-build-v1', 'device_tested': False,
              'apk_sha256': sha(apk), 'sdk_root': lock['root'],
              'sdk_artifact_count': len(lock['artifacts']),
              'sources': {str(p.relative_to(ROOT)): sha(p) for p in sources +
                          [source / 'AndroidManifest.xml', source / 'sdk-dependencies.json']}}
    (out / 'artifacts.json').write_text(json.dumps(report, indent=2) + '\n')
    print('GOOGLE_AUTH_PROBE_BUILT', apk)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['sdk', 'jdk', 'dependencies', 'signing-key', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    for name in ['sdk', 'jdk', 'dependencies', 'signing_key']:
        setattr(args, name, getattr(args, name).resolve())
    build(args)
