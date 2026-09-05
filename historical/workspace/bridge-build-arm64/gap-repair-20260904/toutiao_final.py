"""Fresh-data startup and unchanged-APK copied-profile regression, no consent changes."""
import json,subprocess,time
import device
ROOT=device.ROOT
def cmd(*args):subprocess.run(['python3',*args],cwd=ROOT,check=True,timeout=90)
def state(run):return json.loads((ROOT/(run+'-processes.json')).read_text())
cmd('launch_probe.py','--candidate','candidate13','--run','egl13fallback','--activity','BenchmarkActivity','--egl-no-damage')
try:
    s=state('egl13fallback');remote=f'/data/service/el1/public/appspawnx/adapter_child_{s["child"]}.stderr'
    for _ in range(40):
        log=device.shell('cat '+remote)
        if 'PAGE_COMPLETE samples=12' in log or 'Fatal signal' in log:break
        time.sleep(1)
    (ROOT/'egl13fallback-gate.log').write_text(log)
    assert 'PAGE_COMPLETE samples=12' in log and '[OH_EGL] swap mode=full' in log and 'Fatal signal' not in log
    print('FINAL_EGL_FALLBACK_PASS',flush=True)
finally:cmd('stop_probe.py','egl13fallback')
cmd('launch_probe.py','--candidate','candidate13','--run','fresh13','--app','toutiao')
try:
    s=state('fresh13')
    for _ in range(15):
        log=device.save('fresh13-current.log',f'/system/bin/cat /data/service/el1/public/appspawnx/adapter_child_{s["child"]}.stderr')
        assert 'Fatal signal' not in log
        tree=device.tree(s['child'],'fresh13-ui')
        if '个人信息保护指引' in tree:break
        time.sleep(1)
    # Feed/network activity before accepting the app's privacy terms is not
    # a valid fresh-install requirement. Do not accept consent in this probe.
    assert '个人信息保护指引' in tree and 'DecorView' in tree
    device.capture('fresh13-consent')
    print('FINAL_FRESH_STARTUP_PASS_NO_CONSENT_ACCEPTED',flush=True)
finally:cmd('stop_probe.py','fresh13')
cmd('launch_probe.py','--candidate','candidate13','--run','toutiao13','--app','toutiao','--snapshot-accepted-data')
print('TOUTIAO_COPIED_PROFILE_RUNNING',state('toutiao13'),flush=True)
