"""HOST/FIXTURE checks for launcher worker metadata and process control boundaries."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools/launcher'))
import worker_manager as workers

class WorkerManagerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sporemp-worker-host-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.addCleanup(patch.stopall)
        patch.object(workers, 'REPO', self.root).start()

    def write(self, path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps({'schema_version': 1, **value}), encoding='utf-8')

    def test_unknown_worker_and_path_escape_refused(self):
        for value in ('', '04', '../01', '01 --play', None):
            with self.assertRaises(ValueError): workers.root_for(value)
        marker = self.root / 'local/worker-accounts/01/current.json'
        self.write(marker, {'worker_id': '01', 'generation': 'a' * 32, 'supervisor_pid': 123, 'run': 'C:/Users/Developer'})
        with self.assertRaisesRegex(ValueError, 'escaped'): workers.current('01')

    def test_stale_or_malformed_generation_refused(self):
        marker = self.root / 'local/worker-accounts/01/current.json'
        for generation in ('0' * 32, 'A' * 32, 'a' * 33, '../run'):
            self.write(marker, {'worker_id': '01', 'generation': generation, 'supervisor_pid': 123})
            with self.assertRaises(ValueError): workers.current('01')

    def test_unprepared_account_never_enables_start(self):
        self.write(self.root / 'local/worker-accounts/01/account.json', {'prepared': False})
        row = workers.worker_list()['workers'][0]
        self.assertFalse(row['can_start'])
        self.assertFalse(row['can_stop'])

    def test_third_prepared_profile_is_listed_for_two_client_acceptance(self):
        self.write(self.root / 'local/worker-accounts/03/account.json', {'prepared': True})
        rows = workers.worker_list()['workers']
        self.assertEqual([row['id'] for row in rows], ['03'])
        self.assertTrue(rows[0]['can_start'])

    def test_control_binds_generation_process_and_epoch(self):
        value = {'generation': '1' * 32, 'supervisor_pid': 731}
        completed = subprocess.CompletedProcess([], 0, json.dumps({'schema_version': 1, 'op': 'shutdown', 'result': 'accepted'}), '')
        with patch.object(workers, 'current', return_value=value), patch.object(workers.subprocess, 'run', return_value=completed) as run:
            workers.control('01', 'shutdown', 7)
        self.assertEqual(run.call_args.args[0][1:], ['1' * 32, '731', 'shutdown', '7'])
        self.assertEqual(run.call_args.kwargs['timeout'], 13)
        self.assertFalse(run.call_args.kwargs.get('shell', False))

    def test_mutation_timeout_is_not_retried(self):
        with patch.object(workers, 'current', return_value={'generation': '1' * 32, 'supervisor_pid': 731}), \
             patch.object(workers.subprocess, 'run', return_value=subprocess.CompletedProcess([], 6, '', 'outcome unknown')) as run:
            with self.assertRaisesRegex(ValueError, 'outcome unknown'): workers.control('01', 'shutdown', 7)
        self.assertEqual(run.call_count, 1)

    def test_wrong_control_operation_is_rejected(self):
        response = subprocess.CompletedProcess([], 0, '{"schema_version":1,"op":"setup"}', '')
        with patch.object(workers, 'current', return_value={'generation': '1' * 32, 'supervisor_pid': 731}), patch.object(workers.subprocess, 'run', return_value=response):
            with self.assertRaisesRegex(ValueError, 'Unexpected'): workers.control('01', 'shutdown', 7)

    def test_refused_shutdown_is_not_reported_as_success(self):
        with patch.object(workers, 'current', return_value={}), \
             patch.object(workers, 'control', side_effect=[{'epoch': 7}, {'result': 'unavailable'}]) as control:
            with self.assertRaisesRegex(ValueError, 'refused shutdown'): workers.stop('01')
        self.assertEqual(control.call_count, 2)

    def test_stop_does_not_retarget_replacement_generation(self):
        marker = self.root / 'local/worker-accounts/01'
        self.write(marker / 'account.json', {'prepared': True})
        original = {'worker_id': '01', 'generation': '1' * 32, 'supervisor_pid': 731,
                    'run': 'C:/ProgramData/SporeMP/M04/01/runs/' + '1' * 32}
        replacement = {**original, 'generation': '2' * 32, 'supervisor_pid': 732,
                       'run': 'C:/ProgramData/SporeMP/M04/01/runs/' + '2' * 32}
        self.write(marker / 'current.json', original)
        def reply(command, **kwargs):
            operation = command[3]
            if operation == 'status': self.write(marker / 'current.json', replacement)
            return subprocess.CompletedProcess(command, 0, json.dumps(
                {'schema_version': 1, 'op': operation, 'result': 'accepted', 'epoch': 7}), '')
        with patch.object(workers.subprocess, 'run', side_effect=reply) as run, \
             patch.object(workers, 'supervisor_running', return_value=False):
            result = workers.stop('01')
        self.assertEqual([call.args[0][1:] for call in run.call_args_list],
                         [['1' * 32, '731', 'status'], ['1' * 32, '731', 'shutdown', '7']])
        self.assertEqual(result['generation'], original['generation'])
        self.assertEqual(result['supervisor_pid'], original['supervisor_pid'])
        self.assertEqual(workers.current('01')['generation'], replacement['generation'])

    def test_captured_control_identity_is_validated_without_rereading_current(self):
        captured = {'worker_id': '02', 'generation': '1' * 32, 'supervisor_pid': 731,
                    'run': 'C:/ProgramData/SporeMP/M04/02/runs/' + '1' * 32}
        with patch.object(workers, 'current', side_effect=AssertionError('must not retarget')), \
             patch.object(workers.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'identity mismatch'):
                workers.control('01', 'shutdown', 7, captured=captured)
        run.assert_not_called()

    def test_peer_metadata_error_preserves_shutdown_acknowledgement(self):
        marker = self.root / 'local/worker-accounts/01'
        self.write(marker / 'account.json', {'prepared': True})
        self.write(marker / 'current.json', {'worker_id': '01', 'generation': '1' * 32, 'supervisor_pid': 731,
                                          'run': 'C:/ProgramData/SporeMP/M04/01/runs/' + '1' * 32})
        peer = self.root / 'local/worker-accounts/02/account.json'
        peer.parent.mkdir(parents=True)
        peer.write_text('{broken metadata', encoding='utf-8')
        def reply(command, **kwargs):
            return subprocess.CompletedProcess(command, 0, json.dumps(
                {'schema_version': 1, 'op': command[3], 'result': 'accepted', 'epoch': 7}), '')
        with patch.object(workers.subprocess, 'run', side_effect=reply) as run, \
             patch.object(workers, 'supervisor_running', return_value=False):
            result = workers.stop('01')
        self.assertEqual(result['shutdown_result'], 'accepted')
        self.assertEqual(run.call_count, 2)
        self.assertEqual([row['id'] for row in result['workers']], ['01', '02'])
        self.assertEqual(result['workers'][1]['state'], 'unavailable')
        self.assertFalse(result['workers'][1]['can_start'])
        self.assertFalse(result['workers'][1]['can_stop'])

    def test_player_coexistence_requires_live_registered_identity(self):
        marker = self.root / 'local/worker-accounts/01'
        self.write(marker / 'account.json', {'prepared': True, 'user': 'SporeMP-M04-01', 'sid': 'worker-sid', 'game_root': 'C:/Games/SPORE'})
        self.write(marker / 'current.json', {})
        run = self.root / 'run'
        self.write(run / 'worker-status.json', {'generation': '1' * 32, 'game_pid': 734})
        value = {'generation': '1' * 32, 'supervisor_pid': 731, 'run': str(run)}
        game = {'pid': 734, 'parent': 731, 'sid': 'worker-sid', 'path': str(Path('C:/Games/SPORE/SporebinEP1/SporeApp.exe'))}
        with patch.object(workers, 'current', return_value=value), patch.object(workers, 'os_games', return_value=[game]), patch.object(workers, 'supervisor_running', return_value=True):
            self.assertTrue(workers.only_registered_workers_running())
            for field, invalid in (('pid', 735), ('parent', 732), ('sid', 'personal-sid'), ('path', 'C:/Other/SporeApp.exe')):
                with patch.object(workers, 'os_games', return_value=[{**game, field: invalid}]):
                    self.assertFalse(workers.only_registered_workers_running())
            with patch.object(workers, 'supervisor_running', return_value=False):
                self.assertFalse(workers.only_registered_workers_running())

    def test_unqueryable_games_never_enable_player_coexistence(self):
        with patch.object(workers, 'os_games', side_effect=OSError('unqueryable process')):
            self.assertFalse(workers.only_registered_workers_running())

if __name__ == '__main__': unittest.main()
