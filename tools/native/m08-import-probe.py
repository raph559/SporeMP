"""Import one bounded PNG through original SPORE in an existing isolated worker.

Explicit developer mutation: stages a fresh fixed-name file in this captured run,
checks its exact digest in the bridge, invokes the original importer once, and
inspects the returned creation. Neither an IPC ACK nor native return publishes it.
No automatic retries, account creation, native launch, or personal-profile writes.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
import time

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("m08_probe", Path(__file__).with_name("m08-content-probe.py"))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)
content = probe.content
VERSION = "0.0.48"


def import_observation(rows, request, epoch, sha256, size, before_sequence):
    relevant = [r for r in rows if r["sequence"] > before_sequence and r.get("request") == request]
    errors = [r for r in relevant if r["event"] in ("content_import_rejected", "content_import_unqualified")]
    probe.require(not errors, "Native import denied: " + (errors[0].get("reason", "unknown") if errors else ""))
    begins = [r for r in relevant if r["event"] == "content_import_begin"]
    returns = [r for r in relevant if r["event"] == "content_import_returned"]
    if not returns:
        return None
    probe.require(len(begins) == len(returns) == 1, "Import call/result count mismatch")
    begin, returned = begins[0], returns[0]
    probe.require(begin["sequence"] < returned["sequence"] and begin["epoch"] == returned["epoch"] == epoch and
                  begin.get("png_sha256") == sha256 and begin.get("png_bytes") == size and
                  begin.get("readiness") is False and returned.get("readiness") is False and
                  returned.get("native_commit_validation") is False, "Import provenance or epoch mismatch")
    probe.require(returned.get("native_result") is True,
                  "Original import returned false; an existing key is not a new import")
    key = content.key_text(probe.uint(returned.get("group"), 32), probe.uint(returned.get("instance"), 32),
                           probe.uint(returned.get("type"), 32))
    values = probe.creation_key(key)
    observation = probe.inspection(rows, request, epoch, values, returned["sequence"])
    if observation is None:
        return None
    return dict(native_key=key, native_return=returned, inspection=observation)


def run(args):
    output = probe.private_path(args.output)
    output.mkdir(parents=True, exist_ok=False)
    report = dict(schema_version=1, operation="import_creation", requested_version=args.version, commands=[],
                  imported_and_inspected=False, native_acceptance="NOT_VERIFIED", transfer_approved=False, readiness=False)
    raw_before = raw_after = None
    trace = None
    with (output/"report.json").open("x", encoding="utf-8") as sink:
        def flush():
            sink.seek(0)
            json.dump(report, sink, indent=2, allow_nan=False)
            sink.truncate(); sink.flush(); os.fsync(sink.fileno())
        flush()
        try:
            source = probe.private_path(args.png)
            png = content.read_bounded(source, content.MAX_CREATION_PNG)
            report["source"] = dict(path=str(source), **content.inspect_creation_png(png))
            worker = dict(probe.workers.current(args.worker))
            report["worker"] = worker
            pid, report["initial_status"] = probe.live_identity(worker)
            report["game_pid"] = pid
            report["payload_hashes"] = probe.payload_identity(worker)
            trace = content.safe_path(Path(worker["run"])/f"actors-{pid}.jsonl")
            raw_before = probe.live_trace(trace)
            prefix, rows = probe.trace_rows(raw_before, pid, args.version)
            probe.adapter_ready(rows)
            bindings = [r for r in rows if r["event"] == "content_import_bindings_checked"]
            probe.require(len(bindings) == 1 and bindings[0].get("code_prefixes") == 3 and not any(
                r["event"] == "content_import_binding_rejected" for r in rows), "Import bindings unavailable")
            before_sequence = rows[-1]["sequence"]
            staged = content.safe_path(Path(worker["run"])/"m08-import.png")
            probe.require(staged.parent == content.safe_path(worker["run"]), "Fixed quarantine path changed")
            content.write_new(staged, png)
            probe.require(content.read_bounded(staged,content.MAX_CREATION_PNG) == png, "Staged PNG differs")
            report["staged_path"] = str(staged)
            live = probe.command(worker, "status", report, flush)
            epoch = probe.uint(live.get("epoch"), nonzero=True)
            probe.require(live["result"] == "accepted" and live.get("phase") in (1,3), "Stable original menu or scene required")
            probe.live_identity(worker,pid)
            words = struct.unpack("<8I",bytes.fromhex(report["source"]["sha256"]))
            reply = probe.command(worker,"import_creation",report,flush,epoch,words)
            report["reply"] = reply
            deadline = time.monotonic()+3
            while True:
                raw_after = probe.live_trace(trace)
                probe.require(raw_after.startswith(prefix), "Native trace prefix changed")
                _, rows = probe.trace_rows(raw_after,pid,args.version)
                observed = import_observation(rows,reply["request"],epoch,report["source"]["sha256"],len(png),before_sequence)
                probe.require(reply["result"] == "accepted" and reply["epoch"] == epoch,
                              "Original worker refused import; inspect preserved reply and trace")
                if observed is not None: break
                probe.require(time.monotonic() < deadline, "No matching flushed native import/inspection result")
                time.sleep(.1)
            probe.live_identity(worker,pid)
            probe.require(probe.payload_identity(worker) == report["payload_hashes"], "Native payload changed")
            probe.require(content.read_bounded(source,content.MAX_CREATION_PNG) == png and
                          content.read_bounded(staged,content.MAX_CREATION_PNG) == png, "PNG changed during native import")
            report.update(imported_and_inspected=True, observation=observed, source_unchanged=True, trace_closed=False)
        except Exception as error:
            report["error"] = str(error)
        finally:
            if trace is not None:
                try: raw_after = probe.live_trace(trace)
                except Exception as error: report["failure_trace_error"] = str(error)
            for name,raw in (("before-trace.jsonl",raw_before),("after-trace.jsonl",raw_after)):
                if raw is not None: content.write_new(output/name,raw)
            flush()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker",choices=("01","02","03"),required=True)
    parser.add_argument("--png",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--version",default=VERSION)
    args=parser.parse_args()
    result=run(args)
    print(json.dumps({"imported_and_inspected":result["imported_and_inspected"],"readiness":False,
                     "error":result.get("error"),"report":str(args.output/"report.json")}))
    return 0 if result["imported_and_inspected"] else 2


if __name__ == "__main__":
    sys.exit(main())
