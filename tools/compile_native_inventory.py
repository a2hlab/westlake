#!/usr/bin/env python3
"""Compile a checked-in native source inventory with pinned OHOS compiler paths."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
from build_hwui_objects import ROOT, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'inventory', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--header-build', type=Path, action='append', default=[])
    parser.add_argument('--westlake-source', type=Path)
    args = parser.parse_args()
    workspace, output, inventory = args.workspace.resolve(), args.out.absolute(), args.inventory.resolve()
    if output.exists() or args.jobs < 1 or not inventory.is_relative_to(ROOT):
        parser.error('--out must be new, --jobs positive, and inventory part of Westlake')
    spec = json.loads(inventory.read_text())
    westlake = (args.westlake_source or workspace / 'westlake').resolve()
    if len(spec['sources']) != len(set(spec['sources'])):
        raise ValueError('Duplicate translation unit in inventory')

    def source(relative):
        if Path(relative).is_absolute() or '..' in Path(relative).parts:
            raise ValueError('Outside inventory path: ' + relative)
        if relative == 'westlake' or relative.startswith('westlake/'):
            return westlake / Path(relative).relative_to('westlake')
        return workspace / relative

    output.mkdir(parents=True)
    transforms = {}
    normalized = {}
    for relative, files in spec.get('source_tree_replacements', {}).items():
        original = source(relative)
        target = output / 'ported' / relative
        shutil.copytree(original, target, symlinks=True)
        normalized[relative] = target
        for name, replacements in files.items():
            path = target / name
            if not path.resolve().is_relative_to(target.resolve()):
                raise ValueError('Outside source tree port: ' + name)
            text = path.read_text()
            before_hash = sha(path)
            for before, after in replacements.items():
                if before not in text:
                    raise ValueError('Source tree port changed: ' + name)
                text = text.replace(before, after)
            path.write_text(text)
            transforms[relative + '/' + name] = {
                'before': before_hash, 'after': sha(path), 'replacements': replacements}
    for relative in spec.get('normalize_include_directories', []):
        original = source(relative)
        target = output / 'compat' / Path(relative).name
        shutil.copytree(original, target)
        normalized[relative] = target
        for path in sorted(target.rglob('*.h')):
            old = path.read_text()
            new = old
            for before, after in spec.get('include_replacements', {}).items():
                new = new.replace(before, after)
            if new != old:
                transforms[str(path.relative_to(output))] = {'before': sha(original / path.relative_to(target))}
                path.write_text(new)
                transforms[str(path.relative_to(output))]['after'] = sha(path)

    sdk = workspace / 'toolchains/ohos-sdk/native'
    platform = workspace / 'toolchains/clang-15'
    flags = ['--target=aarch64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot'), '-fPIC', '-O2',
             '-Werror=date-time', '-ffile-prefix-map=' + str(workspace) + '=/src',
             '-ffile-prefix-map=' + str(output) + '=/build/native']
    header_builds = {}
    generated_sources = []
    release_flags = {}
    for directory in args.header_build:
        directory = directory.resolve()
        report = json.loads((directory / 'artifacts.json').read_text())
        if report.get('errors'):
            raise ValueError('Failed header prerequisite: ' + str(directory))
        for name, artifact in report['artifacts'].items():
            path = directory / name
            if not path.resolve().is_relative_to(directory) or sha(path) != artifact['sha256']:
                raise ValueError('Generated header input differs: ' + str(path))
        header_builds[str(directory)] = sha(directory / 'artifacts.json')
        for name in report.get('generated_sources', []):
            if any(name.startswith(prefix) for prefix in spec.get('generated_source_prefixes', [])):
                if name not in report['artifacts'] or not name.endswith('.cpp'):
                    raise ValueError('Undeclared generated source: ' + name)
                generated_sources.append(directory / name)
        for package, values in report.get('flags', {}).items():
            if package in release_flags and release_flags[package] != values:
                raise ValueError('Conflicting native release flags: ' + package)
            release_flags[package] = values
        for name in report['include_directories']:
            path = directory / name
            if not path.resolve().is_relative_to(directory) or not path.is_dir():
                raise ValueError('Invalid generated include directory: ' + str(path))
            flags += ['-I' + str(path)]
        flags += ['-ffile-prefix-map=' + str(directory) + '=/build/headers/' + str(len(header_builds))]
    for package, values in spec.get('required_release_flags', {}).items():
        for name, value in values.items():
            if release_flags.get(package, {}).get(name) is not value:
                raise ValueError('Required native release flag differs: ' + package + '.' + name)
    for relative in spec['includes']:
        if not source(relative).is_dir():
            raise ValueError('Missing declared include directory: ' + relative)
        flags += ['-I' + str(normalized.get(relative, source(relative)))]
    flags += ['-D' + d for d in spec.get('defines', [])]
    cpp = ['-std=c++17', '-nostdinc++', '-isystem', str(platform / 'include/libcxx-ohos/include/c++/v1')]
    cpp += spec.get('cpp_flags', [])
    for relative in spec.get('force_includes', []):
        cpp += ['-include', str(source(relative))]
    cpp += ['-include', 'sys/types.h']
    (output / 'objects').mkdir()
    record = {'scope': spec['scope'], 'inventory_sha256': sha(inventory), 'sources': {}, 'headers': {},
              'header_builds': header_builds,
              'westlake_source': str(westlake), 'header_transforms': transforms,
              'commands': [], 'artifacts': {}, 'errors': [], 'compiler_sha256': sha(sdk / 'llvm/bin/clang')}
    jobs = []
    for relative in spec['sources']:
        path = source(relative)
        record['sources'][relative] = sha(path)
        for root, target in normalized.items():
            if Path(relative).is_relative_to(root):
                path = target / Path(relative).relative_to(root)
                break
        if relative in spec.get('source_replacements', {}):
            original = path
            text = path.read_text()
            for before, after in spec['source_replacements'][relative].items():
                if before not in text:
                    raise ValueError('Source port changed: ' + relative)
                text = text.replace(before, after)
            path = output / ('source-' + original.name)
            path.write_text(text)
            record.setdefault('source_transforms', {})[relative] = {
                'before': sha(original), 'after': sha(path),
                'replacements': spec['source_replacements'][relative]}
        obj = output / 'objects' / (relative.replace('/', '_') + '.o')
        compiler = sdk / 'llvm/bin' / ('clang' if path.suffix == '.c' else 'clang++')
        lang = ['-std=gnu11'] if path.suffix == '.c' else cpp
        local_includes = []
        for name in spec.get('includes_by_source', {}).get(relative, []):
            directory = normalized.get(name, source(name))
            if not directory.is_dir():
                raise ValueError('Missing source-specific include directory: ' + name)
            local_includes.append('-I' + str(directory))
        command = [str(compiler), *local_includes, *flags, *lang, '-MMD', '-MF', str(obj) + '.d', '-c', str(path), '-o', str(obj)]
        record['commands'].append(command)
        jobs.append((relative, obj, command))
    for path in generated_sources:
        relative = 'generated/' + path.name
        if relative in record['sources']:
            raise ValueError('Duplicate generated source: ' + relative)
        record['sources'][relative] = sha(path)
        obj = output / 'objects' / (relative.replace('/', '_') + '.o')
        command = [str(sdk / 'llvm/bin/clang++'), *flags, *cpp, '-MMD', '-MF', str(obj) + '.d',
                   '-c', str(path), '-o', str(obj)]
        record['commands'].append(command)
        jobs.append((relative, obj, command))
    env = dict(os.environ, LC_ALL='C', TZ='UTC', SOURCE_DATE_EPOCH='1772755200')

    def compile_one(job):
        relative, obj, command = job
        with Path(str(obj) + '.log').open('x') as log:
            code = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT).returncode
        return relative, obj, code

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for relative, obj, code in pool.map(compile_one, jobs):
            if code:
                record['errors'].append(relative)
                print('NATIVE_COMPILE_FAILED', relative, flush=True)
                continue
            record['artifacts'][str(obj.relative_to(output))] = {'sha256': sha(obj), 'bytes': obj.stat().st_size}
            text = Path(str(obj) + '.d').read_text().replace('\\\n', '').split(':', 1)[1]
            for value in shlex.split(text):
                path = Path(value)
                if not any(path.is_relative_to(p) for p in [workspace, output, westlake, *map(Path, header_builds)]):
                    raise ValueError('Undeclared header outside workspace: ' + value)
                key = str(path.relative_to(workspace)) if path.is_relative_to(workspace) else str(path)
                record['headers'][key] = sha(path)
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('NATIVE_SOURCE_OBJECTS', len(record['artifacts']), 'FAILED', len(record['errors']), flush=True)
    if record['errors']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
