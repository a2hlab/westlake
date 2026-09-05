#!/usr/bin/env python3
"""Unchanged-APK device comparison. No app data clearing or runtime modification.

Visual response is an interval, not a frame-perfect tap-to-photon claim. Each
capture is bracketed by host monotonic timestamps. Raw captures and command
records remain available to verify the endpoint independently.
"""
import argparse
import concurrent.futures
import io
import json
import re
import subprocess
import threading
import time
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parent
HDC = '/mnt/c/Users/dspfa/Dev/ohos-tools/hdc.exe'
ADB = '/mnt/c/Users/dspfa/Dev/platform-tools/adb.exe'
OH = [HDC, '-t', '5cdbf6af00000000000000000923012c']
OP = [ADB, '-s', 'cfb7c9e3']
PKG = 'com.ss.android.article.news'
ACTIVITY = PKG + '/' + PKG + '.activity.MainActivity'
REMOTE = '/data/local/tmp/asx/out-jit-review-fixes-20260904'
LOCK = threading.Lock()


def run(device, args, timeout=30):
    command = (OH if device == 'ohos' else OP) + args
    start = time.monotonic_ns()
    result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=timeout)
    end = time.monotonic_ns()
    with LOCK, (ROOT / 'commands.jsonl').open('a') as out:
        out.write(json.dumps(dict(device=device, command=command, start_ns=start,
            end_ns=end, returncode=result.returncode,
            stderr=result.stderr.decode(errors='replace')[-1000:])) + '\n')
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors='replace'))
    return result.stdout, start, end


def shell(device, command, timeout=30):
    raw, start, end = run(device, ['shell', command], timeout)
    return raw.decode(errors='replace').replace('\r', ''), start, end


def capture(device, folder, index):
    name = f'{index:04d}.' + ('jpeg' if device == 'ohos' else 'png')
    local = folder / name
    if device == 'ohos':
        remote = f'{REMOTE}/bench-{folder.name}-{index}.jpeg'
        text, start, end = shell(device, f'snapshot_display -f {remote} >/dev/null 2>&1')
        run(device, ['file', 'recv', remote, str(local.relative_to(ROOT))])
    else:
        raw, start, end = run(device, ['exec-out', 'screencap', '-p'])
        local.write_bytes(raw)
    im = Image.open(local).convert('RGB')
    # Exclude status/navigation bars; retain app header and body. Resizing is
    # only for analysis; the raw screenshot on disk is unchanged.
    w, h = im.size
    roi = im.crop((0, int(h * .08), w, int(h * .88))).resize((120, 160))
    return dict(file=str(local.relative_to(ROOT)), start_ns=start, end_ns=end), roi


def visual(device, name, x=None, y=None, back=False, pid=None, seconds=3):
    if device == 'ohos' and not pid:
        raise ValueError('OHOS actions require an explicit child PID')
    if not back and (x is None or y is None):
        raise ValueError('Tap coordinates are required')
    folder = ROOT / name
    folder.mkdir()  # Never overwrite a previous trial.
    frames = []
    initial = threading.Event()
    stop = threading.Event()

    def collect():
        index = 0
        while not stop.is_set():
            record, roi = capture(device, folder, index)
            frames.append((record, roi))
            index += 1
            if index >= 2:
                initial.set()

    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
        task = pool.submit(collect)
        if not initial.wait(20):
            stop.set()
            task.result()
            raise TimeoutError('No initial captures')
        try:
            if device == 'ohos':
                action = 'back' if back else f'{x} {y}'
                _, begin, end = shell(device, f"echo '{action}' > /data/local/tmp/noice_tap.{pid}")
            else:
                action = 'keyevent 4' if back else f'tap {x} {y}'
                _, begin, end = shell(device, 'input ' + action)
            time.sleep(seconds)
        finally:
            stop.set()
        task.result()

    before = [(r, im) for r, im in frames if r['end_ns'] <= begin]
    if not before:
        raise RuntimeError('No unambiguous pre-input baseline')
    previous, baseline = before[-1]
    first = None
    for record, im in frames:
        record['mean_absolute_pixel_change'] = sum(ImageStat.Stat(
            ImageChops.difference(baseline, im)).mean) / (3 * 255)
        if record['end_ns'] <= begin:
            continue
        # Require a material app-region change. Raw frames must also confirm
        # that it was the intended UI response, not unrelated animation.
        if record['mean_absolute_pixel_change'] >= .015:
            first = record
            break
        previous = record
    response = None if first is None else dict(
        lower_ms=max(0, (previous['start_ns'] - end) / 1e6),
        upper_ms=max(0, (first['end_ns'] - begin) / 1e6),
        first_changed_capture=first['file'])
    result = dict(device=device, name=name, metric='first_significant_visual_response',
        action=action, input_begin_ns=begin, input_end_ns=end,
        input_command_ms=(end - begin) / 1e6, response_interval=response,
        frames=[r for r, _ in frames], semantic_validation='pending',
        note='Includes injected-input path; not frame-perfect physical tap-to-photon.')
    (folder / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps({k: v for k, v in result.items() if k != 'frames'}), flush=True)
    return result


