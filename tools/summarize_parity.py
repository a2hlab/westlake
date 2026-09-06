#!/usr/bin/env python3
"""Summarize complete benchmark logs; reject missing, duplicate or unfocused samples."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics

def stats(values):
    return dict(samples=values, median=statistics.median(values),
                p90=sorted(values)[math.ceil(.9 * len(values)) - 1])

def summarize(log):
    if '[GAP_BENCH] FAIL' in log or 'Fatal signal' in log:
        raise ValueError('Failed benchmark')
    def samples(pattern, first):
        rows=[(int(i), int(value)) for i, value in re.findall(pattern, log)]
        if [i for i, _ in rows] != list(range(first, first+12)):
            raise ValueError('Missing, duplicate or unordered benchmark samples')
        return [value for _, value in rows]
    pages=samples(r'\[GAP_BENCH\] PAGE sample=(\d+) load_finished_ms=\d+ visual_ready_ms=(\d+)', 0)
    clicks=samples(r'\[GAP_BENCH\] INPUT sample=(\d+) event_to_click_ms=(\d+)', 1)
    frames=samples(r'\[GAP_BENCH\] INPUT_FRAME sample=(\d+) event_to_frame_callback_ms=(\d+)', 1)
    phases=[dict(sample=int(i),uptime=int(t),cpu_ms=int(cpu),focus=focus,visibility=int(vis))
            for i,t,cpu,focus,vis in re.findall(r'\[BENCH_PHASE\] sample=(\d+) end_uptime=(\d+) cpu_ms=(\d+) focus=(\w+) visibility=(\d+)',log)]
    if [x['sample'] for x in phases] != list(range(12)):
        raise ValueError('Missing or duplicate phase records')
    if any(x['focus']!='true' or x['visibility']!=0 for x in phases):
        raise ValueError('Unfocused or hidden benchmark')
    if '[GAP_BENCH] PAGE_COMPLETE samples=12 warmup=2' not in log:
        raise ValueError('Incomplete page workload')
    return dict(visual_ready_ms=stats(pages[2:]),event_to_click_ms=stats(clicks),
                event_to_frame_callback_ms=stats(frames),phases=phases)

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs',type=Path,nargs='+')
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    if args.out.exists():parser.error('--out must be new')
    result={path.stem:summarize(path.read_text()) for path in args.logs}
    args.out.write_text(json.dumps(result,indent=2)+'\n')
    print('PARITY_SAMPLES_VERIFIED',len(result))
