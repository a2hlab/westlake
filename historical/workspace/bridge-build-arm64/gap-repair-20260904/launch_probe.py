"""Launch one isolated control process, preserving all prior parent/child processes."""
from pathlib import Path
import argparse,json,re,struct,time,hashlib
import device
ROOT=device.ROOT
p=argparse.ArgumentParser();p.add_argument('--run',required=True);p.add_argument('--candidate');p.add_argument('--activity',default='WebActivity');p.add_argument('--app',choices=['control','toutiao'],default='control');p.add_argument('--snapshot-accepted-data',action='store_true');p.add_argument('--snapshot-pid',type=int);p.add_argument('--spawn-helper');p.add_argument('--art-helper');p.add_argument('--private-runtime',action='store_true');p.add_argument('--private-runtime-version',choices=['17','18','19'],default='19');p.add_argument('--jit-anon-fallback',action='store_true');p.add_argument('--jit-file-cache',action='store_true');p.add_argument('--webview-debug',action='store_true');p.add_argument('--interpreter',action='store_true');p.add_argument('--no-jit',action='store_true');p.add_argument('--webview-order-fix',action='store_true');p.add_argument('--network-helper');p.add_argument('--popen-helper');p.add_argument('--popen-trace',action='store_true');p.add_argument('--control-apk',default='control-out/control.apk');p.add_argument('--geometry-helper');p.add_argument('--hardware-functor',action='store_true');p.add_argument('--egl-no-damage',action='store_true');args=p.parse_args()
assert re.fullmatch('[a-z0-9-]+',args.run)
assert not (args.interpreter and args.no_jit)
assert not args.art_helper or args.spawn_helper
assert not args.popen_helper or args.spawn_helper
assert not args.jit_anon_fallback or args.art_helper
assert not args.jit_file_cache or (args.jit_anon_fallback and args.private_runtime)
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
host='org.westlake.imehost'
device.save(args.run+'-host-wakeup.log','power-shell wakeup')
foreground=device.save(args.run+'-host-foreground.log','aa start -b '+host+' -a EntryAbility')
assert 'start ability successfully' in foreground
pid=int(device.shell('pidof '+host).strip())
raw=device.save(args.run+'-bundle.json','bm dump -n '+host)
app=json.loads(raw[raw.index('{'):])['applicationInfo'];uid=app['uid'];token=app['accessTokenIdEx']
status=device.shell(f'cat /proc/{pid}/status')
assert int(re.search(r'^Uid:\s+(\d+)',status,re.M)[1])==uid and uid>20000000
state=json.loads(device.shell(f'cat /proc/{pid}/root/data/storage/el2/base/haps/entry/files/window-state.json'))
window=state['details'].get('window',state['details']);wid=int(window['id']);assert wid>0
for _ in range(20):
    windows=device.shell("hidumper -s WindowManagerService -a '-a'")
    focused=re.search(r'^Focus window:\s*(\d+)',windows,re.M)
    if focused:
        focus_id=int(focused[1])
        if focus_id==wid:break
        # An existing Android child of this host may regain focus with it.
        owners=[line.split()[2] for line in windows.splitlines()
                if len(line.split())>=4 and line.split()[2].isdigit()
                and line.split()[3]==str(focus_id)]
        if len(owners)==1:
            owner_status=device.shell('cat /proc/'+owners[0]+'/status')
            owner_uid=re.search(r'^Uid:\s+(\d+)',owner_status,re.M)
            if owner_uid and int(owner_uid[1])==uid:break
    time.sleep(.2)
else:raise RuntimeError('Host window is not focused; device may still be locked')
(ROOT/(args.run+'-host-windows.log')).write_text(windows)
device.shell(f'mkdir -p {remote}')
runner=(reference/'run.sh').read_text().replace('export ASX_SOCKET_NAME=AppSpawnXImeHost04','export ASX_SOCKET_NAME='+socket.rsplit('/',1)[1])
if args.popen_trace:
    assert args.popen_helper
    runner=runner.replace('set -eu','set -eu\nexport WESTLAKE_POPEN_TRACE=1',1)
if args.webview_debug:
    runner=runner.replace('set -eu','set -eu\nexport WESTLAKE_WEBVIEW_DEBUG=1',1)
if args.jit_file_cache:
    cache_package='com.ss.android.article.news' if args.app=='toutiao' else 'org.westlake.gapcontrol'
    runner=runner.replace('set -eu','set -eu\nexport WESTLAKE_OH_JIT_FILE_CACHE_DIR=/data/data/'+cache_package+'/code_cache/art-volatile',1)
if args.jit_anon_fallback:
    runner=runner.replace('set -eu','set -eu\nexport WESTLAKE_OH_JIT_ANON_FALLBACK=1',1)
