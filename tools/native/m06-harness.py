"""Archive or exercise already-running original M06 worker/client processes.

Never launches/stops SPORE, sends desktop input, copies saves, or grants a
milestone. IPC accepted means queued; actions require separate native evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools/launcher"))
import launcher_service as service
import worker_manager as workers

EXE = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37"
SDK = "cbf9206b9a823f0911cd9be0217104a49d72380b"
MAX_TRACE = 34 * 1024 * 1024
MAX_COORDINATOR = 256 * 1024 * 1024
OWNERS = {"01": 0, "02": 1, "03": 2}


def uint(value, bits=64, nonzero=False):
    if type(value) is not int or not int(nonzero) <= value < 1 << bits:
        raise ValueError("Invalid unsigned evidence scalar")
    return value


def read_json(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result: raise ValueError("Duplicate JSON key")
            result[key] = value
        return result
    def invalid(_): raise ValueError("Nonfinite JSON value")
    return json.loads(raw, object_pairs_hook=unique, parse_constant=invalid)


def bounded(path, maximum=MAX_TRACE, jsonl=False):
    path = service.diag.no_reparse(Path(path).absolute())
    with path.open("rb") as stream: raw = stream.read(maximum + 1)
    if len(raw) > maximum: raise ValueError("Evidence file exceeds its bounded archive budget: " + path.name)
    return raw[:raw.rfind(b"\n") + 1] if jsonl else raw


def trace_rows(raw, pid, version):
    if len(raw) > MAX_TRACE: raise ValueError("Oversized actor trace")
    complete = raw[:raw.rfind(b"\n") + 1]
    rows = [read_json(line) for line in complete.splitlines()]
    if not rows or any(not isinstance(row, dict) for row in rows): raise ValueError("Missing actor trace objects")
    first = rows[0]
    if (first.get("event"), first.get("evidence_class"), first.get("executable_sha256"), first.get("sdk_commit"), first.get("bridge_version")) != (
        "trace_start", "NATIVE_PROBE", EXE, SDK, version):
        raise ValueError("Actor trace provenance differs from the explicitly selected native build")
    thread, frequency = uint(first.get("thread_id"), 32, True), uint(first.get("qpc_frequency"), nonzero=True)
    previous_qpc = -1
    for index, row in enumerate(rows, 1):
        if row.get("schema_version") != 1 or uint(row.get("sequence"), nonzero=True) != index or uint(row.get("pid"), 32, True) != pid:
            raise ValueError("Actor trace schema/sequence/PID mismatch")
        if row.get("thread_id") != thread or row.get("foreign_callbacks") != 0 or row.get("qpc_frequency") != frequency:
            raise ValueError("Foreign native callback, engine thread or clock mismatch")
        qpc = uint(row.get("qpc"))
        if qpc < previous_qpc: raise ValueError("Actor trace QPC moved backwards")
        previous_qpc = qpc
    return complete, rows


def vector(value, dimensions=3):
    if not isinstance(value, list) or len(value) != dimensions or any(type(x) not in (int, float) or not math.isfinite(x) for x in value):
        raise ValueError("Malformed native vector")
    return value


def distance(a, b): return math.sqrt(sum((x-y)**2 for x, y in zip(vector(a), vector(b))))


def source(worker_id, frozen=None):
    value = workers.validate_current(worker_id, frozen) if frozen else workers.current(worker_id)
    run = service.diag.no_reparse(Path(value["run"]))
    status = read_json(bounded(run / "worker-status.json", 65536))
    if status.get("generation") != value["generation"]: raise ValueError("Worker status generation mismatch")
    pid = uint(status.get("game_pid"), 32, True)
    return {"worker_id": worker_id, "expected_player": OWNERS[worker_id], "generation": value["generation"],
            "supervisor_pid": uint(value["supervisor_pid"], 32, True), "game_pid": pid, "run": str(run),
            "payload": value.get("payload"), "current": {key: value[key] for key in
                ("schema_version", "worker_id", "generation", "supervisor_pid", "run", "payload", "supervisor_sha256", "desktop", "started_utc") if key in value}}


def current_rows(item, version):
    return trace_rows(bounded(Path(item["run"]) / f"actors-{item['game_pid']}.jsonl", jsonl=True), item["game_pid"], version)[1]


def command(item, operation, commands, epoch=0, parameters=()):
    argv = [str(REPO / "build/win32/Release/SporeMP.WorkerControl.exe"), item["generation"], str(item["supervisor_pid"]), operation]
    if operation != "status": argv += [str(uint(epoch)), *(str(uint(x)) for x in parameters)]
    record = {"worker_id": item["worker_id"], "argv": argv, "expected_exit": 0, "expected_result": "accepted", "utc": service.diag.utc_now()}
    commands.append(record)
    try:
        completed = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8", timeout=13, creationflags=subprocess.CREATE_NO_WINDOW)
    except subprocess.TimeoutExpired:
        record["outcome"] = "UNKNOWN_NO_AUTOMATIC_RETRY"
        raise ValueError("Worker IPC timed out; mutation was not retried") from None
    if len(completed.stdout) > 65536 or len(completed.stderr) > 65536: raise ValueError("Oversized control response")
    record.update(exit_code=completed.returncode, stdout=completed.stdout, stderr=completed.stderr)
    if completed.returncode not in (0, 5): raise ValueError("Worker IPC outcome unknown; no automatic retry")
    response = read_json(completed.stdout)
    if response.get("schema_version") != 1 or response.get("op") != operation or response.get("result") != "accepted" or completed.returncode != 0:
        raise ValueError("Worker IPC refused the explicit operation; no automatic retry")
    return response


def archive(output, items, version, coordinator_run=None):
    output.mkdir(parents=True, exist_ok=False)
    # Actor qpc is relative to each process's trace start. Simultaneous process
    # presence needs an actual shared OS enumeration, never subtraction of those
    # unrelated trace counters. This read-only witness does not infer duration.
    witness = {"utc_started": service.diag.utc_now(), "status": "NOT_RUN", "games": []}
    try:
        witness["games"] = workers.checked_games()
        witness["status"] = "OBSERVED"
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        witness["error"] = str(error)
    witness["utc_finished"] = service.diag.utc_now()
    records = {}
    for worker_id, item in items.items():
        destination = output / worker_id
        destination.mkdir()
        record = {**item, "files": {}, "actor_trace_valid": False}
        records[worker_id] = record
        run = Path(item["run"])
        for filename in (f"actors-{item['game_pid']}.jsonl", f"bridge-{item['game_pid']}.jsonl", "native-host.jsonl", "worker-status.json", "network-status.json"):
            path = run / filename
            if not path.is_file(): continue
            raw = bounded(path, jsonl=filename.endswith(".jsonl"))
            target = destination / filename
            with target.open("xb") as stream: stream.write(raw)
            record["files"][filename] = {"archive": str(target.absolute()), "source": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
                                          "complete_jsonl_prefix": filename.endswith(".jsonl"), "source_size_at_stat": path.stat().st_size}
            if filename.startswith("actors-"):
                try:
                    _, rows = trace_rows(raw, item["game_pid"], version)
                    record.update(actor_trace_valid=True, actor_events=len(rows), last_sequence=rows[-1]["sequence"],
                                  last_qpc=rows[-1]["qpc"], bridge_version=rows[0]["bridge_version"],
                                  trace_closed=rows[-1].get("event") == "trace_stop",
                                  trace_healthy=rows[-1].get("healthy") if rows[-1].get("event") == "trace_stop" else None,
                                  scene_exit_count=sum(row.get("event") == "scene_exit" for row in rows))
                except ValueError as error: record["actor_trace_error"] = str(error)
            elif filename == "network-status.json": record["network_status"] = read_json(raw)
        # Hash only public executable payloads; raw session configurations are forbidden.
        if item.get("payload"):
            record["payload_hashes"] = {}
            for relative in ("SporeMP.NativeHost.exe", "mLibs/SporeMP.Bridge.dll", "mLibs/SporeModAPI.dll", "ModAPI.DLLInjector.dll"):
                path = service.diag.no_reparse(Path(item["payload"]) / relative)
                if path.is_file(): record["payload_hashes"][relative] = service.diag.fingerprint(path)["sha256"]
    coordinator = None
    if coordinator_run:
        coordinator_root = service.diag.no_reparse(Path(coordinator_run).absolute())
        original = coordinator_root / "coordinator.jsonl"
        if not original.is_file():
            # The server supervisor stores its protocol log beside restricted
            # invitations. Only this literal JSONL is ever read or archived.
            original = service.diag.no_reparse(coordinator_root / "private/coordinator.jsonl")
        raw = bounded(original, MAX_COORDINATOR, True)
        path = output / "coordinator.jsonl"
        with path.open("xb") as stream: stream.write(raw)
        coordinator = {"archive": str(path.absolute()), "source": str(original), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(), "complete_jsonl_prefix": True}
    return {"utc": service.diag.utc_now(), "workers": records, "coordinator": coordinator, "process_witness": witness}


def require_connected(item, version, commands):
    rows = current_rows(item, version)
    status = read_json(bounded(Path(item["run"]) / "network-status.json", 65536))
    if status.get("state") != "connected" or status.get("player_id") != item["expected_player"] or not uint(status.get("baseline_sequence"), nonzero=True):
        raise ValueError("The original process has not admitted its expected network baseline")
    live = command(item, "status", commands)
    if live.get("phase") != 3 or not live.get("actor_a") or not live.get("actor_b"): raise ValueError("The original living Creature actors are not ready")
    if not any(row.get("event") == ("scene_native_authority_sample" if item["worker_id"] == "01" else "scene_native_replica_sample") for row in rows):
        raise ValueError("Connected status has no actual native scene sample yet")
    return live, rows


def action(item, authority, operation, direction, version, commands, timeout, threshold):
    before_client = current_rows(item, version)
    before_authority = current_rows(authority, version)
    live = command(item, "status", commands)
    if live.get("phase") != 3: raise ValueError("Client left the Creature scene before the action")
    owner = item["expected_player"]
    prior = [row for row in before_authority if row.get("event") == "scene_native_authority_sample" and row.get("owner") == owner]
    if not prior: raise ValueError("Missing prior original controlled-actor sample")
    starting = prior[-1]
    evidence = {"client": item["worker_id"], "owner": owner, "operation": operation, "direction": direction if operation == "move" else None,
                "client_before_sequence": before_client[-1]["sequence"], "authority_before_sequence": before_authority[-1]["sequence"],
                "source_entity": starting["remote_entity"], "source_generation": starting["entity_generation"],
                "native_action_observed": False, "accepted_is_queued": True}
    # Every client owns its local bootstrap A/avatar. The authenticated network
    # implementation chooses its true remote owner; it never trusts these IPC IDs.
    parameters = [owner, uint(live["actor_a"], nonzero=True)] + ([direction] if operation == "move" else [])
    response = command(item, operation, commands, uint(live["epoch"], nonzero=True), parameters)
    evidence["ipc_request"] = response["request"]
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        local = [row for row in current_rows(item, version) if row["sequence"] > evidence["client_before_sequence"]]
        intents = [row for row in local if row.get("event") == "network_intention" and row.get("player") == owner and row.get("verb") == (0 if operation == "move" else 1)]
        if len(intents) > 1: raise ValueError("Multiple client inputs overlap the bounded action window; attribution is ambiguous")
        if intents:
            request = intents[0]["request"]
            evidence["network_intention"] = intents[0]
            remote = [row for row in current_rows(authority, version) if row["sequence"] > evidence["authority_before_sequence"]]
            received = [row for row in remote if row.get("event") == "network_native_intention" and row.get("request") == request and row.get("player") == owner]
            queued = [row for row in remote if row.get("event") == "worker_command_queued" and row.get("request") == request and row.get("owner") == owner and row.get("verb") == operation]
            if len(received) > 1 or len(queued) > 1: raise ValueError("Ambiguous authority action correlation")
            if received and received[0].get("queued") is not True: raise ValueError("Authority rejected the forwarded intention")
            if received and queued:
                event = queued[0]
                evidence.update(authority_received=received[0], authority_queued=event)
                native = [row for row in remote if row.get("executing_command") == event["command"] and row.get("actor") == event["actor"] and
                          row.get("event") == ("native_walk_request" if operation == "move" else "native_jump_return")]
                if len(native) > 1: raise ValueError("Repeated original native entry for one network intention")
                if native:
                    evidence["native_entry"] = native[0]
                    if operation == "jump":
                        if native[0].get("accepted") is not True: raise ValueError("Original DoJump returned false; no automatic retry")
                        landing = [row for row in remote if row.get("event") == "native_landing" and row.get("jump_command") == event["command"] and row.get("actor") == event["actor"] and row.get("sequence", 0) > native[0]["sequence"]]
                        if len(landing) == 1: evidence.update(native_action_observed=True, native_landing=landing[0]); break
                    else:
                        sampled = [row for row in remote if row.get("event") == "scene_native_authority_sample" and row.get("owner") == owner and
                                   row.get("remote_entity") == starting["remote_entity"] and row.get("entity_generation") == starting["entity_generation"] and row["sequence"] > native[0]["sequence"]]
                        moved = [(distance(starting["position"], row["position"]), row) for row in sampled]
                        if moved and max(value[0] for value in moved) >= threshold:
                            measured, pose = max(moved, key=lambda value: value[0])
                            evidence.update(native_action_observed=True, initial_native_pose=starting, final_native_pose=pose,
                                            native_displacement=measured, exercise_threshold=threshold)
                            break
        time.sleep(.15)
    if not evidence["native_action_observed"]: evidence["error"] = "No correlated native movement or landing within the bounded action window"
    after_client, after_authority = current_rows(item, version), current_rows(authority, version)
    evidence["scene_lifecycle_unchanged"] = all(
        sum(row.get("event") == "scene_exit" for row in earlier) == sum(row.get("event") == "scene_exit" for row in later)
        for earlier, later in ((before_client, after_client), (before_authority, after_authority)))
    if not evidence["scene_lifecycle_unchanged"]: evidence["native_action_observed"] = False
    return evidence


def self_test():
    row = {"schema_version": 1, "event": "trace_start", "evidence_class": "NATIVE_PROBE", "executable_sha256": EXE, "sdk_commit": SDK,
           "bridge_version": "fixture", "sequence": 1, "pid": 17, "thread_id": 19, "qpc": 10, "qpc_frequency": 1000, "foreign_callbacks": 0}
    raw = (json.dumps(row) + "\n").encode()
    assert trace_rows(raw+b'{partial', 17, "fixture")[1] == [row]
    for changed in ({**row, "foreign_callbacks": 1}, {**row, "pid": 18}, {**row, "sequence": 2}):
        try: trace_rows((json.dumps(changed)+"\n").encode(), 17, "fixture")
        except ValueError: pass
        else: raise AssertionError("Invalid original trace accepted")
    try: read_json('{"x":1,"x":2}')
    except ValueError: pass
    else: raise AssertionError("Duplicate JSON key accepted")
    assert distance([1, 2, 3], [4, 6, 3]) == 5
    print(json.dumps({"evidence_class": "HOST_SELF_TEST", "checks": 6, "native_game": "NOT_RUN"}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("snapshot", "archive", "actions", "self-test"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--bridge-version", default="0.0.30")
    parser.add_argument("--coordinator-run", type=Path)
    parser.add_argument("--from-snapshot", type=Path, help="Archive the exact old generations captured in an earlier report after root-driven reconnect/exit")
    parser.add_argument("--clients", nargs="+", choices=("02", "03"), default=["02", "03"])
    parser.add_argument("--seconds", type=float, default=12)
    parser.add_argument("--move-distance", type=float, default=.5)
    args = parser.parse_args()
    if args.operation == "self-test": self_test(); return 0
    if args.output is None: parser.error("--output is required")
    if not 1 <= args.seconds <= 30 or not .05 <= args.move_distance <= 6 or len(set(args.clients)) != len(args.clients): parser.error("Bounded action options are invalid")
    if args.from_snapshot and args.operation != "archive": parser.error("--from-snapshot is only accepted by archive")
    output = service.diag.no_reparse(args.output.absolute())
    output.mkdir(parents=True, exist_ok=False)
    report = {"schema_version": 1, "evidence_class": "ORIGINAL_M06_PROCESS_HARNESS", "operation": args.operation,
              "utc_started": service.diag.utc_now(), "expected_bridge_version": args.bridge_version,
              "milestone_acceptance": "NOT_VERIFIED", "operation_completed": False, "commands": [], "actions": [], "snapshots": {},
              "negative_network_tests": "SEPARATE_HOST_EVIDENCE_REQUIRED", "native_visual_acceptance": "SEPARATE_INSPECTION_REQUIRED"}
    items = {}
    try:
        frozen = None
        if args.from_snapshot:
            prior = read_json(bounded(args.from_snapshot, 16 * 1024 * 1024))
            if prior.get("schema_version") != 1 or prior.get("evidence_class") != report["evidence_class"]: raise ValueError("Unknown prior snapshot")
            snapshots = prior["snapshots"]
            frozen = snapshots.get("after", snapshots.get("snapshot", snapshots.get("before")))["workers"]
        for worker_id in OWNERS: items[worker_id] = source(worker_id, frozen[worker_id]["current"] if frozen else None)
        if args.operation == "actions":
            for item in items.values(): require_connected(item, args.bridge_version, report["commands"])
            report["snapshots"]["before"] = archive(output / "before", items, args.bridge_version, args.coordinator_run)
            for worker_id in args.clients:
                for operation in ("move", "jump"):
                    evidence = action(items[worker_id], items["01"], operation, 1 if worker_id == "02" else 3, args.bridge_version, report["commands"], args.seconds, args.move_distance)
                    report["actions"].append(evidence)
                    if not evidence["native_action_observed"]: raise ValueError(evidence.get("error", "Action evidence failed"))
        else:
            report["snapshots"]["snapshot"] = archive(output / "snapshot", items, args.bridge_version, args.coordinator_run)
        report["operation_completed"] = True
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
    finally:
        if args.operation == "actions" and items:
            try: report["snapshots"]["after"] = archive(output / "after", items, args.bridge_version, args.coordinator_run)
            except (OSError, ValueError, KeyError, TypeError) as error: report["archive_error"] = str(error)
        report["utc_finished"] = service.diag.utc_now()
        report["harness_sha256"] = service.diag.fingerprint(Path(__file__))["sha256"]
        service.diag.write_json(output / "report.json", report)
    print(json.dumps({"operation_completed": report["operation_completed"], "report": str(output / "report.json"), "milestone_acceptance": "NOT_VERIFIED", "error": report.get("error")}))
    return 0 if report["operation_completed"] else 1


if __name__ == "__main__": raise SystemExit(main())
