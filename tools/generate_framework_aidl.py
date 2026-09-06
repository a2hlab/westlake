#!/usr/bin/env python3
"""Generate the framework's declared AIDL sources and versioned interface dependencies."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import subprocess
from aosp_blueprint import index, Sources, evaluate


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--aidl', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--java-module', action='append', default=[])
    parser.add_argument('--aidl-interface', action='append', default=[])
    parser.add_argument('--profile', type=Path)
    args = parser.parse_args()
    if args.profile:
        profile = json.loads(args.profile.read_text())
        if profile['schema'] != 1:
            parser.error('Unsupported Java profile schema')
        args.java_module = list(dict.fromkeys(profile['java_modules'] + args.java_module))
        args.aidl_interface = list(dict.fromkeys(profile['aidl_interfaces'] + args.aidl_interface))
    workspace, output = args.workspace.resolve(), args.out.absolute()
    root = workspace / 'android-source'
    if output.exists():
        parser.error('--out must be new')
    modules, failures = index(root)
    if failures:
        raise ValueError('Blueprint parse failures: ' + str(failures))
    groups = Sources(root, modules)
    selected = set()
    generated = {}
    seen = set()

    def collect(name):
        if name in seen:
            return
        seen.add(name)
        if name not in modules:
            generated[name] = {'kind': 'synthetic'}
            return
        module = groups.module(name)
        fields = module['fields']
        if module['kind'] == 'platform_filegroup':
            # This stack includes crash recovery in the platform framework.
            patterns = fields['soong_config_variables']['crashrecovery_files_in_platform']['srcs']
        elif module['kind'] in {'filegroup', 'java_library', 'java_library_static', 'java_sdk_library', 'java_defaults'}:
            patterns = evaluate(fields.get('srcs', []), module['environment'])
            for dependency in fields.get('defaults', []):
                collect(dependency)
            for key in ['static_libs', 'impl_only_static_libs']:
                for dependency in evaluate(fields.get(key, []), module['environment']):
                    collect(dependency)
            android = fields.get('target', {}).get('android', {})
            patterns += evaluate(android.get('srcs', []), module['environment'])
            for dependency in evaluate(android.get('static_libs', []), module['environment']):
                collect(dependency)
        else:
            generated[name] = module
            return
        base = root / Path(module['file']).parent
        own = set()
        for pattern in patterns:
            if pattern.startswith(':'):
                collect(pattern[1:])
            else:
                own.update(p for p in base.glob(pattern) if p.is_file())
        for pattern in evaluate(fields.get('exclude_srcs', []), module['environment']):
            own.difference_update(base.glob(pattern))
        selected.update(own)

    collect('framework-non-updatable-sources')
    for name in args.java_module:
        collect(name)
    versions = {}
    interfaces = {}
    pending = [n.removesuffix('-java-source') for n in generated if n.endswith('-java-source')]
    for name in generated:
        if not name.endswith('-java'):
            continue
        interface = name.removesuffix('-java')
        base = re.sub(r'-V[0-9]+$', '', interface)
        choices = modules.get(base, [])
        if len(choices) == 1 and choices[0]['kind'] == 'aidl_interface':
            pending.append(interface)
    pending += args.aidl_interface
    while pending:
        request = pending.pop(0)
        match = re.fullmatch(r'(.+)-V([0-9]+)', request)
        name, version = (match[1], int(match[2])) if match else (request, 0)
        module = groups.module(name)
        if module['kind'] != 'aidl_interface':
            raise ValueError('Expected AIDL interface: ' + name)
        fields = module['fields']
        base = root / Path(module['file']).parent
        if not version and not fields.get('unstable'):
            # AIDL's unversioned import aliases its next source version.
            version = max([int(v['version']) for v in fields.get('versions_with_info', [])] +
                          [int(v) for v in fields.get('versions', [])] + [0]) + 1
        if name in versions and versions[name] >= version:
            continue
        frozen = next((v for v in fields.get('versions_with_info', []) if int(v['version']) == version), None)
        legacy_frozen = str(version) in fields.get('versions', [])
        if frozen or legacy_frozen:
            directory = base / 'aidl_api' / name / str(version)
            files = sorted(directory.rglob('*.aidl'))
            if not files or not (directory / '.hash').is_file():
                raise ValueError('Missing frozen AIDL source/hash: ' + request)
            content_hash = (directory / '.hash').read_text().splitlines()[-1]
            dependencies = frozen['imports'] if frozen else fields.get('imports', [])
        else:
            maximum = max([int(v['version']) for v in fields.get('versions_with_info', [])] +
                          [int(v) for v in fields.get('versions', [])] + [0])
            if version and version != maximum + 1:
                raise ValueError('Requested interface version does not exist: ' + request)
            files = groups.files(name)
            directory = base / fields.get('local_include_dir', '.')
            content_hash = 'notfrozen'
            dependencies = fields.get('imports', [])
        versions[name] = version
        interfaces[name] = {'files': files, 'root': directory, 'version': version,
                            'hash': content_hash, 'stability': fields.get('stability'),
                            'imports': dependencies}
        pending += dependencies
    # Stable AIDL guarantees that a newer version preserves earlier declarations.
    # A Java classpath owns one definition per interface; select the highest
    # explicitly requested version and record the complete import graph.
    units = {p: {} for p in selected if p.suffix == '.aidl'}
    for spec in interfaces.values():
        for path in spec['files']:
            units[path] = spec
    include_roots = {spec['root'] for spec in interfaces.values()}
    aidl_index = {}
    package_re = re.compile(r'^\s*package\s+([\w.]+)\s*;', re.M)
    for path in sorted(root.glob('*/**/*.aidl')):
        if any(part in path.parts for part in ['aidl_api', 'test', 'tests', 'testdata', 'test_data', 'samples', 'examples']):
            continue
        match = package_re.search(path.read_text())
        if not match:
            continue
        suffix = Path(*match[1].split('.')) / path.name
        if not str(path).endswith('/' + suffix.as_posix()):
            continue
        declared = {path.stem} | set(re.findall(r'\bparcelable\s+([\w.]+)', path.read_text()))
        for name in declared:
            aidl_index.setdefault(match[1] + '.' + name, []).append((path, path.parents[len(suffix.parts)-1]))
    for path in units:
        match = package_re.search(path.read_text())
        if match:
            suffix = Path(*match[1].split('.')) / path.name
            if str(path).endswith('/' + suffix.as_posix()):
                include_roots.add(path.parents[len(suffix.parts)-1])
    # Declarations exported by updatable modules are legitimate compiler inputs;
    # their implementations are compiled in their own framework Java libraries.
    for candidates in aidl_index.values():
        for path, directory in candidates:
            if 'aidl-export' in path.parts:
                include_roots.add(directory)
    all_imports = set()
    todo = list(units)
    while todo:
        path = todo.pop()
        if path in all_imports:
            continue
        all_imports.add(path)
        for name in re.findall(r'^\s*import\s+([\w.]+)\s*;', path.read_text(), re.M):
            if name in {'java.util.List', 'java.util.Map', 'android.os.ParcelFileDescriptor',
                            'android.os.IInterface', 'android.os.IBinder', 'android.os.Parcelable',
                            'android.os.Parcel', 'android.content.Context', 'java.lang.String',
                            'java.lang.CharSequence'}:
                continue
            parts = name.split('.')
            existing = []
            choices = []
            while parts:
                relative = Path(*parts).with_suffix('.aidl')
                existing = [p / relative for p in include_roots if (p / relative).is_file()]
                choices = aidl_index.get('.'.join(parts), [])
                if existing or choices:
                    break
                parts.pop()
            if not existing:
                # The Android tree sometimes repeats an identical parcelable
                # declaration in exports. Different declarations require review.
                if len({sha(p) for p, _ in choices}) != 1:
                    raise ValueError('Missing/ambiguous AIDL import: ' + name + ': ' + str([str(p) for p, _ in choices]))
                path_choice, directory = choices[0]
                include_roots.add(directory)
                existing = [path_choice]
            todo.extend(existing)
    output.mkdir(parents=True)
    (output / 'java').mkdir()
    (output / 'logs').mkdir()
    # SDK-style preprocessed parcelable declarations also resolve the legacy
    # short type names used by framework module interfaces without imports.
    declarations = []
    for path in sorted(units):
        text = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
        if 'parcelable ' in text and '{' not in text:
            declarations.append(path)
    preprocessed = output / 'framework.aidl'
    preprocess_command = [str(args.aidl.resolve()), *['-I' + str(p) for p in sorted(include_roots)],
                          '--preprocess', str(preprocessed), *map(str, declarations)]
    with (output / 'preprocess.log').open('x') as log:
        subprocess.run(preprocess_command, stdout=log, stderr=subprocess.STDOUT, check=True)
    # Android 15 preprocess.cpp's Visit(AidlParcelable) writes these already
    # unquoted AST strings verbatim. Restore the string literals required by
    # the AIDL grammar, without changing any type or header value.
    raw_preprocessed = preprocessed.read_text()
    (output / 'framework.aidl.raw').write_text(raw_preprocessed)
    normalized = re.sub(r'\b(cpp_header|ndk_header|rust_type) ([A-Za-z0-9_./:+-]+)',
                        lambda m: m[1] + ' ' + json.dumps(m[2]), raw_preprocessed)
    preprocessed.write_text(normalized)
    commands = []
    for i, (path, spec) in enumerate(sorted(units.items())):
        command = [str(args.aidl.resolve()), '--lang=java', '--min_sdk_version=35', '--transaction_names',
                   '-o' + str(output / 'java')]
        if not spec:
            command += ['-p' + str(preprocessed)]
        if spec:
            command += ['--structured']
            if spec['version']:
                command += ['--version=' + str(spec['version']), '--hash=' + spec['hash']]
            if spec['stability']:
                command += ['--stability=' + spec['stability']]
        command += ['-I' + str(p) for p in sorted(include_roots)] + [str(path)]
        commands.append((command, output / 'logs' / (str(i) + '.log')))

    def run(item):
        command, log = item
        with log.open('x') as stream:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
        return (command[-1], result.returncode)

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(run, commands))
    errors = [name for name, code in results if code]
    record = {'compiler_sha256': sha(args.aidl), 'sources': [str(p.relative_to(root)) for p in sorted(selected)],
              'inputs': {str(p.relative_to(root)): sha(p) for p in sorted(all_imports)},
              'definitions': {p: sha(root / p) for p in sorted(groups.definitions)},
              'interfaces': {n: {k: ([str(p.relative_to(root)) for p in v] if k == 'files' else
                                   str(v.relative_to(root)) if k == 'root' else v)
                                 for k, v in s.items()} for n, s in sorted(interfaces.items())},
              'generated': {p.relative_to(output).as_posix(): sha(p) for p in sorted((output / 'java').rglob('*.java'))},
              'commands': [c for c, _ in commands], 'errors': errors,
              'remaining_generators': {n: v for n, v in generated.items() if not n.endswith('-java-source')},
              'preprocess_command': preprocess_command, 'framework_aidl_sha256': sha(preprocessed),
              'framework_aidl_raw_sha256': sha(output / 'framework.aidl.raw'),
              'java_modules': args.java_module, 'aidl_interfaces': args.aidl_interface,
              'collected_modules': sorted(seen),
              'declaration_only_outputs': [str(p.relative_to(output)) for p in sorted((output / 'java').rglob('*.java'))
                                           if not p.stat().st_size],
              'aidl_use_unfrozen': True, 'full_framework_accepted': False}
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('FRAMEWORK_AIDL', len(units), 'generated', len(record['generated']), 'errors', len(errors))
    raise SystemExit(bool(errors))


if __name__ == '__main__':
    main()
