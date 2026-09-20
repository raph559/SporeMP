"""Synthetic schema/evidence-negative tests, never SPORE runtime evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("gameplay_trace", ROOT / "tools/native/analyze-gameplay-trace.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture():
    records = []
    epoch = 1
    def add(event, **fields):
        nonlocal epoch
        row = dict.fromkeys(module.INTEGER, 0)
        row.update(schema_version=1, sequence=len(records) + 1, qpc=len(records), qpc_frequency=10_000_000,
                   pid=100, thread_id=101, scene_epoch=epoch, event=event, sdk_commit=module.SDK,
                   evidence_class="NATIVE_PROBE", executable_sha256=module.EXE, bridge_version="FIXTURE",
                   source="engine" if event in module.ENGINE else "mod", has_state=False, dead=False,
                   health=None, energy=None, hunger=None, multiplayer_mutations=False, mode_id=module.CREATURE_MODE)
        row.update(fields)
        records.append(row)
        if event == "scene_exit":
            epoch += 1
    add("trace_start")
    add("hooks_ready", count=7)
    add("entity_created", entity_id=1)
    add("jump_enter", entity_id=1, action_id=1, argument=0)
    add("jump_return", entity_id=1, action_id=1, argument=0, result=1)
    add("jump_land", entity_id=1)
    add("entity_invalidated", entity_id=1)
    add("scene_exit")
    add("trace_stop")
    return records


class GameplayTraceTests(unittest.TestCase):
    def test_complete_fixture_cannot_certify_native_acceptance(self):
        result = module.analyze(fixture(), require_native=True)
        self.assertTrue(result["m02_trace_coverage_ready"])
        self.assertEqual(result["native_acceptance"], "NOT_VERIFIED")

    def test_orphan_duplicate_and_unfinished_actions_are_rejected(self):
        for index, change in [(3, {"action_id": 0}), (4, {"action_id": 5}), (4, {"argument": 9}),
                              (4, {"event": "jump_land", "action_id": 0})]:
            with self.subTest(change=change):
                records = fixture()
                records[index].update(change)
                with self.assertRaises(ValueError): module.analyze(records)

    def test_retired_entity_cannot_resurrect(self):
        records = fixture()
        records[6]["event"] = "entity_invalidated"
        records[7].update(event="entity_observed", source="mod", entity_id=1)
        with self.assertRaisesRegex(ValueError, "resurrected"): module.analyze(records)

    def test_return_after_reentrant_destruction_is_allowed_without_dereference(self):
        records = fixture()
        records[4], records[6] = records[6], records[4]
        records[5].update(event="diagnostic_snapshot", source="mod", entity_id=0)
        for i, row in enumerate(records): row.update(sequence=i+1, qpc=i)
        result = module.analyze(records)
        self.assertEqual(result["completed_action_calls"], 1)
        self.assertFalse(result["m02_trace_coverage_ready"])

    def test_fixture_and_wrong_identity_cannot_pass_native_gate(self):
        records = fixture()
        for row in records:
            row.update(evidence_class="HOST_FIXTURE", executable_sha256="HOST_FIXTURE_NOT_GAME")
        with self.assertRaisesRegex(ValueError, "HOST_FIXTURE"): module.analyze(records, True)
        self.assertFalse(module.analyze(records)["m02_trace_coverage_ready"])
        for row in records: row["executable_sha256"] = module.EXE
        with self.assertRaises(ValueError): module.analyze(records)

    def test_loss_thread_errors_and_install_errors_prevent_coverage(self):
        for field in ("lost_records", "foreign_callbacks"):
            records = fixture()
            records[-1][field] = 1
            self.assertFalse(module.analyze(records)["m02_trace_coverage_ready"])
        records = fixture()
        records[1]["event"] = "hooks_failed"
        self.assertFalse(module.analyze(records)["m02_trace_coverage_ready"])

    def test_wrong_stage_or_hook_count_cannot_cover_campaign_probe(self):
        records = fixture()
        for row in records: row["mode_id"] = 0x1654C10  # Adventure is not the campaign fixture.
        self.assertFalse(module.analyze(records)["m02_trace_coverage_ready"])
        records = fixture(); records[1]["count"] = 2
        self.assertFalse(module.analyze(records)["m02_trace_coverage_ready"])

    def test_sequence_epochs_provenance_and_mixed_runs(self):
        for index, field, value in [(4, "sequence", 99), (5, "scene_epoch", 9), (4, "source", "mod"),
                                    (5, "pid", 200), (5, "sdk_commit", "other"), (5, "qpc", 1),
                                    (4, "multiplayer_mutations", True)]:
            with self.subTest(field=field):
                records = fixture(); records[index][field] = value
                with self.assertRaises(ValueError): module.analyze(records)

    def test_strict_json_numbers_and_no_pointer_fields(self):
        for field, value in [("health", float("nan")), ("entity_id", True), ("health", 100), ("pointer", "0x1234"),
                             ("health", 10**500), ("qpc", 2**100), ("event", {}), ("evidence_class", [])]:
            records = fixture(); records[4][field] = value
            with self.assertRaises(ValueError): module.analyze(records)

    def test_object_observed_inside_factory_can_later_have_one_creation_return(self):
        records = fixture()
        observed = copy.deepcopy(records[2]); observed.update(event="entity_observed", source="mod")
        records.insert(2, observed)
        for i, row in enumerate(records): row.update(sequence=i+1, qpc=i)
        self.assertTrue(module.analyze(records)["m02_trace_coverage_ready"])

    def test_missing_footer_and_trailing_records(self):
        with self.assertRaises(ValueError): module.analyze(fixture()[:-1])
        records = fixture()
        extra = copy.deepcopy(records[-1]); extra.update(sequence=10, qpc=10)
        with self.assertRaises(ValueError): module.analyze(records + [extra])

    def test_no_duplicate_keys_or_truncated_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            for text in ('{"event":"trace_start","event":"trace_stop"}\n', '{"event":"trace_start"}', '{"value":NaN}\n'):
                path.write_text(text, encoding="utf-8")
                with self.assertRaises(ValueError): list(module.read_records(path))

    def test_valid_file_reader(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            path.write_text("".join(json.dumps(row) + "\n" for row in fixture()), encoding="utf-8")
            self.assertEqual(module.analyze(module.read_records(path))["records"], 9)


if __name__ == "__main__":
    unittest.main()
