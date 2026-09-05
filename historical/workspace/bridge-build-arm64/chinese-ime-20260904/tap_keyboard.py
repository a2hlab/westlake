#!/usr/bin/env python3
"""Physical-screen replay for this 1200x1920 board; requires visible Pinyin panel.

Does not focus editors, change app text directly, or infer benchmark latency.
Example: python3 tap_keyboard.py zhongguo --commit
"""
import argparse
import device
keys = {}
for row, xs, y in [('qwertyuiop',[85,199,314,429,543,658,772,885,1000,1115],1349),
                    ('asdfghjkl',[142,256,371,486,599,713,829,943,1058],1478),
                    ('zxcvbnm',[256,371,486,599,713,831,945],1608)]:
    keys.update({letter:(x,y) for letter,x in zip(row,xs)})
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('pinyin')
parser.add_argument('--commit',action='store_true',help='tap first candidate after typing')
args=parser.parse_args()
if not args.pinyin or any(c not in keys for c in args.pinyin): parser.error('lowercase letters only')
points=[keys[c] for c in args.pinyin]
if args.commit: points.append((120,1225))
print(device.shell('; '.join(f'uinput -T -c {x} {y}' for x,y in points)))
