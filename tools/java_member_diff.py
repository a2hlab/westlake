#!/usr/bin/env python3
"""Compare two variants of one Java class member by member.

Why this exists: ActivityManagerRouting was maintained in parallel by three
frontline workstreams, each deploying its own build over the others. Reconciling
3000-line files by eye is how you lose a method and find out on a device, so the
union is computed instead -- and this is what computes it. It is also how the
result is audited afterwards: run it against each frontline variant and a clean
"only in B: none" is the claim that nothing was left behind.

Not a Java parser. It splits one known class into its top-level members so two
variants can be compared, tracking strings, chars and comments so a brace inside
a literal does not throw the depth count off. That is enough for this job and
small enough to review.

  python3 tools/java_member_diff.py A.java B.java            # summary
  python3 tools/java_member_diff.py A.java B.java --only-b   # names to graft
  python3 tools/java_member_diff.py A.java B.java --extract NAME
"""
import argparse
import re
import sys
from pathlib import Path


def strip_noise(line):
    """Blank out string/char literals and line comments so braces can be counted."""
    out, i, n = [], 0, len(line)
    while i < n:
        c = line[i]
        if c in '"\'':
            quote = c
            i += 1
            while i < n:
                if line[i] == '\\':
                    i += 2
                    continue
                if line[i] == quote:
                    i += 1
                    break
                i += 1
            out.append('""')
            continue
        if c == '/' and i + 1 < n and line[i + 1] == '/':
            break
        out.append(c)
        i += 1
    return ''.join(out)


# A member declaration sits at class-body depth with exactly four spaces of
# indent, which is this file's house style throughout.
SIGNATURE = re.compile(
    r'^\s{4}(?!\s)(?:@\w+\s+)?'
    r'(?:(?:public|private|protected|static|final|abstract|volatile|synchronized'
    r'|transient|native)\s+)*'
    r'[\w<>\[\],.$?\s]+?\s+(\w+)\s*(\(|=|;)')


def members(path):
    """Map member name -> [(kind, first_line, last_line)], 1-indexed inclusive."""
    lines = Path(path).read_text(encoding='utf-8').splitlines()
    depth, in_comment, found, i = 0, False, {}, 0
    while i < len(lines):
        clean = strip_noise(lines[i])
        if in_comment:
            if '*/' in clean:
                in_comment, clean = False, clean.split('*/', 1)[1]
            else:
                i += 1
                continue
        if '/*' in clean:
            before, _, after = clean.partition('/*')
            if '*/' in after:
                clean = before + after.split('*/', 1)[1]
            else:
                clean, in_comment = before, True

        if depth == 1:
            match = SIGNATURE.match(lines[i])
            if match:
                name, kind = match.group(1), ('method' if match.group(2) == '(' else 'field')
                j, d, closed = i, depth, False
                while j < len(lines):
                    body = strip_noise(lines[j])
                    for ch in body:
                        if ch == '{':
                            d += 1
                        elif ch == '}':
                            d -= 1
                            if d == 1:
                                closed = True
                    if closed:
                        break
                    if kind == 'field' and body.rstrip().endswith(';') and d == 1:
                        break
                    j += 1
                found.setdefault(name, []).append((kind, i + 1, j + 1))
                depth, i = 1, j + 1
                continue
        depth += clean.count('{') - clean.count('}')
        i += 1
    return found


def block(path, span):
    lines = Path(path).read_text(encoding='utf-8').splitlines()
    return '\n'.join(lines[span[1] - 1:span[2]])


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('a', type=Path)
    parser.add_argument('b', type=Path)
    parser.add_argument('--only-b', action='store_true',
                        help='Print just the names present in B and absent from A')
    parser.add_argument('--extract', help='Print B\'s source for one member')
    args = parser.parse_args()

    ma, mb = members(args.a), members(args.b)

    if args.extract:
        if args.extract not in mb:
            print('not in %s: %s' % (args.b.name, args.extract), file=sys.stderr)
            return 1
        print(block(args.b, mb[args.extract][0]))
        return 0

    only_b = sorted(set(mb) - set(ma))
    if args.only_b:
        print('\n'.join(only_b))
        return 0

    only_a, both = sorted(set(ma) - set(mb)), sorted(set(ma) & set(mb))
    differing = [k for k in both
                 if block(args.a, ma[k][0]).strip() != block(args.b, mb[k][0]).strip()]
    print('A = %s  %d members' % (args.a.name, len(ma)))
    print('B = %s  %d members' % (args.b.name, len(mb)))
    print('\nonly in B (%d)%s' % (len(only_b), ':' if only_b else ' -- none'))
    for k in only_b:
        print('    %-32s %s L%d-%d' % (k, mb[k][0][0], mb[k][0][1], mb[k][0][2]))
    print('\nonly in A (%d)' % len(only_a))
    print('\nshared but differing (%d)%s' % (len(differing), ':' if differing else ''))
    for k in differing:
        print('   ', k)
    return 0


if __name__ == '__main__':
    sys.exit(main())
