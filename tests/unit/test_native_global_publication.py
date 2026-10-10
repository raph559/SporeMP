"""HOST contract tests for closed-owner Save approval; no native launches."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("publication_under_test", ROOT / "tools/native/native-global-publication.py")
p = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(p)


def boundary():
    saved = {"players": {}, "avatars": {}, "extension_words": [0] * 4132,
             "primary_stage_words": [0, 0x3F800000, 0, 1, 0, 0, 0, 0, 0, 0],
             "secondary_stage_words": [0, 0x3F800000, 0, 1, 0, 0, 0, 0, 0, 0]}
    saved["extension_words"][3] = 12
    rows = []
    def row(event, seq, **extra):
        return dict(event=event, sequence=seq, epoch=3, request=7, pid=25, **extra)
    for slot in (1, 2):
        player, avatar, herd, game = 10 + slot, 20 + slot, 30 + slot, 103 - slot
        saved["players"][player] = dict(native_id=player, unique_game_id=game,
            progression_bits={name: 0 for _, name in p.artifact.PROGRESS}, statuses=[], unlocked_items=[],
            player_body_sha256=str(slot) * 64, collection_body_sha256=str(slot + 2) * 64)
        saved["avatars"][avatar] = dict(body_sha256=str(slot + 4) * 64,
            tables=[(0, 0, {0x03F64A78: struct.pack(">III", 43, 42, 41)})])
        actor = dict(owner=slot, native_id=avatar, herd_native_id=herd, species_instance=41,
                     species_type=42, species_group=43, archetype=44)
        rows.append(row("checkpoint_actor", slot, **actor))
        saved["extension_words"][4 + (slot - 1) * 6:10 + (slot - 1) * 6] = [avatar, herd, 41, 42, 43, 44]
        state = dict(owner=slot, player_native_id=player, primary=slot == 1, unique_game_id=game,
                     item_statuses=0, unlocked_items=0, **{name: 0.0 for _, name in p.artifact.PROGRESS})
        rows.append(row("native_checkpoint_player", 10 + slot, phase="before_save", **state))
        rows.append(row("native_checkpoint_player", 30 + slot, phase="after_save", **state))
    rows += [row("native_checkpoint_player_census", 13, phase="before_save", players=2, complete=True),
             row("native_checkpoint_player_census", 33, phase="after_save", players=2, complete=True),
             row("native_owned_reward_state", 14, queue_empty=True, brain=0, dna=0.0),
             row("native_checkpoint_barrier", 15, schema=3),
             row("native_checkpoint_extension_written", 21, player_native_id=12)]
    rows.sort(key=lambda r: r["sequence"])
    save = dict(request=7, epoch=3, enter_sequence=20, return_sequence=30, complete_sequence=40)
    return rows, saved, save


class NativeGlobalPublicationTests(unittest.TestCase):
    def test_cleanup_cannot_stand_in_for_destination_activation(self):
        base = {key: "1" * 64 if key in p.artifact.WITNESS_DIGESTS else 1 for key in p.artifact.WITNESS_FIELDS}
        base.update(id=81, player=1, op=5, process_id=25, app_begin=1, app_end=200, ai_begin=10, ai_end=20,
                    source_slot=2, pause_before=1, pause_after=0, elapsed_ms=3000, zero_deltas=199)
        cleanup = dict(base, op=7, process_id=26, native_player=0, native_avatar=0, remaining_imports=0)
        def encoded(row):
            return p.digest(json.dumps({k: row[k] for k in p.artifact.WITNESS_FIELDS}, separators=(",", ":")).encode() + b"\n")
        final = dict(id=81, player=1, avatar_generation=3, durable_revision=15, destination_witness={"bound": "same"},
            destination_witness_sha256="2" * 64, last_receipt=cleanup, last_receipt_sha256=encoded(cleanup))
        earlier = dict(schema=1, ok=True, native_phase=8, native_completion=False, ownership_committed=True,
            id=81, player=1, avatar_generation=3, durable_revision=13, destination_witness={"bound": "same"},
            destination_witness_sha256="2" * 64, last_receipt=base, last_receipt_sha256=encoded(base))
        with self.assertRaises(ValueError):
            p.activation_receipt({}, final)
        def invoke(row):
            with patch.object(p.observer, "pinned_file", return_value=json.dumps(row).encode()):
                return p.activation_receipt({"activation_receipt": "pinned"}, final)
        self.assertEqual(invoke(earlier), (base, encoded(base)))
        for changed in (dict(earlier, native_phase=7), dict(earlier, durable_revision=16),
                        dict(earlier, id=82), dict(earlier, destination_witness_sha256="3" * 64)):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                invoke(changed)

    def test_completed_head_is_bound_to_saved_process_player_and_order(self):
        raw = b"isolated graph decoder fixture"
        decoded = dict(artifact_sha256=p.digest(raw), artifact_bytes=len(raw),
            root=dict(player_native_id=1, avatar_native_id=2, unique_game_id=101, canonical_home=4),
            species=[41, 42, 43], age=1, animal_archetype=1, progression_bits=[0] * 4,
            statuses=[], unlocked=[], unlock_points=0, stage_words=[0, 0x3F800000, 0, 1, 0, 0, 0, 0, 0, 0])
        approval = p.artifact.approval_bytes(decoded, {"herd": dict(archetype=1, archetype_group=0, generation=0, personality=0)})
        base = {key: "1" * 64 if key in p.artifact.WITNESS_DIGESTS else 1 for key in p.artifact.WITNESS_FIELDS}
        base.update(id=81, player=1, worker=12, worker_generation=9, scene=3, process_id=25, process_created=1234,
            artifact=p.digest(raw), approval=p.digest(approval), artifact_bytes=len(raw), approval_bytes=len(approval),
            unique_game_id=101, canonical_home=4, native_player=12, native_avatar=22, native_slot=2,
            app_begin=1, app_end=200, ai_begin=100, ai_end=100, zero_deltas=199, nonzero_deltas=0, elapsed_ms=3000,
            pause_before=0, pause_after=1, op=2)
        receipt = dict(schema=1, ok=True, native_phase=15, native_completion=True, ownership_committed=True,
            player=1, id=81, durable_revision=13, destination_witness=base,
            last_receipt=dict(base, op=5, pause_before=1, pause_after=0, ai_end=110))
        def encoded(r):
            return p.digest(json.dumps({k: r[k] for k in p.artifact.WITNESS_FIELDS}, separators=(",", ":")).encode() + b"\n")
        def invoke(change=None):
            r = copy.deepcopy(receipt)
            rows = [dict(event="native_player_import_return", pid=25, request=81, epoch=3, sequence=10,
                    player_native_id=12, avatar_native_id=22, unique_game_id=101, destination_primary_unchanged=True,
                    global_color_unchanged=True, stage_restored=True, destination_stage_unchanged=True)]
            slots = {2: dict(player_native_id=12, avatar_native_id=22, unique_game_id=101)}
            if change:
                change(r, rows, slots)
            r["destination_witness_sha256"] = encoded(r["destination_witness"])
            r["last_receipt_sha256"] = encoded(r["last_receipt"])
            for op, key, sequence in ((2, "destination_witness_sha256", 20), (5, "last_receipt_sha256", 30)):
                rows.append(dict(event="network_native_transfer_receipt", transfer=81, operation=op, epoch=3, sequence=sequence,
                    state_sha256=r[key], artifact_sha256=p.digest(raw), completed_native_work=True))
            files = {"artifact": raw, "approval": approval, "receipt": json.dumps(r).encode()}
            spec = dict(player=1, artifact="artifact", approval="approval", receipt="receipt")
            with patch.object(p.observer, "pinned_file", side_effect=lambda key, limit: files[key]), patch.object(p.artifact, "decode_artifact", return_value=decoded):
                return p.superseded_head(spec, rows, slots, dict(worker=12, worker_generation=9),
                    dict(epoch=3, enter_sequence=40, complete_sequence=50), 13)
        self.assertEqual(invoke()["transfer_id"], 81)
        changes = [
            lambda r, rows, slots: slots[2].update(player_native_id=99),
            lambda r, rows, slots: r["last_receipt"].update(process_created=5678),
            lambda r, rows, slots: r["destination_witness"].update(nonzero_deltas=1),
            lambda r, rows, slots: r["last_receipt"].update(ai_end=100),
            lambda r, rows, slots: r.update(durable_revision=14),
            lambda r, rows, slots: rows[0].update(sequence=41),
            lambda r, rows, slots: rows[0].update(destination_primary_unchanged=False),
            lambda r, rows, slots: rows.append(dict(event="native_player_retired", player_native_id=12, sequence=35)),
        ]
        for change in changes:
            with self.subTest(change=change), self.assertRaises(ValueError):
                invoke(change)

    def test_class_frames_reject_truncated_duplicate_and_trailing_objects(self):
        a = struct.pack(">III", 16, 0, 1) + b"abcd"
        b = struct.pack(">III", 12, 0, 2)
        self.assertEqual(p.class_frames(a + b, 2), [a, b])
        for raw, count in ((a + b[:-1], 2), (a + a, 2), (a + b, 1), (a, 0), (a, 16385)):
            with self.subTest(raw=raw, count=count), self.assertRaises(ValueError):
                p.class_frames(raw, count)

    def test_save_boundary_requires_both_original_players(self):
        rows, saved, save = boundary()
        slots = p.native_save_inclusion(rows, saved, save, [101, 102, 0, 0])
        self.assertEqual([(slots[s]["canonical_player"], slots[s]["player_native_id"]) for s in (1, 2)], [(2, 11), (1, 12)])
        mutations = [
            lambda r, s: s["players"][12].update(unique_game_id=103),
            lambda r, s: s["players"][12]["progression_bits"].update(goal_progress=0x3F800000),
            lambda r, s: s["players"][12]["statuses"].append(dict(group=1, instance=2, status=1)),
            lambda r, s: s["extension_words"].__setitem__(10, 23),
            lambda r, s: s["extension_words"].__setitem__(3, 11),
            lambda r, s: s["secondary_stage_words"].__setitem__(4, 0x3F800000),
            lambda r, s: next(x for x in r if x["event"] == "native_owned_reward_state").update(queue_empty=False),
            lambda r, s: next(x for x in r if x.get("phase") == "after_save").update(unique_game_id=555),
            lambda r, s: next(x for x in r if x.get("phase") == "before_save").update(sequence=25),
        ]
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                r, s = copy.deepcopy(rows), copy.deepcopy(saved)
                mutate(r, s)
                with self.assertRaises((ValueError, KeyError)):
                    p.native_save_inclusion(r, s, save, [101, 102, 0, 0])

    def test_witness_hash_covers_exact_order_and_every_field(self):
        witness = {key: "1" * 64 if key in p.artifact.WITNESS_DIGESTS else 1 for key in p.artifact.WITNESS_FIELDS}
        raw = json.dumps(witness, separators=(",", ":")).encode() + b"\n"
        self.assertEqual(p.witness(witness, p.digest(raw)), witness)
        altered = dict(witness, process_created=2)
        with self.assertRaises(ValueError):
            p.witness(altered, p.digest(raw))
        with self.assertRaises(ValueError):
            p.witness(dict(witness, extra=1), p.digest(raw))

    def test_m10g_exact_binary_offsets_and_absent_head(self):
        proof = dict(universe="1" * 64, selected_artifact_sha256="2" * 64, candidate_artifact_sha256="3" * 64,
            candidate_provenance="4" * 64, expected_revision=13, selected_checkpoint_revision=2,
            owner=dict(worker=12, worker_generation=9, location=[1, 2, 3, 4]),
            save=dict(request=7, epoch=3), process_id=25, process_created=123456789, event_id=14,
            saved_slots={"1": dict(player_native_id=11, avatar_native_id=21, unique_game_id=102),
                         "2": dict(player_native_id=12, avatar_native_id=22, unique_game_id=101)},
            superseded_heads=[dict(player=1, transfer_id=8104, artifact_sha256="6" * 64, approval_sha256="7" * 64)],
            effect=dict(record=dict(planet_id=99, spice=[100, 0, 0])))
        result = p.encode_approval(proof, "5" * 64)
        self.assertEqual(len(result), 464)
        self.assertEqual(struct.unpack_from("<II", result), (0x4730314D, 1))
        self.assertEqual(result[136:168], bytes.fromhex("5" * 64))
        self.assertEqual(struct.unpack_from("<10Q", result, 168), (13, 2, 12, 9, 1, 2, 3, 4, 7, 3))
        self.assertEqual(struct.unpack_from("<IIQQ8I", result, 248), (25, 6, 123456789, 14, 11, 21, 12, 22, 101, 102, 2, 1))
        self.assertEqual(struct.unpack_from("<Q", result, 304), (8104,))
        self.assertEqual(result[312:344], bytes.fromhex("6" * 64))
        self.assertEqual(result[376:448], bytes(72))
        self.assertEqual(struct.unpack_from("<4I", result, 448), (99, 100, 0, 0))
        # Native Save request7 may repeat in another process; it cannot be the
        # durable global event identity for this publication at revision14.
        with self.assertRaises(ValueError):
            p.encode_approval(dict(proof, event_id=7), "5" * 64)

    def test_stage_and_native_owner_census_are_not_optional(self):
        rows, saved, save = boundary()
        for event in ("checkpoint_actor", "native_checkpoint_player_census", "native_checkpoint_extension_written", "native_owned_reward_state"):
            changed = [r for r in rows if r["event"] != event]
            with self.subTest(event=event), self.assertRaises((ValueError, KeyError)):
                p.native_save_inclusion(changed, saved, save, [101, 102, 0, 0])


if __name__ == "__main__":
    unittest.main()
