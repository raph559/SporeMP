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


def with_action(secondary=True):
    rows = fixture()
    rows[1]["social_context_schema"] = 1
    common = dict(rows[0])
    common.update(social_call=1, action=SOCIAL.SOCIAL_ACTION,
                  caller_rva=SOCIAL.SOCIAL_ACTION_CALLER_RVA, resolved=True,
                  payload_actor_native_id=101, payload_target_native_id=202,
                  primary_player_native_id=303, secondary_present=secondary,
                  secondary_player_native_id=404 if secondary else 0xFFFFFFFF,
                  primary_dna=10.0, primary_goal=0.0, primary_combat=0.0, primary_social=0.0,
                  secondary_dna=8.0 if secondary else 0.0,
                  secondary_goal=0.0, secondary_combat=0.0, secondary_social=0.0)
    rows[5:5] = [dict(common, event="native_social_action_enter"),
                 dict(common, event="native_social_action_return", primary_social=1.0)]
    for index, row in enumerate(rows, 1):
        row.update(sequence=index, qpc=index * 10)
    return rows


def with_owned_result():
    rows = with_action()
    rows[1]["owned_social_result_schema"] = 1
    action = dict(rows[5], primary_social=0.0, secondary_dna=15.0, secondary_goal=7.0, secondary_social=7.0)
    common = dict(rows[0], result_call=1, resolved=True, owner=2, actor=2,
                  actor_native_id=101, target_native_id=202, primary_player_native_id=303,
                  secondary_player_native_id=404, animation=0xFFFFFFFF,
                  primary_dna=10.0, primary_goal=0.0, primary_combat=0.0, primary_social=0.0,
                  secondary_dna=8.0, secondary_goal=0.0, secondary_combat=0.0, secondary_social=0.0)
    child = dict(rows[0], owner=2, actor=2, actor_native_id=101, target_native_id=202)
    result = rows[:3] + [
        dict(common, event="native_social_owner_enter"),
        dict(child, event="native_owned_social_reward_applied", amount=7.0),
        dict(action, event="native_social_action_enter"),
        dict(child, event="native_owned_social_action_applied", amount=0.0),
        dict(action, event="native_social_action_return"),
        dict(common, event="native_social_owner_return", animation=268,
             secondary_dna=15.0, secondary_goal=7.0, secondary_social=7.0), rows[-1]]
    for index, row in enumerate(result, 1):
        row.update(sequence=index, qpc=index * 10)
    return result


