"""Verify this follow-up's frozen evidence; no device mutations or new tests."""
from pathlib import Path
import hashlib,json,re,statistics
ROOT=Path(__file__).resolve().parent
used=set()
def read(name):used.add(name);return (ROOT/name).read_text(encoding='utf-8-sig')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
manifest=json.loads(read('candidate26/manifest.json'))
for name,digest in manifest.items():assert sha(ROOT/'candidate26'/name)==digest,name
assert len(manifest)==66
assert 'GAP_INPUTS_OK' in read('candidate26-stage.log')
assert 'PRIVATE_HASHES_OK' in read('candidate26-private-verified.log')
delta=json.loads(read('resume27-art/object-delta.json'))
assert delta['objects']==454 and delta['changed']==['runtime/jit/jit_memory_region.o']
assert sha(ROOT/'libart-v27.so')==delta['candidate_sha256']
assert sha(ROOT/'libart-v16.so')==delta['baseline_sha256']
assert sha(ROOT/'liboh_popen_v24.so')==sha(ROOT/'liboh_popen_rebuilt_v24.so')
assert sha(ROOT/'candidate26/runtime/liboh_window_policy.so')==sha(ROOT/'liboh_window_policy_rebuilt_v26.so')
for name in ['resume24-popen-contract-allowed.log','resume24-popen-contract-denied.log','oneplus23-popen-v2-allowed.log','oneplus23-popen-v2-denied.log']:
 assert 'failures=0' in read(name)
for name in ['resume24-popen-stress.log','oneplus24-popen-stress.log']:
 text=read(name);assert 'failures=0' in text
for name in ['resume27-jit-dual-app.log','resume27-jit-dual-tmpfile-app.log']:
 text=read(name);assert 'memfd uid=20010054 rw=1 rw_errno=0 rx=0 rx_errno=13' in text
 assert 'private-file uid=20010054 rw=1 rw_errno=0 rx=1 rx_errno=0' in text
 assert 'private-file code_updates=64 failures=0' in text
for phase,count in [('main_on',1),('main_off',0),('dialog_on',1),('dialog_removed',0),('idle_hold_start',1),('idle_release',0)]:
 text=read('resume26window-'+phase+'-locks.log');assert re.search(r'\n  SCREEN: '+str(count)+r'\n',text)
 android=read('oneplus26window-'+phase+'-power.log')
 assert ('SCREEN_BRIGHT_WAKE_LOCK' in android)==bool(count)
assert 'Current State: AWAKE' in read('resume26window-idle-held-power.log')
assert 'Blocked by running lock' in read('resume26window-idle-held-power.log')
assert 'Current State: SLEEP' in read('resume26window-idle-released-power.log')
window_log=read('resume26window-final-child.log')
assert 'idle_hold_interactive=unavailable' in window_log  # PowerManager facade remains open.
assert '[CPU_REF]' in window_log and 'monotonic=true pass=true' in window_log
assert 'monotonic=true pass=true' in read('oneplus26window-child.log')
window_sha=json.loads(read('resume26window-processes.json'))['control_sha256']
assert window_sha==json.loads(read('oneplus26window.json'))['sha256']
for name in ['resume22-close.log','oneplus22-close.log']:
 text=read(name);assert text.count('[CLOSE_REF] timeout=')==16 and '[CLOSE_REF] COMPLETE failures=0' in text
ime=read('resume27ime-gate.log');assert ime.count('[GAP_IME_EDGE] PASS ')==17
assert '[GAP_IME_EDGE] COMPLETE failures=0' in ime and 'Fatal signal' not in ime
suite=read('resume27suite-gate.log')
assert suite.count('[GAP_NET] PASS ')==10 and suite.count('[GAP_CERT] PASS ')==4
for marker in ['full-standard-chain count=4','facade-and-unknown-denial','LOCAL WEBVIEW PASS','JAVASCRIPT PASS','url=https://example.com/ title=Example Domain']:
 assert marker in suite,marker
assert 'Fatal signal' not in suite
summary=json.loads(read('resume27-benchmark-summary.json'))
assert len(summary)==4
apk='8a0065888a673add9c0c4027474622d6d6f31c55e537d7985ba59b9cf304a11b'
for name,data in summary.items():
 state=json.loads(read(name+'-processes.json'));assert state['control_sha256']==apk and state['art_helper']=='libart-v27.so'
 assert len(data['visual_ready_ms']['samples'])==10
 assert len(data['event_to_click_ms']['samples'])==12
 assert len(data['event_to_frame_callback_ms']['samples'])==12
 assert len(data['phases'])==12 and all(x['focus']=='true' and x['visibility']==0 for x in data['phases'])
 log=read(name+'-measure.log');assert '[WESTLAKE-JIT-BOOT] result=1' in log and 'Fatal signal' not in log
 ident=read(name+'-identity.log');assert 'u:r:normal_hap:s0' in ident
 for kind in ['Inh','Prm','Eff','Bnd','Amb']:assert re.search(r'Cap'+kind+r':\s+0000000000000000',ident)
 maps=read(name+'-maps.log')
 if 'file' in name:
  assert 'using app-private unlinked file with dual RW/RX views' in log
  lines=[x.split() for x in maps.splitlines() if '/code_cache/art-volatile/' in x]
  assert any(x[1]=='r-xs' for x in lines) and any(x[1]=='rw-s' for x in lines)
  assert len({x[4] for x in lines})==1 and all('rwx' not in x[1] for x in lines)
 else:assert 'using ART anonymous cache with RWX updates' in log