if args.interpreter or args.no_jit:
    runner=runner.replace('set -eu','set -eu\nexport APPSPAWNX_FORCE_JIT=0 APPSPAWNX_NO_JIT=1'+(' APPSPAWNX_FORCE_INT=1' if args.interpreter else ''),1)
if args.spawn_helper:
    assert re.fullmatch('appspawn-x-v[0-9]+',args.spawn_helper)
    assert not (args.webview_order_fix or args.geometry_helper or args.hardware_functor)
    helper=ROOT/args.spawn_helper;device.send(helper.name,remote+'/'+helper.name)
    device.shell('chmod 755 '+remote+'/'+helper.name)
    common=(reference/'run-common.sh').read_text()
    if args.popen_helper:
        assert re.fullmatch(r'liboh_popen_v[0-9]+\.so',args.popen_helper)
        popen_helper=ROOT/args.popen_helper
        device.send(args.popen_helper,remote+'/'+args.popen_helper)
        assert common.count('export LD_PRELOAD=')==1
        common=common.replace('export LD_PRELOAD=','export LD_PRELOAD='+remote+'/'+args.popen_helper+':')
        runner=runner.replace('exec sh "$BASE/run-common.sh"','expect_sha '+hashlib.sha256(popen_helper.read_bytes()).hexdigest()+' '+remote+'/'+args.popen_helper+'\nexec sh "$BASE/run-common.sh"')
    assert common.count('$RUNTIME/appspawn-x')==2
    common=common.replace('$RUNTIME/appspawn-x',remote+'/'+helper.name)
    if args.art_helper:
        assert re.fullmatch(r'libart-v[0-9]+[a-z]*\.so',args.art_helper)
        art_helper=ROOT/args.art_helper
        device.send(art_helper.name,remote+'/libart.so')
        marker='if [ "${WL_PTRACE_TRACE:-0}" = "1" ]; then'
        assert common.count(marker)==1
        common=common.replace(marker,'export LD_LIBRARY_PATH='+remote+':$LD_LIBRARY_PATH\n'+marker)
        runner=runner.replace('exec sh "$BASE/run-common.sh"','expect_sha '+hashlib.sha256(art_helper.read_bytes()).hexdigest()+' '+remote+'/libart.so\nexec sh "$BASE/run-common.sh"')
    (ROOT/(args.run+'-run-common.sh')).write_text(common)
    device.send(args.run+'-run-common.sh',remote+'/run-common.sh')
    runner=runner.replace('exec sh "$BASE/run-common.sh"','expect_sha '+hashlib.sha256(helper.read_bytes()).hexdigest()+' '+remote+'/'+helper.name+'\nexec sh '+remote+'/run-common.sh')
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
control_path=(ROOT/args.control_apk).resolve();assert control_path.is_relative_to(ROOT) and control_path.suffix=='.apk'
device.send(str(control_path.relative_to(ROOT)),remote+'/control.apk')
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
        # A reboot invalidates recorded PIDs. Require a currently verified owner,
        # or prove that no Android runtime is running before an offline copy.
        source='/data/app/el2/100/base/'+host+'/files/android-toutiao-data'
        source_inode=device.shell('stat -c %i '+source).strip()
        assert source_inode.isdigit()
        if args.snapshot_pid:
            owner=args.snapshot_pid;assert owner>1
            assert device.shell(f'cat /proc/{owner}/cmdline').rstrip('\0\n')=='/data/local/tmp/asx/ime-ability-window-20260904/candidate04/runtime/appspawn-x'
            assert device.shell(f'stat -c %i /proc/{owner}/root/data/data/'+package).strip()==source_inode
            assert apk in device.shell(f'cat /proc/{owner}/maps')
            copied=device.save(args.run+'-snapshot.log',f"trap 'kill -CONT {owner}' EXIT HUP INT TERM; kill -STOP {owner} && timeout 45 cp -a {source}/. {private}/ && echo SNAPSHOT_OK")
            assert 'State:\tT' not in device.shell(f'cat /proc/{owner}/status')
        else:
            assert not device.shell("ls -l /proc/[0-9]*/exe 2>/dev/null | grep '/appspawn-x'").strip(),'Live runtime exists; provide its verified --snapshot-pid'
            copied=device.save(args.run+'-snapshot.log',f'timeout 45 cp -a {source}/. {private}/ && echo SNAPSHOT_OK_OFFLINE')
            assert not device.shell("ls -l /proc/[0-9]*/exe 2>/dev/null | grep '/appspawn-x'").strip(),'Runtime started during offline snapshot'
        assert 'SNAPSHOT_OK' in copied,copied
        assert device.shell('stat -c %i '+source).strip()==source_inode
    launch=f'/data/local/tmp/asx/ime-ability-window-20260904/candidate04/private-data-launch {uid} {private} /data/data/{package} '+launch