def with_objective():
    rows = with_owned_result()
    common = dict(rows[8], call=1, recent_result=1, caller_rva=0x82FBD4,
                  vtable_rva=0x106B3C0, species=[101,202,303])
    field = "fields_7c_8c_90_94_98_9c_a0_a4_a8_b0_bc_c0"
    before = [3,2,0,25,20,0,4,1,1,3,3,1]
    after = [5,3,0,25,20,25,6,1,1,3,3,1]
    rows[-1:-1] = [
        dict(common, event="native_social_relationship_transition", before_valid=True, after_valid=True,
             count_before=2, count_after=3, relation_before=4, relation_after=6, objective_receipt_pending=True),
        dict(common, event="native_social_objective_enter", **{field:before}),
        dict(common, event="native_social_objective_owner_claimed", count_before=2, count_after=3, one_shot=True),
        dict(common, event="native_owned_social_objective_award", amount=25),
        dict(common, event="native_social_objective_return", secondary_dna=40.0, secondary_goal=32.0,
             secondary_social=32.0, **{field:after}),
    ]
    for index, row in enumerate(rows, 1):
        row.update(sequence=index, qpc=index*10)
    return rows


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

    def test_action_snapshots_report_both_players_without_claiming_ownership(self):
        report = self.inspect(with_action())
        self.assertTrue(report["structural_valid"], report["errors"])
        self.assertEqual(report["action_calls"], 1)
        action = report["action_observations"][0]
        self.assertEqual(action["immediate_delta"]["primary_social"], 1.0)
        self.assertEqual(action["immediate_delta"]["primary_dna"], 0.0)
        self.assertEqual(action["immediate_delta"]["secondary_dna"], 0.0)
        self.assertEqual(action["ownership"], "NOT_ESTABLISHED")
        self.assertEqual(report["native_acceptance"], "NOT_VERIFIED")

    def test_primary_only_baseline_does_not_invent_a_secondary_player(self):
        report = self.inspect(with_action(secondary=False))
        self.assertTrue(report["structural_valid"], report["errors"])
        self.assertFalse(report["action_observations"][0]["secondary_present"])
        self.assertEqual(report["action_observations"][0]["secondary_player_native_id"], 0xFFFFFFFF)

    def test_action_identity_lifetime_and_scalar_failures_discard_all_observations(self):
        mutations = [
            (1, "social_context_schema", True), (5, "social_call", True),
            (5, "caller_rva", SOCIAL.SOCIAL_ACTION_CALLER_RVA + 1),
            (5, "action", SOCIAL.SOCIAL_ACTION + 1), (5, "resolved", False),
            (5, "secondary_present", 1), (5, "primary_dna", True),
            (5, "payload_target_native_id", 0xFFFFFFFF),
            (5, "payload_target_native_id", 101), (5, "primary_player_native_id", 404),
            (6, "primary_player_native_id", 505), (6, "payload_actor_native_id", 909),
            (6, "epoch", 4), (6, "secondary_present", False), (6, "social_call", 2),
            (6, "executing_command", 999), (5, "primary_dna", 10 ** 400),
        ]
        for index, key, value in mutations:
            with self.subTest(key=key, value=value):
                rows = with_action()
                rows[index][key] = value
                report = self.inspect(rows)
                self.assertFalse(report["structural_valid"])
                self.assertEqual(report["action_observations"], [])
                self.assertEqual(report["observations"], [])

    def test_lost_action_return_and_reused_call_are_not_usable_evidence(self):
        rows = with_action()
        del rows[6]
        rows[-1]["sequence"] -= 1
        self.assertFalse(self.inspect(rows)["structural_valid"])
        rows = with_action()
        rows[7:7] = [dict(rows[5]), dict(rows[6])]
        for index, row in enumerate(rows, 1):
            row.update(sequence=index, qpc=index * 10)
        self.assertFalse(self.inspect(rows)["structural_valid"])

    def test_absent_secondary_cannot_report_real_progress(self):
        rows = with_action(secondary=False)
        rows[5]["secondary_dna"] = 8.0
        self.assertFalse(self.inspect(rows)["structural_valid"])

    def test_whole_result_measures_progress_but_cannot_certify_native_acceptance(self):
        report = self.inspect(with_owned_result())
        self.assertTrue(report["structural_valid"], report["errors"])
        self.assertEqual(report["owned_result_calls"], 1)
        self.assertEqual(report["owned_progress_isolated"], 1)
        self.assertEqual(report["owned_results"][0]["immediate_delta"]["secondary_social"], 7.0)
        self.assertEqual(report["native_acceptance"], "NOT_VERIFIED")

    def test_pre_action_primary_leak_fails_whole_result_even_with_unchanged_action(self):
        rows = with_owned_result()
        for index in (5, 7, 8):
            rows[index]["primary_social"] = 7.0
        report = self.inspect(rows)
        self.assertTrue(report["structural_valid"], report["errors"])
        self.assertEqual(report["action_observations"][0]["immediate_delta"]["primary_social"], 0)
        self.assertEqual(report["owned_progress_isolated"], 0)
        self.assertEqual(report["owned_results"][0]["immediate_delta"]["primary_social"], 7.0)

    def test_result_roots_scope_schema_and_nonfinite_values_cannot_supply_evidence(self):
        mutations = [(1, "owned_social_result_schema", True), (3, "result_call", True),
                     (3, "owner", 1), (3, "actor", 0), (3, "animation", 1),
                     (3, "primary_dna", float("inf")), (8, "epoch", 4),
                     (8, "actor_native_id", 909), (8, "result_call", 2),
                     (8, "executing_command", 1), (8, "secondary_social", True),
                     (4, "actor", 1), (4, "target_native_id", 909), (4, "amount", -1),
                     (6, "actor_native_id", True), (8, "resolved", False)]
        for index, key, value in mutations:
            with self.subTest(key=key, value=value):
                rows = with_owned_result()
                rows[index][key] = value
                report = self.inspect(rows)
                self.assertFalse(report["structural_valid"])
                self.assertEqual(report["owned_results"], [])
                self.assertEqual(report["action_observations"], [])

    def test_duplicate_missing_or_zero_reward_does_not_qualify_positive_progress(self):
        for alteration in ("duplicate", "missing", "zero", "mismatch"):
            rows = with_owned_result()
            if alteration == "duplicate":
                rows.insert(5, dict(rows[4]))
            elif alteration == "missing":
                del rows[4]
            else:
                rows[4]["amount"] = 0 if alteration == "zero" else 9
            for index, row in enumerate(rows, 1):
                row.update(sequence=index, qpc=index * 10)
            report = self.inspect(rows)
            self.assertTrue(report["structural_valid"], report["errors"])
            self.assertEqual(report["owned_progress_isolated"], 0)

    def test_orphan_and_unfinished_owned_results_are_not_usable(self):
        for removed in (3, 8):
            rows = with_owned_result()
            del rows[removed]
            for index, row in enumerate(rows, 1):
                row.update(sequence=index, qpc=index * 10)
            report = self.inspect(rows)
            self.assertFalse(report["structural_valid"])
            self.assertEqual(report["owned_results"], [])

    def test_delayed_objective_has_its_own_transition_and_isolation_result(self):
        report = self.inspect(with_objective())
        self.assertTrue(report["structural_valid"], report["errors"])
        self.assertEqual(report["owned_progress_isolated"], 1)
        self.assertEqual(report["objective_progress_isolated"], 1)
        self.assertEqual(report["native_acceptance"], "NOT_VERIFIED")

    def test_delayed_primary_leak_is_visible_even_when_individual_result_passes(self):
        rows = with_objective()
        end = next(r for r in rows if r["event"] == "native_social_objective_return")
        end.update(primary_dna=35.0, primary_goal=25.0, primary_social=25.0,
                   secondary_dna=15.0, secondary_goal=7.0, secondary_social=7.0)
        report = self.inspect(rows)
        self.assertTrue(report["structural_valid"], report["errors"])
        self.assertEqual(report["owned_progress_isolated"], 1)
        self.assertEqual(report["objective_progress_isolated"], 0)
        self.assertEqual(report["objectives"][0]["immediate_delta"]["primary_dna"], 25)

    def test_objective_requires_exact_transition_and_one_claim_and_award(self):
        for kind, key, value in [("native_social_relationship_transition", "count_after", 4),
                ("native_social_relationship_transition", "before_valid", False),
                ("native_social_objective_owner_claimed", "one_shot", False),
                ("native_owned_social_objective_award", "amount", 0)]:
            with self.subTest(kind=kind,key=key):
                rows = with_objective()
                next(r for r in rows if r["event"] == kind)[key] = value
                report = self.inspect(rows)
                self.assertTrue(report["structural_valid"], report["errors"])
                self.assertEqual(report["objective_progress_isolated"], 0)
        rows = with_objective()
        index = next(i for i,r in enumerate(rows) if r["event"] == "native_owned_social_objective_award")
        rows.insert(index,dict(rows[index]))
        for index,row in enumerate(rows,1):row.update(sequence=index,qpc=index*10)
        self.assertEqual(self.inspect(rows)["objective_progress_isolated"], 0)

    def test_objective_identity_lifetime_and_malformed_data_discard_evidence(self):
        for kind,key,value in [("native_social_objective_return","actor",99),
                ("native_social_objective_return","recent_result",2),
                ("native_social_objective_enter","species",[1,2]),
                ("native_social_objective_enter","vtable_rva",0),
                ("native_social_objective_return","secondary_dna",True),
                ("native_social_relationship_transition","actor",[]),
                ("native_social_objective_owner_claimed","call",True)]:
            with self.subTest(kind=kind,key=key):
                rows=with_objective();next(r for r in rows if r["event"]==kind)[key]=value
                report=self.inspect(rows)
                self.assertFalse(report["structural_valid"])
                self.assertEqual(report["objectives"],[])
                self.assertEqual(report["owned_results"],[])
        rows=with_objective()
        rows=[r for r in rows if r["event"]!='native_social_objective_return']
        for index,row in enumerate(rows,1):row.update(sequence=index,qpc=index*10)
        self.assertFalse(self.inspect(rows)["structural_valid"])


if __name__ == "__main__":
    unittest.main()
