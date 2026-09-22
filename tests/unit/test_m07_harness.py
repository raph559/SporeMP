"""Synthetic parser fixtures only; never import a native acceptance conclusion."""
import importlib.util
from copy import deepcopy
from pathlib import Path
from threading import Barrier
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("m07_harness_test", ROOT / "tools/native/m07-harness.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def trace(events):
    return [dict(sequence=i, event=name, **fields) for i, (name, fields) in enumerate(events, 1)]


def fixture():
    marker = {"client": "02", "owner": 1, "client_before_sequence": 0, "authority_before_sequence": 0}
    target = {"remote_entity": 77, "entity_generation": 5, "native_id": 44}
    client = trace([
        ("network_intention", dict(request=7, player=1, entity=66, verb=6, target=77, target_generation=5)),
        ("network_action_result", dict(request=7, error="none")),
    ])
    authority = trace([
        ("network_actor_command_queued", dict(request=7, command=30, actor=1, owner=1, verb="engage", target=3, target_native_id=44, decision="accepted")),
        ("network_native_intention", dict(request=7, player=1, entity=66, target=77, target_generation=5, queued=True)),
        ("command_begin", dict(command=30, actor=1, owner=1, target=3)),
        ("native_target_enter", dict(executing_command=30, actor=1, target=3, target_native_id=44)),
        ("native_animal_damage_enter", dict(call=41, attacker=1, receiver=3)),
        ("native_damage_enter", dict(call=42, attacker=1, receiver=3, receiver_health=2.0, animal_damage_scope=41)),
        ("native_damage_return", dict(call=42, attacker=1, receiver=3, animal_damage_scope=41)),
        ("damage_state", dict(actor=3, health=1.0, animal_damage_scope=41)),
        ("native_animal_damage_return", dict(call=41, attacker=1, receiver=3)),
    ])
    return marker, target, client, authority


def sample(event, entity, owner, dna=0.0, dead=False, generation=5, native_observed=True):
    return event, dict(remote_entity=entity, entity_generation=generation, native_id=entity+10, owner=owner,
                       health=0.0 if dead else 10.0, hunger=50.0, dead=dead, source_dna=dna,
                       native_dna=dna if native_observed else None, dna_native_observed=native_observed,
                       is_native_avatar=bool(owner), source_tick=1000, position=[1.0, 2.0, 3.0])


def unauthorized_fixture():
    marker = dict(client_before_sequence=0, authority_before_sequence=0, foreign_entity=67, foreign_generation=5)
    client = trace([
        ("network_intention", dict(request=8, player=1, entity=67, verb=1, target=0, target_generation=0)),
        ("network_rejected", dict(request=8, player=1, entity=67, error="wrong_actor_owner")),
    ])
    common = dict(event=1, connection=12, request=8, player=1, entity=67, generation=5, target=0, target_generation=0)
    # Coordinator dispatch is logged before the enclosing incoming packet record.
    coordinator = [dict(common, outbound=True, kind=3, error="wrong_actor_owner", detail="dispatch_queued"),
                   dict(common, outbound=False, kind=10, verb=1, error="none", detail="")]
    flags = dict(other_connected=True, prefixes_match=True, observation_complete=True)
    return marker, client, coordinator, flags


def pickup_fixture(winner=2, dna_amount=2.0):
    before, rows = {}, {}
    def samples(wid, fed=False):
        name = "scene_native_authority_sample" if wid == "01" else "scene_native_replica_sample"
        entries = [sample(name, 77, 0, dead=True), sample(name, 66, 1, dna_amount if fed and winner == 1 else 0.0),
                   sample(name, 67, 2, 7.0+dna_amount if fed and winner == 2 else 7.0)]
        entries[0][1].update(source_fed_on=int(fed), native_fed_on=int(fed), source_food=90.0 if fed else 100.0,
                             native_food=90.0 if fed else 100.0, source_pickup_owner=winner if fed else 0)
        for _, data in entries: data["source_tick"] = 1100 if fed else 1000
        return entries
    for wid in ("01", "02", "03"):
        before[wid] = trace(samples(wid))
        for r in before[wid]: r.update(qpc=r["sequence"]*10, qpc_frequency=100)
        rows[wid] = deepcopy(before[wid])
    def append(wid, name, **fields):
        sequence = len(rows[wid])+1
        rows[wid].append(dict(sequence=sequence, qpc=sequence*10, qpc_frequency=100, event=name, **fields))
    markers = [dict(client=wid, owner=owner, client_before_sequence=3, authority_before_sequence=3)
               for wid, owner in (("02", 1), ("03", 2))]
    for marker in markers:
        owner, wid = marker["owner"], marker["client"]
        append(wid, "network_intention", request=7, player=owner, entity=65+owner, verb=7, target=77, target_generation=5)
        append(wid, "network_action_result", request=7, error="none")
        append("01", "network_actor_command_queued", request=7, command=30+owner, actor=owner, owner=owner, verb="pickup", target=3, target_native_id=87, decision="accepted")
        append("01", "network_native_intention", request=7, player=owner, entity=65+owner, target=77, target_generation=5, queued=True)
        append("01", "command_begin", command=30+owner, actor=owner, owner=owner, target=3)
        append("01", "native_pickup_order_return", executing_command=30+owner, actor=owner, owner=owner, target=3, ordered=True, accepted_is_queued=True)
    common = dict(pickup_scope=80, actor=winner, owner=winner)
    body = dict(actor_native_id=75+winner, claimant_native_id=75+winner, claimant_present=True, claimed_by_actor=True, health=10.0)
    append("01", "native_pickup_tick_enter", **common, **body, corpse_native_id=87, fed_on=False, food=100.0, hunger=50.0)
    if winner == 2:
        append("01", "native_owned_pickup_reward_applied", **common, corpse_native_id=87, reward_kind="corpse_first_feed",
               caller_rva=module.PICKUP_REWARD_CALLER_RVA, amount=dna_amount, a_dna_before=0.0, a_dna_after=0.0, b_dna_before=7.0, b_dna_after=7.0+dna_amount)
    else:
        append("01", "native_global_dna_enter", pickup_scope=80, call=90, avatar=1, before=0.0, amount=dna_amount, caller_rva=module.PICKUP_REWARD_CALLER_RVA)
        append("01", "native_global_dna_return", pickup_scope=80, call=90, avatar=1, after=dna_amount)
    append("01", "native_pickup_tick_return_state", **common, **body, corpse_native_id=87, fed_on=True, food=90.0, hunger=60.0)
    append("01", "native_pickup_first_feed_observed", **common, corpse_native_id=87, food_before=100.0, food_after=90.0, remaining_food_is_separate=True)
    append("01", "native_pickup_tick_return", **common, result=False, state_observed=True, original_reward_return_observed=True)
    for wid in rows:
        for name, fields in samples(wid, True): append(wid, name, **fields)
    return rows, markers, before["01"][0], before


def pickup_result(rows, markers, target, before, **flags):
    return module.summarize_pickup(rows, markers, target, before,
        **dict(dict(observation_complete=True, prefixes_match=True, endpoints_connected=True), **flags))


def retired_pickup_fixture(winner=1):
    rows, markers, target, before = pickup_fixture(winner, 0.0)
    for collection in (before, rows):
        for worker in collection.values():
            for row in worker:
                row["epoch"] = 3
                if row.get("remote_entity") == 77:
                    row["native_pool_cycle"] = 0
                    if row.get("source_fed_on") == 1:
                        row.update(source_food=-0.25, native_food=-0.25)
    def append(wid, name, **fields):
        rows[wid].append(dict(sequence=len(rows[wid])+1, event=name, epoch=3, **fields))
    append("01", "native_pool_return_observed", native_id=87, counter_before=0, counter_after=1, enabled=False, herd_present=False)
    append("01", "native_pickup_grant_retired", corpse_native_id=87, owner=winner, reason="native_pool_return")
    append("01", "network_despawn_published", entity=77)
    for wid in ("02", "03"):
        append(wid, "scene_replica_despawn", remote_entity=77, native_id=87, reason="authoritative_despawn")
    for wid in rows:
        for owner in (1, 2):
            previous = next(r for r in reversed(rows[wid]) if r.get("owner") == owner and "source_dna" in r)
            rows[wid].append(dict(previous, sequence=len(rows[wid])+1, source_tick=1200))
    return rows, markers, target, before


def event(rows, name):
    return next(r for r in rows["01"] if r["event"] == name)


class HarnessTests(unittest.TestCase):
    def test_depleted_corpse_original_retirement_keeps_owned_balance_evidence(self):
        for winner in (1, 2):
            with self.subTest(winner=winner):
                result = pickup_result(*retired_pickup_fixture(winner))
                self.assertTrue(result["bounded_contested_pickup_observed"])
                self.assertTrue(result["terminal_retirement"]["verified"])
                self.assertEqual(result["pickup_projection_outcome"], "retired_after_projection")
                self.assertFalse(result["corpse_currently_projected_on_all_three"])
                self.assertEqual(result["shared_state"]["normalized"], {})
                self.assertEqual(result["unavailable_corpse_workers"], ["01", "02", "03"])
                self.assertEqual(result["shared_state"]["published_owner_balances"]["03"]["2"]["dna"], 7.0)
                self.assertEqual(result["observed_gaps"], [])

    def test_retirement_requires_each_original_and_replica_lifecycle_link(self):
        defects = ("missing_pool", "missing_grant", "missing_despawn", "missing_a_removal", "missing_b_removal",
                   "same_counter", "wrong_counter", "active_pool", "herd_present", "wrong_grant_owner", "destroy_entry_only",
                   "wrong_order", "wrong_client_reason", "wrong_client_native_id", "wrong_epoch", "not_depleted")
        for defect in defects:
            rows, markers, target, before = retired_pickup_fixture()
            if defect.startswith("missing_"):
                wid, name = {"missing_pool": ("01", "native_pool_return_observed"),
                    "missing_grant": ("01", "native_pickup_grant_retired"), "missing_despawn": ("01", "network_despawn_published"),
                    "missing_a_removal": ("02", "scene_replica_despawn"), "missing_b_removal": ("03", "scene_replica_despawn")}[defect]
                rows[wid] = [r for r in rows[wid] if r["event"] != name]
            elif defect in ("same_counter", "wrong_counter", "active_pool", "herd_present"):
                field, value = {"same_counter": ("counter_after", 0), "wrong_counter": ("counter_before", 2),
                    "active_pool": ("enabled", True), "herd_present": ("herd_present", True)}[defect]
                event(rows, "native_pool_return_observed")[field] = value
            elif defect == "wrong_grant_owner": event(rows, "native_pickup_grant_retired")["owner"] = 2
            elif defect == "destroy_entry_only": event(rows, "native_pickup_grant_retired")["reason"] = "destroy"
            elif defect == "wrong_order":
                event(rows, "native_pickup_grant_retired")["sequence"] = event(rows, "native_pool_return_observed")["sequence"]-1
            elif defect == "not_depleted":
                for worker in rows.values():
                    for r in worker:
                        if r.get("source_fed_on") == 1: r.update(source_food=50.0, native_food=50.0)
            else:
                removal = next(r for r in rows["03"] if r["event"] == "scene_replica_despawn")
                field, value = {"wrong_client_reason": ("reason", "baseline_extra_noun"),
                    "wrong_client_native_id": ("native_id", 999), "wrong_epoch": ("epoch", 4)}[defect]
                removal[field] = value
            with self.subTest(defect=defect):
                self.assertFalse(pickup_result(rows, markers, target, before)["bounded_contested_pickup_observed"])

    def test_retirement_first_feed_claim_can_precede_original_nutrition_tick(self):
        rows, markers, target, before = retired_pickup_fixture()
        event(rows, "native_pickup_tick_return_state").update(food=100.0, hunger=50.0)
        event(rows, "native_pickup_first_feed_observed")["food_after"] = 100.0
        self.assertFalse(pickup_result(rows, markers, target, before)["bounded_contested_pickup_observed"])
        body = dict(pickup_scope=81, actor=1, owner=1, actor_native_id=76, claimant_native_id=76,
                    claimant_present=True, claimed_by_actor=True, corpse_native_id=87, fed_on=True, health=10.0, epoch=3)
        callback = [dict(event="native_pickup_tick_enter", **body, food=100.0, hunger=50.0),
                    dict(event="native_pickup_tick_return_state", **body, food=90.0, hunger=60.0),
                    dict(event="native_pickup_tick_return", pickup_scope=81, actor=1, owner=1,
                         result=True, state_observed=True, original_reward_return_observed=False, epoch=3)]
        index = rows["01"].index(event(rows, "native_pickup_tick_return"))+1
        rows["01"][index:index] = callback
        for sequence, row in enumerate(rows["01"], 1): row["sequence"] = sequence
        result = pickup_result(rows, markers, target, before)
        self.assertTrue(result["bounded_contested_pickup_observed"])
        self.assertEqual([t["scope"] for t in result["native_nutrition_transfers"]], [81])

    def test_retirement_requires_exact_prior_generation_projections_and_fresh_owners(self):
        for defect in ("wrong_generation", "missing_prior_projection", "stale_owner", "changed_balance", "changed_owner_generation", "failure", "reappeared"):
            rows, markers, target, before = retired_pickup_fixture()
            if defect in ("wrong_generation", "missing_prior_projection"):
                projected = next(r for r in rows["03"] if r.get("source_fed_on") == 1)
                projected["entity_generation" if defect == "wrong_generation" else "source_tick"] = 999
            elif defect == "stale_owner": rows["03"].pop()
            elif defect in ("changed_balance", "changed_owner_generation"):
                field, value = ("source_dna", 9.0) if defect == "changed_balance" else ("entity_generation", 6)
                rows["03"][-1][field] = value
                if defect == "changed_balance": rows["03"][-1]["native_dna"] = value
            elif defect == "failure": rows["03"].append(dict(sequence=len(rows["03"])+1, event="scene_projection_rejected"))
            else:
                old = next(r for r in rows["03"] if r.get("source_fed_on") == 1)
                rows["03"].append(dict(old, sequence=len(rows["03"])+1, entity_generation=6))
            with self.subTest(defect=defect):
                self.assertFalse(pickup_result(rows, markers, target, before)["bounded_contested_pickup_observed"])

    def test_zero_dna_a_and_b_require_real_claimed_nutrition(self):
        for winner in (1, 2):
            rows, markers, target, before = pickup_fixture(winner, 0.0)
            result = pickup_result(rows, markers, target, before)
            self.assertTrue(result["bounded_contested_pickup_observed"])
            self.assertTrue(result["native_dna_zero_amount"])
            self.assertEqual(result["native_first_feed_dna_calls"][0]["observed_delta"], 0.0)
            self.assertTrue(result["positive_native_food_consumption_observed"])
            self.assertEqual(result["native_nutrition_transfers"][0]["food_used"], 10.0)

    def test_zero_dna_flag_alone_food_alone_or_unclaimed_transfer_never_passes(self):
        for defect in ("no_nutrition", "no_food", "wrong_claimant", "unclaimed", "negative_amount", "dna_changed_on_zero"):
            rows, markers, target, before = pickup_fixture(2, 0.0)
            after = event(rows, "native_pickup_tick_return_state")
            if defect == "no_nutrition": after["hunger"] = 50.0
            elif defect == "no_food":
                after["food"] = 100.0
                event(rows, "native_pickup_first_feed_observed")["food_after"] = 100.0
            elif defect == "wrong_claimant": after["claimant_native_id"] = 76
            elif defect == "unclaimed": after["claimed_by_actor"] = False
            elif defect == "negative_amount": event(rows, "native_owned_pickup_reward_applied")["amount"] = -1.0
            else: event(rows, "native_owned_pickup_reward_applied")["b_dna_after"] = 8.0
            with self.subTest(defect=defect):
                self.assertFalse(pickup_result(rows, markers, target, before)["bounded_contested_pickup_observed"])

    def test_positive_native_dna_amount_may_be_capped_but_nutrition_is_still_required(self):
        rows, markers, target, before = pickup_fixture(2, 0.0)
        event(rows, "native_owned_pickup_reward_applied")["amount"] = 2.0
        result = pickup_result(rows, markers, target, before)
        self.assertTrue(result["bounded_contested_pickup_observed"])
        self.assertFalse(result["native_dna_zero_amount"])
        self.assertEqual(result["native_first_feed_dna_calls"][0]["observed_delta"], 0.0)

    def test_zero_dna_requires_post_feed_native_balance_readbacks(self):
        rows, markers, target, before = pickup_fixture(2, 0.0)
        rows["03"][-1]["source_tick"] = 1000
        result = pickup_result(rows, markers, target, before)
        self.assertFalse(result["bounded_contested_pickup_observed"])
        self.assertFalse(result["native_dna_readback_after_first_feed"])

    def test_losing_owner_food_or_nutrition_transfer_blocks_single_consumer(self):
        for defect in ("food_callback", "unattributed_hunger", "unattributed_health"):
            rows, markers, target, before = pickup_fixture(2, 0.0)
            if defect == "food_callback":
                body = dict(pickup_scope=81, actor=1, owner=1, actor_native_id=76, claimant_native_id=76,
                            claimant_present=True, claimed_by_actor=True, corpse_native_id=87, fed_on=True, health=10.0)
                insertion = [dict(event="native_pickup_tick_enter", **body, food=90.0, hunger=50.0),
                             dict(event="native_pickup_tick_return_state", **body, food=80.0, hunger=60.0),
                             dict(event="native_pickup_tick_return", pickup_scope=81, actor=1, owner=1, state_observed=True,
                                  result=True, original_reward_return_observed=False)]
                rows["01"][-3:-3] = insertion
                for i, r in enumerate(rows["01"], 1): r["sequence"] = i
            else: rows["01"][-2]["hunger" if defect == "unattributed_hunger" else "health"] += 1.0
            result = pickup_result(rows, markers, target, before)
            self.assertFalse(result["bounded_contested_pickup_observed"])
            self.assertFalse(result["positive_native_food_consumption_observed"])

    def test_continuous_food_uses_exact_revision_pairs_not_unrelated_latest_values(self):
        rows, markers, target, before = pickup_fixture(2, 0.0)
        # A observes revision1100/food90; B observes1200/food80. Both are exact
        # original revisions, even though the latest three-way digest differs.
        source = dict(rows["01"][-3], event="scene_native_pickup_state_published", sequence=len(rows["01"])+1,
                      source_tick=1200, source_food=80.0, native_food=80.0)
        rows["01"].append(source)
        rows["03"][-3].update(event="scene_native_pickup_state_applied", source_tick=1200, source_food=80.0, native_food=80.0)
        result = pickup_result(rows, markers, target, before)
        self.assertFalse(result["shared_state"]["sampled_shared_state_agreement"])
        self.assertTrue(result["bounded_contested_pickup_observed"])
        self.assertEqual(result["exact_revision_pickup_pairs"]["02"]["source_tick"], 1100)
        self.assertEqual(result["exact_revision_pickup_pairs"]["03"]["source_tick"], 1200)
        self.assertEqual(module.scene(rows["01"], True)[77]["source_food"], 80.0)

    def test_exact_food_pair_never_rounds_or_reuses_earlier_matching_revision(self):
        for defect in ("different_tick", "small_food_difference"):
            rows, markers, target, before = pickup_fixture(2, 0.0)
            local = dict(rows["03"][-3], sequence=len(rows["03"])+1, event="scene_native_pickup_state_applied")
            if defect == "different_tick": local["source_tick"] = 1101
            else: local.update(source_food=90.000001, native_food=90.000001)
            rows["03"].append(local)
            result = pickup_result(rows, markers, target, before)
            self.assertFalse(result["bounded_contested_pickup_observed"])
            self.assertNotIn("03", result["exact_revision_pickup_pairs"])

    def test_exact_settled_negative_food_preserves_original_native_value(self):
        rows, markers, target, before = pickup_fixture(2, 0.0)
        for worker in rows.values():
            worker[-3].update(source_food=-0.25, native_food=-0.25)
        result = pickup_result(rows, markers, target, before)
        self.assertTrue(result["bounded_contested_pickup_observed"])
        self.assertEqual(result["exact_revision_pickup_pairs"]["03"]["food"], -0.25)

    def test_pickup_requires_original_orders_transition_grant_and_three_native_projections(self):
        for winner in (1, 2):
            with self.subTest(winner=winner):
                result = pickup_result(*pickup_fixture(winner))
                self.assertTrue(result["bounded_contested_pickup_observed"])
                self.assertEqual(result["winner"], winner)
                self.assertEqual(result["native_first_feed_dna_calls"][0]["observed_delta"], 2.0)
                self.assertEqual(result["shared_state"]["normalized"]["03"]["state"]["npc"]["pickup_owner"], winner)
                self.assertEqual(result["full_m07"], "NOT_VERIFIED")

    def test_pickup_queue_or_order_alone_cannot_claim_original_feeding(self):
        for retain_orders in (False, True):
            rows, markers, target, before = pickup_fixture()
            rows["01"] = [r for r in rows["01"] if not r["event"].startswith("native_pickup_tick") and
                r["event"] not in ("native_pickup_first_feed_observed", "native_owned_pickup_reward_applied") and
                (retain_orders or r["event"] != "native_pickup_order_return")]
            result = pickup_result(rows, markers, target, before)
            self.assertEqual(result["both_original_orders_returned"], retain_orders)
            self.assertFalse(result["bounded_contested_pickup_observed"])

    def test_pickup_both_original_orders_must_precede_winning_tick(self):
        rows, markers, target, before = pickup_fixture()
        order = next(r for r in rows["01"] if r["event"] == "native_pickup_order_return" and r["owner"] == 2)
        rows["01"].remove(order)
        rows["01"].insert(rows["01"].index(event(rows, "native_owned_pickup_reward_applied")), order)
        for i, r in enumerate(rows["01"], 1): r["sequence"] = i
        result = pickup_result(rows, markers, target, before)
        self.assertTrue(result["both_original_orders_returned"])
        self.assertTrue(result["single_original_first_feed_call_observed"])
        self.assertFalse(result["both_original_orders_before_first_feed"])
        self.assertFalse(result["bounded_contested_pickup_observed"])

    def test_pickup_wrong_identity_scope_return_or_owner_leak_cannot_pass(self):
        defects = (("network_native_intention", "target_generation", 6),
                   ("network_actor_command_queued", "owner", 2),
                   ("native_pickup_order_return", "executing_command", 99),
                   ("native_pickup_tick_enter", "corpse_native_id", 88),
                   ("native_pickup_tick_return_state", "pickup_scope", 81),
                   ("native_pickup_tick_return", "state_observed", False),
                   ("native_pickup_tick_return", "original_reward_return_observed", False),
                   ("native_owned_pickup_reward_applied", "owner", 1),
                   ("native_owned_pickup_reward_applied", "caller_rva", 0x946cd3),
                   ("native_owned_pickup_reward_applied", "a_dna_after", 2.0),
                   ("native_pickup_first_feed_observed", "actor", 1))
        for name, field, value in defects:
            rows, markers, target, before = pickup_fixture()
            event(rows, name)[field] = value
            with self.subTest(event=name, field=field):
                if name == "network_actor_command_queued":
                    with self.assertRaisesRegex(ValueError, "Ambiguous"):
                        pickup_result(rows, markers, target, before)
                else:
                    self.assertFalse(pickup_result(rows, markers, target, before)["bounded_contested_pickup_observed"])

    def test_pickup_a_reward_without_matching_return_is_unknown(self):
        for defect in ("missing", "scope", "avatar"):
            rows, markers, target, before = pickup_fixture(1)
            returned = event(rows, "native_global_dna_return")
            if defect == "missing": rows["01"].remove(returned)
            elif defect == "scope": returned["pickup_scope"] = 81
            else: returned["avatar"] = 2
            result = pickup_result(rows, markers, target, before)
            self.assertFalse(result["bounded_contested_pickup_observed"])
            self.assertTrue(result["unresolved_native_outcomes"])

    def test_pickup_duplicate_first_feed_or_reward_never_passes(self):
        for name in ("native_pickup_first_feed_observed", "native_owned_pickup_reward_applied"):
            rows, markers, target, before = pickup_fixture()
            original = event(rows, name)
            rows["01"].insert(rows["01"].index(original)+1, dict(original))
            for i, row in enumerate(rows["01"], 1): row["sequence"] = i
            self.assertFalse(pickup_result(rows, markers, target, before)["bounded_contested_pickup_observed"])

    def test_pickup_later_food_consumption_is_distinct_from_duplicate_first_feed(self):
        rows, markers, target, before = pickup_fixture()
        at = rows["01"].index(next(r for r in rows["01"] if r["event"] == "scene_native_authority_sample" and r.get("source_fed_on") == 1))
        body = dict(pickup_scope=81, actor=2, owner=2, actor_native_id=77, claimant_native_id=77,
                    claimant_present=True, claimed_by_actor=True, corpse_native_id=87, fed_on=True, health=10.0)
        callback = [dict(event="native_pickup_tick_enter", **body, food=90.0, hunger=60.0),
                    dict(event="native_pickup_tick_return_state", **body, food=80.0, hunger=70.0),
                    dict(event="native_pickup_tick_return", pickup_scope=81, actor=2, owner=2, result=True, state_observed=True, original_reward_return_observed=False)]
        rows["01"][at:at] = callback
        for i, row in enumerate(rows["01"], 1): row.update(sequence=i, qpc=i*10, qpc_frequency=100)
        for worker in rows.values():
            for row in worker:
                if row.get("source_fed_on") == 1: row.update(source_food=80.0, native_food=80.0)
        result = pickup_result(rows, markers, target, before)
        self.assertTrue(result["bounded_contested_pickup_observed"])
        self.assertEqual(len(result["native_first_feed_dna_calls"]), 1)

    def test_pickup_shared_projection_and_native_dna_are_mandatory(self):
        for field, value in (("source_fed_on", 0), ("native_fed_on", 0), ("source_food", 80.0),
                             ("native_food", 80.0), ("source_pickup_owner", 1), ("source_tick", 999),
                             ("entity_generation", 6)):
            rows, markers, target, before = pickup_fixture()
            rows["03"][-3][field] = value
            with self.subTest(field=field):
                self.assertFalse(pickup_result(rows, markers, target, before)["bounded_contested_pickup_observed"])
        rows, markers, target, before = pickup_fixture()
        rows["03"][-1].update(native_dna=None, dna_native_observed=False)
        self.assertFalse(pickup_result(rows, markers, target, before)["bounded_contested_pickup_observed"])

    def test_pickup_prior_feed_additional_balance_change_or_incomplete_window_fails(self):
        for defect in ("prior_feed", "additional_dna", "observation_complete", "prefixes_match", "endpoints_connected", "scene_exit", "network_error"):
            rows, markers, target, before = pickup_fixture()
            flags = {}
            if defect == "prior_feed":
                for worker in before.values(): worker[0].update(source_fed_on=1, native_fed_on=1, source_pickup_owner=2)
            elif defect == "additional_dna":
                for worker in rows.values(): worker[-1].update(source_dna=10.0, native_dna=10.0)
            elif defect in ("scene_exit", "network_error"):
                rows["03"].append(dict(sequence=9, event="scene_exit" if defect == "scene_exit" else "network_status", state="error"))
            else: flags[defect] = False
            with self.subTest(defect=defect):
                self.assertFalse(pickup_result(rows, markers, target, before, **flags)["bounded_contested_pickup_observed"])

    def test_pickup_parallel_real_ipc_arguments_and_uncertain_outcomes_are_retained(self):
        _, _, _, before = pickup_fixture()
        barrier = Barrier(2)
        calls = []
        def command(item, op, commands, epoch, values):
            calls.append((item, op, epoch, values))
            barrier.wait(timeout=3)  # Sequential submissions cannot satisfy this fixture.
            if item == "02": raise ValueError("fixture unknown IPC outcome")
            return {"result": "accepted"}
        report = {"commands": []}
        with patch.object(module.m06, "require_connected", side_effect=lambda item, *a: ({"epoch": 3}, before[item])), \
             patch.object(module.m06, "command", side_effect=command):
            with self.assertRaisesRegex(ValueError, "neither request was retried"):
                module.contested_pickup({wid: wid for wid in before}, "fixture", 77, 5, 1, report)
        self.assertCountEqual(calls, [("02", "network_action", 3, (1, 0, 7, 0, 77, 5)), ("03", "network_action", 3, (2, 0, 7, 0, 77, 5))])
        self.assertEqual(len(report["submissions"]), 2)
        self.assertIn("unknown", report["submissions"][0]["error"])
        self.assertEqual(report["submissions"][1]["response"]["result"], "accepted")
        self.assertFalse(report["bounded_contested_pickup_observed"])

    def test_pickup_operation_finishes_when_evidence_matches_and_rechecks_peers(self):
        rows, _, _, before = pickup_fixture()
        elapsed, reads = [0.0], []
        def connected(item, *args):
            reads.append(item)
            return {"epoch": 3}, before[item] if len(reads) <= 3 else rows[item]
        report = {"commands": []}
        with patch.object(module.m06, "require_connected", side_effect=connected), \
             patch.object(module.m06, "current_rows", side_effect=lambda item, version: rows[item] if elapsed[0] >= 1 else before[item]), \
             patch.object(module.m06, "command", return_value={"result": "accepted"}) as command, \
             patch.object(module.time, "monotonic", side_effect=lambda: elapsed[0]), \
             patch.object(module.time, "sleep", side_effect=lambda value: elapsed.__setitem__(0, elapsed[0]+value)):
            module.contested_pickup({wid: wid for wid in before}, "fixture", 77, 5, 2, report)
        self.assertEqual(command.call_count, 2)
        self.assertEqual(elapsed[0], 1)
        self.assertEqual(reads, ["02", "03", "01", "02", "03", "01"])
        self.assertTrue(report["bounded_contested_pickup_observed"])
        self.assertTrue(report["observation_ended_early"])

    def test_pickup_missing_evidence_waits_until_deadline_without_success(self):
        _, _, _, before = pickup_fixture()
        elapsed = [0.0]
        report = {"commands": []}
        with patch.object(module.m06, "require_connected", side_effect=lambda item, *a: ({"epoch": 3}, before[item])), \
             patch.object(module.m06, "current_rows", side_effect=lambda item, version: before[item]), \
             patch.object(module.m06, "command", return_value={"result": "accepted"}), \
             patch.object(module.time, "monotonic", side_effect=lambda: elapsed[0]), \
             patch.object(module.time, "sleep", side_effect=lambda value: elapsed.__setitem__(0, elapsed[0]+value)):
            module.contested_pickup({wid: wid for wid in before}, "fixture", 77, 5, 2, report)
        self.assertEqual(elapsed[0], 2)
        self.assertFalse(report["bounded_contested_pickup_observed"])
        self.assertFalse(report["observation_ended_early"])

    def test_pickup_reads_advancing_client_revisions_before_their_source_prefix(self):
        rows, _, _, before = pickup_fixture()
        elapsed, reads, connections, revision = [0.0], [], [], [1100]
        def current(item, version):
            reads.append(item)
            if item == "03":
                revision[0] += 1
                for wid in ("01", "03"):
                    previous = next(r for r in reversed(rows[wid]) if r.get("source_fed_on") == 1)
                    rows[wid].append(dict(previous, sequence=len(rows[wid])+1, source_tick=revision[0],
                        source_food=80.0, native_food=80.0))
            return deepcopy(rows[item])
        def connected(item, *args):
            connections.append(item)
            return {"epoch": 3}, deepcopy(before[item]) if len(connections) <= 3 else current(item, "fixture")
        report = {"commands": []}
        with patch.object(module.m06, "require_connected", side_effect=connected), \
             patch.object(module.m06, "current_rows", side_effect=current), \
             patch.object(module.m06, "command", return_value={"result": "accepted"}) as command, \
             patch.object(module.time, "monotonic", side_effect=lambda: elapsed[0]), \
             patch.object(module.time, "sleep", side_effect=lambda value: elapsed.__setitem__(0, elapsed[0]+value)):
            module.contested_pickup({wid: wid for wid in before}, "fixture", 77, 5, 2, report)
        self.assertEqual(reads, ["02", "03", "01", "02", "03", "01"])
        self.assertEqual(elapsed[0], 0)
        self.assertEqual(command.call_count, 2)
        self.assertTrue(report["bounded_contested_pickup_observed"])
        self.assertEqual(report["pickup"]["exact_revision_pickup_pairs"]["03"]["source_tick"], 1102)

    def test_pickup_rechecks_lost_convergence_only_within_original_deadline(self):
        for defect in ("delayed_source", "mismatched_food", "changed_prefix", "disconnected"):
            rows, _, _, before = pickup_fixture()
            newer = deepcopy(rows)
            for wid in ("01", "03"):
                previous = next(r for r in reversed(newer[wid]) if r.get("source_fed_on") == 1)
                newer[wid].append(dict(previous, sequence=len(newer[wid])+1, source_tick=1200,
                    source_food=80.0, native_food=80.0))
            if defect == "mismatched_food": newer["03"][-1].update(source_food=80.000001, native_food=80.000001)
            if defect == "changed_prefix": newer["03"][0]["hunger"] = 49.0
            elapsed, calls = [0.0], []
            def current(item, version): return deepcopy(rows[item] if elapsed[0] == 0 else newer[item])
            def connected(item, *args):
                calls.append(item)
                if len(calls) <= 3: return {"epoch": 3}, deepcopy(before[item])
                if defect == "disconnected" and item == "03": raise ValueError("fixture endpoint disconnected")
                # The source file has not flushed the newest client revision on
                # the first final recheck, despite the source-last read order.
                value = rows[item] if elapsed[0] == 0 and item == "01" else newer[item]
                return {"epoch": 3}, deepcopy(value)
            report = {"commands": []}
            with self.subTest(defect=defect), \
                 patch.object(module.m06, "require_connected", side_effect=connected), \
                 patch.object(module.m06, "current_rows", side_effect=current), \
                 patch.object(module.m06, "command", return_value={"result": "accepted"}) as command, \
                 patch.object(module.time, "monotonic", side_effect=lambda: elapsed[0]), \
                 patch.object(module.time, "sleep", side_effect=lambda value: elapsed.__setitem__(0, elapsed[0]+value)):
                if defect == "disconnected":
                    with self.assertRaisesRegex(ValueError, "disconnected"):
                        module.contested_pickup({wid: wid for wid in before}, "fixture", 77, 5, 2, report)
                else:
                    module.contested_pickup({wid: wid for wid in before}, "fixture", 77, 5, 2, report)
                self.assertEqual(command.call_count, 2)
                self.assertEqual(report["bounded_contested_pickup_observed"], defect == "delayed_source")
                self.assertEqual(elapsed[0], {"delayed_source": .5, "mismatched_food": 2, "changed_prefix": 0, "disconnected": 0}[defect])
                if defect == "delayed_source":
                    self.assertEqual([r["passed"] for r in report["pickup_rechecks"]], [False, True])
                    self.assertTrue(all(r["prefixes_match"] for r in report["pickup_rechecks"]))

    def test_pickup_removed_corpse_is_explicitly_unavailable(self):
        rows, markers, target, before = pickup_fixture()
        rows["03"].append(dict(sequence=len(rows["03"])+1, event="scene_replica_despawn", remote_entity=77))
        result = pickup_result(rows, markers, target, before)
        self.assertFalse(result["bounded_contested_pickup_observed"])
        self.assertEqual(result["unavailable_corpse_workers"], ["03"])
        self.assertTrue(any("consumption/removal" in gap for gap in result["observed_gaps"]))

    def test_b_lead_retains_first_submission_and_never_retries_after_npc_dies(self):
        items = {wid: wid for wid in ("01", "02", "03")}
        calls = []
        reads = 0
        elapsed = [0.0]
        def connected(item, version, commands):
            nonlocal reads
            reads += 1
            name = "scene_native_authority_sample" if item == "01" else "scene_native_replica_sample"
            rows = trace([sample(name, 77, 0, dead=reads > 3)])
            rows[0].update(qpc=100, qpc_frequency=10)
            return {"epoch": 3}, rows
        def command(item, op, commands, epoch, values):
            calls.append((item, values)); return {"result": "accepted"}
        report = {"commands": []}
        with patch.object(module.m06, "require_connected", side_effect=connected), \
             patch.object(module.m06, "command", side_effect=command), \
             patch.object(module.time, "monotonic", side_effect=lambda: elapsed[0]), \
             patch.object(module.time, "sleep", side_effect=lambda value: elapsed.__setitem__(0, elapsed[0]+value)):
            with self.assertRaisesRegex(ValueError, "A was not submitted"):
                module.combat(items, "fixture", 77, 5, "engage", 1, report, 2)
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0], ("03", (2, 0, 6, 0, 77, 5)))
        self.assertEqual(report["submissions"][0]["client"], "03")
        self.assertEqual(report["observed_b_lead_seconds"], 2)

    def test_uncertain_simultaneous_submission_retains_both_outcomes(self):
        def connected(item, version, commands):
            name = "scene_native_authority_sample" if item == "01" else "scene_native_replica_sample"
            rows = trace([sample(name, 77, 0)])
            rows[0].update(qpc=100, qpc_frequency=10)
            return {"epoch": 3}, rows
        def command(item, *args):
            if item == "02": raise ValueError("uncertain fixture outcome")
            return {"result": "accepted"}
        report = {"commands": []}
        with patch.object(module.m06, "require_connected", side_effect=connected), \
             patch.object(module.m06, "command", side_effect=command) as submit:
            with self.assertRaisesRegex(ValueError, "neither request was retried"):
                module.combat({wid: wid for wid in ("01", "02", "03")}, "fixture", 77, 5, "engage", 1, report)
        self.assertEqual(submit.call_count, 2)
        self.assertEqual(len(report["submissions"]), 2)
        self.assertIn("uncertain", report["submissions"][0]["error"])
        self.assertEqual(report["submissions"][1]["response"]["result"], "accepted")

    def test_queue_acceptance_never_becomes_native_damage(self):
        marker, target, client, authority = fixture()
        result = module.correlate(client, authority[:2], marker, target, "engage")
        self.assertTrue(result["accepted_is_queued"])
        self.assertFalse(result["native_target_observed"])
        self.assertFalse(result["native_damage_observed"])

    def test_native_chain_requires_actual_health_decrease(self):
        marker, target, client, authority = fixture()
        result = module.correlate(client, authority, marker, target, "engage")
        self.assertTrue(result["native_target_observed"])
        self.assertTrue(result["native_damage_observed"])
        self.assertEqual(result["native_health_decreases"][0]["health_decrease"], 1.0)
        authority[7]["health"] = 2.0
        self.assertFalse(module.correlate(client, authority, marker, target, "engage")["native_damage_observed"])

    def test_request_numbers_cannot_cross_player_identity_or_target_generation(self):
        for index, field, value in ((1, "player", 2), (1, "target_generation", 6), (0, "owner", 2), (0, "target_native_id", 45)):
            marker, target, client, authority = fixture()
            authority[index][field] = value
            with self.subTest(field=field):
                self.assertFalse(module.correlate(client, authority, marker, target, "engage")["native_damage_observed"])

    def test_missing_return_or_mixed_damage_scope_cannot_claim_hit(self):
        for edit in (lambda r: r.pop(), lambda r: r[7].update(animal_damage_scope=99), lambda r: r[6].update(call=43)):
            marker, target, client, authority = fixture()
            edit(authority)
            self.assertFalse(module.correlate(client, authority, marker, target, "engage")["native_damage_observed"])

    def test_scene_exit_or_superseding_command_fences_attribution(self):
        marker, target, client, authority = fixture()
        authority.append(dict(sequence=10, event="scene_exit"))
        self.assertFalse(module.correlate(client, authority, marker, target, "engage")["native_damage_observed"])
        marker, target, client, authority = fixture()
        authority[4] = dict(sequence=5, event="command_begin", actor=1, command=31)
        self.assertFalse(module.correlate(client, authority, marker, target, "engage")["native_damage_observed"])

    def test_duplicate_intentions_are_ambiguous_not_two_successes(self):
        marker, target, client, authority = fixture()
        client.append(dict(client[0], sequence=3, request=8))
        with self.assertRaisesRegex(ValueError, "Ambiguous"):
            module.correlate(client, authority, marker, target, "engage")

    def test_all_three_native_scenes_and_current_dna_are_required(self):
        rows = {}
        for wid in ("01", "02", "03"):
            name = "scene_native_authority_sample" if wid == "01" else "scene_native_replica_sample"
            rows[wid] = trace([sample(name, 77, 0, dead=True), sample(name, 66, 1, 8.75), sample(name, 67, 2)])
        target = dict(remote_entity=77, entity_generation=5)
        result = module.shared_state(rows, target)
        self.assertTrue(result["npc_dead_on_all_machines"])
        self.assertTrue(all(result["controlled_native_dna_matches"].values()))
        rows["03"][2].update(native_dna=None, dna_native_observed=False)
        result = module.shared_state(rows, target)
        self.assertTrue(result["sampled_shared_state_agreement"])
        self.assertFalse(result["controlled_native_dna_matches"]["2"])
        rows["03"][0]["entity_generation"] = 6
        self.assertFalse(module.shared_state(rows, target)["npc_dead_on_all_machines"])

    def test_despawn_and_scene_exit_clear_old_sample_candidates(self):
        rows = trace([sample("scene_native_authority_sample", 77, 0), ("network_despawn_published", dict(entity=77))])
        self.assertNotIn(77, module.scene(rows, True))
        rows[-1] = dict(sequence=2, event="scene_exit")
        self.assertFalse(module.scene(rows, True))

    def test_reward_entry_without_return_cannot_claim_native_grant(self):
        marker, target, client, authority = fixture()
        authority[7]["health"] = 0.0
        authority.append(dict(sequence=10, event="native_global_dna_enter", call=60, animal_damage_scope=41, before=0.0, amount=8.75))
        rows = {"01": authority, "02": client, "03": []}
        result = module.summarize(rows, [marker], target, "engage")
        self.assertFalse(result["single_native_kill_reward_call_observed"])
        self.assertEqual(result["full_m07"], "NOT_VERIFIED")
        authority.append(dict(sequence=11, event="native_global_dna_return", call=60, animal_damage_scope=41, after=8.75))
        self.assertTrue(module.summarize(rows, [marker], target, "engage")["single_native_kill_reward_call_observed"])

    def test_remote_ownership_rejection_is_player_scoped_and_bounded(self):
        marker, client, coordinator, flags = unauthorized_fixture()
        authority = trace([("network_native_intention", dict(request=8, player=2)),
                           ("network_actor_command_queued", dict(request=8, owner=2))])
        coordinator.append(dict(coordinator[1], outbound=True, player=2))
        result = module.unauthorized_evidence(client, authority, coordinator, marker, **flags)
        self.assertTrue(result["unauthorized_action_rejected"])
        self.assertTrue(result["no_authority_action_in_observed_prefix"])
        self.assertEqual(result["full_m07"], "NOT_VERIFIED")

    def test_local_refusal_or_missing_remote_record_never_satisfies_rejection(self):
        for omitted in ("intention", "client_rejection", "coordinator_all", "coordinator_incoming", "coordinator_rejection", "connection"):
            marker, client, coordinator, flags = unauthorized_fixture()
            if omitted == "intention": client = client[1:]
            elif omitted == "client_rejection": client = client[:1]
            elif omitted == "coordinator_all": coordinator = []
            elif omitted == "coordinator_incoming": coordinator = coordinator[:1]
            elif omitted == "coordinator_rejection": coordinator = coordinator[1:]
            else:
                for row in coordinator: row.pop("connection")
            with self.subTest(omitted=omitted):
                self.assertFalse(module.unauthorized_evidence(client, [], coordinator, marker, **flags)["unauthorized_action_rejected"])

    def test_forwarded_challenge_peer_loss_or_incomplete_window_fails_rejection(self):
        for defect in ("forwarded", "authority_received", "authority_queued", "wrong_generation", "wrong_connection",
                       "other_connected", "prefixes_match", "observation_complete"):
            marker, client, coordinator, flags = unauthorized_fixture()
            authority = []
            if defect == "forwarded": coordinator.append(dict(coordinator[1], outbound=True))
            elif defect == "authority_received": authority = trace([("network_native_intention", dict(request=8, player=1))])
            elif defect == "authority_queued": authority = trace([("network_actor_command_queued", dict(request=8, owner=1))])
            elif defect == "wrong_generation": coordinator[0]["generation"] = 6
            elif defect == "wrong_connection": coordinator[0]["connection"] = 13
            else: flags[defect] = False
            with self.subTest(defect=defect):
                self.assertFalse(module.unauthorized_evidence(client, authority, coordinator, marker, **flags)["unauthorized_action_rejected"])


if __name__ == "__main__": unittest.main()
