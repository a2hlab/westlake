#!/usr/bin/env python3
"""Generate Android framework protocol, stats and audio-schema Java sources."""
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
    for name in ['workspace', 'native-tools', 'schema-tools', 'xsdc-build', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--java', type=Path, required=True)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    root = workspace / 'android-source'
    if output.exists():
        parser.error('--out must be new')
    tools = {}
    record = {'inputs': {}, 'commands': [], 'artifacts': {}, 'full_framework_accepted': False}
    for directory in [args.native_tools.resolve(), args.schema_tools.resolve(), args.xsdc_build.resolve()]:
        report = json.loads((directory / 'artifacts.json').read_text())
        for name, entry in report['artifacts'].items():
            path = directory / entry.get('path', name)
            if not path.resolve().is_relative_to(directory) or sha(path) != entry['sha256']:
                raise ValueError('Source generator artifact differs: ' + name)
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
        path.parent.mkdir(parents=True, exist_ok=True)
        path.symlink_to(workspace / relative, target_is_directory=True)

    def virtual(path):
        parts = path.relative_to(workspace).parts
        project = '/'.join(parts[:2])
        return layout[project] + '/' + '/'.join(parts[2:])

    env = dict(os.environ, LC_ALL='C', TZ='UTC')
    env['LD_LIBRARY_PATH'] = str(args.native_tools.resolve() / 'lib')
    for name in ['JAVA_TOOL_OPTIONS', '_JAVA_OPTIONS', 'JDK_JAVA_OPTIONS', 'CLASSPATH']:
        env.pop(name, None)

    def run(command, name):
        command = list(map(str, command))
        record['commands'].append(command)
        with (output / (name + '.log')).open('x') as log:
            subprocess.run(command, env=env, check=True, stdout=log, stderr=subprocess.STDOUT)

    for name in ['statslog-framework-java-gen', 'statslog-telephony-common-java-gen', 'statslog-telephony-java-gen']:
        module = groups.module(name)
        path = output / 'stats' / module['fields']['out'][0]
        path.parent.mkdir(parents=True, exist_ok=True)
        # These source genrules are a single argv command; no shell is used.
        text = evaluate(module['fields']['cmd'], module['environment'])
        text = text.replace('$(location stats-log-api-gen)', str(tools['stats-log-api-gen'])).replace('$(out)', str(path))
        command = shlex.split(text)
        if any(token in {'&&', ';', '|', '||'} or '$(' in token for token in command):
            raise ValueError('Unsupported stats generator command')
        run(command, name)
    for name, plugin, mode in [('framework-javastream-protos', 'protoc-gen-javastream', 'javastream'),
                                ('framework-protos', 'protoc-gen-javanano', 'javanano')]:
        paths = groups.files(name)
        destination = output / name
        destination.mkdir()
        names = [virtual(p) for p in paths]
        pending, consumed = list(names), set()
        while pending:
            relative = pending.pop()
            if relative in consumed:
                continue
            path = view / relative
            if not path.is_file():
                path = root / 'protobuf/src' / relative
            if not path.is_file():
                raise ValueError('Missing protocol source/import: ' + relative)
            consumed.add(relative)
            record['inputs'][str(path)] = sha(path)
            pending += re.findall(r'^\s*import\s+(?:public\s+|weak\s+)?"([^"]+)"', path.read_text(), re.M)
        run([tools['protoc'], '-I' + str(view), '-I' + str(root / 'protobuf/src'),
             '--plugin=' + str(tools[plugin]), '--' + mode + '_out=' + str(destination),
             *[str(view / n) for n in names]], name)
    name = 'audio_policy_configuration_V7_0'
    fields = groups.module(name)['fields']
    destination = output / 'audio-schema'
    destination.mkdir()
    java = args.java.resolve()
    record['java_sha256'] = sha(java)
    paths = groups.files(name)
    command = [java, '-jar', tools['xsdc.jar'], '-j', '-p', fields['package_name'], '-o', destination]
    if fields.get('nullability'):
        command.append('-n')
    command += paths
    for path in paths:
        record['inputs'][str(path)] = sha(path)
    run(command, name)
    record['definitions'] = {p: sha(root / p) for p in sorted(groups.definitions)}
    for directory in ['stats', 'framework-javastream-protos', 'framework-protos', 'audio-schema']:
        for path in sorted((output / directory).rglob('*.java')):
            record['artifacts'][str(path.relative_to(output))] = {'sha256': sha(path), 'bytes': path.stat().st_size}
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('FRAMEWORK_JAVA_GENERATION_PASS', len(record['artifacts']))


if __name__ == '__main__':
    main()
