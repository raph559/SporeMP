"""HOST/FIXTURE validation of M04 checkpoint provenance, never native gameplay."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('worker_checkpoint', REPO / 'tools/native/worker-checkpoint.py')
checkpoint = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checkpoint)


class WorkerCheckpointTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sporemp-checkpoint-host-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.addCleanup(patch.stopall)
        patch.object(checkpoint, 'REPO', self.root).start()
        self.source_generation = 'a' * 32
        self.run = self.root / 'source-run'
        self.run.mkdir()
        self.expected_run = Path('C:/ProgramData/SporeMP/M04/01/runs') / self.source_generation
        no_reparse = checkpoint.service.diag.no_reparse
        def fixture_paths(path):
            path = Path(path)
            if path == self.expected_run or self.expected_run in path.parents:
                path = self.run / path.relative_to(self.expected_run)
            return no_reparse(path)
        patch.object(checkpoint.service.diag, 'no_reparse', side_effect=fixture_paths).start()
        self.pid = 41
        self.epoch = 3
        self.request = 20
        self.actors = [dict(owner=1, native_id=232, herd_native_id=208, species_instance=101,
                            species_type=102, species_group=103, archetype=0xaecb24d4),
                       dict(owner=2, native_id=7425, herd_native_id=7424, species_instance=101,
                            species_type=102, species_group=103, archetype=0xaecb24d4)]
        self.rows = self.trace_rows()
        self.trace = self.run / f'actors-{self.pid}.jsonl'
        self.write_trace(self.trace, self.rows)
        self.games = self.root / 'games'
        (self.games / 'Game0').mkdir(parents=True)
        (self.games / 'Game0/Satiria.spo').write_bytes(b'HOST FIXTURE, NOT A GAME SAVE')
        self.backup = self.root / 'local/worker-checkpoints/01' / self.source_generation
        with patch.object(checkpoint.service.diag, 'game_running', return_value=False):
            backed = checkpoint.service.diag.backup([self.games], self.backup)
        self.manifest = backed['sources'][0]['snapshot']
        self.account = {'sid': 'fixture-worker-sid'}
        self.sidecar = dict(schema_version=1, kind='m04-native-actor-checkpoint', worker_id='01',
            sid=self.account['sid'], generation=self.source_generation, run=str(self.expected_run), game_pid=self.pid,
            save_request=self.request, save_epoch=self.epoch, actors=copy.deepcopy(self.actors),
            native_executable_sha256=checkpoint.EXE_HASH, sdk_commit=checkpoint.SDK, bridge_version='0.0.14',
            trace_sha256=checkpoint.service.diag.fingerprint(self.trace)['sha256'], backup=str(self.backup),
            backup_manifest_sha256=checkpoint.service.diag.fingerprint(self.backup / 'backup-manifest.json')['sha256'],
            games=self.manifest, games_sha256=checkpoint.digest(self.manifest), native_save_returned_success=True)

    def trace_rows(self):
        rows = [dict(event='trace_start', executable_sha256=checkpoint.EXE_HASH, sdk_commit=checkpoint.SDK, bridge_version='0.0.14')]
        rows += [dict(event='checkpoint_actor', request=self.request, **actor) for actor in self.actors]
        rows += [dict(event='checkpoint_snapshot', request=self.request, valid=True),
                 dict(event='native_save_return', request=self.request, native_success=True,
                      requested_epoch=self.epoch, observed_epoch=self.epoch),
                 dict(event='trace_stop', healthy=True, detach_status=0)]
        return [dict(schema_version=1, pid=self.pid, thread_id=91, foreign_callbacks=0,
                     sequence=i, epoch=self.epoch, **row) for i, row in enumerate(rows, 1)]

    def write_trace(self, path, rows):
        path.write_text(''.join(json.dumps(row) + '\n' for row in rows), encoding='utf-8')

    def validate(self, value=None):
        return checkpoint.validate_sidecar(self.sidecar if value is None else value, '01', self.games, self.account)

    def test_valid_closed_fixture_is_tied_to_original_save_pair(self):
        self.assertEqual(self.validate(), self.actors)
        saved, actors = checkpoint.saved_fingerprints(checkpoint.checked_trace(self.trace, self.pid, closed=True))
        self.assertEqual(saved['request'], self.request)
        self.assertEqual(actors, self.actors)

    def test_duplicate_missing_or_ambiguous_snapshot_is_rejected(self):
        for rows in (self.rows[:2] + self.rows[3:],
                     self.rows[:3] + [copy.deepcopy(self.rows[2])] + self.rows[3:],
                     self.rows[:4] + [copy.deepcopy(self.rows[3])] + self.rows[4:]):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                checkpoint.fingerprints(rows, self.request)
        duplicate_owner = copy.deepcopy(self.rows)
        duplicate_owner[2]['owner'] = 1
        with self.assertRaisesRegex(ValueError, 'Duplicate or missing'):
            checkpoint.fingerprints(duplicate_owner, self.request)

    def test_fingerprint_native_ids_and_numeric_widths_are_strict(self):
        for field, invalid in (('native_id', 0), ('native_id', 0xffffffff), ('native_id', -1),
                               ('herd_native_id', 0), ('species_instance', 1 << 32),
                               ('species_type', True), ('species_group', '103'),
                               ('archetype', -(1 << 31) - 1), ('archetype', 1 << 32)):
            rows = copy.deepcopy(self.rows)
            rows[1][field] = invalid
            with self.subTest(field=field, invalid=invalid), self.assertRaises(ValueError):
                checkpoint.fingerprints(rows, self.request)
        rows = copy.deepcopy(self.rows)
        rows[2]['native_id'] = rows[1]['native_id']
        with self.assertRaisesRegex(ValueError, 'same native noun'):
            checkpoint.fingerprints(rows, self.request)

    def test_signed_archetype_normalizes_to_same_u32_wire_value(self):
        rows = copy.deepcopy(self.rows)
        rows[1]['archetype'] -= 1 << 32
        self.assertEqual(checkpoint.fingerprints(rows, self.request), self.actors)
        packed = checkpoint.pack_actors(self.actors)
        unpacked = [part for word in packed for part in (word & 0xffffffff, word >> 32)]
        self.assertEqual(unpacked, [actor[field] for actor in self.actors for field in checkpoint.FIELDS])
        self.assertTrue(all(0 <= word < 1 << 64 for word in packed))

    def test_epoch_request_and_owner_cannot_use_bool_or_overflow(self):
        for index, field, invalid in ((1, 'epoch', 4), (3, 'epoch', 1 << 64), (1, 'epoch', True),
                                       (1, 'owner', True), (3, 'request', True)):
            rows = copy.deepcopy(self.rows)
            rows[index][field] = invalid
            with self.subTest(field=field, invalid=invalid), self.assertRaises(ValueError):
                checkpoint.fingerprints(rows, self.request)
        for request in (True, 0, -1, 1 << 64):
            with self.subTest(request=request), self.assertRaises(ValueError):
                checkpoint.fingerprints(self.rows, request)

    def test_snapshot_must_precede_save_and_follow_both_actor_rows(self):
        rows = copy.deepcopy(self.rows)
        rows[2], rows[3] = rows[3], rows[2]
        with self.assertRaisesRegex(ValueError, 'after its snapshot'):
            checkpoint.fingerprints(rows, self.request)
        rows = copy.deepcopy(self.rows)
        rows[3], rows[4] = rows[4], rows[3]
        with self.assertRaisesRegex(ValueError, 'after its original save'):
            checkpoint.saved_fingerprints(rows)
        rows = copy.deepcopy(self.rows)
        rows[4]['observed_epoch'] += 1
        with self.assertRaisesRegex(ValueError, 'Scene changed'):
            checkpoint.saved_fingerprints(rows)

    def test_failed_last_save_does_not_reuse_earlier_success(self):
        rows = copy.deepcopy(self.rows)
        rows.insert(-1, dict(rows[4], request=self.request + 1, native_success=False))
        with self.assertRaisesRegex(ValueError, 'successful final'):
            checkpoint.saved_fingerprints(rows)

    def test_closed_native_trace_requires_same_pid_thread_sequence_and_clean_exit(self):
        for index, field, invalid in ((1, 'pid', 42), (2, 'thread_id', 92), (2, 'sequence', 9),
                                      (1, 'foreign_callbacks', True), (5, 'healthy', False), (5, 'detach_status', 5)):
            rows = copy.deepcopy(self.rows)
            rows[index][field] = invalid
            self.write_trace(self.trace, rows)
            with self.subTest(field=field), self.assertRaises(ValueError):
                checkpoint.checked_trace(self.trace, self.pid, closed=True)
        self.write_trace(self.trace, self.rows[:-1])
        with self.assertRaisesRegex(ValueError, 'retire cleanly'):
            checkpoint.checked_trace(self.trace, self.pid, closed=True)

    def test_sidecar_actor_tampering_cannot_pass_width_checks_alone(self):
        for field, value in (('native_id', 999), ('species_instance', 104), ('herd_native_id', 7777)):
            sidecar = copy.deepcopy(self.sidecar)
            sidecar['actors'][1][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'original save evidence'):
                self.validate(sidecar)
        sidecar = copy.deepcopy(self.sidecar)
        sidecar['actors'].append(None)
        with self.assertRaisesRegex(ValueError, 'malformed actors'):
            self.validate(sidecar)

    def test_sidecar_source_epoch_request_and_path_are_pinned(self):
        for field, invalid in (('save_epoch', 4), ('save_request', 21), ('generation', '../run'),
                               ('run', str(self.root)), ('game_pid', True)):
            sidecar = copy.deepcopy(self.sidecar)
            sidecar[field] = invalid
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.validate(sidecar)

    def test_native_tree_and_sidecar_manifest_mismatch_are_rejected(self):
        sidecar = copy.deepcopy(self.sidecar)
        sidecar['games']['files'][0]['sha256'] = '0' * 64
        sidecar['games_sha256'] = checkpoint.digest(sidecar['games'])
        with self.assertRaisesRegex(ValueError, 'sealed sidecar'):
            self.validate(sidecar)
        (self.games / 'Game0/Satiria.spo').write_bytes(b'changed native fixture')
        with self.assertRaisesRegex(ValueError, 'sealed sidecar'):
            self.validate()

    def test_changed_source_trace_is_rejected_even_if_pair_is_still_valid(self):
        rows = copy.deepcopy(self.rows)
        rows[-1]['unused_note'] = 'changed after sealing'
        self.write_trace(self.trace, rows)
        with self.assertRaisesRegex(ValueError, 'source trace changed'):
            self.validate()

    def test_changed_backup_payload_and_manifest_are_rejected(self):
        payload = self.backup / 'source-0/Game0/Satiria.spo'
        original = payload.read_bytes()
        payload.write_bytes(b'changed backup')
        with self.assertRaisesRegex(ValueError, 'Backup mismatch'):
            self.validate()
        payload.write_bytes(original)
        manifest_path = self.backup / 'backup-manifest.json'
        data = json.loads(manifest_path.read_text())
        data['committed_utc'] = 'changed after sealing'
        manifest_path.write_text(json.dumps(data), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Backup manifest changed'):
            self.validate()

    def test_restore_status_needs_matching_zero_creation_native_event(self):
        status = dict(epoch=6, actor_a=1, actor_b=2)
        row = dict(event='checkpoint_restore', request=50, valid=True, reason='adopted_existing_nouns',
                   created_nouns=0, **status)
        self.assertEqual(checkpoint.restored_evidence([row], 50, status), row)
        for field, invalid in (('created_nouns', 1), ('created_nouns', False), ('valid', False),
                               ('actor_b', 3), ('epoch', 7), ('reason', 'created_replacement')):
            with self.subTest(field=field), self.assertRaises(ValueError):
                checkpoint.restored_evidence([{**row, field: invalid}], 50, status)
        with self.assertRaises(ValueError): checkpoint.restored_evidence([], 50, status)
        with self.assertRaises(ValueError): checkpoint.restored_evidence([row, row], 50, status)

    def test_load_restore_accepts_zero_request_status_and_does_not_retry_mutations(self):
        sidecar_path = self.root / 'sidecar.json'
        sidecar_path.write_text(json.dumps(self.sidecar), encoding='utf-8')
        destination = self.root / 'destination-run'
        destination.mkdir()
        adopted = dict(event='checkpoint_restore', request=22, valid=True, created_nouns=0,
                       reason='adopted_existing_nouns', actor_a=1, actor_b=2)
        rows = [dict(self.rows[0], epoch=6),
                dict(schema_version=1, pid=self.pid, thread_id=91, foreign_callbacks=0, sequence=2, epoch=6, **adopted)]
        self.write_trace(destination / f'actors-{self.pid}.jsonl', rows)
        nonce = 'b' * 32
        (destination / 'worker-status.json').write_text(json.dumps(dict(schema_version=1, generation=nonce, game_pid=self.pid)), encoding='utf-8')
        current = dict(generation=nonce, supervisor_pid=73, run=str(destination))
        base = dict(schema_version=1, result='accepted', epoch=6, phase=4, actor_a=0, actor_b=0,
                    request=0, persistence_request=21, persistence_state=6)
        replies = [dict(base, op='status', epoch=1, phase=1),
                   dict(base, op='load', epoch=1, request=21), dict(base, op='status'),
                   dict(base, op='restore', request=22), dict(base, op='status', actor_a=1, actor_b=2),
                   dict(base, op='jump', request=23, result='invalid'), dict(base, op='jump', request=24, result='stale')]
        completed = [subprocess.CompletedProcess([], 0 if row['result'] == 'accepted' else 5, json.dumps(row), '') for row in replies]
        with patch.object(checkpoint.workers, 'current', return_value=current), \
             patch.object(checkpoint, 'account_paths', return_value=(self.account, self.games)), \
             patch.object(checkpoint.subprocess, 'run', side_effect=completed) as run:
            report = checkpoint.load_restore('01', sidecar_path, self.root / 'report.json')
        self.assertTrue(report['restored'], report.get('error'))
        self.assertEqual([call.args[0][3] for call in run.call_args_list], ['status', 'load', 'status', 'restore', 'status', 'jump', 'jump'])
        self.assertEqual(report['adoption_record']['created_nouns'], 0)

    def test_unknown_load_outcome_is_not_retried_or_reported_restored(self):
        sidecar_path = self.root / 'sidecar.json'
        sidecar_path.write_text(json.dumps(self.sidecar), encoding='utf-8')
        current = dict(generation='b' * 32, supervisor_pid=73, run=str(self.root / 'destination-run'))
        initial = dict(schema_version=1, op='status', result='accepted', epoch=1,
                       phase=1, actor_a=0, actor_b=0, request=0)
        effects = [subprocess.CompletedProcess([], 0, json.dumps(initial), ''),
                   subprocess.TimeoutExpired(['fixture-load-command'], 13)]
        with patch.object(checkpoint.workers, 'current', return_value=current), \
             patch.object(checkpoint, 'account_paths', return_value=(self.account, self.games)), \
             patch.object(checkpoint.subprocess, 'run', side_effect=effects) as run:
            report = checkpoint.load_restore('01', sidecar_path, self.root / 'report.json')
        self.assertFalse(report['restored'])
        self.assertIn('timed out', report['error'])
        self.assertEqual([call.args[0][3] for call in run.call_args_list], ['status', 'load'])


if __name__ == '__main__': unittest.main()
