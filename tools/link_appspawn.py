#!/usr/bin/env python3
"""Link the source appspawn launcher against the source native platform."""
import argparse
import json
from pathlib import Path
import re
import subprocess
from build_hwui_objects import sha
from link_native_platform import verified_build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'objects', 'runtime', 'core-runtime', 'imports', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    if output.exists():
        parser.error('--out must be new')
    roots = {n: getattr(args, n.replace('-', '_')).resolve() for n in ['objects', 'runtime', 'core-runtime', 'imports']}
    reports = {n: verified_build(p) for n, p in roots.items()}
    if reports['imports'].get('deployable') is not False:
        raise ValueError('Expected link-only declarations')
    sdk = workspace / 'toolchains/ohos-sdk/native'
    source_objects = [roots['objects'] / n for n in reports['objects']['artifacts'] if n.endswith('.o')]
    names = ['libnativehelper.so', 'libbase.so', 'libutils.so', 'liblog.so']
    providers = [roots['core-runtime'] / n for n in names]
    providers += [roots['runtime'] / n for n in ['liboh_android_runtime.so', 'liboh_adapter_bridge.so',
                                                'libwestlake_bionic.so', 'libandroid.so']]
    providers += [roots['imports'] / n for n, e in reports['imports']['libraries'].items() if e['origin'] == 'host-firmware']
    # zlib has to be the executable's own dependency, even though no appspawn
    # object calls it, so --no-as-needed keeps it. A shared library's undefined
    # symbols resolve against the process-wide global scope before that library's
    # own DT_NEEDED chain, and the chipset GPU driver statically links zlib and
    # exports 90 of its symbols. Whenever the driver reached the global scope it
    # therefore answered deflate*/inflate* for everyone, including our own
    # java.util.zip boundary -- which does declare libz.so -- so a z_stream set
    # up by one zlib was handed to the driver's copy and Toutiao died in
    # deflateReset+60. Naming libz.so here puts it in the global scope at process
    # start, ahead of anything a later dlopen can add, so the split cannot occur.
    zlib = [roots['core-runtime'] / 'libz.so']
    output.mkdir(parents=True)
    target = output / 'appspawn-x'
    command = [str(sdk / 'llvm/bin/clang++'), '--target=aarch64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot'),
               '-fuse-ld=lld', '-nostdlib++', '-pie', '-Wl,-z,defs', '-Wl,--allow-shlib-undefined',
               '-Wl,--error-limit=0', '-Wl,--build-id=sha1', *map(str, source_objects),
               '-Wl,--no-as-needed', *map(str, zlib),
               '-Wl,--as-needed', *map(str, providers), '-o', str(target)]
    record = {'scope': 'source appspawn executable; full framework/device acceptance pending',
              'prerequisites': {n: sha(p / 'artifacts.json') for n, p in roots.items()},
              'commands': [command], 'artifacts': {}, 'errors': []}
    with (output / 'link.log').open('x') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        record['errors'] = ['appspawn-x']
    else:
        dynamic = subprocess.check_output([str(sdk / 'llvm/bin/llvm-readelf'), '-d', str(target)], text=True)
        record['artifacts']['appspawn-x'] = {'sha256': sha(target), 'bytes': target.stat().st_size,
                                            'needed': re.findall(r'\(NEEDED\).*\[([^]]+)\]', dynamic)}
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    if result.returncode:
        raise SystemExit(1)
    print('SOURCE_APPSPAWN_LINKED', record['artifacts']['appspawn-x'], flush=True)


if __name__ == '__main__':
    main()
