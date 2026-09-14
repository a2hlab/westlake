#!/usr/bin/env python3
"""Build deterministic unsigned host/Pinyin HAPs, including resources, from source.

Signing is deliberately outside the reproducible payload: no developer or
production key, profile, certificate, or device identifier is required here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def archive(path, entries):
    with zipfile.ZipFile(path, 'x', zipfile.ZIP_STORED) as result:
        for name, data in sorted(entries.items()):
            item = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            item.create_system = 3
            item.external_attr = 0o100644 << 16
            result.writestr(item, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True, help='SDK root containing native, ets and toolchains')
    parser.add_argument('--restool', type=Path, required=True, help='Source-built V1 Restool 4.105')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--host-bundle', default='org.westlake.imehost',
                        help='Use a separate host identity for isolated source device validation')
    parser.add_argument('--only-host', action='store_true')
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    out = args.out.absolute()
    if out.exists():
        parser.error('--out must be new')
    if not re.fullmatch(r'[a-zA-Z][a-zA-Z0-9_]*(?:\.[a-zA-Z][a-zA-Z0-9_]*)+', args.host_bundle):
        parser.error('Invalid host bundle name')
    restool = args.restool.resolve()
    version = subprocess.check_output([str(restool), '-v'], text=True)
    if version.strip() != 'Info: Restool version= Restool 4.105':
        parser.error('The legacy device requires locked V1 Restool 4.105')
    out.mkdir(parents=True)
    env = dict(os.environ, SOURCE_DATE_EPOCH='1772755200', TZ='UTC', LC_ALL='C')

    def run(*command, **kwargs):
        subprocess.run([str(x) for x in command], check=True, env=env, **kwargs)

    entries = {
        'imehost': {'EntryAbility': 'ets/entryability/EntryAbility.abc', 'Index': 'ets/pages/Index.abc'},
        'pinyin': {'EntryAbility': 'ets/entryability/EntryAbility.abc',
                   'ServiceExtAbility': 'ets/ServiceExtAbility/ServiceExtAbility.abc',
                   'Keyboard': 'ets/pages/Keyboard.abc', 'Settings': 'ets/pages/Settings.abc'},
    }
    report = {'artifacts': {}, 'restool_sha256': sha(restool)}
    for name, classes in entries.items():
        if args.only_host and name != 'imehost':
            continue
        source = ROOT / 'apps' / name
        output = out / name
        output.mkdir()
        package = args.host_bundle if name == 'imehost' else 'org.westlake.' + name
        if name == 'imehost' and package != 'org.westlake.imehost':
            copied = output / 'source'
            shutil.copytree(source, copied)
            module = json.loads((copied / 'module.json').read_text())
            module['app']['bundleName'] = package
            (copied / 'module.json').write_text(json.dumps(module, indent=2) + '\n')
            source = copied
        resources = output / 'resources'
        resources.mkdir()
        run(restool, '-i', source, '-j', source / 'module.json', '-p', package,
            '-o', resources, '-r', output / 'ResourceTable.js', '-f')
        payload = {p.relative_to(resources).as_posix(): p.read_bytes()
                   for p in sorted(resources.rglob('*')) if p.is_file()}
        for cls, destination in classes.items():
            run(sdk / 'ets/build-tools/ets-loader/bin/ark/build/bin/es2abc', '--target-api-version=20',
                '--output=' + str(output / (cls + '.abc')), cls + '.js', cwd=source)
            payload[destination] = (output / (cls + '.abc')).read_bytes()
        payload['.pages.info'] = (source / 'resources/base/profile/main_pages.json').read_bytes()
        if name == 'pinyin':
            engine = ROOT / 'third_party/pinyin'
            provenance = json.loads((engine / 'UPSTREAM.json').read_text())
            for entry in provenance['files']:
                if sha(engine / entry['path']) != entry['sha256']:
                    raise ValueError('Upstream Pinyin engine/dictionary changed: ' + entry['path'])
            native = sdk / 'native'
            library = output / 'libwestlake_pinyin.so'
            sources = sorted((engine / 'share').glob('*.cpp'))
            if len(sources) != 16:
                raise ValueError('Unexpected decoder source inventory')
            run(native / 'llvm/bin/clang++', '--target=aarch64-linux-ohos',
                '--sysroot=' + str(native / 'sysroot'), '-shared', '-fPIC', '-O2', '-std=c++17',
                '-Werror=date-time', '-ffile-prefix-map=' + str(ROOT) + '=/src/westlake',
                '-ffile-prefix-map=' + str(out) + '=/build/apps', '-I' + str(engine / 'include'),
                source / 'pinyin_napi.cpp', *sources, '-lace_napi.z', '-Wl,-z,defs', '-Wl,--build-id=sha1',
                '-Wl,-soname,libwestlake_pinyin.so', '-o', library)
            payload['libs/arm64-v8a/libwestlake_pinyin.so'] = library.read_bytes()
            payload['libs/arm64-v8a/libc++_shared.so'] = (native / 'llvm/lib/aarch64-linux-ohos/libc++_shared.so').read_bytes()
            payload['resources/rawfile/dict_pinyin.dat'] = (engine / 'dict_pinyin.dat').read_bytes()
            payload['resources/rawfile/PINYIN_LICENSE.txt'] = (engine / 'LICENSE').read_bytes()
            payload['resources/rawfile/NOTICE.txt'] = (
                'Pinyin decoder: Copyright (C) 2009 The Android Open Source Project.\n'
                'OHOS port: ' + provenance['url'] + ' @ ' + provenance['revision'] + '\n'
                'Apache License 2.0; engine and dictionary retained unchanged. New Westlake UI and N-API boundary.\n'
            ).encode()
        hap = out / (name + '-unsigned.hap')
        archive(hap, payload)
        report['artifacts'][hap.name] = {'sha256': sha(hap), 'bytes': hap.stat().st_size,
                                        'entries': {key: hashlib.sha256(value).hexdigest() for key, value in sorted(payload.items())}}
        print('HAP_BUILT', hap.name, sha(hap), flush=True)
    (out / 'artifacts.json').write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')


if __name__ == '__main__':
    main()
