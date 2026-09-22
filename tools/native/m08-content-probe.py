"""Inspect one existing .crt in an already-running isolated original worker.

This explicit developer request may populate original-game caches. It does not
import the supplied archive, start a game, approve transfer or grant readiness.
The archive must be an operator-provided closed copy under repository local/.
Its bytes are not assumed to match a same-key resource in the running engine.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools/content"))
sys.path.insert(0, str(REPO / "tools/launcher"))
import spore_content as content
import worker_manager as workers

EXE = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37"
SDK = "cbf9206b9a823f0911cd9be0217104a49d72380b"
VERSION = "0.0.46"
CRT = 0x2B978C46
BEM = 0x1A99B06B
DERIVED = 0x0F43029A
MAX_TRACE = 66 * 1024 * 1024
MAX_BLOCKS = 512
MAX_CAPABILITIES = 4096
EVENTS = {"content_inspection_begin", "content_rigblock", "content_capability",
          "content_inspection_complete", "content_inspection_failed"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def uint(value, bits=64, nonzero=False):
    require(type(value) is int and int(nonzero) <= value < (1 << bits), "Invalid unsigned scalar")
    return value


def sint(value, bits=32):
    require(type(value) is int and -(1 << (bits - 1)) <= value < (1 << (bits - 1)),
            "Invalid signed scalar")
    return value


def read_json(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "Duplicate JSON key")
            result[key] = value
        return result
    def number(text):
        value = float(text)
        require(math.isfinite(value), "Nonfinite JSON value")
        return value
    def invalid(_):
        raise ValueError("Nonfinite JSON value")
    return json.loads(raw, object_pairs_hook=unique, parse_float=number, parse_constant=invalid)


def private_path(path):
    path, root = content.safe_path(path), content.safe_path(REPO / "local")
    require(root in path.parents, "Path must be inside repository local/")
    return path


def creation_key(key):
    require(isinstance(key, str) and content.KEY_PATTERN.fullmatch(key), "Invalid exact native resource key")
    group, tail = key.split("!")
    instance, kind = tail.split(".")
    instance, kind, group = int(instance, 16), int(kind, 16), int(group, 16)
    require(0 < instance < 0xFFFFFFFF and kind == CRT and group < 0xFFFFFFFF,
            "Only a non-sentinel .crt creation key is admitted")
    return instance, kind, group


def source_record(path, key, save_trace=None, version=VERSION):
    values = creation_key(key)
    path = private_path(path)
    data = content.read_bounded(path)
    inventory = content.inspect_bytes(data)  # Validate all records, including unselected payloads.
    records = [record for record in inventory["records"] if record["key"] == key]
    witness = None
    representation = "literal_crt_record"
    if save_trace is not None:
        witness = closed_save_witness(save_trace, values, version)
    if not records and witness is not None:
        # cEditorResource::resource_type is BEM in the pinned SDK. Native editor
        # results use the logical CRT key; the original save retains the same
        # group/instance in a BEM record. This admits inspection only, not import
        # or byte equivalence between the supplied archive and running engine.
        persisted_key = f"{values[2]:08x}!{values[0]:08x}.{BEM:08x}"
        records = [record for record in inventory["records"] if record["key"] == persisted_key]
        representation = "bem_with_closed_native_editor_result"
    require(len(records) == 1, "Selected creation key is absent from the closed copied archive")
    return values, dict(path=str(path), archive_sha256=inventory["archive_sha256"],
                       archive_size=len(data), record_count=inventory["record_count"],
                       record_set_sha256=inventory["record_set_sha256"], record=records[0],
                       logical_key=key, representation=representation, save_witness=witness,
                       closed_copy="OPERATOR_REQUIRED_NOT_PROVEN_BY_TOOL",
                       native_byte_equivalence="NOT_VERIFIED")


def trace_rows(raw, pid, version):
    require(len(raw) <= MAX_TRACE, "Oversized native trace")
    complete = raw[:raw.rfind(b"\n") + 1]
    rows = [read_json(line) for line in complete.splitlines()]
    require(rows and all(isinstance(row, dict) for row in rows), "Missing native trace objects")
    first = rows[0]
    require((first.get("event"), first.get("evidence_class"), first.get("executable_sha256"),
             first.get("sdk_commit"), first.get("bridge_version")) ==
            ("trace_start", "NATIVE_PROBE", EXE, SDK, version), "Native trace provenance mismatch")
    thread = uint(first.get("thread_id"), 32, True)
    frequency = uint(first.get("qpc_frequency"), nonzero=True)
    previous_qpc = -1
    for index, row in enumerate(rows, 1):
        require(type(row.get("schema_version")) is int and row["schema_version"] == 1 and
                uint(row.get("sequence"), nonzero=True) == index and
                uint(row.get("pid"), 32, True) == pid, "Native trace schema, sequence or PID mismatch")
        require(row.get("evidence_class") == "NATIVE_PROBE" and
                uint(row.get("thread_id"), 32, True) == thread and
                uint(row.get("foreign_callbacks")) == 0 and
                uint(row.get("qpc_frequency"), nonzero=True) == frequency,
                "Native trace thread, callback or clock mismatch")
        qpc = uint(row.get("qpc"))
        uint(row.get("epoch"))
        require(qpc >= previous_qpc, "Native trace clock moved backwards")
        previous_qpc = qpc
    require(sum(row.get("event") == "trace_start" for row in rows) == 1, "Duplicate native trace header")
    return complete, rows


def closed_save_witness(path, values, version):
    """Require a complete, healthy native editor observation for the logical key.

    This is provenance for the diagnostic key mapping, not proof of durable
    publication, dependencies, archive byte identity, or an authority transaction.
    Routing IDs only match each sequential request/result; they are not nonces.
    """
    path = private_path(path)
    raw = content.read_bounded(path, MAX_TRACE)
    first_line = raw.split(b"\n", 1)[0]
    first = read_json(first_line)
    require(isinstance(first, dict), "Missing native save trace header")
    pid = uint(first.get("pid"), 32, True)
    complete, rows = trace_rows(raw, pid, version)
    require(complete == raw, "Native save trace has an incomplete trailing record")
    stops = [row for row in rows if row.get("event") == "trace_stop"]
    require(len(stops) == 1 and stops[0] is rows[-1] and stops[0].get("healthy") is True and
            uint(stops[0].get("detach_status"), 32) == 0, "Native save trace lacks a healthy closed footer")
    registered = [row for row in rows if row.get("event") == "editor_observer_registered"]
    disposed = [row for row in rows if row.get("event") == "editor_observer_disposed"]
    require(len(registered) == len(disposed) == 1 and
            uint(registered[0].get("producer_prefixes"), 32) == 2 and
            uint(registered[0].get("request_message"), 32) == 0xB03BC30C and
            uint(registered[0].get("result_message"), 32) == 0x030C11C7 and
            registered[0].get("observation_only") is True and
            registered[0].get("native_execution_qualified") is False and
            registered[0].get("readiness") is False and
            disposed[0].get("listeners_removed") is True and
            uint(disposed[0].get("foreign_thread_callbacks"), 32) == 0 and
            disposed[0].get("readiness") is False and
            registered[0]["sequence"] < disposed[0]["sequence"] < stops[0]["sequence"] and
            not any(row.get("event") in ("editor_observation_rejected", "editor_observer_unavailable") for row in rows),
            "Native save editor observer was unavailable, rejected a message, or failed cleanup")
    pending, matches = None, []
    for row in rows:
        event = row.get("event")
        if event not in ("editor_request_observed", "editor_result_observed"):
            continue
        require(registered[0]["sequence"] < row["sequence"] < disposed[0]["sequence"] and
                row.get("observation_only") is True and row.get("readiness") is False,
                "Editor observation lies outside the active observer lifetime")
        key = tuple(uint(row.get(name), 32) for name in ("instance", "type", "group"))
        route = uint(row.get("routing_id"), 32)
        if event == "editor_request_observed":
            require(pending is None and uint(row.get("message"), 32) == 0xB03BC30C and
                    row.get("editor_landing_verified") is False, "Overlapping or malformed native editor request")
            pending = row
        else:
            require(pending is not None and uint(row.get("message"), 32) == 0x030C11C7 and
                    row.get("native_commit_validation") is False and route == pending["routing_id"] and
                    row.get("outcome") in ("accepted", "cancelled") and uint(row.get("play"), 8) <= 1,
                    "Unpaired or malformed native editor outcome")
            uint(row.get("model_type"), 32)
            if row["outcome"] == "accepted" and key == values:
                matches.append((pending, row))
            pending = None
    require(pending is None and len(matches) == 1, "Expected one accepted native editor result for selected creation")
    request, result = matches[0]
    return dict(path=str(path), bytes=len(raw), sha256=content.digest(raw), pid=pid,
                bridge_version=version, request_sequence=request["sequence"], result_sequence=result["sequence"],
                result=result, trace_closed=True, native_commit_validation=False)


def adapter_ready(rows):
    bindings = [row for row in rows if row.get("event") == "content_bindings_checked"]
    require(len(bindings) == 1 and type(bindings[0].get("code_prefixes")) is int and
            bindings[0]["code_prefixes"] == 2 and
            bindings[0].get("native_execution_qualified") is False and
            bindings[0].get("readiness") is False and
            not any(row.get("event") in ("content_binding_rejected", "trace_stop") for row in rows),
            "Content adapter is unavailable, unrecognized, or stopped")


def inspection(rows, request, epoch, values, after_sequence):
    """Correlate the one native return; partial traces cannot become success."""
    uint(request, nonzero=True)
    related = [row for row in rows if row.get("event") in EVENTS and row.get("request") == request]
    require(all(uint(row.get("request"), nonzero=True) == request and row["sequence"] > after_sequence
                and row["epoch"] == epoch for row in related), "Stale or wrong-epoch inspection evidence")
    failed = [row for row in related if row["event"] == "content_inspection_failed"]
    require(not failed, "Original content inspection failed: " + str(failed[0].get("reason")) if failed else "")
    begins = [row for row in related if row["event"] == "content_inspection_begin"]
    ends = [row for row in related if row["event"] == "content_inspection_complete"]
    require(len(begins) <= 1 and len(ends) <= 1, "Duplicated native inspection boundary")
    if not ends:
        return None
    require(len(begins) == 1 and related[0] is begins[0] and related[-1] is ends[0],
            "Missing or reordered native inspection boundary")
    begin, end = begins[0], ends[0]
    key_fields = ("instance", "type", "group")
    require(tuple(uint(begin.get(name), 32) for name in key_fields) == values and
            begin.get("may_generate_native_cache") is True, "Native request key differs from selected key")
    require(tuple(uint(end.get(name), 32) for name in key_fields) == (values[0], DERIVED, values[2]) and
            end.get("owned_release_completed") is True and end.get("gameplay_validation") is False and
            end.get("readiness") is False, "Native result identity or release/acceptance boundary mismatch")
    uint(end.get("model_type"), 32)
    count, cap_count = uint(end.get("rigblocks"), 32, True), uint(end.get("capabilities"), 32)
    require(count <= MAX_BLOCKS and cap_count <= MAX_CAPABILITIES, "Native content count exceeds budget")
    blocks = [row for row in related if row["event"] == "content_rigblock"]
    caps = [row for row in related if row["event"] == "content_capability"]
    require(len(blocks) == count and len(caps) == cap_count and related == [begin, *blocks, *caps, end],
            "Missing, extra or reordered native content records")
    for ordinal, row in enumerate(blocks):
        require(uint(row.get("ordinal"), 32) == ordinal, "Rigblock ordinal mismatch")
        for name in ("group", "instance"):
            uint(row.get(name), 32)
        for name in ("index", "parent", "symmetric", "flags", "block_type"):
            sint(row.get(name))
        start, size = uint(row.get("capability_start"), 32), uint(row.get("capability_count"), 32)
        require(start <= cap_count and size <= cap_count - start, "Rigblock capability range exceeds result")
    for ordinal, row in enumerate(caps):
        require(uint(row.get("ordinal"), 32) == ordinal and isinstance(row.get("tag_hex"), str) and
                re.fullmatch(r"[0-9a-f]{8}", row["tag_hex"]), "Capability ordinal or tag mismatch")
        sint(row.get("native_level"), 8)
    return dict(begin=begin, rigblocks=blocks, capabilities=caps, complete=end)


def live_trace(path):
    """Read a bounded live prefix, allowing append but refusing replacement/shrink."""
    path = content.safe_path(path)
    require(stat.S_ISREG(path.stat().st_mode), "Native trace must be a regular file")
    with path.open("rb") as stream:
        before = os.fstat(stream.fileno())
        require(before.st_size <= MAX_TRACE, "Oversized native trace")
        raw = stream.read(MAX_TRACE + 1)
        after = os.fstat(stream.fileno())
    current = content.safe_path(path).stat()
    require(len(raw) <= MAX_TRACE and (before.st_dev, before.st_ino) ==
            (after.st_dev, after.st_ino) == (current.st_dev, current.st_ino) and
            before.st_size <= after.st_size <= current.st_size and len(raw) >= before.st_size,
            "Native trace changed identity, shrank or exceeded budget")
    return raw


def live_identity(worker, expected_pid=None):
    require(workers.supervisor_running(worker), "Original supervisor is not running")
    status_path = content.safe_path(Path(worker["run"]) / "worker-status.json")
    require(-2 <= time.time() - status_path.stat().st_mtime <= 5, "Stale worker status")
    status = read_json(content.read_bounded(status_path, 65536))
    require(isinstance(status, dict) and type(status.get("schema_version")) is int and
            status["schema_version"] == 1 and status.get("generation") == worker["generation"],
            "Worker status generation mismatch")
    pid = uint(status.get("game_pid"), 32, True)
    require(expected_pid is None or pid == expected_pid, "Original game PID changed")
    account = workers.read_json(workers.root_for(worker["worker_id"]) / "account.json")
    require(account.get("prepared") is True and account.get("user") == "SporeMP-M04-" + worker["worker_id"] and
            isinstance(account.get("sid"), str) and account["sid"] and isinstance(account.get("game_root"), str),
            "Prepared isolated account identity mismatch")
    expected = (pid, worker["supervisor_pid"], account["sid"],
                str(Path(account["game_root"]) / "SporebinEP1/SporeApp.exe").lower())
    games = workers.os_games()
    matching = [game for game in games if (game.get("pid"), game.get("parent"), game.get("sid"),
                (game.get("path") or "").lower()) == expected]
    require(len(matching) == 1, "Original game OS process is not the captured isolated worker")
    return pid, status


def payload_identity(worker):
    payload = content.safe_path(worker["payload"])
    records = {}
    for relative in ("SporeMP.NativeHost.exe", "mLibs/SporeMP.Bridge.dll", "mLibs/SporeModAPI.dll",
                     "ModAPI.DLLInjector.dll"):
        data = content.read_bounded(payload / relative)
        require(data, "Empty native payload artifact")
        records[relative] = dict(bytes=len(data), sha256=content.digest(data))
    require(records["SporeMP.NativeHost.exe"]["sha256"] == worker.get("supervisor_sha256"),
            "Captured supervisor payload hash mismatch")
    control = content.read_bounded(REPO / "build/win32/Release/SporeMP.WorkerControl.exe")
    require(control, "Empty worker control artifact")
    records["SporeMP.WorkerControl.exe"] = dict(bytes=len(control), sha256=content.digest(control))
    return records


def command(worker, operation, report, flush, epoch=0, values=()):
    argv = [str(REPO / "build/win32/Release/SporeMP.WorkerControl.exe"), worker["generation"],
            str(worker["supervisor_pid"]), operation]
    if operation != "status":
        argv.extend([str(uint(epoch)), *(str(uint(value, 32)) for value in values)])
    entry = dict(argv=argv, expected_exit=0, expected_result="accepted", outcome="UNKNOWN_NO_AUTOMATIC_RETRY")
    report["commands"].append(entry)
    flush()
    try:
        result = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8", timeout=13,
                                creationflags=subprocess.CREATE_NO_WINDOW)
    except subprocess.TimeoutExpired as error:
        entry["error"] = str(error)
        raise ValueError("Worker IPC timed out; no automatic retry") from None
    require(len(result.stdout) <= 65536 and len(result.stderr) <= 65536, "Oversized worker control response")
    entry.update(exit_code=result.returncode, stdout=result.stdout, stderr=result.stderr)
    flush()
    require(result.returncode in (0, 5), "Unknown worker IPC outcome; no automatic retry")
    reply = read_json(result.stdout)
    require(isinstance(reply, dict) and type(reply.get("schema_version")) is int and reply["schema_version"] == 1 and
            reply.get("op") == operation and reply.get("result") in
            ("accepted", "unavailable", "stale", "invalid", "busy", "failed"), "Unrecognized worker reply")
    require(result.returncode == (0 if reply["result"] == "accepted" else 5), "Exit code contradicts worker reply")
    uint(reply.get("epoch"))
    if operation != "status":
        uint(reply.get("request"), nonzero=True)
    entry["outcome"] = "REPLY_RECEIVED"
    return reply


def run(args):
    output = private_path(args.output)
    require(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version), "Explicit native version required")
    output.mkdir(parents=True, exist_ok=False)
    report = dict(schema_version=1, evidence_class="HOST", operation="inspect_creation", commands=[], requested_version=args.version,
                  probe_checks_passed=False, native_return_observed=False, native_byte_equivalence="NOT_VERIFIED",
                  native_acceptance="NOT_VERIFIED", gameplay_validation=False, dependency_status="UNKNOWN",
                  transfer_approved=False, readiness=False)
    captured_raw = {}
    with (output / "report.json").open("x", encoding="utf-8") as sink:
        def flush():
            sink.seek(0)
            json.dump(report, sink, indent=2, allow_nan=False)
            sink.truncate()
            sink.flush()
            os.fsync(sink.fileno())
        flush()
        try:
            values, source = source_record(args.source_archive, args.key, args.save_trace, args.version)
            report["source"] = source
            worker = dict(workers.current(args.worker))  # Freeze once; never retarget a replacement generation.
            report["worker"] = worker
            pid, report["initial_status"] = live_identity(worker)
            report["game_pid"] = pid
            report["payload_hashes"] = payload_identity(worker)
            trace = content.safe_path(Path(worker["run"]) / f"actors-{pid}.jsonl")
            report["native_trace_source"] = str(trace)
            before = captured_raw["before-trace.jsonl"] = live_trace(trace)
            prefix, rows = trace_rows(before, pid, args.version)
            adapter_ready(rows)
            before_sequence = rows[-1]["sequence"]
            report["before_sequence"] = before_sequence
            live = command(worker, "status", report, flush)
            require(live["result"] == "accepted", "Worker status refused")
            epoch = uint(live.get("epoch"), nonzero=True)
            require(uint(live.get("phase")) in (1, 3), "Worker must be in a stable original menu or scene")
            live_identity(worker, pid)
            reply = command(worker, "inspect_creation", report, flush, epoch, values)
            report["reply"] = reply
            deadline = time.monotonic() + 3
            while True:
                raw = captured_raw["after-trace.jsonl"] = live_trace(trace)
                require(raw.startswith(prefix), "Native trace prefix was rewritten after request")
                prefix, rows = trace_rows(raw, pid, args.version)
                adapter_ready(rows)
                if reply["result"] != "accepted":
                    raise ValueError("Original worker refused inspection; preserved reply and native trace")
                require(reply["epoch"] == epoch, "Worker epoch changed during inspection")
                observed = inspection(rows, reply["request"], epoch, values, before_sequence)
                if observed is not None:
                    break
                require(time.monotonic() < deadline, "No complete matching flushed native content result")
                time.sleep(0.1)
            live_identity(worker, pid)
            require(payload_identity(worker) == report["payload_hashes"], "Native payload artifacts changed during inspection")
            _, final_source = source_record(args.source_archive, args.key, args.save_trace, args.version)
            require(source == final_source, "Source copied archive changed during inspection")
            report.update(probe_checks_passed=True, native_return_observed=True, observation=observed,
                          actual_bridge_version=rows[0]["bridge_version"], source_unchanged=True,
                          trace_closed=False, native_byte_equivalence="NOT_VERIFIED")
        except Exception as error:
            report["error"] = str(error)
            # A timed-out native call may have executed. Preserve available bytes
            # without retrying the command or converting uncertain work to success.
            if "after-trace.jsonl" not in captured_raw and any(
                    entry["argv"][3] == "inspect_creation" for entry in report["commands"]):
                try:
                    captured_raw["after-trace.jsonl"] = live_trace(trace)
                except Exception as trace_error:
                    report["failure_trace_error"] = str(trace_error)
        finally:
            report["traces"] = {}
            for name, raw in captured_raw.items():
                try:
                    content.write_new(output / name, raw)
                    report["traces"][name] = dict(bytes=len(raw), sha256=content.digest(raw),
                                                  complete_prefix_bytes=raw.rfind(b"\n") + 1)
                except Exception as error:
                    report.update(probe_checks_passed=False, error="Evidence archive failed: " + str(error))
            report["utc"] = datetime.now(timezone.utc).isoformat()
            report["tool_sha256"] = content.digest(content.read_bounded(Path(__file__)))
            flush()
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", required=True, choices=("01", "02", "03"))
    parser.add_argument("--source-archive", type=Path, required=True)
    parser.add_argument("--save-trace", type=Path,
                        help="Closed native editor trace required for a saved BEM behind a logical CRT key")
    parser.add_argument("--key", required=True)
    parser.add_argument("--output", type=Path, required=True, help="Fresh private output directory under local/")
    parser.add_argument("--version", default=VERSION, help="Exact expected trace build; no automatic version relaxation")
    args = parser.parse_args(argv)
    try:
        report = run(args)
    except (OSError, ValueError) as error:
        print(json.dumps(dict(error=str(error), readiness=False)), file=sys.stderr)
        return 2
    print(json.dumps(dict(probe_checks_passed=report["probe_checks_passed"], report=str(args.output / "report.json"),
                          native_acceptance="NOT_VERIFIED", readiness=False)))
    return 0 if report["probe_checks_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
