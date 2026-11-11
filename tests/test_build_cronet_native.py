import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    sys.path.insert(0, str(ROOT / 'tools'))
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def elf(dynstr):
    """A minimal ELF64 image with section headers: null, .shstrtab, .dynstr."""
    shstrtab = b'\0.shstrtab\0.dynstr\0'
    header = bytearray(64)
    header[:6] = b'\x7fELF\x02\x01'
    body = bytearray(header) + shstrtab + dynstr
    shoff = len(body)
    sections = [(0, 0, 0, 0, 0, 0), (1, 3, 0, 0, 64, len(shstrtab)), (11, 3, 0, 0, 64 + len(shstrtab), len(dynstr))]
    for name, kind, flags, addr, offset, size in sections:
        body += struct.pack('<IIQQQQIIQQ', name, kind, flags, addr, offset, size, 0, 0, 1, 0)
    struct.pack_into('<Q', body, 0x28, shoff)
    struct.pack_into('<HHH', body, 0x3a, 64, len(sections), 1)
    return body, 64 + len(shstrtab)


class CronetNativeTests(unittest.TestCase):
    def test_imports_renamed_in_place(self):
        cronet = module('build_cronet_native')
        image, start = elf(b'\0libc.so\0my_connect\0connect\0getaddrinfo\0sigaction\0')
        size = len(image)
        rewritten = cronet.rewrite_imports(image, {'connect', 'getaddrinfo', 'my_connect'})
        self.assertEqual(len(image), size)
        self.assertEqual(set(rewritten), {'connect', 'getaddrinfo'})
        names = image[start:start + 52].split(b'\0')
        # A longer name ending in a rewritten one, and a name the library does not import, stay.
        self.assertEqual(names[1:6], [b'libc.so', b'my_connect', b'wl_conn', b'wl_getai_oh', b'sigaction'])
        self.assertEqual(image[rewritten['getaddrinfo'][1]:][:11], b'wl_getai_oh')

    def test_duplicate_entry_refused(self):
        cronet = module('build_cronet_native')
        image, _ = elf(b'\0getaddrinfo\0getaddrinfo\0')
        with self.assertRaisesRegex(ValueError, 'getaddrinfo is 2 .dynstr entries'):
            cronet.rewrite_imports(image, {'getaddrinfo'})

    def test_rewrites_keep_length(self):
        cronet = module('build_cronet_native')
        for old, new in cronet.OH_IMPORT_REWRITES.items():
            self.assertEqual(len(old), len(new), old)

    def test_variants_merge_after_defaults(self):
        cronet = module('build_cronet_native')
        with tempfile.TemporaryDirectory(prefix='a2hlab-test-') as tmp:
            root = Path(tmp)
            (root / 'Android.bp').write_text('''
cc_defaults {
    name: "d",
    cflags: ["-DDEFAULT"],
}
FLAGS = ["-DVAR"]
FLAGS += ["-DMORE"]
cc_library_static {
    name: "m",
    defaults: ["d"],
    cflags: FLAGS,
    target: {
        android_arm64: { cflags: ["-DARM64"] },
        host: { cflags: ["-DHOST"] },
    },
    arch: { x86_64: { srcs: ["x.cc"] }, arm64: { srcs: ["a.cc"] } },
}
''')
            bp = cronet.Blueprint(root)
            m = cronet.Module(bp, ('.', 'm'))
            self.assertEqual(m.get('cflags'), ['-DDEFAULT', '-DVAR', '-DMORE', '-DARM64'])
            self.assertEqual(m.get('srcs'), ['a.cc'])


if __name__ == '__main__':
    unittest.main()
