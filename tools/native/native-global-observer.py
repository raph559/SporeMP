"""Strict HOST audit of closed passive native global observations.

Validates original trace provenance and bounded producer/census coverage. An
optional effect joins one original spice assignment to the owner's closed
native archive and a fresh original reader using byte-identical global files.
Never rewrites archives, launches games, grants ownership, or passes M10 L07.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("global_artifact_helpers", HERE / "native-player-artifact.py")
artifact = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(artifact)
content = artifact.content
require, uint, digest = artifact.require, artifact.uint, artifact.digest
PRODUCERS = dict(zip(("universe_update", "relationship_tick", "relationship_apply", "assign_planet_spice",
                     "generate_star", "generate_missing", "star_update", "star_postupdate"),
                    (0xC14CD0, 0x906C70, 0x906280, 0xC3CA50, 0x7A8980, 0x7B77A0, 0x7A71A0, 0x7A71D0)))
MUTATIONS = {"relationship_apply", "assign_planet_spice", "generate_star", "generate_missing"}
CREATURE = 0x01654C01
STATE_SCOPE = "starter_saved_sol_temp_scenario_stars_cached_planets_only"
INPUTS = []


def pinned_file(descriptor, limit):
    require(type(descriptor) is dict and set(descriptor) == {"path", "sha256"}, "Pinned file path/SHA required")
    require(type(descriptor["path"]) is str and re.fullmatch(r"[0-9a-f]{64}", descriptor["sha256"]) is not None,
            "Invalid pinned file descriptor")
    path = Path(descriptor["path"])
    raw = artifact.read_file(path, limit)
    require(digest(raw) == descriptor["sha256"], "Input SHA mismatch: " + path.name)
    INPUTS.append((path, limit, digest(raw)))
    return raw


def words(row, key, size=16):
    values = row.get(key)
    require(type(values) is list and len(values) == size, "Native word vector width: " + key)
    return [uint(value) for value in values]


def run_provenance(raw, host_raw, generation):
    rows, hosts = artifact.jsonl(raw, native=True), artifact.jsonl(host_raw)
    start, stop = artifact.only(rows, "trace_start"), artifact.only(rows, "trace_stop")
    require(stop is rows[-1] and stop.get("healthy") is True and stop.get("detach_status") == 0,
            "Closed healthy native trace required")
    require(start.get("executable_sha256") == artifact.EXE and start.get("sdk_commit") == artifact.SDK,
            "Unknown executable or SDK")
    pid, thread = artifact.assigned(start["pid"]), artifact.assigned(start["thread_id"])
    for sequence, row in enumerate(rows, 1):
        require(row.get("sequence") == sequence and row.get("pid") == pid and row.get("thread_id") == thread
                and row.get("foreign_callbacks") == 0 and row.get("evidence_class") == "NATIVE_PROBE",
                "Trace identity/sequence/thread mismatch")
    supervisor = artifact.only(hosts, "worker_supervisor_started")
    require(re.fullmatch(r"[0-9a-f]{32}", generation) is not None and supervisor["generation"] == generation,
            "Worker generation mismatch")
    for name in ("game_exited", "worker_exited"):
        closed = artifact.only(hosts, name)
        require(closed["exit_code"] == 0 and closed["host_pid"] == supervisor["host_pid"], "Normal original closure missing")
        if name == "game_exited":
            require(closed["game_pid"] == pid, "Closed game PID mismatch")
    return rows, {"pid": pid, "engine_thread": thread, "generation": generation,
                  "trace_sha256": digest(raw), "host_trace_sha256": digest(host_raw)}


def analyze_run(spec):
    require(type(spec) is dict and set(spec) == {"label", "trace", "host_trace", "generation"}, "Run fields")
    require(re.fullmatch(r"[a-z0-9_-]{1,48}", spec["label"]) is not None, "Run label")
    rows, proof = run_provenance(pinned_file(spec["trace"], artifact.MAX_TRACE),
                                 pinned_file(spec["host_trace"], artifact.MAX_TRACE), spec["generation"])
    attached = artifact.only(rows, "native_global_observer_attached")
    detached = artifact.only(rows, "native_global_observer_detached")
    require(attached["hooks"] == 8 and attached["pass_through"] is True and detached["detached"] is True
            and attached["sequence"] < detached["sequence"], "Observer attachment window")
    require(not any(row.get("event") in {"native_global_observer_rejected", "native_global_observer_detach_failed",
                                         "native_global_observer_dispose_failed", "trace_limit"} for row in rows),
            "Observer or trace failure")
    summaries = [row for row in rows if row.get("event") == "native_global_observer_summary"]
    require(summaries, "Missing observer counters")
    groups = collections.OrderedDict()
    prior = {name: 0 for name in PRODUCERS}
    for row in summaries:
        producer = row.get("producer")
        require(producer in PRODUCERS and row["rva"] == PRODUCERS[producer], "Unknown producer binding")
        for key in ("mark", "request", "observed_epoch", "entries", "returns", "creature_entries",
                    "other_mode_entries", "app_ticks", "elapsed_ms", "foreign_thread_entries", "dropped_samples"):
            uint(row[key], 64)
        require(row["entries"] == row["returns"] and row["entries"] >= prior[producer]
                and row["foreign_thread_entries"] == row["dropped_samples"] == row["suppressed_calls"] == 0
                and row["entries"] == row["creature_entries"] + row["other_mode_entries"]
                and row["global_ownership_qualified"] is False, "Incomplete or inconsistent native counters")
        prior[producer] = row["entries"]
        key = (row["mark"], row["attached"])
        group = groups.setdefault(key, {})
        require(producer not in group, "Duplicate producer summary")
        group[producer] = row
    for group in groups.values():
        require(set(group) == set(PRODUCERS), "Incomplete eight-producer summary")
        require(len({(r["phase"], r["observed_epoch"], r["request"], r["app_ticks"], r["attached"]) for r in group.values()}) == 1,
                "Mixed summary scopes")
    first, final = next(iter(groups.values())), next(reversed(groups.values()))
    require(all(r["phase"] == "before_load" and r["entries"] == 0 and r["attached"] is True for r in first.values()),
            "Observer did not start with a zero before-load window")
    require(all(r["phase"] == "end" and r["attached"] is False for r in final.values()), "Missing detached final counters")
    samples = [row for row in rows if row.get("event") == "native_global_callback"]
    per_producer = collections.defaultdict(list)
    for row in samples:
        name = row.get("producer")
        require(name in PRODUCERS and row["original_returned"] is True and row["thread"] == proof["engine_thread"],
                "Callback identity or original return")
        require(attached["sequence"] < row["sequence"] < detached["sequence"], "Callback outside attachment window")
        words(row, "args", 4)
        words(row, "before")
        words(row, "after")
        ordinal = uint(row["entry_ordinal"], 64)
        require(0 < ordinal <= final[name]["entries"], "Callback ordinal outside counted range")
        require(not any(previous["entry_ordinal"] == ordinal for previous in per_producer[name]), "Duplicate callback ordinal")
        per_producer[name].append(row)
    for name in MUTATIONS:
        require(len(per_producer[name]) == final[name]["entries"], "Mutation callback detail missing: " + name)
        require({row["entry_ordinal"] for row in per_producer[name]} == set(range(1, final[name]["entries"] + 1)),
                "Mutation ordinal gap")
    for name in PRODUCERS:
        require(final[name]["entries"] == 0 or any(row["entry_ordinal"] == 1 for row in per_producer[name]),
                "Missing first native callback sample: " + name)
    ticker_rows = [row for row in rows if row.get("event") == "native_global_ticker_census"]
    require(ticker_rows, "Missing ticker census")
    for row in ticker_rows:
        require(row["complete"] is True and row["deferred_removals_excluded"] is True, "Incomplete bounded ticker census")
        entries = [e for e in rows if e.get("event") == "native_global_ticker_entry" and e["mark"] == row["mark"]]
        require(len(entries) == row["active"] + row["pending"] <= 512
                and [e["ordinal"] for e in entries] == list(range(1, len(entries) + 1))
                and sum(e["universe"] is True for e in entries) == row["universe_count"], "Ticker entry/count mismatch")
        for entry in entries:
            require(entry["observed_epoch"] == row["observed_epoch"] and entry["request"] == row["request"]
                    and entry["list"] in (0, 1), "Ticker entry scope")
            require(entry["universe"] == (entry["vtable_rva"] == 0x1095750 and entry["update_rva"] == 0xC14CD0),
                    "Universe ticker identity mismatch")
    state_marks = {}
    for census in (row for row in rows if row.get("event") == "native_global_record_census"):
        require(census["scope"] == STATE_SCOPE and census["whole_galaxy"] is False, "Unexpected record scope")
        mark = census["mark"]
        stars, planets = {}, {}
        for name, target in (("native_global_star_state", stars), ("native_global_planet_state", planets)):
            for row in (r for r in rows if r.get("event") == name and r["mark"] == mark):
                value = words(row, "words")
                require(row["observed_epoch"] == census["observed_epoch"] and value[0] not in target,
                        "Duplicate or incorrectly scoped native record")
                target[value[0]] = value
        require(len(stars) == census["stars"] and len(planets) == census["cached_planets"], "Record census count mismatch")
        group = groups.get((mark, True))
        require(group, "Record census without summary scope")
        phase = group["universe_update"]["phase"]
        if phase in ("scene_ready", "before_save", "after_save", "checkpoint"):
            require(census["manager_seen"] is True and census["complete_within_scope"] is True,
                    "Relevant record census is partial")
        require(mark not in state_marks, "Duplicate record mark")
        state_marks[mark] = {"phase": phase, "epoch": census["observed_epoch"], "sequence": census["sequence"],
                             "complete": census["complete_within_scope"], "stars": stars, "planets": planets}
    require(any(mark["phase"] == "scene_ready" for mark in state_marks.values()), "No observed loaded original scene")
    universes = [r for r in rows if r.get("event") == "native_global_universe_state"]
    require(universes and all(r["singleton_readable"] is True for r in universes), "Unreadable Universe singleton")
    periodic_inactive = (final["universe_update"]["entries"] == final["relationship_tick"]["entries"] == 0
                         and all(not r["present"] for r in universes) and all(r["universe_count"] == 0 for r in ticker_rows))
    changes = []
    for row in per_producer["assign_planet_spice"]:
        require(row["before_valid"] is True and row["after_valid"] is True and row["before"][0] == row["after"][0],
                "Spice record identity unavailable across original return")
        if row["before"][4:7] != row["after"][4:7]:
            changes.append({"sequence": row["sequence"], "planet_id": row["after"][0], "caller_rva": row["caller_rva"],
                            "mode": row["mode"], "before_spice": row["before"][4:7], "after_spice": row["after"][4:7]})
    result = {"label": spec["label"], **proof, "observation_validated": True,
              "duration_ms": final["universe_update"]["elapsed_ms"],
              "counters": {name: {key: final[name][key] for key in ("entries", "returns", "creature_entries", "other_mode_entries")}
                           for name in PRODUCERS},
              "periodic_universe_political_inactive_in_window": periodic_inactive,
              "mutation_candidates": changes, "loaded_mark_count": len(state_marks), "ticker_census_count": len(ticker_rows)}
    return result, {"rows": rows, "samples": per_producer, "marks": state_marks}


def planet_record(raw, planet_id):
    key = f"00000001!{planet_id:08x}.05220cb8"
    entries = content.parse_archive(raw)
    matches = [entry for entry in entries if entry["key"] == key]
    require(len(matches) == 1, "Chosen native PlanetID absent from closed owner archive")
    outer = content.record_bytes(raw, matches[0])
    require(outer[:4] == bytes.fromhex("00250003") and len(outer) <= content.MAX_RECORD, "Planet native version/header")
    inner = outer[4:]
    children = content.parse_archive(inner)
    require(len(children) == 1 and children[0]["key"] == "00000001!00000001.00000001", "Planet inner DBPF shape")
    payload = content.record_bytes(inner, children[0])
    # This consequence is a generated, uninhabited planet. Populated home
    # records carry a nonzero serializer object prefix and need their own graph
    # qualification; do not silently treat that prefix as padding.
    require(payload[:16] == bytes(16), "Only the empty-object generated planet prefix is qualified")
    tables, offset = [], 16
    while offset < len(payload):
        fields, end = artifact.attributes(payload, offset)
        tables.append((offset, end, fields))
        offset = end
        require(len(tables) <= 16, "Planet ClassSerializer block budget")
    require(offset == len(payload), "Trailing native planet bytes")
    def resource(attribute):
        group, kind, instance = struct.unpack(">III", artifact.field(tables, attribute, 12))
        return [instance, kind, group]
    return {"planet_id": planet_id, "record_key": key, "outer_record_sha256": digest(outer),
            "inner_record_sha256": digest(payload), "spice": resource(0x03D58B9C), "terrain": resource(0x03D58B9B),
            "rotation_axis": list(struct.unpack(">III", artifact.field(tables, 0x0417A8E9, 12))),
            "rotation_period": artifact.be32(artifact.field(tables, 0x0417A902, 4))}


def correlate_effect(spec, reports, internals):
    expected = {"kind", "owner", "reader", "planet_id", "owner_input_planets", "owner_planets",
                "reader_input_planets", "owner_stars", "reader_input_stars", "owner_input_location",
                "owner_retained_location", "reader_input_location", "owner_temp_planets", "reader_input_temp_planets"}
    require(type(spec) is dict and set(spec) == expected, "Effect specification fields")
    require(spec["kind"] == "native_planet_spice_initialization", "Unqualified native consequence kind")
    owner_label, reader_label = spec["owner"], spec["reader"]
    require(owner_label != reader_label and owner_label in reports and reader_label in reports, "Distinct observed owner and reader required")
    owner, reader = internals[owner_label], internals[reader_label]
    require(reports[owner_label]["pid"] != reports[reader_label]["pid"] and
            reports[owner_label]["generation"] != reports[reader_label]["generation"], "Fresh isolated reader process/generation required")
    planet_id = artifact.assigned(spec["planet_id"])
    source_planets = pinned_file(spec["owner_input_planets"], content.MAX_ARCHIVE)
    owner_planets = pinned_file(spec["owner_planets"], content.MAX_ARCHIVE)
    reader_planets = pinned_file(spec["reader_input_planets"], content.MAX_ARCHIVE)
    owner_stars = pinned_file(spec["owner_stars"], content.MAX_ARCHIVE)
    reader_stars = pinned_file(spec["reader_input_stars"], content.MAX_ARCHIVE)
    owner_temp = pinned_file(spec["owner_temp_planets"], content.MAX_ARCHIVE)
    reader_temp = pinned_file(spec["reader_input_temp_planets"], content.MAX_ARCHIVE)
    require(owner_planets == reader_planets and owner_stars == reader_stars and owner_temp == reader_temp,
            "Reader global files must be byte-identical owner output, without record rewriting")
    require(not content.parse_archive(owner_temp), "Only a closed empty temporary planet archive is qualified")
    retained_location = pinned_file(spec["owner_input_location"], content.MAX_ARCHIVE)
    require(retained_location == pinned_file(spec["owner_retained_location"], content.MAX_ARCHIVE)
            == pinned_file(spec["reader_input_location"], content.MAX_ARCHIVE),
            "Other location must be the exact retained original input file")
    content.parse_archive(retained_location)
    content.parse_archive(owner_stars)
    record = planet_record(owner_planets, planet_id)
    source_entries = content.parse_archive(source_planets)
    source_present = any(entry["key"] == record["record_key"] for entry in source_entries)
    source_record = planet_record(source_planets, planet_id) if source_present else None
    require(source_record is None or source_record["spice"] == [0, 0, 0],
            "Owner input already contains a selected spice outcome")
    mutations = [s for s in owner["samples"]["assign_planet_spice"] if s["after"][0] == planet_id and s["before"][4:7] != s["after"][4:7]]
    require(len(mutations) == 1, "Exactly one native owner spice change for chosen PlanetID required")
    effect = mutations[0]
    require(effect["caller_rva"] == 0x8719A7 and effect["args"][0] == 0
            and effect["before"][4:7] == [0, 0, 0] and effect["after"][4:7] == record["spice"] != [0, 0, 0],
            "Owner original setup callsite/force/initialization/output mismatch")
    require(not any(s["before_valid"] and s["before"][0] == planet_id & 0x00FFFFFF
                    for s in owner["samples"]["generate_star"]), "Unqualified owner generation path for selected star")
    saves = [s for s in owner["rows"] if s.get("event") == "native_save_return" and s.get("native_success") is True
             and s["sequence"] > effect["sequence"]]
    require(saves, "No successful controlled original owner Save after the native consequence")
    cached = [(mark_id, mark) for mark_id, mark in owner["marks"].items() if mark["complete"]
              and mark["phase"] == "before_save" and effect["sequence"] < mark["sequence"] < saves[-1]["sequence"]
              and planet_id in mark["planets"]]
    require(cached and all(mark["planets"][planet_id][4:7] == record["spice"] for _, mark in cached),
            "Owner pre-Save native census does not match its closed spice record")
    for row in reader["samples"]["assign_planet_spice"]:
        if row["after"][0] == planet_id:
            require(row["caller_rva"] == 0x8719A7 and row["args"][0] == 0
                    and row["before"][4:7] == row["after"][4:7] == record["spice"],
                    "Reader reran or changed the selected global spice outcome")
    require(not any(s["before_valid"] and s["before"][0] == planet_id & 0x00FFFFFF for s in reader["samples"]["generate_star"]),
            "Reader regenerated the selected native star")
    adopted = [(mark_id, mark) for mark_id, mark in reader["marks"].items() if mark["complete"]
               and mark["phase"] in ("scene_ready", "checkpoint", "before_save") and planet_id in mark["planets"]]
    require(adopted, "Fresh original reader never exposed the selected cached planet")
    for _, mark in adopted:
        require(mark["planets"][planet_id][4:7] == record["spice"], "Fresh original reader spice differs from owner output")
        star = mark["stars"].get(planet_id & 0x00FFFFFF)
        require(star and star[1] == 5, "Fresh original reader selected star count is outside this bounded case")
    require(any(row.get("event") == "native_load_complete" for row in reader["rows"]), "Fresh native controlled Load completion missing")
    return {"correlation_validated": True, "kind": spec["kind"], "owner": owner_label, "reader": reader_label,
            "record": record, "owner_input_record_present": source_present, "owner_input_record": source_record,
            "owner_native_sequence": effect["sequence"], "owner_spice_before": effect["before"][4:7],
            "owner_before_save_marks": [m for m, _ in cached], "retained_location_sha256": digest(retained_location),
            "owner_save_sequences": [s["sequence"] for s in saves], "reader_adoption_marks": [m for m, _ in adopted],
            "global_archive_sha256": {"planetRecords.pkp": digest(owner_planets), "stars.db": digest(owner_stars),
                                      "planetRecords.pkt": digest(owner_temp)},
            "selected_reader_regeneration_calls": 0, "selected_owner_spice_changes": 1,
            "canonical_owner_authorized": False, "durable_publication_qualified": False,
            "limitation": "Caller must independently prove these immutable input files were staged into this reader's isolated live profile before launch; coordinator ownership, durable publication and kill/revocation recovery are separate gates."}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", type=Path, required=True, help="Schema-1 pinned run inputs and optional exact effect specification")
    parser.add_argument("--output", type=Path, required=True, help="New private local/ JSON report")
    parser.add_argument("--confirmed-closed", action="store_true", required=True)
    args = parser.parse_args(argv)
    try:
        spec_raw = artifact.read_file(args.spec, 65536)
        spec = artifact.strict_json(spec_raw)
        require(type(spec) is dict and set(spec) in ({"schema_version", "runs"}, {"schema_version", "runs", "effect"})
                and spec["schema_version"] == 1 and type(spec["runs"]) is list and 1 <= len(spec["runs"]) <= 4,
                "Observation specification schema")
        reports, internals = {}, {}
        for run in spec["runs"]:
            report, internal = analyze_run(run)
            require(report["label"] not in reports, "Duplicate run label")
            reports[report["label"]], internals[report["label"]] = report, internal
        effect = correlate_effect(spec["effect"], reports, internals) if "effect" in spec else None
        for path, limit, expected in INPUTS:
            require(digest(artifact.read_file(path, limit)) == expected, "Input changed during observation audit")
        require(artifact.read_file(args.spec, 65536) == spec_raw, "Specification changed during audit")
        report = {"schema_version": 1, "kind": "native-global-observer-audit", "evidence_class": "HOST",
                  "closed_native_observations_validated": True, "spec_sha256": digest(spec_raw),
                  "runs": reports, "effect": effect, "global_ownership_qualified": False, "m10_l07_complete": False,
                  "limits": ["Passive counters cover exactly eight guarded native functions, not every native global mutation.",
                             "Ticker census excludes deferred removals; record census covers only the declared cached subset.",
                             "Callback tick details are first per mode; all counters and bounded mutation details are checked.",
                             "A matching native value and archive are not a durable coordinator publication or authority lease."]}
        output = content.private_output(args.output)
        require(not output.exists() and output.parent.is_dir(), "Fresh report file in an existing private directory required")
        with output.open("xb") as stream:
            stream.write(content.canonical(report) + b"\n")
            stream.flush()
            os.fsync(stream.fileno())
        print(json.dumps({"closed_native_observations_validated": True, "effect_correlated": effect is not None,
                          "runs": list(reports), "m10_l07_complete": False}, sort_keys=True))
        return 0
    except (ValueError, OSError, KeyError, TypeError, OverflowError, struct.error, UnicodeError) as error:
        print(json.dumps({"closed_native_observations_validated": False, "error": str(error)}, sort_keys=True))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
