"""Regenerate descriptive latency statistics from retained raw logs."""
from pathlib import Path
import hashlib,json,math,re,statistics
ROOT=Path(__file__).resolve().parent
def describe(values):
    if not values:return None
    return dict(n=len(values),median_ms=statistics.median(values),
                p90_nearest_rank_ms=sorted(values)[math.ceil(.9*len(values))-1],max_ms=max(values))
names=['benchsw01-final-child.log','benchhw01-final-child.log','benchhw02-final-child.log',
       'benchfast01-final-child.log','benchfast02-final-child.log',
       'bench05a-measure.log','bench05b-measure.log','bench05c-measure.log','bench05d-measure.log',
       'oneplus-benchmark01.log','oneplus-benchmark02.log','oneplus-benchmark03.log']
results=[]
for name in names:
    path=ROOT/name
    if not path.exists():continue
    text=path.read_text();rows=[tuple(map(int,r)) for r in re.findall(r'PAGE sample=(\d+) load_finished_ms=(\d+) visual_ready_ms=(\d+)',text)]
    rows=[(i,a,b) for i,a,b in rows if i>=2]
    clicks=list(map(int,re.findall(r'INPUT sample=\d+ event_to_click_ms=(\d+)',text)))
    frames=list(map(int,re.findall(r'INPUT_FRAME sample=\d+ event_to_frame_callback_ms=(\d+)',text)))
    valid_input=name.startswith(('benchfast01','bench05','oneplus'))
    row=dict(log=name,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
       page_complete='PAGE_COMPLETE samples=12 warmup=2' in text,
       load_finished=describe([a for i,a,b in rows]),visual_ready=describe([b for i,a,b in rows]),
       injected_event_to_click=describe(clicks) if valid_input else None,
       injected_event_to_frame_callback=describe(frames) if valid_input else None,
       input_timestamp_usable=valid_input,
       errors=[line for line in text.splitlines() if 'ASSERT FAILED' in line or '[GAP_BENCH] FAIL' in line])
    results.append(row)
report={'page_warmup_excluded':2,'input_warmup_excluded':0,'runs':results,
 'limitations':['Descriptive small-sample statistics, no equivalence claim',
 'WebView callback is not physical panel presentation','Different device resolution, density and WebView versions',
 'OnePlus had no active default network; no live Toutiao comparison',
 'OH baseline process remained live except explicitly logged bench05c/d pauses',
 'benchhw01/02 and software input -c timestamps were invalid for release latency',
 'benchfast02 EGL initialization failed and remains a failed run; not silently excluded']}
(ROOT/'performance-results.json').write_text(json.dumps(report,indent=2)+'\n')
for r in results:print(r['log'], 'page=',r['visual_ready'],'click=',r['injected_event_to_click'],'errors=',len(r['errors']))
