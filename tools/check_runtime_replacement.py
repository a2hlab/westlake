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
import re
import struct
from pathlib import Path
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dex_class_inventory import descriptor_to_binary, read_reference


def defined(path):
    return {descriptor_to_binary(d) for d in read_reference(path)}


# Method-name shaped: a lowercase-initial identifier of reasonable length. This
# is a heuristic over the DEX string table, not a method_ids walk, and it is
# deliberately loose -- it is used only to *warn*, never to decide, so a false
# positive costs a line of output and a false negative costs nothing that the
# class-level check already covers.
IDENTIFIER = re.compile(r'^[a-z][A-Za-z0-9_]{4,}$')


def dex_strings(path):
    """Every string in every classes*.dex of a jar (or a bare dex)."""
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            blobs = [archive.read(n) for n in sorted(archive.namelist())
                     if re.fullmatch(r'classes\d*\.dex', n)]
    else:
        blobs = [Path(path).read_bytes()]
    found = set()
    for data in blobs:
        count, offset = struct.unpack_from('<II', data, 56)
        for i in range(count):
            at, = struct.unpack_from('<I', data, offset + 4 * i)
            while data[at] & 0x80:
                at += 1
            at += 1
            found.add(data[at:data.index(b'\0', at)].decode('utf-8', 'replace'))
    return found


def missing_identifiers(candidate, deployed):
    """Identifier-shaped strings the deployed jar has and the candidate lacks.

    Why this exists: the class-level check passed a candidate that was a clean
    superset by class name and would still have regressed the device, because
    the deployed jar's classes had *grown methods* the candidate's had not. A
    jar can define every class its predecessor defined and still be a functional
    step backwards, and nothing about the class list shows it.
    """
    return sorted(s for s in (dex_strings(deployed) - dex_strings(candidate))
                  if IDENTIFIER.match(s))


def top_level(names):
    """Collapse Foo$Bar and d8 synthetics onto their outer class."""
    return sorted({n.split('$', 1)[0].split('-', 1)[0] for n in names})


ANON = None  # set below; kept module-level so tests can reach it


def is_anonymous_inner(name):
    """True for a compiler-generated anonymous inner like Outer$3 or Outer$3$1.

    Every $-separated segment after the outer name must be purely numeric.
    A *named* nested class (Outer$FlagMaskHandler) is excluded on purpose: those
    are reachable by Class.forName and dropping one is a real removal.
    """
    parts = name.split('$')
    return len(parts) > 1 and all(p.isdigit() for p in parts[1:])


def compare(candidate, deployed):
    new, old = defined(candidate), defined(deployed)
    removed, added = old - new, new - old

    # An anonymous inner is only ever referenced from its own outer class's
    # bytecode. If the candidate supplies that outer class, it supplies its own
    # anonymous inners too, and the numbering differing between two independent
    # compilations of the same source is expected -- not a removal. Anything
    # else, including named nested classes, still counts.
    benign = {r for r in removed
              if is_anonymous_inner(r) and r.split('$')[0] in new}
    removed = removed - benign

    return {
        'renumbered_anonymous': sorted(benign),
        'missing_identifiers': missing_identifiers(candidate, deployed),
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
    parser.add_argument('--allow-missing-identifiers', action='store_true',
                        help='Downgrade the method-level regression check to a warning')
    args = parser.parse_args()
    allow_missing = args.allow_missing_identifiers

    result = compare(args.candidate, args.deployed)
    print('candidate defines %d classes; deployed defines %d'
          % (result['candidate_classes'], result['deployed_classes']))

    if result['added']:
        print('\nadded by the candidate (%d top-level) -- not a problem:'
              % len(top_level(result['added'])))
        for name in top_level(result['added']):
            print('   +', name)

    if result['renumbered_anonymous']:
        print('\nanonymous inners renumbered by recompilation (%d) -- not a removal:'
              % len(result['renumbered_anonymous']))
        for name in result['renumbered_anonymous'][:8]:
            print('   ~', name)

    absent = result['missing_identifiers']
    if absent:
        print('\nREFUSED: %d identifiers present in the deployed jar are absent from '
              'the candidate.' % len(absent))
        for name in absent[:20]:
            print('   -', name)
        if len(absent) > 20:
            print('   ... and %d more' % (len(absent) - 20))
        print('\nThe class list can be a clean superset while the classes themselves\n'
              'have lost methods. That is a functional regression the deployed device\n'
              'would show as a feature quietly not working, so it fails here instead.\n'
              'If these are genuinely obsolete, re-run with --allow-missing-identifiers.')
        if not allow_missing:
            return 1

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
