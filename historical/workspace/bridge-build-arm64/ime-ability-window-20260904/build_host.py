#!/usr/bin/env python3
"""Build a minimal isolated OHOS host; no bundled system DSOs, no ART loading."""
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent
SDK = Path('/home/dspfac/ohos-sdk-6.1/linux')
TEMPLATE = Path('/home/dspfac/bridge-build-arm64/host-transparent-inspect/e15')
OUT = ROOT / 'host-out'
OUT.mkdir(exist_ok=True)
module = json.loads((TEMPLATE / 'module.json').read_text())
module['app'].update(bundleName='org.westlake.imehost', label='Westlake IME host', labelId=0)
module['module']['description'] = 'Isolated Android window and keyboard host'
module['module']['requestPermissions'] = [{'name': 'ohos.permission.INTERNET'}]
module['app']['versionCode'] = 2
for ability in module['module']['abilities']:
    ability.update(label='Westlake IME host', labelId=0)
for name in ('EntryAbility', 'Index'):
    subprocess.run([str(SDK / 'ets/build-tools/ets-loader/bin/ark/build/bin/es2abc'),
        '--target-api-version=20', '--output=' + str(OUT / (name + '.abc')),
        str(ROOT / 'host' / (name + '.js'))], check=True)
with zipfile.ZipFile(OUT / 'imehost-unsigned.hap', 'w', zipfile.ZIP_DEFLATED) as hap:
    hap.writestr('module.json', json.dumps(module))
    hap.write(OUT / 'EntryAbility.abc', 'ets/entryability/EntryAbility.abc')
    hap.write(OUT / 'Index.abc', 'ets/pages/Index.abc')
    hap.write(TEMPLATE / 'resources.index', 'resources.index')
    hap.writestr('resources/base/profile/main_pages.json', '{"src":["pages/Index"]}')
    hap.writestr('.pages.info', '{"src":["pages/Index"]}')
manifest = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in [ROOT / 'host/EntryAbility.js', ROOT / 'host/Index.js', OUT / 'imehost-unsigned.hap']}
(OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
