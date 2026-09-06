#!/usr/bin/env python3
"""Build the independent popen lifecycle probes for OH or stock Android."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--oh-sdk', type=Path)
    group.add_argument('--android-ndk', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    output = args.out.absolute()
    if output.exists():
        parser.error('--out must be new')
    if args.oh_sdk:
        sdk = args.oh_sdk.resolve()
        cc = sdk / 'llvm/bin/clang'
        flags = ['--target=aarch64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot')]
    else:
        sdk = args.android_ndk.resolve()
        cc = sdk / 'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang'
        flags = []
    if not cc.is_file():
        parser.error('Compiler missing: ' + str(cc))
    output.mkdir(parents=True)
    records = {}
    for name in ['popen_contract_probe', 'popen_stress_probe']:
        source = ROOT / 'tests/parity/native' / (name + '.c')
        binary = output / name
        command = [str(cc), *flags, '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                   '-ffile-prefix-map=' + str(ROOT) + '=/src/westlake',
                   str(source), '-pthread', '-o', str(binary)]
        subprocess.run(command, check=True)
        records[name] = dict(sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                             source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), command=command)
    (output / 'artifacts.json').write_text(json.dumps(records, indent=2) + '\n')
    print('PARITY_PROBES_BUILT', 'OH' if args.oh_sdk else 'Android')

if __name__ == '__main__':
    main()
