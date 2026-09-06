"""Verify frozen follow-up evidence, without changing any device state."""
from pathlib import Path
import hashlib, json, re

ROOT = Path(__file__).resolve().parent
used = set()

def read(name):
    used.add(name)
    return (ROOT / name).read_text()

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

manifest = json.loads(read('candidate21/manifest.json'))
for name, sha in manifest.items():
    assert digest(ROOT / 'candidate21' / name) == sha, name
assert 'GAP_INPUTS_OK' in read('candidate21-stage.log')
network = read('resume20web-final-child.log')
assert len(re.findall(r'\[GAP_NET\] PASS ', network)) == 10
assert len(re.findall(r'\[GAP_CERT\] PASS ', network)) == 4
assert 'PASS full-standard-chain count=4' in network
assert 'numeric_fd_reused=30/32' in network
assert 'JAVASCRIPT PASS' in network
assert 'Fatal signal' not in network
ime = read('resume20ime-current.log')
assert len(re.findall(r'\[GAP_IME_EDGE\] PASS ', ime)) == 17
assert '[GAP_IME_EDGE] COMPLETE failures=0' in ime
for name in ('resume20-interface-v2-root.log', 'resume20-interface-v2-app.log'):
    assert 'kernel_roundtrips=33 failures=0' in read(name)
assert '"你好"' in read('resume21-query-commit.tree')

def search_dom(name):
    matches = [t['dom'] for t in json.loads(read(name))
               if t.get('dom') and t['dom']['title'] == '你好-头条搜索']
    assert len(matches) == 1
    return matches[0]

before = search_dom('resume21-scroll-focused-before.json')
after = search_dom('resume21-scroll-focused-after.json')
assert after['ready'] == 'complete' and after['textLength'] > 1000
assert before['scrollY'] == 0 and after['scrollY'] > before['scrollY']
runtime = read('resume21toutiao-final-child.log')
assert 'WESTLAKE-JIT-BOOT' in runtime and 'result=1' in runtime
assert 'Fatal signal 6 (SIGABRT)' in runtime  # Explicit open gate, never hidden.
preservation = read('resume21-preservation.log')
assert 'Enforcing' in preservation and 'u:r:normal_hap:s0' in preservation
for kind in ('Inh', 'Prm', 'Eff', 'Bnd', 'Amb'):
    assert re.search(r'Cap' + kind + r':\s+0000000000000000', preservation)
assert 'a5436670f182aa3e24fde289ed38a0cd' in preservation
assert '49251 ' in preservation and '50270 ' in preservation
assert len(read('resume21-private-payload-sha256.txt').splitlines()) == 354
timings = json.loads(read('resume20-benchmark-summary.json'))
for run in timings.values():
    assert len(run['visual_ready_ms']['samples']) == 10
    assert len(run['event_to_click_ms']['samples']) == 12
assert 'HTTP=200' in read('wifi-20260905/final-tunnel-https.log')
assert any('Transports: VPN' in line and 'VALIDATED' in line and 'VPN CONNECTED' in line
           for line in read('wifi-20260905/final-connectivity.log').splitlines())
power = read('resume21-power-restored-final.log')
assert 'Restore screen off time\n' in power and 'Restore screen off time failed' not in power
assert 'Timeout=30000ms' in power
report = dict(evidence_verified=True, all_gaps_closed=False, promotion_approved=False,
              candidate_artifacts=len(manifest), network_groups=10, ime_assertions=17,
              usb_network_verified=True, unchanged_toutiao_search_dom=True,
              touch_scroll_css_pixels=after['scrollY']-before['scrollY'],
              native_worker_abort_open=True, repeated_page_tail_open=True,
              physical_presentation_measured=False, timings=timings,
              evidence_sha256={name: digest(ROOT/name) for name in sorted(used)})
(ROOT/'resume-verification.json').write_text(json.dumps(report, indent=2)+'\n')
print('RESUME_EVIDENCE_VERIFIED; promotion_approved=false')
