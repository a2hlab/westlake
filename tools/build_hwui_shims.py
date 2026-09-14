#!/usr/bin/env python3
"""Compile Westlake's HWUI platform shims and Android codec integration."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shlex
import subprocess
from build_hwui_objects import ROOT, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--westlake-source', type=Path, default=ROOT)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    westlake = args.westlake_source.resolve()
    if output.exists() or args.jobs < 1:
        parser.error('--out must be new and --jobs positive')
    inventory = westlake / 'native/hwui-shim-sources.json'
    spec = json.loads(inventory.read_text())
    output.mkdir(parents=True)
    (output / 'objects').mkdir()
    record = {'inventory_sha256': sha(inventory), 'sources': {}, 'headers': {}, 'commands': [],
              'artifacts': {}, 'errors': [], 'scope': 'source graphics shim objects; linking and device acceptance pending'}
    sdk = workspace / 'toolchains/ohos-sdk/native'
    platform = workspace / 'toolchains/clang-15'
    root = workspace / 'android-source'
    local = workspace / 'preserved/runtime-sources/local'
    skia = root / 'oh-skia/m133'
    mirror = root / 'oh61-source-carrier/upstream/sources/oh61-v7-b2133b5b/local_oh_headers/oh_mirror'
    includes = [root / 'libnativehelper/include_jni', local / 'skia_compat_headers',
                workspace / 'preserved/runtime-sources/aosp14-minikin/include',
                root / 'logging/liblog/include', root / 'system-core/libcutils/include',
                root / 'system-core/include', root / 'libbase/include',
                root / 'frameworks-native/opengl/include', skia, skia / 'include', skia / 'src',
                skia / 'modules/skcms/src', westlake / 'framework/appspawn-x/bionic_compat/include']
    includes += [mirror / p for p in ['foundation/graphic/graphic_surface/interfaces/inner_api/surface',
                 'foundation/graphic/graphic_surface/interfaces/inner_api/common',
                 'foundation/graphic/graphic_2d/interfaces/inner_api/composer',
                 'foundation/graphic/graphic_2d/rosen/modules/2d_graphics/drawing_ndk/include']]
    for path in includes:
        if not path.is_dir():
            raise ValueError('Missing declared header root: ' + str(path))
    common = ['--target=aarch64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot'), '-fPIC', '-O2',
              '-Werror=date-time', '-ffile-prefix-map=' + str(workspace) + '=/src',
              '-ffile-prefix-map=' + str(westlake) + '=/src/westlake',
              '-ffile-prefix-map=' + str(output) + '=/build/hwui-shims', *['-I' + str(p) for p in includes]]
    cpp = ['-std=c++17', '-nostdinc++', '-isystem', str(platform / 'include/libcxx-ohos/include/c++/v1'),
           '-D_LIBCPP_PROMOTE_H', '-include', str(local / 'libcxx_compat_arm64.h')]
    graphics = ['ANDROID', 'SK_BUILD_FOR_ANDROID', 'SK_BUILD_FOR_ANDROID_FRAMEWORK', 'SK_GANESH', '__ANDROID_API__=33',
                'ENABLE_TEXT_ENHANCE', 'SKIA_OHOS', 'SKIA_DLL', 'SK_GL', 'AHARDWAREBUFFER_FORMAT_R8_UNORM=0x38']
    jobs = []
    for relative in spec['sources'] + [spec['rtti_source']]:
        path = (westlake / relative.removeprefix('westlake/')
                if relative.startswith('westlake/') else workspace / relative)
        record['sources'][relative] = sha(path)
        obj = output / 'objects' / (relative.replace('/', '_') + '.o')
        flags = common[:]
        compiler = sdk / 'llvm/bin' / ('clang' if path.suffix == '.c' else 'clang++')
        if path.suffix == '.cpp':
            flags += cpp
            if relative == spec['rtti_source']:
                flags += ['-frtti', '-fvisibility=hidden', '-fno-exceptions']
            else:
                flags += ['-D' + d for d in graphics]
                if relative.startswith('android-source/oh-skia/'):
                    flags += ['-fno-rtti']
        command = [str(compiler), *flags, '-MMD', '-MF', str(obj) + '.d', '-c', str(path), '-o', str(obj)]
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
                print('HWUI_SHIM_FAILED', relative, flush=True)
                continue
            record['artifacts'][str(obj.relative_to(output))] = {'sha256': sha(obj), 'bytes': obj.stat().st_size}
            text = Path(str(obj) + '.d').read_text().replace('\\\n', '').split(':', 1)[1]
            for name in shlex.split(text):
                path = Path(name)
                if path.is_relative_to(westlake):
                    key = 'westlake/' + str(path.relative_to(westlake))
                elif path.is_relative_to(workspace):
                    key = str(path.relative_to(workspace))
                else:
                    raise ValueError('Header escaped workspace: ' + name)
                record['headers'][key] = sha(path)
    record['compiler_sha256'] = sha(sdk / 'llvm/bin/clang')
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('HWUI_SHIM_SOURCE_OBJECTS', len(record['artifacts']), 'FAILED', len(record['errors']), flush=True)
    if record['errors']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
