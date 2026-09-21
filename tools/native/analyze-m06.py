"""Closed/prefix M06 evidence analysis; never grants milestone acceptance.

Inputs are m06-harness report.json files and their hash-bound trace archives.
Exact source ticks match dense original network_authority_pose events. Client
positions must lie on the previous-to-target interpolation segment, with the
recorded float-rounding tolerance. This is numerical evidence, not visual proof.
"""
from __future__ import annotations

import argparse
from bisect import bisect_left
from collections import defaultdict
from datetime import datetime
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import tempfile

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("m06_harness", REPO / "tools/native/m06-harness.py")
harness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(harness)


def gate(value, detail): return {"status": "NOT_RUN" if value is None else "PASS" if value else "FAIL", "detail": detail}


def utc(value):
    result = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if result.utcoffset() is None: raise ValueError("Evidence timestamp has no UTC offset")
    return result


def within_lifetime(observed, created, exited):
    return utc(created) <= utc(observed) <= utc(exited)


def angle(a, b):
    a, b = harness.vector(a, 4), harness.vector(b, 4)
    aa, bb = math.sqrt(sum(x*x for x in a)), math.sqrt(sum(x*x for x in b))
    if aa <= 0 or bb <= 0: raise ValueError("Zero source/replica orientation")
    return 2 * math.acos(min(1., abs(sum(x*y for x,y in zip(a,b))/(aa*bb))))


def projection(actual, previous, target):
    p, a, b = harness.vector(actual), harness.vector(previous), harness.vector(target)
    delta = [y-x for x,y in zip(a,b)]
    length2 = sum(x*x for x in delta)
    alpha = min(1., max(0., sum((x-y)*z for x,y,z in zip(p,a,delta))/length2)) if length2 else 1.
    nearest = [x+alpha*y for x,y in zip(a,delta)]
    return {"alpha_from_position": alpha, "segment_error": harness.distance(p,nearest), "target_error": harness.distance(p,b)}


def match_pose(replica, sources, position_tolerance, orientation_tolerance, velocity_tolerance):
    target = sources[-1]
    previous = sources[-2] if len(sources) > 1 else target
    measured = projection(replica["position"], previous["position"], target["position"])
    arc = angle(previous["orientation"], target["orientation"])
    arc_excess = max(0., angle(previous["orientation"], replica["orientation"]) + angle(replica["orientation"], target["orientation"]) - arc)
    velocity_error = harness.distance(replica["velocity"], target["velocity"])
    measured.update(orientation_arc_excess_radians=arc_excess, velocity_error=velocity_error,
                    matched=measured["segment_error"] <= position_tolerance and arc_excess <= orientation_tolerance and velocity_error <= velocity_tolerance)
    return measured


def verified_file(record, maximum=harness.MAX_TRACE):
    path = Path(record["archive"])
    raw = harness.bounded(path, maximum)
    if len(raw) != record["bytes"] or hashlib.sha256(raw).hexdigest() != record["sha256"]:
        raise ValueError("Archived evidence content changed: " + path.name)
    return raw


