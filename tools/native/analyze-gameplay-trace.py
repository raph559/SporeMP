"""Read-only M02 JSONL validation. Log structure is never native behavior acceptance."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import sys

SDK = "cbf9206b9a823f0911cd9be0217104a49d72380b"
EXE = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37"
CREATURE_MODE = 0x1654C01
ENGINE = {"scene_enter", "scene_exit", "entity_created", "entity_invalidated",
          "avatar_assigned", "jump_enter", "jump_return", "jump_land"}
EVENTS = ENGINE | {"trace_start", "trace_stop", "hooks_ready", "hooks_failed", "stage_observed",
                   "entity_observed", "avatar_state", "ai_summary", "diagnostic_snapshot", "overflow"}
INTEGER = {"schema_version", "sequence", "qpc", "qpc_frequency", "pid", "thread_id", "scene_epoch",
           "entity_id", "action_id", "parent_action_id", "native_id", "political_id", "type_id",
           "mode_id", "result", "argument", "count", "secondary_count", "foreign_callbacks", "lost_records"}
FIELDS = INTEGER | {"evidence_class", "source", "event", "has_state", "health", "energy", "hunger", "dead",
                    "multiplayer_mutations", "executable_sha256", "sdk_commit", "bridge_version"}


def no_duplicates(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError(f"duplicate JSON field: {key}")
        value[key] = item
    return value


def read_records(path: Path):
    if path.stat().st_size > 70 * 1024 * 1024:
        raise ValueError("trace exceeds the bounded probe size")
    with path.open("r", encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            if len(line) > 4096 or not line.endswith("\n"):
                raise ValueError(f"line {line_number}: oversized or truncated record")
            try:
                yield json.loads(line, object_pairs_hook=no_duplicates,
                                 parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f"invalid number: {value}")))
            except (ValueError, json.JSONDecodeError) as error:
                raise ValueError(f"line {line_number}: {error}") from error


def analyze(records, require_native: bool = False):
    events = Counter()
    started = None
    previous_qpc = -1
    expected_epoch = 1
    alive = set()
    all_ids = set()
    created_ids = set()
    pending = {}
    action_ids = set()
    accepted = set()
    completed = 0
    landed = set()
    issues = []
    total = 0
    last = None
    max_foreign = max_lost = 0
    npc_ticks = avatar_ticks = 0
    stages = set()
    incomplete_state = 0
    for total, row in enumerate(records, 1):
        if not isinstance(row, dict) or set(row) != FIELDS:
            raise ValueError(f"record {total}: unknown or missing fields (raw pointers/containers are not allowed)")
        for field in INTEGER:
            signed = field in {"argument", "result"}
            if type(row[field]) is not int or not ((-2**31 if signed else 0) <= row[field] <= (2**31-1 if signed else 2**64-1)):
                raise ValueError(f"record {total}: invalid integer {field}")
        for field in FIELDS - INTEGER - {"has_state", "dead", "multiplayer_mutations", "health", "energy", "hunger"}:
            if type(row[field]) is not str or len(row[field]) > 128:
                raise ValueError(f"record {total}: invalid text {field}")
        for field in ("has_state", "dead", "multiplayer_mutations"):
            if type(row[field]) is not bool:
                raise ValueError(f"record {total}: invalid boolean {field}")
        for field in ("health", "energy", "hunger"):
            number = row[field]
            if number is not None and (type(number) not in (int, float) or not abs(number) <= 3.402824e38 or not math.isfinite(number)):
                raise ValueError(f"record {total}: invalid scalar {field}")
            if not row["has_state"] and number is not None:
                raise ValueError(f"record {total}: state scalar present without a state sample")
        if row["has_state"] and any(row[field] is None for field in ("health", "energy", "hunger")):
            incomplete_state += 1
        event = row["event"]
        if row["schema_version"] != 1 or event not in EVENTS:
            raise ValueError(f"record {total}: unknown schema/event")
        if row["source"] != ("engine" if event in ENGINE else "mod"):
            raise ValueError(f"record {total}: engine/mod provenance mismatch")
        if row["sequence"] != total or row["qpc"] < previous_qpc or row["qpc_frequency"] <= 0:
            raise ValueError(f"record {total}: lost/reordered sequence or invalid clock")
        if row["scene_epoch"] != expected_epoch:
            raise ValueError(f"record {total}: scene epoch changed without an observed exit")
        if started is None:
            started = row
            if event != "trace_start":
                raise ValueError("trace does not begin with trace_start")
            if row["evidence_class"] not in {"NATIVE_PROBE", "HOST_FIXTURE"}:
                raise ValueError("unknown evidence class")
            if row["sdk_commit"] != SDK:
                raise ValueError("unqualified SDK identity")
            expected_exe = EXE if row["evidence_class"] == "NATIVE_PROBE" else "HOST_FIXTURE_NOT_GAME"
            if row["executable_sha256"] != expected_exe:
                raise ValueError("unqualified executable identity")
            if require_native and row["evidence_class"] != "NATIVE_PROBE":
                raise ValueError("HOST_FIXTURE cannot satisfy a native evidence request")
        else:
            for field in ("evidence_class", "executable_sha256", "sdk_commit", "bridge_version", "pid", "thread_id", "qpc_frequency"):
                if row[field] != started[field]:
                    raise ValueError(f"record {total}: mixed run/identity ({field})")
            if event == "trace_start" or last["event"] == "trace_stop":
                raise ValueError("multiple sessions or records after trace_stop")
        if row["multiplayer_mutations"]:
            raise ValueError("observational baseline declares multiplayer mutations")
        entity = row["entity_id"]
        if event in {"entity_created", "entity_observed"} and entity:
            if entity in all_ids and entity not in alive:
                raise ValueError(f"record {total}: retired entity ID resurrected")
            if event == "entity_created" and entity in created_ids:
                raise ValueError(f"record {total}: duplicate entity creation")
            if event == "entity_created":
                created_ids.add(entity)  # An object may be observed inside its factory before CreateInstance returns.
            alive.add(entity)
            all_ids.add(entity)
        elif event == "entity_invalidated":
            if entity not in alive:
                raise ValueError(f"record {total}: invalidation of an unknown/retired entity")
            alive.remove(entity)
        elif entity and event != "jump_return" and entity not in alive:
            raise ValueError(f"record {total}: stale/unknown entity referenced")
        action = row["action_id"]
        if event == "jump_enter":
            if not action or action in action_ids:
                raise ValueError("missing/duplicate action ID")
            if row["parent_action_id"] and row["parent_action_id"] not in pending:
                raise ValueError("action parent is not active")
            pending[action] = (entity, row["parent_action_id"], row["argument"], row["mode_id"])
            action_ids.add(action)
        elif event == "jump_return":
            if pending.pop(action, None) != (entity, row["parent_action_id"], row["argument"], row["mode_id"]):
                raise ValueError("orphan, duplicated or mismatched native action return")
            if row["result"] not in {0, 1}:
                raise ValueError("DoJump return is not a native bool")
            completed += 1
            if row["result"] == 1 and entity in alive and row["mode_id"] == CREATURE_MODE:
                accepted.add((expected_epoch, entity))
        elif action:
            raise ValueError("action ID on a non-action event")
        if event == "jump_land" and row["mode_id"] == CREATURE_MODE and (expected_epoch, entity) in accepted:
            landed.add((expected_epoch, entity))
        if event == "scene_exit":
            expected_epoch += 1
            alive.clear()
        if event == "ai_summary":
            npc_ticks += row["count"]
            avatar_ticks += row["secondary_count"]
        if event == "stage_observed":
            stages.add(row["mode_id"])
        if event == "hooks_ready" and row["count"] != 7:
            issues.append("unexpected installed hook count")
        max_foreign = max(max_foreign, row["foreign_callbacks"])
        max_lost = max(max_lost, row["lost_records"])
        previous_qpc = row["qpc"]
        events[event] += 1
        last = row
    if not total or last["event"] != "trace_stop":
        raise ValueError("trace is empty or lacks a clean terminal record")
    if pending:
        raise ValueError(f"unfinished native actions: {sorted(pending)}")
    if events["hooks_ready"] != 1:
        issues.append("exactly one successful hook installation is required")
    if events["hooks_failed"] or events["overflow"] or max_lost:
        issues.append("instrumentation failure or lost records")
    if max_foreign:
        issues.append("callbacks occurred outside the assumed engine thread")
    if incomplete_state:
        issues.append("non-finite or missing native scalar state")
    if not accepted:
        issues.append("no accepted Creature-campaign jump with a tracked live entity")
    if not landed:
        issues.append("no landing observed after an accepted jump on the same entity in the same scene")
    if not events["entity_invalidated"] or not events["scene_exit"]:
        issues.append("entity invalidation and scene-exit coverage are both required")
    if started["evidence_class"] != "NATIVE_PROBE":
        issues.append("host fixture is not original-game execution")
    return {
        "schema_version": 1, "records": total, "evidence_class": started["evidence_class"],
        "structurally_valid": True, "m02_trace_coverage_ready": not issues,
        "native_acceptance": "NOT_VERIFIED", "coverage_gaps": issues,
        "completed_action_calls": completed, "accepted_jump_entities": len(accepted),
        "entities_with_later_landing": len(landed), "unique_entities": len(all_ids),
        "native_ai_calls": {"npc": npc_ticks, "avatar": avatar_ticks},
        "stage_mode_ids": sorted(stages), "events": dict(events),
        "foreign_callbacks": max_foreign, "lost_records": max_lost,
        "required_external_evidence": [
            "matching guarded-host payload/game fingerprints, PID and normal exit",
            "matching bridge lifecycle and source/artifact/environment manifest",
            "reference and observed runs from the same disposable fixture, with visible gameplay comparison",
            "native lifetime/ABI evidence and unchanged behavior; a later landing is temporal association only",
        ],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--require-native", action="store_true")
    parser.add_argument("--require-m02-coverage", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = analyze(read_records(args.trace), args.require_native or args.require_m02_coverage)
        digest = hashlib.sha256()
        with args.trace.open("rb") as source:
            for chunk in iter(lambda: source.read(65536), b""):
                digest.update(chunk)
        result["trace_sha256"] = digest.hexdigest()
        code = 0 if not args.require_m02_coverage or result["m02_trace_coverage_ready"] else 21
        encoded = json.dumps(result, indent=2, allow_nan=False) + "\n"
        if args.output:
            with args.output.open("x", encoding="utf-8") as output:
                output.write(encoded)
        print(encoded, end="")
        return code
    except (OSError, ValueError) as error:
        print(f"Trace rejected: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
