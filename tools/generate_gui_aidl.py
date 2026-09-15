#!/usr/bin/env python3
"""Generate Android GUI parcelables required by the source graphics JNI."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from build_hwui_objects import sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    if output.exists():
        parser.error('--out must be new')
    compiler = workspace / 'toolchains/android-build-tools35/android-15/aidl'
    sources = workspace / 'android-source/frameworks-native/libs/gui/aidl'
    schema = sources / 'android/gui/OverlayProperties.aidl'
    (output / 'include').mkdir(parents=True)
    (output / 'src').mkdir()
    command = [str(compiler), '--lang=cpp', '--structured', '--min_sdk_version=35',
               '-I' + str(sources), '-h' + str(output / 'include'),
               '-o' + str(output / 'src'), str(schema)]
    record = {'scope': 'Android GUI parcelables generated from original AIDL',
              'compiler_sha256': sha(compiler), 'inputs': {
                  str(schema.relative_to(workspace)): sha(schema)},
              'commands': [command], 'artifacts': {}, 'include_directories': ['include'],
              'generated_sources': []}
    with (output / 'generate.log').open('x') as log:
        subprocess.run(command, env=dict(os.environ, LC_ALL='C', TZ='UTC'),
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    for path in sorted(output.rglob('*')):
        if path.suffix in ('.h', '.cpp'):
            name = str(path.relative_to(output))
            record['artifacts'][name] = {'sha256': sha(path), 'bytes': path.stat().st_size}
            if path.suffix == '.cpp':
                record['generated_sources'].append(name)
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('GUI_AIDL_GENERATED', len(record['artifacts']))


if __name__ == '__main__':
    main()