def action_matches(action, trace_index):
    """Require each claimed native event to equal a hash-verified trace row."""
    required = ["network_intention", "authority_received", "authority_queued", "native_entry"]
    operation = action.get("operation")
    required += ["initial_native_pose", "final_native_pose"] if operation == "move" else ["native_landing"]
    if operation not in ("move", "jump") or not action.get("native_action_observed") or not action.get("scene_lifecycle_unchanged"):
        return False
    if any(not isinstance(action.get(name), dict) or trace_index.get((action[name].get("pid"), action[name].get("sequence"))) != action[name] for name in required):
        return False
    intent, received, queued, entry = (action[name] for name in required[:4])
    owner = action.get("owner")
    if owner != harness.OWNERS.get(action.get("client")) or owner not in (1, 2): return False
    if not (intent.get("event") == "network_intention" and intent.get("player") == owner and intent.get("verb") == (0 if operation == "move" else 1)
            and intent.get("entity") == action.get("source_entity") and intent["sequence"] > action.get("client_before_sequence", 0)
            and received.get("event") == "network_native_intention" and received.get("player") == owner and received.get("queued") is True
            and queued.get("event") == "worker_command_queued" and queued.get("owner") == owner and queued.get("verb") == operation
            and intent.get("request") == received.get("request") == queued.get("request")
            and entry.get("executing_command") == queued.get("command") and entry.get("actor") == queued.get("actor")
            and entry.get("epoch") == queued.get("epoch") == received.get("epoch")
            and entry["pid"] == queued["pid"] == received["pid"] != intent["pid"]
            and action.get("authority_before_sequence", 0) < queued["sequence"] < entry["sequence"]): return False
    if operation == "jump":
        landing = action["native_landing"]
        return (entry.get("event") == "native_jump_return" and entry.get("accepted") is True and landing.get("event") == "native_landing"
                and landing["pid"] == entry["pid"] and landing["sequence"] > entry["sequence"] and landing.get("epoch") == entry.get("epoch")
                and landing.get("actor") == entry.get("actor") and landing.get("jump_command") == queued.get("command"))
    initial, final = action["initial_native_pose"], action["final_native_pose"]
    threshold = action.get("exercise_threshold", 0)
    return (entry.get("event") == "native_walk_request" and .05 <= threshold <= 6
            and all(row.get("event") == "scene_native_authority_sample" and row["pid"] == entry["pid"] and row.get("owner") == owner
                    and row.get("remote_entity") == action.get("source_entity") and row.get("entity_generation") == action.get("source_generation")
                    and row.get("epoch") == entry.get("epoch") for row in (initial, final))
            and initial["sequence"] < entry["sequence"] < final["sequence"] and harness.distance(initial["position"], final["position"]) >= threshold)


def connection_witness(report, records):
    """Historical action reports predate OS enumeration; retain their live IPC evidence."""
    if report.get("operation") != "actions" or not report.get("operation_completed"): return None
    before, after = (report.get("snapshots", {}).get(name) for name in ("before", "after"))
    if not before or not after or set(before["workers"]) != {"01", "02", "03"}: return None
    games, checks = [], []
    observed = before["utc"]
    for worker_id, item in before["workers"].items():
        following = after["workers"].get(worker_id, {})
        record = records.get((worker_id, item["generation"]), {})
        commands = [command for command in report.get("commands", []) if command.get("worker_id") == worker_id and command.get("argv", [""])[-1] == "status"]
        command = commands[0] if commands else {}
        reply = harness.read_json(command.get("stdout", "{}"))
        args = command.get("argv", [])
        identity = item["game_pid"] == following.get("game_pid") and item["generation"] == following.get("generation")
        status_ok = True
        for snapshot_item in (item, following):
            status_file = snapshot_item.get("files", {}).get("network-status.json")
            status = harness.read_json(verified_file(status_file)) if status_file else {}
            status_ok &= (status == snapshot_item.get("network_status") and status.get("state") == "connected"
                          and status.get("player_id") == harness.OWNERS[worker_id] and status.get("baseline_sequence", 0) > 0
                          and snapshot_item.get("actor_trace_valid") is True and snapshot_item.get("trace_closed") is False)
        status_ok &= (command.get("exit_code") == 0 and len(args) == 4 and args[1] == item["generation"] and args[2] == str(item["supervisor_pid"])
                      and reply.get("op") == "status" and reply.get("result") == "accepted" and reply.get("phase") == 3
                      and bool(reply.get("actor_a")) and bool(reply.get("actor_b"))
                      and utc(report["utc_started"]) <= utc(command["utc"]) <= utc(observed) <= utc(report["utc_finished"]))
        lifetime = record.get("host_process_lifetime", {})
        lifetime_ok = (bool(lifetime.get("created_utc")) and bool(lifetime.get("exited_utc"))
                       and within_lifetime(observed, lifetime["created_utc"], lifetime["exited_utc"]))
        checks.append(identity and status_ok and lifetime_ok)
        games.append({"worker_id": worker_id, "generation": item["generation"], "game_pid": item["game_pid"], "status_request_utc": command.get("utc"),
                      "same_game_before_after": identity, "authenticated_live_scene_status": bool(status_ok),
                      "host_lifetime_contains_snapshot": lifetime_ok, **lifetime})
    return {"basis": "Successful actions harness, generation-bound live IPC status, hash-bound connected before/after snapshots, and matching NativeHost process lifetimes.",
            "snapshot_utc": observed, "three_current_original_pids_present": all(checks) and len({game["game_pid"] for game in games}) == 3,
            "games": games, "clock_scope": "NativeHost wall-clock UTC bounds process lifetime only; actor relative-QPC origins are never subtracted."}


