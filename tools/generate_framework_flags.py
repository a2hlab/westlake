#!/usr/bin/env python3
"""Generate framework flags with the pinned Android 15 AP3A release values."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from aosp_blueprint import Sources, index


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--compiler-build', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--group', action='append')
    parser.add_argument('--library', action='append', default=[])
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    root = workspace / 'android-source'
    compiler = args.compiler_build.resolve() / 'aconfig'
    report = json.loads((compiler.parent / 'artifacts.json').read_text())
    if sha(compiler) != report['artifacts']['aconfig']['sha256']:
        raise ValueError('Flag compiler differs from source build report')
    if output.exists():
        parser.error('--out must be new')
    modules, errors = index(root)
    if errors:
        raise ValueError('Blueprint parse failures: ' + str(errors))
    groups = Sources(root, modules)
    libraries = set(args.library)
    seen = set()

    def visit(name):
        if name in seen:
            return
        seen.add(name)
        fields = groups.module(name)['fields']
        libraries.update(fields.get('java_aconfig_libraries', []))
        for dependency in fields.get('aconfig_declarations_groups', []):
            visit(dependency)

    for name in args.group or ['framework-minus-apex-aconfig-declarations']:
        visit(name)
    declarations = {groups.module(name)['fields']['aconfig_declarations'] for name in libraries}
    values = {}
    for name in ['aconfig_value_set-platform_build_release-root', 'aconfig_value_set-platform_build_release-ap3a']:
        for dependency in groups.module(name)['fields']['values']:
            fields = groups.module(dependency)['fields']
            values.setdefault(fields['package'], []).extend(groups.files(dependency))
    output.mkdir(parents=True)
    (output / 'logs').mkdir()
    (output / 'caches').mkdir()
    (output / 'java').mkdir()
    record = {'compiler_sha256': sha(compiler), 'release': 'Android 15 AP3A', 'default_permission': 'read_only',
              'declarations': {}, 'inputs': {}, 'definitions': {}, 'commands': [], 'flags': {},
              'artifacts': {}, 'full_framework_accepted': False}

    def run(command, log):
        command = list(map(str, command))
        record['commands'].append(command)
        with log.open('x') as stream:
            subprocess.run(command, check=True, stdout=stream, stderr=subprocess.STDOUT)

    packages = set()
    for name in sorted(declarations):
        fields = groups.module(name)['fields']
        package = fields['package']
        if package in packages:
            raise ValueError('Two flag declarations own package ' + package)
        packages.add(package)
        files = groups.files(name)
        cache = output / 'caches' / (package + '.pb')
        command = [compiler, 'create-cache', '--package', package, '--container', fields['container'],
                   '--default-permission', 'read_only', '--cache', cache]
        for path in files:
            command += ['--declarations', path]
        for path in values.get(package, []):
            command += ['--values', path]
        for path in files + values.get(package, []):
            record['inputs'][str(path.relative_to(root))] = sha(path)
        record['declarations'][name] = {'package': package, 'container': fields['container']}
        run(command, output / 'logs' / (package + '-cache.log'))
        run([compiler, 'create-java-lib', '--cache', cache, '--out', output / 'java', '--mode', 'production'],
            output / 'logs' / (package + '-java.log'))
        dump = output / 'logs' / (package + '-values.txt')
        run([compiler, 'dump-cache', '--cache', cache, '--format', '{fully_qualified_name}={state}'], dump)
        for line in dump.read_text().splitlines():
            key, state = line.split('=', 1)
            if state not in {'ENABLED', 'DISABLED'} or key in record['flags']:
                raise ValueError('Unexpected or duplicate generated flag: ' + line)
            record['flags'][key] = state == 'ENABLED'
    record['definitions'] = {p: sha(root / p) for p in sorted(groups.definitions)}
    record['artifacts'] = {str(p.relative_to(output)): {'sha256': sha(p), 'bytes': p.stat().st_size}
                           for directory in ['java', 'caches'] for p in sorted((output / directory).rglob('*')) if p.is_file()}
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('FRAMEWORK_FLAGS_PASS', len(packages), 'packages', len(record['flags']), 'flags')


if __name__ == '__main__':
    main()
