#!/usr/bin/env python3
"""Archive and verify the exact deployed boot inputs before building a repair."""
import concurrent.futures
import hashlib
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
HDC = ['/mnt/c/Users/dspfa/Dev/ohos-tools/hdc.exe', '-t', '5cdbf6af00000000000000000923012c']
PINS = dict(
    core_oj='366bf7205be4b39e789a8ce908bf1cdb',
    core_libart='f4cf9889b154ed75bbe18c930db08a92',
    core_icu4j='46ac0e313e9407b3bf920da2bb3a20e7',
    okhttp='24d148391606995267bdae292ed590a1',
    bouncycastle='85dd99f8ac82c08cfacb4e530d3bb22b',
    apache_xml='67055626a70feaab530aee4a6ba1012d',
    adapter_mainline_stubs='f62106537ce442659b9a9f7b2b8dbcfe',
    framework='a39a4525221136f86c082802d4beb507',
    adapter_runtime_bcp='9c5e98061a50f946e82c7064e73326ea',
    oh_adapter_framework='180010c554b036e92c56b11b33ee301b')

def fetch(item):
    key, digest = item
    name = key.replace('_', '-') + '.jar'
    target = ROOT / 'original-fw' / name
    if not target.exists():
        subprocess.run(HDC + ['file', 'recv', '/data/local/tmp/asx/fw/' + name,
            str(target.relative_to(ROOT)).replace('/', '\\')], cwd=ROOT, check=True, capture_output=True)
    assert hashlib.md5(target.read_bytes()).hexdigest() == digest, name
    print('Verified', name, digest, flush=True)

if __name__ == '__main__':
    (ROOT / 'original-fw').mkdir(exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(3) as pool:
        list(pool.map(fetch, PINS.items()))
