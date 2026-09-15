#!/usr/bin/env python3
"""Generate the declared framework HIDL Java graph using the source-built compiler."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from aosp_blueprint import Sources, index


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'aidl-build', 'compiler-build', 'native-tools', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    if output.exists():
        parser.error('--out must be new')
    source = workspace / 'android-source'
    compiler_report = json.loads((args.compiler_build / 'artifacts.json').read_text())
    compiler = args.compiler_build.resolve() / 'hidl-gen'
    if sha(compiler) != compiler_report['artifacts']['hidl-gen']['sha256']:
        raise ValueError('Source HIDL compiler differs from build report')
    aidl_report = json.loads((args.aidl_build / 'artifacts.json').read_text())
    if aidl_report['errors']:
        raise ValueError('AIDL dependency discovery did not finish successfully')
    modules, errors = index(source)
    if errors:
        raise ValueError('Blueprint parse failures: ' + str(errors))
    groups = Sources(source, modules)
    pending = []
    for name in aidl_report['remaining_generators']:
        match = re.fullmatch(r'(.+)-V(\d+\.\d+)-java(-constants)?', name)
        if match:
            pending.append((match[1] + '@' + match[2], bool(match[3])))
    roots = {}
    for choices in modules.values():
        for module in choices:
            if module['kind'] == 'hidl_package_root':
                fields = module['fields']
                roots[fields['name']] = source / Path(module['file']).parent
    output.mkdir(parents=True)
    (output / 'java').mkdir()
    record = {'compiler_sha256': sha(compiler), 'aidl_report_sha256': sha(args.aidl_build / 'artifacts.json'),
              'inputs': {}, 'interfaces': {}, 'commands': [], 'artifacts': {}, 'full_framework_accepted': False}
    env = dict(os.environ, LC_ALL='C', TZ='UTC', LD_LIBRARY_PATH=str(args.native_tools.resolve() / 'lib'))
    seen = set()
    while pending:
        name, constants = pending.pop(0)
        if (name, constants) in seen:
            continue
        seen.add((name, constants))
        module = groups.module(name)
        if module['kind'] != 'hidl_interface':
            raise ValueError('Expected HIDL interface: ' + name)
        fields = module['fields']
        files = groups.files(name)
        for path in files:
            record['inputs'][str(path.relative_to(source))] = sha(path)
        # Constants-only modules do not import other generated Java modules.
        if not constants:
            pending += [(n, False) for n in fields.get('interfaces', [])]
        record['interfaces'][name + ('-constants' if constants else '')] = fields.get('interfaces', [])
        command = [str(compiler), '-Ljava-constants' if constants else '-Ljava', '-o', str(output / 'java')]
        for root, path in sorted(roots.items()):
            command += ['-r', root + ':' + str(path)]
        command.append(name)
        record['commands'].append(command)
        with (output / (name + ('-constants' if constants else '') + '.log')).open('x') as log:
            subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    record['definitions'] = {p: sha(source / p) for p in sorted(groups.definitions)}
    record['artifacts'] = {str(p.relative_to(output)): {'sha256': sha(p), 'bytes': p.stat().st_size}
                           for p in sorted((output / 'java').rglob('*.java'))}
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('FRAMEWORK_HIDL_PASS', len(record['interfaces']), 'interfaces', len(record['artifacts']), 'Java files')


if __name__ == '__main__':
    main()
