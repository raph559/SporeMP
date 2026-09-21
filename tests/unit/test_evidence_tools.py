"""HOST/FIXTURE checks for tools separated from private historical evidence."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
TOOLS = REPO / 'tools/native'


class EvidenceToolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sporemp-evidence-tools-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def run_tool(self, name, *args):
        # An unrelated working directory catches accidental dependence on cwd
        # or on data next to the tool in the removed evidence tree.
        return subprocess.run([sys.executable, str(TOOLS / name), *map(str, args)],
                              cwd=self.root, capture_output=True, text=True, timeout=30)

    def test_m06_self_test_from_unrelated_directory(self):
        result = self.run_tool('analyze-m06.py', '--self-test')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report['evidence_class'], 'HOST_SELF_TEST')
        self.assertEqual(report['native_game'], 'NOT_RUN')

    def test_prefix_generator_rejects_unknown_executable_before_output(self):
        executable = self.root / 'unknown.exe'
        executable.write_bytes(b'not the pinned executable')
        output, report = self.root / 'prefix.inc', self.root / 'audit.json'
        result = self.run_tool('generate-award-prefixes.py', '--executable', executable,
                               '--output', output, '--report', report)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unknown executable SHA-256', result.stderr)
        self.assertFalse(output.exists())
        self.assertFalse(report.exists())

    def test_m05_tools_read_supplied_archive_and_preserve_existing_output(self):
        archive = self.root / 'archive'
        run = archive / 'native-01'
        run.mkdir(parents=True)
        (run / 'archive.json').write_text(json.dumps({
            'game_exit': {'exit_code': 0}, 'sealed_games_unchanged': True}), encoding='utf-8')
        rows = [
            {'event': 'trace_start', 'sequence': 1, 'qpc': 0, 'qpc_frequency': 100,
             'thread_id': 1, 'bridge_version': 'FIXTURE'},
            {'event': 'trace_stop', 'sequence': 2, 'qpc': 1, 'thread_id': 1},
        ]
        (run / 'actors-1.jsonl').write_text(''.join(json.dumps(row) + '\n' for row in rows), encoding='utf-8')
        (archive / 'personal-after-01.json').write_text('{}', encoding='utf-8')
        for name, acceptance in (('analyze-m05-presentation.py', 'NOT_VERIFIED'),
                                 ('analyze-m05-ability.py', 'NOT_INFERRED_BY_ANALYZER')):
            with self.subTest(tool=name):
                output = self.root / (name + '.json')
                result = self.run_tool(name, '--input-dir', archive, '--output', output)
                self.assertEqual(result.returncode, 0, result.stderr)
                report = json.loads(output.read_text())
                self.assertEqual(report['full_m05_acceptance'], acceptance)
                self.assertEqual(report['runs']['native-01']['build'], 'FIXTURE')
                self.assertTrue(report['runs']['native-01']['sequences_contiguous'])
                original = output.read_bytes()
                result = self.run_tool(name, '--input-dir', archive, '--output', output)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(output.read_bytes(), original)

    def test_m05_tools_reject_empty_archive(self):
        for name in ('analyze-m05-presentation.py', 'analyze-m05-ability.py'):
            with self.subTest(tool=name):
                output = self.root / (name + '.json')
                result = self.run_tool(name, '--input-dir', self.root, '--output', output)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('No native-*/archive.json runs found', result.stderr)
                self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
