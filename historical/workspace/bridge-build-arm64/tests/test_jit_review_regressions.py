"""Host regressions for the actual build recipes and device-readiness shell.

Run: python3 -m unittest discover -s tests -p test_jit_review_regressions.py -v
No device state is changed. Optional JIT_REVIEW_BUILD selects cross-built ABI
objects for emitted-instruction and incremental-dependency checks.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ART = Path('/home/dspfac/art-latest')
LLVM = Path('/home/dspfac/openharmony/prebuilts/clang/ohos/linux-x86_64/llvm/bin')


class Readiness(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='jit-ready-test-')
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)

    def shell(self, code):
        return subprocess.run(
            ['bash', '-c', 'set -uo pipefail\nsource ' +
             shlex.quote(str(ROOT / 'ttwalk_ready.sh')) + '\n' + code],
            text=True, capture_output=True, cwd=self.path, timeout=15)

    def test_live_identity_and_fatal_precedence(self):
        (self.path / 'child.log').write_text('healthy\n')
        result = self.shell('dev() { bash -c "$1"; }; ready_child_state $$ child.log')
        self.assertRegex(result.stdout, r'^LIVE:[0-9]+\n$')
        (self.path / 'child.log').write_text('FATAL EXCEPTION: main\n')
        result = self.shell('dev() { bash -c "$1"; }; ready_child_state 99999999 child.log')
        self.assertEqual(result.stdout, 'FATAL\n')

    def test_dead_and_unreadable_fail_closed(self):
        (self.path / 'child.log').write_text('healthy\n')
        for log, expected in [('child.log', 'DEAD'), ('absent.log', 'ERROR')]:
            with self.subTest(log=log):
                result = self.shell(f'dev() {{ bash -c "$1"; }}; ready_child_state 99999999 {log}')
                self.assertEqual(result.stdout.strip(), expected)

    def surface(self, owner, buffered=True, first_byte=1):
        # Execute the real remote query locally, replacing only the RS service.
        tree = self.path / 'tree'
        tree.write_text(
            f'SURFACE_NODE[{owner * 2**32 + 1}], Name '
            '[com.ss.android.article.news/com.ss.android.article.news.activity.MainActivity], '
            'Visible: 1\n')
        return self.shell(f'''
dev() {{ eval "$1"; }}
hidumper() {{
  if [ "$4" = RSTree ]; then cat tree
  else echo 'totalBuffersMemSize = {10 if buffered else 0}'; fi
}}
ready_view_and_surface 42 child.log {first_byte}
''').stdout.strip()

    def test_surface_requires_pid_buffer_and_fresh_nonempty_view(self):
        line = '[3/OH_InputBridge] VT View rect=[0,0 1200x1920]\n'
        (self.path / 'child.log').write_text(line)
        self.assertEqual(self.surface(42), 'READY')
        self.assertEqual(self.surface(43), '')
        self.assertEqual(self.surface(42, buffered=False), '')
        self.assertEqual(self.surface(42, first_byte=len(line) + 1), '')
        (self.path / 'child.log').write_text(line + line)
        self.assertEqual(self.surface(42, first_byte=len(line) + 1), 'READY')
        (self.path / 'child.log').write_text(line.replace('1200x1920', '0x0'))
        self.assertEqual(self.surface(42), '')

    def gate(self, states, ready=True):
        (self.path / 'states').write_text('\n'.join(states) + '\n')
        (self.path / 'clock').write_text('0\n')
        return self.shell('''
ASX=/logs; LAUNCH_TIMEOUT_SECONDS=1
sleep() { :; }
ready_monotonic_ms() { local n; read -r n < clock; n=$((n + 100)); echo "$n" > clock; echo "$n"; }
childlog() { echo /logs/adapter_child_42.stderr; }
ready_child_state() { local s; read -r s < states; sed -i '1d' states; echo "$s"; }
ready_request_view() { echo 123; }
dev() { if [[ "$1" == *side-channels* ]]; then echo YES; else echo "$1" >> writes; fi; }
proc_cpu_ticks() { echo 17; }
start_touch_forwarder() { echo touch >> writes; }
''' + ('ready_view_and_surface() { echo READY; }\n' if ready else
       'ready_view_and_surface() { :; }\n') + 'wait_for_ready\n')

    def test_fatal_and_dead_cannot_pass_ready_oracles(self):
        for state in ['FATAL', 'DEAD', 'ERROR', '']:
            with self.subTest(state=state):
                result = self.gate([state])
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn('LAUNCHED', result.stdout)
                self.assertFalse((self.path / 'writes').exists())

    def test_pid_reuse_and_post_gate_death_fail(self):
        for states in [['LIVE:100', 'LIVE:101'], ['LIVE:100', 'LIVE:100', 'DEAD']]:
            with self.subTest(states=states):
                result = self.gate(states)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn('LAUNCHED', result.stdout)

    def test_success_waits_for_post_gate_and_timeout_never_passes(self):
        result = self.gate(['LIVE:100'] * 3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('LAUNCHED child=42 identity=LIVE:100 gateMs=', result.stdout)
        self.assertIn('walkpid', (self.path / 'writes').read_text())
        result = self.gate(['LIVE:100'] * 20, ready=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('within 1s', result.stdout)


class BuildContracts(unittest.TestCase):
    def test_linker_rejects_incomplete_declared_inventory(self):
        linker = ROOT / 'link_libart_clean_real_nterp.sh'
        with tempfile.TemporaryDirectory(prefix='jit-link-test-') as tmp:
            root = Path(tmp)
            # Satisfy the old, partial required-object list. The full declared
            # inventory must still reject this tree before invoking the linker.
            required = re.findall(r'"\$BUILD/([^"\n]+\.o)"', linker.read_text())
            for name in required + ['runtime/stale-extra.o']:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'fixture')
            boundary = root / 'boundary.so'
            boundary.write_bytes(b'fixture')
            output = root / 'libart.so'
            result = subprocess.run(['bash', str(linker)], env=dict(os.environ,
                WL_CLEAN_ART_BUILD_DIR=str(root), WL_CLEAN_ART_OUTPUT=str(output),
                WL_NTERP_RUNTIME_BOUNDARY_SO=str(boundary)), text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('missing declared object:', result.stderr)
            self.assertFalse(output.exists())

    def test_runtime_policy_bodies(self):
        source = Path('/home/dspfac/aosp-art-15/runtime/jit/jit.cc').read_text()
        names = ['IsMethodCompilationAllowed', 'IsOsrAllowed',
                 'MaybeEnqueueWestlakeSafeBootCompilation']
        bodies = []
        for name in names:
            match = re.search(r'^bool Jit::' + name + r'\(.*?^\}', source, re.M | re.S)
            self.assertIsNotNone(match, name)
            bodies.append(match.group())
        with tempfile.TemporaryDirectory(prefix='jit-policy-test-') as tmp:
            root = Path(tmp)
            (root / 'policy_bodies.inc').write_text('\n'.join(bodies))
            subprocess.run(['c++', '-std=c++17', '-include', 'initializer_list',
                '-I', str(root), str(ROOT / 'tests/jit_policy_fixture.cc'),
                '-o', str(root / 'policy-test')], check=True, capture_output=True, timeout=30)
            subprocess.run([str(root / 'policy-test')], check=True, capture_output=True, timeout=10)

    def test_compiler_failures_propagate(self):
        # Seed just the generated-header prerequisite; no real compilation is
        # needed to exercise the actual recipes' error status.
        for target in ['compiler/jit/jit_compiler.o', 'ziparchive', 'fmtlib',
                       'nativehelper', 'sigchain', 'link-stubs']:
            with self.subTest(target=target), tempfile.TemporaryDirectory(prefix='jit-make-test-') as tmp:
                build = Path(tmp)
                (build / 'asm_arm64').mkdir()
                (build / 'asm_arm64/asm_defines.h').touch()
                requested = str(build / target) if target.endswith('.o') else target
                result = subprocess.run(
                    ['make', '-sf', 'Makefile.ohos-arm64', 'CXX=false', f'BUILDDIR={build}',
                     '-o', str(build / 'asm_arm64/asm_defines.h'), requested],
                    cwd=ART, text=True, capture_output=True, timeout=15)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn('FAIL:', result.stdout)
                self.assertFalse(any(build.rglob('*.o')))

    @unittest.skipUnless(os.environ.get('JIT_REVIEW_BUILD'), 'set JIT_REVIEW_BUILD for target ABI checks')
    def test_target_abi_and_incremental_rebuild(self):
        build = Path(os.environ['JIT_REVIEW_BUILD']).resolve()
        asm = build / 'asm_arm64'
        for obj, symbol, pattern in [
            ('mterp_arm64ng.o', 'nterp_op_invoke_virtual', r'add\s+x16, x19, #2656'),
            ('mterp_arm64ng.o', 'nterp_op_new_instance', r'ldr\s+x30, \[x19, #464\]'),
            ('jni_entrypoints_arm64.o', 'art_jni_dlsym_lookup_stub', r'#1572864')]:
            result = subprocess.check_output(
                [str(LLVM / 'llvm-objdump'), '-d', '--no-show-raw-insn',
                 f'--disassemble-symbols={symbol}', str(asm / obj)], text=True)
            self.assertRegex(result, pattern)
        jni = str(asm / 'jni_entrypoints_arm64.o')
        result = subprocess.check_output(
            ['make', '-nf', 'Makefile.ohos-arm64', f'BUILDDIR={build}',
             '-W', str(asm / 'asm_defines.h'), jni], cwd=ART, text=True)
        self.assertIn('assembler-with-cpp', result)
        result = subprocess.check_output(
            ['make', '-nf', 'Makefile.ohos-arm64', f'BUILDDIR={build}',
             '-W', '/home/dspfac/aosp-art-15/runtime/thread.h',
             str(asm / 'asm_defines.h')], cwd=ART, text=True)
        self.assertIn('make_header.py', result)
        defines = (asm / 'asm_defines.h').read_text()
        self.assertRegex(defines, r'ACCESS_FLAGS_METHOD_IS_CRITICAL_NATIVE\s+0x100000')
        self.assertRegex(defines, r'THREAD_INTERPRETER_CACHE_OFFSET\s+0xa60')

    def test_pinned_wrapper_overrides_are_locked(self):
        # Run the actual wrapper with relocated ASX and mock hash/exec tools.
        # Runtime/image hashing is not under test here; effective configuration
        # and app/provider/delegate rejection are.
        for name in ['run-jit-v142-nterp-trust-matched-dexcache-verify-20260904.sh',
                     'run-jit-v160-reproducible-osc-adapter-verify-20260905.sh']:
            with self.subTest(name=name), tempfile.TemporaryDirectory(prefix='jit-runner-test-') as tmp:
                root = Path(tmp)
                source = (ROOT / name).read_text().replace('ASX=/data/local/tmp/asx', f'ASX={root}')
                # Replace the checksum implementation with a logging contract
                # double; leave the wrapper's configuration and calls intact.
                source = re.sub(r'expect_md5\(\) \{.*?\n\}', '''expect_md5() {
  case "$2" in */wrong.apk|*/wrong-webview.apk) return 1;; esac
  echo "$2" >> "$ASX/checked"
}''', source, count=1, flags=re.S)
                runner = root / 'runner.sh'
                runner.write_text(source)
                shim = root / 'sh'
                shim.write_text('#!/bin/bash\nprintf "FW=%s\\nAPK=%s\\n" "$ASX_FW_DIR" "$ASX_APK_PATH"\n')
                shim.chmod(0o755)
                env = dict(os.environ, PATH=f'{root}:' + os.environ['PATH'], ASX_FW_DIR='/wrong/fw')
                result = subprocess.run(['/bin/sh', str(runner)], env=env, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f'FW={root}/fw', (root / 'asx.err').read_text())
                checks = (root / 'checked').read_text()
                for suffix in ['toutiao.apk', 'webview-t.apk', 'run-jit-aot-candidate-20260902.sh']:
                    self.assertIn(suffix, checks)
                for override in ['ASX_APK_PATH', 'ASX_WEBVIEW_APK']:
                    result = subprocess.run(['/bin/sh', str(runner)],
                        env=dict(env, **{override: '/wrong.apk'}), capture_output=True)
                    self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
