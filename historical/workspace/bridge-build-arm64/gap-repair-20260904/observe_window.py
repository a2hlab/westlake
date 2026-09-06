import sys,time,json,re
import device
run=sys.argv[1]
for _ in range(80):
 p=device.ROOT/(run+'-processes.json')
 if p.exists():break
 time.sleep(.5)
s=json.loads(p.read_text());pid=s['child'];seen=set();start=time.monotonic()
while time.monotonic()-start<110:
 log=device.shell(f'cat /data/service/el1/public/appspawnx/adapter_child_{pid}.stderr')
 phases=re.findall(r'\[WINDOW_REF\] phase=(\w+)',log)
 phase=phases[-1] if phases else None
 if phase and phase not in seen:
  time.sleep(.7)
  seen.add(phase)
  device.save(run+'-'+phase+'-locks.log','power-shell dump -r')
  device.save(run+'-'+phase+'-power.log','power-shell dump -s')
  device.save(run+'-'+phase+'-windows.log',"hidumper -s WindowManagerService -a '-a'")
  print(phase,flush=True)
 if 'idle_hold_start' in seen and time.monotonic()-start>52 and 'hold_check' not in seen:
  seen.add('hold_check');device.save(run+'-idle-held-power.log','power-shell dump -s')
 if 'idle_release' in seen and time.monotonic()-start>95 and 'release_check' not in seen:
  seen.add('release_check');device.save(run+'-idle-released-power.log','power-shell dump -s')
  break
 if 'Fatal signal' in log or 'UnsatisfiedLinkError' in log:print('FAIL runtime marker',flush=True);break
 time.sleep(1)
(device.ROOT/(run+'-observed-child.log')).write_text(log)
print('phases',sorted(seen),flush=True)
