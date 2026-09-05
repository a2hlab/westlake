import ctypes as c
from pathlib import Path
import tempfile
import unittest
ROOT = Path(__file__).resolve().parent
class DecoderTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='pinyin-dict-')
        self.lib = c.CDLL(str(ROOT / 'out/libpinyin_host.so'))
        self.lib.im_open_decoder.argtypes = [c.c_char_p, c.c_char_p]
        self.lib.im_open_decoder.restype = c.c_bool
        self.lib.im_search.argtypes = [c.c_char_p, c.c_size_t]
        self.lib.im_search.restype = c.c_size_t
        self.lib.im_get_candidate.argtypes = [c.c_size_t, c.POINTER(c.c_uint16), c.c_size_t]
        self.lib.im_get_candidate.restype = c.POINTER(c.c_uint16)
        self.lib.im_choose.argtypes = [c.c_size_t]
        self.lib.im_choose.restype = c.c_size_t
        self.lib.im_get_fixed_len.restype = c.c_size_t
        self.assertTrue(self.lib.im_open_decoder(str(ROOT / 'upstream/entry/src/main/resources/rawfile/dict_pinyin.dat').encode(),
            str(Path(self.tmp.name) / 'user.dict').encode()))
    def tearDown(self):
        self.lib.im_close_decoder(); self.tmp.cleanup()
    def candidates(self, raw):
        self.lib.im_reset_search()
        count = self.lib.im_search(raw.encode(), len(raw))
        return [self.choice(i) for i in range(min(count, 60))]
    def choice(self, i):
        buf = (c.c_uint16 * 128)()
        self.assertTrue(self.lib.im_get_candidate(i, buf, 128))
        return bytes(buf).decode('utf-16-le').split('\0')[0]
    def test_common_phrases(self):
        for raw, expected in [('nihao','你好'), ('zhongguo','中国'), ('xinwen','新闻'),
                              ('toutiao','头条'), ('beijing','北京'), ('shurufa','输入法')]:
            with self.subTest(raw=raw):
                choices = self.candidates(raw)
                self.assertIn(expected, choices)
                self.lib.im_choose(choices.index(expected))
                self.assertEqual(self.choice(0), expected)
                self.assertEqual(self.lib.im_get_fixed_len(), len(expected))
    def test_ambiguous_candidates(self):
        values = self.candidates('shi')
        self.assertIn('是', values); self.assertIn('时', values)
        self.assertGreater(len(values), 5)
    def test_backspace_search_reset(self):
        self.candidates('nihao'); self.candidates('niha')
        self.assertIn('你好', self.candidates('nihao'))
if __name__ == '__main__': unittest.main(verbosity=2)
