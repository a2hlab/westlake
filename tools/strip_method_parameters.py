#!/usr/bin/env python3
"""Remove MethodParameters attributes from .class files, in place.

javac 21 records the unnamed mandated parameter of an anonymous class's constructor in a
MethodParameters attribute (JDK-8292275). d8 8.x reads that name as a string and fails with
"Cannot invoke String.length() because <parameter1> is null". The attribute only feeds reflection
(Parameter.getName); dex does not carry it by default either.

Usage: strip_method_parameters.py <classes dir>
"""
import pathlib
import struct
import sys


def strip(data):
    pos = 10
    count = struct.unpack_from('>H', data, 8)[0]
    utf8 = {}
    index = 1
    while index < count:
        tag = data[pos]
        if tag == 1:
            length = struct.unpack_from('>H', data, pos + 1)[0]
            utf8[index] = data[pos + 3:pos + 3 + length]
            pos += 3 + length
        elif tag in (3, 4, 9, 10, 11, 12, 17, 18):
            pos += 5
        elif tag in (5, 6):
            pos += 9
            index += 1
        elif tag in (7, 8, 16, 19, 20):
            pos += 3
        elif tag == 15:
            pos += 4
        else:
            raise ValueError('constant pool tag %d' % tag)
        index += 1
    out = bytearray(data[:pos])
    pos += 6  # access, this, super
    interfaces = struct.unpack_from('>H', data, pos)[0]
    pos += 2 + 2 * interfaces
    out += data[len(out):pos]

    def members(pos, out):
        n = struct.unpack_from('>H', data, pos)[0]
        out += data[pos:pos + 2]
        pos += 2
        for _ in range(n):
            out += data[pos:pos + 6]
            pos += 6
            pos, out = attributes(pos, out)
        return pos, out

    def attributes(pos, out):
        n = struct.unpack_from('>H', data, pos)[0]
        pos += 2
        kept = []
        for _ in range(n):
            name, length = struct.unpack_from('>HI', data, pos)
            if utf8.get(name) != b'MethodParameters':
                kept.append(data[pos:pos + 6 + length])
            pos += 6 + length
        out += struct.pack('>H', len(kept)) + b''.join(kept)
        return pos, out

    pos, out = members(pos, out)  # fields
    pos, out = members(pos, out)  # methods
    out += data[pos:]  # class attributes
    return bytes(out)


changed = 0
for path in pathlib.Path(sys.argv[1]).rglob('*.class'):
    data = path.read_bytes()
    stripped = strip(data)
    if stripped != data:
        path.write_bytes(stripped)
        changed += 1
print('stripped MethodParameters from', changed, 'classes')
