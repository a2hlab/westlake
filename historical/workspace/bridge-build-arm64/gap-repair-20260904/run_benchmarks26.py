import subprocess,json
from pathlib import Path
root=Path(__file__).resolve().parent
common=['--candidate','candidate26','--activity','BenchmarkActivity','--control-apk','control24-out/control.apk','--spawn-helper','appspawn-x-v15','--art-helper','libart-v16.so','--private-runtime','--jit-anon-fallback','--network-helper','liboh_network_jni_v4.so','--popen-helper','liboh_popen_v24.so']
def run(script,*args):subprocess.run(['python3',str(root/script),*args],cwd=root,check=True)
for name in ['resume26bencha','resume26benchb']:
 run('launch_probe.py','--run',name,*common)
 try:run('measure_control.py',name)
 finally:run('stop_probe.py',name)
print('REPEATED_BENCHMARKS_COMPLETE',flush=True)
