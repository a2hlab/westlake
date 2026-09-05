import json
import time
import bench

pid = 11260
for index in range(2, 7):
    name = f'oneplus-launch-{index:02d}'
    bench.shell('oneplus', 'am force-stop ' + bench.PKG)
    text, lo, hi = bench.shell('oneplus', 'am start -W -n ' + bench.ACTIVITY)
    result = dict(device='oneplus', name=name, metric='am_start_W',
                  host_command_ms=(hi-lo)/1e6, raw=text)
    (bench.ROOT / (name + '.json')).write_text(json.dumps(result, indent=2))
    print(json.dumps(result), flush=True)
    pid = bench.oh_launch(f'ohos-launch-{index:02d}', pid)
    (bench.ROOT / 'current-bench-child.txt').write_text(str(pid))
    time.sleep(2)
