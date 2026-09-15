#!/usr/bin/env python3
"""Execute Chromium's declared source Java generators with pinned host tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import zipfile
from aosp_blueprint import Sources, index, evaluate


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['workspace', 'aidl-build', 'native-tools', 'out']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    workspace, output = args.workspace.resolve(), args.out.absolute()
    if output.exists():
        parser.error('--out must be new')
    root = workspace / 'android-source'
    cronet = root / 'external-cronet'
    sdk = workspace / 'toolchains/ohos-sdk/native/llvm/bin'
    graph = json.loads((args.aidl_build / 'artifacts.json').read_text())
    if graph['errors']:
        raise ValueError('Framework discovery failed')
    prerequisite = args.native_tools.resolve()
    native_report = json.loads((prerequisite / 'artifacts.json').read_text())
    protoc = prerequisite / native_report['artifacts']['protoc']['path']
    if sha(protoc) != native_report['artifacts']['protoc']['sha256']:
        raise ValueError('Protocol compiler differs from source report')
    modules, errors = index(root)
    if errors:
        raise ValueError('Blueprint parse failures: ' + str(errors))
    groups = Sources(root, modules)
    output.mkdir(parents=True)
    (output / 'tmp').mkdir()
    view = output / 'android'
    (view / 'external').mkdir(parents=True)
    (view / 'external/cronet').symlink_to(cronet, target_is_directory=True)
    record = {'inputs': {str(protoc): sha(protoc)}, 'commands': [], 'artifacts': {},
              'source_archives': {}, 'full_framework_accepted': False}
    env = dict(os.environ, LC_ALL='C', TZ='UTC', PYTHONDONTWRITEBYTECODE='1',
               TMPDIR=str(output / 'tmp'),
               LD_LIBRARY_PATH=str(prerequisite / 'lib'))
    for key in ['PYTHONPATH', 'CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH']:
        env.pop(key, None)
    done, running = {}, set()

    def consume(path):
        if not path.is_file():
            raise ValueError('Missing generator input: ' + str(path))
        record['inputs'][str(path)] = sha(path)
        return path

    def generated(name):
        if name in done:
            return done[name]
        if name in running:
            raise ValueError('Cyclic generator dependency: ' + name)
        running.add(name)
        module = groups.module(name)
        fields, kind = module['fields'], module['kind']
        directory = output / name
        directory.mkdir()

        def locate(value):
            if value == 'cronet_aml_third_party_protobuf_protoc':
                return [protoc]
            if value == 'soong_zip':
                return [Path('a2hlab-source-zip')]
            if value.startswith(':'):
                return generated(value[1:])
            return [consume(cronet / value)]

        inputs = [p for name in fields.get('srcs', []) for p in locate(name)]
        for relative in fields.get('tool_files', []):
            locate(relative)
        if kind == 'filegroup':
            result = [consume(p) for p in groups.files(name)]
        elif kind == 'cc_object' and '-E' in fields.get('cflags', []):
            if len(inputs) != 1:
                raise ValueError('Expected one preprocessing input: ' + name)
            result = [directory / inputs[0].name]
            command = [str(sdk / 'clang'), '-x', 'c++', *fields['cflags'], '-I' + str(cronet),
                       '-MMD', '-MF', str(directory / 'includes.d'), str(inputs[0]), '-o', str(result[0])]
            record['commands'].append(command)
            with (directory / 'preprocess.log').open('x') as log:
                subprocess.run(command, cwd=view, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            for value in shlex.split((directory / 'includes.d').read_text().replace('\\\n', '').split(':', 1)[1]):
                consume(Path(value))
        elif kind in {'genrule', 'java_genrule'}:
            result = [directory / relative for relative in fields['out']]
            for path in result:
                path.parent.mkdir(parents=True, exist_ok=True)
            text = evaluate(fields['cmd'], module['environment'])

            def replacement(match):
                paths = locate(match[2])
                if match[1] == 'location' and len(paths) != 1:
                    raise ValueError('Ambiguous generator location: ' + match[2])
                return ' '.join(shlex.quote(str(p)) for p in paths)

            text = re.sub(r'\$\((location|locations) ([^)]+)\)', replacement, text)
            text = text.replace('$(genDir)', str(directory))
            text = text.replace('$(out)', ' '.join(shlex.quote(str(p)) for p in result))
            text = text.replace('$(in)', ' '.join(shlex.quote(str(p)) for p in inputs))
            if name == 'cronet_aml_components_cronet_android_cronet_jni_registration__java':
                # The Soong rule's shell loop converts relative Java input
                # names into a response file. Our inputs are already absolute.
                (directory / 'java.sources').write_text(''.join(str(p) + '\n' for p in inputs))
                marker = '; python3 '
                if marker not in text:
                    raise ValueError('JNI source-list rule changed')
                text = 'python3 ' + text.split(marker, 1)[1]
            for number, part in enumerate(text.split('&&')):
                command = shlex.split(part)
                command = [str(cronet / token.removeprefix('external/cronet/'))
                           if token.startswith('external/cronet/') else token for token in command]
                if not command or any(token in {';', '|', '||', '>', '<'} or '$(' in token for token in command):
                    raise ValueError('Unsupported source generator command: ' + part)
                record['commands'].append(command)
                if command[:2] == ['mkdir', '-p']:
                    for directory_name in command[2:]:
                        path = Path(directory_name)
                        if not path.is_relative_to(directory):
                            raise ValueError('Generator directory escapes output')
                        path.mkdir(parents=True, exist_ok=True)
                    continue
                if command[0] == 'cp':
                    if len(command) != 3 or not Path(command[2]).is_relative_to(directory):
                        raise ValueError('Unsupported source copy')
                    shutil.copyfile(consume(Path(command[1])), command[2])
                    continue
                if command[0] == 'a2hlab-source-zip':
                    # soong_zip -srcjar stores Java files at their package path.
                    target = Path(command[command.index('-o') + 1])
                    files = [Path(command[i + 1]) for i, token in enumerate(command) if token == '-f']
                    with zipfile.ZipFile(target, 'x', compression=zipfile.ZIP_STORED) as archive:
                        for path in files:
                            content = consume(path).read_bytes()
                            package = re.search(rb'(?m)^\s*package\s+([\w.]+)\s*;', content)
                            if not package:
                                raise ValueError('Source archive member lacks package')
                            relative = package[1].decode().replace('.', '/') + '/' + path.name
                            info = zipfile.ZipInfo(relative, (1980, 1, 1, 0, 0, 0))
                            info.external_attr = 0o100644 << 16
                            archive.writestr(info, content)
                    continue
                tool = Path(command[0])
                if command[0] == 'python3':
                    command = command[1:]
                    tool = Path(command[0])
                if not tool.is_relative_to(cronet) or tool.suffix != '.py':
                    raise ValueError('Undeclared source executable: ' + command[0])
                command = [sys.executable, '-B', '-S', *command]
                record['commands'][-1] = command
                with (directory / (str(number) + '.log')).open('x') as log:
                    process = subprocess.run(command, cwd=view, env=env, stdout=log, stderr=subprocess.STDOUT)
                if process.returncode:
                    raise RuntimeError('Source generator failed: ' + str(directory / (str(number) + '.log')))
            for path in result:
                if not path.is_file():
                    raise ValueError('Declared generator output missing: ' + str(path))
        else:
            raise ValueError('Unsupported generator module: ' + name + ': ' + kind)
        running.remove(name)
        done[name] = result
        return result

    names = [n for n, m in graph['remaining_generators'].items()
             if n.startswith('cronet_') and m['kind'] in {'genrule', 'java_genrule'}]
    for name in names:
        generated(name)
    for name, files in done.items():
        for path in files:
            if path.suffix not in {'.srcjar', '.jar'}:
                continue
            record['source_archives'][str(path.relative_to(output))] = sha(path)
            with zipfile.ZipFile(path) as archive:
                for member in archive.infolist():
                    if member.is_dir():
                        continue
                    relative = Path(member.filename)
                    if relative.is_absolute() or '..' in relative.parts or relative.suffix != '.java':
                        raise ValueError('Unexpected source archive member: ' + member.filename)
                    destination = output / 'java' / name / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_bytes(archive.read(member))
    record['definitions'] = {p: sha(root / p) for p in sorted(groups.definitions)}
    record['artifacts'] = {str(p.relative_to(output)): {'sha256': sha(p), 'bytes': p.stat().st_size}
                           for p in sorted(output.rglob('*.java')) if not p.is_relative_to(view)}
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('CRONET_JAVA_GENERATION_PASS', len(done), 'modules', len(record['artifacts']), 'Java files')


if __name__ == '__main__':
    main()
