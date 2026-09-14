#!/usr/bin/env python3
"""Link the source Skia GPU probe against verified renderer and host APIs."""
import argparse
import json
from pathlib import Path
import subprocess
from build_hwui_objects import sha
from link_native_platform import verified_build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'objects', 'runtime', 'imports', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    output = args.out.absolute()
    if output.exists():
        parser.error('--out must be new')
    roots = {name: getattr(args, name).resolve() for name in ['objects', 'runtime', 'imports']}
    reports = {name: verified_build(path) for name, path in roots.items()}
    if reports['imports'].get('deployable') is not False:
        raise ValueError('Expected link-only firmware declarations')
    objects = [roots['objects'] / name for name in reports['objects']['artifacts'] if name.endswith('.o')]
    providers = [roots['runtime'] / 'libhwui.so']
    providers += [roots['imports'] / name for name in ['libEGL.so', 'libGLES_mali.z.so', 'libc++.so', 'libc.so']]
    sdk = args.workspace.resolve() / 'toolchains/ohos-sdk/native'
    output.mkdir(parents=True)
    target = output / 'platform_load_probe'
    command = [str(sdk / 'llvm/bin/clang++'), '--target=aarch64-linux-ohos',
               '--sysroot=' + str(sdk / 'sysroot'), '-nostdlib++', '-pie', '-fuse-ld=lld',
               '-Wl,-z,defs', '-Wl,--allow-shlib-undefined', *map(str, objects),
               '-Wl,--as-needed', *map(str, providers), '-o', str(target)]
    result = subprocess.run(command, capture_output=True, text=True)
    (output / 'link.log').write_text(result.stdout + result.stderr)
    record = {'scope': 'source Ganesh GPU rendering probe',
              'prerequisites': {name: sha(path / 'artifacts.json') for name, path in roots.items()},
              'command': command, 'artifacts': {}, 'errors': []}
    if result.returncode:
        record['errors'].append(target.name)
    else:
        record['artifacts'][target.name] = {'sha256': sha(target), 'bytes': target.stat().st_size}
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2) + '\n')
    if result.returncode:
        raise SystemExit(result.returncode)
    print('SOURCE_GRAPHICS_PROBE_LINKED', sha(target))


if __name__ == '__main__':
    main()
