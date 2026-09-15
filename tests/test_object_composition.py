"""Object replacement must preserve provenance and reject ambiguous ownership."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'tools/combine_native_objects.py'


class ObjectComposition(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def build(self, name, contents):
        root = self.root / name
        (root / 'objects').mkdir(parents=True)
        (root / 'objects/renderer.o').write_bytes(contents)
        (root / 'artifacts.json').write_text(json.dumps({'errors': [], 'artifacts': {
            'objects/renderer.o': {'bytes': len(contents),
                'sha256': hashlib.sha256(contents).hexdigest()}}}))
        return root

    def run_build(self, builds, replace=()):
        args = [sys.executable, str(SCRIPT), '--out', str(self.root / 'out')]
        for build in builds:
            args.extend(['--build', str(build)])
        for name in replace:
            args.extend(['--replace', name])
        return subprocess.run(args, text=True, capture_output=True)

    def test_explicit_replacement_records_both_hashes(self):
        before, after = self.build('before', b'old'), self.build('after', b'new')
        result = self.run_build([before, after], ['objects/renderer.o'])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'out/objects/renderer.o').read_bytes(), b'new')
        report = json.loads((self.root / 'out/artifacts.json').read_text())
        replacement = report['replacements']['objects/renderer.o']
        self.assertEqual(replacement['from_sha256'], hashlib.sha256(b'old').hexdigest())
        self.assertEqual(replacement['to_sha256'], hashlib.sha256(b'new').hexdigest())

    def test_duplicate_requires_explicit_replacement(self):
        result = self.run_build([self.build('a', b'a'), self.build('b', b'b')])
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / 'out').exists())

    def test_replacement_cannot_mask_three_owners(self):
        builds = [self.build(name, name.encode()) for name in ('a', 'b', 'c')]
        result = self.run_build(builds, ['objects/renderer.o'])
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / 'out').exists())

    def test_unused_replacement_fails(self):
        result = self.run_build([self.build('a', b'a')], ['objects/renderer.o'])
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / 'out').exists())


if __name__ == '__main__':
    unittest.main()
