import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

path = Path(__file__).resolve().parents[2] / 'tools/native/worker-personal.py'
spec = importlib.util.spec_from_file_location('worker_personal', path)
personal = importlib.util.module_from_spec(spec); spec.loader.exec_module(personal)


class ReferenceReportTests(unittest.TestCase):
    """Which backup report a starting worker is checked against. No game and no personal file is touched."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.shared = Path(self.directory.name) / 'personal-backup.json'
        self.backups = 0

    def backup(self):
        self.backups += 1
        return dict(backup_directory='fresh')

    def test_first_worker_takes_a_backup_and_shares_it(self):
        report = personal.reference_report(self.shared, games=lambda: [], ensure_backup=self.backup)
        self.assertEqual((report, json.loads(self.shared.read_text()), self.backups), (dict(backup_directory='fresh'),) * 2 + (1,))

    def test_later_worker_uses_the_shared_report_while_a_worker_game_runs(self):
        self.shared.write_text(json.dumps(dict(backup_directory='shared')))
        report = personal.reference_report(self.shared, games=lambda: ['player-1'], ensure_backup=self.backup)
        self.assertEqual((report, self.backups), (dict(backup_directory='shared'), 0))

    def test_worker_game_that_starts_during_the_backup_falls_back_to_the_shared_report(self):
        # Trial251: the earlier workers' games had not started when the third worker looked, and one started
        # while its backup was being verified.
        self.shared.write_text(json.dumps(dict(backup_directory='shared')))
        seen = iter([[], ['player-2']])

        def refused():
            raise ValueError('SPORE started or saves changed during preparation; retry after closing the game')
        report = personal.reference_report(self.shared, games=lambda: next(seen), ensure_backup=refused)
        self.assertEqual(report, dict(backup_directory='shared'))

    def test_refusal_with_no_worker_game_running_stays_a_refusal(self):
        self.shared.write_text(json.dumps(dict(backup_directory='shared')))

        def refused():
            raise ValueError('SPORE started or saves changed during preparation; retry after closing the game')
        with self.assertRaises(ValueError):
            personal.reference_report(self.shared, games=lambda: [], ensure_backup=refused)

    def test_refusal_without_any_shared_report_stays_a_refusal(self):
        seen = iter([[], ['player-2']])

        def refused():
            raise ValueError('SPORE started or saves changed during preparation; retry after closing the game')
        with self.assertRaises(ValueError):
            personal.reference_report(self.shared, games=lambda: next(seen), ensure_backup=refused)


if __name__ == '__main__':
    unittest.main()
