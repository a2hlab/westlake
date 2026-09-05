"""Verify captured device acceptance evidence, not a substitute for a live replay."""
import hashlib
import json
from pathlib import Path
import re
import unittest
import zipfile
ROOT = Path(__file__).resolve().parent
class Acceptance(unittest.TestCase):
    def test_android_editor_values(self):
        expected = {'toutiao-nihao-preview':'nihao', 'toutiao-nihao-commit':'你好',
            'toutiao-hello-china':'你好中国','toutiao-delete-chinese':'你好中',
            'toutiao-composition-backspace':'你好中niha','toutiao-composition-cancel':'你好中',
            'toutiao-alternate-candidate':'时','toutiao-news-query':'新闻','toutiao-news-submit':'新闻'}
        for name,value in expected.items():
            with self.subTest(name=name):
                text=(ROOT/(name+'.tree')).read_text()
                values=re.findall(r'SearchAutoCompleteTextView id=cx[^\n]* "([^"\n]*)"',text)
                self.assertEqual(values,[value])
    def test_native_control(self):
        self.assertIn('native text=你好中国',(ROOT/'native-hello-china.log').read_text())
        self.assertIn('native text=你好中\n',(ROOT/'native-delete-chinese.log').read_text())
        self.assertIn('native text=你好\n',(ROOT/'native-final-build.log').read_text())
    def test_real_preview_and_editor_action(self):
        text=(ROOT/'bridge-ime-callbacks.log').read_text()
        self.assertIn('runOp(4) done',text)
        self.assertIn('runOp(5) done',text)
        text=(ROOT/'toutiao-submit-callbacks.log').read_text()
        self.assertIn('inputStart preview=true action=3',text)
        self.assertIn('editor action sent',text)
        self.assertIn('hide -> nativeHideKeyboard=true',text)
    def test_pinned_inputs_and_selected_ime(self):
        manifest=json.loads((ROOT/'out/manifest.json').read_text())
        for name,digest in manifest['files'].items():
            self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),digest,name)
        after=(ROOT/'after-install.log').read_text()
        self.assertIn(manifest['files']['out/pinyin.hap'],after)
        self.assertRegex(after,r'org.westlake.pinyin/ServiceExtAbility[^\n]*"isCurrentIme": "true"')
        for digest in ['e84e57746adaa181fe04117d75f3e33c','cc99caaaa3baffbb100118753fa1d130',
                       'c8b5e7352662109ddcb9ca3b97020894','b58ed9c01a93df2601e9c0671afdf2df',
                       '20e48b90b6b01468f155ed7c48097e70']:
            self.assertIn(digest,after)
    def test_offline_package(self):
        with zipfile.ZipFile(ROOT/'out/pinyin.hap') as hap:
            module=json.loads(hap.read('module.json'))
            self.assertEqual(module['app']['bundleName'],'org.westlake.pinyin')
            self.assertEqual(module['module']['requestPermissions'],[])
            self.assertIn('inputMethod',[e['type'] for e in module['module']['extensionAbilities']])
            self.assertTrue(hap.read('resources.index').startswith(b'Restool 4.105'))
            self.assertEqual(hap.read('resources/rawfile/dict_pinyin.dat'),
                (ROOT/'upstream/entry/src/main/resources/rawfile/dict_pinyin.dat').read_bytes())
            self.assertIn('resources/rawfile/PINYIN_LICENSE.txt',hap.namelist())
if __name__ == '__main__': unittest.main(verbosity=2)
