#!/usr/bin/env python3
"""Build Android's HIDL compiler from its declared sources and source host tools."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
from aosp_blueprint import Sources, index


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'native-tools', 'crypto-build', 'parser-build', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=6)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    if output.exists() or args.jobs < 1:
        parser.error('--out must be new and --jobs positive')
    source = workspace / 'android-source'
    sdk = workspace / 'toolchains/ohos-sdk/native/llvm'
    libraries = {}
    record = {'inputs': {}, 'commands': [], 'objects': {}, 'headers': {}, 'artifacts': {},
              'scope': 'source host HIDL compiler; no device runtime acceptance'}
    for directory in [args.native_tools.resolve(), args.crypto_build.resolve()]:
        report = json.loads((directory / 'artifacts.json').read_text())
        if report['target'] != 'host':
            raise ValueError('HIDL compiler prerequisite must be a host build')
        for name, entry in report['artifacts'].items():
            path = directory / entry['path']
            if not path.resolve().is_relative_to(directory) or sha(path) != entry['sha256']:
                raise ValueError('HIDL compiler prerequisite differs: ' + name)
            if name in libraries and sha(libraries[name]) != sha(path):
                raise ValueError('Conflicting HIDL prerequisite: ' + name)
            libraries[name] = path
            record['inputs'][str(path)] = sha(path)
    parser_build = args.parser_build.resolve()
    parser_report = json.loads((parser_build / 'artifacts.json').read_text())
    for name, entry in parser_report['artifacts'].items():
        path = parser_build / name
        if not path.resolve().is_relative_to(parser_build) or sha(path) != entry['sha256']:
            raise ValueError('Parser generator differs: ' + name)
        record['inputs'][str(path)] = sha(path)
    parser_root = parser_build / parser_report['install_root']
    env = dict(os.environ, LC_ALL='C', TZ='UTC', M4=str(parser_root / 'bin/m4'),
               BISON_PKGDATADIR=str(parser_root / 'share/bison'),
               PATH=str(parser_root / 'bin') + ':/usr/bin:/bin',
               LD_LIBRARY_PATH=str(args.native_tools.resolve() / 'lib'))
    for name in ['CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH']:
        env.pop(name, None)
    output.mkdir(parents=True)
    generated = output / 'generated'
    generated.mkdir()

    def run(command, log, cwd=None):
        command = list(map(str, command))
        record['commands'].append(command)
        with log.open('x') as stream:
            subprocess.run(command, cwd=cwd, env=env, check=True, stdout=stream, stderr=subprocess.STDOUT)

    hidl = source / 'system-tools-hidl'
    # Relative output filenames keep Bison header guards independent of the checkout.
    run([parser_root / 'bin/bison', '--no-lines', '--defines=hidl-gen_y.h',
         '-o', 'hidl-gen_y.cpp', hidl / 'hidl-gen_y.yy'], output / 'bison.log', generated)
    run([parser_root / 'bin/flex', '-L', '-o', 'hidl-gen_l.cpp', hidl / 'hidl-gen_l.ll'],
        output / 'flex.log', generated)
    modules, errors = index(source)
    if errors:
        raise ValueError('Blueprint parse failures: ' + str(errors))
    groups = Sources(source, modules)
    files = []
    for name in ['libhidl-gen', 'libhidl-gen-ast', 'libhidl-gen-hash',
                 'libhidl-gen-utils', 'libhidl-gen-host-utils', 'hidl-gen', 'libjsoncpp_defaults']:
        files += groups.files(name)
    for path in files:
        record['inputs'][str(path)] = sha(path)
    files = [p for p in files if p.suffix in {'.cpp', '.cc', '.c'}]
    files += [generated / 'hidl-gen_y.cpp', generated / 'hidl-gen_l.cpp']
    includes = [generated, hidl, hidl / 'hashing/include', hidl / 'utils/include',
                hidl / 'utils/include/hidl-util', hidl / 'host_utils/include',
                hidl / 'host_utils/include/hidl-util', source / 'libbase/include',
                source / 'logging/liblog/include', source / 'fmtlib/include',
                source / 'boringssl/src/include', source / 'external-jsoncpp/include',
                source / 'system-core/libutils/binder/include',
                source / 'system-libhwbinder/include']
    flags = ['-O2', '-g0', '-std=gnu++20', '-D_GNU_SOURCE', '-DNDEBUG',
             '-DJSON_USE_EXCEPTION=0', '-DJSONCPP_NO_LOCALE_SUPPORT', '-Werror=date-time',
             '-nostdinc++', '-isystem', str(sdk / 'include/c++/v1'),
             '-isystem', str(sdk / 'include/x86_64-unknown-linux-gnu/c++/v1'),
             '-ffile-prefix-map=' + str(workspace) + '=/src',
             '-ffile-prefix-map=' + str(output) + '=/build/hidl', *['-I' + str(p) for p in includes]]
    (output / 'objects').mkdir()
    object_map = {}
    for path in sorted(set(files)):
        relative = path.relative_to(output) if path.is_relative_to(output) else path.relative_to(source)
        object_map[path] = output / 'objects' / (str(relative).replace('/', '_') + '.o')

    def compile_one(item):
        path, obj = item
        run([sdk / 'bin/clang++', *flags, '-MMD', '-MF', str(obj) + '.d', '-c', path, '-o', obj],
            Path(str(obj) + '.log'))

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(compile_one, object_map.items()))
    for path, obj in object_map.items():
        record['inputs'][str(path)] = sha(path)
        record['objects'][str(obj.relative_to(output))] = sha(obj)
        for value in shlex.split(Path(str(obj) + '.d').read_text().replace('\\\n', '').split(':', 1)[1]):
            header = Path(value)
            if not header.is_relative_to(workspace):
                raise ValueError('Undeclared HIDL compiler header: ' + value)
            record['headers'][str(header.relative_to(workspace))] = sha(header)
    path = output / 'hidl-gen'
    run([sdk / 'bin/clang++', '-fuse-ld=lld', '-nostdlib++', '-Wl,-z,defs', '-Wl,--build-id=sha1',
         *object_map.values(), libraries['libcrypto.a'], libraries['libbase.so'], libraries['liblog.so'],
         sdk / 'lib/x86_64-unknown-linux-gnu/libc++.a', sdk / 'lib/x86_64-unknown-linux-gnu/libc++abi.a',
         '-lpthread', '-ldl', '-lm', '-o', path], output / 'link.log')
    record['definitions'] = {p: sha(source / p) for p in sorted(groups.definitions)}
    record['commands'].sort(key=lambda c: c[-1])
    record['artifacts']['hidl-gen'] = {'path': 'hidl-gen', 'sha256': sha(path), 'bytes': path.stat().st_size}
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('HIDL_COMPILER_SOURCE_PASS', sha(path), flush=True)


if __name__ == '__main__':
    main()
