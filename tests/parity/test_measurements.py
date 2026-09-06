import importlib.util
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('summary',ROOT/'tools/summarize_parity.py')
summary=importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)
EVIDENCE=Path(__file__).resolve().parent/'evidence'

class MeasurementTests(unittest.TestCase):
    def test_recomputes_all_published_samples(self):
        expected=json.loads((EVIDENCE/'resume27-benchmark-summary.json').read_text())
        for name,data in expected.items():
            with self.subTest(name=name):
                self.assertEqual(summary.summarize((EVIDENCE/(name+'.log')).read_text()),data)

    def test_rejects_incomplete_duplicate_and_unfocused_trials(self):
        valid=(EVIDENCE/'resume27filea.log').read_text()
        invalid=[valid.replace('[GAP_BENCH] PAGE sample=3 ','[REMOVED] '),
                 valid+'\n[GAP_BENCH] INPUT sample=12 event_to_click_ms=0\n',
                 valid.replace('focus=true','focus=false',1),valid+'\nFatal signal 6\n']
        for log in invalid:
            with self.assertRaises(ValueError):summary.summarize(log)

if __name__=='__main__':unittest.main()
