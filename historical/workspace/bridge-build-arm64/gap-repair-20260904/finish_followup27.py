"""Record the clean ready launch, preserved baselines and live USB networking."""
import base64,hashlib,json,re,subprocess,time
import device
r=device.ROOT
s=json.loads((r/'resume27ready-processes.json').read_text());pid=s['child']
device.shell('power-shell wakeup; aa start -b org.westlake.imehost -a EntryAbility')
for attempt in range(6):
    tree=device.tree(pid,'resume27ready-feed')
    if 'FeedTitleTextView' in tree:break
    time.sleep(2)
else:raise AssertionError('Clean launch has no populated feed tree')
log=device.save('resume27ready-child.log',f'cat /data/service/el1/public/appspawnx/adapter_child_{pid}.stderr')
assert 'Fatal signal' not in log
assert '[WESTLAKE-JIT-BOOT] result=1' in log
assert 'using app-private unlinked file with dual RW/RX views' in log
assert '[OH_POPEN]' not in log
maps=device.save('resume27ready-maps.log',f'cat /proc/{pid}/maps')
views=[line.split() for line in maps.splitlines() if '/code_cache/art-volatile/' in line]
assert any(x[1]=='r-xs' for x in views) and any(x[1]=='rw-s' for x in views)
assert len({x[4] for x in views})==1 and all('rwx' not in x[1] for x in views)
env=device.save('resume27ready-debug-settings.log',f"grep -azE '^WESTLAKE_(POPEN_TRACE|WEBVIEW_DEBUG)=' /proc/{s['parent']}/environ")
assert not env.strip(),'Debug mode enabled in ready launch'
device.save('resume27ready-windows.log',"hidumper -s WindowManagerService -a '-a'")
preserve=device.save('resume27-final-preservation.log',f'''getenforce
md5sum /data/local/tmp/asx/libart.so /data/local/tmp/asx/toutiao.apk
stat -c '%i %u %g %a' /data/data/com.ss.android.article.news /data/app/el2/100/base/org.westlake.imehost/files/android-toutiao-data
cat /proc/{pid}/attr/current
grep -E '^(Uid|Gid|Cap)' /proc/{pid}/status
cat /proc/{pid}/cgroup
test -r /proc/{pid}/maps && echo READY_CHILD_RUNNING
ls -l /proc/[0-9]*/exe 2>/dev/null | grep -E '/(appspawn-x|hiperf|strace)' ''')
assert 'Enforcing' in preserve and 'u:r:normal_hap:s0' in preserve
assert 'a5436670f182aa3e24fde289ed38a0cd' in preserve
for kind in ['Inh','Prm','Eff','Bnd','Amb']:assert re.search(r'Cap'+kind+r':\s+0000000000000000',preserve)
assert '49251 ' in preserve and '50270 ' in preserve
assert device.shell(f'stat -c %i /proc/{pid}/root/data/data/'+s['package']).strip()==device.shell('stat -c %i '+s['private_data']).strip()
(r/'resume27-forward-ports-final.log').write_text(device.run(['fport','ls']))
assert '9222' not in (r/'resume27-forward-ports-final.log').read_text()
device.save('resume27-power-restore-command.log','power-shell timeout -o 30000; power-shell timeout -r')
power=device.save('resume27-power-restored.log','power-shell dump -s')
assert 'Timeout=30000ms' in power and 'OverrideTimeout=' not in power
adb=['/mnt/c/Users/dspfa/Dev/platform-tools/adb.exe','-s','cfb7c9e3']
def android(name,cmd):
    p=subprocess.run(adb+['shell',cmd],cwd=r,capture_output=True,timeout=30)
    assert p.returncode==0,p.stderr
    out=p.stdout.decode(errors='replace').replace('\r','');(r/name).write_text(out);return out
vpn=android('oneplus27-final-connectivity.log','dumpsys connectivity')
assert any('Transports: VPN' in x and 'VALIDATED' in x and 'VPN CONNECTED' in x for x in vpn.splitlines())
android_pid=android('oneplus27-final-toutiao-pid.log','pidof com.ss.android.article.news').strip();assert android_pid.isdigit()
paths=android('oneplus27-final-apk-path.log','pm path com.ss.android.article.news').splitlines()
base=[x.removeprefix('package:') for x in paths if x.endswith('/base.apk')];assert len(base)==1
assert re.fullmatch(r'/[A-Za-z0-9_./=+~-]+',base[0])
digest=android('oneplus27-final-apk-sha256.log','sha256sum '+base[0]).split()[0]
assert digest==hashlib.sha256((r.parent/'toutiao.apk').read_bytes()).hexdigest()
ps="[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false); Get-Process -Name gnirehtet -ErrorAction Stop | Select-Object Id,ProcessName,StartTime | ConvertTo-Json -Compress"
p=subprocess.run(['/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe','-NoProfile','-EncodedCommand',base64.b64encode(ps.encode('utf-16le')).decode()],capture_output=True,timeout=30)
assert p.returncode==0,p.stderr
(r/'resume27-final-host-relay.json').write_bytes(p.stdout)
print('READY_VERIFIED',json.dumps(dict(oh_child=pid,oh_parent=s['parent'],android_child=int(android_pid),toutiao_sha256=digest,relay=json.loads(p.stdout.decode('utf-8-sig')))))
