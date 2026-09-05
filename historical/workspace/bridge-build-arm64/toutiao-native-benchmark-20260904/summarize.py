#!/usr/bin/env python3
"""Validate saved benchmark records and derive statistics without device access."""
import hashlib
import json
import re
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def read(path):
    return json.loads((ROOT / path).read_text())


def main():
    native, ohos = [], []
    for trial in range(2, 7):
        n = read(f'oneplus-launch-{trial:02d}.json')
        assert 'Status: ok' in n['raw'] and 'LaunchState: COLD' in n['raw']
        n['first_draw_ms'] = int(re.search(r'TotalTime: (\d+)', n['raw'])[1])
        native.append(n)
        o = read(f'ohos-launch-{trial:02d}/result.json')
        assert o['metric'] == 'process_launch_to_visible_buffered_MainActivity'
        assert 0 <= o['lower_ms'] <= o['upper_ms']
        identity = (ROOT / f'ohos-launch-{trial:02d}/identity.txt').read_text()
        assert identity.startswith(str(o['pid']) + ' ')
        assert '/out-jit-review-fixes-20260904/libart.so' in identity
        assert (ROOT / f'ohos-launch-{trial:02d}/0000.jpeg').stat().st_size > 1000
        ohos.append(o)
    assert len({o['pid'] for o in ohos}) == 5
    semantics = read('semantic-validation.json')
    actions = []
    for name, annotation in semantics['actions'].items():
        a = read(f'{name}/result.json')
        assert a['input_begin_ns'] <= a['input_end_ns']
        assert any(f['end_ns'] <= a['input_begin_ns'] for f in a['frames'])
        for f in a['frames']:
            assert f['start_ns'] <= f['end_ns']
            assert (ROOT / f['file']).stat().st_size > 1000
        if a['response_interval']:
            assert 0 <= a['response_interval']['lower_ms'] <= a['response_interval']['upper_ms']
        a['semantic_validation'] = annotation
        a['observed_after_input_ms'] = (
            max(f['end_ns'] for f in a['frames']) - a['input_end_ns']) / 1e6
        actions.append(a)
    log = (ROOT / 'ohos-settled-06/child.log').read_text(errors='replace')
    commands = [json.loads(line) for line in (ROOT / 'commands.jsonl').read_text().splitlines()]
    launches = [c for c in commands if any('spawn_client /dev/unix/socket/AppSpawnXJitReview' in arg for arg in c['command'])]
    settled = next(c for c in commands if any('snapshot_display -f' in arg and 'bench-ohos-settled-06-0.jpeg' in arg for arg in c['command']))
    last_launch = max((c for c in launches if c['start_ns'] < settled['start_ns']), key=lambda c: c['start_ns'])
    result = {
        'verdict': 'Native end-to-end parity not demonstrated; functional gaps observed. No JIT-only causal conclusion.',
        'native_launch': {'n': 5, 'endpoint': 'Android am start -W TotalTime / first draw',
            'median_ms': statistics.median(n['first_draw_ms'] for n in native),
            'min_ms': min(n['first_draw_ms'] for n in native),
            'max_ms': max(n['first_draw_ms'] for n in native), 'trials': native},
        'ohos_launch': {'n': 5, 'endpoint': 'Host-bracketed PID-owned visible buffered MainActivity surface',
            'median_lower_ms': statistics.median(o['lower_ms'] for o in ohos),
            'median_upper_ms': statistics.median(o['upper_ms'] for o in ohos), 'trials': ohos,
            'caveat': 'Different timing endpoint and polling overhead; not an exact native slowdown ratio.'},
        'ohos_settled_blank_capture_after_launch_ms': (settled['start_ns'] - last_launch['start_ns']) / 1e6,
        'ohos_settled_log': {'pid': 16640,
            'boot_jit_success_records': len(re.findall(r'\[WESTLAKE-JIT-BOOT\] result=1', log)),
            'fatal_pattern_matches': len(re.findall(r'FATAL EXCEPTION|Fatal signal|Check failed|WESTLAKE-CHILDSEGV.*fatal', log)),
            'caveat': 'Sampled diagnostics, not total JIT compilation count or proof of GC correctness.'},
        'semantics': semantics, 'actions': actions,
        'limitations': ['Different hardware, resolution, networks, app data, cache/profile history and runtime logging.',
            'No shared frame-presentation endpoint, no isolated JIT-on/off A/B, no repeatable content-locked article comparison.',
            'Response trials have one sample per action and coarse screenshots; no response percentiles or equality test.']
    }
    (ROOT / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k not in ('actions', 'semantics')}, indent=2))
    print('Validated 10 launch records and', len(actions), 'response records.')
    manifest = {}
    for p in sorted(ROOT.rglob('*')):
        if p.is_file() and '__pycache__' not in p.parts and p.name != 'artifact-sha256.json':
            manifest[str(p.relative_to(ROOT))] = hashlib.sha256(p.read_bytes()).hexdigest()
    (ROOT / 'artifact-sha256.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
