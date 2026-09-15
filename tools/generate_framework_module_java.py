#!/usr/bin/env python3
"""Generate framework module protocol, property and statistics Java from source definitions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
from aosp_blueprint import Sources, index, evaluate

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'aidl-build', 'native-tools', 'schema-tools', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--catalog-build', type=Path)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    if output.exists():
        parser.error('--out must be new')
    root = workspace / 'android-source'
    graph = json.loads((args.aidl_build / 'artifacts.json').read_text())
    if graph['errors']:
        raise ValueError('Framework source discovery failed')
    record = {'inputs': {}, 'commands': [], 'artifacts': {}, 'modules': {}, 'full_framework_accepted': False,
              'aidl_report_sha256': sha(args.aidl_build / 'artifacts.json')}
    tools = {}
    directories = [args.native_tools.resolve(), args.schema_tools.resolve()]
    if args.catalog_build:
        directories.append(args.catalog_build.resolve())
    for directory in directories:
        report = json.loads((directory / 'artifacts.json').read_text())
        for name, entry in report['artifacts'].items():
            path = directory / entry.get('path', name)
            if not path.resolve().is_relative_to(directory) or sha(path) != entry['sha256']:
                raise ValueError('Generator differs from source build: ' + name)
            tools[name] = path
            record['inputs'][str(path)] = sha(path)
    modules, errors = index(root)
    if errors:
        raise ValueError('Blueprint parse failures: ' + str(errors))
    groups = Sources(root, modules)
    output.mkdir(parents=True)
    view = output / 'android'
    layout_file = ROOT / 'framework-tools/android-source-layout.json'
    layout = json.loads(layout_file.read_text())['projects']
    record['source_layout_sha256'] = sha(layout_file)
    for relative, android_path in layout.items():
        if not (workspace / relative).is_dir():
            continue
        path = view / android_path
        if any(parent.is_symlink() for parent in path.parents if parent.is_relative_to(view)):
            raise ValueError('Overlapping source layout would write through a source symlink: ' + android_path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.symlink_to(workspace / relative, target_is_directory=True)

    def virtual(path):
        parts = path.relative_to(workspace).parts
        return layout['/'.join(parts[:2])] + '/' + '/'.join(parts[2:])

    env = dict(os.environ, LC_ALL='C', TZ='UTC', LD_LIBRARY_PATH=str(args.native_tools.resolve() / 'lib'))

    def run(command, name):
        command = list(map(str, command))
        record['commands'].append(command)
        with (output / (name + '.log')).open('x') as log:
            result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError('Generator failed: ' + name + '; see ' + str(output / (name + '.log')))

    for name in graph['collected_modules']:
        if name not in modules:
            continue
        module = groups.module(name)
        fields = module['fields']
        kind = module['kind']
        generator = next((n for n in ['stats-log-api-gen', 'expresscatalog-codegen']
                          if n in fields.get('tools', []) and n in tools), None)
        if kind in {'genrule', 'java_genrule'} and generator:
            destination = output / name
            destination.mkdir()
            (destination / fields['out'][0]).parent.mkdir(parents=True, exist_ok=True)
            text = evaluate(fields['cmd'], module['environment'])
            text = text.replace('$(location ' + generator + ')', str(tools[generator]))
            text = text.replace('$(out)', str(destination / fields['out'][0])).replace('$(genDir)', str(destination))
            command = shlex.split(text)
            if generator == 'expresscatalog-codegen':
                command += ['--configDir', str(root / 'frameworks-proto-logging/stats/express/catalog')]
                for path in groups.files(name):
                    record['inputs'][str(path)] = sha(path)
            if any(token in {'&&', ';', '|', '||'} or '$(' in token for token in command):
                raise ValueError('Unsupported stats generator command: ' + name)
            run(command, name)
        elif kind == 'sysprop_library':
            destination = output / name
            destination.mkdir()
            for number, path in enumerate(groups.files(name)):
                record['inputs'][str(path)] = sha(path)
                run([tools['sysprop_java'], '--scope', 'internal', '--java-output-dir', destination, path],
                    name + '-' + str(number))
        elif ((kind in {'java_library', 'java_library_static'} and 'proto' in fields)
              or (kind == 'gensrcs' and 'protoc-gen-javastream' in fields.get('tools', []))):
            paths = [p for p in groups.files(name) if p.suffix == '.proto']
            if not paths:
                continue
            destination = output / name
            destination.mkdir()
            options = fields.get('proto', {})
            mode = 'javastream' if kind == 'gensrcs' else options.get('type', 'lite')
            if mode == 'stream':
                mode = 'javastream'
            record['modules'][name] = {'mode': mode, 'proto': options,
                                       'jarjar_rules': fields.get('jarjar_rules')}
            includes = [view, root / 'protobuf/src']
            includes += [view / p for p in options.get('include_dirs', [])]
            includes += [root / Path(module['file']).parent / p for p in options.get('local_include_dirs', [])]
            # Streaming genrules can declare additional -I roots directly.
            if kind == 'gensrcs':
                command_text = evaluate(fields['cmd'], module['environment'])
                includes += [view / p for p in re.findall(r'(?:^|\s)-I\s*([A-Za-z0-9_./-]+)', command_text)]
            # A protocol has one virtual filename. Prefer explicit project
            # roots over the whole Android tree, matching genrule -I ordering.
            includes = sorted(dict.fromkeys(includes), key=lambda p: -len(p.parts))
            pending = list(paths)
            consumed = set()
            while pending:
                path = pending.pop()
                if path in consumed:
                    continue
                consumed.add(path)
                record['inputs'][str(path)] = sha(path)
                for relative in re.findall(r'^\s*import\s+(?:public\s+|weak\s+)?"([^"]+)"', path.read_text(), re.M):
                    found = next((p / relative for p in includes if (p / relative).is_file()), None)
                    if found is None:
                        raise ValueError('Missing protocol import: ' + name + ': ' + relative)
                    pending.append(found)
            command = [tools['protoc'], *['-I' + str(p) for p in includes]]
            params = options.get('output_params', [])
            if mode in {'nano', 'javastream'}:
                plugin = 'javanano' if mode == 'nano' else mode
                command += ['--plugin=' + str(tools['protoc-gen-' + plugin]),
                            '--' + plugin + '_out=' + (','.join(params) + ':' if params else '') + str(destination)]
            elif mode in {'lite', 'full'}:
                command += ['--java_out=' + ('lite:' if mode == 'lite' else '') + str(destination)]
            else:
                raise ValueError('Unsupported Java protocol mode: ' + mode)
            command += [view / virtual(p) for p in paths]
            run(command, name)
    record['definitions'] = {p: sha(root / p) for p in sorted(groups.definitions)}
    record['artifacts'] = {str(p.relative_to(output)): {'sha256': sha(p), 'bytes': p.stat().st_size}
                           for p in sorted(output.rglob('*.java')) if not p.is_relative_to(view)}
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('FRAMEWORK_MODULE_JAVA_PASS', len(record['artifacts']), 'Java files')


if __name__ == '__main__':
    main()