def normal_scene_close(record, error):
    return (error.get("detail") == "The native scene was closed. Rejoin from the launcher."
            and record.get("native_host_game_exits") == [0] and record.get("trace_healthy") is True)


def analyze(report_paths, position_tolerance=.002, orientation_tolerance=.001, velocity_tolerance=.002):
    runs, inputs, actions, coordinators, witnesses, reports = {}, [], [], [], [], []
    for path in report_paths:
        raw = harness.bounded(path, 16*1024*1024)
        report = harness.read_json(raw)
        if report.get("schema_version") != 1 or report.get("evidence_class") != "ORIGINAL_M06_PROCESS_HARNESS": raise ValueError("Unknown harness report")
        reports.append(report)
        inputs.append({"path": str(Path(path).absolute()), "sha256": hashlib.sha256(raw).hexdigest(), "operation": report["operation"],
                       "operation_completed": report["operation_completed"], "error": report.get("error")})
        actions.extend(report.get("actions", []))
        for snapshot in report["snapshots"].values():
            if snapshot.get("process_witness"):
                witness = snapshot["process_witness"]
                expected = {item["game_pid"] for item in snapshot["workers"].values()}
                actual = {game.get("pid") for game in witness.get("games", [])}
                witnesses.append({**witness, "expected_game_pids": sorted(expected), "three_current_original_pids_present":
                                  witness.get("status") == "OBSERVED" and len(expected) == 3 and expected.issubset(actual)})
            if snapshot.get("coordinator"):
                coordinator = snapshot["coordinator"]
                verified_file(coordinator, harness.MAX_COORDINATOR)
                coordinators.append(coordinator)
            for worker_id, item in snapshot["workers"].items():
                files = item["files"]
                filename = f"actors-{item['game_pid']}.jsonl"
                if filename not in files: continue
                raw_trace = verified_file(files[filename])
                _, rows = harness.trace_rows(raw_trace, item["game_pid"], report["expected_bridge_version"])
                for other_name, file in files.items():
                    if other_name != filename: verified_file(file)
                key = worker_id, item["generation"]
                previous = runs.get(key)
                if previous:
                    shorter, longer = sorted((previous["raw"], raw_trace), key=len)
                    if not longer.startswith(shorter): raise ValueError("Two archives disagree about one original trace prefix")
                    if len(raw_trace) <= len(previous["raw"]): continue
                runs[key] = {"item": item, "rows": rows, "raw": raw_trace, "version": report["expected_bridge_version"]}
    output = {"schema_version": 1, "evidence_class": "ORIGINAL_M06_TRACE_ANALYSIS", "inputs": inputs,
              "milestone_acceptance": "NOT_VERIFIED", "analysis_valid": True,
              "tolerances": {"position_native_units": position_tolerance, "orientation_arc_excess_radians": orientation_tolerance,
                             "velocity_native_units": velocity_tolerance, "interpretation": "Numerical source/interpolation correspondence only; not perceptual acceptance."},
              "original_runs": [], "gates": {}, "replicas": [], "reconnects": [], "actions": actions,
              "remaining_manual_gates": ["Inspect actual original-game captures for two controlled actors and consistent scene.",
                                         "Review stale/wrong-owner/malformed/oversized/version rejection in separately labeled network HOST/TLS evidence.",
                                         "Verify personal-save hashes, isolated-profile writes and final original process exits."]}
    sources = defaultdict(dict)
    source_owners = defaultdict(set)
    authority_generations = set()
    for (worker_id, generation), run in runs.items():
        rows, item = run["rows"], run["item"]
        role = "authority" if worker_id == "01" else "replica"
        policies = [row for row in rows if row.get("event") == "replica_policy_ready"]
        connected = [row for row in rows if row.get("event") == "network_status" and row.get("state") == "connected"]
        samples = [row for row in rows if row.get("event") == ("scene_native_authority_sample" if worker_id == "01" else "scene_native_replica_sample")]
        last = rows[-1]
        record = {"worker_id": worker_id, "generation": generation, "game_pid": item["game_pid"], "supervisor_pid": item["supervisor_pid"],
                  "bridge_version": run["version"], "records": len(rows), "engine_thread": rows[0]["thread_id"],
                  "trace_healthy": last.get("healthy") if last.get("event") == "trace_stop" else None,
                  "trace_detach_status": last.get("detach_status") if last.get("event") == "trace_stop" else None,
                  "closed_trace": last.get("event") == "trace_stop", "policy_role_matches": len(policies) == 1 and policies[0].get("role") == role,
                  "connected_baselines": [row.get("baseline_sequence") for row in connected],
                  "scene_baseline_applications": sum(row.get("event") == "scene_baseline_applied" for row in rows),
                  "scene_exit_count": sum(row.get("event") == "scene_exit" for row in rows), "native_scene_samples": len(samples),
                  "network_errors": [{"sequence": row["sequence"], "detail": row.get("detail")} for row in rows if row.get("event") == "network_status" and row.get("state") == "error"],
                  "native_host_game_exits": [], "host_process_lifetime": {}, "controlled_actor_deaths": []}
        host_file = item["files"].get("native-host.jsonl")
        if host_file:
            host_rows = [harness.read_json(line) for line in verified_file(host_file).splitlines()]
            record["native_host_game_exits"] = [row.get("exit_code") for row in host_rows if row.get("event") == "game_exited"]
            for name, field in (("created_suspended", "created_utc"), ("game_exited", "exited_utc")):
                matching = [row for row in host_rows if row.get("event") == name and row.get("game_pid") == item["game_pid"] and row.get("host_pid") == item["supervisor_pid"]]
                if len(matching) == 1:
                    utc(matching[0]["utc"])
                    record["host_process_lifetime"][field] = matching[0]["utc"]
        if worker_id == "01":
            dead = set()
            for row in rows:
                if row.get("event") == "actor_state" and row.get("owner") in (1, 2) and row.get("dead") is True:
                    key = row["owner"], row.get("native_id")
                    if key not in dead:
                        dead.add(key)
                        record["controlled_actor_deaths"].append({"sequence": row["sequence"], "owner": row["owner"], "native_id": row.get("native_id"),
                            "health": row.get("health"), "scope": "Terminal state outside the qualified living-actor fixture; this run cannot establish death/respawn parity."})
        output["original_runs"].append(record)
        if worker_id == "01":
            authority_generations.add(generation)
            for row in rows:
                if row.get("event") != "network_authority_pose": continue
                owner = harness.uint(row.get("owner"), nonzero=True)
                key = harness.uint(row.get("entity"), nonzero=True), harness.uint(row.get("generation"), nonzero=True)
                tick = harness.uint(row.get("tick"), nonzero=True)
                if owner not in (1,2): raise ValueError("Unexpected controlled source owner")
                if tick in sources[key] and sources[key][tick] != row: raise ValueError("Two authoritative poses claim the same entity generation/tick")
                sources[key][tick] = row
                source_owners[owner].add(key)
    gates = output["gates"]
    records = {(row["worker_id"], row["generation"]): row for row in output["original_runs"]}
    for report in reports:
        witness = connection_witness(report, records)
        if witness: witnesses.append(witness)
    gates["native_trace_integrity"] = gate(bool(runs) and all(row["policy_role_matches"] for row in output["original_runs"]), "Hashes, sequence, PID, pinned executable/SDK/build, engine thread and zero foreign callbacks checked for every selected trace.")
    ready_roles = {row["worker_id"] for row in output["original_runs"] if row["connected_baselines"] and row["native_scene_samples"]}
    gates["three_original_connected_processes"] = gate(any(row["three_current_original_pids_present"] for row in witnesses) and ready_roles == {"01","02","03"} if witnesses else None,
        {"process_witnesses": witnesses, "overlapping_native_ready_seconds": None,
         "clock_scope": "Actor qpc is PER-PROCESS RELATIVE. No cross-process QPC duration or latency is inferred. Process presence requires recorded OS enumeration or live IPC/status backed by NativeHost UTC lifetimes."})
    gates["distinct_authoritative_player_identities"] = gate(all(len(source_owners[i]) == 1 for i in (1,2)) and source_owners[1].isdisjoint(source_owners[2]) if sources else None,
        {str(owner): [list(key) for key in sorted(keys)] for owner,keys in source_owners.items()})
    source_lists = {key: [rows[tick] for tick in sorted(rows)] for key,rows in sources.items()}
    source_ticks = {key: sorted(rows) for key,rows in sources.items()}
    all_geometry, all_ownership, all_ticks = [], [], []
    for (worker_id,generation), run in runs.items():
        if worker_id == "01": continue
        controlled = harness.OWNERS[worker_id]
        rows = run["rows"]
        samples = [row for row in rows if row.get("event") == "scene_native_replica_sample"]
        summary = {"worker_id": worker_id, "generation": generation, "controlled_owner": controlled, "samples": len(samples),
                   "matched": 0, "unmatched_source_tick": 0, "source_archive_trailing": 0, "geometry_failures": [], "ownership_failures": [],
                   "source_tick_regressions": [], "owners": {}, "latency_ms": {}}
        native_ids, last_ticks = defaultdict(set), {}
        errors, orientations, velocities = [], [], []
        for row in samples:
            owner = row.get("owner")
            key = row.get("remote_entity"), row.get("entity_generation")
            tick = harness.uint(row.get("source_tick"), nonzero=True)
            native_ids[owner].add(row.get("native_id"))
            if owner not in (1,2) or row.get("is_native_avatar") is not (owner == controlled) or key not in source_owners[owner]:
                summary["ownership_failures"].append({"sequence": row["sequence"], "owner": owner, "entity": list(key), "is_native_avatar": row.get("is_native_avatar")})
            if key in last_ticks and tick < last_ticks[key]: summary["source_tick_regressions"].append(row["sequence"])
            last_ticks[key] = tick
            if key not in sources or tick not in sources[key]:
                if key in source_ticks and tick > source_ticks[key][-1]: summary["source_archive_trailing"] += 1
                else: summary["unmatched_source_tick"] += 1
                continue
            index = bisect_left(source_ticks[key], tick)
            source_pair = source_lists[key][max(0,index-1):index+1]
            result = match_pose(row, source_pair, position_tolerance, orientation_tolerance, velocity_tolerance)
            errors.append(result["segment_error"]); orientations.append(result["orientation_arc_excess_radians"]); velocities.append(result["velocity_error"])
            source_row = source_pair[-1]
            if result["matched"]: summary["matched"] += 1
            elif len(summary["geometry_failures"]) < 32: summary["geometry_failures"].append({"sequence": row["sequence"], "source_sequence": source_row["sequence"], "source_tick": tick, **result})
        summary["owners"] = {str(owner): sorted(ids) for owner,ids in native_ids.items()}
        owner_ok = not summary["ownership_failures"] and all(len(native_ids[i]) == 1 for i in (1,2)) and native_ids[1].isdisjoint(native_ids[2])
        geometry_ok = summary["matched"] == len(samples) and not summary["geometry_failures"] and bool(samples)
        tick_ok = not summary["source_tick_regressions"] and summary["unmatched_source_tick"] == 0 and summary["source_archive_trailing"] == 0
        summary.update(ownership_matches=owner_ok, geometry_matches=geometry_ok,
                       maximum_segment_error=max(errors,default=None), maximum_orientation_arc_excess=max(orientations,default=None), maximum_velocity_error=max(velocities,default=None),
                       source_tick_range={"minimum":min((row["source_tick"] for row in samples),default=None),"maximum":max((row["source_tick"] for row in samples),default=None)},
                       latency_ms={"status":"NOT_RUN","note":"Actor qpc has a different process-local origin in every trace. Exact shared source_tick correspondence is valid; cross-process latency subtraction is not."})
        output["replicas"].append(summary)
        if samples: all_geometry.append(geometry_ok); all_ownership.append(owner_ok); all_ticks.append(tick_ok)
    gates["owner1_and_owner2_local_native_avatars"] = gate(all(all_ownership) if len(all_ownership) >= 2 else None, "Client02 must expose owner1 as the sole local native avatar; client03 owner2, with one distinct local noun for the other owner.")
    gates["authoritative_tick_and_generation_correspondence"] = gate(all(all_ticks) if len(all_ticks) >= 2 else None, "Each replica sample must have an exact dense original source entity/generation/tick; missing or trailing source archives are not silently ignored.")
    gates["native_interpolated_pose_correspondence"] = gate(all(all_geometry) if len(all_geometry) >= 2 else None, "Actual re-read native position, quaternion and velocity compared to the immediately previous/target authoritative poses; review per-client errors and tolerances.")
    for worker_id in ("02","03"):
        candidates = []
        for (wid,generation), run in runs.items():
            if wid != worker_id: continue
            rows = run["rows"]
            for row in rows:
                if row.get("event") == "network_status" and row.get("state") == "connected":
                    later = [sample for sample in rows if sample["sequence"] > row["sequence"] and sample.get("event") == "scene_native_replica_sample"]
                    controlled_sample = next((sample for sample in later if sample.get("owner") == harness.OWNERS[worker_id]),None)
                    # Source ticks are emitted by the same authority clock and
                    # survive a client restart; process-local qpc cannot order it.
                    if controlled_sample: candidates.append((controlled_sample["source_tick"],generation,row,controlled_sample))
        candidates.sort(key=lambda value:value[0])
        for previous, current in zip(candidates,candidates[1:]):
            old, new = previous[2], current[2]
            old_pose, new_pose = previous[3], current[3]
            same_identity = (old_pose["remote_entity"],old_pose["entity_generation"],old["player_id"]) == (new_pose["remote_entity"],new_pose["entity_generation"],new["player_id"])
            fresh = new["baseline_sequence"] > old["baseline_sequence"]
            output["reconnects"].append({"worker_id":worker_id,"old_generation":previous[1],"new_generation":current[1],
                                       "old_baseline":old["baseline_sequence"],"new_baseline":new["baseline_sequence"],
                                       "same_player_and_global_entity":same_identity,"fresh_authoritative_baseline":fresh,
                                       "authority_generation_unchanged":len(authority_generations)==1})
    gates["reconnect_fresh_baseline_without_duplicate_player"] = gate(all(row["same_player_and_global_entity"] and row["fresh_authoritative_baseline"] and row["authority_generation_unchanged"] for row in output["reconnects"]) if output["reconnects"] else None,
        "Requires actual earlier/later connected samples from supplied reports. A new baseline sequence alone does not prove complete world persistence.")
    trace_index = {}
    for run in runs.values():
        for row in run["rows"]:
            key = row["pid"], row["sequence"]
            if key in trace_index and trace_index[key] != row: raise ValueError("Selected traces reuse a PID/sequence with ambiguous action provenance")
            trace_index[key] = row
    action_checks = [{"client": row.get("client"), "operation": row.get("operation"), "archive_verified": action_matches(row, trace_index)} for row in actions]
    output["action_archive_checks"] = action_checks
    action_keys = {(row["client"], row["operation"]) for row in action_checks if row["archive_verified"]}
    gates["both_client_intentions_reach_original_native_actions"] = gate(all(row["archive_verified"] for row in action_checks) and all((client,operation) in action_keys for client in ("02","03") for operation in ("move","jump")) if actions else None,
        "Every claimed intention/queue/native-entry/landing or movement sample must equal an actual hash-verified trace row, with owner/request/command/epoch/sequence checks and recomputed displacement. Queued acknowledgments alone do not pass.")
    closed = output["original_runs"]
    gates["closed_original_lifecycles"] = gate(all(row["trace_healthy"] is True and row["trace_detach_status"] == 0 and row["native_host_game_exits"] == [0] for row in closed) if closed and all(row["closed_trace"] for row in closed) else None,
        "A running trace prefix is NOT RUN for shutdown; explicit closed archive is required.")
    material_errors = []
    expected_scene_closures = []
    for row in output["original_runs"]:
        for error in row["network_errors"]:
            item = {"worker_id":row["worker_id"],"generation":row["generation"],**error}
            if normal_scene_close(row, error):
                expected_scene_closures.append(item)
            else: material_errors.append(item)
    gates["no_material_native_network_lifecycle_errors"] = gate(not material_errors if output["original_runs"] else None,
        {"material_errors":material_errors,"normal_closed_scene_notifications":expected_scene_closures,
         "note":"Entity spawn/removal/application failures remain material even near a later clean process exit."})
    deaths = [{"worker_id": row["worker_id"], "generation": row["generation"], **death} for row in output["original_runs"] for death in row["controlled_actor_deaths"]]
    gates["controlled_actors_remain_in_qualified_living_fixture"] = gate(not deaths if output["original_runs"] else None,
        {"terminal_native_deaths": deaths, "note": "A successful living prefix is retained, but an observed controlled-actor death is not silently treated as supported death/respawn replication."})
    gates["network_negative_cases"] = gate(None, "Review separately recorded real TLS/HOST parser/session tests. This analyzer does not infer malformed/stale challenge execution from absence of bad native state.")
    gates["visual_original_scene_and_control"] = gate(None, "Requires inspected original-game WGC recordings/screenshots from the actual process identities.")
    output["coordinator_archives"] = coordinators
    output["numerical_checks_have_failures"] = any(value["status"] == "FAIL" for value in gates.values())
    output["selected_checks_have_failures"] = output["numerical_checks_have_failures"]
    output["native_acceptance"] = "NOT_VERIFIED_BY_ANALYZER"
    return output


