"""Build and SDK-sign the separate, offline Pinyin HAP; preserve upstream code."""
from pathlib import Path
import hashlib
import json
import subprocess
import zipfile
ROOT = Path(__file__).resolve().parent
SDK = Path('/home/dspfac/ohos-sdk-6.1/linux')
OUT = ROOT / 'out'
OUT.mkdir(exist_ok=True)
(OUT / 'resources-compiled').mkdir(exist_ok=True)
revision = subprocess.check_output(['git','-C',str(ROOT/'upstream'),'rev-parse','HEAD'],text=True).strip()
assert revision == 'cd27df54150e5d96d86757dc7315ead69d469d9b'
assert not subprocess.check_output(['git','-C',str(ROOT/'upstream'),'status','--porcelain']), 'Upstream inputs changed'
def run(*args): subprocess.run([str(x) for x in args], check=True)
# Device resource manager accepts the V1 index format, not SDK 6.1 RestoolV2.
RESTOOL = Path('/home/dspfac/openharmony/out/sdk/ohos-sdk/linux/toolchains/restool')
run(RESTOOL,'-i',ROOT,'-j',ROOT/'module.json',
    '-p','org.westlake.pinyin','-o',OUT/'resources-compiled','-r',OUT/'ResourceTable.js','-f')
entries = {'ServiceExtAbility':'ets/ServiceExtAbility/ServiceExtAbility.abc',
    'EntryAbility':'ets/entryability/EntryAbility.abc','Keyboard':'ets/pages/Keyboard.abc','Settings':'ets/pages/Settings.abc'}
for name in entries:
    run(SDK/'ets/build-tools/ets-loader/bin/ark/build/bin/es2abc','--target-api-version=20',
        '--output='+str(OUT/(name+'.abc')),ROOT/(name+'.js'))
with zipfile.ZipFile(OUT/'pinyin-unsigned.hap','w',zipfile.ZIP_DEFLATED) as hap:
    for path in (OUT/'resources-compiled').rglob('*'):
        if path.is_file(): hap.write(path,str(path.relative_to(OUT/'resources-compiled')))
    for name, path in entries.items(): hap.write(OUT/(name+'.abc'),path)
    hap.write(OUT/'libwestlake_pinyin.so','libs/arm64-v8a/libwestlake_pinyin.so')
    hap.write(SDK/'native/llvm/lib/aarch64-linux-ohos/libc++_shared.so','libs/arm64-v8a/libc++_shared.so')
    hap.write(ROOT/'upstream/entry/src/main/resources/rawfile/dict_pinyin.dat','resources/rawfile/dict_pinyin.dat')
    hap.write(ROOT/'upstream/LICENSE','resources/rawfile/PINYIN_LICENSE.txt')
    hap.writestr('resources/rawfile/NOTICE.txt','Pinyin decoder: Copyright (C) 2009 The Android Open Source Project.\n'
        'OHOS port: https://gitee.com/ohos_port/applications_inputmethod @ '+revision+'.\n'
        'Apache License 2.0; engine and dictionary retained unchanged. New Westlake UI and N-API boundary.\n')
    hap.writestr('.pages.info',(ROOT/'resources/base/profile/main_pages.json').read_text())
lib = SDK/'toolchains/lib'
profile = json.loads((lib/'UnsgnedReleasedProfileTemplate.json').read_text())
profile['bundle-info']['bundle-name'] = 'org.westlake.pinyin'
profile['acls']['allowed-acls'] = []
profile['validity'] = {'not-before':1767225600,'not-after':1924991999}
(OUT/'profile.json').write_text(json.dumps(profile,indent=2))
# Public SDK sample-key password, not a private production credential.
common = ['-signAlg','SHA256withECDSA','-mode','localSign','-keystoreFile',lib/'OpenHarmony.p12',
    '-keyPwd','123456','-keystorePwd','123456']
tool = ['java','-jar',lib/'hap-sign-tool.jar']
run(*tool,'sign-profile','-keyAlias','openharmony application profile release',
    '-profileCertFile',lib/'OpenHarmonyProfileRelease.pem','-inFile',OUT/'profile.json','-outFile',OUT/'profile.p7b',*common)
run(*tool,'sign-app','-keyAlias','openharmony application release',
    '-appCertFile','/home/dspfac/developtools/hapsigner/dist/OpenHarmonyApplication.pem',
    '-profileFile',OUT/'profile.p7b','-inFile',OUT/'pinyin-unsigned.hap','-outFile',OUT/'pinyin.hap','-signCode','1',*common)
files = [ROOT/(name+'.js') for name in entries] + [ROOT/'pinyin_napi.cpp',ROOT/'module.json',
    OUT/'pinyin.hap',OUT/'libwestlake_pinyin.so',ROOT/'upstream/entry/src/main/resources/rawfile/dict_pinyin.dat']
manifest = {'upstream':revision,'files':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('PINYIN_BUILD_OK',manifest['files']['out/pinyin.hap'])
