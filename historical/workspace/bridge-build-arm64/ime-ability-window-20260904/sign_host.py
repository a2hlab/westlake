#!/usr/bin/env python3
"""Sign only the test host using the SDK's published OpenHarmony development keys."""
import json
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parent
LIB = Path('/home/dspfac/ohos-sdk-6.1/linux/toolchains/lib')
OUT = ROOT / 'host-out'
profile = json.loads((LIB / 'UnsgnedReleasedProfileTemplate.json').read_text())
profile['bundle-info']['bundle-name'] = 'org.westlake.imehost'
profile['validity'] = {'not-before': 1767225600, 'not-after': 1924991999}
profile['acls']['allowed-acls'] = []
(OUT / 'profile.json').write_text(json.dumps(profile, indent=2))
# This is the public sample-key password distributed with the OHOS SDK.
common = ['-signAlg', 'SHA256withECDSA', '-mode', 'localSign',
    '-keystoreFile', str(LIB / 'OpenHarmony.p12'),
    '-keyPwd', '123456', '-keystorePwd', '123456']
tool = ['java', '-jar', str(LIB / 'hap-sign-tool.jar')]
subprocess.run(tool + ['sign-profile', '-keyAlias', 'openharmony application profile release',
    '-profileCertFile', str(LIB / 'OpenHarmonyProfileRelease.pem'),
    '-inFile', str(OUT / 'profile.json'), '-outFile', str(OUT / 'profile.p7b')] + common, check=True)
subprocess.run(tool + ['sign-app', '-keyAlias', 'openharmony application release',
    '-appCertFile', '/home/dspfac/developtools/hapsigner/dist/OpenHarmonyApplication.pem',
    '-profileFile', str(OUT / 'profile.p7b'), '-inFile', str(OUT / 'imehost-unsigned.hap'),
    '-outFile', str(OUT / 'imehost.hap'), '-signCode', '1'] + common, check=True)
