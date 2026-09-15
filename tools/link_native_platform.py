#!/usr/bin/env python3
"""Strictly link source HWUI, Binder, runtime and OH bridge libraries."""
import argparse
import json
from pathlib import Path
import re
import subprocess
from build_hwui_objects import sha

DEPENDENCIES = {
    'libandroid.so': ['libhwui.so', 'liboh_android_runtime.so', 'libwestlake_binder.so',
                      'libandroidfw.so', 'libutils.so', 'libbase.so', 'liblog.so'],
    'libjnigraphics.so': ['libhwui.so', 'libandroid.so', 'liblog.so'],
    'libjpeg.so': [],
    'libwebp.so': [],
    'libwestlake_securec.so': [],
    'libwuffs.so': [],
    'libstdc++.so': [],
    'libwestlake_bionic.so': [],
    'liboh_permission_queries.so': [],
    'liboh_account_state.so': ['libnativehelper.so', 'liblog.so'],
    'libstats_jni.so': ['libstatssocket.so', 'liblog.so'],
    'libframework-connectivity-tiramisu-jni.so': ['libandroid.so', 'libnativehelper.so', 'liblog.so'],
    # Core framework-connectivity JNI. android.net.NetworkUtils loads exactly this
    # soname, and until it existed NetworkUtils.<clinit> raised UnsatisfiedLinkError,
    # which failed handleBindApplication through ConnectivityManager.getDefaultProxy().
    # Distinct from the tiramisu library above, which only carries TrafficStats.
    'libframework-connectivity-jni.so': ['libandroid.so', 'libnativehelper.so', 'liblog.so'],
    'libwestlake_libcore_linux.so': ['libnativehelper.so', 'libbase.so', 'liblog.so'],
    'libstatspull.so': ['libbinder_ndk.so', 'libstatssocket.so', 'libutils.so', 'liblog.so'],
    'libstatssocket.so': ['libbase.so', 'liblog.so'],
    'libbinder_ndk.so': ['libwestlake_binder.so', 'liboh_android_runtime.so', 'libnativehelper.so',
                         'libbase.so', 'libutils.so', 'liblog.so'],
    'libgif.so': [],
    'libultrahdr.so': ['liblog.so'],
    'libminikin.so': ['libharfbuzz_ng.so', 'libicuuc.so', 'libicui18n.so',
                      'libutils.so', 'libcutils.so', 'libbase.so', 'liblog.so'],
    'libhwui.so': ['libandroidfw.so', 'libminikin.so', 'libharfbuzz_ng.so', 'libft2.so',
                  'libicuuc.so', 'libicui18n.so', 'libutils.so', 'libcutils.so', 'libbase.so',
                  'liblog.so', 'libnativehelper.so', 'liboh_hwui_shim.so',
                  'liboh_skia_rtti_shim.so', 'liboh_adapter_bridge.so', 'liboh_android_runtime.so'],
    'liboh_adapter_bridge.so': ['libbase.so', 'liblog.so', 'libnativehelper.so', 'libutils.so',
                                'libz.so', 'libhwui.so', 'liboh_android_runtime.so', 'liboh_hwui_shim.so'],
    'liboh_android_runtime.so': ['libandroidfw.so', 'libnativehelper.so', 'libutils.so',
                                 'libcutils.so', 'libbase.so', 'liblog.so', 'libicuuc.so',
                                 'libicui18n.so', 'libhwui.so', 'liboh_adapter_bridge.so', 'libwestlake_binder.so'],
    'liboh_hwui_shim.so': ['libminikin.so', 'libutils.so', 'libbase.so', 'liblog.so',
                          'libhwui.so', 'liboh_adapter_bridge.so', 'liboh_skia_rtti_shim.so'],
    'liboh_skia_rtti_shim.so': [],
    'libwestlake_binder.so': ['libbase.so', 'libcutils.so', 'libutils.so', 'liblog.so'],
}


