import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]

# build_native imports build_sqlite as a sibling. Running the tool as a script
# puts tools/ on sys.path automatically; loading it by file path here does not.
if str(ROOT / 'tools') not in sys.path:
    sys.path.insert(0, str(ROOT / 'tools'))


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

    def test_sqlite_refuses_reused_output(self):
        with tempfile.TemporaryDirectory(prefix='a2hlab-test-') as tmp:
            with self.assertRaisesRegex(ValueError, 'NEW directory'):
                module('build_sqlite').build(Path(tmp), sdk=Path('/missing-sdk'))

    def test_sqlite_requires_exactly_one_abi(self):
        builder = module('build_sqlite')
        with tempfile.TemporaryDirectory(prefix='a2hlab-test-') as tmp:
            for kwargs in ({}, {'sdk': Path('/a'), 'ndk': Path('/b')}):
                with self.assertRaisesRegex(ValueError, 'exactly one'):
                    builder.build(Path(tmp) / 'out', **kwargs)

    def test_sqlite_options_come_from_cmakelists(self):
        """The driver must not restate the option set; drift would be silent."""
        defines = module('build_sqlite').sqlite_defines()
        self.assertIn('-DSQLITE_THREADSAFE=1', defines)
        self.assertIn('-DSQLITE_TEMP_STORE=3', defines)
        self.assertTrue(all(d.startswith('-DSQLITE_') or d.startswith('-DHAVE_')
                            for d in defines), defines)
        cmake = (ROOT / 'framework/sqlite/CMakeLists.txt').read_text()
        for define in defines:
            self.assertIn(define[2:], cmake)

    def test_sqlite_sources_all_exist(self):
        builder = module('build_sqlite')
        for source in builder.C_SOURCES + builder.CXX_SOURCES:
            self.assertTrue(source.is_file(), source)

    def test_dex_inventory_reads_a_synthetic_dex(self):
        """Exercise the DEX tables the inventory walks, without a checked-in blob."""
        inventory = module('dex_class_inventory')
        self.assertEqual(inventory.descriptor_to_binary('Ladapter/activity/Foo$Bar;'),
                         'adapter.activity.Foo$Bar')
        self.assertEqual(inventory.read_uleb128(bytes([0x00]), 0), (0, 1))
        self.assertEqual(inventory.read_uleb128(bytes([0x81, 0x02]), 0), (257, 2))
        with self.assertRaisesRegex(ValueError, 'bad magic'):
            inventory.dex_defined_types(b'not-a-dex' + bytes(200))

    def test_dex_inventory_attributes_sources(self):
        inventory = module('dex_class_inventory')
        with tempfile.TemporaryDirectory(prefix='a2hlab-test-') as tmp:
            root = Path(tmp)
            (root / 'p').mkdir()
            (root / 'p' / 'Thing.java').write_text(
                'package a.b;\npublic final class Thing {\n  class Nested {}\n}\n')
            owned, duplicates = inventory.source_classes([root])
            self.assertEqual(duplicates, [])
            self.assertIn('a.b.Thing', owned)


if __name__ == '__main__':
    unittest.main()
