"""Launch one isolated control process, preserving all prior parent/child processes."""
from pathlib import Path
import argparse,json,re,struct,time,hashlib
import device
ROOT=device.ROOT
p=argparse.ArgumentParser();p.add_argument('--run',required=True);p.add_argument('--candidate');p.add_argument('--activity',default='WebActivity');p.add_argument('--app',choices=['control','toutiao'],default='control');p.add_argument('--snapshot-accepted-data',action='store_true');p.add_argument('--webview-order-fix',action='store_true');p.add_argument('--network-helper');p.add_argument('--geometry-helper');p.add_argument('--hardware-functor',action='store_true');p.add_argument('--egl-no-damage',action='store_true');args=p.parse_args()
assert re.fullmatch('[a-z0-9-]+',args.run)
assert not (ROOT/(args.run+'-processes.json')).exists()
reference=ROOT.parent/'ime-ability-window-20260904/candidate04'
if args.candidate:
    assert re.fullmatch('candidate[0-9]+',args.candidate)
    reference=ROOT/args.candidate
assert re.fullmatch('[A-Za-z]+Activity',args.activity)
remote='/data/local/tmp/asx/gap-repair-20260904/'+args.run
socket='/dev/unix/socket/AppSpawnXGap'+args.run.replace('-','')
assert socket not in device.shell('cat /proc/net/unix')
assert not device.shell('test -e '+socket+' && echo EXISTS').strip()
host='org.westlake.imehost';pid=int(device.shell('pidof '+host).strip())
raw=device.save(args.run+'-bundle.json','bm dump -n '+host)
app=json.loads(raw[raw.index('{'):])['applicationInfo'];uid=app['uid'];token=app['accessTokenIdEx']
status=device.shell(f'cat /proc/{pid}/status')
assert int(re.search(r'^Uid:\s+(\d+)',status,re.M)[1])==uid and uid>20000000
state=json.loads(device.shell(f'cat /proc/{pid}/root/data/storage/el2/base/haps/entry/files/window-state.json'))
window=state['details'].get('window',state['details']);wid=int(window['id']);assert wid>0
device.shell(f'mkdir -p {remote}')
runner=(reference/'run.sh').read_text().replace('export ASX_SOCKET_NAME=AppSpawnXImeHost04','export ASX_SOCKET_NAME='+socket.rsplit('/',1)[1])
if args.egl_no_damage:
    assert args.candidate and (reference/'runtime/liboh_egl_boundary.so').exists()
    runner=runner.replace('set -eu','set -eu\nexport WESTLAKE_EGL_TEST_NO_DAMAGE=1',1)
if args.geometry_helper:
    assert re.fullmatch('liboh_ime_geometry_v[0-9]+.so',args.geometry_helper)
    assert not args.webview_order_fix
    helper=ROOT/args.geometry_helper;device.send(helper.name,remote+'/'+helper.name)
    common=(reference/'run-common.sh').read_text()
    assert common.count('$RUNTIME/liboh_ime_geometry.so')==1
    common=common.replace('$RUNTIME/liboh_ime_geometry.so',remote+'/'+helper.name)
    (ROOT/(args.run+'-run-common.sh')).write_text(common)
    device.send(args.run+'-run-common.sh',remote+'/run-common.sh')
    runner=runner.replace('exec sh "$BASE/run-common.sh"','expect_sha '+hashlib.sha256(helper.read_bytes()).hexdigest()+' '+remote+'/'+helper.name+'\nexec sh '+remote+'/run-common.sh')
if args.hardware_functor:
    assert args.webview_order_fix and args.candidate
    helper=ROOT/'liboh_hwui_functor.so';device.send(helper.name,remote+'/'+helper.name)
    needle='export WESTLAKE_USE_BC_DIGESTS=1 WESTLAKE_WEBVIEW_SOFTWARE_FALLBACK=1';assert needle in runner
    runner=runner.replace(needle,'expect_sha '+hashlib.sha256(helper.read_bytes()).hexdigest()+' '+remote+'/'+helper.name+'\nexpect_sha '+hashlib.sha256((ROOT/'baseline-libhwui.so').read_bytes()).hexdigest()+' /data/local/tmp/asx/libhwui.so\nexport WESTLAKE_USE_BC_DIGESTS=1 WESTLAKE_WEBVIEW_SOFTWARE_FALLBACK=0\nexport WESTLAKE_HWUI_HELPER_PATH='+remote+'/'+helper.name)
if args.network_helper:
    assert re.fullmatch('liboh_network_jni_v[0-9]+.so',args.network_helper)
    helper=ROOT/args.network_helper;device.send(helper.name,remote+'/'+helper.name)
    needle='export WESTLAKE_NET_HELPER_PATH=$BASE/runtime/liboh_network_jni.so';assert needle in runner
    runner=runner.replace(needle,'expect_sha '+hashlib.sha256(helper.read_bytes()).hexdigest()+' '+remote+'/'+helper.name+'\nexport WESTLAKE_NET_HELPER_PATH='+remote+'/'+helper.name)
