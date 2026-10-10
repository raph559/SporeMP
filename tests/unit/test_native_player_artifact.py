"""HOST/FIXTURE malformed export tests. All serialized values are synthetic."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

from test_spore_content import archive

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("native_player_artifact", REPO / "tools/native/native-player-artifact.py")
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)


def word(value):
    return struct.pack(">I", value)


def attrs(fields):
    fields = list(fields.items())
    size = 12 + len(fields) * 8 + sum(len(value) for _, value in fields)
    return struct.pack(">III", 0x01A80D26, size - 8, len(fields)) + b"".join(struct.pack(">II", key, len(value)) for key, value in fields) + b"".join(value for _, value in fields)


def frame(token, body):
    return struct.pack(">III", len(body) + 12, 0, token) + body


def records(slot=1):
    player = bytes.fromhex("02c216ed000000003f0000003f0000003f000000017f243b")
    player += attrs({0x0240E421: word(0), 0x03CDD54B: word(11)})
    player += attrs({0x05EE017B: word(33), 0x03A3A4B4: struct.pack(">II", 303, tool.ITEMS), **{key: struct.pack(">f", 1000 if name == "goal_total" else 0) for key, name in tool.PROGRESS}})
    avatar = bytes.fromhex("d0036e08ce9f6639017f243b") + attrs({0x0240E421: word(0), 0x03CDD54B: word(22)})
    avatar += bytes.fromhex("0135b1e8") + attrs({})
    avatar += bytes.fromhex("0116dd1b01186577") + attrs({0x03F64A78: struct.pack(">III", 30, 20, 10)})
    avatar += attrs({0x02415693: word(0)})
    avatar += bytes.fromhex("013f94d400000000") + attrs({})
    avatar += word(0) + attrs({})
    avatar += attrs({0x52773F6F: word(1), 0x33ACE6B5: word(55), 0x051B38FF: word(0x200 if slot == 1 else 0)})
    avatar += attrs({0x72AE9FE0: word(0)})
    items = bytes.fromhex("03a3aa3a") + attrs({0x03A3B3D4: word(1) + struct.pack(">IIB", 1, 2, 1), 0x06676971: word(1) + struct.pack(">II", 1, 2), 0x03A3B30D: word(0)})
    flags = bytearray(88)
    flags[30] = flags[37] = 1
    stage = attrs({0xB4177E0E: word(0), 0x061AFD98: word(0x3F800000), 0x061AFD99: word(0), 0xB4177E0F: word(1), 0x7220206F: word(0), 0xB4177E0D: bytes(8), 0x076B6609: word(88) + flags})
    return {tool.ROOT: struct.pack(">10I", 0x5030314D, 3, 11, 33, 22, 44, 101, tool.PLAYER, 202, tool.AVATAR),
            tool.INDEX: struct.pack(">9I", 1, 3, 3, tool.PLAYER, 1, tool.AVATAR, 1, tool.ITEMS, 1),
            tool.STAGE: stage,
            f"00000000!0179d304.{tool.PLAYER:08x}": frame(101, player),
            f"00000000!0179d304.{tool.AVATAR:08x}": frame(202, avatar),
            f"00000000!0179d304.{tool.ITEMS:08x}": frame(303, items)}


def packed(values):
    return archive([(key, data, len(data), 0) for key, data in values.items()])


def jsonlines(rows):
    return b"".join(json.dumps(row, sort_keys=True).encode() + b"\n" for row in rows)


def proof_fixture(slot=1):
    raw = packed(records(slot))
    expected = dict(request=9, generation="a" * 32, canonical_player=1, native_slot=slot, unique_game_id=33, canonical_home=44, artifact_sha256=tool.digest(raw))
    player = dict(event="native_checkpoint_player", owner=slot, player_native_id=11, primary=slot == 1, unique_game_id=33, goal_progress=0, goal_total=1000, social_progress=0, combat_progress=0, item_statuses=1, unlocked_items=1)
    census = [player, dict(event="native_checkpoint_item_status", player_native_id=11, group=1, instance=2, status=1), dict(event="native_checkpoint_unlocked_item", player_native_id=11, index=0, group=1, instance=2)]
    if slot == 2:
        census.append(dict(event="native_checkpoint_player", owner=1, player_native_id=1, primary=True, unique_game_id=99, goal_progress=0, goal_total=1000, social_progress=0, combat_progress=0, item_statuses=0, unlocked_items=0))
    census.append(dict(event="native_checkpoint_player_census", players=slot, complete=True))
    stage = dict(zip(tool.STAGE_NAMES, [0, 0x3F800000, 0, 1, 0, 0, 0, 1 << 30, 32, 0]))
    stage.update(event="native_primary_stage_export" if slot == 1 else "native_selected_stage_export", player_native_id=11, avatar_native_id=22, unique_game_id=33, serialization_version=tool.VERSION,
                 source_stage_unchanged=True, source_counter_idle=True, source_world_pending=False, original_write_return=True, stream_good=True, transfer_qualified=False)
    if slot == 2:
        stage.update(native_slot=2, destination_primary_stage_unchanged=True)
    rr = [dict(event="trace_start", executable_sha256=tool.EXE, sdk_commit=tool.SDK),
          dict(event="native_player_graph_selection", canonical_player=1, native_slot=slot, player_native_id=11, avatar_native_id=22, unique_game_id=33, canonical_home=44),
          dict(event="native_player_graph_export_enter", home_planet=44)]
    rr += [dict(row, phase="graph_before") for row in census]
    rr += [stage,
           dict(event="native_player_graph_herd", native_id=23, species_instance=10, species_type=20, species_group=30, archetype=66, archetype_group=0, generation=-1, personality=0),
           dict(event="native_player_graph_scene_references", unchanged=True, source_herd_present=True),
           dict(event="native_player_graph_projection_detach", status=0, neighbor_fields=1, herd_fields=1)]
    rr += [dict(row, phase="graph_after") for row in census]
    rr += [dict(event="native_player_graph_export_return", initialized=True, opened=True, root_good=True, classes_good=True, closed=True, source_identity_unchanged=True, success=True, bytes=len(raw), keys=6, transfer_qualified=False), dict(event="trace_stop", healthy=True, detach_status=0)]
    rows = [dict(schema_version=1, evidence_class="NATIVE_PROBE", sequence=i, pid=5, thread_id=6, foreign_callbacks=0, epoch=3, request=9, **row) for i, row in enumerate(rr, 1)]
    hosts = [dict(event="worker_supervisor_started", generation="a" * 32, host_pid=7), dict(event="game_exited", host_pid=7, game_pid=5, exit_code=0), dict(event="worker_exited", host_pid=7, exit_code=0)]
    return raw, rows, hosts, expected


def witness_bytes(witness):
    return json.dumps({key: witness[key] for key in tool.WITNESS_FIELDS}, separators=(",", ":")).encode() + b"\n"


def paused_fixture(slot=1):
    raw, rows, hosts, expected = proof_fixture(slot)
    expected['producer_mode'] = 'paused'
    witness = {key: 1 for key in tool.WITNESS_FIELDS}
    witness.update(id=9, player=1, source_player=11, native_player=11, source_avatar=22, native_avatar=22,
                   source_slot=slot, native_slot=slot, unique_game_id=33, canonical_home=44, scene=3, pause_request=9,
                   artifact_bytes=len(raw), approval_bytes=0, app_begin=100, app_end=300, ai_begin=50, ai_end=50,
                   zero_deltas=200, nonzero_deltas=0, elapsed_ms=3200, pause_before=0, pause_after=1,
                   process_id=5, process_created=1234567, preparation='a' * 64, challenge='b' * 64,
                   artifact=tool.digest(raw), approval='0' * 64)
    data = witness_bytes(witness)
    expected['source_witness_sha256'] = tool.digest(data)
    pause = dict(rows[0], event='native_quiescence_observed', owns_pause=True, gameplay_count=1, pause_flags=3,
                 foreign_pause_calls=0, input_epoch=1, app_begin=100, app_end=285, ai_begin=50, ai_end=50,
                 zero_native_deltas=185, nonzero_native_deltas=0, elapsed_ms=3000)
    rows.insert(1, pause)
    rows[-1] = dict(rows[-1], event='network_native_transfer_receipt', transfer=9, operation=1,
                    state_sha256=tool.digest(data), artifact_sha256=tool.digest(raw), completed_native_work=True)
    for i, row in enumerate(rows, 1):
        row['sequence'] = i
    hosts = [hosts[0], dict(event='created_suspended', host_pid=7, game_pid=5)]
    return raw, rows, hosts, expected, witness


class NativePlayerArtifactTests(unittest.TestCase):
    def reject_records(self, values):
        with self.assertRaises((tool.ArtifactError, tool.content.ContentError)):
            tool.decode_artifact(packed(values))

    def prove(self, slot=1):
        raw, rows, hosts, expected = proof_fixture(slot)
        decoded = tool.decode_artifact(raw)
        proof = tool.validate_provenance(decoded, jsonlines(rows), jsonlines(hosts), expected)
        return decoded, proof

    def test_primary_and_secondary_selected_census(self):
        for slot in (1, 2):
            with self.subTest(slot=slot):
                decoded, proof = self.prove(slot)
                self.assertEqual(proof["selection"]["native_slot"], slot)
                self.assertEqual(decoded["stage_words"][-3:], [1 << 30, 32, 0])

    def test_exact_approval_layout_and_no_native_tokens(self):
        decoded, proof = self.prove(2)
        data = tool.approval_bytes(decoded, proof)
        self.assertEqual(len(data), 220)
        self.assertEqual(struct.unpack_from("<II", data), (0x4630314D, 3))
        self.assertEqual(data[8:72], decoded["artifact_sha256"].encode())
        words = struct.unpack_from("<32I", data, 72)
        self.assertEqual(words[1:6], (tool.VERSION, 11, 22, 33, 44))
        self.assertEqual(words[13], 0xFFFFFFFF)
        self.assertEqual(list(words[22:]), decoded["stage_words"])
        self.assertEqual(struct.unpack_from("<III", data, 200), (2, 1, 1))
        self.assertEqual(struct.unpack_from("<II", data, 212), (2, 1))
        self.assertFalse(any(token in json.dumps(proof) for token in ('"token"', '"body"')))

    def test_native_nine_digit_progression_roundtrip(self):
        raw, rows, hosts, expected = proof_fixture()
        decoded = tool.decode_artifact(raw)
        decoded['progression_bits'][0] = 0x3DFCD6EA
        value = tool.finite(decoded['progression_bits'][0])
        printed = float(format(value, '.9g'))
        self.assertNotEqual(value, printed)
        for row in rows:
            if row['event'] == 'native_checkpoint_player':
                row['goal_progress'] = printed
        tool.validate_provenance(decoded, jsonlines(rows), jsonlines(hosts), expected)

    def test_archive_key_allowlist_and_missing_stage(self):
        values = records()
        del values[tool.STAGE]
        self.reject_records(values)
        values["00000000!00000001.deadbeef"] = b"junk"
        self.reject_records(values)

    def test_container_and_size_limits(self):
        for data in (b"", b"DBBF" + packed(records())[4:], b"X" * (tool.MAX_ARTIFACT + 1)):
            with self.subTest(size=len(data)), self.assertRaises(tool.ArtifactError):
                tool.decode_artifact(data)

    def test_root_schema_classes_and_unassigned_ids(self):
        for index, value in ((0, 0), (1, 2), (2, 0), (3, 0xFFFFFFFF), (4, 11), (6, 999), (7, tool.AVATAR), (9, tool.PLAYER)):
            with self.subTest(index=index):
                values = records()
                root = bytearray(values[tool.ROOT])
                struct.pack_into(">I", root, index * 4, value)
                values[tool.ROOT] = bytes(root)
                self.reject_records(values)

    def test_class_count_duplicate_and_extra_object(self):
        values = records()
        values[tool.INDEX] = struct.pack(">9I", 1, 3, 3, tool.PLAYER, 1, tool.PLAYER, 1, tool.ITEMS, 1)
        self.reject_records(values)
        values = records()
        key = f"00000000!0179d304.{tool.PLAYER:08x}"
        values[key] *= 2
        self.reject_records(values)

    def test_duplicate_serialization_token_and_trailing_body(self):
        for change in ('token', 'trailing'):
            values = records()
            key = f"00000000!0179d304.{tool.AVATAR:08x}"
            data = bytearray(values[key])
            if change == 'token':
                struct.pack_into(">I", data, 8, 101)
            else:
                data += b"trailing"
                struct.pack_into(">I", data, 0, len(data))
            values[key] = bytes(data)
            self.reject_records(values)

    def test_nonempty_source_neighbors_and_herd(self):
        for attribute in (0x02415693, 0x72AE9FE0):
            values = records()
            key = f"00000000!0179d304.{tool.AVATAR:08x}"
            data = values[key]
            marker = attrs({attribute: word(0)})
            self.assertIn(marker, data)
            values[key] = data.replace(marker, attrs({attribute: word(1)}))
            self.reject_records(values)

    def test_malformed_stage_flags_and_nonfinite_float(self):
        for key, value in ((0x076B6609, word(88) + bytes([2]) + bytes(87)), (0x076B6609, word(87) + bytes(88)), (0x061AFD98, word(0x7FC00000)), (0x7220206F, word(0xBF800000))):
            values = records()
            fields, _ = tool.attributes(values[tool.STAGE], 0)
            fields[key] = value
            values[tool.STAGE] = attrs(fields)
            self.reject_records(values)

    def test_json_duplicate_nonfinite_and_truncated_rows(self):
        for data in (b'{"a":1,"a":2}\n', b'{"a":NaN}\n', b'{"a":1}', b'[]\n'):
            with self.subTest(data=data), self.assertRaises(tool.ArtifactError):
                tool.jsonl(data)

    def test_only_identical_native_epoch_redundancy_is_supported(self):
        row = b'{"epoch":3,"epoch":3}\n'
        self.assertEqual(tool.jsonl(row, native=True), [{'epoch': 3}])
        with self.assertRaises(tool.ArtifactError):
            tool.jsonl(row)
        for row in (b'{"epoch":3,"epoch":4}\n', b'{"epoch":3,"epoch":3.0}\n', b'{"request":9,"request":9}\n'):
            with self.assertRaises(tool.ArtifactError):
                tool.jsonl(row, native=True)

    def test_missing_selection_is_not_legacy_approval(self):
        raw, rows, hosts, expected = proof_fixture()
        del rows[1]
        for i, row in enumerate(rows, 1):
            row['sequence'] = i
        with self.assertRaisesRegex(tool.ArtifactError, 'selection'):
            tool.validate_provenance(tool.decode_artifact(raw), jsonlines(rows), jsonlines(hosts), expected)

    def test_identity_epoch_thread_and_census_mismatches(self):
        changes = [('native_player_graph_selection', 'canonical_player', 78), ('native_player_graph_selection', 'epoch', 4),
                   ('native_player_graph_selection', 'native_slot', 2), ('native_player_graph_export_return', 'thread_id', 99),
                   ('native_player_graph_export_return', 'success', False), ('native_checkpoint_player', 'unique_game_id', 34),
                   ('native_primary_stage_export', 'learned_0', 0), ('native_player_graph_herd', 'species_instance', 12)]
        for event, key, value in changes:
            raw, rows, hosts, expected = proof_fixture()
            next(row for row in rows if row['event'] == event)[key] = value
            with self.subTest(event=event, key=key), self.assertRaises(tool.ArtifactError):
                tool.validate_provenance(tool.decode_artifact(raw), jsonlines(rows), jsonlines(hosts), expected)

    def test_secondary_cannot_mutate_unselected_primary(self):
        raw, rows, hosts, expected = proof_fixture(2)
        next(row for row in rows if row['event'] == 'native_checkpoint_player' and row.get('phase') == 'graph_after' and row['owner'] == 1)['goal_progress'] = 1
        with self.assertRaisesRegex(tool.ArtifactError, 'Whole source'):
            tool.validate_provenance(tool.decode_artifact(raw), jsonlines(rows), jsonlines(hosts), expected)

    def test_secondary_stage_preserves_primary(self):
        raw, rows, hosts, expected = proof_fixture(2)
        next(row for row in rows if row['event'] == 'native_selected_stage_export')['destination_primary_stage_unchanged'] = False
        with self.assertRaisesRegex(tool.ArtifactError, 'Secondary stage'):
            tool.validate_provenance(tool.decode_artifact(raw), jsonlines(rows), jsonlines(hosts), expected)

    def test_wrong_producer_generation_process_exit_and_digest(self):
        for change in ('generation', 'pid', 'exit', 'digest'):
            raw, rows, hosts, expected = proof_fixture()
            if change == 'generation':
                hosts[0]['generation'] = 'b' * 32
            elif change == 'pid':
                hosts[1]['game_pid'] = 66
            elif change == 'exit':
                hosts[1]['exit_code'] = 1
            else:
                expected['artifact_sha256'] = '0' * 64
            with self.subTest(change=change), self.assertRaises(tool.ArtifactError):
                tool.validate_provenance(tool.decode_artifact(raw), jsonlines(rows), jsonlines(hosts), expected)

    def test_output_refuses_existing_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(tool.ArtifactError):
                tool.write_outputs(Path(directory), {}, b"fixture")

    def test_regular_input_size_and_hardlink_guard(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'input'
            path.write_bytes(b'12345')
            with self.assertRaises(tool.content.ContentError):
                tool.read_file(path, 4)
            alias = path.with_name('alias')
            import os
            os.link(path, alias)
            with self.assertRaises(tool.ArtifactError):
                tool.read_file(path, 10)

    def test_paused_selected_source_without_process_exit(self):
        for slot in (1, 2):
            raw, rows, hosts, expected, witness = paused_fixture(slot)
            result = tool.validate_provenance(tool.decode_artifact(raw), jsonlines(rows), jsonlines(hosts), expected, witness_bytes(witness))
            self.assertEqual(result['producer_mode'], 'paused')
            self.assertEqual(result['source_witness']['sha256'], expected['source_witness_sha256'])

    def test_paused_mode_requires_pinned_witness_and_native_receipt(self):
        for change in ('absent', 'digest', 'missing_receipt', 'receipt_digest', 'receipt_order', 'exit', 'release'):
            raw, rows, hosts, expected, witness = paused_fixture()
            data = witness_bytes(witness)
            if change == 'absent':
                data = None
            elif change == 'digest':
                expected['source_witness_sha256'] = '0' * 64
            elif change == 'missing_receipt':
                rows.pop()
            elif change == 'receipt_digest':
                rows[-1]['state_sha256'] = '0' * 64
            elif change == 'receipt_order':
                rows.insert(2, rows.pop())
                for i, row in enumerate(rows, 1):
                    row['sequence'] = i
            elif change == 'exit':
                hosts.append(dict(event='game_exited', game_pid=5, exit_code=0))
            else:
                rows.append(dict(rows[-1], event='native_quiescence_resume_return', sequence=len(rows) + 1))
            with self.subTest(change=change), self.assertRaises(tool.ArtifactError):
                tool.validate_provenance(tool.decode_artifact(raw), jsonlines(rows), jsonlines(hosts), expected, data)

    def test_witness_identity_scope_and_measurements(self):
        changes = {'schema': 2, 'op': 2, 'id': 10, 'player': 2, 'revision': 0, 'worker_generation': 0,
                   'preparation': '0' * 64, 'challenge': 'A' * 64, 'artifact': '0' * 64, 'approval': 'c' * 64,
                   'artifact_bytes': 0, 'approval_bytes': 4, 'unique_game_id': 34, 'canonical_home': 45,
                   'source_player': 1, 'source_avatar': 1, 'native_slot': 2, 'source_slot': 2,
                   'pause_request': 8, 'process_id': 99, 'process_created': 0, 'scene': 4,
                   'app_end': 100, 'ai_end': 51, 'zero_deltas': 0, 'nonzero_deltas': 1, 'elapsed_ms': 2999,
                   'pause_before': 1, 'pause_after': 2, 'remaining_imports': 2, 'baseline': True}
        for key, value in changes.items():
            raw, rows, hosts, expected, witness = paused_fixture()
            witness[key] = value
            data = witness_bytes(witness)
            expected['source_witness_sha256'] = tool.digest(data)
            rows[-1]['state_sha256'] = tool.digest(data)
            with self.subTest(key=key), self.assertRaises(tool.ArtifactError):
                tool.validate_provenance(tool.decode_artifact(raw), jsonlines(rows), jsonlines(hosts), expected, data)

    def test_witness_canonical_fields_and_bytes(self):
        for change in ('extra', 'omitted', 'reordered', 'spaces', 'trailing'):
            raw, rows, hosts, expected, witness = paused_fixture()
            if change == 'extra':
                witness['state'] = '0' * 64
            elif change == 'omitted':
                del witness['process_created']
            if change in ('extra', 'omitted'):
                data = json.dumps(witness, separators=(',', ':')).encode() + b'\n'
            elif change == 'reordered':
                data = json.dumps(witness, sort_keys=True, separators=(',', ':')).encode() + b'\n'
            elif change == 'spaces':
                data = json.dumps(witness).encode() + b'\n'
            else:
                data = witness_bytes(witness) + b'\n'
            expected['source_witness_sha256'] = tool.digest(data)
            rows[-1]['state_sha256'] = tool.digest(data)
            with self.subTest(change=change), self.assertRaises(tool.ArtifactError):
                tool.validate_provenance(tool.decode_artifact(raw), jsonlines(rows), jsonlines(hosts), expected, data)


if __name__ == '__main__':
    unittest.main()
