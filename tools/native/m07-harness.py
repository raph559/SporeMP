"""Inspect or exercise already-running original M07 authority and two clients.

Shared-combat, contested-pickup and unauthorized-action submit intentions through real client IPC
and authenticated connections. This tool never launches/stops SPORE, sends desktop
input, restores saves, performs an invented pickup, or grants M07 acceptance.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import struct
import time

_spec = importlib.util.spec_from_file_location("m07_m06_helpers", Path(__file__).with_name("m06-harness.py"))
m06 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(m06)
VERBS = {"jump": 1, "attack": 2, "approach": 5, "engage": 6, "stop": 3, "pickup": 7}
CLASS = "ORIGINAL_M07_PROCESS_HARNESS"
PICKUP_REWARD_CALLER_RVA = 0x9713b2


def scalar(value):
    if type(value) not in (int, float) or not math.isfinite(value):
        raise ValueError("Malformed native scalar")
    return value


def scene(rows, authority):
    """Last directly sampled native state, respecting observed lifecycle cuts."""
    result = {}
    names = ("scene_native_authority_sample", "scene_native_pickup_state_published") if authority else ("scene_native_replica_sample", "scene_native_pickup_state_applied")
    for row in rows:
        if row.get("event") == "scene_exit": result.clear()
        elif row.get("event") in ("network_despawn_published", "scene_replica_despawn"):
            result.pop(row.get("entity", row.get("remote_entity")), None)
        elif row.get("event") in names:
            identity = m06.uint(row.get("remote_entity"), nonzero=True)
            m06.uint(row.get("entity_generation"), nonzero=True)
            if row.get("owner") not in (0, 1, 2) or type(row.get("dead")) is not bool:
                raise ValueError("Native scene sample lacks explicit owner/life state")
            scalar(row.get("health")); scalar(row.get("source_dna"))
            m06.uint(row.get("source_tick"), nonzero=True)
            m06.vector(row.get("position"))
            if row.get("dna_native_observed") is True: scalar(row.get("native_dna"))
            result[identity] = row
    return result


def unique(rows, name):
    if len(rows) > 1: raise ValueError("Ambiguous " + name + " in the bounded encounter window")
    return rows[0] if rows else None


def damage_outcomes(rows, attacker, receiver, after_sequence=0):
    """Pair original calls and actual health changes, never count requested damage."""
    results = []
    for start in rows:
        if start.get("event") != "native_animal_damage_enter" or start.get("attacker") != attacker or start.get("receiver") != receiver or start["sequence"] <= after_sequence:
            continue
        call = m06.uint(start.get("call"), nonzero=True)
        end = unique([r for r in rows if r.get("event") == "native_animal_damage_return" and r.get("call") == call], "native damage return")
        if not end or end["sequence"] <= start["sequence"] or end.get("attacker") != attacker or end.get("receiver") != receiver:
            continue
        inside = [r for r in rows if start["sequence"] < r["sequence"] < end["sequence"] and r.get("animal_damage_scope") == call]
        native = [r for r in inside if r.get("event") == "native_damage_enter" and r.get("attacker") == attacker and r.get("receiver") == receiver]
        state = [r for r in inside if r.get("event") in ("damage_state", "animal_damage_state") and r.get("actor") == receiver]
        if not native or not state: continue
        first = native[0]
        returned = [r for r in inside if r.get("event") == "native_damage_return" and r.get("call") == first.get("call") and r.get("receiver") == receiver and r.get("attacker") == attacker]
        if len(returned) != 1: continue
        state = [r for r in state if r["sequence"] > returned[0]["sequence"]]
        if not state: continue
        before, after = scalar(first.get("receiver_health")), scalar(state[-1].get("health"))
        if after < before:
            results.append({"native_call": call, "attacker_actor": attacker, "receiver_actor": receiver,
                            "before_health": before, "after_health": after, "health_decrease": before-after,
                            "enter_sequence": start["sequence"], "return_sequence": end["sequence"]})
    return results


def correlate(client_rows, authority_rows, marker, target, verb):
    owner = marker["owner"]
    local = [r for r in client_rows if r["sequence"] > marker["client_before_sequence"]]
    remote = [r for r in authority_rows if r["sequence"] > marker["authority_before_sequence"]]
    evidence = {**marker, "accepted_is_queued": True, "native_target_observed": False, "native_damage_observed": False}
    intention = unique([r for r in local if r.get("event") == "network_intention" and r.get("player") == owner and
                        r.get("verb") == VERBS[verb] and r.get("target") == target["remote_entity"] and
                        r.get("target_generation") == target["entity_generation"]], "client encounter intention")
    if not intention: return evidence
    request = m06.uint(intention.get("request"), nonzero=True)
    evidence["network_intention"] = intention
    result = unique([r for r in local if r.get("event") == "network_action_result" and r.get("request") == request], "action result")
    if result: evidence["action_result"] = result
    received = unique([r for r in remote if r.get("event") == "network_native_intention" and r.get("request") == request and
                       r.get("player") == owner and r.get("entity") == intention.get("entity") and
                       r.get("target") == target["remote_entity"] and r.get("target_generation") == target["entity_generation"]], "authority encounter reception")
    queued = unique([r for r in remote if r.get("event") == "network_actor_command_queued" and r.get("request") == request and
                     r.get("owner") == owner and r.get("verb") == verb and r.get("target_native_id") == target["native_id"]], "authority native queue")
    if received: evidence["authority_received"] = received
    if queued: evidence["authority_queued"] = queued
    if not received or not queued or received.get("queued") is not True or queued.get("decision") != "accepted": return evidence
    actor, command, npc = queued["actor"], queued["command"], queued["target"]
    started = unique([r for r in remote if r.get("event") == "command_begin" and r.get("actor") == actor and
                      r.get("command") == command and r.get("owner") == owner and r.get("target") == npc], "original command dispatch")
    if not started: return evidence
    evidence["native_command_begin"] = started
    superseding = [r["sequence"] for r in remote if r.get("event") == "command_begin" and r.get("actor") == actor and
                   r.get("command") != command and r["sequence"] > started["sequence"]]
    until = min(superseding, default=authority_rows[-1]["sequence"] + 1)
    window = [r for r in remote if started["sequence"] <= r["sequence"] < until]
    targets = [r for r in window if r.get("event") == "native_target_enter" and r.get("executing_command") == command and
               r.get("actor") == actor and r.get("target") == npc and r.get("target_native_id") == target["native_id"]]
    if targets:
        evidence["native_target_observed"] = True
        evidence["native_target"] = targets[0]
        hits = damage_outcomes(window, actor, npc, targets[0]["sequence"])
        evidence["native_damage_observed"] = bool(hits)
        evidence["native_health_decreases"] = hits
        evidence["native_npc_health_decreases_player"] = damage_outcomes(window, npc, actor)
        evidence["native_npc_target_requests"] = [r for r in window if r.get("event") == "native_target_enter" and r.get("actor") == npc and r.get("target") == actor]
        evidence["damage_attribution"] = "Resolved native actor and NPC within this encounter; no later bridge command for that actor"
    if superseding: evidence["superseded_at_sequence"] = until
    evidence["scene_lifecycle_unchanged"] = not any(r.get("event") == "scene_exit" for r in remote+local)
    if not evidence["scene_lifecycle_unchanged"]:
        evidence["native_target_observed"] = evidence["native_damage_observed"] = False
    return evidence


def shared_state(rows_by_worker, target):
    scenes = {wid: scene(rows, wid == "01") for wid, rows in rows_by_worker.items()}
    normalized, target_rows, owner_balances = {}, {}, {}
    for wid, entries in scenes.items():
        owners = {}
        for owner in (1, 2):
            candidates = [r for r in entries.values() if r["owner"] == owner]
            if len(candidates) == 1:
                row = candidates[0]
                owners[str(owner)] = {"entity": row["remote_entity"], "generation": row["entity_generation"], "dna": row["source_dna"]}
        # Owned balances remain observable after the original corpse is retired.
        owner_balances[wid] = owners
        target_row = entries.get(target["remote_entity"])
        if not target_row or target_row["entity_generation"] != target["entity_generation"]: continue
        target_rows[wid] = target_row
        value = {"npc": {"entity": target_row["remote_entity"], "generation": target_row["entity_generation"],
                         "health": target_row["health"], "dead": target_row["dead"],
                         "fed_on": target_row.get("source_fed_on"), "food": target_row.get("source_food"),
                         "pickup_owner": target_row.get("source_pickup_owner")}, "published_owner_balances": owners}
        raw = json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()
        normalized[wid] = {"state": value, "sha256": hashlib.sha256(raw).hexdigest(), "source_tick": target_row["source_tick"]}
    agreement = len(normalized) == 3 and all(len(v["state"]["published_owner_balances"]) == 2 for v in normalized.values()) and len({v["sha256"] for v in normalized.values()}) == 1
    native_dna = {}
    for wid, owner in (("02", 1), ("03", 2)):
        authority = [r for r in scenes.get("01", {}).values() if r["owner"] == owner]
        local = [r for r in scenes.get(wid, {}).values() if r["owner"] == owner]
        native_dna[str(owner)] = bool(len(authority) == len(local) == 1 and authority[0].get("dna_native_observed") is True and
            local[0].get("dna_native_observed") is True and local[0].get("is_native_avatar") is True and
            authority[0].get("source_dna") == authority[0].get("native_dna") == local[0].get("native_dna") == local[0].get("source_dna"))
    return {"normalized": normalized, "sampled_shared_state_agreement": agreement,
            "published_owner_balances": owner_balances,
            "npc_dead_on_all_machines": agreement and all(r["dead"] for r in target_rows.values()),
            "controlled_native_dna_matches": native_dna,
            "pickup_native_state_matches": {wid: pickup_fields_match(target_rows.get(wid)) for wid in ("01", "02", "03")},
            "limits": "Latest actual samples; no cross-process QPC subtraction, no persistence or pickup acceptance"}


def pickup_fields_match(row):
    """Absent fields in historical combat traces are unknown pickup evidence."""
    if not row: return False
    return (type(row.get("source_fed_on")) is int and row["source_fed_on"] in (0, 1) and
            type(row.get("native_fed_on")) is int and row["native_fed_on"] == row["source_fed_on"] and
            type(row.get("source_pickup_owner")) is int and row["source_pickup_owner"] in (0, 1, 2) and
            type(row.get("source_food")) in (int, float) and math.isfinite(row["source_food"]) and abs(row["source_food"]) <= 1e9 and
            type(row.get("native_food")) in (int, float) and row["native_food"] == row["source_food"])


def pickup_projection_pairs(rows, markers, target, winner):
    """Compare actual reads at the same source revision, without rounding food."""
    source = {}
    cut = min((m["authority_before_sequence"] for m in markers), default=0)
    fields = ("remote_entity", "entity_generation", "health", "dead", "source_fed_on", "source_food", "source_pickup_owner")
    def eligible(row):
        return (row.get("remote_entity") == target["remote_entity"] and row.get("entity_generation") == target["entity_generation"] and
                row.get("dead") is True and row.get("health") == 0 and row.get("source_fed_on") == 1 and
                row.get("source_pickup_owner") == winner and pickup_fields_match(row))
    for row in rows["01"]:
        if row["sequence"] > cut and row.get("event") in ("scene_native_authority_sample", "scene_native_pickup_state_published") and eligible(row):
            source.setdefault(row["source_tick"], []).append(row)
    pairs = {}
    for marker in markers:
        for local in rows[marker["client"]]:
            if local["sequence"] <= marker["client_before_sequence"] or local.get("event") not in ("scene_native_replica_sample", "scene_native_pickup_state_applied") or not eligible(local): continue
            pairs.pop(marker["client"], None)
            for original in source.get(local["source_tick"], []):
                if all(local.get(k) == original.get(k) for k in fields):
                    pairs[marker["client"]] = {"source_tick": local["source_tick"], "source_sequence": original["sequence"],
                        "client_sequence": local["sequence"], "source_event": original["event"], "client_event": local["event"],
                        "fed_on": 1, "food": original["source_food"], "pickup_owner": winner,
                        "native_source_food": original["native_food"], "native_client_food": local["native_food"]}
    return pairs


def pickup_terminal_retirement(rows, markers, target, winner, pairs, after_feed):
    """Qualify observed original pool retirement, never infer it from absence.

    Removal events lack generation fields. Bind each to its preceding native
    sample, reject reuse in the bounded window, and retain only this incarnation.
    A destroy-hook entry alone does not prove original destruction returned.
    """
    result = {"verified": False, "reason": "Exact prior projections and an original pool retirement chain are required"}
    if set(pairs) != {"02", "03"}: return result
    cuts = {"01": min((m["authority_before_sequence"] for m in markers), default=0),
            **{m["client"]: m["client_before_sequence"] for m in markers}}
    if set(cuts) != {"01", "02", "03"}: return result
    windows = {wid: [r for r in rows[wid] if r["sequence"] > cut] for wid, cut in cuts.items()}
    current = {wid: scene(rows[wid], wid == "01") for wid in cuts}
    if any(target["remote_entity"] in entries for entries in current.values()):
        result["reason"] = "The target ID is still present or has reappeared"
        return result
    removals, preceding = {}, {}
    for wid, window in windows.items():
        source = wid == "01"
        name = "network_despawn_published" if source else "scene_replica_despawn"
        field = "entity" if source else "remote_entity"
        removed = [r for r in window if r.get("event") == name and r.get(field) == target["remote_entity"]]
        if len(removed) != 1:
            result["reason"] = "Exactly one removal for the selected ID is required on every worker"
            return result
        removal = removed[0]
        names = ("scene_native_authority_sample", "scene_native_pickup_state_published") if source else ("scene_native_replica_sample", "scene_native_pickup_state_applied")
        samples = [r for r in window if r.get("event") in names and r.get("remote_entity") == target["remote_entity"]]
        if not samples or any(r.get("entity_generation") != target["entity_generation"] or
                r["sequence"] >= removal["sequence"] for r in samples):
            result["reason"] = "Missing incarnation sample or ID reuse around removal"
            return result
        previous = samples[-1]
        epoch = previous.get("epoch")
        if (type(epoch) is not int or epoch <= 0 or removal.get("epoch") != epoch or
                previous.get("owner") != 0 or previous.get("dead") is not True or previous.get("health") != 0 or
                previous.get("source_fed_on") != 1 or previous.get("source_pickup_owner") != winner or
                not pickup_fields_match(previous) or previous["source_food"] > 0 or
                any(r.get("native_id") != previous.get("native_id") or r.get("epoch") != epoch for r in samples)):
            result["reason"] = "The removed incarnation lacks a depleted native corpse readback"
            return result
        if any(r.get("event") in names and r.get("native_id") == previous["native_id"] and
               r.get("remote_entity") != target["remote_entity"] for r in window):
            result["reason"] = "Native ID reuse makes retirement attribution ambiguous"
            return result
        if source:
            if previous["native_id"] != target["native_id"] or previous["sequence"] <= after_feed: return result
            if any(p["source_sequence"] > previous["sequence"] for p in pairs.values()): return result
            if any(r.get("event") == "network_authority_pose" and r.get("entity") == target["remote_entity"] and
                   (r.get("generation") != target["entity_generation"] or r["sequence"] >= removal["sequence"]) for r in window): return result
        elif (removal.get("reason") != "authoritative_despawn" or removal.get("native_id") != previous["native_id"] or
              pairs[wid]["client_sequence"] > previous["sequence"]): return result
        # Fresh balances must be read after each local removal, not cached from
        # the earlier fed-corpse sample or a different owner incarnation.
        for owner in (1, 2):
            owned = [r for r in current[wid].values() if r["owner"] == owner]
            if len(owned) != 1 or owned[0]["sequence"] <= removal["sequence"] or owned[0].get("epoch") != epoch:
                result["reason"] = "Current owned identity and balance samples after removal are missing"
                return result
        removals[wid], preceding[wid] = removal, previous
    original = windows["01"]
    pools = [r for r in original if r.get("event") == "native_pool_return_observed" and r.get("native_id") == target["native_id"]]
    grants = [r for r in original if r.get("event") == "native_pickup_grant_retired" and r.get("corpse_native_id") == target["native_id"]]
    if len(pools) != 1 or len(grants) != 1: return result
    pool, grant, last = pools[0], grants[0], preceding["01"]
    counters = (pool.get("counter_before"), pool.get("counter_after"), last.get("native_pool_cycle"))
    if (not all(type(v) is int and 0 <= v <= 0xffffffff for v in counters) or counters[0] != counters[2] or counters[0] == counters[1] or
            pool.get("enabled") is not False or pool.get("herd_present") is not False or
            grant.get("owner") != winner or grant.get("reason") != "native_pool_return" or
            pool.get("epoch") != last["epoch"] or grant.get("epoch") != last["epoch"] or
            not last["sequence"] < pool["sequence"] < grant["sequence"] < removals["01"]["sequence"]): return result
    result.update(verified=True, reason="Original depleted-corpse pool return, grant retirement and both authoritative removals observed",
                  entity=target["remote_entity"], generation=target["entity_generation"],
                  source_pool_return=pool, grant_retirement=grant, removals=removals,
                  preceding_sample_sequences={wid: r["sequence"] for wid, r in preceding.items()})
    return result


def summarize_pickup(rows, markers, target, before, *, observation_complete=False,
                     prefixes_match=False, endpoints_connected=False):
    """Bound one original first-feed claim and real nutrition, including zero DNA."""
    chains = [correlate(rows[m["client"]], rows["01"], m, target, "pickup") for m in markers]
    cut = min((m["authority_before_sequence"] for m in markers), default=0)
    remote = [r for r in rows["01"] if r["sequence"] > cut]
    state = shared_state(rows, target)
    baseline = shared_state(before, target)
    gaps, orders = [], []
    for chain in chains:
        queued, started = chain.get("authority_queued"), chain.get("native_command_begin")
        found = []
        if queued and started:
            found = [r for r in remote if r.get("event") == "native_pickup_order_return" and
                     r.get("executing_command") == queued["command"] and r.get("actor") == queued["actor"] and
                     r.get("owner") == chain["owner"] and r.get("target") == queued["target"] and
                     started["sequence"] < r["sequence"] < chain.get("superseded_at_sequence", math.inf)]
        order = unique(found, "original pickup order return")
        chain["native_pickup_order_return"] = order
        if order and order.get("ordered") is True and chain.get("scene_lifecycle_unchanged"):
            orders.append(order)
    both_orders = len(chains) == len(orders) == 2 and {c["owner"] for c in chains} == {1, 2}
    if not both_orders: gaps.append("Two authenticated requests with accepted original native pickup order returns are missing")

    ticks, transitions, grants, reward_attempts, unresolved = [], [], [], [], []
    scopes = {}
    for row in remote:
        if row.get("pickup_scope"): scopes.setdefault(row["pickup_scope"], []).append(row)
    for entry in remote:
        if entry.get("event") != "native_pickup_tick_enter" or entry.get("corpse_native_id") != target["native_id"]: continue
        scope = m06.uint(entry.get("pickup_scope"), nonzero=True)
        inside = scopes[scope]
        end = unique([r for r in inside if r.get("event") == "native_pickup_tick_return"], "pickup tick return")
        after = unique([r for r in inside if r.get("event") == "native_pickup_tick_return_state"], "pickup state return")
        matched = bool(end and after and entry["sequence"] < after["sequence"] < end["sequence"] and
                       all(r.get("actor") == entry.get("actor") and r.get("owner") == entry.get("owner") for r in (after, end)) and
                       after.get("corpse_native_id") == target["native_id"] and end.get("state_observed") is True)
        ticks.append({"scope": scope, "entry": entry, "state_return": after, "return": end, "matched": matched})
        if not matched:
            unresolved.append({"scope": scope, "reason": "Missing or mismatched original pickup callback return/readback"})
        if matched and entry.get("fed_on") is False and after.get("fed_on") is True:
            transitions.append(ticks[-1])
        for reward in inside:
            name = reward.get("event")
            if name not in ("native_global_dna_enter", "native_owned_pickup_reward_applied"): continue
            if reward.get("caller_rva") != PICKUP_REWARD_CALLER_RVA: continue
            reward_attempts.append(reward)
            valid = matched and entry["sequence"] < reward["sequence"] < after["sequence"]
            if name == "native_owned_pickup_reward_applied":
                valid = valid and entry.get("owner") == reward.get("owner") == 2 and reward.get("actor") == entry.get("actor") and reward.get("corpse_native_id") == target["native_id"] and reward.get("reward_kind") == "corpse_first_feed"
                start_dna, end_dna = scalar(reward.get("b_dna_before")), scalar(reward.get("b_dna_after"))
                valid = valid and scalar(reward.get("a_dna_before")) == scalar(reward.get("a_dna_after"))
                returned = reward  # This event is emitted after the original owned award returns.
            else:
                returned = unique([r for r in inside if r.get("event") == "native_global_dna_return" and
                                   r.get("call") == reward.get("call")], "original pickup DNA return")
                valid = valid and entry.get("owner") == 1 and reward.get("avatar") == entry.get("actor") and bool(returned) and returned.get("avatar") == entry.get("actor") and reward["sequence"] < returned["sequence"] < after["sequence"]
                start_dna = scalar(reward.get("before"))
                end_dna = scalar(returned.get("after")) if returned else start_dna
            amount = scalar(reward.get("amount"))
            # The native bound adds two IEEE float values. Preserve that ABI
            # arithmetic here; never round food or source-revision comparisons.
            f32 = lambda value: struct.unpack("f", struct.pack("f", value))[0]
            upper = f32(f32(start_dna)+f32(amount))
            valid = valid and amount >= 0 and end_dna >= start_dna and (end_dna == start_dna if amount == 0 else f32(end_dna) <= upper)
            if valid:
                grants.append({"scope": scope, "owner": entry["owner"], "actor": entry["actor"], "before": start_dna,
                               "after": end_dna, "amount": amount, "observed_delta": end_dna-start_dna,
                               "zero_original_dna_amount": amount == 0, "native_event": reward, "native_return": returned})
            else: unresolved.append({"scope": scope, "reason": "Original first-feed DNA call is unpaired, misattributed, negative, or has an incompatible observed delta"})
    feeds = [r for r in remote if r.get("event") == "native_pickup_first_feed_observed" and r.get("corpse_native_id") == target["native_id"]]
    single = len(transitions) == len(grants) == len(reward_attempts) == len(feeds) == 1 and not unresolved
    winner, grant, transition = (grants[0]["owner"], grants[0], transitions[0]) if single else (None, None, None)
    if single:
        feed = feeds[0]
        single = (feed.get("pickup_scope") == grant["scope"] == transition["scope"] and
                  feed.get("owner") == winner and feed.get("actor") == grant["actor"] == transition["entry"].get("actor") and
                  transition["state_return"]["sequence"] < feed["sequence"] < transition["return"]["sequence"] and
                  transition["return"].get("original_reward_return_observed") is True and
                  scalar(feed.get("food_before")) == scalar(transition["entry"].get("food")) and
                  scalar(feed.get("food_after")) == scalar(transition["state_return"].get("food")))
    if not single: gaps.append("Exactly one paired original first-feed transition and returned original DNA call has not been proved")
    # Both returns preceding the winning tick's entry is stricter than relying
    # on a reward event emitted only after the original call returned.
    ordered_before = bool(len(transitions) == 1 and both_orders and all(o["sequence"] < transitions[0]["entry"]["sequence"] for o in orders))
    if not ordered_before: gaps.append("Both original orders must return before the winning feeding callback begins")
    attributable = bool(single and any(c.get("authority_queued", {}).get("actor") == grant["actor"] and
        c["owner"] == winner and c.get("superseded_at_sequence", math.inf) > transition["return"]["sequence"] for c in chains))
    if not attributable: gaps.append("The winning native feeder is not attributable to a still-current submitted order")

    transfers = []
    for tick in ticks:
        if not tick["matched"]: continue
        entry, after = tick["entry"], tick["state_return"]
        food_used = scalar(entry.get("food")) - scalar(after.get("food"))
        hunger_gain = scalar(after.get("hunger")) - scalar(entry.get("hunger"))
        health_gain = scalar(after.get("health")) - scalar(entry.get("health"))
        claimed = (entry.get("claimed_by_actor") is True and after.get("claimed_by_actor") is True and
                   entry.get("claimant_present") is True and after.get("claimant_present") is True and
                   entry.get("claimant_native_id") == after.get("claimant_native_id") == entry.get("actor_native_id") == after.get("actor_native_id"))
        if food_used > 0 or hunger_gain > 0 or health_gain > 0:
            transfers.append({"scope": tick["scope"], "actor": entry.get("actor"), "owner": entry.get("owner"),
                "enter_sequence": entry["sequence"], "return_sequence": tick["return"]["sequence"], "food_used": food_used,
                "hunger_gain": hunger_gain, "health_gain": health_gain, "claim_verified": claimed,
                "fed_on_after": after.get("fed_on") is True})
    feeding_until = min((c.get("superseded_at_sequence", math.inf) for c in chains if single and
        c.get("authority_queued", {}).get("actor") == grant["actor"] and c["owner"] == winner), default=math.inf)
    nutrition = [t for t in transfers if single and t["owner"] == winner and t["actor"] == grant["actor"] and
                 t["enter_sequence"] >= transition["entry"]["sequence"] and t["claim_verified"] and t["fed_on_after"] and
                 t["return_sequence"] < feeding_until and
                 t["food_used"] > 0 and (t["hunger_gain"] > 0 or t["health_gain"] > 0)]
    losing_transfers = [t for t in transfers if single and (t["owner"] != winner or t["actor"] != grant["actor"])]
    loser_balance = False
    if single:
        loser = 3-winner
        a = [r for r in scene(before["01"], True).values() if r["owner"] == loser]
        b = [r for r in scene(rows["01"], True).values() if r["owner"] == loser]
        loser_balance = bool(len(a) == len(b) == 1 and a[0]["remote_entity"] == b[0]["remote_entity"] and
            b[0]["sequence"] > transition["return"]["sequence"] and a[0]["entity_generation"] == b[0]["entity_generation"] and scalar(b[0].get("hunger")) <= scalar(a[0].get("hunger")) and
            b[0]["health"] <= a[0]["health"])
    consumed = bool(nutrition and not losing_transfers and loser_balance)
    if not consumed: gaps.append("Positive native food-to-hunger/health transfer for the verified claimant, without losing-owner transfer, is missing")

    first_source = next((r for r in remote if r.get("event") in ("scene_native_authority_sample", "scene_native_pickup_state_published") and
        r.get("remote_entity") == target["remote_entity"] and r.get("entity_generation") == target["entity_generation"] and
        r.get("source_fed_on") == 1 and (not transition or r["sequence"] > transition["return"]["sequence"])), None)
    pairs = pickup_projection_pairs(rows, markers, target, winner) if single else {}
    dna_readback_after_feed = bool(single and first_source)
    if dna_readback_after_feed:
        for wid, owner in (("01", 1), ("01", 2), ("02", 1), ("03", 2)):
            observed = [r for r in scene(rows[wid], wid == "01").values() if r["owner"] == owner]
            dna_readback_after_feed = dna_readback_after_feed and len(observed) == 1 and observed[0]["source_tick"] >= first_source["source_tick"]
            if wid == "01": dna_readback_after_feed = dna_readback_after_feed and observed[0]["sequence"] > transition["return"]["sequence"]
    present_projected = bool(single and len(pairs) == 2 and set(pairs) == {"02", "03"} and len(state["normalized"]) == 3 and
        dna_readback_after_feed and all(state["pickup_native_state_matches"].values()) and all(state["controlled_native_dna_matches"].values()) and
        all(v["state"]["npc"]["dead"] and v["state"]["npc"]["fed_on"] == 1 and v["state"]["npc"]["pickup_owner"] == winner for v in state["normalized"].values()))
    retirement = pickup_terminal_retirement(rows, markers, target, winner, pairs, transition["return"]["sequence"]) if single else {"verified": False}
    retired_projected = bool(retirement["verified"] and consumed and dna_readback_after_feed and
        all(state["controlled_native_dna_matches"].values()))
    projected = present_projected or retired_projected
    owner_values = list(state["published_owner_balances"].values())
    owners_agree = len(owner_values) == 3 and all(len(v) == 2 and v == owner_values[0] for v in owner_values)
    balances = bool(single and baseline["sampled_shared_state_agreement"] and
                    all(baseline["controlled_native_dna_matches"].values()) and owners_agree)
    if balances:
        initial = baseline["normalized"]["01"]["state"]["published_owner_balances"]
        final = state["published_owner_balances"]["01"]
        balances = all((initial[str(owner)]["entity"], initial[str(owner)]["generation"]) ==
                       (final[str(owner)]["entity"], final[str(owner)]["generation"]) and
                       (initial[str(owner)]["dna"] == grant["before"] and final[str(owner)]["dna"] == grant["after"]
                        if owner == winner else initial[str(owner)]["dna"] == final[str(owner)]["dna"]) for owner in (1, 2))
    if not projected: gaps.append("Exact source-revision corpse flag/food/owner readbacks for both clients and controlled native DNA are missing")
    unavailable = [wid for wid in ("01", "02", "03") if wid not in state["normalized"]]
    if unavailable and not retired_projected: gaps.append("The selected corpse incarnation is absent from the current native scene on " + ", ".join(unavailable) + "; consumption/removal lacks a qualified terminal retirement chain")
    if not balances: gaps.append("Baseline-to-final owner balances do not isolate the single original first-feed grant")
    failures = []
    for marker in [{"client": "01", "client_before_sequence": cut}, *markers]:
        for r in rows[marker["client"]]:
            if r["sequence"] <= marker["client_before_sequence"]: continue
            if (r.get("event") in ("scene_exit", "trace_limit", "scene_capture_rejected", "scene_projection_rejected", "native_pickup_observation_rejected", "native_owned_pickup_reward_rejected", "native_owned_pickup_property_reward_rejected") or
                r.get("event") == "network_status" and r.get("state") == "error"):
                failures.append({"worker": marker["client"], "event": r})
    baseline_valid = bool(baseline["npc_dead_on_all_machines"] and all(baseline["pickup_native_state_matches"].values()) and
        all(v["state"]["npc"]["fed_on"] == 0 and v["state"]["npc"]["pickup_owner"] == 0 and v["state"]["npc"]["food"] > 0 for v in baseline["normalized"].values()))
    if not baseline_valid: gaps.append("A shared untouched native corpse with remaining food was not observed before submission")
    complete = observation_complete and prefixes_match and endpoints_connected and not failures
    if not complete: gaps.append("The bounded observation, connected peers, unchanged log prefixes, or lifecycle/error checks are incomplete")
    ready = bool(single and consumed and ordered_before and attributable and projected and balances and baseline_valid and not failures)
    return {"actions": chains, "native_pickup_order_returns": orders, "both_original_orders_returned": both_orders,
            "both_original_orders_before_first_feed": ordered_before, "native_first_feed_transitions": transitions,
            "native_first_feed_events": feeds, "original_first_feed_reward_attempts": reward_attempts,
            "native_first_feed_dna_calls": grants, "unresolved_native_outcomes": unresolved,
            "single_original_first_feed_call_observed": bool(single), "winner": winner if single else None,
            "native_nutrition_transfers": nutrition, "losing_owner_transfers": losing_transfers,
            "losing_owner_no_nutrition_gain": loser_balance, "positive_native_food_consumption_observed": consumed,
            "native_dna_zero_amount": grant["zero_original_dna_amount"] if single else None,
            "native_dna_readback_after_first_feed": dna_readback_after_feed,
            "exact_revision_pickup_pairs": pairs,
            "source_first_fed_sample": first_source, "baseline": baseline, "shared_state": state,
            "single_owner_balance_delta_matches": balances, "pickup_projected_on_all_three": projected,
            "corpse_currently_projected_on_all_three": present_projected, "terminal_retirement": retirement,
            "pickup_projection_outcome": "retired_after_projection" if retired_projected else "present_corpse" if present_projected else "unverified",
            "bounded_observation_complete": observation_complete, "complete_prefixes_match": prefixes_match,
            "all_endpoints_connected": endpoints_connected, "observed_failures": failures,
            "native_pickup_evidence_ready": ready, "unavailable_corpse_workers": unavailable,
            "bounded_contested_pickup_observed": bool(ready and complete),
            "observed_gaps": gaps, "full_m07": "NOT_VERIFIED",
            "limits": "One original first-feed claim plus positive native nutrition in this bounded window. DNA may legitimately be zero; its exact returned delta is retained. Client food comparisons use exact source revisions, which may differ between clients. No visual, persistence, reconnect, or loss acceptance."}


def summarize(rows, markers, target, verb):
    chains = [correlate(rows[m["client"]], rows["01"], m, target, verb) for m in markers]
    native = all(c["native_damage_observed"] for c in chains) and len(chains) == 2
    state = shared_state(rows, target)
    grants = []
    scopes = {h["native_call"] for c in chains for h in c.get("native_health_decreases", []) if h["after_health"] <= 0}
    for r in rows["01"]:
        if r.get("animal_damage_scope") not in scopes: continue
        if r.get("event") == "native_owned_reward_applied":
            before, after = scalar(r.get("dna_before")), scalar(r.get("dna_after"))
            if after > before:
                grants.append({"owner": 2, "native_event": r, "before": before, "after": after, "observed_delta": after-before})
        elif r.get("event") == "native_global_dna_enter":
            returned = unique([other for other in rows["01"] if other.get("event") == "native_global_dna_return" and
                               other.get("call") == r.get("call") and other.get("animal_damage_scope") == r.get("animal_damage_scope")], "native DNA return")
            if returned and returned["sequence"] > r["sequence"]:
                before, after = scalar(r.get("before")), scalar(returned.get("after"))
                if after > before:
                    grants.append({"owner": 1, "native_event": r, "native_return": returned, "before": before, "after": after, "observed_delta": after-before})
    return {"actions": chains, "both_players_damaged_shared_npc": native, "shared_state": state,
            "native_reward_calls_in_observed_lethal_damage_scope": grants,
            "single_native_kill_reward_call_observed": len(grants) == 1,
            "npc_damaged_players": {str(c["owner"]): bool(c.get("native_npc_health_decreases_player")) for c in chains},
            "full_m07": "NOT_VERIFIED", "missing_acceptance": ["Contested native pickup", "Reconnect after death", "Configured latency/loss repeat", "Unauthorized action", "Inspected original recordings"]}


def unauthorized_evidence(client_rows, authority_rows, coordinator_rows, marker, *,
                          other_connected, prefixes_match, observation_complete):
    """A real remote ownership rejection is necessary; local refusal is not proof."""
    local = [r for r in client_rows if r["sequence"] > marker["client_before_sequence"]]
    authority = [r for r in authority_rows if r["sequence"] > marker["authority_before_sequence"]]
    result = {"client": "02", "authenticated_player": 1, "foreign_entity": marker["foreign_entity"],
              "foreign_generation": marker["foreign_generation"], "unauthorized_action_rejected": False,
              "other_client_connected": other_connected, "complete_prefixes_match": prefixes_match,
              "bounded_observation_complete": observation_complete, "full_m07": "NOT_VERIFIED"}
    intention = unique([r for r in local if r.get("event") == "network_intention" and r.get("player") == 1 and
                        r.get("entity") == marker["foreign_entity"] and r.get("verb") == VERBS["jump"] and
                        r.get("target") == 0 and r.get("target_generation") == 0], "unauthorized client intention")
    if not intention: return result
    request = m06.uint(intention.get("request"), nonzero=True)
    result["network_intention"] = intention
    rejection = unique([r for r in local if r.get("event") == "network_rejected" and r.get("request") == request and
                        r.get("player") == 1 and r.get("entity") == marker["foreign_entity"] and
                        r["sequence"] > intention["sequence"] and
                        r.get("error") == "wrong_actor_owner"], "client ownership rejection")
    matches = [r for r in coordinator_rows if r.get("request") == request and r.get("player") == 1 and
               r.get("entity") == marker["foreign_entity"] and r.get("generation") == marker["foreign_generation"] and r.get("event") == 1]
    incoming = unique([r for r in matches if r.get("outbound") is False and r.get("kind") == 10 and
                       r.get("verb") == VERBS["jump"] and r.get("target") == 0 and r.get("target_generation") == 0], "coordinator incoming unauthorized action")
    outgoing = unique([r for r in matches if r.get("outbound") is True and r.get("kind") == 3 and
                       r.get("error") == "wrong_actor_owner" and r.get("detail") == "dispatch_queued"], "coordinator dispatched ownership rejection")
    forwarded = [r for r in coordinator_rows if r.get("outbound") is True and r.get("kind") == 10 and
                 r.get("request") == request and r.get("player") == 1]
    reached = [r for r in authority if r.get("request") == request and
               ((r.get("event") == "network_native_intention" and r.get("player") == 1) or
                (r.get("event") == "network_actor_command_queued" and r.get("owner") == 1))]
    result.update(client_rejection=rejection, coordinator_incoming=incoming, coordinator_rejection=outgoing,
                  coordinator_forwarded_actions=forwarded, authority_matching_actions=reached,
                  no_authority_action_in_observed_prefix=not reached and not forwarded,
                  observation_limit="Absence is bounded to the recorded complete post-submission prefixes; no future-state or durability claim")
    result["unauthorized_action_rejected"] = bool(rejection and incoming and outgoing and
        type(incoming.get("connection")) is int and incoming["connection"] > 0 and
        incoming.get("connection") == outgoing.get("connection") and not forwarded and not reached and
        other_connected and prefixes_match and observation_complete)
    return result


def coordinator_prefix(directory):
    root = m06.service.diag.no_reparse(Path(directory).absolute())
    path = root / "coordinator.jsonl"
    if not path.is_file(): path = m06.service.diag.no_reparse(root / "private/coordinator.jsonl")
    return path, m06.bounded(path, m06.MAX_COORDINATOR, jsonl=True)


def unauthorized(items, version, seconds, coordinator_run, report):
    connected = {wid: m06.require_connected(item, version, report["commands"]) for wid, item in items.items()}
    before = {wid: value[1] for wid, value in connected.items()}
    owners = [r for r in scene(before["01"], True).values() if r["owner"] == 2 and not r["dead"]]
    if len(owners) != 1: raise ValueError("Authority must have exactly one living owner-2 actor")
    foreign = owners[0]
    for wid, rows in before.items():
        same = scene(rows, wid == "01").get(foreign["remote_entity"])
        if not same or same["owner"] != 2 or same["entity_generation"] != foreign["entity_generation"] or same["dead"]:
            raise ValueError("Foreign actor incarnation is not present on all three original machines")
        if rows[-1]["qpc"] - same["qpc"] > 3*rows[-1]["qpc_frequency"]:
            raise ValueError("Foreign actor native sample is stale")
    path, coordinator_before = coordinator_prefix(coordinator_run)
    marker = {"client_before_sequence": before["02"][-1]["sequence"], "authority_before_sequence": before["01"][-1]["sequence"],
              "foreign_entity": foreign["remote_entity"], "foreign_generation": foreign["entity_generation"]}
    report["unauthorized_marker"] = marker
    report["requested_observation_seconds"] = seconds
    report["submission"] = {"client": "02", "authenticated_player": 1, "controlled_entity_override": foreign["remote_entity"],
                            "verb": "jump", "accepted_is_queued": True, "utc_started": m06.service.diag.utc_now()}
    report["submission"]["response"] = m06.command(items["02"], "network_action", report["commands"], connected["02"][0]["epoch"],
        (1, foreign["remote_entity"], VERBS["jump"], 0, 0, 0))
    report["submission"]["utc_finished"] = m06.service.diag.utc_now()
    started = time.monotonic()
    deadline = started + seconds
    while time.monotonic() < deadline:
        time.sleep(min(.25, max(0, deadline-time.monotonic())))
    after = {wid: m06.current_rows(item, version) for wid, item in items.items()}
    _, coordinator_after = coordinator_prefix(coordinator_run)
    matched = coordinator_after.startswith(coordinator_before) and all(after[wid][:len(before[wid])] == before[wid] for wid in items)
    if not matched: raise ValueError("Evidence prefix changed during the unauthorized-action window")
    records = [m06.read_json(line) for line in coordinator_after[len(coordinator_before):].splitlines()]
    if any(not isinstance(row, dict) for row in records): raise ValueError("Malformed coordinator JSONL object")
    active_ticks = after["01"][-1]["qpc"] - before["01"][-1]["qpc"]
    source_span = active_ticks / after["01"][-1]["qpc_frequency"]
    observation_complete = time.monotonic()-started >= seconds and source_span >= max(.5, seconds-.75)
    report["observation"] = {"wall_seconds": time.monotonic()-started, "authority_trace_elapsed_seconds": source_span,
        "authority_before_sequence": marker["authority_before_sequence"], "authority_after_sequence": after["01"][-1]["sequence"],
        "client_before_sequence": marker["client_before_sequence"], "client_after_sequence": after["02"][-1]["sequence"],
        "coordinator_path": str(path), "coordinator_before_bytes": len(coordinator_before), "coordinator_after_bytes": len(coordinator_after),
        "coordinator_before_sha256": hashlib.sha256(coordinator_before).hexdigest(), "coordinator_after_sha256": hashlib.sha256(coordinator_after).hexdigest(),
        "complete_jsonl_prefix": True, "prefixes_match": matched}
    other_connected = False
    try:
        live, peer_rows = m06.require_connected(items["03"], version, report["commands"])
        report["other_client_status_after"] = live
        changes = [r for r in peer_rows if r["sequence"] > before["03"][-1]["sequence"] and
                   r.get("event") == "network_status" and r.get("state") != "connected"]
        other_connected = not changes
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        report["other_client_status_error"] = str(error)
    report["unauthorized_action"] = unauthorized_evidence(after["02"], after["01"], records, marker,
        other_connected=other_connected, prefixes_match=matched, observation_complete=observation_complete)
    if not report["unauthorized_action"]["unauthorized_action_rejected"]:
        report["observed_gap"] = "Remote ownership rejection, coordinator dispatch, uninterrupted peer, or bounded no-authority-action evidence is missing; local IPC refusal cannot satisfy this check"


def inspect(items, version):
    rows = {wid: m06.current_rows(item, version) for wid, item in items.items()}
    entries = scene(rows["01"], True)
    players = [r for r in entries.values() if r["owner"]]
    candidates, corpses = [], []
    for r in entries.values():
        if not r["owner"] and r["dead"] and pickup_fields_match(r) and r["source_fed_on"] == 0 and r["source_pickup_owner"] == 0 and r["source_food"] > 0:
            corpses.append({"entity": r["remote_entity"], "generation": r["entity_generation"], "native_id": r["native_id"],
                            "fed_on": r["source_fed_on"], "food": r["source_food"], "pickup_owner": r["source_pickup_owner"],
                            "position": r["position"], "distance_to_players": {str(p["owner"]): m06.distance(r["position"], p["position"]) for p in players}})
        if r["owner"] or r["dead"]: continue
        candidates.append({"entity": r["remote_entity"], "generation": r["entity_generation"], "native_id": r["native_id"],
                           "health": r["health"], "position": r["position"],
                           "distance_to_players": {str(p["owner"]): m06.distance(r["position"], p["position"]) for p in players}})
    candidates.sort(key=lambda r: max(r["distance_to_players"].values(), default=math.inf))
    corpses.sort(key=lambda r: max(r["distance_to_players"].values(), default=math.inf))
    return {"players": players, "living_npc_candidates": candidates, "corpse_pickup_candidates": corpses, "candidate_is_selected": False}, rows


def contested_pickup(items, version, target_id, generation, seconds, report):
    # Client readbacks can contain revisions published while another file is
    # being read. Read the source last so its prefix can contain those revisions.
    read_order = ("02", "03", "01")
    connected = {wid: m06.require_connected(items[wid], version, report["commands"]) for wid in read_order}
    before = {wid: value[1] for wid, value in connected.items()}
    target = scene(before["01"], True).get(target_id)
    if not target or target["entity_generation"] != generation or target["owner"] or not target["dead"]:
        raise ValueError("Explicit target is not the current sampled native NPC corpse incarnation")
    baseline = shared_state(before, target)
    if not baseline["npc_dead_on_all_machines"] or not all(baseline["pickup_native_state_matches"].values()) or not all(baseline["controlled_native_dna_matches"].values()):
        raise ValueError("All three scenes must agree on the native corpse and current owner DNA before pickup")
    for wid, rows in before.items():
        entries = scene(rows, wid == "01")
        local = entries[target_id]
        if local["source_fed_on"] != 0 or local["source_pickup_owner"] != 0 or local["source_food"] <= 0:
            raise ValueError("Selected native corpse is already fed on, claimed by a recorded pickup, or has no food")
        for sample in [local, *[r for r in entries.values() if r["owner"] in (1, 2)]]:
            if rows[-1]["qpc"] - sample["qpc"] > 3 * rows[-1]["qpc_frequency"]:
                raise ValueError("Selected corpse or player native sample is stale")
            if sample["owner"] and (sample["dead"] or sample["health"] <= 0):
                raise ValueError("Both original player actors must be alive before contested pickup")
    markers = [{"client": wid, "owner": m06.OWNERS[wid], "client_before_sequence": before[wid][-1]["sequence"],
                "authority_before_sequence": before["01"][-1]["sequence"]} for wid in ("02", "03")]
    report.update(target=target, encounter_markers=markers, verb="pickup", requested_observation_seconds=seconds,
                  submissions=[], evidence_read_order=list(read_order), pickup_rechecks=[], bounded_contested_pickup_observed=False)

    def submit(marker):
        commands, response, error = [], None, None
        started = m06.service.diag.utc_now()
        try:
            response = m06.command(items[marker["client"]], "network_action", commands, connected[marker["client"]][0]["epoch"],
                                   (marker["owner"], 0, VERBS["pickup"], 0, target_id, generation))
        except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as exc: error = str(exc)
        return {"client": marker["client"], "utc_started": started, "utc_finished": m06.service.diag.utc_now(),
                "response": response, "error": error, "accepted_is_queued": True}, commands

    # Two independent real client IPC submissions; native ordering is proved
    # only by authority returns, never by thread scheduling or response timing.
    with ThreadPoolExecutor(max_workers=2) as pool:
        submissions = list(pool.map(submit, markers))
    for result, commands in submissions:
        report["submissions"].append(result); report["commands"].extend(commands)
    if any(result["error"] for result, _ in submissions):
        report["observed_gap"] = "A real IPC outcome is refused or unknown; both outcomes are retained and neither request was retried"
        raise ValueError(report["observed_gap"])
    started = time.monotonic()
    deadline = started + seconds
    while True:
        rows = {wid: m06.current_rows(items[wid], version) for wid in read_order}
        report["pickup"] = summarize_pickup(rows, markers, target, before)
        if report["pickup"]["observed_failures"]:
            report["observed_gap"] = "A native lifecycle, projection, pickup or network failure ended the observation; see retained events"
            raise ValueError(report["observed_gap"])
        if report["pickup"]["native_pickup_evidence_ready"] or time.monotonic() >= deadline:
            ready_before_recheck = report["pickup"]["native_pickup_evidence_ready"]
            # Recheck current endpoints and complete prefixes in the same read
            # order. A newer unmatched readback remains unqualified; never fall
            # back to an older pair merely because it matched earlier.
            after = {wid: m06.require_connected(items[wid], version, report["commands"])[1] for wid in read_order}
            matched = all(after[wid][:len(before[wid])] == before[wid] and
                          after[wid][:len(rows[wid])] == rows[wid] for wid in read_order)
            report["pickup"] = summarize_pickup(after, markers, target, before, observation_complete=True,
                                                 prefixes_match=matched, endpoints_connected=True)
            report["pickup_rechecks"].append({"elapsed_seconds": time.monotonic()-started,
                "native_evidence_ready_before_recheck": ready_before_recheck,
                "passed": report["pickup"]["bounded_contested_pickup_observed"],
                "prefixes_match": matched, "observed_gaps": report["pickup"]["observed_gaps"]})
            if (report["pickup"]["bounded_contested_pickup_observed"] or not matched or
                    report["pickup"]["observed_failures"] or time.monotonic() >= deadline): break
            # Original log flushing can still lag a client readback. Continue
            # observation only to the original deadline; neither intention is
            # retried, and subsequent evidence must pass every existing guard.
        time.sleep(min(.5, max(0, deadline-time.monotonic())))
    report["observed_pickup_seconds"] = time.monotonic() - started
    report["observation_ended_early"] = report["observed_pickup_seconds"] < seconds
    report["bounded_contested_pickup_observed"] = report["pickup"]["bounded_contested_pickup_observed"]
    if not report["bounded_contested_pickup_observed"]:
        report["observed_gap"] = "; ".join(report["pickup"]["observed_gaps"])


def combat(items, version, target_id, generation, verb, seconds, report, b_lead_seconds=0):
    connected = {wid: m06.require_connected(item, version, report["commands"]) for wid, item in items.items()}
    before = {wid: value[1] for wid, value in connected.items()}
    target = scene(before["01"], True).get(target_id)
    if not target or target["entity_generation"] != generation or target["owner"] or target["dead"]:
        raise ValueError("Explicit target is not the current sampled living NPC incarnation")
    # Require fresh actual native samples and the same ID/generation on each
    # client before issuing either authenticated command.
    for wid, rows in before.items():
        local = scene(rows, wid == "01").get(target_id)
        if not local or local["entity_generation"] != generation or local["dead"] or local["owner"]:
            raise ValueError("All three original scenes must contain the selected living NPC")
        if rows[-1]["qpc"] - local["qpc"] > 3 * rows[-1]["qpc_frequency"]:
            raise ValueError("Selected NPC native sample is stale")
    markers = [{"client": wid, "owner": m06.OWNERS[wid], "client_before_sequence": before[wid][-1]["sequence"],
                "authority_before_sequence": before["01"][-1]["sequence"]} for wid in ("02", "03")]
    report.update(target=target, encounter_markers=markers, verb=verb, requested_observation_seconds=seconds,
                  requested_b_lead_seconds=b_lead_seconds, submissions=[])

    def submit(marker):
        commands = []
        result = None
        error = None
        started = m06.service.diag.utc_now()
        try:
            result = m06.command(items[marker["client"]], "network_action", commands, connected[marker["client"]][0]["epoch"],
                                 (marker["owner"], 0, VERBS[verb], 0, target_id, generation))
        except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as exc: error = str(exc)
        return {"client": marker["client"], "utc_started": started, "utc_finished": m06.service.diag.utc_now(),
                "response": result, "error": error, "accepted_is_queued": True}, commands

    def retain(submission, fail=True):
        result, commands = submission
        report["submissions"].append(result); report["commands"].extend(commands)
        if fail and result["error"]:
            raise ValueError("One client submission failed or is uncertain; neither request was retried")

    if b_lead_seconds:
        # Only authenticated original actor intentions change. NPC targeting is
        # still chosen by the original AI; no target or damage is manufactured.
        retain(submit(markers[1]))
        lead_started = time.monotonic()
        deadline = lead_started + b_lead_seconds
        while time.monotonic() < deadline:
            time.sleep(min(.25, max(0, deadline-time.monotonic())))
        report["observed_b_lead_seconds"] = time.monotonic()-lead_started
        for wid, item in items.items():
            _, live_rows = m06.require_connected(item, version, report["commands"])
            current = scene(live_rows, wid == "01").get(target_id)
            if not current or current["entity_generation"] != generation or current["dead"]:
                raise ValueError("NPC unavailable after B's lead; A was not submitted")
        retain(submit(markers[0]))
    else:
        # Independent IPC submissions are not a claim of simultaneous native
        # application or measured network latency.
        with ThreadPoolExecutor(max_workers=2) as pool:
            submissions = list(pool.map(submit, markers))
        for submission in submissions: retain(submission, fail=False)
        if any(result["error"] for result, _ in submissions):
            raise ValueError("One client submission failed or is uncertain; neither request was retried")
    deadline = time.monotonic() + seconds
    while True:
        rows = {wid: m06.current_rows(item, version) for wid, item in items.items()}
        report["encounter"] = summarize(rows, markers, target, verb)
        if time.monotonic() >= deadline: break
        time.sleep(min(.5, max(0, deadline-time.monotonic())))
    report["bounded_encounter_observed"] = bool(report["encounter"]["both_players_damaged_shared_npc"] and
        report["encounter"]["shared_state"]["npc_dead_on_all_machines"] and
        all(report["encounter"]["shared_state"]["controlled_native_dna_matches"].values()) and
        report["encounter"]["single_native_kill_reward_call_observed"])
    if not report["bounded_encounter_observed"]:
        report["observed_gap"] = "See separate request/target/actual-health/reward/shared-state fields; a queued request alone does not complete the encounter"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("inspect", "shared-combat", "contested-pickup", "unauthorized-action", "archive"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--bridge-version", default="0.0.31")
    parser.add_argument("--coordinator-run", type=Path)
    parser.add_argument("--from-snapshot", type=Path)
    parser.add_argument("--target", type=int)
    parser.add_argument("--generation", type=int)
    parser.add_argument("--verb", choices=("attack", "engage"), default="engage")
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--b-lead-seconds", type=float, default=0,
                        help="Let B engage first for 0..10 seconds; original AI chooses its response")
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or not 1 <= args.seconds <= 45: parser.error("--seconds must be 1..45")
    if not math.isfinite(args.b_lead_seconds) or not 0 <= args.b_lead_seconds <= 10: parser.error("--b-lead-seconds must be 0..10")
    if args.b_lead_seconds and args.operation != "shared-combat": parser.error("--b-lead-seconds requires shared-combat")
    if args.operation in ("shared-combat", "contested-pickup") and (not args.target or not args.generation): parser.error(args.operation + " requires --target and --generation from inspect")
    if args.operation == "unauthorized-action" and not args.coordinator_run: parser.error("unauthorized-action requires --coordinator-run for actual rejection evidence")
    if args.from_snapshot and args.operation != "archive": parser.error("--from-snapshot is only accepted by archive")
    for value in (args.target, args.generation):
        if value is not None:
            try: m06.uint(value, nonzero=True)
            except ValueError: parser.error("Invalid unsigned target/generation")
    output = m06.service.diag.no_reparse(args.output.absolute())
    output.mkdir(parents=True, exist_ok=False)
    report = {"schema_version": 1, "evidence_class": CLASS, "operation": args.operation, "utc_started": m06.service.diag.utc_now(),
              "expected_bridge_version": args.bridge_version, "operation_completed": False, "full_m07": "NOT_VERIFIED",
              "commands": [], "snapshots": {}, "native_visual_acceptance": "SEPARATE_INSPECTION_REQUIRED"}
    items = {}
    try:
        frozen = None
        if args.from_snapshot:
            prior = m06.read_json(m06.bounded(args.from_snapshot, 16*1024*1024))
            if prior.get("schema_version") != 1 or prior.get("evidence_class") not in (CLASS, "ORIGINAL_M06_PROCESS_HARNESS"):
                raise ValueError("Unknown prior process snapshot")
            snapshots = prior["snapshots"]
            frozen = snapshots.get("after", snapshots.get("snapshot", snapshots.get("before")))["workers"]
        for wid in m06.OWNERS: items[wid] = m06.source(wid, frozen[wid]["current"] if frozen else None)
        if args.operation in ("shared-combat", "contested-pickup", "unauthorized-action"):
            report["snapshots"]["before"] = m06.archive(output/"before", items, args.bridge_version, args.coordinator_run)
            if args.operation == "shared-combat": combat(items, args.bridge_version, args.target, args.generation, args.verb, args.seconds, report, args.b_lead_seconds)
            elif args.operation == "contested-pickup": contested_pickup(items, args.bridge_version, args.target, args.generation, args.seconds, report)
            else: unauthorized(items, args.bridge_version, args.seconds, args.coordinator_run, report)
        else:
            report["snapshots"]["snapshot"] = m06.archive(output/"snapshot", items, args.bridge_version, args.coordinator_run)
            report["inspection"], _ = inspect(items, args.bridge_version)
        report["operation_completed"] = True
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
    finally:
        if args.operation in ("shared-combat", "contested-pickup", "unauthorized-action") and items:
            try: report["snapshots"]["after"] = m06.archive(output/"after", items, args.bridge_version, args.coordinator_run)
            except (OSError, ValueError, KeyError, TypeError) as error: report["archive_error"] = str(error)
        report["utc_finished"] = m06.service.diag.utc_now()
        report["harness_sha256"] = m06.service.diag.fingerprint(Path(__file__))["sha256"]
        report["m06_helpers_sha256"] = m06.service.diag.fingerprint(Path(m06.__file__))["sha256"]
        m06.service.diag.write_json(output/"report.json", report)
    print(json.dumps({"operation_completed": report["operation_completed"], "bounded_encounter_observed": report.get("bounded_encounter_observed"),
                      "bounded_contested_pickup_observed": report.get("bounded_contested_pickup_observed"),
                      "unauthorized_action_rejected": report.get("unauthorized_action", {}).get("unauthorized_action_rejected"),
                      "full_m07": "NOT_VERIFIED", "report": str(output/"report.json"), "error": report.get("error")}))
    return 0 if report["operation_completed"] else 1


if __name__ == "__main__": raise SystemExit(main())