if args.private_runtime:
    runtime_source='/data/app/el2/100/base/'+host+'/files/android-runtime-gap-17'
    assert device.shell('stat -c %u '+runtime_source).strip()==str(uid)
    runtime_run=runtime_source+'/gap-repair-20260904/'+args.run
    assert 'EXISTS' not in device.shell('test -e '+runtime_run+' && echo EXISTS')
    copied=device.save(args.run+'-private-runtime-copy.log',f'mkdir {runtime_run} && cp -R {remote}/. {runtime_run}/ && chown -R {uid}:{uid} {runtime_run} && echo PRIVATE_RUN_COPY_OK')
    assert 'PRIVATE_RUN_COPY_OK' in copied
    wrapper='/data/app/el2/100/base/'+host+'/files/private-runtime-launch-gap'+args.private_runtime_version
    # The app-data wrapper, when present, runs before the runtime mount.
    runtime_launch=wrapper+' '+runtime_source+' sh '+remote+'/run.sh'
    if private:
        launch=launch.replace('sh '+remote+'/run.sh',runtime_launch)
    else:
        private='/data/app/el2/100/base/'+host+'/files/android-control-gap-'+args.run
        assert 'EXISTS' not in device.shell('test -e '+private+' && echo EXISTS')
        made=device.shell(f'mkdir {private} && chown {uid}:{uid} {private} && chmod 700 {private} && mkdir {private}/webview-{args.run} && chown {uid}:{uid} {private}/webview-{args.run} && echo PRIVATE_CONTROL_OK')
        assert 'PRIVATE_CONTROL_OK' in made
        launch=f'/data/local/tmp/asx/ime-ability-window-20260904/candidate04/private-data-launch {uid} {private} /data/data/{package} '+runtime_launch
if args.private_runtime and args.private_runtime_version == '19':
    assert private
    made=device.shell(f'mkdir -p {private}-parent/{package} {private}-parent/{host} && chown -R {uid}:{uid} {private}-parent && chmod 700 {private}-parent && echo PRIVATE_PARENT_OK')
    assert 'PRIVATE_PARENT_OK' in made
    launch='env ASX_PRIVATE_APP_DATA='+private+' '+launch
if args.jit_file_cache:
    assert private
    cache=private+'/code_cache/art-volatile'
    result=device.shell(f'mkdir -p {private}/code_cache && mkdir {cache} && chown {uid}:{uid} {private}/code_cache {cache} && chmod 700 {cache} && stat -c "%u %a" {cache}')
    assert result.strip()==f'{uid} 700',result
cmd=f'ASX_SOCKET_NAME={socket.rsplit("/",1)[1]} WL_PARENT_ID={wid} ASX_LAUNCH_PKG={package} ASX_LAUNCH_ACTIVITY={activity} ASX_APK_PATH={apk} ASX_WEBVIEW_DATA_DIR={webdata} nohup {launch} >{remote}/parent.log 2>&1 </dev/null & echo $!'
parent=int(device.shell(cmd).strip())
for _ in range(60):
    if 'READY' in device.shell(f'test -S {socket} && echo READY'):break
    time.sleep(.5)
else:raise RuntimeError(device.shell('tail -n 60 '+remote+'/parent.log'))
result=device.save(args.run+'-spawn.log',f'/data/local/tmp/asx/ime-ability-window-20260904/candidate04/host-spawn {socket} {remote}/request.bin')
match=re.search(r'HOST_SPAWN result=0 pid=(\d+)',result);assert match,result
runtime=('/data/local/tmp/asx/gap-repair-20260904/'+args.candidate if args.candidate else '/data/local/tmp/asx/ime-ability-window-20260904/candidate04')+'/runtime/appspawn-x'
if args.spawn_helper:runtime=remote+'/'+args.spawn_helper
state=dict(parent=parent,child=int(match[1]),socket=socket,host_pid=pid,host_uid=uid,window=wid,remote=remote,runtime=runtime,apk=apk,package=package,private_data=private)
state['private_runtime_wrapper']=wrapper if args.private_runtime else None
state['control_apk_source']=str(control_path)
state['control_sha256']=hashlib.sha256(control_path.read_bytes()).hexdigest()
state['jit_requested']=not (args.interpreter or args.no_jit)
state['jit_explicitly_disabled']=bool(args.interpreter or args.no_jit)
state['interpreter_forced']=args.interpreter
state['jit_runtime_verified']=False
state['art_helper']=args.art_helper
state['popen_helper']=args.popen_helper
state['webview_debug_requested']=args.webview_debug
state['private_runtime_source']=runtime_source if args.private_runtime else None
state['jit_anon_fallback_requested']=args.jit_anon_fallback
state['jit_file_cache_requested']=args.jit_file_cache
(ROOT/(args.run+'-processes.json')).write_text(json.dumps(state,indent=2)+'\n');print(state)
