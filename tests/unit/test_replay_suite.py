"""HOST replay of the retained native trials through their scenarios; skipped where the evidence is absent.

Raw trial evidence stays under ignored local/, so this runs only in the checkout that holds it.
It checks scenario expectations against recorded original-game behaviour; it launches nothing
and does not exercise mod code changed since the recordings were made.
"""
import importlib.util
import json
from pathlib import Path
import unittest

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('native_trial', REPO / 'tools/native/native-trial.py')
trial = importlib.util.module_from_spec(spec); spec.loader.exec_module(trial)
MANIFEST = REPO / 'tests/engine/scenarios/replay-suite.json'


class RetainedTrialReplay(unittest.TestCase):
    def test_every_retained_trial_gives_its_recorded_outcome(self):
        entries = json.loads(MANIFEST.read_text(encoding='utf-8'))
        if not all((REPO / entry['trial']).is_dir() for entry in entries):
            self.skipTest('retained native trials are not present in this checkout')
        results = trial.replay_suite(MANIFEST)
        self.assertEqual([(r['trial'], r['scenario'], r['outcome'], r['error']) for r in results if not r['as_expected']], [])

    def test_every_listed_scenario_loads(self):
        for entry in json.loads(MANIFEST.read_text(encoding='utf-8')):
            scenario = trial.load_scenario(REPO / entry['scenario'], require_artifact=False)
            self.assertIn(entry['expect'], ('PASS', 'FAIL'), scenario.name)


if __name__ == '__main__':
    unittest.main()
