"""Briefly suspend this experiment's identified baseline; always resume it."""
import argparse,re,subprocess,time
import device
p=argparse.ArgumentParser();p.add_argument('run');p.add_argument('--candidate',default='candidate05');a=p.parse_args()
assert re.fullmatch('[a-z0-9-]+',a.run)
assert re.fullmatch('candidate[0-9]+',a.candidate)
assert device.shell('cat /proc/22641/cmdline').rstrip('\0\n')=='/data/local/tmp/asx/ime-ability-window-20260904/candidate04/runtime/appspawn-x'
assert 'PPid:\t22507' in device.shell('cat /proc/22641/status')
assert '/data/local/tmp/asx/toutiao.apk' in device.shell('cat /proc/22641/maps')
# A device-side watchdog resumes the exact already-validated process even if
# the host tool/session disconnects. Its maximum suspension is 55 seconds.
watchdog=device.shell("nohup sh -c 'trap \"kill -CONT 22641\" EXIT HUP INT TERM; sleep 55' >/data/local/tmp/asx/gap-repair-20260904/baseline-resume-watchdog.log 2>&1 </dev/null & echo $!").strip()
assert watchdog.isdigit()
device.save(a.run+'-baseline-pause.log','kill -STOP 22641; cat /proc/22641/status | head -8')
try:
    subprocess.run(['python3','launch_probe.py','--candidate',a.candidate,'--activity','BenchmarkActivity','--run',a.run],cwd=device.ROOT,check=True,timeout=35)
    subprocess.run(['python3','measure_control.py',a.run],cwd=device.ROOT,check=True,timeout=40)
    status=device.save(a.run+'-baseline-pause-end.log','cat /proc/22641/status | head -8')
    assert 'State:\tT' in status,'Watchdog resumed baseline during measurement; do not label isolated'
finally:
    device.save(a.run+'-baseline-resumed.log','kill -CONT 22641; cat /proc/22641/status | head -8')
    command=device.shell('cat /proc/'+watchdog+'/cmdline 2>/dev/null')
    if 'kill -CONT 22641' in command and 'sleep 55' in command:
        device.shell('kill -TERM '+watchdog)
