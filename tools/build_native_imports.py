#!/usr/bin/env python3
"""Build link-only ELF declarations for host firmware and source DSO cycles.

Generated functions trap if executed. These files are linker inputs only and
must never appear in a deployment payload. Actual source DSOs are audited
separately after linking, including cycles between HWUI and the OH bridge.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
from build_hwui_objects import sha


def elf_symbols(readelf, path):
    output = subprocess.check_output([str(readelf), '--syms', '--wide', str(path)], text=True)
    symbols = {}
    for line in output.splitlines():
        row = line.split()
        if (len(row) < 8 or not row[0].endswith(':') or row[4] not in ['GLOBAL', 'WEAK']
                or row[5] not in ['DEFAULT', 'PROTECTED'] or row[6] in ['UND', 'ABS']):
            continue
        name = row[7]
        if '@' in name:
            raise ValueError('Unexpected versioned object symbol: ' + name)
        symbols[name] = {'type': row[3], 'size': int(row[2]), 'binding': row[4]}
    return symbols


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'firmware-abi', 'object-map', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    if output.exists():
        parser.error('--out must be new')
    sdk = workspace / 'toolchains/ohos-sdk/native'
    compiler, readelf = sdk / 'llvm/bin/clang', sdk / 'llvm/bin/llvm-readelf'
    declarations = json.loads(args.firmware_abi.read_text())['firmware']
    libraries = {name: dict(entry, origin='host-firmware') for name, entry in declarations.items()}
    builds = json.loads(args.object_map.read_text())
    prerequisites = {}
    for name, spec in builds.items():
        build = Path(spec['build']).resolve()
        report = json.loads((build / 'artifacts.json').read_text())
        if report.get('errors'):
            raise ValueError('Failed object prerequisite: ' + str(build))
        prerequisites[str(build)] = sha(build / 'artifacts.json')
        symbols = {}
        names = spec.get('objects', [n for n in report['artifacts'] if n.endswith('.o')])
        for obj in names:
            path = build / obj
            if not path.resolve().is_relative_to(build) or sha(path) != report['artifacts'][obj]['sha256']:
                raise ValueError('Source object differs: ' + str(path))
            symbols.update(elf_symbols(readelf, path))
        if name in libraries:
            raise ValueError('Source DSO shadows declared firmware: ' + name)
        libraries[name] = {'origin': 'source-cycle', 'symbols': symbols}
    output.mkdir(parents=True)
    (output / 'DO_NOT_DEPLOY').write_text('Link-only import declarations. Functions trap. Use the separately linked source libraries.\n')
    record = {'scope': 'link-only declarations; never deploy', 'deployable': False,
              'firmware_abi_sha256': sha(args.firmware_abi), 'prerequisites': prerequisites,
              'object_map_sha256': sha(args.object_map), 'compiler_sha256': sha(compiler),
              'commands': [], 'artifacts': {}, 'libraries': {}}
    for name, entry in sorted(libraries.items()):
        if not re.fullmatch(r'[A-Za-z0-9_.+-]+', name):
            raise ValueError('Invalid SONAME: ' + name)
        lines, versions = [], {}
        for index, (symbol, definition) in enumerate(sorted(entry['symbols'].items())):
            if not re.fullmatch(r'[A-Za-z0-9_.$]+', symbol):
                raise ValueError('Unsupported ELF symbol name: ' + symbol)
            version = definition.get('version')
            if version and not definition.get('default_version'):
                raise ValueError('Non-default version needs a separate declaration: ' + symbol)
            if version:
                versions.setdefault(version, []).append(symbol)
            kind = definition['type']
            binding = '.weak' if definition['binding'] == 'WEAK' else '.global'
            if kind in ['FUNC', 'IFUNC', 'NOTYPE']:
                lines += ['.text', '.p2align 2', f'{binding} {symbol}', f'.type {symbol}, %function',
                          symbol + ':', 'brk #0', f'.size {symbol}, .-{symbol}']
            elif kind in ['OBJECT', 'TLS']:
                section = '.section .tbss,"awT",%nobits' if kind == 'TLS' else '.section .bss,"aw",%nobits'
                elf_type = '%tls_object' if kind == 'TLS' else '%object'
                lines += [section, '.p2align 4', f'{binding} {symbol}', f'.type {symbol}, {elf_type}',
                          symbol + ':', '.zero ' + str(max(1, definition['size'])),
                          f'.size {symbol}, ' + str(definition['size'])]
            else:
                raise ValueError('Unsupported ELF symbol type: ' + kind)
        lines += ['.section .note.GNU-stack,"",%progbits']
        # No C preprocessor: Rust-mangled ELF names can contain the token unix.
        assembly = output / (name + '.s')
        assembly.write_text('\n'.join(lines) + '\n')
        flags = []
        if versions:
            version_file = output / (name + '.map')
            text = ''
            for version, names in sorted(versions.items()):
                if not re.fullmatch(r'[A-Za-z0-9_.]+', version):
                    raise ValueError('Invalid version name')
                text += version + ' { global:\n' + ''.join('  ' + s + ';\n' for s in names) + '};\n'
            version_file.write_text(text)
            flags += ['-Wl,--version-script=' + str(version_file)]
        target = output / name
        command = [str(compiler), '--target=aarch64-linux-ohos', '-nostdlib', '-shared', '-fuse-ld=lld',
                   '-Wl,-z,defs', '-Wl,-soname,' + name, '-Wl,--build-id=sha1', *flags,
                   str(assembly), '-o', str(target)]
        record['commands'].append(command)
        subprocess.run(command, check=True)
        record['artifacts'][name] = {'sha256': sha(target), 'bytes': target.stat().st_size}
        record['libraries'][name] = {'origin': entry['origin'], 'symbols': len(entry['symbols'])}
    (output / 'artifacts.json').write_text(json.dumps(record, sort_keys=True, indent=2) + '\n')
    print('NATIVE_LINK_IMPORTS', len(libraries), 'DEPLOYABLE_FALSE', flush=True)


if __name__ == '__main__':
    main()
