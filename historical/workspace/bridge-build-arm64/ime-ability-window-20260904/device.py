#!/usr/bin/env python3
"""Journal device commands and preserve raw evidence for this isolated candidate."""
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys
import time
ROOT = Path(__file__).resolve().parent
HDC = ['/mnt/c/Users/dspfa/Dev/ohos-tools/hdc.exe', '-t', '5cdbf6af00000000000000000923012c']
REMOTE = '/data/local/tmp/asx/ime-ability-window-20260904'
def run(args, timeout=45):
    start = time.monotonic_ns()
    p = subprocess.run(HDC + args, cwd=ROOT, capture_output=True, timeout=timeout)
    output = p.stdout.decode(errors='replace').replace('\r', '')
    with (ROOT / 'device-commands.jsonl').open('a') as out:
        out.write(json.dumps(dict(args=args, start_ns=start, end_ns=time.monotonic_ns(),
            returncode=p.returncode, stdout=output, stderr=p.stderr.decode(errors='replace'))) + '\n')
    if p.returncode or '[Fail]' in output: raise RuntimeError(output)
    return output
def shell(command, timeout=45): return run(['shell', command], timeout)
def save(name, command):
    output = shell(command)
    (ROOT / name).write_text(output)
    return output
def send(path, remote):
    path = ROOT / path
    run(['file', 'send', str(path.relative_to(ROOT)).replace('/', '\\'), remote])
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    assert shell('sha256sum ' + remote).split()[0] == digest, 'remote hash mismatch'
    return digest
def capture(name):
    remote = REMOTE + '/' + name + '.jpeg'
    shell('mkdir -p ' + REMOTE + '; snapshot_display -f ' + remote)
    run(['file', 'recv', remote, name + '.jpeg'])
def tree(pid, name):
    log = f'/data/service/el1/public/appspawnx/adapter_child_{pid}.stderr'
    status = shell(f'cat /proc/{pid}/status')
    uid = int(re.search(r'^Uid:\s+(\d+)', status, re.M)[1])
    mailbox = f'/data/local/tmp/noice_tap.{pid}'
    # The child must be able to consume/truncate its own diagnostic mailbox;
    # otherwise one root-created command is reprocessed every polling tick.
    shell(f'touch {mailbox}; chown {uid}:{uid} {mailbox}; chmod 600 {mailbox}')
    offset = int(shell(f'N=$(wc -c < {log}); echo v > {mailbox}; echo $((N+1))').strip())
    for _ in range(25):
        output = shell(f'tail -c +{offset} {log}')
        if 'VT ==== end ====' in output: break
        time.sleep(.2)
    (ROOT / (name + '.tree')).write_text(output)
    return output
if __name__ == '__main__':
    if sys.argv[1] == 'save':
        output = save(sys.argv[2], sys.argv[3])
        print('Saved', sys.argv[2], len(output), 'characters')
    elif sys.argv[1] == 'shell': print(shell(sys.argv[2]))
    elif sys.argv[1] == 'capture': capture(sys.argv[2])
    elif sys.argv[1] == 'send': print(send(sys.argv[2], sys.argv[3]))
    elif sys.argv[1] == 'tree':
        output = tree(int(sys.argv[2]), sys.argv[3])
        print('Saved', sys.argv[3] + '.tree', len(output), 'characters')
