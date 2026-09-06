import subprocess,time,re,json,hashlib
from pathlib import Path
r=Path(__file__).resolve().parent
adb=['/mnt/c/Users/dspfa/Dev/platform-tools/adb.exe','-s','cfb7c9e3']
def call(*args):
 p=subprocess.run(adb+list(args),cwd=r,capture_output=True,timeout=30);assert p.returncode==0,p.stderr
 return p.stdout.decode(errors='replace').replace('\r','')
assert 'Success' in call('install','-r','control25-out/control.apk')
call('shell','am force-stop org.westlake.gapcontrol')
call('shell','input keyevent KEYCODE_WAKEUP')
print(call('shell','am start -W -n org.westlake.gapcontrol/.KeepScreenActivity'),flush=True)
pid=call('shell','pidof org.westlake.gapcontrol').strip();assert pid.isdigit()
(r/'oneplus25window.json').write_text(json.dumps(dict(pid=int(pid),sha256=hashlib.sha256((r/'control25-out/control.apk').read_bytes()).hexdigest())))
seen=set();start=time.monotonic()
while time.monotonic()-start<106:
 log=call('logcat','-d','--pid='+pid,'-v','brief');phases=re.findall(r'\[WINDOW_REF\] phase=(\w+)',log);phase=phases[-1] if phases else None
 if phase and phase not in seen:
  time.sleep(.7);seen.add(phase)
  (r/('oneplus25window-'+phase+'-power.log')).write_text(call('shell','dumpsys power'))
  print(phase,flush=True)
 if time.monotonic()-start>53 and 'hold_check' not in seen:
  seen.add('hold_check');(r/'oneplus25window-idle-held-power.log').write_text(call('shell','dumpsys power'))
 if time.monotonic()-start>98:
  (r/'oneplus25window-idle-released-power.log').write_text(call('shell','dumpsys power'));break
 time.sleep(1)
(r/'oneplus25window-child.log').write_text(log)
print('COMPLETE',flush=True)
