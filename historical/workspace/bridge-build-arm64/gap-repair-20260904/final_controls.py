"""Run independent final-candidate gates; stop only each owned diagnostic child."""
import json,subprocess,time
import device
ROOT=device.ROOT
def cmd(*args):subprocess.run(['python3',*args],cwd=ROOT,check=True,timeout=75)
def wait(run,markers,seconds=50):
    state=json.loads((ROOT/(run+'-processes.json')).read_text())
    remote=f'/data/service/el1/public/appspawnx/adapter_child_{state["child"]}.stderr'
    for _ in range(seconds):
        log=device.shell('cat '+remote)
        if 'Fatal signal' in log or '[GAP_IME_EDGE] FAIL' in log or '[GAP_PERMISSION] FAIL' in log or '[GAP_NET] FAIL' in log:
            break
        if all(marker in log for marker in markers):break
        time.sleep(1)
    (ROOT/(run+'-gate.log')).write_text(log)
    assert all(marker in log for marker in markers),'Gate incomplete: '+run
    assert 'Fatal signal' not in log and '[GAP_IME_EDGE] FAIL' not in log and '[GAP_NET] FAIL' not in log and '[GAP_PERMISSION] FAIL' not in log
    return state
cmd('launch_probe.py','--candidate','candidate13','--run','ime13','--activity','ImeEdgeActivity')
try:
    state=wait('ime13',['[GAP_IME_EDGE] COMPLETE failures=0'])
    device.tree(state['child'],'ime13-ui')
    print('FINAL_IME_GATE_PASS',flush=True)
finally:cmd('stop_probe.py','ime13')
cmd('launch_probe.py','--candidate','candidate13','--run','suite13','--activity','WebActivity')
try:
    state=wait('suite13',['[GAP_NET_STRESS] COMPLETE','[GAP_TRUST] PASS full-standard-chain',
        '[GAP_CERT] COMPLETE','[GAP_PERMISSION] PASS facade-and-unknown-denial','LOCAL WEBVIEW PASS'])
    device.shell('uinput -T -d 600 150 -i 30 -u 600 150')
    wait('suite13',['url=https://example.com/ title=Example Domain'])
    device.tree(state['child'],'suite13-ui');device.capture('suite13-online')
    print('FINAL_NETWORK_TLS_PERMISSION_WEB_GATE_PASS',flush=True)
finally:cmd('stop_probe.py','suite13')
