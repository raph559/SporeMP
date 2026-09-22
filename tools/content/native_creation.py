"""Build a private source candidate from closed original editor/import evidence.

This supplies the owner's observation and exact native-produced PNG to the
transaction adapter. Authority must still import and derive its own properties;
this file does not grant authority attestation or multiplayer publication.
"""
import importlib.util
from pathlib import Path
import re
import struct
import sys

import spore_content as content
from content_session import ROOT, private, require

sys.path.insert(0, str(ROOT/"tools/launcher"))
import multiplayer_session

SPEC = importlib.util.spec_from_file_location("m08_native_creation_probe", ROOT/"tools/native/m08-content-probe.py")
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


def config_fields(path):
    raw = content.read_bounded(private(path), 4096)
    values = {}
    for line in raw.decode("ascii").splitlines():
        key, separator, value = line.partition("=")
        require(separator and key not in values, "Invalid or duplicate private configuration field")
        values[key] = value
    require(values.get("schema") == "2" and values.get("role") == "player", "Player config schema 2 required")
    for field in ("build_sha256", "executable_sha256", "content_sha256", "fixture_sha256",
                  *(name for name, _ in multiplayer_session.WORLD_FILES)):
        require(re.fullmatch("[0-9a-f]{64}", values.get(field, "")) and values[field] != "0"*64,
                "Missing or invalid native session identity: " + field)
    return values


def observation_bytes(png, values, observed, resolved, fields):
    blocks, capabilities = observed["rigblocks"], observed["capabilities"]
    require(1 <= len(blocks) <= 512 and len(capabilities) <= 4096 and 1 <= len(resolved) <= 512,
            "Native observation budget exceeded")
    data = bytearray(struct.pack("<II", 0x384f4d53, 1))
    data += bytes.fromhex(content.digest(png))
    data += struct.pack("<IIII", values[2], values[0], values[1], probe.uint(observed["complete"]["model_type"], 32))
    data += bytes.fromhex(fields["content_sha256"])
    for name, _ in multiplayer_session.WORLD_FILES:
        data += bytes.fromhex(fields[name])
    data += struct.pack("<III", len(blocks), len(capabilities), len(resolved))
    for b in blocks:
        data += struct.pack("<IIIiiiiiii", probe.uint(b["group"], 32), probe.uint(b["instance"], 32), 0x00b1b104,
            *(probe.sint(b[k]) for k in ("index", "parent", "symmetric", "flags", "block_type", "capability_start", "capability_count")))
    for c in capabilities:
        require(re.fullmatch("[0-9a-f]{8}", c["tag_hex"]), "Invalid native capability tag")
        data += bytes.fromhex(c["tag_hex"])+struct.pack("<i", probe.sint(c["native_level"], 8))
    for group, instance, kind in sorted(resolved):
        data += struct.pack("<III", group, instance, kind)
    require(len(data) <= 64*1024, "Native observation envelope exceeded its limit")
    return bytes(data)