for name in ['oneplus24a','oneplus24b']:
 assert json.loads(read(name+'.json'))['control_sha256']==apk
 assert read(name+'.log').count('[GAP_BENCH] PAGE sample=')==12
file_runs=[x for n,x in summary.items() if 'file' in n];anon_runs=[x for n,x in summary.items() if 'anon' in n]
file_tail=statistics.median(x['visual_ready_ms']['p90'] for x in file_runs)
anon_tail=statistics.median(x['visual_ready_ms']['p90'] for x in anon_runs)
file_cpu=statistics.median(x['phases'][-1]['cpu_ms'] for x in file_runs)
anon_cpu=statistics.median(x['phases'][-1]['cpu_ms'] for x in anon_runs)
assert file_tail<anon_tail and file_cpu<anon_cpu
assert any('Transports: VPN' in x and 'VALIDATED' in x and 'VPN CONNECTED' in x for x in read('oneplus27-connectivity.log').splitlines())
assert 'Fatal signal' not in read('oneplus27-toutiao.log')
# Final real-app and preservation records must exist before this verifier can pass.
app=read('resume27toutiao-final-child.log')
assert app.count('caller=/data/local/tmp/asx/lib/arm64/libumeng-spy.so')>=3
assert 'Fatal signal' not in app and '[WESTLAKE-JIT-BOOT] result=1' in app
assert 'FeedTitleTextView' in read('resume27-toutiao-feed.tree')
for name,value in [('resume27-query-composing.tree','nihao'),('resume27-query-commit.tree','你好')]:
 lines=[x for x in read(name).splitlines() if 'SearchAutoCompleteTextView' in x]
 assert lines and ('\"'+value+'\"') in lines[-1]
def dom(name):
 matches=[t['dom'] for t in json.loads(read(name)) if t.get('dom') and t['dom']['title']=='你好-头条搜索']
 assert len(matches)==1;return matches[0]
before=dom('resume27-search-dom-before.json');after=dom('resume27-search-dom-after.json')
assert before['ready']=='complete' and before['textLength']>1000
assert after['scrollY']>before['scrollY']
preserve=read('resume27-final-preservation.log')
for marker in ['Enforcing','u:r:normal_hap:s0','a5436670f182aa3e24fde289ed38a0cd','49251','50270','READY_CHILD_RUNNING']:
 assert marker in preserve,marker
for kind in ['Inh','Prm','Eff','Bnd','Amb']:assert re.search(r'Cap'+kind+r':\s+0000000000000000',preserve)
power=read('resume27-power-restored.log');assert 'Timeout=30000ms' in power and 'OverrideTimeout=' not in power
assert '9222' not in read('resume27-forward-ports-final.log')
ready_state=json.loads(read('resume27ready-processes.json'))
assert ready_state['art_helper']=='libart-v27.so' and ready_state['jit_file_cache_requested']
assert ready_state['popen_helper']=='liboh_popen_v24.so' and not ready_state['webview_debug_requested']
ready=read('resume27ready-child.log')
assert 'Fatal signal' not in ready and '[OH_POPEN]' not in ready
assert '[WESTLAKE-JIT-BOOT] result=1' in ready
assert 'using app-private unlinked file with dual RW/RX views' in ready
assert not read('resume27ready-debug-settings.log').strip()
assert 'FeedTitleTextView' in read('resume27ready-feed.tree')
views=[line.split() for line in read('resume27ready-maps.log').splitlines() if '/code_cache/art-volatile/' in line]
assert any(x[1]=='r-xs' for x in views) and any(x[1]=='rw-s' for x in views)
assert len({x[4] for x in views})==1 and all('rwx' not in x[1] for x in views)
assert any('Transports: VPN' in x and 'VALIDATED' in x and 'VPN CONNECTED' in x for x in read('oneplus27-final-connectivity.log').splitlines())
assert read('oneplus27-final-apk-sha256.log').split()[0]==sha(ROOT.parent/'toutiao.apk')
assert read('oneplus27-final-toutiao-pid.log').strip().isdigit()
assert json.loads(read('resume27-final-host-relay.json'))['ProcessName']=='gnirehtet'
result=dict(evidence_verified=True,all_gaps_closed=False,global_runtime_replaced=False,
 candidate_artifacts=len(manifest),art_changed_objects=delta['changed'],
 native_worker_abort_reproduced_before=True,native_worker_abort_observed_after=False,
 cpu_time_api=True,keep_screen_on_flag=True,power_manager_facade_complete=False,
 ime_assertions=17,network_groups=10,certificate_groups=4,
 page_tail_reduction_percent=100*(1-file_tail/anon_tail),process_cpu_reduction_percent=100*(1-file_cpu/anon_cpu),
 physical_presentation_measured=False,live_toutiao_performance_measured=False,
 unchanged_toutiao_search_scroll=True,usb_network_validated=True,
 ready_child=ready_state['child'],ready_parent=ready_state['parent'],ready_tracing_enabled=False,
 evidence_sha256={name:sha(ROOT/name) for name in sorted(used)})
(ROOT/'followup27-verification.json').write_text(json.dumps(result,indent=2)+'\n')
print('FOLLOWUP27_EVIDENCE_VERIFIED',json.dumps({k:v for k,v in result.items() if k!='evidence_sha256'}))
