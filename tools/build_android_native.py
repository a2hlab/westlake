#!/usr/bin/env python3
"""Build declared Android support libraries for Linux host tools or the OHOS runtime."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--target', choices=['ohos', 'host'], default='ohos')
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--library', action='append')
    parser.add_argument('--extra-inventory', type=Path)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    if output.exists() or args.jobs < 1:
        parser.error('--out must be new and --jobs positive')
    inventory = json.loads((ROOT / 'native/libraries.json').read_text())
    extra_hash = None
    if args.extra_inventory:
        extra_path = args.extra_inventory.resolve()
        if not extra_path.is_relative_to(ROOT):
            raise ValueError('Additional inventory must be part of the Westlake source checkout')
        extra = json.loads(extra_path.read_text())
        if set(extra['libraries']) & set(inventory['libraries']):
            raise ValueError('Additional inventory redefines a native library')
        inventory['libraries'].update(extra['libraries'])
        inventory['includes'] += extra.get('includes', [])
        extra_hash = sha(extra_path)
    libraries = inventory['libraries']
    requested = args.library or list(libraries)
    selected = []

    def visit(name):
        if name in selected:
            return
        if name not in libraries:
            raise ValueError('Unknown library: ' + name)
        if args.target not in libraries[name].get('targets', ['ohos', 'host']):
            raise ValueError('Unsupported target for ' + name)
        for dependency in libraries[name].get('deps', []):
            visit(dependency)
        selected.append(name)

    for name in requested:
        visit(name)
    sdk, platform = workspace / 'toolchains/ohos-sdk/native', workspace / 'toolchains/clang-15'
    cc, cxx = sdk / 'llvm/bin/clang', sdk / 'llvm/bin/clang++'
    flags = ['-O2', '-fPIC', '-D_GNU_SOURCE', '-D_POSIX_C_SOURCE=200809L', '-DNDEBUG', '-D_FILE_OFFSET_BITS=64',
             '-Werror=date-time', '-ffile-prefix-map=' + str(workspace) + '=/src',
             # AOSP builds all native code with zero-initialized locals, and its sources rely on
             # it (libicu_jni's ScopedIcuLocale reads an uninitialized UErrorCode). clang 15 wants
             # the opt-in switch; AOSP passed it too until clang made the flag permanent.
             '-ftrivial-auto-var-init=zero',
             '-enable-trivial-auto-var-init-zero-knowing-it-will-be-removed-from-clang',
             '-ffile-prefix-map=' + str(output) + '=/build/native-support']
    cpp = ['-std=gnu++20', '-nostdinc++']
    cflags = ['-std=gnu11']
    linkflags = ['-shared', '-fuse-ld=lld', '-Wl,-z,defs', '-Wl,--build-id=sha1', '-nostdlib++']
    if args.target == 'ohos':
        flags += ['--target=aarch64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot'),
                  '-include', str(ROOT / 'native/include/ohos_port.h')]
        cpp += ['-isystem', str(platform / 'include/libcxx-ohos/include/c++/v1')]
        cpp_runtime = [platform / 'lib/aarch64-linux-ohos/libc++.so']
    else:
        cpp += ['-isystem', str(sdk / 'llvm/include/c++/v1'),
                '-isystem', str(sdk / 'llvm/include/x86_64-unknown-linux-gnu/c++/v1')]
        cpp_runtime = [sdk / 'llvm/lib/x86_64-unknown-linux-gnu/libc++.a',
                       sdk / 'llvm/lib/x86_64-unknown-linux-gnu/libc++abi.a']
    for relative in inventory['includes']:
        path = workspace / relative
        if not path.is_dir():
            raise ValueError('Missing declared include directory: ' + relative)
        flags += ['-I' + str(path)]
    output.mkdir(parents=True)
    (output / 'lib').mkdir()
    record = {'target': args.target, 'scope': 'source Android native support; device acceptance is separate',
              'artifacts': {}, 'inputs': {}, 'commands': [], 'objects': {}, 'device_accepted': False,
              'inventory_sha256': sha(ROOT / 'native/libraries.json'),
              'extra_inventory_sha256': extra_hash,
              'compiler_sha256': sha(cc), 'headers': {},
              'compiler_runtimes': {p.name: sha(p) for p in cpp_runtime}}
    env = dict(os.environ, SOURCE_DATE_EPOCH='1772755200', TZ='UTC', LC_ALL='C')

    def run(command, log):
        with log.open('x') as stream:
            subprocess.run(list(map(str, command)), check=True, env=env, stdout=stream, stderr=subprocess.STDOUT)

    for name in selected:
        spec = libraries[name]
        sources = spec['sources'] + spec.get(args.target + '_sources', [])
        if len(sources) != len(set(sources)):
            raise ValueError('Duplicate declared translation unit in ' + name)
        directory = output / Path(name).stem
        directory.mkdir()
        commands = []
        local_flags = []
        for relative in spec.get('includes', []):
            if not (workspace / relative).is_dir():
                raise ValueError('Missing include: ' + relative)
            local_flags.append('-I' + str(workspace / relative))
        for relative in sources:
            source = workspace / relative
            if Path(relative).is_absolute() or '..' in Path(relative).parts or not source.is_file():
                raise ValueError('Missing/outside declared source: ' + relative)
            record['inputs'][relative] = sha(source)
            object_path = directory / (relative.replace('/', '_') + '.o')
            lang = cflags if source.suffix == '.c' else cpp
            compiler = cc if source.suffix == '.c' else cxx
            definitions = spec.get('defines', []) + spec.get(args.target + '_defines', [])
            command = [compiler, *flags, *local_flags, *lang, *['-D' + d for d in definitions],
                       *spec.get('flags', []), '-MMD', '-MF', str(object_path) + '.d',
                       '-c', source, '-o', object_path]
            commands.append((command, Path(str(object_path) + '.log')))
            record['commands'].append(list(map(str, command)))
        if data := spec.get('embedded_data'):
            path, symbol = workspace / data['path'], data['symbol']
            if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', symbol) or not path.is_file():
                raise ValueError('Invalid embedded data input')
            # This is a versioned Unicode data asset, not a captured executable.
            record['inputs'][data['path']] = sha(path)
            source = directory / 'data.S'
            source.write_text('.section .rodata\n.balign 16\n.global ' + symbol + '\n' +
                              '.type ' + symbol + ', %object\n' + symbol + ':\n.incbin ' +
                              json.dumps(str(path)) + '\n.size ' + symbol + ', .-' + symbol + '\n' +
                              '.section .note.GNU-stack,"",%progbits\n')
            command = [cc, *flags, '-c', source, '-o', directory / 'data.o']
            commands.append((command, directory / 'data.log'))
            record['commands'].append(list(map(str, command)))
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            list(pool.map(lambda item: run(*item), commands))
        objects = [Path(command[-1]) for command, _ in commands]
        for path in objects:
            record['objects'][path.relative_to(output).as_posix()] = sha(path)
            dependency_file = Path(str(path) + '.d')
            if dependency_file.is_file():
                dependencies = dependency_file.read_text().replace('\\\n', '').split(':', 1)[1]
                for value in shlex.split(dependencies):
                    header = Path(value)
                    if not header.is_relative_to(workspace):
                        raise ValueError('Undeclared project dependency: ' + value)
                    record['headers'][header.relative_to(workspace).as_posix()] = sha(header)
        artifact = output / 'lib' / name
        dependencies = [output / 'lib' / dep for dep in spec.get('deps', [])]
        if spec.get('kind') == 'static':
            command = [sdk / 'llvm/bin/llvm-ar', 'rcsD', artifact, *objects]
        else:
            shared = spec.get('kind') != 'executable'
            library_flags = linkflags if shared else [f for f in linkflags if f != '-shared']
            command = [cxx, *flags, *library_flags, *(['-Wl,-soname,' + name] if shared else []),
                       *spec.get('linkflags', []), *objects, *dependencies,
                       *cpp_runtime, *spec.get('libraries', []),
                       '-ldl', '-lpthread', '-lm', '-o', artifact]
        record['commands'].append(list(map(str, command)))
        run(command, directory / 'link.log')
        dynamic = '' if spec.get('kind') == 'static' else subprocess.check_output(
            [str(sdk / 'llvm/bin/llvm-readelf'), '-d', str(artifact)], text=True)
        (directory / 'dynamic.txt').write_text(dynamic)
        record['artifacts'][name] = {'sha256': sha(artifact), 'bytes': artifact.stat().st_size,
                                     'path': artifact.relative_to(output).as_posix()}
        print('BUILT', args.target, name, record['artifacts'][name]['sha256'], flush=True)
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
