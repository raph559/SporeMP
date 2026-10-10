"""Inspect the pinned social reward caller in closed actor traces; never certify M11."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "sporemp_actor_context_audit", Path(__file__).with_name("analyze-actor-context.py")
)
_context = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_context)

# Pinned PE32 preferred VA D9BD16 calls original AddEvolutionPoints D2E8A0.
# The return address is D9BD1B; subtract preferred image base 400000.
# Static caller D9BBD0 passes true to C042A0 and uses species relationships.
# This identifies a candidate call path, not a proven social outcome or owner.
SOCIAL_REWARD_CALLER_RVA = 0x99BD1B
SOCIAL_ACTION = 0x060B4123
SOCIAL_ACTION_CALLER_RVA = 0x99BD64
PROGRESS_FIELDS = ("dna", "goal", "combat", "social")


def finite_number(value):
    if type(value) not in (int, float):
        return False
    try:
        return math.isfinite(value)
    except OverflowError:
        return False


def action_observations(events, errors):
    """Pair the observed action dispatch, retaining its limited attribution."""
    schema = any(row.get("event") == "harness_ready" and
                 type(row.get("social_context_schema")) is int and
                 row["social_context_schema"] == 1 for row in events)
    stack, seen, result = [], set(), []
    identities = ("payload_actor_native_id", "payload_target_native_id",
                  "primary_player_native_id", "secondary_player_native_id")
    for row in events:
        kind = row["event"]
        if kind == "scene_exit" and stack:
            errors.append("scene exit inside an unfinished social action")
        if kind not in ("native_social_action_enter", "native_social_action_return"):
            continue
        call = row.get("social_call")
        if not schema or type(call) is not int or call <= 0:
            errors.append("social action has missing schema or invalid call identity")
            continue
        if (type(row.get("action")) is not int or row["action"] != SOCIAL_ACTION or
                type(row.get("caller_rva")) is not int or row["caller_rva"] != SOCIAL_ACTION_CALLER_RVA or
                row.get("resolved") is not True or type(row.get("secondary_present")) is not bool):
            errors.append(f"social call {call}: unresolved or unsupported dispatch")
            continue
        if (any(type(row.get(key)) is not int or not 0 <= row[key] <= 0xFFFFFFFF for key in identities) or
                any(row[key] == 0xFFFFFFFF for key in identities[:3]) or
                row[identities[0]] == row[identities[1]] or
                (row["secondary_present"] and (row[identities[3]] == 0xFFFFFFFF or row[identities[2]] == row[identities[3]])) or
                (not row["secondary_present"] and row[identities[3]] != 0xFFFFFFFF)):
            errors.append(f"social call {call}: invalid native roots")
            continue
        fields = tuple(f"{owner}_{field}" for owner in ("primary", "secondary") for field in PROGRESS_FIELDS)
        if (any(not finite_number(row.get(key)) for key in fields) or
                (not row["secondary_present"] and any(row[f"secondary_{field}"] != 0 for field in PROGRESS_FIELDS))):
            errors.append(f"social call {call}: invalid progress observation")
            continue
        if kind.endswith("_enter"):
            if call in seen:
                errors.append(f"social call {call}: reused call identity")
            seen.add(call)
            stack.append(row)
            continue
        entry = stack.pop() if stack else None
        if (not entry or entry["social_call"] != call or entry["epoch"] != row["epoch"] or
                entry["secondary_present"] != row["secondary_present"] or
                any(entry[key] != row[key] for key in identities) or
                any(entry.get(key) != row.get(key) for key in (*_context.SCOPES, "executing_command"))):
            errors.append(f"social call {call}: unmatched return or changed native roots")
            continue
        delta = {key: row[key] - entry[key] for key in fields}
        if any(not math.isfinite(value) for value in delta.values()):
            errors.append(f"social call {call}: non-finite progress difference")
            continue
        result.append({
            "call": call, "epoch": entry["epoch"],
            "enter_sequence": entry["sequence"], "return_sequence": row["sequence"],
            **{key: entry[key] for key in identities},
            "secondary_present": entry["secondary_present"],
            "progress_before": {key: entry[key] for key in fields},
            "progress_after": {key: row[key] for key in fields}, "immediate_delta": delta,
            "ownership": "NOT_ESTABLISHED",
        })
    if stack:
        errors.append("unfinished social action at end of trace")
    return result


def owned_result_observations(events, errors):
    """Observe the whole original result; do not certify relationships or UI."""
    schema = any(row.get("event") == "harness_ready" and
                 type(row.get("owned_social_result_schema")) is int and
                 row["owned_social_result_schema"] == 1 for row in events)
    pending, seen, result = None, set(), []
    identities = ("actor_native_id", "target_native_id", "primary_player_native_id",
                  "secondary_player_native_id")
    fields = tuple(f"{owner}_{field}" for owner in ("primary", "secondary") for field in PROGRESS_FIELDS)
    children = []
    for row in events:
        kind = row["event"]
        if pending and kind in ("scene_exit", "trace_stop"):
            errors.append("unfinished owned social result at scene/trace exit")
        if kind in ("native_owned_social_reward_applied", "native_owned_social_action_applied"):
            if pending:
                children.append(row)
            elif schema:
                errors.append("owned social reward/action outside a complete result scope")
            continue
        if kind not in ("native_social_owner_enter", "native_social_owner_return"):
            continue
        call = row.get("result_call")
        if (not schema or type(call) is not int or not 0 < call <= 0xFFFFFFFFFFFFFFFF or
                row.get("resolved") is not True or type(row.get("owner")) is not int or row["owner"] != 2 or
                type(row.get("actor")) is not int or not 0 < row["actor"] <= 0xFFFFFFFFFFFFFFFF or
                any(type(row.get(key)) is not int or not 0 <= row[key] < 0xFFFFFFFF for key in identities) or
                row.get("actor_native_id") == row.get("target_native_id") or
                row.get("primary_player_native_id") == row.get("secondary_player_native_id") or
                type(row.get("animation")) is not int or not 0 <= row["animation"] <= 0xFFFFFFFF or
                any(not finite_number(row.get(key)) for key in fields)):
            errors.append(f"owned social result {call}: invalid schema, roots or progress")
            continue
        if kind.endswith("_enter"):
            if pending or call in seen or row["animation"] != 0xFFFFFFFF:
                errors.append(f"owned social result {call}: overlapping or reused result")
            seen.add(call)
            pending, children = row, []
            continue
        entry, pending = pending, None
        if (not entry or entry["result_call"] != call or
                any(entry.get(key) != row.get(key) for key in
                    (*identities, "actor", "epoch", *_context.SCOPES, "executing_command"))):
            errors.append(f"owned social result {call}: unmatched return or changed identity")
            continue
        delta = {key: row[key] - entry[key] for key in fields}
        if any(not math.isfinite(value) for value in delta.values()):
            errors.append(f"owned social result {call}: non-finite progress difference")
            continue
        rewards = [child for child in children if child["event"] == "native_owned_social_reward_applied"]
        actions = [child for child in children if child["event"] == "native_owned_social_action_applied"]
        if any(child.get("owner") != 2 or type(child.get("owner")) is not int or
               any(type(child.get(key)) is not int or child[key] != entry[key]
                   for key in ("actor", "actor_native_id", "target_native_id", "epoch")) or
               not finite_number(child.get("amount")) or child["amount"] < 0 for child in children):
            errors.append(f"owned social result {call}: reward/action identity or amount mismatch")
            continue
        dna = delta["secondary_dna"]
        isolated = (len(rewards) == len(actions) == 1 and
                    all(delta[f"primary_{field}"] == 0 for field in PROGRESS_FIELDS) and
                    delta["secondary_combat"] == 0 and dna > 0 and
                    all(math.isclose(value, dna, rel_tol=1e-6, abs_tol=1e-5) for value in
                        (delta["secondary_goal"], delta["secondary_social"], rewards[0]["amount"])))
        result.append({"call": call, "epoch": entry["epoch"], "actor": entry["actor"],
                       **{key: entry[key] for key in identities},
                       "enter_sequence": entry["sequence"], "return_sequence": row["sequence"],
                       "animation": row["animation"], "progress_before": {key: entry[key] for key in fields},
                       "progress_after": {key: row[key] for key in fields}, "immediate_delta": delta,
                       "reward_calls": len(rewards), "action_calls": len(actions),
                       "progress_isolated": isolated, "native_acceptance": "NOT_VERIFIED"})
    if pending:
        errors.append("unfinished owned social result at end of trace")
    return result


def objective_observations(events, owned, errors):
    """Keep delayed species awards separate from the individual result."""
    fields = tuple(f"{owner}_{field}" for owner in ("primary", "secondary") for field in PROGRESS_FIELDS)
    raw_fields = "fields_7c_8c_90_94_98_9c_a0_a4_a8_b0_bc_c0"
    pending, seen, result, claims, awards = None, set(), [], [], []
    transitions = {}
    for row in events:
        kind = row["event"]
        if kind == "native_social_relationship_transition":
            if any(type(row.get(k)) is not int or not 0 < row[k] <= 0xFFFFFFFFFFFFFFFF
                   for k in ("epoch", "actor", "result_call")):
                errors.append("invalid social relationship transition identity")
                continue
            key = (row.get("epoch"), row.get("actor"), row.get("result_call"))
            if key in transitions:
                errors.append("duplicate social relationship transition")
            transitions[key] = row
        if kind in ("native_social_objective_owner_claimed", "native_owned_social_objective_award"):
            if not pending or any(type(row.get(k)) is not int or row[k] != pending.get(k)
                                  for k in ("call", "actor", "epoch")):
                errors.append("objective claim/award outside its matching callback")
            else:
                (claims if kind.endswith("_claimed") else awards).append(row)
            continue
        if kind not in ("native_social_objective_enter", "native_social_objective_return"):
            if pending and kind in ("scene_exit", "trace_stop"):
                errors.append("unfinished social objective at scene/trace exit")
            continue
        call = row.get("call")
        if (type(call) is not int or not 0 < call <= 0xFFFFFFFFFFFFFFFF or
                any(type(row.get(k)) is not int or not 0 < row[k] <= 0xFFFFFFFFFFFFFFFF
                    for k in ("actor", "recent_result")) or
                type(row.get("vtable_rva")) is not int or row["vtable_rva"] != 0x106B3C0 or
                type(row.get("species")) is not list or len(row["species"]) != 3 or
                any(type(v) is not int or not 0 <= v <= 0xFFFFFFFF for v in row["species"]) or
                type(row.get(raw_fields)) is not list or len(row[raw_fields]) != 12 or
                any(type(v) is not int or not 0 <= v <= 0xFFFFFFFF for v in row[raw_fields]) or
                any(not finite_number(row.get(k)) for k in fields)):
            errors.append(f"objective {call}: invalid receiver, species, transition or progress")
            continue
        if kind.endswith("_enter"):
            if pending or call in seen or row.get("caller_rva") != 0x82FBD4:
                errors.append(f"objective {call}: overlapping, reused or unsupported callback")
            seen.add(call); pending, claims, awards = row, [], []
            continue
        entry, pending = pending, None
        if (not entry or any(entry.get(k) != row.get(k) for k in
                            ("call", "actor", "epoch", "recent_result", "species", "vtable_rva",
                             *_context.SCOPES, "executing_command"))):
            errors.append(f"objective {call}: unmatched return or changed identity")
            continue
        source = next((r for r in owned if r["call"] == entry["recent_result"] and
                       r["epoch"] == entry["epoch"] and r["actor"] == entry["actor"] and
                       r["return_sequence"] < entry["sequence"]), None)
        if not source:
            errors.append(f"objective {call}: missing preceding owned result")
            continue
        delta = {k: row[k] - entry[k] for k in fields}
        if any(not math.isfinite(v) for v in delta.values()):
            errors.append(f"objective {call}: non-finite progress difference")
            continue
        transition = transitions.get((entry["epoch"], entry["actor"], entry["recent_result"]))
        before, after = entry[raw_fields], row[raw_fields]
        observed_transition = (transition is not None and transition.get("before_valid") is True and
            transition.get("after_valid") is True and transition.get("objective_receipt_pending") is True and
            all(type(transition.get(k)) is int for k in ("count_before", "count_after", "relation_before", "relation_after")) and
            transition["sequence"] < entry["sequence"] and transition["count_before"] == before[1] and
            transition["count_after"] == after[1] == before[1] + 1 == before[10] and
            transition["relation_before"] == before[6] != 6 and transition["relation_after"] == after[6] == 6)
        child = awards[0] if len(awards) == 1 else {}
        claim = claims[0] if len(claims) == 1 else {}
        amount = child.get("amount")
        isolated = (source["progress_isolated"] and observed_transition and
            claim.get("one_shot") is True and type(claim.get("result_call")) is int and
            claim["result_call"] == entry["recent_result"] and
            claim.get("count_before") == before[1] and claim.get("count_after") == after[1] and
            finite_number(amount) and amount > 0 and amount == after[5] and
            all(delta[f"primary_{f}"] == 0 for f in PROGRESS_FIELDS) and delta["secondary_combat"] == 0 and
            all(math.isclose(delta[f"secondary_{f}"], amount, rel_tol=1e-6, abs_tol=1e-5)
                for f in ("dna", "goal", "social")))
        result.append(dict(call=call, result_call=entry["recent_result"], epoch=entry["epoch"], actor=entry["actor"],
            enter_sequence=entry["sequence"], return_sequence=row["sequence"], immediate_delta=delta,
            progress_isolated=bool(isolated), relationship_transition_observed=bool(observed_transition),
            claim_calls=len(claims), award_calls=len(awards), native_acceptance="NOT_VERIFIED"))
    if pending:
        errors.append("unfinished social objective at end of trace")
    return result


def analyze(path):
    path = Path(path)
    context = _context.analyze(path)
    errors = list(context["errors"])
    with path.open("rb") as stream:
        raw = stream.read(_context.MAX_TRACE + 1)
    if len(raw) > _context.MAX_TRACE or hashlib.sha256(raw).hexdigest() != context["sha256"]:
        raise ValueError("Trace changed during analysis or exceeds the probe bound")
    if not context["context_observers_present"]:
        errors.append("paired DNA observer schema is missing")
    observations, actions, owned, objectives = [], [], [], []
    # The shared validator checks strict JSON, complete lifecycle, process/thread,
    # pinned native provenance, scene epochs, unique call IDs and matching returns.
    # Do not reduce an invalid trace into apparently usable observations.
    if not errors:
        events = [json.loads(line) for line in raw.decode("utf-8-sig").splitlines()]
        actions = action_observations(events, errors)
        owned = owned_result_observations(events, errors)
        objectives = objective_observations(events, owned, errors)
        pending = {}
        for row in events:
            if row["event"] == "native_global_dna_enter" and row.get("caller_rva") == SOCIAL_REWARD_CALLER_RVA:
                pending[row["call"]] = row
            elif row["event"] == "native_global_dna_return" and row["call"] in pending:
                entry = pending.pop(row["call"])
                values = (entry.get("amount"), entry.get("before"), row.get("after"))
                if any(not finite_number(value) for value in values):
                    errors.append(f"call {row['call']}: missing or non-finite DNA scalar")
                    continue
                avatar = entry.get("avatar")
                if type(avatar) is not int or avatar <= 0:
                    errors.append(f"call {row['call']}: no resolved campaign avatar")
                    continue
                amount, before, after = values
                delta = after - before
                if not math.isfinite(delta):
                    errors.append(f"call {row['call']}: non-finite DNA difference")
                    continue
                observations.append({
                    "call": row["call"], "epoch": entry["epoch"],
                    "enter_sequence": entry["sequence"], "return_sequence": row["sequence"],
                    "avatar": avatar, "requested_amount": amount,
                    "global_dna_before": before, "global_dna_after": after,
                    "global_dna_delta_at_return": delta,
                    "ownership": "NOT_ESTABLISHED",
                })
    if errors:
        observations = []
        actions = []
        owned = []
        objectives = []
    return {
        "trace": str(path), "sha256": context["sha256"],
        "evidence_class": context["evidence_class"],
        "structural_valid": not errors, "errors": list(dict.fromkeys(errors)),
        "candidate_caller_rva": SOCIAL_REWARD_CALLER_RVA,
        "candidate_calls": len(observations), "observations": observations,
        "action_calls": len(actions), "action_observations": actions,
        "owned_result_calls": len(owned), "owned_results": owned,
        "owned_progress_isolated": sum(row["progress_isolated"] for row in owned),
        "objective_calls": len(objectives), "objectives": objectives,
        "objective_progress_isolated": sum(row["progress_isolated"] for row in objectives),
        "native_acceptance": "NOT_VERIFIED", "m11_status": "IN_PROGRESS",
        "limits": [
            "The caller classification is static evidence pending an inspected original social interaction.",
            "A returned global DNA call may enqueue a later award; its immediate delta is not the completed reward.",
            "The campaign avatar is not proof of the initiating player, independent species ownership or relationship outcome.",
            "The social action payload is observed after the original relationship update; it is not a before/after relationship comparison.",
            "Whole-result progress isolation does not qualify native relationships, shared objectives, tutorials, concurrency, persistence or network controls.",
            "Delayed objective isolation requires its own matched native relationship transition and complete callback; it does not certify default distinct-species goals.",
            "Trace headers and parser success do not establish provenance, visual behavior, protected saves or M11 acceptance.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--require-social", action="store_true",
                        help="Fail when no complete call from the candidate social reward path is observed.")
    parser.add_argument("--require-action", action="store_true",
                        help="Also require a complete, root-resolved original social action dispatch.")
    parser.add_argument("--require-owned", action="store_true",
                        help="Require a complete B result with one reward/action, matching positive B progress and unchanged A progress.")
    parser.add_argument("--require-objective", action="store_true",
                        help="Require a separately matched delayed objective with positive B progress and unchanged A progress.")
    args = parser.parse_args()
    report = analyze(args.trace)
    if args.output.resolve() == args.trace.resolve():
        raise ValueError("The report must not overwrite the input trace")
    # Keep both existing evidence and the input trace immutable.
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    print(json.dumps({key: report[key] for key in
                      ("structural_valid", "candidate_calls", "action_calls", "owned_result_calls", "owned_progress_isolated", "objective_calls", "objective_progress_isolated", "native_acceptance")}))
    return 0 if (report["structural_valid"] and (report["candidate_calls"] or not args.require_social)
                 and (report["action_calls"] or not args.require_action)
                 and (report["owned_progress_isolated"] or not args.require_owned)
                 and ((report["objective_calls"] > 0 and
                       report["objective_progress_isolated"] == report["objective_calls"])
                      or not args.require_objective)) else 1


if __name__ == "__main__":
    raise SystemExit(main())
