"""Read every available ranked APK's DEX type table; emit dependency evidence, not outcomes."""
import ast
import concurrent.futures
import csv
import datetime
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import time
import zipfile

BASE = Path('/home/dspfac/a2hlab')
CORPUS = BASE / 'source-closure/inputs/corpus-hk'
OUT = BASE / 'reports/hk-top1000-audit-20261115'
REFERENCE = Path('/home/dspfac/westlake-harness/benchmark/2026-10-25-hk-top1000-static-baseline/gms_reach.py')
tree = ast.parse(REFERENCE.read_text())
FAMILIES = next(ast.literal_eval(n.value) for n in tree.body
                if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'FAMILIES' for t in n.targets))
FAMILIES['gms:ads-api-excluding-identifier'] = ['Lcom/google/android/gms/ads/']
PREFIXES = {k: tuple(p.encode() for p in v) for k, v in FAMILIES.items()}


def dex_blobs(z, prefix='', depth=0):
    for entry in z.infolist():
        name = entry.filename
        if re.fullmatch(r'classes(?:\d+)?\.dex', name):
            yield prefix + name, z.read(entry)
        elif name.endswith('.apk') and depth < 2:
            if entry.file_size <= 64 * 1024 * 1024:
                with zipfile.ZipFile(io.BytesIO(z.read(entry))) as inner:
                    yield from dex_blobs(inner, prefix + name + '!/', depth + 1)
            else:
                with z.open(entry) as stream, zipfile.ZipFile(stream) as inner:
                    yield from dex_blobs(inner, prefix + name + '!/', depth + 1)


def scan(item):
    package, archive = item
    path = Path(archive)
    before = path.stat()
    found, dexes, abis = {}, [], set()
    result = {'package': package, 'archive': path.name, 'archive_bytes': before.st_size,
              'archive_mtime_ns': before.st_mtime_ns, 'status': 'ok', 'families': found, 'dex': dexes}
    try:
        with zipfile.ZipFile(path) as z:
            for name in z.namelist():
                match = re.match(r'lib/([^/]+)/', name)
                if match: abis.add(match[1])
            for name, data in dex_blobs(z):
                if not data.startswith(b'dex\n'):
                    raise ValueError('unsupported DEX header')
                strings = struct.unpack_from('<I', data, 0x3c)[0]
                count, offset = struct.unpack_from('<II', data, 0x40)
                if offset + count * 4 > len(data): raise ValueError('invalid type table')
                dexes.append({'entry': name, 'sha256': hashlib.sha256(data).hexdigest(),
                              'bytes': len(data), 'type_references': count})
                for i in range(count):
                    index = struct.unpack_from('<I', data, offset + i * 4)[0]
                    start = struct.unpack_from('<I', data, strings + index * 4)[0]
                    for _ in range(5):
                        b = data[start]; start += 1
                        if b < 128: break
                    if not data.startswith((b'Lcom/google/', b'Lcom/android/billingclient/'), start): continue
                    end = data.index(b'\0', start)
                    descriptor = data[start:end]
                    for family, prefixes in PREFIXES.items():
                        if family == 'gms:ads-api-excluding-identifier' and descriptor.startswith(b'Lcom/google/android/gms/ads/identifier/'):
                            continue
                        if descriptor.startswith(prefixes):
                            evidence = found.setdefault(family, {'type_hits': 0, 'examples': []})
                            evidence['type_hits'] += 1
                            value = descriptor.decode('utf-8', 'replace')
                            if len(evidence['examples']) < 3 and value not in evidence['examples']:
                                evidence['examples'].append(value)
        after = path.stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            result['status'] = 'input-changed-during-scan'
        elif not dexes: result['status'] = 'no-dex'
    except Exception as exc:
        result['status'] = 'error'
        result['error'] = type(exc).__name__ + ': ' + str(exc)[:200]
    result['completed_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    return result


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    ranked = list(csv.DictReader((CORPUS / 'hk-top1000.csv').open()))
    packages = {r['appId'] for r in ranked}
    grouped = {}
    for path in sorted((CORPUS / 'dl').iterdir()):
        package = path.name.split('@')[0]
        if path.is_file() and package in packages: grouped.setdefault(package, []).append(str(path))
    items = [(pkg, paths[0]) for pkg, paths in sorted(grouped.items())]
    result = {'generated_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'method': 'All classes*.dex type tables, including nested APK splits; presence is not execution or a confirmed blocker.',
              'families': FAMILIES, 'ranked': len(ranked), 'available_archives': len(items),
              'missing_archives': sorted(packages - grouped.keys()),
              'multiple_candidates': {k: [Path(p).name for p in v] for k, v in grouped.items() if len(v) > 1},
              'per_app': {}}
    start = time.monotonic()
    with concurrent.futures.ProcessPoolExecutor(max_workers=4) as pool:
        jobs = [pool.submit(scan, item) for item in items]
        for future in concurrent.futures.as_completed(jobs):
            row = future.result()
            result['per_app'][row['package']] = row
            n = len(result['per_app'])
            if n % 50 == 0 or n == len(jobs):
                (OUT / 'google-static-scan.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
                print(f'{n}/{len(jobs)} scanned in {time.monotonic()-start:.1f}s', flush=True)
    result['completed_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    result['counts'] = {family: sum(family in r['families'] and r['status'] == 'ok' for r in result['per_app'].values()) for family in FAMILIES}
    (OUT / 'google-static-scan.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'counts': result['counts'], 'errors': {k: r['status'] for k, r in result['per_app'].items() if r['status'] != 'ok'}}), flush=True)


if __name__ == '__main__': main()
