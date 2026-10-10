"""Seal one closed original Haagos owner output as an M10 candidate head.

The entire owner profile is retained. The non-owner Satiria save must equal
the selected immutable head. No record merge, live-profile write, coordinator
publication, or fresh-reader acceptance is performed by this HOST tool.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path, PureWindowsPath
import re
import struct
import sys

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SPEC = importlib.util.spec_from_file_location("global_checkpoint_observer", HERE / "native-global-observer.py")
observer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(observer)
artifact, content = observer.artifact, observer.content
require, digest, uint = artifact.require, artifact.digest, artifact.uint
MAX_BYTES = 80 * 1024 * 1024
MAGIC = 0x39424D53
WORLD = ("Games/Game0/Satiria.spo", "Games/Game0/planetRecords.pkp", "Games/Game0/planetRecords.pkt",
         "Games/Game0/PlanetScripts.pld", "Games/Game0/stars.db", "Planets.package")
SATIRIA, HAAGOS = "spore/Games/Game0/Satiria.spo", "spore/Games/Game0/Haagos.spo"
PLANETS, TEMP, STARS = ("spore/" + WORLD[i] for i in (1, 2, 4))


def member_name(name):
    require(type(name) is str and 0 < len(name) <= 240 and name.startswith(("spore/", "creations/")), "Member root")
    for part in name.split("/"):
        require(part and part not in (".", "..") and part[-1] not in ". "
                and all(32 <= ord(c) < 127 and c not in '\\:*?"<>|' for c in part), "Unsafe member path")
        stem = part.split(".")[0].upper()
        require(stem not in ("CON", "PRN", "AUX", "NUL") and re.fullmatch(r"(?:COM|LPT)[0-9]", stem) is None,
                "Reserved member name")
    return name


def parse_checkpoint(raw):
    require(len(raw) <= MAX_BYTES, "Checkpoint size limit")
    position = 0
    def take(size):
        nonlocal position
        require(0 <= size <= len(raw) - position, "Truncated checkpoint")
        value = raw[position:position + size]
        position += size
        return value
    def word():
        return struct.unpack("<I", take(4))[0]
    require(word() == MAGIC and word() == 2, "Selected two-location schema2 checkpoint required")
    header = {key: take(32).hex() for key in ("build", "executable", "content")}
    header["world"] = [take(32).hex() for _ in WORLD]
    header["provenance"] = take(32).hex()
    save_request, rollback, *ids = struct.unpack("<QI4I", take(28))
    require(not save_request and not rollback and 0 < ids[0] < 0xFFFFFFFF and 0 < ids[1] < 0xFFFFFFFF
            and ids[0] != ids[1] and ids[2:] == [0, 0], "Schema2 original game identities")
    require(all(value != "0" * 64 for value in [header[k] for k in ("build", "executable", "content", "provenance")]
                + header["world"]), "Empty checkpoint identity")
    header["native_ids"] = ids
    count = word()
    require(7 <= count <= 512, "Checkpoint member count")
    members, folded = {}, set()
    for _ in range(count):
        size = word()
        require(size <= 240, "Member name limit")
        name = member_name(take(size).decode("ascii"))
        require(name.casefold() not in folded, "Duplicate checkpoint member")
        folded.add(name.casefold())
        expected, size = take(32).hex(), word()
        require(size <= MAX_BYTES, "Member byte limit")
        data = take(size)
        require(digest(data) == expected, "Member SHA mismatch")
        members[name] = data
    require(position == len(raw), "Trailing checkpoint bytes")
    for index, name in enumerate(WORLD):
        require("spore/" + name in members and digest(members["spore/" + name]) == header["world"][index], "World member mismatch")
    require(HAAGOS in members, "Other original save missing")
    header["artifact_sha256"] = digest(raw)
    return header, members


def encode_checkpoint(previous, members, proof):
    require(7 <= len(members) <= 512, "Candidate member count")
    raw = bytearray(struct.pack("<II", MAGIC, 2))
    for key in ("build", "executable", "content"):
        raw.extend(bytes.fromhex(previous[key]))
    for name in WORLD:
        raw.extend(bytes.fromhex(digest(members["spore/" + name])))
    raw.extend(bytes.fromhex(digest(content.canonical(proof))))
    raw.extend(struct.pack("<QI4II", 0, 0, *previous["native_ids"], len(members)))
    folded = set()
    for name, data in sorted(members.items()):
        encoded = member_name(name).encode("ascii")
        require(name.casefold() not in folded, "Duplicate candidate member")
        folded.add(name.casefold())
        raw.extend(struct.pack("<I", len(encoded)) + encoded + bytes.fromhex(digest(data)) + struct.pack("<I", len(data)) + data)
        require(len(raw) <= MAX_BYTES, "Candidate artifact byte budget")
    parse_checkpoint(raw)
    return bytes(raw)


def read_owner_profile(root):
    root = content.private_output(root)
    require(root.is_dir(), "Closed owner directory missing")
    members, paths, folded, total = {}, {}, set(), 0
    for folder, prefix in (("Spore", "spore"), ("My Spore Creations", "creations")):
        base = content.safe_path(root / folder)
        require(base.is_dir(), "Complete owner profile and creation library required")
        for path in sorted(base.rglob("*")):
            path = content.safe_path(path)
            if path.is_dir():
                continue
            name = member_name(prefix + "/" + path.relative_to(base).as_posix())
            require(name.casefold() not in folded, "Duplicate owner path")
            folded.add(name.casefold())
            data = artifact.read_file(path, MAX_BYTES)
            total += len(data)
            require(total <= MAX_BYTES and len(members) < 512, "Owner profile budget")
            members[name], paths[name] = data, path
    manifest = [{"name": name, "bytes": len(data), "sha256": digest(data)} for name, data in sorted(members.items())]
    return members, paths, manifest


def saved_owner(rows, hosts, previous, request, owner):
    require(type(owner) is dict and set(owner) == {"worker", "worker_generation", "location", "native_load_slot"}, "Owner scope fields")
    require(uint(owner["worker"], 64) and uint(owner["worker_generation"], 64)
            and owner["native_load_slot"] == 1, "This projection requires the declared Haagos owner")
    location = owner["location"]
    require(type(location) is list and len(location) == 4 and all(uint(v) for v in location), "Owner location")
    require(uint(request, 64), "Save request required")
    def one(name):
        found = [r for r in rows if r.get("event") == name and r.get("request") == request]
        require(len(found) == 1, "Missing or duplicate same-request native event: " + name)
        return found[0]
    enter, saved = one("native_save_enter"), one("native_save_return")
    complete = one("native_checkpoint_save_complete")
    barrier, extension = one("native_checkpoint_barrier"), one("native_checkpoint_extension_written")
    epoch = enter.get("epoch")
    require(saved.get("native_success") is True and saved.get("requested_epoch") == saved.get("observed_epoch") == epoch
            and complete.get("success") is True and complete.get("diagnostic_only") is False
            and extension.get("success") is True and extension.get("schema") == barrier.get("schema") == 3
            and barrier["sequence"] < enter["sequence"] < extension["sequence"] < saved["sequence"] < complete["sequence"]
            and all(r.get("epoch") == epoch for r in (saved, complete, barrier, extension)), "Original Save/extension/epoch did not complete")
    require(PureWindowsPath(enter.get("fixture", "")).name == "Haagos.spo", "Save selected a different original location")
    require(not any(r.get("event") == "native_save_enter" and r["sequence"] > saved["sequence"] for r in rows),
            "Chosen Save is not the final controlled Save")
    worlds = [r for r in rows if r.get("event") == "native_world_observed" and r.get("epoch") == epoch]
    require(worlds and all([location[1], location[2], location[3]] ==
            [r["native_home_star_id_candidate"], r["native_home_planet_id_candidate"], r["generated_terrain_instance"]]
            and r.get("home_world") is True for r in worlds), "Native owner home does not match location")
    mappings = [r for r in rows if r.get("event") == "network_native_owner_mapping"]
    require(len(mappings) == 1 and mappings[0].get("slot1_owner") == 2 and mappings[0].get("slot2_owner") == 1,
            "Qualified Haagos native/canonical slot mapping missing")
    primary = [r for r in rows if r.get("event") == "native_checkpoint_player" and r.get("request") == request
               and r.get("phase") == "before_save" and r.get("primary") is True]
    require(len(primary) == 1 and primary[0].get("unique_game_id") == previous["native_ids"][1], "Original resident game identity changed")
    bridges = [r for r in hosts if r.get("event") == "module" and PureWindowsPath(r.get("path", "")).name.lower() == "sporemp.bridge.dll"]
    require(bridges and all(r.get("sha256") == previous["build"] and r.get("game_pid") == rows[0]["pid"] for r in bridges),
            "Observed original bridge differs from selected head")
    network = [r for r in hosts if r.get("event") == "network_config_validated"]
    require(len(network) == 1 and network[0].get("role") == "authority", "Native authority host required")
    return {"request": request, "epoch": epoch, "enter_sequence": enter["sequence"], "return_sequence": saved["sequence"],
            "complete_sequence": complete["sequence"], "resident_unique_game_id": primary[0]["unique_game_id"]}


def owner_effect(internal, previous_members, members, planet_id, save, owner=None, selected_hash=None):
    artifact.assigned(planet_id)
    require(not content.parse_archive(members[TEMP]), "Only empty closed temporary planet archive qualified")
    record = observer.planet_record(members[PLANETS], planet_id)
    present = any(e["key"] == record["record_key"] for e in content.parse_archive(previous_members[PLANETS]))
    previous = observer.planet_record(previous_members[PLANETS], planet_id)["spice"] if present else [0, 0, 0]
    preserved = previous != [0, 0, 0]
    changes = [r for r in internal["samples"]["assign_planet_spice"] if r["after"][0] == planet_id
               and r["before"][4:7] != r["after"][4:7]]
    if preserved:
        # Subsequent whole-world Saves need not regenerate a global outcome.
        # Require the guarded original owner to read the exact selected value,
        # retain it through Save, and report zero new assignments explicitly.
        require(owner is not None and selected_hash is not None and previous == record["spice"] and not changes,
                "Previously initialized spice changed or lacks owner scope")
        observed = [r for r in internal["samples"]["assign_planet_spice"] if r["before"][0] == planet_id]
        require(observed, "Guarded original owner did not observe the selected spice")
        for sample in observed:
            require(sample["before_valid"] and sample["after_valid"] and sample["caller_rva"] == 0x8719A7
                    and sample["args"][0] == 0 and sample["before"][4:7] == sample["after"][4:7] == previous
                    and sample["sequence"] < save["enter_sequence"] and sample.get("global_guard_active") is True
                    and sample.get("global_guard_owner") is True, "Unqualified preserved native spice observation")
            grants = [r for r in internal["rows"] if r.get("event") == "native_global_grant_installed"
                      and r.get("grant_nonce") == sample.get("grant_nonce") and r["sequence"] < sample["sequence"]]
            require(len(grants) == 1, "Preserved outcome has no exact preceding native grant")
            grant = grants[0]
            require(grant["native_role"] == 2 and grant["producer_mask"] == 15
                    and grant["worker"] == grant["global_owner_worker"] == owner["worker"]
                    and grant["worker_generation"] == grant["global_owner_generation"] == owner["worker_generation"]
                    and grant["checkpoint_sha256"] == selected_hash and grant["global_guard_armed"] is True,
                    "Preserved outcome grant belongs to another owner or selected head")
        change = observed[0]
    else:
        require(len(changes) == 1, "Exactly one owner spice assignment required")
        change = changes[0]
        require(change["before_valid"] and change["after_valid"] and change["caller_rva"] == 0x8719A7 and change["args"][0] == 0
                and change["before"][4:7] == [0, 0, 0] and change["after"][4:7] == record["spice"] != [0, 0, 0]
                and change["sequence"] < save["enter_sequence"], "Original assignment or persisted spice differs")
    require(not any(r["before_valid"] and r["before"][0] == planet_id & 0x00FFFFFF
                    for r in internal["samples"]["generate_star"]), "Selected star used unqualified generation path")
    before = [mark for mark in internal["marks"].values() if mark["complete"] and mark["phase"] == "before_save"
              and save["enter_sequence"] < mark["sequence"] < save["return_sequence"] and planet_id in mark["planets"]]
    require(len(before) == 1 and before[0]["planets"][planet_id][4:7] == record["spice"], "Same-Save native cached spice missing")
    return {"kind": "native_planet_spice_preserved" if preserved else "native_planet_spice_initialization",
            "record": record, "native_sequence": change["sequence"],
            "owner_spice_changes": 0 if preserved else 1, "fresh_reader_qualified": False}


def build_candidate(spec):
    require(type(spec) is dict and set(spec) == {"schema_version", "selected", "owner", "run", "owner_profile", "save_request", "planet_id"}
            and spec["schema_version"] == 1, "Candidate specification fields")
    observer.INPUTS.clear()
    selected = observer.pinned_file(spec["selected"], MAX_BYTES)
    previous, selected_members = parse_checkpoint(selected)
    require(previous["executable"] == artifact.EXE, "Unsupported selected executable")
    run, internal = observer.analyze_run(spec["run"])
    hosts = artifact.jsonl(observer.pinned_file(spec["run"]["host_trace"], artifact.MAX_TRACE))
    profile = spec["owner_profile"]
    require(type(profile) is dict and set(profile) == {"path", "manifest_sha256"}, "Pinned complete owner profile required")
    members, paths, manifest = read_owner_profile(profile["path"])
    require(digest(content.canonical(manifest)) == profile["manifest_sha256"], "Closed profile manifest differs")
    require(SATIRIA in members and members[SATIRIA] == selected_members[SATIRIA], "Non-owner original save changed")
    require(HAAGOS in members, "Owner original save missing")
    require(set(selected_members) <= set(members), "Owner output dropped selected profile members")
    for name in (SATIRIA, HAAGOS, STARS):
        content.parse_archive(members[name])
    save = saved_owner(internal["rows"], hosts, previous, spec["save_request"], spec["owner"])
    effect = owner_effect(internal, selected_members, members, spec["planet_id"], save, spec["owner"], digest(selected))
    proof = {"schema_version": 1, "kind": "closed-native-owner-candidate", "selected_artifact_sha256": digest(selected),
             "owner": spec["owner"], "run": run, "save": save, "effect": effect, "member_manifest": manifest,
             "unchanged_nonowner_sha256": digest(members[SATIRIA]), "record_merge": False,
             "canonical_owner_authorized": False, "durable_publication": False, "fresh_reader_qualified": False}
    result = encode_checkpoint(previous, members, proof)
    for path, limit, expected in observer.INPUTS:
        require(digest(artifact.read_file(path, limit)) == expected, "Pinned input changed while sealing")
    final, final_paths, _ = read_owner_profile(profile["path"])
    require(final == members and final_paths == paths, "Closed owner tree changed while sealing")
    return result, proof


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New directory under ignored local/")
    parser.add_argument("--confirmed-closed", action="store_true", required=True)
    args = parser.parse_args(argv)
    try:
        sys.path.insert(0, str(REPO / "tools/launcher"))
        import launcher_service as service
        require(not service.diag.game_running(), "Close all original SPORE processes before sealing")
        raw = artifact.read_file(args.spec, 65536)
        result, proof = build_candidate(artifact.strict_json(raw))
        require(not service.diag.game_running() and artifact.read_file(args.spec, 65536) == raw, "Inputs or native process state changed")
        output = content.private_output(args.output)
        require(not output.exists() and output.parent.is_dir(), "Fresh private output directory required")
        output.mkdir()
        for name, data in (("checkpoint.smp", result), ("native-owner-proof.json", content.canonical(proof) + b"\n")):
            with (output / name).open("xb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
        print(json.dumps({"sealed": True, "artifact_sha256": digest(result), "artifact_bytes": len(result),
                          "durable_publication": False, "fresh_reader_qualified": False}, sort_keys=True))
        return 0
    except (ValueError, OSError, KeyError, TypeError, OverflowError, struct.error, UnicodeError) as error:
        print(json.dumps({"sealed": False, "error": str(error)}, sort_keys=True))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
