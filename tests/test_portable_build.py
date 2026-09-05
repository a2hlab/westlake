import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class PortableBuildTests(unittest.TestCase):
    def test_unsigned_archive_deterministic(self):
        builder = module('build_apps')
        with tempfile.TemporaryDirectory(prefix='a2hlab-test-') as tmp:
            first, second = Path(tmp) / 'a.hap', Path(tmp) / 'b.hap'
            builder.archive(first, {'z': b'last', 'a': b'first'})
            builder.archive(second, {'a': b'first', 'z': b'last'})
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as hap:
                self.assertEqual(hap.namelist(), ['a', 'z'])
                self.assertTrue(all(i.date_time == (1980, 1, 1, 0, 0, 0) for i in hap.infolist()))
            with self.assertRaises(FileExistsError):
                builder.archive(first, {})

    def test_native_refuses_reused_output(self):
        with tempfile.TemporaryDirectory(prefix='a2hlab-test-') as tmp:
            with self.assertRaisesRegex(ValueError, 'NEW directory'):
                module('build_native').build(Path('/missing-sdk'), Path(tmp))

    def test_native_requires_sdk(self):
        with tempfile.TemporaryDirectory(prefix='a2hlab-test-') as tmp:
            with self.assertRaisesRegex(ValueError, 'Incomplete OH native SDK'):
                module('build_native').build(Path(tmp) / 'missing-sdk', Path(tmp) / 'out')

    def test_tls_registration_inventory(self):
        generated = module('build_native').tls_unit()
        for name in ['nativeHandshake', 'nativeRead', 'nativeWrite', 'nativePeerCert',
                     'nativePeerChain', 'nativeInfo', 'nativeClose']:
            self.assertEqual(generated.count('M("' + name + '"'), 1)


if __name__ == '__main__':
    unittest.main()
