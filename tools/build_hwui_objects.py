#!/usr/bin/env python3
"""Compile the explicit Westlake HWUI source inventory for the OHOS boundary."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--westlake-source', type=Path, default=ROOT)
    parser.add_argument('--inventory', type=Path, default=ROOT / 'native/hwui-sources.json')
    parser.add_argument('--header-build', type=Path, action='append', default=[])
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    if output.exists() or args.jobs < 1:
        parser.error('--out must be new and --jobs positive')
    inventory_path = args.inventory.resolve()
    if not inventory_path.is_relative_to(ROOT):
        parser.error('--inventory must be part of Westlake')
    spec = json.loads(inventory_path.read_text())
    output.mkdir(parents=True)
    (output / 'objects').mkdir()
    record = {'inventory_sha256': sha(inventory_path), 'sources': {}, 'headers': {},
              'commands': [], 'artifacts': {}, 'errors': [], 'source_transforms': {},
              'scope': 'source HWUI objects; native linking and device acceptance pending'}
    legacy = workspace / 'preserved/runtime-sources'
    local = legacy / 'local'
    # The preserved source snapshot contains two workstation include paths.
    # Normalize only these include directives in an isolated header copy.
    compat = output / 'compat'
    for name in ['hwui_stubinc', 'skia_compat_headers']:
        shutil.copytree(local / name, compat / name)
    replacements = {
        'hwui_stubinc/android/configuration.h': (
            '#include "/home/dspfac/aosp-15/frameworks/native/include/android/configuration.h"',
            '#include_next <android/configuration.h>'),
        'skia_compat_headers/nativehelper/JNIPlatformHelp.h': (
            '#include "/home/dspfac/bridge-build/aosp/libnativehelper/include_platform/nativehelper/JNIPlatformHelp.h"',
            '#include_next <nativehelper/JNIPlatformHelp.h>'),
    }
    for relative, (old, new) in replacements.items():
        path = compat / relative
        if path.read_text().count(old) != 1:
            raise ValueError('Preserved compatibility header changed: ' + relative)
        record['source_transforms'][relative] = {'before': sha(path), 'replacement': new}
        path.write_text(path.read_text().replace(old, new))
        record['source_transforms'][relative]['after'] = sha(path)
    for relative, changes in spec.get('compat_replacements', {}).items():
        path = compat / relative
        before_hash = sha(path)
        text = path.read_text()
        for before, after in changes.items():
            if text.count(before) != 1:
                raise ValueError('Compatibility include changed: ' + relative)
            text = text.replace(before, after)
        path.write_text(text)
        record['source_transforms'][relative] = {'before': before_hash, 'after': sha(path), 'replacement': changes}
    for relative in spec.get('remove_compat_headers', []):
        path = compat / relative
        if path.is_file():
            record['source_transforms'][relative] = {'before': sha(path), 'replacement': 'canonical source header'}
            path.unlink()
    root = workspace / 'android-source'
    original_hwui = workspace / spec.get('hwui_source_root', 'preserved/runtime-sources/aosp14-hwui/libs/hwui')
    hwui = original_hwui
    if spec.get('header_replacements'):
        hwui = output / 'hwui'
        shutil.copytree(original_hwui, hwui, symlinks=True)
        for relative, changes in spec['header_replacements'].items():
            path = hwui / relative
            text = path.read_text()
            for before, after in changes.items():
                if before not in text:
                    raise ValueError('HWUI header port changed: ' + relative)
                text = text.replace(before, after)
            path.write_text(text)
            record['source_transforms']['hwui/' + relative] = {
                'before': sha(original_hwui / relative), 'after': sha(path), 'replacement': changes}
    skia = root / 'oh-skia/m133'
    mirror = root / 'oh61-source-carrier/upstream/sources/oh61-v7-b2133b5b/local_oh_headers/oh_mirror'
    sdk = workspace / 'toolchains/ohos-sdk/native'
    platform = workspace / 'toolchains/clang-15'
    compiler = sdk / 'llvm/bin/clang++'
    include = [compat / 'hwui_stubinc', hwui, hwui.parent, hwui / 'jni', hwui / 'renderthread',
               root / 'frameworks-base/libs/androidfw/include', root / 'incremental-delivery/incfs/util/include',
               root / 'fmtlib/include', workspace / spec.get('minikin_include', 'preserved/runtime-sources/aosp14-minikin/include'), legacy / 'aosp11-harfbuzz/src',
               compat / 'skia_compat_headers']
    include = [workspace / p for p in spec.get('includes_before_compat', [])] + include
    include += [skia / p for p in ['', 'include', 'include/core', 'include/private', 'src/core',
                 'src/gpu', 'src/image', 'src/utils', 'src/shaders', 'src/codec', 'include/private/base']]
    if spec.get('hwui_source_root'):
        include += [hwui / 'platform/android', skia / 'include/android',
                    ROOT / 'native/hwui/include']
    include += [root / p for p in ['libbase/include', 'system-core/include', 'system-core/libcutils/include',
                'system-core/libutils/include', 'system-core/libutils/binder/include', 'logging/liblog/include',
                'system-core/libsystem/include']]
    include += [root / 'frameworks-native' / p for p in ['include', 'libs/gui/include',
                'libs/nativewindow/include', 'libs/nativebase/include', 'libs/ui/include',
                'libs/ui/include_types', 'libs/arect/include', 'libs/math/include', 'opengl/include']]
    include += [root / 'libnativehelper' / p for p in ['include_jni', 'include', 'include_platform',
                'include_platform_header_only', 'header_only_include']]
    include += [workspace / 'upstream/aosp-11/libnativehelper' / p for p in ['include', 'header_only_include']]
    include += [mirror / p for p in ['foundation/graphic/graphic_surface/interfaces/inner_api/surface',
                'foundation/graphic/graphic_surface/interfaces/inner_api/common',
                'foundation/graphic/graphic_surface/interfaces/inner_api/buffer_handle',
                'base/hiviewdfx/hilog/interfaces/native/innerkits/include', 'commonlibrary/c_utils/base/include']]
    include += [legacy / 'aosp11-freetype/include', root / 'icu/icu4c/source/common',
                workspace / 'westlake/framework/surface/jni', workspace / 'westlake/framework/appspawn-x/bionic_compat/include']
    record['header_builds'] = {}
    generated_sources = []
    for directory in args.header_build:
        directory = directory.resolve()
        report = json.loads((directory / 'artifacts.json').read_text())
        for name, artifact in report['artifacts'].items():
            path = directory / name
            if not path.resolve().is_relative_to(directory) or sha(path) != artifact['sha256']:
                raise ValueError('Changed generated header: ' + str(path))
        record['header_builds'][str(directory)] = sha(directory / 'artifacts.json')
        include = [directory / name for name in report['include_directories']] + include
        if spec.get('include_generated_sources'):
            for name in report.get('generated_sources', []):
                if name not in report['artifacts'] or not name.endswith('.cpp'):
                    raise ValueError('Undeclared generated translation unit: ' + name)
                generated_sources.append(directory / name)
    for path in include:
        if not path.is_dir():
            raise ValueError('Missing declared include root: ' + str(path))
    defines = ['EGL_EGLEXT_PROTOTYPES', 'GL_GLEXT_PROTOTYPES', 'ATRACE_TAG=ATRACE_TAG_VIEW',
               'LOG_TAG="OpenGLRenderer"', '__OHOS__', '_GNU_SOURCE', 'ANDROID', 'HWUI_NO_VULKAN=1',
               'SK_BUILD_FOR_ANDROID_FRAMEWORK=1', 'HWUI_NO_STATS=1', 'HWUI_OH_SURFACE=1',
               'ENABLE_TEXT_ENHANCE', 'SKIA_OHOS_SINGLE_OWNER', 'SK_GANESH', 'SKIA_OHOS', 'SK_GL',
               'SKIA_DLL', 'NDEBUG', 'GPU_TEST_UTILS=1', 'ENABLE_STRING8_OBSOLETE_METHODS', '_LIBCPP_PROMOTE_H']
    defines = [d for d in defines if d not in spec.get('remove_defines', [])]
    defines += spec.get('defines', [])
    flags = ['--target=aarch64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot'), '-fPIC', '-O2',
             '-std=' + spec.get('cpp_standard', 'c++17'), '-nostdinc++', '-isystem', str(platform / 'include/libcxx-ohos/include/c++/v1'),
             '-Wno-deprecated-declarations', '-Wno-invalid-constexpr', '-Werror=date-time',
             '-ffile-prefix-map=' + str(workspace) + '=/src', '-ffile-prefix-map=' + str(output) + '=/build/hwui']
    flags += ['-D' + s for s in defines] + ['-I' + str(p) for p in include]
    flags += spec.get('cpp_flags', [])
    for path in [local / 'libcxx_compat_arm64.h', compat / 'skia_compat_headers/hwui_force_include.h',
                 ROOT / 'native/hwui/include/hwui_port.h']:
        flags += ['-include', str(path)]
    env = dict(os.environ, LC_ALL='C', TZ='UTC', SOURCE_DATE_EPOCH='1772755200')
    jobs = []
    for relative in spec['sources']:
        path = workspace / relative
        if relative.startswith('westlake/'):
            path = args.westlake_source.resolve() / relative.removeprefix('westlake/')
        if Path(relative).is_absolute() or '..' in Path(relative).parts or not path.is_file():
            raise ValueError('Invalid source: ' + relative)
        record['sources'][relative] = sha(path)
        if hwui != original_hwui and path.is_relative_to(original_hwui):
            path = hwui / path.relative_to(original_hwui)
        if relative in spec.get('include_replacements_by_source', {}):
            original = path
            text = path.read_text()
            for old, new in spec['include_replacements_by_source'][relative].items():
                if text.count(old) != 1:
                    raise ValueError('JNI include boundary changed: ' + relative)
                text = text.replace(old, new)
            path = output / ('jni-' + original.name)
            path.write_text(text)
            record['source_transforms'][relative] = {
                'before': sha(original), 'after': sha(path),
                'replacement': spec['include_replacements_by_source'][relative]}
        if relative == 'preserved/runtime-sources/local/hwui-overrides/hwui_oh_abi_patch.cpp':
            # The interposer's hand-written void* alias conflicts with the
            # canonical EGLNativeWindowType. Both use the same pointer ABI;
            # retain the upstream declaration and the existing boundary cast.
            old = 'typedef void*    EGLNativeWindowType;'
            text = path.read_text()
            if text.count(old) != 1:
                raise ValueError('Preserved EGL interposer typedef changed')
            transformed = output / 'hwui_oh_abi_patch.cpp'
            transformed.write_text(text.replace(old, '// EGLNativeWindowType is provided by EGL/egl.h.'))
            record['source_transforms'][relative] = {'before': sha(path), 'after': sha(transformed),
                                                   'replacement': 'canonical EGLNativeWindowType'}
            path = transformed
        obj = output / 'objects' / (relative.replace('/', '_') + '.o')
        source_flags = spec.get('flags_by_source_prefix', {})
        extra = [flag for prefix, values in source_flags.items() if relative.startswith(prefix)
                 for flag in values]
        command = [str(compiler), *flags, *extra, '-MMD', '-MF', str(obj) + '.d', '-c', str(path), '-o', str(obj)]
        jobs.append((relative, obj, command))
    for path in generated_sources:
        relative = 'generated/' + path.name
        if relative in record['sources']:
            raise ValueError('Duplicate generated translation unit: ' + relative)
        record['sources'][relative] = sha(path)
        obj = output / 'objects' / (relative.replace('/', '_') + '.o')
        command = [str(compiler), *flags, '-MMD', '-MF', str(obj) + '.d', '-c', str(path), '-o', str(obj)]
        jobs.append((relative, obj, command))
    record['commands'] = [command for _, _, command in jobs]

    def compile_one(job):
        relative, obj, command = job
        with Path(str(obj) + '.log').open('x') as log:
            result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        return relative, obj, result.returncode

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for relative, obj, code in pool.map(compile_one, jobs):
            if code:
                record['errors'].append(relative)
                print('HWUI_COMPILE_FAILED', relative, flush=True)
                continue
            record['artifacts'][str(obj.relative_to(output))] = {'sha256': sha(obj), 'bytes': obj.stat().st_size}
            dependencies = Path(str(obj) + '.d').read_text().replace('\\\n', '').split(':', 1)[1]
            for name in shlex.split(dependencies):
                path = Path(name)
                if path.is_relative_to(output):
                    key = 'output/' + str(path.relative_to(output))
                elif path.is_relative_to(workspace):
                    key = 'workspace/' + str(path.relative_to(workspace))
                elif path.is_relative_to(ROOT):
                    key = 'recipe/' + str(path.relative_to(ROOT))
                elif any(path.is_relative_to(Path(root)) for root in record['header_builds']):
                    key = 'generated/' + str(path)
                else:
                    raise ValueError('Header escaped declared roots: ' + name)
                record['headers'][key] = sha(path)
    record['compiler_sha256'] = sha(compiler)
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('HWUI_SOURCE_OBJECTS', len(record['artifacts']), 'FAILED', len(record['errors']), flush=True)
    if record['errors']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