def verified_build(path):
    report = json.loads((path / 'artifacts.json').read_text())
    if report.get('errors'):
        raise ValueError('Failed prerequisite: ' + str(path))
    for name, entry in report['artifacts'].items():
        artifact = path / name
        if not artifact.resolve().is_relative_to(path) or sha(artifact) != entry['sha256']:
            raise ValueError('Prerequisite artifact differs: ' + str(artifact))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'object-map', 'imports', 'core-runtime', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    imports, core = args.imports.resolve(), args.core_runtime.resolve()
    if output.exists():
        parser.error('--out must be new')
    import_report, core_report = verified_build(imports), verified_build(core)
    if import_report.get('deployable') is not False or not (imports / 'DO_NOT_DEPLOY').is_file():
        raise ValueError('Expected declared link-only import build')
    if sha(args.object_map) != import_report['object_map_sha256']:
        raise ValueError('Import declarations refer to a different source object map')
    mapping = json.loads(args.object_map.read_text())
    sdk = workspace / 'toolchains/ohos-sdk/native'
    compiler = sdk / 'llvm/bin/clang++'
    compiler_rt = sdk / 'llvm/lib/clang/15.0.4/lib/aarch64-linux-ohos'
    output.mkdir(parents=True)
    record = {'scope': 'source native platform; device acceptance pending', 'commands': [],
              'imports_sha256': sha(imports / 'artifacts.json'), 'core_runtime_sha256': sha(core / 'artifacts.json'),
              'object_map_sha256': sha(args.object_map), 'object_builds': {}, 'artifacts': {},
              'errors': [], 'device_accepted': False, 'compiler_sha256': sha(compiler),
              'compiler_runtime_inputs': {n: sha(compiler_rt / n) for n in
                  ['clang_rt.crtbegin.o', 'clang_rt.crtend.o', 'libclang_rt.builtins.a']}}
    for name, spec in sorted(mapping.items()):
        if not re.fullmatch(r'lib[A-Za-z0-9_.+-]+\.so', name):
            raise ValueError('Invalid output library name: ' + name)
        directory = Path(spec['build']).resolve()
        report = verified_build(directory)
        fingerprint = sha(directory / 'artifacts.json')
        if import_report['prerequisites'].get(str(directory)) != fingerprint:
            raise ValueError('Source cycle declarations differ from object prerequisite')
        record['object_builds'][str(directory)] = fingerprint
        objects = [directory / n for n in spec.get('objects', [n for n in report['artifacts'] if n.endswith('.o')])]
        for path in objects:
            if str(path.relative_to(directory)) not in report['artifacts']:
                raise ValueError('Undeclared source object: ' + str(path))
        # Resolve source providers first. Cycles use declarations derived from
        # the exact input objects. Firmware declarations supply only host APIs.
        dependencies = [*DEPENDENCIES[name], *spec.get('additional_dependencies', [])]
        excluded = set(spec.get('excluded_dependencies', []))
        if not excluded <= set(dependencies):
            raise ValueError('Unknown dependency exclusion for ' + name)
        providers = [(imports if n in mapping else core if n in core_report['artifacts'] else imports) / n
                     for n in dependencies if n not in excluded]
        # Bind standard runtime symbols to their owner before other firmware
        # libraries which may happen to re-export the same C++ instantiations.
        firmware = [n for n, e in import_report['libraries'].items() if e['origin'] == 'host-firmware']
        excluded_firmware = set(spec.get('excluded_firmware', []))
        if not excluded_firmware <= set(firmware):
            raise ValueError('Unknown firmware exclusion for ' + name)
        firmware = [n for n in firmware if n not in excluded_firmware]
        runtime_order = {'libc++.so': 0, 'libc.so': 1}
        providers += [imports / n for n in sorted(firmware, key=lambda n: (runtime_order.get(n, 2), n))]
        flags = ['-Wl,-Bsymbolic-functions'] if name == 'libhwui.so' else []
        if spec.get('global_symbols'):
            flags += ['-Wl,-z,global']
        if spec.get('version_script'):
            recipe = Path(__file__).resolve().parents[1]
            versions = (recipe / spec['version_script']).resolve()
            if not versions.is_relative_to(recipe):
                raise ValueError('Version script escaped the source repository')
            flags += ['-Wl,--version-script=' + str(versions)]
            record.setdefault('version_scripts', {})[name] = {
                'path': str(versions.relative_to(recipe)), 'sha256': sha(versions)}
        command = [str(compiler), '--target=aarch64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot'),
                   '-fuse-ld=lld', '-nostdlib', '-shared', '-Wl,-z,defs', '-Wl,--error-limit=0',
                   '-Wl,--allow-shlib-undefined', '-Wl,--build-id=sha1', '-Wl,-soname,' + name,
                   *flags, str(compiler_rt / 'clang_rt.crtbegin.o'), *map(str, objects),
                   '-Wl,--as-needed', *map(str, providers), str(compiler_rt / 'libclang_rt.builtins.a'),
                   str(compiler_rt / 'clang_rt.crtend.o'), '-o', str(output / name)]
        record['commands'].append(command)
        with (output / (name + '.link.log')).open('x') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            record['errors'].append(name)
            print('NATIVE_LINK_FAILED', name, flush=True)
            continue
        dynamic = subprocess.check_output([str(sdk / 'llvm/bin/llvm-readelf'), '-d', str(output / name)], text=True)
        record['artifacts'][name] = {'sha256': sha(output / name), 'bytes': (output / name).stat().st_size,
                                     'needed': re.findall(r'\(NEEDED\).*\[([^]]+)\]', dynamic)}
        print('NATIVE_LINKED', name, flush=True)
    (output / 'artifacts.json').write_text(json.dumps(record, sort_keys=True, indent=2) + '\n')
    if record['errors']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
