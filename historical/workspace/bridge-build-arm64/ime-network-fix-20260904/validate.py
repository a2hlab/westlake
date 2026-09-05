#!/usr/bin/env python3
"""Validate saved build/device evidence; does not contact or mutate a device."""
import hashlib
import json
import re
import struct
import unittest
import zipfile
from pathlib import Path

from fetch_inputs import ROOT, PINS


class RepairChecks(unittest.TestCase):
    def test_pinned_inputs(self):
        for name, digest in PINS.items():
            path = ROOT / 'original-fw' / (name.replace('_', '-') + '.jar')
            self.assertEqual(hashlib.md5(path.read_bytes()).hexdigest(), digest, name)

    def test_multidex_and_resources_preserved(self):
        for old in (ROOT / 'original-fw').glob('*.jar'):
            with zipfile.ZipFile(old) as before, zipfile.ZipFile(ROOT / 'fw' / old.name) as after:
                self.assertEqual(set(before.namelist()), set(after.namelist()), old.name)
                for entry in before.namelist():
                    if entry != 'classes.dex':
                        self.assertEqual(before.read(entry), after.read(entry), (old.name, entry))

    def test_only_planned_boot_jars_changed(self):
        planned = {'core-libart.jar', 'okhttp.jar', 'bouncycastle.jar',
                   'adapter-runtime-bcp.jar', 'oh-adapter-framework.jar'}
        changed = {p.name for p in (ROOT / 'original-fw').glob('*.jar')
                   if p.read_bytes() != (ROOT / 'fw' / p.name).read_bytes()}
        self.assertEqual(changed, planned)

    def test_native_abi_and_unchanged_jit(self):
        for name in ['liboh_ime_helper_capi.so', 'liboh_network_jni.so']:
            data = (ROOT / name).read_bytes()
            self.assertEqual(data[:5], b'\x7fELF\x02')
            self.assertEqual(struct.unpack_from('<H', data, 18)[0], 183)
        self.assertEqual(hashlib.md5((ROOT / 'runtime/libart.so').read_bytes()).hexdigest(),
                         'c8b5e7352662109ddcb9ca3b97020894')

    def test_device_network_and_negative_trust_controls(self):
        log = (ROOT / 'control05-child32588.log').read_text()
        for marker in ['SHA256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad PASS=true',
                       'JAVA_VENDOR=The Android Project', 'VALID_CHAIN=PASS',
                       'untrusted.pem REJECT=PASS', 'expired.pem REJECT=PASS',
                       'CONST IP_MULTICAST_ALL=49', 'UDP_CREATE=PASS',
                       'UDP_LOOPBACK 127.0.0.1 PASS=true', 'UDP_LOOPBACK ::1 PASS=true',
                       'DNS=PASS', 'HTTPS_STATUS=200']:
            self.assertIn('[IME_NET_CONTROL] ' + marker, log)
        self.assertEqual(log.count('[IME_NET_CONTROL] UDP_TIMEOUT=PASS'), 2)
        self.assertRegex(log, r'SYSTEM_TRUST=adapter.security.OhSystemTrustManager ROOTS=[1-9][0-9]*')
        self.assertNotRegex(log, r'\[IME_NET_CONTROL\].*(?:=FAIL|FAIL=|CRYPTO_FAIL|HTTPS_FAIL)')

    def test_ime_remains_explicitly_unverified(self):
        log = (ROOT / 'child31091.log').read_text()
        self.assertIn('[OH_IME_CAPI] show focus=-2 attach/show=12800003', log)
        self.assertIn('[IME_NET_CONTROL] HELPER_SHOW=false', log)
        self.assertIn('hasWindowFocus=false', log)

    def test_toutiao_feed_evidence(self):
        tree = (ROOT / 'toutiao01-settled.tree').read_text()
        log = (ROOT / 'child29738.log').read_text()
        self.assertRegex(tree, r'RECYCLER adapter=1 itemCount=[1-9][0-9]* childCount=[1-9]')
        self.assertGreaterEqual(tree.count('FeedTitleTextView'), 2)
        self.assertGreaterEqual(tree.count('failed=0 data=0 fetched=CloseableReference'), 2)
        self.assertIn('HANDSHAKE OK host=api16-normal-lq.toutiaoapi.com', log)
        self.assertNotRegex(log, r'(?:JNI DETECTED ERROR|Fatal signal|SIGSEGV|SIGABRT)')


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(RepairChecks)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    inventory = {}
    paths = [ROOT / 'run.sh', ROOT / 'run-common.sh', ROOT / 'control.apk']
    for folder in ['fw', 'boot', 'runtime']:
        paths += [p for p in (ROOT / folder).iterdir()
                  if p.suffix in {'.jar', '.art', '.oat', '.vdex', '.so'} or p.name == 'appspawn-x']
    for path in sorted(paths):
        inventory[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    report = dict(checks=result.testsRun, failures=len(result.failures), errors=len(result.errors),
                  ime_status='BLOCKED_NO_OH_WINDOW_FOCUS',
                  network_status='HTTPS_IPV4_IPV6_UDP_CONTROLS_PASS', sha256=inventory)
    (ROOT / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    raise SystemExit(0 if result.wasSuccessful() else 1)
