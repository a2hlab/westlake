import subprocess,json,re
from pathlib import Path
import device
root=Path(__file__).resolve().parent
common=['--candidate','candidate26','--activity','BenchmarkActivity','--control-apk','control24-out/control.apk','--spawn-helper','appspawn-x-v15','--art-helper','libart-v27.so','--private-runtime','--jit-anon-fallback','--network-helper','liboh_network_jni_v4.so','--popen-helper','liboh_popen_v24.so']
def run(script,*args):subprocess.run(['python3',str(root/script),*args],cwd=root,check=True)
for name,file_cache in [('resume27filea',True),('resume27anona',False),('resume27fileb',True),('resume27anonb',False)]:
 run('launch_probe.py','--run',name,*common,*(['--jit-file-cache'] if file_cache else []))
 try:
  run('measure_control.py',name)
  state=json.loads((root/(name+'-processes.json')).read_text());pid=state['child']
  log=(root/(name+'-measure.log')).read_text()
  assert '[WESTLAKE-JIT-BOOT] result=1' in log
  maps=device.save(name+'-maps.log',f'cat /proc/{pid}/maps')
  device.save(name+'-identity.log',f'cat /proc/{pid}/status; cat /proc/{pid}/attr/current')
  if file_cache:
   assert 'using app-private unlinked file with dual RW/RX views' in log
   code=[line for line in maps.splitlines() if '/code_cache/art-volatile/' in line]
   assert code and all('rwx' not in line.split()[1] for line in code)
   assert any('r-xs' in line for line in code) and any('rw-s' in line for line in code)
  else:assert 'using ART anonymous cache with RWX updates' in log
  print('JIT_MAPPING_VERIFIED',name,'private-file' if file_cache else 'anonymous',flush=True)
 finally:run('stop_probe.py',name)
print('CONTROLLED_BACKING_COMPARISON_COMPLETE',flush=True)
