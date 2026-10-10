"""HOST/FIXTURE artifact refusal and protected profile restore; no game launch."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from copy import deepcopy
from unittest.mock import patch

path = Path(__file__).resolve().parents[2] / 'tools/native/durable-checkpoint.py'
spec = importlib.util.spec_from_file_location('durable_checkpoint', path)
checkpoint = importlib.util.module_from_spec(spec); spec.loader.exec_module(checkpoint)


def artifact(names=None):
    content = b'HOST native bytes fixture; never loaded by SPORE'
    digest = bytes.fromhex(checkpoint.sha(content))
    result = bytearray(struct.pack('<II', checkpoint.MAGIC, 1))
    result.extend(bytes.fromhex('11' * 32 + checkpoint.EXE + '33' * 32)); result.extend(digest * 6)
    result.extend(bytes.fromhex('44' * 32)); result.extend(struct.pack('<QI4II', 7, 300000, 232, 9601, 1, 9602, 6))
    for name in names or ['spore/' + relative for _, relative in checkpoint.multiplayer.WORLD_FILES]:
        encoded = name.encode(); result.extend(struct.pack('<I', len(encoded))); result.extend(encoded); result.extend(digest)
        result.extend(struct.pack('<I', len(content))); result.extend(content)
    return bytes(result)


class DurableCheckpointTests(unittest.TestCase):
    def test_foreign_save_requires_both_native_roots_inside_same_barrier(self):
        rows = [dict(event='trace_start', bridge_version='HOST'),
                dict(event='checkpoint_actor', request=7, epoch=9, owner=1, native_id=11),
                dict(event='checkpoint_actor', request=7, epoch=9, owner=2, native_id=12),
                dict(event='native_checkpoint_player', request=7, epoch=9, phase='before_save', primary=True, player_native_id=21),
                dict(event='checkpoint_probe_snapshot', request=7, epoch=9, valid=True),
                dict(event='native_checkpoint_barrier', request=7, epoch=9, schema=4),
                dict(event='native_relationship_checkpoint_written', epoch=9, owner=2, success=True, primary_unchanged=True, replayed_actions=0),
                dict(event='native_history_checkpoint_written', epoch=9, owner=2, success=True, primary_unchanged=True, replayed_events=0),
                dict(event='native_checkpoint_extension_written', request=7, epoch=9, schema=4, success=True, player_native_id=22),
                dict(event='native_save_return', request=7, epoch=9, native_success=True, requested_epoch=9, observed_epoch=9),
                dict(event='native_checkpoint_save_complete', request=7, epoch=9, success=True, diagnostic_only=False)]
        for n, row in enumerate(rows, 1): row['sequence'] = n
        self.assertEqual(checkpoint.saved_metadata(rows)['native_ids'], [11, 12, 21, 22])
        legacy = deepcopy(rows)
        legacy[5]['schema'] = legacy[8]['schema'] = 3
        self.assertEqual(checkpoint.saved_metadata(legacy[:6] + legacy[8:])['native_ids'], [11, 12, 21, 22])
        for index in (6, 7):
            with self.subTest(missing=index), self.assertRaises(ValueError):
                checkpoint.saved_metadata(rows[:index] + rows[index+1:])
            for field, value in [('owner', 1), ('epoch', 8), ('success', False), ('primary_unchanged', False),
                                 ('sequence', 1), ('replayed_actions' if index == 6 else 'replayed_events', 1)]:
                invalid = deepcopy(rows); invalid[index][field] = value
                with self.subTest(index=index, field=field), self.assertRaises(ValueError):
                    checkpoint.saved_metadata(invalid)
            with self.subTest(duplicate=index), self.assertRaises(ValueError):
                checkpoint.saved_metadata(rows + [deepcopy(rows[index])])
        invalid = deepcopy(rows); invalid[8]['schema'] = 3
        with self.assertRaises(ValueError): checkpoint.saved_metadata(invalid)

    def test_bounded_codec_and_corruption(self):
        original = artifact(); metadata, members = checkpoint.read_artifact(original)
        self.assertEqual(metadata['native_ids'], [232, 9601, 1, 9602]); self.assertEqual(len(members), 6)
        for candidate in [b'', original[:327], original[:-1], original+b'x', original[:-1]+bytes([original[-1]^1])]:
            with self.subTest(length=len(candidate)), self.assertRaises(ValueError): checkpoint.read_artifact(candidate)

    def test_unsafe_duplicate_and_missing_members(self):
        names = ['spore/' + relative for _, relative in checkpoint.multiplayer.WORLD_FILES]
        for bad in ['spore/../outside', 'spore/a:b', 'spore/CON.spo', 'spore/a.', 'creations//x', 'spore/Planets.package', 'spore/planets.package']:
            with self.subTest(path=bad), self.assertRaises(ValueError): checkpoint.read_artifact(artifact([bad]+names[1:]))

    def test_no_admission_or_diagnostic_result_can_seal(self):
        for rows in [[], [{'event':'native_save_return','native_success':True}], [{'event':'native_checkpoint_save_complete','success':True,'diagnostic_only':True}]]:
            with self.assertRaises(ValueError): checkpoint.saved_metadata(rows)

    def test_output_traversal_cannot_export_raw_saves_outside_local(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); (root/'local').mkdir()
            with patch.object(checkpoint,'REPO',root):
                with self.assertRaises(ValueError): checkpoint.new_output(root/'local/../public-raw')
                self.assertFalse((root/'public-raw').exists())

    def test_restore_keeps_original_and_post_restore_work(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root/'local').mkdir(); spore = root/'profile/Spore'; creations=root/'profile/Creations'
            spore.mkdir(parents=True); creations.mkdir(); (spore/'original.spo').write_bytes(b'original protected work')
            source = root/'local/input.smp'; source.write_bytes(artifact()); output=root/'local/restore'
            with patch.object(checkpoint,'REPO',root), patch.object(checkpoint,'no_games'), patch.object(checkpoint.service.diag,'game_running',return_value=False), patch.object(checkpoint,'profile_paths',return_value=[spore,creations]), patch.object(checkpoint.workers,'current',return_value={}), patch.object(checkpoint.workers,'supervisor_running',return_value=False):
                result=checkpoint.restore(source,['01'],output)
                self.assertTrue(result['restored']); self.assertFalse((spore/'original.spo').exists())
                (spore/'after.spo').write_bytes(b'new work after restore')
                checkpoint.undo(output/'restore.json')
                self.assertEqual((spore/'original.spo').read_bytes(),b'original protected work')
                journal=json.loads((output/'restore.json').read_text())
                retained=next(e for e in journal['entries'] if e['active']==str(spore))['post_restore']
                self.assertEqual((Path(retained)/'after.spo').read_bytes(),b'new work after restore')

    def test_restore_accepts_never_started_worker_but_not_invalid_metadata(self):
        for failure, accepted in [(FileNotFoundError(2, 'missing current.json'), True), (ValueError('Invalid worker generation'), False)]:
            with self.subTest(failure=type(failure).__name__), tempfile.TemporaryDirectory() as temp:
                root = Path(temp); (root/'local').mkdir(); spore = root/'profile/Spore'; creations=root/'profile/Creations'
                spore.mkdir(parents=True); creations.mkdir(); (spore/'original.spo').write_bytes(b'original protected work')
                source = root/'local/input.smp'; source.write_bytes(artifact()); output=root/'local/restore'
                with patch.object(checkpoint,'REPO',root), patch.object(checkpoint,'no_games'), patch.object(checkpoint.service.diag,'game_running',return_value=False), patch.object(checkpoint,'profile_paths',return_value=[spore,creations]), patch.object(checkpoint.workers,'current',side_effect=failure), patch.object(checkpoint.workers,'supervisor_running') as running:
                    if accepted:
                        self.assertTrue(checkpoint.restore(source,['01'],output)['restored'])
                        running.assert_not_called()
                        checkpoint.undo(output/'restore.json')
                        self.assertEqual((spore/'original.spo').read_bytes(),b'original protected work')
                    else:
                        with self.assertRaises(ValueError): checkpoint.restore(source,['01'],output)
                        self.assertEqual((spore/'original.spo').read_bytes(),b'original protected work')


if __name__ == '__main__': unittest.main()
