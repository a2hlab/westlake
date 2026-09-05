"""Verify the frozen candidate and explicit pass/fail criteria, not screenshots alone."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parent
manifest=json.loads((ROOT/'candidate05/manifest.json').read_text())
for relative,expected in manifest.items():
    assert hashlib.sha256((ROOT/'candidate05'/relative).read_bytes()).hexdigest()==expected,relative
suite=(ROOT/'suite05b-final-child.log').read_text()
for name in ['udp-direct-offset','unix-abstract','multicast-loopback','datagram-api-async-close']:
    assert '[GAP_NET] PASS '+name in suite,name
for name in ['valid EC chain accepted=true','untrusted EC root accepted=false',
             'expired EC leaf accepted=false','tampered EC signature accepted=false']:
    assert '[GAP_CERT] PASS '+name in suite,name
assert '[GAP_CRYPTO] PASS kernel_random_and_aes256_roundtrip' in suite
assert 'url=https://example.com/ title=Example Domain' in suite
assert 'SSL_ERROR=' not in suite
assert '[OH_HWUI_FUNCTOR] hardware recording installed=1' in suite
ime=(ROOT/'ime05-final-child.log').read_text()
for name in ['emoji delete','selected replacement','stale queued edit discarded']:
    assert '[GAP_IME] PASS '+name in ime,name
assert 'layout height=1094 editorBottom=1064' in ime
assert 'layout height=1920 editorBottom=1890' in ime
assert '"中国"' in (ROOT/'toutiao05copy-chinese-commit.tree').read_text()
assert '"泥好"' in (ROOT/'ime05-learning-commit.tree').read_text()
assert (ROOT/'ime05-learning-disk.log').read_text().startswith('66 ')
assert (ROOT/'pinyin-reopened-state.log').read_text().startswith('66 ')
assert (ROOT/'pinyin/decoder-tests-final.log').read_text().rstrip().endswith('OK')
assert (ROOT/'pinyin/controller-tests-final.log').read_text().count('PASS ')==7
perf=json.loads((ROOT/'performance-results.json').read_text())
for run in ('bench05c-measure.log','bench05d-measure.log','oneplus-benchmark03.log'):
    row=next(r for r in perf['runs'] if r['log']==run)
    assert row['page_complete'] and row['visual_ready']['n']==10 and not row['errors']
    assert row['injected_event_to_click']['n']==12
known_open=['Full Android IME session/enumeration facade','Floating/rotated/multiwindow IME insets',
 'EGL startup failure in benchfast02','Clean-data Toutiao PermissionManager service',
 'Native HTTPS peer full-certificate-chain API','IPv6/fd-reuse/GC/negative network stress',
 'Live Toutiao performance parity: OnePlus has no active network']
report=dict(candidate='candidate05',frozen_artifacts_verified=len(manifest),
    network_controls_passed=4,certificate_controls_passed=4,crypto_controls_passed=1,
    ime_edit_controls_passed=3,decoder_tests_passed=4,controller_groups_passed=7,
    live_chinese_phrase_persistence=True,all_five_gaps_closed=False,native_parity_established=False,
    known_open=known_open)
(ROOT/'verification-results.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
