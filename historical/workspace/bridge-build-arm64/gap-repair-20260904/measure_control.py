"""Collect a complete offline run, failing visibly on missing page/input samples."""
import argparse,json,re,time,hashlib
import device
p=argparse.ArgumentParser();p.add_argument('run');a=p.parse_args()
assert re.fullmatch('[a-z0-9-]+',a.run)
s=json.loads((device.ROOT/(a.run+'-processes.json')).read_text())
remote=f'/data/service/el1/public/appspawnx/adapter_child_{s["child"]}.stderr'
for attempt in range(30):
    log=device.shell('cat '+remote)
    if '[GAP_BENCH] PAGE_COMPLETE samples=12 warmup=2' in log:break
    if 'ASSERT FAILED' in log or '[GAP_BENCH] FAIL' in log:break
    if attempt%5==0:print('Waiting for offline pages',a.run,flush=True)
    time.sleep(1)
else:pass
(device.ROOT/(a.run+'-pages.log')).write_text(log)
assert '[GAP_BENCH] PAGE_COMPLETE samples=12 warmup=2' in log,'Incomplete pages; evidence preserved'
assert '[GAP_BENCH] INPUT ' not in log,'Start with an untapped benchmark activity'
for _ in range(12):
    # -c reuses the DOWN event timestamp for UP on this board. Separate
    # -d/-u operations create fresh timestamped events; no clock correction.
    device.shell('uinput -T -d 600 160 -i 30 -u 600 160');time.sleep(.18)
time.sleep(.3)
log=device.save(a.run+'-measure.log','cat '+remote)
assert len(re.findall(r'\[GAP_BENCH\] INPUT sample=',log))==12
assert len(re.findall(r'\[GAP_BENCH\] INPUT_FRAME sample=',log))==12
metadata={'run':a.run,'process':s,'control_sha256':s.get('control_sha256') or hashlib.sha256((device.ROOT/'control-out/control.apk').read_bytes()).hexdigest(),
 'page_samples':12,'page_warmup_excluded':2,'input_samples':12,
 'input_protocol':'uinput -T -d 600 160 -i 30 -u 600 160',
 'limitations':['WebView visual callback is not physical presentation','Injected input is not touch to photon','Not live Toutiao']}
(device.ROOT/(a.run+'-measure.json')).write_text(json.dumps(metadata,indent=2)+'\n')
print('BENCHMARK_COMPLETE',a.run)
