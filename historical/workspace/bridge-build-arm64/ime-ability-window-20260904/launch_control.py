#!/usr/bin/env python3
"""Development harness: use the dedicated host's real UID/token/window."""
import json
import argparse
import re
import struct
import time
import device
ROOT = device.ROOT
HOST = 'org.westlake.imehost'
parser = argparse.ArgumentParser()
parser.add_argument('--app', choices=['control', 'toutiao'], default='control')
parser.add_argument('--run', default='control04')
parser.add_argument('--candidate', default='candidate04')
args = parser.parse_args()
assert re.fullmatch(r'candidate[0-9]{2,}', args.candidate)
BASE = device.REMOTE + '/' + args.candidate
socket = '/dev/unix/socket/AppSpawnXImeHost' + args.candidate.removeprefix('candidate')
assert re.fullmatch(r'[a-z0-9-]+', args.run)
assert not (ROOT / (args.run + '-processes.json')).exists(), 'Choose a new evidence run name'
pid_text = device.shell('pidof ' + HOST).strip()
assert re.fullmatch(r'[0-9]+', pid_text), 'Exactly one live dedicated host required'
host_pid = int(pid_text)
bundle_text = device.save(args.run + '-launch-bundle.json', 'bm dump -n ' + HOST)
bundle = json.loads(bundle_text[bundle_text.index('{'):])
app = bundle['applicationInfo']
uid, token = app['uid'], app['accessTokenIdEx']
assert uid > 20000000 and token > 0 and app['bundleName'] == HOST
status = device.shell(f'cat /proc/{host_pid}/status')
assert int(re.search(r'^Uid:\s+(\d+)', status, re.M)[1]) == uid
state_text = device.save(args.run + '-launch-window.json', f'cat /proc/{host_pid}/root/data/storage/el2/base/haps/entry/files/window-state.json')
state = json.loads(state_text)
assert state['event'] in ('window-ready', 'stage-event'), state
window = state['details'].get('window', state['details'])
window_id = int(window['id'])
assert window_id > 0 and window['type'] == 1 and window['focusable']
if socket in device.shell('ls ' + socket + ' 2>/dev/null'):
    assert socket not in device.shell('cat /proc/net/unix'), 'Candidate socket is still live'
    # This exact test socket remains after SIGTERM; never remove a live socket.
    device.shell('rm ' + socket)
def tlv(kind, payload):
    payload += bytes((-len(payload)) % 4)
    return struct.pack('<HH', len(payload) + 4, kind) + payload
body = tlv(0, struct.pack('<I', 0) + HOST.encode() + b'\0')
body += tlv(2, struct.pack('<III64I64s', uid, uid, 1, 3099, *([0]*63), b'imehost'))
body += tlv(5, struct.pack('<Q', token))
body += tlv(3, struct.pack('<I', 0) + b'normal\0')
# Actual packed source structure is 276 bytes, despite old comments saying 280.
request = struct.pack('<5I256s', 0xef201234, 0, 276 + len(body), 0x494d4501, 4, HOST.encode()) + body
(ROOT / 'host-request.bin').write_bytes(request)
device.send('host-request.bin', BASE + '/host-request.bin')
# Never change ownership of an existing Android app's data.
setup = f'''for d in /data/data/org.westlake.imenetcontrol /data/data/{HOST}; do
if [ ! -e "$d" ]; then mkdir "$d" && chown {uid}:{uid} "$d" && chmod 700 "$d"; fi
done'''
device.shell(setup)
if args.app == 'toutiao':
    apk = '/data/local/tmp/asx/toutiao.apk'
    assert device.shell('md5sum ' + apk).split()[0] == 'cc99caaaa3baffbb100118753fa1d130'
    package, activity = 'com.ss.android.article.news', 'com.ss.android.article.news.activity.MainActivity'
    launch = (f'{BASE}/private-data-launch {uid} '
        '/data/app/el2/100/base/org.westlake.imehost/files/android-toutiao-data '
        f'/data/data/{package} sh {BASE}/run.sh')
else:
    package, activity = 'org.westlake.imenetcontrol', 'org.westlake.imenetcontrol.MainActivity'
    apk = '/data/local/tmp/asx/ime-network-fix-20260904/control.apk'
    launch = f'sh {BASE}/run.sh'
parent_log = f'{BASE}/{args.run}-parent.log'
cmd = (f'WL_PARENT_ID={window_id} ASX_LAUNCH_PKG={package} '
    f'ASX_LAUNCH_ACTIVITY={activity} ASX_APK_PATH={apk} '
    f'nohup {launch} >{parent_log} 2>&1 </dev/null & echo $!')
parent = int(device.shell(cmd).strip())
for _ in range(60):
    ready = device.shell(f'test -S {socket} && echo SOCKET_READY; tail -3 {parent_log}')
    if 'SOCKET_READY' in ready: break
    time.sleep(.5)
else: raise RuntimeError(ready)
result = device.save(args.run + '-spawn.log', f'{BASE}/host-spawn {socket} {BASE}/host-request.bin')
match = re.search(r'HOST_SPAWN result=0 pid=(\d+)', result)
assert match, result
state = dict(host_pid=host_pid, host_uid=uid, host_token=token, window_id=window_id, parent=parent, child=int(match[1]))
(ROOT / (args.run + '-processes.json')).write_text(json.dumps(state, indent=2) + '\n')
print(state)
