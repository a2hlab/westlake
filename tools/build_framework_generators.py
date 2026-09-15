#!/usr/bin/env python3
"""Build Android schema generators using source-built protobuf and host libraries."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
from aosp_blueprint import index, Sources


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'bootstrap', 'host-base', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--catalog', action='store_true', help='Build the source Express metric catalog generator')
    parser.add_argument('--sysprop-only', action='store_true', help='Build Java and C++ system property generators')
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    output = args.out.absolute()
    if output.exists() or args.jobs < 1:
        parser.error('--out must be new and --jobs positive')
    if args.catalog and args.sysprop_only:
        parser.error('--catalog and --sysprop-only are mutually exclusive')
    sdk = workspace / 'toolchains/ohos-sdk/native/llvm'
    source = workspace / 'android-source'
    libraries = {}
    record = {'inputs': {}, 'commands': [], 'artifacts': {}, 'objects': {}, 'headers': {},
              'scope': 'source compiler tools; no device runtime acceptance'}
    for directory in [args.bootstrap.resolve(), args.host_base.resolve()]:
        report = json.loads((directory / 'artifacts.json').read_text())
        if report['target'] != 'host':
            raise ValueError('Compiler prerequisite is not a host build')
        for name, entry in report['artifacts'].items():
            path = directory / entry['path']
            if not path.resolve().is_relative_to(directory) or sha(path) != entry['sha256']:
                raise ValueError('Compiler prerequisite differs: ' + name)
            if name in libraries and sha(libraries[name]) != sha(path):
                raise ValueError('Conflicting compiler prerequisite: ' + name)
            libraries[name] = path
            record['inputs'][str(path.relative_to(workspace))] = sha(path)
    output.mkdir(parents=True)
    view = output / 'android'
    mapping = {'frameworks/base': 'frameworks-base', 'frameworks/proto_logging': 'frameworks-proto-logging',
               'external/protobuf': 'protobuf', 'system/tools/sysprop': 'system-tools-sysprop'}
    for relative, project in mapping.items():
        path = view / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.symlink_to(source / project, target_is_directory=True)
    modules, failures = index(source)
    if failures:
        raise ValueError('Cannot parse source definitions: ' + str(failures))
    groups = Sources(source, modules)
    env = dict(os.environ, LC_ALL='C', TZ='UTC')
    for name in ['CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'LD_LIBRARY_PATH']:
        env.pop(name, None)
    env['LD_LIBRARY_PATH'] = ':'.join(sorted({str(p.parent) for p in libraries.values()}))

    def run(command, log):
        command = list(map(str, command))
        record['commands'].append(command)
        with log.open('x') as stream:
            subprocess.run(command, check=True, env=env, stdout=stream, stderr=subprocess.STDOUT)

    def virtual(path):
        relative = path.relative_to(source)
        for android_path, project in mapping.items():
            if relative.parts[0] == project:
                return android_path + '/' + '/'.join(relative.parts[1:])
        raise ValueError('Undeclared Android project mapping: ' + str(path))

    generated = output / 'generated'
    generated.mkdir()
    protos = ([source / 'frameworks-proto-logging/stats/express/express_config.proto'] if args.catalog
              else groups.files('libstats_proto_host') + [source / 'system-tools-sysprop/sysprop.proto'])
    if args.sysprop_only:
        protos = [source / 'system-tools-sysprop/sysprop.proto']
    pending = [virtual(p) for p in protos]
    consumed = set()
    while pending:
        name = pending.pop()
        if name in consumed:
            continue
        path = view / name
        if not path.is_file():
            path = source / 'protobuf/src' / name
        if not path.is_file():
            raise ValueError('Missing protocol import: ' + name)
        consumed.add(name)
        record['inputs'][str(path.relative_to(workspace))] = sha(path)
        pending += re.findall(r'^\s*import\s+(?:public\s+|weak\s+)?"([^"]+)"', path.read_text(), re.M)
    for definition in groups.definitions:
        record['inputs']['android-source/' + definition] = sha(source / definition)
    run([libraries['protoc'], '-I' + str(view), '-I' + str(source / 'protobuf/src'),
         '--cpp_out=' + str(generated), *[str(view / virtual(p)) for p in protos]], output / 'protoc.log')
    generated_cpp = [generated / Path(virtual(p)).with_suffix('.pb.cc') for p in protos]
    stats_cpp = [] if args.sysprop_only else groups.files('stats-log-api-gen')
    sysprop_common = [source / 'system-tools-sysprop' / n for n in ['Common.cpp', 'CodeWriter.cpp']]
    property_root = source / 'system-core/property_service/libpropertyinfoserializer'
    # Common.cpp uses the upstream property-context type validator.
    sysprop_common += groups.files('libpropertyinfoserializer') + groups.files('libpropertyinfoparser')
    sysprop_java = sysprop_common + [source / 'system-tools-sysprop' / n for n in ['JavaGen.cpp', 'JavaMain.cpp']]
    sysprop_native = sysprop_common + [source / 'system-tools-sysprop' / n for n in ['CppGen.cpp', 'CppMain.cpp']]
    sysprop_cpp = sysprop_java + sysprop_native
    if args.catalog:
        stats_cpp = (groups.files('expresscatalog-codegen') + groups.files('libexpresscatalog') +
                     groups.files('libtextclassifier_hash_defaults') + groups.files('gflags_defaults'))
        sysprop_cpp = []
    flags = ['-O2', '-fPIC', '-std=gnu++20', '-D_GNU_SOURCE', '-DNDEBUG', '-DWITH_VENDOR',
             '-Werror=date-time', '-nostdinc++', '-isystem', str(sdk / 'include/c++/v1'),
             '-isystem', str(sdk / 'include/x86_64-unknown-linux-gnu/c++/v1'),
             '-ffile-prefix-map=' + str(workspace) + '=/src',
             '-ffile-prefix-map=' + str(output) + '=/build/framework-generators']
    if args.catalog:
        flags += ['-DNAMESPACE_FOR_HASH_FUNCTIONS=farmhash', '-D__STDC_FORMAT_MACROS',
                  '-DHAVE_INTTYPES_H', '-DHAVE_SYS_STAT_H', '-DHAVE_PTHREAD']
    includes = [generated, source / 'protobuf/src', source / 'libbase/include',
                source / 'logging/liblog/include', source / 'fmtlib/include', source / 'system-tools-sysprop/include',
                generated / 'system/tools/sysprop', property_root / 'include',
                source / 'system-core/property_service/libpropertyinfoparser/include']
    if args.catalog:
        express = source / 'frameworks-proto-logging/stats/express'
        includes += [express / 'libexpresscatalog/include', express / 'libexpresscatalog-utils/include',
                     generated / 'frameworks/proto_logging/stats/express', source / 'external-gflags/android',
                     source / 'external-libtextclassifier/native']
    flags += ['-I' + str(p) for p in includes]
    directory = output / 'objects'
    directory.mkdir()
    commands = []
    object_map = {}
    for path in sorted(set(generated_cpp + stats_cpp + sysprop_cpp)):
        relative = path.relative_to(output) if path.is_relative_to(output) else path.relative_to(workspace)
        obj = directory / (str(relative).replace('/', '_') + '.o')
        object_map[path] = obj
        record['inputs'][str(path.relative_to(workspace))] = sha(path)
        commands.append(([sdk / 'bin/clang++', *flags, '-MMD', '-MF', str(obj) + '.d',
                          '-c', path, '-o', obj], Path(str(obj) + '.log')))
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(lambda item: run(*item), commands))
    for path in object_map.values():
        record['objects'][str(path.relative_to(output))] = sha(path)
        for value in shlex.split(Path(str(path) + '.d').read_text().replace('\\\n', '').split(':', 1)[1]):
            header = Path(value)
            if not header.is_relative_to(workspace):
                raise ValueError('Undeclared compiler project header: ' + value)
            record['headers'][str(header.relative_to(workspace))] = sha(header)
    (output / 'bin').mkdir()
    targets = ([('expresscatalog-codegen', generated_cpp + stats_cpp)] if args.catalog else
               [('stats-log-api-gen', generated_cpp[:-1] + stats_cpp),
                ('sysprop_java', generated_cpp[-1:] + sysprop_java),
                ('sysprop_cpp', generated_cpp[-1:] + sysprop_native)])
    if args.sysprop_only:
        targets = [target for target in targets if target[0].startswith('sysprop_')]
    for name, selected in targets:
        path = output / 'bin' / name
        command = [sdk / 'bin/clang++', '-fuse-ld=lld', '-nostdlib++', '-Wl,-z,defs', '-Wl,--build-id=sha1',
                   *[object_map[p] for p in selected], libraries['libprotoc.a'], libraries['libprotobuf.a'],
                   libraries['libbase.so'], libraries['liblog.so'], libraries['libz.so'],
                   sdk / 'lib/x86_64-unknown-linux-gnu/libc++.a', sdk / 'lib/x86_64-unknown-linux-gnu/libc++abi.a',
                   '-lpthread', '-ldl', '-lm', '-o', path]
        run(command, output / (name + '-link.log'))
        record['artifacts'][name] = {'path': str(path.relative_to(output)), 'sha256': sha(path)}
        print('BUILT', name, sha(path), flush=True)
    record['commands'].sort(key=lambda c: c[-1])
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
