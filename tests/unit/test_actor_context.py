import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / "tools/native/analyze-actor-context.py"
spec = importlib.util.spec_from_file_location("actor_context", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture():
    rows = []
    def add(event, **fields):
        row = dict(event=event, sequence=len(rows)+1, pid=10, thread_id=20, qpc=len(rows)+1,
                   qpc_frequency=1000, epoch=3, executing_command=0, foreign_callbacks=0,
                   ability_scope=0, strike_scope=0, animal_damage_scope=0,
                   evidence_class="HOST_FIXTURE", harness="M03")
        row.update(fields)
        rows.append(row)
    add("trace_start")
    add("context_bindings_checked", code_prefixes=4, virtual_slots=3)
    add("harness_ready", combat_context_schema=1)
    add("native_ability_enter", call=1, actor=2, owner=2, target=3, ability_scope=1)
    add("native_ability_select_enter", call=2, actor=2, ability_scope=1)
    add("native_ability_select_return", call=2, actor=2, result_index=5, ability_scope=1)
    add("ability_return_context", actor=2, owner=2, flags=1, native_player_owned=False, ability_scope=1)
    add("native_ability_return", call=1, actor=2, ability_scope=1)
    add("native_strike_enter", call=3, actor=2, owner=2, target=3, strike_scope=3)
    add("native_animal_damage_enter", call=4, receiver=3, attacker=2, strike_scope=3, animal_damage_scope=4)
    add("native_damage_enter", call=5, receiver=3, attacker=2, damage=1.0, receiver_health=6.0, strike_scope=3, animal_damage_scope=4)
    add("native_damage_return", call=5, receiver=3, attacker=2, result=0, strike_scope=3, animal_damage_scope=4)
    add("damage_state", actor=3, health=5.0, strike_scope=3, animal_damage_scope=4)
    add("native_animal_damage_return", call=4, receiver=3, attacker=2, result=0, strike_scope=3, animal_damage_scope=4)
    add("native_strike_return", call=3, actor=2, target=3, result=False, strike_scope=3)
    add("trace_stop", healthy=True, detach_status=0)
    return rows


class ActorContextTest(unittest.TestCase):
    def report(self, rows=None, raw=None):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "host-fixture.jsonl"
            path.write_text(raw if raw is not None else "\n".join(json.dumps(r) for r in rows), encoding="utf-8")
            return module.analyze(path)

    def test_nested_calls_preserve_asynchronous_identity_without_certifying_native(self):
        report = self.report(fixture())
        self.assertTrue(report["structural_valid"], report["errors"])
        self.assertTrue(report["context_observers_present"])
        self.assertEqual(report["actors"]["2"]["selection_returned_index"], 1)
        self.assertEqual(report["observed_actor_health_decreases"],
                         [dict(call=5, attacker=2, receiver=3, health_decrease=1.0, strike_scope=3, animal_damage_scope=4)])
        self.assertEqual(report["native_acceptance"], "NOT_VERIFIED")

    def test_marker_and_native_true_result_do_not_count_as_damage(self):
        rows = fixture()
        rows[6]["animation_marker"] = 0x746968
        rows[12]["health"] = 6.0
        rows[14]["result"] = True
        report = self.report(rows)
        self.assertTrue(report["structural_valid"])
        self.assertEqual(report["observed_actor_health_decreases"], [])
        self.assertEqual(report["actors"]["2"]["sampled_hit_markers"], 1)

    def test_cross_actor_cross_epoch_and_wrong_scope_are_rejected(self):
        for index, key, value in ((14, "target", 4), (11, "epoch", 6), (10, "strike_scope", 1), (13, "animal_damage_scope", 0)):
            with self.subTest(key=key):
                rows = fixture()
                rows[index][key] = value
                self.assertFalse(self.report(rows)["structural_valid"])

    def test_truncated_mixed_foreign_and_reused_calls_fail(self):
        for mutate in (lambda r: r.pop(), lambda r: r[5].update(pid=11),
                       lambda r: r[-1].update(foreign_callbacks=1), lambda r: r[4].update(call=1)):
            rows = fixture()
            mutate(rows)
            self.assertFalse(self.report(rows)["structural_valid"])

    def test_invalid_json_values_are_structural_failures(self):
        for raw in ('{"event":"a","event":"b"}', '{"event":NaN}', '[]', '{broken'):
            with self.subTest(raw=raw):
                self.assertFalse(self.report(raw=raw)["structural_valid"])
        rows = fixture()
        rows[10]["call"] = []
        self.assertFalse(self.report(rows)["structural_valid"])

    def test_legacy_trace_does_not_gain_context_coverage(self):
        rows = fixture()[:1] + fixture()[-1:]
        rows[-1]["sequence"] = 2
        report = self.report(rows)
        self.assertTrue(report["structural_valid"])
        self.assertFalse(report["context_observers_present"])

    def test_claimed_native_trace_requires_the_pinned_identity(self):
        rows = fixture()
        for row in rows:
            row["evidence_class"] = "NATIVE_PROBE"
        self.assertFalse(self.report(rows)["structural_valid"])
        rows[0].update(executable_sha256=module.PINNED_EXE, sdk_commit=module.PINNED_SDK)
        report = self.report(rows)
        self.assertTrue(report["structural_valid"])
        self.assertEqual(report["native_acceptance"], "NOT_VERIFIED")


if __name__ == "__main__":
    unittest.main()