if args.webview_order_fix:
    helper=ROOT/'liboh_webview_startup_order.so'
    device.send(helper.name,remote+'/'+helper.name)
    common=(reference/'run-common.sh').read_text().replace('export LD_PRELOAD=','export LD_PRELOAD='+remote+'/'+helper.name+':')
    (ROOT/(args.run+'-run-common.sh')).write_text(common)
    device.send(args.run+'-run-common.sh',remote+'/run-common.sh')
    needle='exec sh "$BASE/run-common.sh"';assert needle in runner
    runner=runner.replace(needle,'expect_sha '+hashlib.sha256(helper.read_bytes()).hexdigest()+' '+remote+'/'+helper.name+'\nexec sh '+remote+'/run-common.sh')
(ROOT/(args.run+'-run.sh')).write_text(runner)
device.send(args.run+'-run.sh',remote+'/run.sh')
device.send('control-out/control.apk',remote+'/control.apk')
def tlv(kind,payload):
    payload+=bytes((-len(payload))%4);return struct.pack('<HH',len(payload)+4,kind)+payload
body=tlv(0,struct.pack('<I',0)+host.encode()+b'\0')
body+=tlv(2,struct.pack('<III64I64s',uid,uid,1,3099,*([0]*63),b'imehost'))
body+=tlv(5,struct.pack('<Q',token))+tlv(3,struct.pack('<I',0)+b'normal\0')
(ROOT/(args.run+'-request.bin')).write_bytes(struct.pack('<5I256s',0xef201234,0,276+len(body),0x47415001,4,host.encode())+body)
device.send(args.run+'-request.bin',remote+'/request.bin')
data='/data/data/org.westlake.gapcontrol'
result=device.shell(f'if [ ! -e {data} ]; then mkdir {data} && chown {uid}:{uid} {data} && chmod 700 {data}; fi; stat -c "%u %g %a" {data}')
assert result.strip()==f'{uid} {uid} 700',result
webdata=f'{data}/webview-'+args.run
device.shell(f'mkdir {webdata} && chown {uid}:{uid} {webdata} && chmod 700 {webdata}')
package='org.westlake.gapcontrol';activity=package+'.'+args.activity;apk=remote+'/control.apk'
launch='sh '+remote+'/run.sh';private=None
if args.app=='toutiao':
    package='com.ss.android.article.news';activity=package+'.activity.MainActivity';apk='/data/local/tmp/asx/toutiao.apk'
    assert device.shell('md5sum '+apk).split()[0]=='cc99caaaa3baffbb100118753fa1d130'
    private='/data/app/el2/100/base/'+host+'/files/android-toutiao-gap-'+args.run
    assert 'EXISTS' not in device.shell('test -e '+private+' && echo EXISTS')
    result=device.shell(f'mkdir {private} && chown {uid}:{uid} {private} && chmod 700 {private} && stat -c "%u %g %a" {private}')
    assert result.strip()==f'{uid} {uid} 700',result
    if args.snapshot_accepted_data:
        # This experiment's known baseline only. Freeze briefly for a coherent
        # SQLite/WAL copy; shell trap resumes it even if copying fails.
        source='/data/app/el2/100/base/'+host+'/files/android-toutiao-data'
        assert device.shell('cat /proc/22641/cmdline').rstrip('\0\n')=='/data/local/tmp/asx/ime-ability-window-20260904/candidate04/runtime/appspawn-x'
        assert device.shell('stat -c %i /proc/22641/root/data/data/'+package).strip()==device.shell('stat -c %i '+source).strip()
        assert apk in device.shell('cat /proc/22641/maps')
        copied=device.save(args.run+'-snapshot.log',f"trap 'kill -CONT 22641' EXIT HUP INT TERM; kill -STOP 22641 && timeout 45 cp -a {source}/. {private}/ && echo SNAPSHOT_OK")
        assert 'SNAPSHOT_OK' in copied,copied
        assert 'State:\tT' not in device.shell('cat /proc/22641/status')
    launch=f'/data/local/tmp/asx/ime-ability-window-20260904/candidate04/private-data-launch {uid} {private} /data/data/{package} '+launch
cmd=f'ASX_SOCKET_NAME={socket.rsplit("/",1)[1]} WL_PARENT_ID={wid} ASX_LAUNCH_PKG={package} ASX_LAUNCH_ACTIVITY={activity} ASX_APK_PATH={apk} ASX_WEBVIEW_DATA_DIR={webdata} nohup {launch} >{remote}/parent.log 2>&1 </dev/null & echo $!'
parent=int(device.shell(cmd).strip())
for _ in range(60):
    if 'READY' in device.shell(f'test -S {socket} && echo READY'):break
    time.sleep(.5)
else:raise RuntimeError(device.shell('tail -n 60 '+remote+'/parent.log'))
result=device.save(args.run+'-spawn.log',f'/data/local/tmp/asx/ime-ability-window-20260904/candidate04/host-spawn {socket} {remote}/request.bin')
match=re.search(r'HOST_SPAWN result=0 pid=(\d+)',result);assert match,result
runtime=('/data/local/tmp/asx/gap-repair-20260904/'+args.candidate if args.candidate else '/data/local/tmp/asx/ime-ability-window-20260904/candidate04')+'/runtime/appspawn-x'
state=dict(parent=parent,child=int(match[1]),socket=socket,host_pid=pid,host_uid=uid,window=wid,remote=remote,runtime=runtime,apk=apk,package=package,private_data=private)
(ROOT/(args.run+'-processes.json')).write_text(json.dumps(state,indent=2)+'\n');print(state)