def prepare(spore_copy, closed_run, config, key, request, output, version="0.0.49"):
    spore_copy, closed_run, output = private(spore_copy), private(closed_run), private(output)
    values = probe.creation_key(key)
    traces = list(closed_run.glob("actors-*.jsonl"))
    require(len(traces) == 1, "Exactly one original actor trace required")
    trace = traces[0]
    raw = content.read_bounded(trace, probe.MAX_TRACE)
    header = probe.read_json(raw.split(b"\n", 1)[0])
    pid = probe.uint(header["pid"], 32, True)
    prefix, rows = probe.trace_rows(raw, pid, version)
    require(prefix == raw, "Unclosed actor trace")
    witness = probe.closed_save_witness(trace, values, version)
    host_raw = content.read_bounded(closed_run/"native-host.jsonl", 4*1024*1024)
    host = [probe.read_json(line) for line in host_raw.splitlines()]
    games = [r for r in host if r.get("event") == "game_exited"]
    supervisors = [r for r in host if r.get("event") == "worker_exited"]
    require(len(games) == len(supervisors) == 1 and games[0].get("game_pid") == pid and
        type(games[0].get("exit_code")) is int and games[0]["exit_code"] == 0 and
        type(supervisors[0].get("exit_code")) is int and supervisors[0]["exit_code"] == 0 and supervisors[0].get("requested") is True,
        "Source game and supervisor must have closed cleanly")
    fields = config_fields(config)
    require(fields["executable_sha256"] == header["executable_sha256"] and
        fields["content_sha256"] == content.digest(content.read_bounded(ROOT/"config/compatibility.candidate.json")),
        "Native source executable/installed profile differs from session")
    bridges = [r for r in host if r.get("event") == "module" and
        str(r.get("path", "")).replace("\\", "/").lower().endswith("/sporemp.bridge.dll")]
    require(len(bridges) == 1 and bridges[0].get("sha256") == fields["build_sha256"], "Source bridge differs from session build")
    for name, relative in multiplayer_session.WORLD_FILES:
        require(content.digest(content.read_bounded(spore_copy/relative)) == fields[name], "Canonical world mismatch: " + relative)
    inspections = [r for r in rows if r.get("event") == "content_inspection_begin" and r.get("request") == request]
    require(len(inspections) == 1, "Exactly one selected native inspection required")
    observed = probe.inspection(rows, request, inspections[0]["epoch"], values, witness["result_sequence"])
    require(observed is not None, "No complete native inspection after editor acceptance")
    required = {(r["group"], r["instance"], 0x00b1b104) for r in observed["rigblocks"]}
    resolved = set()
    for group, instance, kind in required:
        found = [r for r in rows if r.get("event") == "content_record_observed" and r.get("purpose") == "rigblock" and
            r.get("request") == request and (r.get("group"), r.get("instance"), r.get("type")) == (group, instance, kind)]
        require(len(found) == 2 and {r.get("manager") for r in found} == {"app", "creation"}, "Missing original part lookup witnesses")
        require(all(r.get("exact_found") is True and r.get("mapped_found") is True and
            (r.get("resolved_group"), r.get("resolved_instance"), r.get("resolved_type")) == (group, instance, kind) and
            r.get("exact_location_qualified") is True and r.get("mapped_location_qualified") is True and
            r.get("readiness") is False and r["epoch"] == inspections[0]["epoch"] and
            inspections[0]["sequence"] < r["sequence"] < observed["complete"]["sequence"] for r in found),
            "Unresolved, substituted or stale native part")
        resolved.add((group, instance, kind))
    archive = spore_copy/"EditorSaves.package"
    source_values, source = probe.source_record(archive, key, trace, version)
    require(source_values == values, "Source key changed")
    archive_raw = content.read_bounded(archive)
    records = content.parse_archive(archive_raw)
    png_key = content.key_text(values[2], values[0], 0x2f7d0004)
    candidates = [r for r in records if r["key"] == png_key]
    require(len(candidates) == 1, "Native accepted key lacks its matching original PNG record")
    png = content.record_bytes(archive_raw, candidates[0])
    shape = content.inspect_creation_png(png)
    binary = observation_bytes(png, values, observed, resolved, fields)
    require(content.read_bounded(trace, probe.MAX_TRACE) == raw and content.read_bounded(archive) == archive_raw,
            "Source evidence changed during preparation")
    output.mkdir(parents=True, exist_ok=False)
    content.write_new(output/"creation.png", png)
    content.write_new(output/"native-observation.bin", binary)
    report = dict(schema_version=1, source_key=key, source=source, png=shape, observation_sha256=content.digest(binary),
        native_inspection_request=request, native_parts=len(resolved), trace_sha256=content.digest(raw),
        host_trace_sha256=content.digest(host_raw), source_closed=True, authority_validation="REQUIRED_NOT_PERFORMED",
        multiplayer_publication=False, readiness=False)
    content.write_new(output/"source.json", content.canonical(report))
    return report
