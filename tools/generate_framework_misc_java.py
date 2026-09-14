#!/usr/bin/env python3
"""Run the source event-log, APEX schema and Cronet version generators."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
from aosp_blueprint import Sources, index, evaluate


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'aidl-build', 'xsdc-build', 'java', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    if output.exists():
        parser.error('--out must be new')
    root = workspace / 'android-source'
    graph = json.loads((args.aidl_build / 'artifacts.json').read_text())
    if graph['errors']:
        raise ValueError('Framework source discovery failed')
    xsdc = args.xsdc_build.resolve() / 'xsdc.jar'
    if sha(xsdc) != json.loads((xsdc.parent / 'artifacts.json').read_text())['artifacts']['xsdc.jar']['sha256']:
        raise ValueError('XSD compiler differs from source report')
    modules, errors = index(root)
    if errors:
        raise ValueError('Blueprint parse failures: ' + str(errors))
    groups = Sources(root, modules)
    output.mkdir(parents=True)
    record = {'inputs': {str(xsdc): sha(xsdc)}, 'commands': [], 'artifacts': {}, 'full_framework_accepted': False}
    env = dict(os.environ, LC_ALL='C', TZ='UTC')
    for name in ['JAVA_TOOL_OPTIONS', 'JDK_JAVA_OPTIONS', '_JAVA_OPTIONS', 'CLASSPATH', 'PYTHONPATH']:
        env.pop(name, None)

    def consume(path):
        record['inputs'][str(path)] = sha(path)
        return path

    def run(command, name):
        command = list(map(str, command))
        record['commands'].append(command)
        with (output / (name + '.log')).open('x') as log:
            result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError('Generator failed: ' + name + '; see ' + str(output / (name + '.log')))

    generator = consume(root / 'build-make/tools/java-event-log-tags.py')
    consume(root / 'build-make/tools/event_log_tags.py')
    for relative in graph['sources']:
        if not relative.endswith('.logtags'):
            continue
        path = consume(root / relative)
        destination = output / 'logtags' / Path(relative).with_suffix('.java')
        destination.parent.mkdir(parents=True, exist_ok=True)
        run([sys.executable, generator, '-o', destination, path], relative.replace('/', '_'))
    name = 'apex-info-list'
    fields = groups.module(name)['fields']
    destination = output / name
    destination.mkdir()
    command = [args.java.resolve(), '-jar', xsdc, '-j', '-p', fields['package_name'], '-o', destination]
    if fields.get('gen_writer'):
        command.append('-w')
    for element in fields.get('root_elements', []):
        command += ['-r', element]
    command += [consume(p) for p in groups.files(name)]
    run(command, name)
    for name in graph['collected_modules']:
        if name not in {'cronet_aml_components_cronet_android_interface_api_version',
                        'cronet_aml_components_cronet_android_implementation_api_version'}:
            continue
        module = groups.module(name)
        fields = module['fields']
        base = root / Path(module['file']).parent
        destination = output / name / fields['out'][0]
        destination.parent.mkdir(parents=True, exist_ok=True)
        for relative in fields['tool_files']:
            consume(base / relative)
        text = evaluate(fields['cmd'], module['environment']).split('&&', 1)[1].strip()
        text = re.sub(r'\$\(location ([^)]+)\)', lambda m: str(base / m[1]), text)
        text = text.replace('$(out)', str(destination))
        command = shlex.split(text)
        if any(token in {'&&', ';', '|', '||'} or '$(' in token for token in command):
            raise ValueError('Unsupported version generator command: ' + name)
        run([sys.executable, *command], name)
    record['definitions'] = {p: sha(root / p) for p in sorted(groups.definitions)}
    record['artifacts'] = {str(p.relative_to(output)): {'sha256': sha(p), 'bytes': p.stat().st_size}
                           for p in sorted(output.rglob('*.java'))}
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('FRAMEWORK_MISC_JAVA_PASS', len(record['artifacts']), 'Java files')


if __name__ == '__main__':
    main()
