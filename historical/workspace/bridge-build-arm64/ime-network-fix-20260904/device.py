#!/usr/bin/env python3
import json
import subprocess
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REMOTE = '/data/local/tmp/asx/ime-network-fix-20260904'
HDC = ['/mnt/c/Users/dspfa/Dev/ohos-tools/hdc.exe', '-t', '5cdbf6af00000000000000000923012c']
LOCK = threading.Lock()

def run(args, timeout=45):
    start = time.monotonic_ns()
    result = subprocess.run(HDC + args, cwd=ROOT, capture_output=True, timeout=timeout)
    text = result.stdout.decode(errors='replace').replace('\r', '')
    with LOCK, (ROOT / 'device-commands.jsonl').open('a') as out:
        out.write(json.dumps(dict(args=args, start_ns=start, end_ns=time.monotonic_ns(),
            returncode=result.returncode, stdout=text[-12000:],
            stderr=result.stderr.decode(errors='replace')[-2000:])) + '\n')
    if result.returncode or '[Fail]' in text:
        raise RuntimeError(text + result.stderr.decode(errors='replace'))
    return text

def shell(command, timeout=45):
    return run(['shell', command], timeout)

def capture(name):
    remote = REMOTE + '/' + name + '.jpeg'
    shell('snapshot_display -f ' + remote)
    run(['file', 'recv', remote, name + '.jpeg'])

def tree(pid, name):
    log = f'/data/service/el1/public/appspawnx/adapter_child_{pid}.stderr'
    offset = int(shell(f"N=$(wc -c < {log}); echo v > /data/local/tmp/noice_tap.{pid}; echo $((N + 1))").strip())
    text = ''
    for _ in range(25):
        text = shell(f'tail -c +{offset} {log}')
        if 'VT ==== end ====' in text: break
        time.sleep(.2)
    (ROOT / (name + '.tree')).write_text(text)
    return text
