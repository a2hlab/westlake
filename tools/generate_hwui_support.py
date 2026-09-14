#!/usr/bin/env python3
"""Generate Android 15 HWUI property, graphics type and telemetry declarations."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from build_hwui_objects import sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'sysprop-build', 'hidl-build', 'stats-build', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--host-library-build', type=Path, action='append', required=True)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    source = workspace / 'android-source'
    if output.exists():
        parser.error('--out must be new')
    tools = {}
    record = {'prerequisites': {}, 'inputs': {}, 'commands': [], 'artifacts': {},
              'include_directories': ['include'], 'generated_sources': [],
              'scope': 'source-generated HWUI native declarations and property accessors'}
    paths = []
    for directory in [args.sysprop_build, args.hidl_build, args.stats_build, *args.host_library_build]:
        directory = directory.resolve()
        report = json.loads((directory / 'artifacts.json').read_text())
        if report.get('errors'):
            raise ValueError('Failed generator prerequisite: ' + str(directory))
        for name, entry in report['artifacts'].items():
            path = directory / entry.get('path', name)
            if not path.resolve().is_relative_to(directory) or sha(path) != entry['sha256']:
                raise ValueError('Changed generator prerequisite: ' + str(path))
            if name in ['sysprop_cpp', 'hidl-gen', 'stats-log-api-gen']:
                tools[name] = path
            if path.suffix == '.so':
                paths.append(str(path.parent))
        record['prerequisites'][str(directory)] = sha(directory / 'artifacts.json')
    env = dict(os.environ, LC_ALL='C', TZ='UTC', LD_LIBRARY_PATH=':'.join(sorted(set(paths))))
    for name in ['include', 'public', 'src']:
        (output / name).mkdir(parents=True)

    def run(command):
        command = list(map(str, command))
        record['commands'].append(command)
        with (output / ('command-' + str(len(record['commands'])) + '.log')).open('x') as log:
            subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)

    schema = source / 'frameworks-base/libs/hwui/HWUIProperties.sysprop'
    record['inputs'][str(schema.relative_to(workspace))] = sha(schema)
    run([tools['sysprop_cpp'], '--header-dir', output / 'include', '--public-header-dir', output / 'public',
         '--source-dir', output / 'src', '--include-name', 'HWUIProperties.sysprop.h', schema])
    for version in ['1.0', '1.1', '1.2']:
        schema = source / 'hardware-interfaces/graphics/common' / version / 'types.hal'
        record['inputs'][str(schema.relative_to(workspace))] = sha(schema)
        run([tools['hidl-gen'], '-Lc++-headers', '-o', output / 'include',
             '-r', 'android.hardware:' + str(source / 'hardware-interfaces'),
             '-r', 'android.hidl:' + str(source / 'system-libbase-hidl/transport'),
             'android.hardware.graphics.common@' + version])
    aidl = workspace / 'toolchains/android-build-tools35/android-15/aidl'
    record['compiler_inputs'] = {'aidl': sha(aidl)}
    common = source / 'hardware-interfaces/graphics/common/aidl'
    composer = source / 'hardware-interfaces/graphics/composer/aidl'
    hardware_common = source / 'hardware-interfaces/common/aidl'
    selected = sorted((common / 'android/hardware/graphics/common').glob('*.aidl'))
    selected += [composer / 'android/hardware/graphics/composer3/RenderIntent.aidl']
    selected += sorted((hardware_common / 'android/hardware/common').glob('*.aidl'))
    for schema in selected:
        record['inputs'][str(schema.relative_to(workspace))] = sha(schema)
        run([aidl, '--lang=ndk', '--min_sdk_version=35', '--structured', '--stability=vintf',
             '-I' + str(common), '-I' + str(composer), '-I' + str(hardware_common), '-h' + str(output / 'include'),
             '-o' + str(output / 'src'), schema])
    run([tools['stats-log-api-gen'], '--header', output / 'include/statslog_hwui.h',
         '--module', 'hwui', '--namespace', 'android,uirenderer,stats'])
    for path in sorted(output.rglob('*')):
        if path.is_file() and path.suffix in ['.h', '.cpp']:
            name = str(path.relative_to(output))
            record['artifacts'][name] = {'sha256': sha(path), 'bytes': path.stat().st_size}
            if path.name == 'HWUIProperties.sysprop.cpp':
                record['generated_sources'].append(name)
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('HWUI_SUPPORT_GENERATED', len(record['artifacts']), flush=True)


if __name__ == '__main__':
    main()
