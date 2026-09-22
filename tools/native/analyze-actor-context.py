"""Audit native M03 call nesting without converting diagnostics into acceptance."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path

PAIRS = {
    "native_ability": ("actor",),
    "native_ability_select": ("actor",),
    "native_strike": ("actor", "target"),
    "native_animal_damage": ("receiver", "attacker"),
    "native_damage": ("receiver", "attacker"),
    "native_target": ("actor",),
    "native_global_dna": ("avatar",),
    "native_attack_stop": ("actor", "target"),
}
SCOPES = {"ability_scope": "native_ability", "strike_scope": "native_strike",
          "animal_damage_scope": "native_animal_damage"}
PINNED_EXE = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37"
PINNED_SDK = "cbf9206b9a823f0911cd9be0217104a49d72380b"
MAX_TRACE = 66 * 1024 * 1024


def analyze(path):
    with Path(path).open("rb") as stream:
        raw = stream.read(MAX_TRACE + 1)
    if len(raw) > MAX_TRACE:
        raise ValueError("Actor trace exceeds the bounded probe size")
    errors = []
    events = []
    def reject_constant(value):
        raise ValueError(f"non-finite JSON constant {value}")
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON field {key}")
            result[key] = value
        return result
    for number, line in enumerate(raw.decode("utf-8-sig").splitlines(), 1):
        try:
            row = json.loads(line, parse_constant=reject_constant, object_pairs_hook=unique_object)
            if not isinstance(row, dict):
                raise ValueError("record must be an object")
            if not isinstance(row.get("event"), str):
                raise ValueError("event must be a string")
            for key in ("sequence", "pid", "thread_id", "qpc", "qpc_frequency", "epoch", "executing_command", "foreign_callbacks"):
                if type(row.get(key)) is not int or row[key] < 0:
                    raise ValueError(f"invalid integer {key}")
            for key in ("call", "actor", "target", "receiver", "attacker", "owner", "attacker_owner", "receiver_owner"):
                if key in row and (type(row[key]) is not int or row[key] < 0):
                    raise ValueError(f"invalid identity {key}")
            if any(isinstance(v, float) and not math.isfinite(v) for v in row.values()):
                raise ValueError("non-finite scalar")
            events.append(row)
        except (ValueError, TypeError) as error:
            errors.append(f"line {number}: {error}")
    counts = Counter(r.get("event") for r in events)
    first = events[0] if events else {}
    last = events[-1] if events else {}
    if counts["trace_start"] != 1 or first.get("event") != "trace_start":
        errors.append("missing/duplicated/misplaced trace_start")
    if counts["trace_stop"] != 1 or last.get("event") != "trace_stop" or last.get("healthy") is not True or last.get("detach_status") != 0:
        errors.append("missing or unhealthy terminal record")
    native = first.get("evidence_class") == "NATIVE_PROBE"
    if native and (first.get("executable_sha256") != PINNED_EXE or first.get("sdk_commit") != PINNED_SDK):
        errors.append("native executable or SDK differs from the pinned profile")
    has_context = any(r.get("event") == "harness_ready" and r.get("combat_context_schema") == 1 for r in events)
    if has_context and counts["context_bindings_checked"] != 1:
        errors.append("context observer ABI guard evidence missing")
    pending = {}
    seen_calls = set()
    stats = defaultdict(Counter)
    rewards = []
    contexts = {}
    last_damage = {}
    hits = []
    previous_qpc = -1
    for ordinal, row in enumerate(events, 1):
        event = row.get("event", "")
        seq = row.get("sequence")
        if seq != ordinal:
            errors.append(f"record {ordinal}: non-contiguous sequence {seq}")
        if any(row.get(k) != first.get(k) for k in ("pid", "thread_id", "qpc_frequency", "evidence_class", "harness")):
            errors.append(f"record {ordinal}: mixed process/thread/schema identity")
        qpc = row.get("qpc")
        if not isinstance(qpc, int) or qpc < previous_qpc or not isinstance(row.get("qpc_frequency"), int) or row["qpc_frequency"] <= 0:
            errors.append(f"record {ordinal}: invalid clock")
        else:
            previous_qpc = qpc
        if row.get("foreign_callbacks") != 0:
            errors.append(f"record {ordinal}: unobserved foreign callbacks")
        pair = None
        suffix = None
        for ending in ("_enter", "_return"):
            if event.endswith(ending) and event[:-len(ending)] in PAIRS:
                pair, suffix = event[:-len(ending)], ending
        if pair == "native_global_dna" and not has_context:
            pair = suffix = None  # Older traces did not assign DNA call IDs.
        call = row.get("call")
        if suffix == "_enter":
            if not isinstance(call, int) or isinstance(call, bool) or call <= 0 or call in seen_calls:
                errors.append(f"record {ordinal}: missing/reused call ID")
            else:
                seen_calls.add(call)
                pending[call] = (pair, row)
        if has_context:
            for field, kind in SCOPES.items():
                scope = row.get(field)
                if not isinstance(scope, int) or isinstance(scope, bool) or scope < 0:
                    errors.append(f"record {ordinal}: invalid {field}")
                elif scope and (scope not in pending or pending[scope][0] != kind or pending[scope][1].get("epoch") != row.get("epoch")):
                    errors.append(f"record {ordinal}: orphaned or wrong-kind {field}")
        if suffix == "_return":
            entry = pending.pop(call, None)
            if not entry or entry[0] != pair:
                errors.append(f"record {ordinal}: unmatched native return")
            elif entry[1].get("epoch") != row.get("epoch") or any(entry[1].get(k) != row.get(k) for k in PAIRS[pair]):
                errors.append(f"record {ordinal}: changed native call identity")
            if has_context and entry and any(entry[1].get(k) != row.get(k) for k in SCOPES):
                errors.append(f"record {ordinal}: changed native call nesting")
            if pair == "native_damage" and entry:
                last_damage[(row.get("epoch"), row.get("receiver"))] = entry[1]
        actor = str(row.get("actor", 0))
        if event == "native_ability_enter":
            stats[actor]["ability_requests"] += 1
        elif event == "native_ability_select_return":
            stats[actor]["selection_rejected" if row.get("result_index") == -1 else "selection_returned_index"] += 1
        elif event == "native_strike_enter":
            stats[actor]["effect_dispatches"] += 1
        elif event.endswith("combat_context") or event in ("sampled_attack_context", "strike_actor_context", "strike_target_context", "ability_return_context"):
            contexts[actor] = {k: row.get(k) for k in ("owner", "flags", "native_player_owned", "herd_owned_by_avatar", "herd_damage_multiplier")}
            if row.get("animation_marker") == 0x7373696d:
                stats[actor]["sampled_miss_markers"] += 1
            if row.get("animation_marker") == 0x746968:
                stats[actor]["sampled_hit_markers"] += 1
        elif event == "damage_state":
            entry = last_damage.pop((row.get("epoch"), row.get("actor")), None)
            if entry and isinstance(entry.get("receiver_health"), (float, int)) and isinstance(row.get("health"), (float, int)):
                delta = entry["receiver_health"] - row["health"]
                if delta > 0 and entry.get("attacker", 0):
                    hits.append({"call": entry["call"], "attacker": entry["attacker"], "receiver": row["actor"],
                                 "health_decrease": delta, "strike_scope": entry.get("strike_scope"),
                                 "animal_damage_scope": entry.get("animal_damage_scope")})
        elif event == "native_global_dna_enter":
            rewards.append({k: row.get(k) for k in ("sequence", "amount", "avatar", "before", "caller_rva", "strike_scope", "animal_damage_scope")})
        if event == "scene_exit" and pending:
            errors.append(f"record {ordinal}: scene exit inside an unfinished native call")
    if pending:
        errors.append("unfinished native calls at end of trace")
    return {"trace": str(path), "sha256": hashlib.sha256(raw).hexdigest(), "evidence_class": first.get("evidence_class"),
            "records": len(events), "structural_valid": not errors, "errors": list(dict.fromkeys(errors)),
            "context_observers_present": has_context, "actors": dict(stats), "last_sampled_context": contexts,
            "observed_actor_health_decreases": hits, "global_dna_calls": rewards,
            "native_acceptance": "NOT_VERIFIED",
            "limits": ["Sampled animation markers are not a complete decision history or hit outcomes.",
                       "Native strike return values alone do not establish damage.",
                       "A global DNA call identifies a campaign avatar, not independent owner progression.",
                       "Visual behavior, safe native lifecycle and isolated saves need separate evidence."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--require-context", action="store_true")
    args = parser.parse_args()
    report = analyze(args.trace)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    print(json.dumps({k: report[k] for k in ("records", "structural_valid", "context_observers_present", "native_acceptance")}))
    return 0 if report["structural_valid"] and (report["context_observers_present"] or not args.require_context) else 1


if __name__ == "__main__":
    raise SystemExit(main())
