#!/usr/bin/env python3
"""Generate source Binder service and statsd native AIDL interfaces."""
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
    parser.add_argument('--stats-build', type=Path)
    parser.add_argument('--host-library-build', type=Path, action='append', default=[])
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    if output.exists():
        parser.error('--out must be new')
    source = workspace / 'android-source'
    compiler = workspace / 'toolchains/android-build-tools35/android-15/aidl'
    binder = source / 'frameworks-native/libs/binder/aidl'
    stats = source / 'modules-statsd/aidl'
    framework = source / 'frameworks-base/core/java'
    record = {'scope': 'source-generated Binder service and statsd AIDL interfaces',
              'compiler_sha256': sha(compiler), 'inputs': {}, 'commands': [],
              'artifacts': {}, 'include_directories': ['include'], 'generated_sources': []}
    (output / 'include').mkdir(parents=True)
    (output / 'src').mkdir()
    env = dict(os.environ, LC_ALL='C', TZ='UTC')
    schemas = [('cpp', p) for p in sorted((binder / 'android/os').glob('*.aidl'))]
    schemas += [('ndk', p) for p in sorted(stats.rglob('*.aidl'))
                if p.name != 'IStatsManagerService.aidl']
    # ParcelFileDescriptor is an AIDL built-in type in the native backends.
    for language, schema in schemas:
        record['inputs'][str(schema.relative_to(workspace))] = sha(schema)
        command = [str(compiler), '--lang=' + language, '--structured', '--min_sdk_version=35',
                   '-I' + str(binder), '-I' + str(stats), '-I' + str(framework),
                   '-h' + str(output / 'include'), '-o' + str(output / 'src'), str(schema)]
        record['commands'].append(command)
        with (output / ('command-' + str(len(record['commands'])) + '.log')).open('x') as log:
            subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    if args.stats_build:
        libraries = []
        generator = None
        record['prerequisites'] = {}
        for directory in [args.stats_build, *args.host_library_build]:
            directory = directory.resolve()
            report = json.loads((directory / 'artifacts.json').read_text())
            if report.get('errors'):
                raise ValueError('Failed generator prerequisite: ' + str(directory))
            record['prerequisites'][str(directory)] = sha(directory / 'artifacts.json')
            for name, entry in report['artifacts'].items():
                path = directory / entry.get('path', name)
                if not path.resolve().is_relative_to(directory) or sha(path) != entry['sha256']:
                    raise ValueError('Changed generator prerequisite: ' + str(path))
                if name == 'stats-log-api-gen':
                    generator = path
                if path.suffix == '.so':
                    libraries.append(str(path.parent))
        if generator is None:
            raise ValueError('Missing source stats-log-api-gen')
        env['LD_LIBRARY_PATH'] = ':'.join(sorted(set(libraries)))
        for mode, name in [('header', 'include/stats_statsdsocketlog.h'),
                           ('cpp', 'src/stats_statsdsocketlog.cpp')]:
            command = [str(generator), '--' + mode, str(output / name), '--module', 'statsdsocket',
                       '--namespace', 'android,os,statsdsocket']
            if mode == 'cpp':
                command += ['--importHeader', 'stats_statsdsocketlog.h']
            record['commands'].append(command)
            subprocess.run(command, env=env, check=True)
    for path in sorted(output.rglob('*')):
        if path.is_file() and path.suffix in ['.h', '.cpp']:
            name = str(path.relative_to(output))
            record['artifacts'][name] = {'sha256': sha(path), 'bytes': path.stat().st_size}
            if path.suffix == '.cpp':
                record['generated_sources'].append(name)
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('BINDER_STATS_AIDL_GENERATED', len(record['artifacts']))


if __name__ == '__main__':
    main()
