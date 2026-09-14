#!/usr/bin/env python3
"""Compose disjoint, verified source object builds without relinking them."""
import argparse
import json
from pathlib import Path
import shutil

from build_hwui_objects import sha
from link_native_platform import verified_build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--replace', action='append', default=[],
                        help='Explicit object name to replace with the later build (once)')
    args = parser.parse_args()
    output = args.out.absolute()
    if output.exists():
        parser.error('--out must be new')
    inputs = [(p.resolve(), verified_build(p.resolve())) for p in args.build]
    selected = {}
    requested = set(args.replace)
    if len(requested) != len(args.replace):
        parser.error('Duplicate replacement request')
    replacements = {}
    for directory, report in inputs:
        for name, entry in report['artifacts'].items():
            if not name.endswith('.o'):
                continue
            if name in selected:
                if name not in requested or name in replacements:
                    raise ValueError('Duplicate object ownership: ' + name)
                previous, previous_entry = selected[name]
                replacements[name] = {'from': str(previous), 'from_sha256': previous_entry['sha256'],
                                      'to': str(directory / name), 'to_sha256': entry['sha256']}
            selected[name] = (directory / name, entry)
    if requested != set(replacements):
        raise ValueError('Unused object replacements: ' + ', '.join(sorted(requested - set(replacements))))
    if not selected:
        raise ValueError('No source objects')
    output.mkdir(parents=True)
    record = {'scope': 'verified source object composition; link and device checks pending',
              'prerequisites': {str(p): sha(p / 'artifacts.json') for p, _ in inputs},
              'artifacts': {}, 'origins': {}, 'replacements': replacements, 'errors': []}
    for name, (path, entry) in sorted(selected.items()):
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        if sha(target) != entry['sha256']:
            raise ValueError('Copied object differs: ' + name)
        record['artifacts'][name] = entry
        record['origins'][name] = str(path)
    (output / 'artifacts.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    print('NATIVE_OBJECTS_COMBINED', len(selected))


if __name__ == '__main__':
    main()
