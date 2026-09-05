"""Reuse the verified HDC transport, with a separate Chinese-IME journal."""
import importlib.util
from pathlib import Path
import sys
base = Path(__file__).resolve().parent.parent / 'ime-ability-window-20260904/device.py'
spec = importlib.util.spec_from_file_location('ime_device_base', base)
impl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(impl)
impl.ROOT = Path(__file__).resolve().parent
impl.REMOTE = '/data/local/tmp/asx/chinese-ime-20260904'
ROOT, REMOTE = impl.ROOT, impl.REMOTE
shell, save, send, capture, tree, run = impl.shell, impl.save, impl.send, impl.capture, impl.tree, impl.run
if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'shell': print(shell(sys.argv[2]))
    elif cmd == 'save': print('Saved', sys.argv[2], len(save(sys.argv[2], sys.argv[3])), 'characters')
    elif cmd == 'send': print(send(sys.argv[2], sys.argv[3]))
    elif cmd == 'capture': capture(sys.argv[2])
    elif cmd == 'tree': print('Saved', sys.argv[3], len(tree(int(sys.argv[2]), sys.argv[3])), 'characters')
