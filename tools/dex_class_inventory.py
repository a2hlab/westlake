#!/usr/bin/env python3
"""Inventory the classes a reference adapter DEX owns, and who owns them here.

Why this exists
---------------
`manifest/full-build-gaps.json` records:

    "Adapter Java builders have a cold-start dependency cycle and need an
     explicit duplicate-free class ownership inventory."

Until now the adapter runtime jar was produced by *merging* freshly compiled
classes into a byte-patched copy of a shipped `classes.dex`. That works, but it
means nobody can say which classes the jar is supposed to contain, which of them
exist as source in this repository, and which are still only available as
prebuilt bytecode. This tool answers exactly that question, mechanically:

    reference DEX  ->  set of class descriptors it defines
    repository     ->  set of class descriptors our .java sources define
    report         ->  owned-by-source / missing-source / source-only / duplicate

The DEX reader below is deliberately minimal. It walks only `header_item`,
`string_ids`, `type_ids` and `class_defs` -- enough to enumerate defined types --
because a full DEX parser would be a much larger thing to review for a job that
needs four tables. Format reference: https://source.android.com/docs/core/dalvik/dex-format
"""
import argparse
import json
from pathlib import Path
import re
import struct
import sys
import zipfile

DEX_MAGIC = b'dex\n'
HEADER_STRUCT = '<8sI20sIIIIIIIIIIIIIIIIIIII'
# field order after magic/checksum/signature, per dex-format header_item
HEADER_FIELDS = (
    'file_size', 'header_size', 'endian_tag', 'link_size', 'link_off',
    'map_off', 'string_ids_size', 'string_ids_off', 'type_ids_size',
    'type_ids_off', 'proto_ids_size', 'proto_ids_off', 'field_ids_size',
    'field_ids_off', 'method_ids_size', 'method_ids_off', 'class_defs_size',
    'class_defs_off', 'data_size', 'data_off',
)


def read_uleb128(data, offset):
    """Return (value, next_offset). DEX stores string lengths this way."""
    result = shift = 0
    while True:
        byte = data[offset]
        offset += 1
        result |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return result, offset
        shift += 7


def dex_defined_types(data):
    """Every type descriptor this DEX *defines* (not merely references)."""
    if data[:4] != DEX_MAGIC:
        raise ValueError('Not a DEX image (bad magic)')
    values = struct.unpack_from(HEADER_STRUCT, data, 0)
    header = dict(zip(HEADER_FIELDS, values[3:]))
    if header['endian_tag'] != 0x12345678:
        raise ValueError('Unsupported DEX endianness')

    strings = []
    for index in range(header['string_ids_size']):
        data_off, = struct.unpack_from('<I', data, header['string_ids_off'] + 4 * index)
        length, start = read_uleb128(data, data_off)
        # MUTF-8; class descriptors are plain ASCII, so a strict decode of the
        # NUL-terminated run is both correct here and loud if it ever is not.
        end = data.index(b'\0', start)
        strings.append(data[start:end].decode('utf-8'))

    types = []
    for index in range(header['type_ids_size']):
        descriptor_idx, = struct.unpack_from('<I', data, header['type_ids_off'] + 4 * index)
        types.append(strings[descriptor_idx])

    defined = []
    for index in range(header['class_defs_size']):
        class_idx, = struct.unpack_from('<I', data, header['class_defs_off'] + 32 * index)
        defined.append(types[class_idx])
    return defined


def descriptor_to_binary(descriptor):
    """'Ladapter/activity/Foo$Bar;' -> 'adapter.activity.Foo$Bar'."""
    if not (descriptor.startswith('L') and descriptor.endswith(';')):
        return descriptor
    return descriptor[1:-1].replace('/', '.')


def read_reference(path):
    """Accept either a .jar (reads every classes*.dex) or a bare .dex."""
    path = Path(path)
    if zipfile.is_zipfile(path):
        found = []
        with zipfile.ZipFile(path) as archive:
            names = sorted(n for n in archive.namelist()
                           if re.fullmatch(r'classes\d*\.dex', n))
            if not names:
                raise ValueError('Archive contains no classes*.dex: ' + str(path))
            for name in names:
                found += dex_defined_types(archive.read(name))
        return found
    return dex_defined_types(path.read_bytes())


PACKAGE_RE = re.compile(r'^\s*package\s+([\w.]+)\s*;', re.M)
# Top-level type declarations only: nested types are reached through their outer
# class, and d8 names them Outer$Inner, which we synthesise rather than parse.
TYPE_RE = re.compile(
    r'^[ \t]*(?:public\s+|final\s+|abstract\s+|strictfp\s+)*'
    r'(?:class|interface|enum|@interface|record)\s+(\w+)', re.M)


def source_classes(roots):
    """Map binary class name -> source file, for every .java under `roots`."""
    owned = {}
    duplicates = []
    for root in roots:
        for java in sorted(Path(root).rglob('*.java')):
            text = java.read_text(encoding='utf-8', errors='replace')
            package = PACKAGE_RE.search(text)
            prefix = (package.group(1) + '.') if package else ''
            names = TYPE_RE.findall(text)
            if not names:
                continue
            # The first declaration in the file is the top-level type; the rest
            # are either nested (already covered) or additional package-private
            # top-level types in the same file, which javac allows.
            for name in dict.fromkeys(names):
                binary = prefix + name
                if binary in owned and owned[binary] != java:
                    duplicates.append((binary, owned[binary], java))
                owned.setdefault(binary, java)
    return owned, duplicates


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--reference', type=Path, required=True,
                        help='Shipped adapter runtime .jar or .dex to inventory against')
    parser.add_argument('--source', type=Path, action='append', required=True,
                        help='Directory of .java sources (repeatable)')
    parser.add_argument('--out', type=Path, help='Write the inventory as JSON here')
    parser.add_argument('--require-complete', action='store_true',
                        help='Exit non-zero while any reference class lacks a source owner')
    args = parser.parse_args()

    reference = [descriptor_to_binary(d) for d in read_reference(args.reference)]
    if len(reference) != len(set(reference)):
        seen, repeated = set(), sorted({c for c in reference if c in seen or seen.add(c)})
        raise ValueError('Reference DEX defines a class twice: ' + ', '.join(repeated))
    owned, duplicates = source_classes(args.source)

    reference_set = set(reference)
    # Nested types never appear as their own source file; attribute them to the
    # outer class so the report does not cry wolf on every anonymous class.
    def has_owner(name):
        return name.split('$', 1)[0] in owned

    covered = sorted(c for c in reference_set if has_owner(c))
    missing = sorted(c for c in reference_set if not has_owner(c))
    extra = sorted(c for c in owned if c not in reference_set)

    report = {
        'reference': str(args.reference),
        'reference_classes': len(reference_set),
        'covered_by_source': covered,
        'missing_source': missing,
        'source_only': extra,
        'duplicate_owners': [[c, str(a), str(b)] for c, a, b in duplicates],
        'complete': not missing and not duplicates,
    }
    if args.out:
        args.out.write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')

    print('reference classes   :', len(reference_set))
    print('covered by source   :', len(covered))
    print('missing source      :', len(missing))
    print('source-only classes :', len(extra))
    print('duplicate owners    :', len(duplicates))
    for name in missing:
        print('  MISSING', name)
    for name, first, second in duplicates:
        print('  DUPLICATE', name, first, second)
    if args.require_complete and not report['complete']:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
