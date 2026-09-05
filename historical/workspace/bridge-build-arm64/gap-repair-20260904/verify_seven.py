"""Evidence gates for the seven-item follow-up; never claim unmeasured parity."""
from pathlib import Path
import hashlib,json,re,statistics,math
ROOT=Path(__file__).resolve().parent
def read(name):return (ROOT/name).read_text()
manifest=json.loads(read('candidate13/manifest.json'))
for name,sha in manifest.items():assert hashlib.sha256((ROOT/'candidate13'/name).read_bytes()).hexdigest()==sha,name
suite=read('suite13-final-child.log');ime=read('ime13-final-child.log')
for token in ('[GAP_PERMISSION] PASS facade-and-unknown-denial','[GAP_TRUST] PASS full-standard-chain count=4',
              '[GAP_NET_STRESS] COMPLETE','[GAP_CRYPTO] PASS','url=https://example.com/ title=Example Domain'):
    assert token in suite,token
for name in ('udp-direct-offset','unix-abstract','multicast-loopback','datagram-api-async-close',
             'ipv6-heap-direct-loopback','udp-truncation-and-zero-length','errno-and-poll-deadline',
             'concurrent-gc-heap-direct-128','async-close-and-socket-churn-32','observed-numeric-fd-reuse-32'):
    assert '[GAP_NET] PASS '+name in suite,name
for name in ('valid EC chain accepted=true','untrusted EC root accepted=false','expired EC leaf accepted=false','tampered EC signature accepted=false'):
    assert '[GAP_CERT] PASS '+name in suite,name
assert '[GAP_IME_EDGE] COMPLETE failures=0' in ime
assert '[OH_IME_GEOMETRY] occupied=0,1094 1200,826' in ime
assert '[OH_IME_META] large selection length=40000 start=30002 end=30002' in ime
for log in (suite,ime):
    assert 'Fatal signal' not in log and '[GAP_NET] FAIL' not in log and '[GAP_IME_EDGE] FAIL' not in log
fresh=read('fresh13-ui.tree')
assert 'DecorView' in fresh and '个人信息保护指引' in fresh
assert 'PermissionManager.shouldShowRequestPermissionRationale' not in read('fresh13-final-child.log')
assert 'Fatal signal' not in read('fresh13-final-child.log')
app=read('toutiao13-child-current.log')
app_ime_failed='[OH_IME_CAPI] attach/show=12800003' in app
live_chinese=(ROOT/'toutiao13-chinese.tree').exists() and '"中国"' in read('toutiao13-chinese.tree')
assert app_ime_failed or live_chinese,'Missing real-app IME evidence; do not infer a pass from controls'
fallback=read('egl13fallback-final-child.log')
assert '[OH_EGL] TEST removing damage extensions' in fallback
assert '[OH_EGL] swap mode=full' in fallback
assert '[GAP_BENCH] PAGE_COMPLETE samples=12 warmup=2' in fallback
assert 'Fatal signal' not in fallback
def describe(values):return dict(n=len(values),median_ms=statistics.median(values),p90_ms=sorted(values)[math.ceil(.9*len(values))-1],max_ms=max(values))
runs=[]
for name in ('bench13a','bench13b','oneplus13b','oneplus13c'):
    android=name.startswith('oneplus');file=name+'.log' if android else name+'-measure.log'
    log=read(file);meta=json.loads(read(name+('.json' if android else '-measure.json')))
    assert meta['control_sha256']=='44816d9ac706bc94ff2e54780b8a1ad368c5a8c8c71f081232d9dec01b0f7d83'
    pages=[tuple(map(int,row)) for row in re.findall(r'PAGE sample=(\d+) load_finished_ms=(\d+) visual_ready_ms=(\d+)',log)]
    assert len(pages)==12 and 'PAGE_COMPLETE samples=12' in log
    clicks=list(map(int,re.findall(r'INPUT sample=\d+ event_to_click_ms=(\d+)',log)))
    frames=list(map(int,re.findall(r'INPUT_FRAME sample=\d+ event_to_frame_callback_ms=(\d+)',log)))
    assert len(clicks)==len(frames)==12
    if not android:assert 'State:\tT' in read(name+'-baseline-pause-end.log') and 'State:\tT' not in read(name+'-baseline-resumed.log')
    runs.append(dict(run=name,log=file,sha256=hashlib.sha256((ROOT/file).read_bytes()).hexdigest(),
        page_load=describe([a for i,a,b in pages if i>=2]),visual_ready=describe([b for i,a,b in pages if i>=2]),
        event_to_click=describe(clicks),event_to_frame_callback=describe(frames),baseline_temporarily_suspended=not android))
integrity=read('seven-final-baseline-integrity.log')
for value in ('c8b5e7352662109ddcb9ca3b97020894','b58ed9c01a93df2601e9c0671afdf2df',
              '7962c1a77b8f674bf01dfa612e053481','cc99caaaa3baffbb100118753fa1d130','49251','50270'):
    assert value in integrity,value
assert 'Active default network: none' in read('oneplus13c-network.log')
report=dict(candidate='candidate13',frozen_artifacts_verified=len(manifest),network_groups=10,
    numeric_fd_reuse=int(re.search(r'numeric_fd_reused=(\d+)/32',suite)[1]),
    certificate_cases=4,ime_assertions=len(re.findall(r'\[GAP_IME_EDGE\] PASS ',ime)),
    fresh_data_startup='consent screen reached; no consent accepted and no fresh-feed assertion',
    full_oh_rectangle_callback=True,live_pinyin_commit=live_chinese,offline_runs=runs,
    toutiao_ime_regression_passed=live_chinese and not app_ime_failed,
    rollout_approved=False,
    all_seven_complete=False,native_parity_established=False,
    remaining=['Toutiao SearchActivity OH IME attach returned 12800003 (IME_ERR_IMCLIENT); controls do not close this real-app failure',
        'OH USB target disconnected before native attach-failure diagnosis and final process-state verification',
        'OnePlus has no active network: live Toutiao load/scroll comparison unavailable',
        'Physical panel presentation latency is not measured by these callbacks',
        'Floating/landscape/split geometry controls pass; full physical rotation/floating-device matrix is not certified',
        'Android IME APK execution, handwriting, vendor private commands and privileged subtype settings are not supplied by this OH facade',
        'Native legacy SSLSession.getPeerCertificateChain API remains unsupported; modern full peer chain is fixed',
        'Finite networking stress is not an exhaustive concurrency proof'],
    retained_failed_experiments=['candidate07 eager permission singleton/JNI constant initialization',
        'suite08 asynchronous-close lost-wakeup race','ime09 stale OH Property ABI',
        'ime11 single-line test fixture length cap','ime12 early graphics dependency loading'])
(ROOT/'seven-verification-results.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
