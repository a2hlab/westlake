#!/usr/bin/env python3
"""Refuse a runtime jar that would remove classes the deployed one provides.

Why this exists
---------------
oh-adapter-runtime.jar is loaded by the child-process bootstrap. Replacing it
with a jar that is missing a class the runtime resolves by name does not fail at
install time and does not fail loudly -- it fails at the next app launch, on a
board, as a ClassNotFoundException several frames inside appspawn. If the
missing class is AppSpawnXInit, *nothing* starts, and the person who swapped the
jar is not the person who finds out.

That is exactly what a source-built jar invites. Building from an explicit
source inventory is the right thing to do, and it also means the artifact
contains precisely what someone listed -- which is not automatically everything
the previous artifact provided. This tool is the comparison nobody should be
asked to do by eye before a deployment.

Usage:
    check_runtime_replacement.py --candidate new.jar --deployed current.jar

Exit 0 only if the candidate defines every class the deployed jar defines.
Added classes are reported but never fail: a superset is a legitimate upgrade.
"""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dex_class_inventory import descriptor_to_binary, read_reference


def defined(path):
    return {descriptor_to_binary(d) for d in read_reference(path)}


def top_level(names):
    """Collapse Foo$Bar and d8 synthetics onto their outer class."""
    return sorted({n.split('$', 1)[0].split('-', 1)[0] for n in names})


def compare(candidate, deployed):
    new, old = defined(candidate), defined(deployed)
    removed, added = old - new, new - old
    return {
        'candidate': str(candidate),
        'deployed': str(deployed),
        'candidate_classes': len(new),
        'deployed_classes': len(old),
        'removed': sorted(removed),
        'added': sorted(added),
        'safe': not removed,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--candidate', type=Path, required=True,
                        help='The jar you are about to deploy')
    parser.add_argument('--deployed', type=Path, required=True,
                        help='The jar currently on the device')
    args = parser.parse_args()

    result = compare(args.candidate, args.deployed)
    print('candidate defines %d classes; deployed defines %d'
          % (result['candidate_classes'], result['deployed_classes']))

    if result['added']:
        print('\nadded by the candidate (%d top-level) -- not a problem:'
              % len(top_level(result['added'])))
        for name in top_level(result['added']):
            print('   +', name)

    if not result['removed']:
        print('\nSAFE: the candidate is a superset. Nothing the runtime can resolve '
              'today would disappear.')
        return 0

    print('\nREFUSED: the candidate would REMOVE %d classes (%d top-level).'
          % (len(result['removed']), len(top_level(result['removed']))))
    for name in top_level(result['removed']):
        print('   -', name)
    print('\nEach of these is resolvable by name from the deployed runtime today.\n'
          'Deploying this jar replaces a working bootstrap with one that is missing\n'
          'pieces of itself, and the failure surfaces at app launch rather than here.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
