#!/usr/bin/env python3
"""Graft every member a frontline variant has and westlake's copy does not.

ActivityManagerRouting was maintained in parallel by three workstreams. Each
deployed its own build over the others, so the jar on the device is whichever
one ran last and no single source file is a superset of the rest. This closes
that: it takes the union, member by member, computed rather than eyeballed.

Order matters. Later sources fill only what earlier ones did not supply, so a
member defined in two variants comes from the first one listed -- stated here
rather than left to whichever file happened to be read last.

Idempotent: run it twice and the second run grafts nothing.
"""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from java_member_diff import block, members

# Everything grafted lands in one clearly labelled region rather than being
# interleaved, so a reviewer can see exactly what arrived from where.
BANNER = '''
    // ====================================================================
    // Frontline union -- grafted from %s
    //
    // These are members the device's runtime has and this file did not. They
    // arrived by tools/merge_frontline_members.py rather than by hand, and
    // tools/java_member_diff.py against the same source is what proves nothing
    // was left behind.
    //
    // They are frontline code and read like it. Tidying them is a separate
    // change from making the two trees agree, and doing both at once would make
    // it impossible to review either.
    // ====================================================================
'''


def graft(target, sources, anchor):
    text = Path(target).read_text(encoding='utf-8')
    have = set(members(target))
    added_total = []

    for source in sources:
        source_members = members(source)
        names = sorted(set(source_members) - have)
        if not names:
            continue
        chunks = [BANNER % Path(source).name]
        for name in names:
            chunks.append(block(source, source_members[name][0]))
            have.add(name)
        addition = '\n\n'.join(chunks)

        if text.count(anchor) != 1:
            raise SystemExit('anchor is not unique in %s: %r' % (target, anchor))
        text = text.replace(anchor, addition + '\n\n' + anchor)
        added_total.append((Path(source).name, names))

    Path(target).write_text(text, encoding='utf-8')
    return added_total


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--target', type=Path, required=True)
    parser.add_argument('--source', type=Path, action='append', required=True,
                        help='Frontline variant to take members from (repeatable, '
                             'first listed wins a name defined in two)')
    parser.add_argument('--anchor', default='    private static String currentProcessName() {',
                        help='Member declaration the graft is inserted before')
    args = parser.parse_args()

    added = graft(args.target, args.source, args.anchor)
    if not added:
        print('nothing to graft -- the target already has every member')
        return 0
    for name, names in added:
        print('from %s: %d members' % (name, len(names)))
        print('   ' + ' '.join(names))
    print('total grafted:', sum(len(n) for _, n in added))
    return 0


if __name__ == '__main__':
    sys.exit(main())
