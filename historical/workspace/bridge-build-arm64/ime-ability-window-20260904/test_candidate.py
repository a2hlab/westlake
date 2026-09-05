"""Offline artifact/transport regression tests; runtime assertions use saved logs."""
import hashlib
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import unittest
import zipfile

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'candidate04'
OLD = ROOT.parent / 'ime-network-fix-20260904'

class CandidateTests(unittest.TestCase):
    def test_manifest(self):
        for name, expected in json.loads((OUT / 'manifest.json').read_text()).items():
            self.assertEqual(hashlib.sha256((OUT / name).read_bytes()).hexdigest(), expected, name)

    def test_runtime_pins(self):
        expected = {'libart.so': 'c8b5e7352662109ddcb9ca3b97020894',
                    'liboh_adapter_bridge.so': 'b58ed9c01a93df2601e9c0671afdf2df',
                    'appspawn-x': 'a8cb923655f0439dc54f0c0ef1731d8f'}
        for name, digest in expected.items():
            self.assertEqual(hashlib.md5((OUT / 'runtime' / name).read_bytes()).hexdigest(), digest)
        self.assertEqual((OUT / 'runtime/liboh_network_jni.so').read_bytes(),
                         (OLD / 'runtime/liboh_network_jni.so').read_bytes())

    def test_nonreplaced_jar_entries(self):
        replaced = {'oh-adapter-framework.jar': {'classes.dex'}, 'apache-xml.jar': {'classes4.dex'}}
        for path in (OLD / 'fw').glob('*.jar'):
            with zipfile.ZipFile(path) as old, zipfile.ZipFile(OUT / 'fw' / path.name) as new:
                self.assertEqual(old.namelist(), new.namelist())
                for name in old.namelist():
                    if name not in replaced.get(path.name, set()):
                        self.assertEqual(old.read(name), new.read(name), (path.name, name))

    def test_both_focus_definitions(self):
        rel = 'adapter/window/SessionStageBridge.smali'
        first = (OUT / 'apache-smali' / rel).read_text()
        self.assertEqual(first, (OUT / 'adapter-smali' / rel).read_text())
        self.assertIn('WindowCallbackBridge;->onUpdateFocusStatus(Z)V', first)

    def test_minimal_host(self):
        with zipfile.ZipFile(ROOT / 'host-out/imehost.hap') as hap:
            self.assertFalse(any(name.endswith('.so') for name in hap.namelist()))
            module = json.loads(hap.read('module.json'))
            self.assertEqual(module['app']['bundleName'], 'org.westlake.imehost')
            self.assertEqual(module['module']['requestPermissions'], [{'name': 'ohos.permission.INTERNET'}])
        profile = json.loads((ROOT / 'host-out/profile.json').read_text())
        self.assertEqual(profile['bundle-info']['apl'], 'normal')
        self.assertEqual(profile['acls']['allowed-acls'], [])

    def test_host_request_identity_and_packing(self):
        data = (ROOT / 'host-request.bin').read_bytes()
        magic, kind, length, seq, count, name = struct.unpack_from('<5I256s', data)
        self.assertEqual((magic, kind, length, count), (0xef201234, 0, len(data), 4))
        self.assertEqual(name.rstrip(b'\0'), b'org.westlake.imehost')
        fields = {}
        offset = 276
        while offset < len(data):
            size, typ = struct.unpack_from('<HH', data, offset)
            self.assertGreaterEqual(size, 4)
            self.assertEqual(size % 4, 0)
            fields[typ] = data[offset + 4:offset + size]
            offset += size
        self.assertEqual(offset, len(data))
        state = json.loads((ROOT / 'toutiao01-processes.json').read_text())
        self.assertEqual(struct.unpack_from('<II', fields[2]), (state['host_uid'], state['host_uid']))
        self.assertEqual(struct.unpack_from('<Q', fields[5])[0], state['host_token'])

    def test_window_scoped_input(self):
        text = (ROOT / 'control04-actions.log').read_text()
        self.assertIn('subscribe window=37 rc=0', text)
        self.assertIn('window=37 action=0 x=600 y=180 rc=0', text)
        self.assertIn('window=37 action=1 x=600 y=180 rc=0', text)
        # Keyboard touches must not also be injected into the Android window.
        for line in text.splitlines():
            if 'OH_OWNED_INPUT' in line and 'action=' in line:
                self.assertIn('y=180', line)
        src = Path('/home/dspfac/bridge-build/src/framework/window/jni/oh_owned_input_bridge.cpp').read_text()
        self.assertNotIn('AddMonitor(', src)
        self.assertIn('owns(event->GetTargetWindowId())', src)

    def test_live_control_edit_and_lifecycle_evidence(self):
        text = (ROOT / 'control04-actions.log').read_text()
        self.assertEqual(text.count('IMM_SHOW=true WINDOW_FOCUS=true'), 2)
        self.assertIn('EDITOR_ACTION=3 TEXT=wl', text)
        self.assertIn('runOp(1) done', text)
        self.assertIn('keyboard status=1', text)
        self.assertIn('keyboard status=2', text)
        self.assertIn('itm=1 "wl"', (ROOT / 'control04-edited.tree').read_text())

    def test_live_network_evidence(self):
        text = (ROOT / 'control04-initial.log').read_text()
        for expected in ['PASS=true', 'ROOTS=118', 'VALID_CHAIN=PASS',
                         'untrusted.pem REJECT=PASS', 'expired.pem REJECT=PASS',
                         'UDP_LOOPBACK 127.0.0.1 PASS=true', 'UDP_LOOPBACK ::1 PASS=true',
                         'DNS=PASS', 'HTTPS_STATUS=200']:
            self.assertIn(expected, text)
        self.assertEqual(text.count('UDP_TIMEOUT=PASS'), 2)

    def test_live_toutiao_edit_and_isolation_evidence(self):
        lines = (ROOT / 'toutiao01-data-isolation.log').read_text().splitlines()
        self.assertNotEqual(lines[0].split()[0], lines[1].split()[0])
        self.assertEqual(lines[1], lines[2])
        self.assertNotEqual(lines[3], lines[4])
        self.assertEqual(lines[4], lines[5])
        tree = (ROOT / 'toutiao01-typed.tree').read_text()
        self.assertTrue(any('SearchAutoCompleteTextView' in s and '"openai"' in s for s in tree.splitlines()))
        log = (ROOT / 'toutiao01-ime-submit.log').read_text()
        self.assertIn('runOp(1) done', log)
        self.assertIn('detach=0', log)

class TransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='ime-host-transport-')
        cls.root = Path(cls.temp.name)
        cls.bin = cls.root / 'host-spawn'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-O2', str(ROOT / 'host_spawn.c'),
                        '-o', str(cls.bin)], check=True)

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def test_truncated_and_oversized_requests_rejected(self):
        for index, data in enumerate([b'', b'\x34', bytes(275), bytes(4096), bytes(4097)]):
            request = self.root / ('bad-' + str(index))
            request.write_bytes(data)
            p = subprocess.run([str(self.bin), str(self.root / 'absent.sock'), str(request)], capture_output=True)
            self.assertEqual(p.returncode, 2)

    def test_fragmented_response(self):
        path = self.root / 'transport.sock'
        data = (ROOT / 'host-request.bin').read_bytes()
        request = self.root / 'good.bin'
        request.write_bytes(data)
        with socket.socket(socket.AF_UNIX) as server:
            server.bind(str(path)); server.listen(1); server.settimeout(5)
            seen = []
            def respond():
                with server.accept()[0] as client:
                    buf = bytearray()
                    while len(buf) < len(data): buf.extend(client.recv(len(data) - len(buf)))
                    seen.append(bytes(buf))
                    reply = bytearray(300)
                    struct.pack_into('<I', reply, 0, 0xef201234)
                    struct.pack_into('<I', reply, 8, 300)
                    struct.pack_into('<ii', reply, 276, 0, 12345)
                    for offset in range(0, 300, 7): client.sendall(reply[offset:offset + 7])
            thread = threading.Thread(target=respond)
            thread.start()
            p = subprocess.run([str(self.bin), str(path), str(request)], capture_output=True, text=True, timeout=5)
            thread.join(5)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(seen, [data])
        self.assertIn('result=0 pid=12345', p.stdout)

if __name__ == '__main__': unittest.main(verbosity=2)
