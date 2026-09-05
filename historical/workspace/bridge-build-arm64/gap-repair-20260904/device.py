#!/usr/bin/env python3
"""Isolated device journal for the five-gap repair; reuse hash-checked transport."""
import importlib.util
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('gap_device_base', ROOT.parent/'ime-ability-window-20260904/device.py')
base=importlib.util.module_from_spec(spec); spec.loader.exec_module(base)
base.ROOT=ROOT; base.REMOTE='/data/local/tmp/asx/gap-repair-20260904'
shell,save,send,capture,tree,run=base.shell,base.save,base.send,base.capture,base.tree,base.run
if __name__=='__main__':
    action=sys.argv[1]
    if action=='shell': print(shell(sys.argv[2]))
    elif action=='save': print('Saved',sys.argv[2],len(save(sys.argv[2],sys.argv[3])))
    elif action=='capture': capture(sys.argv[2])
    elif action=='tree': print('Saved',sys.argv[3],len(tree(int(sys.argv[2]),sys.argv[3])))
    elif action=='send': print(send(sys.argv[2],sys.argv[3]))
