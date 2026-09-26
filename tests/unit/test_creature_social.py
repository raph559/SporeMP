"""HOST/FIXTURE evidence-integrity checks, without original-game execution."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("creature_social", ROOT / "tools/native/analyze-creature-social.py")
SOCIAL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SOCIAL)


def fixture():
    common = dict(pid=11, thread_id=12, qpc_frequency=1000, epoch=3,
                  executing_command=0, foreign_callbacks=0, evidence_class="HOST_FIXTURE",
                  harness="synthetic_social_trace", ability_scope=0, strike_scope=0,
                  animal_damage_scope=0)
    events = [
        dict(event="trace_start"),
        dict(event="harness_ready", combat_context_schema=1),
        dict(event="context_bindings_checked"),
        dict(event="native_global_dna_enter", call=1, avatar=2, amount=7.0,
             before=10.0, caller_rva=SOCIAL.SOCIAL_REWARD_CALLER_RVA),
        dict(event="native_global_dna_return", call=1, avatar=2, after=10.0),
        dict(event="trace_stop", healthy=True, detach_status=0),
    ]
    return [dict(common, **event, sequence=index, qpc=index * 10)
            for index, event in enumerate(events, 1)]


class CreatureSocialTests(unittest.TestCase):
    def inspect(self, rows):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            path.write_text("\n".join(json.dumps(row) for row in rows) + "\n", encoding="utf-8")
            return SOCIAL.analyze(path)

    def test_returned_call_does_not_claim_an_applied_reward_or_owner(self):
        report = self.inspect(fixture())
        self.assertTrue(report["structural_valid"])
        self.assertEqual(report["candidate_calls"], 1)
        self.assertEqual(report["observations"][0]["global_dna_delta_at_return"], 0)
        self.assertEqual(report["observations"][0]["ownership"], "NOT_ESTABLISHED")
        self.assertEqual(report["native_acceptance"], "NOT_VERIFIED")
        self.assertEqual(report["evidence_class"], "HOST_FIXTURE")

    def test_combat_reward_is_not_a_social_observation(self):
        rows = fixture()
        rows[3]["caller_rva"] = 0x807BF1
        report = self.inspect(rows)
        self.assertTrue(report["structural_valid"])
        self.assertEqual(report["candidate_calls"], 0)

    def test_invalid_evidence_is_never_reduced_to_usable_calls(self):
        mutations = [
            (4, "avatar", 3), (4, "epoch", 4), (4, "thread_id", 99),
            (4, "call", 2), (4, "qpc", 1), (5, "healthy", False),
            (3, "before", float("nan")), (4, "after", True),
            (3, "avatar", 0), (1, "combat_context_schema", 0),
        ]
        for index, key, value in mutations:
            with self.subTest(key=key, value=value):
                rows = fixture()
                rows[index][key] = value
                report = self.inspect(rows)
                self.assertFalse(report["structural_valid"])
                self.assertEqual(report["observations"], [])

    def test_unfinished_call_and_unclosed_trace_are_refused(self):
        rows = fixture()
        del rows[4]
        rows[-1]["sequence"] = 5
        self.assertFalse(self.inspect(rows)["structural_valid"])
        self.assertFalse(self.inspect(fixture()[:-1])["structural_valid"])

    def test_claimed_native_trace_needs_the_pinned_build(self):
        rows = fixture()
        for row in rows:
            row["evidence_class"] = "NATIVE_PROBE"
        rows[0]["executable_sha256"] = "0" * 64
        rows[0]["sdk_commit"] = SOCIAL._context.PINNED_SDK
        report = self.inspect(rows)
        self.assertFalse(report["structural_valid"])
        self.assertEqual(report["candidate_calls"], 0)


if __name__ == "__main__":
    unittest.main()