def self_test():
    start={"position":[0.,0.,0.],"orientation":[0.,0.,0.,1.],"velocity":[1.,0.,0.]}
    end={**start,"position":[10.,0.,0.]}
    correct={**start,"position":[5.,0.,0.]}
    assert match_pose(correct,[start,end],.002,.001,.002)["matched"]
    assert not match_pose({**correct,"position":[5.,.1,0.]},[start,end],.002,.001,.002)["matched"]
    assert not match_pose({**correct,"velocity":[2.,0.,0.]},[start,end],.002,.001,.002)["matched"]
    assert not match_pose({**correct,"orientation":[0.,0.,1.,0.]},[start,end],.002,.001,.002)["matched"]
    assert match_pose({**correct,"orientation":[0.,0.,0.,-1.]},[start,end],.002,.001,.002)["matched"]
    assert not match_pose({**correct,"position":[11.,0.,0.]},[start,end],.002,.001,.002)["matched"]
    checks = 6
    assert within_lifetime("2026-09-15T00:00:05Z", "2026-09-15T02:00:00+02:00", "2026-09-15T00:00:10Z")
    assert not within_lifetime("2026-09-15T00:00:11Z", "2026-09-15T00:00:00Z", "2026-09-15T00:00:10Z")
    checks += 2
    action = {"client": "02", "owner": 1, "operation": "jump", "native_action_observed": True, "scene_lifecycle_unchanged": True,
              "source_entity": 90, "source_generation": 1, "client_before_sequence": 1, "authority_before_sequence": 1,
              "network_intention": {"pid": 2, "sequence": 2, "event": "network_intention", "player": 1, "verb": 1, "entity": 90, "request": 8},
              "authority_received": {"pid": 1, "sequence": 3, "event": "network_native_intention", "player": 1, "queued": True, "request": 8, "epoch": 3},
              "authority_queued": {"pid": 1, "sequence": 2, "event": "worker_command_queued", "owner": 1, "verb": "jump", "request": 8, "command": 9, "actor": 1, "epoch": 3},
              "native_entry": {"pid": 1, "sequence": 4, "event": "native_jump_return", "accepted": True, "executing_command": 9, "actor": 1, "epoch": 3},
              "native_landing": {"pid": 1, "sequence": 5, "event": "native_landing", "jump_command": 9, "actor": 1, "epoch": 3}}
    index = {(row["pid"], row["sequence"]): row for row in action.values() if isinstance(row, dict)}
    assert action_matches(action, index)
    assert not action_matches({**action, "native_landing": {**action["native_landing"], "actor": 2}}, index)
    assert not action_matches({**action, "owner": 2}, index)
    assert not action_matches(action, {key: row for key, row in index.items() if key != (1, 5)})
    assert not action_matches({**action, "scene_lifecycle_unchanged": False}, index)
    checks += 5
    healthy = {"native_host_game_exits": [0], "trace_healthy": True}
    assert normal_scene_close(healthy, {"detail": "The native scene was closed. Rejoin from the launcher."})
    assert not normal_scene_close(healthy, {"detail": "Native entity removal failed."})
    assert not normal_scene_close({**healthy, "trace_healthy": False}, {"detail": "The native scene was closed. Rejoin from the launcher."})
    checks += 3
    with tempfile.TemporaryDirectory(prefix="sporemp-analysis-") as directory:
        items, records, commands = {}, {}, []
        for worker_id, owner in harness.OWNERS.items():
            status = {"state": "connected", "player_id": owner, "baseline_sequence": 1}
            raw = json.dumps(status).encode(); path = Path(directory) / (worker_id + ".json"); path.write_bytes(raw)
            item = {"game_pid": int(worker_id), "supervisor_pid": 100 + int(worker_id), "generation": worker_id,
                    "network_status": status, "actor_trace_valid": True, "trace_closed": False,
                    "files": {"network-status.json": {"archive": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}}}
            items[worker_id] = item
            records[(worker_id, worker_id)] = {"host_process_lifetime": {"created_utc": "2026-09-15T00:00:00Z", "exited_utc": "2026-09-15T00:00:10Z"}}
            commands.append({"worker_id": worker_id, "argv": ["control.exe", worker_id, str(item["supervisor_pid"]), "status"], "exit_code": 0,
                             "utc": "2026-09-15T00:00:04Z", "stdout": json.dumps({"op": "status", "result": "accepted", "phase": 3, "actor_a": 1, "actor_b": 2})})
        snapshot = {"utc": "2026-09-15T00:00:05Z", "workers": items}
        report = {"operation": "actions", "operation_completed": True, "utc_started": "2026-09-15T00:00:03Z", "utc_finished": "2026-09-15T00:00:06Z",
                  "snapshots": {"before": snapshot, "after": snapshot}, "commands": commands}
        assert connection_witness(report, records)["three_current_original_pids_present"]
        records[("03", "03")]["host_process_lifetime"]["exited_utc"] = "2026-09-15T00:00:01Z"
        assert not connection_witness(report, records)["three_current_original_pids_present"]
        records[("03", "03")]["host_process_lifetime"]["exited_utc"] = "2026-09-15T00:00:10Z"
        commands[0]["argv"][1] = "different-generation"
        assert not connection_witness(report, records)["three_current_original_pids_present"]
        checks += 3
    print(json.dumps({"evidence_class":"HOST_SELF_TEST","checks":checks,"native_game":"NOT_RUN"}))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports",nargs="*",type=Path)
    parser.add_argument("--output",type=Path)
    parser.add_argument("--self-test",action="store_true")
    parser.add_argument("--position-tolerance",type=float,default=.002)
    parser.add_argument("--orientation-tolerance",type=float,default=.001)
    parser.add_argument("--velocity-tolerance",type=float,default=.002)
    args=parser.parse_args()
    if args.self_test: self_test(); return 0
    if not args.reports or args.output is None: parser.error("Supply harness reports and a fresh --output JSON path")
    if any(not 0 < value <= .05 for value in (args.position_tolerance,args.orientation_tolerance,args.velocity_tolerance)): parser.error("Explicit numerical tolerance must be >0 and <=0.05")
    output=harness.service.diag.no_reparse(args.output.absolute())
    if output.exists(): raise ValueError("Evidence output already exists")
    result=analyze(args.reports,args.position_tolerance,args.orientation_tolerance,args.velocity_tolerance)
    result["analyzer_sha256"]=harness.service.diag.fingerprint(Path(__file__))["sha256"]
    result["utc"]=harness.service.diag.utc_now()
    harness.service.diag.write_json(output,result)
    print(json.dumps({"analysis_valid":True,"numerical_checks_have_failures":result["numerical_checks_have_failures"],"output":str(output),"native_acceptance":"NOT_VERIFIED_BY_ANALYZER"}))
    return 1 if result["numerical_checks_have_failures"] else 0


if __name__=="__main__": raise SystemExit(main())
