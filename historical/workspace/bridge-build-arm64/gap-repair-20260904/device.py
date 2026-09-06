#!/usr/bin/env python3
"""Isolated device journal for the five-gap repair; reuse hash-checked transport."""
import importlib.util
from pathlib import Path
import sys,re,time
ROOT=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('gap_device_base', ROOT.parent/'ime-ability-window-20260904/device.py')
base=importlib.util.module_from_spec(spec); spec.loader.exec_module(base)
base.ROOT=ROOT; base.REMOTE='/data/local/tmp/asx/gap-repair-20260904'
shell,save,send,capture,tree,run=base.shell,base.save,base.send,base.capture,base.tree,base.run
def tree(pid,name):
    """Address the child's actual namespace, including its private /tmp mount."""
    assert isinstance(pid,int) and pid>1
    status=shell(f'cat /proc/{pid}/status')
    uid=int(re.search(r'^Uid:\s+(\d+)',status,re.M)[1])
    mailbox=f'/proc/{pid}/root/data/local/tmp/noice_tap.{pid}'
    log=f'/data/service/el1/public/appspawnx/adapter_child_{pid}.stderr'
    shell(f'touch {mailbox} && chown {uid}:{uid} {mailbox} && chmod 600 {mailbox}')
    offset=int(shell(f'N=$(wc -c < {log}); echo v > {mailbox}; echo $((N+1))').strip())
    output=''
    for _ in range(25):
        output=shell(f'tail -c +{offset} {log}')
        if 'VT ==== end ====' in output:break
        time.sleep(.2)
    (ROOT/(name+'.tree')).write_text(output)
    return output
if __name__=='__main__':
    action=sys.argv[1]
    if action=='shell': print(shell(sys.argv[2]))
    elif action=='save': print('Saved',sys.argv[2],len(save(sys.argv[2],sys.argv[3])))
    elif action=='capture': capture(sys.argv[2])
    elif action=='tree': print('Saved',sys.argv[3],len(tree(int(sys.argv[2]),sys.argv[3])))
    elif action=='send': print(send(sys.argv[2],sys.argv[3]))
