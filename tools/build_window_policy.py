#!/usr/bin/env python3
"""Build the window power boundary using the declared OH header closure."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--clang', type=Path, required=True, help='Pinned platform clang15 root')
    parser.add_argument('--providers', type=Path, help='Optional exact board libraries; omit for compile-only')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    output = args.out.absolute()
    if output.exists():
        parser.error('--out must be new')
    sdk, clang = args.sdk.resolve(), args.clang.resolve()
    headers = ROOT / 'third_party/oh61-window-headers'
    inventory = json.loads((headers / 'inventory.json').read_text())
    for name, digest in inventory['headers'].items():
        if sha(headers / name) != digest:
            raise ValueError('Modified pinned window header: ' + name)
    compiler = clang / 'bin/clang++'
    output.mkdir(parents=True)
    flags = ['--target=aarch64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot'),
             '-fPIC', '-O2', '-std=c++17', '-fno-rtti', '-D__OHOS__', '-D_GNU_SOURCE',
             '-DUSE_M133_SKIA', '-Wno-c++11-narrowing', '-DOH_WINDOW_POLICY_OVERLAY',
             '-DRENDER_SERVICE_CLIENT_CORE_FEATURE_RS_KEYFRAME_NODE_H',
             '-ffile-prefix-map=' + str(ROOT) + '=/src/westlake',
             '-ffile-prefix-map=' + str(sdk) + '=/toolchain/native',
             '-ffile-prefix-map=' + str(clang) + '=/toolchain/clang15',
             '-I' + str(ROOT / 'third_party/jni'), '-I' + str(ROOT / 'framework/window/jni')]
    flags += ['-I' + str(headers / name) for name in inventory['include_dirs']]
    source = ROOT / 'framework/window/jni/oh_window_policy.cpp'
    obj = output / 'oh_window_policy.o'
    subprocess.run([str(compiler), *flags, '-c', str(source), '-o', str(obj)], check=True)
    report = dict(compile_only=not bool(args.providers), device_accepted=False,
                  source_sha256=sha(source), header_inventory_sha256=sha(headers / 'inventory.json'),
                  artifacts={obj.name: dict(sha256=sha(obj), bytes=obj.stat().st_size)})
    if args.providers:
        providers = args.providers.resolve()
        lock = json.loads((ROOT / 'tests/parity/window-providers.lock.json').read_text())
        for name, digest in lock.items():
            if not (providers / name).is_file() or sha(providers / name) != digest:
                raise ValueError('Missing/mismatched board provider: ' + name)
        library = output / 'liboh_window_policy.so'
        subprocess.run([str(compiler), *flags, '-shared', '-Wl,-z,defs', str(obj),
                        '-L' + str(providers), '-ldl', '-lipc_single.z', '-lutils.z',
                        '-o', str(library)], check=True)
        report['providers'] = lock
        report['artifacts'][library.name] = dict(sha256=sha(library), bytes=library.stat().st_size)
    (output / 'artifacts.json').write_text(json.dumps(report, indent=2) + '\n')
    print('WINDOW_POLICY_BUILT', json.dumps(report))

if __name__ == '__main__':
    main()