def oh_tree(pid):
    log = f'/data/service/el1/public/appspawnx/adapter_child_{pid}.stderr'
    text, _, _ = shell('ohos', f"N=$(wc -c < {log}); echo v > /data/local/tmp/noice_tap.{pid}; echo $((N + 1))")
    offset = int(text.strip())
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        text, _, _ = shell('ohos', f'tail -c +{offset} {log}')
        if 'VT ==== end ====' in text:
            return text
        time.sleep(.1)
    return text


def oh_launch(name, old_pid=None, timeout=120):
    folder = ROOT / name
    folder.mkdir()
    if old_pid:
        # Only terminate a child of the isolated candidate, not a generic pidof.
        text, _, _ = shell('ohos', f"if grep -q '/out-jit-review-fixes-20260904/libart.so' /proc/{old_pid}/maps; then kill -TERM {old_pid}; echo STOPPED; fi")
        if 'STOPPED' not in text:
            raise RuntimeError('Old child identity mismatch')
        time.sleep(1)
    output, begin, _ = shell('ohos', '/data/local/tmp/asx/spawn_client /dev/unix/socket/AppSpawnXJitReview ' + PKG)
    match = re.search(r'result=0 pid=(\d+)', output)
    if not match:
        raise RuntimeError(output)
    pid = int(match[1])
    log = f'/data/service/el1/public/appspawnx/adapter_child_{pid}.stderr'
    deadline = time.monotonic() + timeout
    last_negative = begin
    first = None
    while time.monotonic() < deadline:
        text, lo, hi = shell('ohos', f"if [ ! -d /proc/{pid} ]; then echo DEAD; fi; grep -aE 'FATAL EXCEPTION|WESTLAKE-CHILDSEGV.*fatal|Fatal signal|Check failed|side-channels started' {log} | tail -5")
        if re.search(r'DEAD|FATAL EXCEPTION|WESTLAKE-CHILDSEGV.*fatal|Fatal signal|Check failed', text):
            raise RuntimeError(f'Child {pid} failed: {text}')
        # Time the visible buffered MainActivity before requesting a view dump.
        # VT collection is validation after the timing endpoint, not timed work.
        rs, rs_start, _ = shell('ohos', "hidumper -s RenderService -a RSTree 2>/dev/null | sed -n 's/.*SURFACE_NODE\\[\\([0-9][0-9]*\\)\\].*Name \\[com.ss.android.article.news\\/com.ss.android.article.news.activity.MainActivity\\].*Visible: 1.*/\\1/p'")
        for sid in re.findall(r'^\d+$', rs, re.M):
            if int(sid) >> 32 != pid:
                continue
            buf, _, ready = shell('ohos', f"hidumper -s RenderService -a 'surfacenode {sid}' 2>/dev/null")
            if re.search(r'totalBuffersMemSize = [1-9]', buf):
                first = ready
                (folder / 'surface.txt').write_text(buf)
                break
        if first:
            break
        last_negative = rs_start
        time.sleep(.1)
    if first is None:
        raise TimeoutError(f'Child {pid} no UI within {timeout}s')
    state, _, _ = shell('ohos', f'cat /proc/{pid}/stat; grep /out-jit-review-fixes-20260904/libart.so /proc/{pid}/maps | head -1')
    (folder / 'identity.txt').write_text(state)
    capture('ohos', folder, 0)
    tree = oh_tree(pid)
    (folder / 'gate-tree.txt').write_text(tree)
    result = dict(device='ohos', name=name, pid=pid,
        metric='process_launch_to_visible_buffered_MainActivity', lower_ms=(last_negative-begin)/1e6,
        upper_ms=(first-begin)/1e6, content_ready=False,
        note='Owned visible buffered surface; VT verified after timing. Not content-loaded time; Android am uses its own first-draw boundary.')
    (folder / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result), flush=True)
    return pid


def main():
    p = argparse.ArgumentParser()
    p.add_argument('mode', choices=['visual', 'oh-launch', 'tree', 'native-launch'])
    p.add_argument('--device', choices=['ohos', 'oneplus'], default='ohos')
    p.add_argument('--name', required=True)
    p.add_argument('--pid', type=int)
    p.add_argument('--x', type=int)
    p.add_argument('--y', type=int)
    p.add_argument('--back', action='store_true')
    a = p.parse_args()
    if a.mode == 'visual':
        visual(a.device, a.name, a.x, a.y, a.back, a.pid)
    elif a.mode == 'oh-launch':
        oh_launch(a.name, a.pid)
    elif a.mode == 'tree':
        if a.device == 'ohos':
            text = oh_tree(a.pid)
        else:
            text, _, _ = shell('oneplus', 'uiautomator dump /data/local/tmp/toutiao-bench-window.xml >/dev/null; cat /data/local/tmp/toutiao-bench-window.xml')
        (ROOT / a.name).write_text(text)
    else:
        shell('oneplus', 'am force-stop ' + PKG)
        text, lo, hi = shell('oneplus', 'am start -W -n ' + ACTIVITY)
        result = dict(device='oneplus', name=a.name, metric='am_start_W',
            host_command_ms=(hi-lo)/1e6, raw=text)
        (ROOT / (a.name + '.json')).write_text(json.dumps(result, indent=2))
        print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
