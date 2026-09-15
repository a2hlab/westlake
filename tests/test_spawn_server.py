"""Exercise the real socket receiver under ASan, including buffer growth."""
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import unittest


class SpawnServerSocket(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('g++')
        if compiler is None:
            raise unittest.SkipTest('g++ with AddressSanitizer is required')
        cls.temp = tempfile.TemporaryDirectory(prefix='spawn-server-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        (cls.root / 'hilog').mkdir()
        # Only the host logging backend is substituted; the receiver, parser,
        # dispatch and response code are all the production translation unit.
        (cls.root / 'hilog/log.h').write_text('''#pragma once
#define LOG_CORE 0
#define LOG_INFO 1
#define LOG_DEBUG 2
#define LOG_ERROR 3
#define LOG_WARN 4
inline int HiLogPrint(int, int, unsigned, const char*, const char*, ...) { return 0; }
''')
        (cls.root / 'main.cpp').write_text('''#include "spawn_server.h"
int main() {
    appspawnx::SpawnServer server("SourceSocketTest");
    server.setSpawnHandler([](const appspawnx::SpawnMsg& msg) {
        return msg.procName == "org.example.source" && msg.ohMsgId == 73
            && msg.uid == 12345 && msg.gid == 12346 ? 54321 : -1;
    });
    if (server.createListenSocket() != 0) return 2;
    server.run();
}
''')
        source = Path(__file__).resolve().parents[1] / 'framework/appspawn-x/src'
        cls.exe = cls.root / 'server'
        subprocess.run([compiler, '-std=c++17', '-fsanitize=address',
                        '-fno-omit-frame-pointer', '-g', '-O1',
                        '-include', 'cstddef', '-I', str(cls.root), '-I', str(source),
                        str(cls.root / 'main.cpp'), str(source / 'spawn_server.cpp'),
                        '-o', str(cls.exe)], check=True, capture_output=True)

    def test_binary_body_growth_and_repeated_requests(self):
        path = str(self.root / 'socket')
        with socket.socket(socket.AF_UNIX) as listener:
            listener.bind(path)
            listener.listen(4)
            env = dict(os.environ, OHOS_SOCKET_SourceSocketTest=str(listener.fileno()),
                       ASAN_OPTIONS='detect_leaks=0:abort_on_error=1')
            with tempfile.TemporaryFile(mode='w+b') as log:
                process = subprocess.Popen([str(self.exe)], env=env,
                                           pass_fds=(listener.fileno(),), stderr=log)
                try:
                    for padding in (0, 16384, 32000):
                        dac = struct.pack('<3I64I64s', 12345, 12346, 0,
                                          *([0] * 64), b'')
                        body = struct.pack('<HH', len(dac) + 4, 2) + dac
                        if padding:
                            body += struct.pack('<HH', padding + 4, 99) + bytes(padding)
                        request = struct.pack('<5I256s', 0xef201234, 0,
                                              276 + len(body), 73,
                                              2 if padding else 1, b'org.example.source') + body
                        with socket.socket(socket.AF_UNIX) as client:
                            client.settimeout(5)
                            client.connect(path)
                            # Send header and body separately as a stream client may.
                            client.sendall(request[:276])
                            client.sendall(request[276:])
                            response = bytearray()
                            while len(response) < 300:
                                chunk = client.recv(300 - len(response))
                                if not chunk:
                                    break
                                response.extend(chunk)
                        log.seek(0)
                        diagnostic = log.read().decode(errors='replace')
                        self.assertEqual(len(response), 300, diagnostic)
                        self.assertEqual(struct.unpack_from('<I', response, 12)[0], 73)
                        self.assertEqual(struct.unpack_from('<ii', response, 276), (0, 54321))
                    self.assertIsNone(process.poll(), diagnostic)
                    self.assertNotIn('ERROR: AddressSanitizer', diagnostic)
                finally:
                    if process.poll() is None:
                        process.terminate()
                    process.wait(timeout=5)
                    Path(path).unlink(missing_ok=True)


if __name__ == '__main__':
    unittest.main()
