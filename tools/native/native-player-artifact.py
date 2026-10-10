"""Validate a closed, selected original-player export and emit M10F schema 3.

HOST format/provenance validation only. No engine execution, archive rewriting,
asset extraction, ownership authorization, or native transfer acceptance.
The caller must authenticate the selected identity and bind both output hashes
to its transfer receipt. Native serialization tokens are never emitted.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import stat
import struct

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("native_player_content", REPO / "tools/content/spore_content.py")
content = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(content)

MAX_ARTIFACT = 1024 * 1024
MAX_RECORD = MAX_ARTIFACT
MAX_WITNESS = 16384
MAX_TRACE = 128 * 1024 * 1024
MAX_ROWS = 200000
MAX_APPROVAL = 20680
VERSION = 0x00250003
EXE = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37"
SDK = "cbf9206b9a823f0911cd9be0217104a49d72380b"
ROOT = "53504d50!00000001.53504d47"
STAGE = "53504d50!00000001.53504d53"
INDEX = "00000000!0179d304.0179d310"
PLAYER, AVATAR, ITEMS = 0x02C21781, 0x018EB45E, 0x03A3AC9F
CLASSES = (PLAYER, AVATAR, ITEMS)
OBSERVED_REFERENCE_CLASSES = set(CLASSES) | {0x018C84A9, 0x01BE418E, 0x02C9CC91, 0x03A2511E, 0x52AA6122}
ENVELOPE = {"schema_version", "evidence_class", "harness", "sequence", "pid", "thread_id", "qpc", "qpc_frequency", "epoch", "executing_command", "foreign_callbacks", "ability_scope", "strike_scope", "animal_damage_scope", "pickup_scope", "request", "phase"}
PROGRESS = ((0x055A9786, "goal_progress"), (0x055A97A7, "goal_total"), (0x06132C98, "social_progress"), (0x06132CA7, "combat_progress"))
STAGE_NAMES = ("brain", "normalizing_scale_bits", "flaps", "ability_mode", "dna_bits", "deprecated_abilities_low", "deprecated_abilities_high", "learned_0", "learned_1", "learned_2")
WITNESS_FIELDS = ("schema", "op", "id", "player", "revision", "avatar_generation", "worker", "worker_generation", "session", "scene", "baseline", "preparation", "challenge", "artifact", "approval", "artifact_bytes", "approval_bytes", "unique_game_id", "canonical_home", "source_player", "source_avatar", "source_slot", "native_player", "native_avatar", "native_slot", "pause_request", "input_epoch", "app_begin", "app_end", "ai_begin", "ai_end", "zero_deltas", "nonzero_deltas", "elapsed_ms", "pause_before", "pause_after", "remaining_imports", "process_id", "process_created")
WITNESS_DIGESTS = {"preparation", "challenge", "artifact", "approval"}
WITNESS_U32 = {"schema", "op", "artifact_bytes", "approval_bytes", "unique_game_id", "canonical_home", "source_player", "source_avatar", "source_slot", "native_player", "native_avatar", "native_slot", "pause_before", "pause_after", "remaining_imports", "process_id"}


class ArtifactError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ArtifactError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def uint(value, bits=32):
    require(type(value) is int and 0 <= value < 1 << bits, f"Expected unsigned {bits}-bit value")
    return value


def assigned(value):
    uint(value)
    require(value not in (0, 0xFFFFFFFF), "Unassigned native identity")
    return value


def be32(data, offset=0):
    require(0 <= offset <= len(data) - 4, "Truncated big-endian word")
    return struct.unpack_from(">I", data, offset)[0]


def finite(bits):
    value = struct.unpack(">f", struct.pack(">I", uint(bits)))[0]
    require(math.isfinite(value), "Nonfinite native float")
    return value


def read_file(path, limit):
    path = content.safe_path(path)
    info = path.stat()
    require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1, "Regular unaliased file required")
    return content.read_bounded(path, limit)


def attributes(data, offset):
    require(be32(data, offset) == 0x01A80D26, "Missing ClassSerializer marker")
    length, count = be32(data, offset + 4), be32(data, offset + 8)
    end = offset + length + 8
    require(count <= 128 and length >= 4 + count * 8 and end <= len(data), "Attribute table bounds")
    position = offset + 12 + count * 8
    result = {}
    for i in range(count):
        key, size = struct.unpack_from(">II", data, offset + 12 + i * 8)
        require(key not in result and size <= end - position, "Duplicate or truncated attribute")
        result[key] = data[position:position + size]
        position += size
    require(position == end, "Trailing ClassSerializer bytes")
    return result, end


def blocks(data):
    found, position = [], 0
    while True:
        at = data.find(b"\x01\xa8\x0d\x26", position)
        if at < 0:
            return found
        position = at + 4
        try:
            fields, end = attributes(data, at)
        except (ArtifactError, struct.error):
            continue
        found.append((at, end, fields))
        require(len(found) <= 128, "Too many nested attribute tables")


def field(tables, key, size=None):
    values = [fields[key] for _, _, fields in tables if key in fields]
    require(len(values) == 1, f"Missing or ambiguous attribute {key:08x}")
    require(size is None or len(values[0]) == size, f"Attribute width {key:08x}")
    return values[0]


def number(tables, key):
    return be32(field(tables, key, 4))


def object_frame(data, kind):
    require(12 <= len(data) <= MAX_RECORD, "Class stream size")
    size, checksum, token = struct.unpack_from(">III", data)
    require(size == len(data), "Expected exactly one complete class object")
    assigned(token)
    body = data[12:]
    tables = blocks(body)
    outer = [(s, e, a) for s, e, a in tables if not any(s > ss and e <= ee for ss, ee, _ in tables)]
    # Pinned native inheritance layout; non-attribute data is explicitly bounded.
    if kind == PLAYER:
        require(len(body) >= 24 and body[:4] == bytes.fromhex("02c216ed") and body[20:24] == bytes.fromhex("017f243b"), "Player prefix")
        for off in (8, 12, 16):
            require(0 <= finite(be32(body, off)) <= 1, "Player identity color")
        gaps = [body[:24], b""]
    elif kind == AVATAR:
        gaps = [bytes.fromhex(s) for s in ("d0036e08ce9f6639017f243b", "0135b1e8", "0116dd1b01186577", "", "013f94d400000000", "00000000", "", "")]
    else:
        gaps = [bytes.fromhex("03a3aa3a")]
    require(len(outer) == len(gaps), "Unknown native inheritance layout")
    previous = 0
    for (start, end, _), gap in zip(outer, gaps):
        require(body[previous:start] == gap, "Unqualified bytes between native attribute tables")
        previous = end
    require(previous == len(body), "Trailing native object bytes")
    return {"token": token, "body": body, "tables": tables, "checksum": checksum}


def decode_stage(data):
    fields, end = attributes(data, 0)
    require(len(data) == end == 188, "Stage must be one exact 188-byte serializer")
    expected = {0xB4177E0E, 0x061AFD98, 0x061AFD99, 0xB4177E0F, 0x7220206F, 0xB4177E0D, 0x076B6609}
    require(set(fields) == expected, "Unknown or missing persistent stage attributes")
    tables = [(0, end, fields)]
    old = int.from_bytes(field(tables, 0xB4177E0D, 8), "big")
    flags = field(tables, 0x076B6609, 92)
    require(be32(flags) == 88 and all(value in (0, 1) for value in flags[4:]), "Invalid learned ability flags")
    learned = [0, 0, 0]
    for bit, value in enumerate(flags[4:]):
        learned[bit >> 5] |= value << (bit & 31)
    words = [number(tables, key) for key in (0xB4177E0E, 0x061AFD98, 0x061AFD99, 0xB4177E0F, 0x7220206F)] + [old & 0xFFFFFFFF, old >> 32] + learned
    require(words[0] <= 3 and words[2] <= 1000 and words[3] <= 1, "Persistent stage range")
    require(finite(words[1]) > 0 and finite(words[4]) >= 0 and not words[9] & 0xFF000000, "Persistent stage value")
    return words


def decode_artifact(raw):
    require(isinstance(raw, bytes) and 96 <= len(raw) <= MAX_ARTIFACT and raw[:4] == b"DBPF", "Bounded DBPF required")
    entries = content.parse_archive(raw)
    expected = {ROOT, STAGE, INDEX} | {f"00000000!0179d304.{kind:08x}" for kind in CLASSES}
    require(len(entries) == 6 and {entry["key"] for entry in entries} == expected, "Exact schema-3 six-key set required")
    require(all(entry["size"] <= MAX_RECORD for entry in entries) and sum(entry["size"] for entry in entries) <= MAX_ARTIFACT, "Expanded record budget")
    records = {entry["key"]: content.record_bytes(raw, entry) for entry in entries}
    index = records[INDEX]
    require(len(index) == 36 and struct.unpack_from(">III", index) == (1, 3, 3), "Class index version/count")
    pairs = [struct.unpack_from(">II", index, 12 + i * 8) for i in range(3)]
    require(len(set(pairs)) == 3 and dict(pairs) == {kind: 1 for kind in CLASSES}, "Class index identities")
    require(len(records[ROOT]) == 40, "Root record width")
    root = struct.unpack(">10I", records[ROOT])
    require(root[:2] == (0x5030314D, 3) and root[7] == PLAYER and root[9] == AVATAR, "Root schema/classes")
    for value in root[2:6]:
        assigned(value)
    require(root[2] != root[4], "Duplicate player/avatar noun ID")
    objects = {kind: object_frame(records[f"00000000!0179d304.{kind:08x}"], kind) for kind in CLASSES}
    require(len({obj["token"] for obj in objects.values()}) == 3, "Duplicate serialization identities")
    player, avatar, items = (objects[kind] for kind in CLASSES)
    require(root[6] == player["token"] and root[8] == avatar["token"], "Unresolved root reference")
    require(number(player["tables"], 0x03CDD54B) == root[2] and number(player["tables"], 0x05EE017B) == root[3] and number(avatar["tables"], 0x03CDD54B) == root[4], "Root native identity mismatch")
    require(not any(0x03CDD54B in a for _, _, a in items["tables"]), "Collection unexpectedly owns noun ID")
    for obj in (player, avatar):
        require(field(obj["tables"], 0x0240E421, 4) == bytes(4), "Concrete native owner reference")
    require(field(avatar["tables"], 0x02415693, 4) == bytes(4) and field(avatar["tables"], 0x72AE9FE0, 4) == bytes(4), "Source scene references not cut")
    expected_ref = struct.pack(">II", items["token"], ITEMS)
    require(field(player["tables"], 0x03A3A4B4, 8) == expected_ref, "Player collection reference")
    pattern = re.compile(b"|".join(re.escape(struct.pack(">I", kind)) for kind in sorted(OBSERVED_REFERENCE_CLASSES)))
    edges = 0
    for kind, obj in objects.items():
        for match in pattern.finditer(obj["body"]):
            if match.start() < 4:
                continue
            reference = obj["body"][match.start() - 4:match.end()]
            if be32(reference) in (0, 0xFFFFFFFF):
                continue
            require(kind == PLAYER and reference == expected_ref, "Unexpected known-class concrete reference")
            edges += 1
    require(edges == 1, "Expected one player-to-collection edge")
    status = field(items["tables"], 0x03A3B3D4)
    unlock = field(items["tables"], 0x06676971)
    ns, nu = be32(status), be32(unlock)
    require(ns <= 1024 and nu <= 1024 and len(status) == 4 + ns * 9 and len(unlock) == 4 + nu * 8, "Collection count/length")
    statuses = [struct.unpack_from(">IIB", status, 4 + i * 9) for i in range(ns)]
    unlocked = [struct.unpack_from(">II", unlock, 4 + i * 8) for i in range(nu)]
    require(len({row[:2] for row in statuses}) == ns, "Duplicate item status key")
    progress = [number(player["tables"], key) for key, _ in PROGRESS]
    for bits in progress:
        finite(bits)
    group, kind, instance = struct.unpack(">III", field(avatar["tables"], 0x03F64A78, 12))
    return {"artifact_sha256": digest(raw), "artifact_bytes": len(raw), "root": {"player_native_id": root[2], "unique_game_id": root[3], "avatar_native_id": root[4], "canonical_home": root[5]},
            "species": [instance, kind, group], "age": number(avatar["tables"], 0x52773F6F), "animal_archetype": number(avatar["tables"], 0x33ACE6B5), "general_flags": number(avatar["tables"], 0x051B38FF),
            "progression_bits": progress, "statuses": sorted(statuses), "unlocked": unlocked, "unlock_points": number(items["tables"], 0x03A3B30D), "stage_words": decode_stage(records[STAGE]),
            "limits": ["Original object frame checksum is bounded but not independently recomputed.", "Concrete-reference audit covers typed owner/scene/collection fields and every class in the observed native eight-class graph; it is not a universal pointer or external-asset proof."]}


def strict_json(data, identical_epoch=False):
    def pairs(values):
        result = {}
        for key, value in values:
            if key in result:
                # The native event envelope and some pinned event payloads both
                # emit epoch. Accept that one historical redundancy only when
                # its integer value is identical; never apply this to receipts.
                require(identical_epoch and key == "epoch" and type(value) is int and type(result[key]) is int and value == result[key], "Duplicate JSON field")
                continue
            result[key] = value
        return result
    def invalid(value):
        raise ArtifactError("Nonfinite JSON value")
    return json.loads(data, object_pairs_hook=pairs, parse_constant=invalid)


def jsonl(data, native=False):
    require(data.endswith(b"\n") and len(data) <= MAX_TRACE, "Incomplete or oversized trace")
    lines = data.splitlines()
    require(0 < len(lines) <= MAX_ROWS and all(0 < len(line) <= 65536 for line in lines), "Trace row budget")
    rows = [strict_json(line, identical_epoch=native) for line in lines]
    require(all(type(row) is dict for row in rows), "Trace objects required")
    return rows


def only(rows, event, request=None):
    found = [row for row in rows if row.get("event") == event and (request is None or row.get("request") == request)]
    require(len(found) == 1, f"Expected exactly one {event}")
    return found[0]


def normalized(row):
    return {key: value for key, value in row.items() if key not in ENVELOPE}


def census(rows, phase, request):
    return [normalized(row) for row in rows if row.get("request") == request and row.get("phase") == phase and row.get("event", "").startswith("native_checkpoint_")]


def counted(rows):
    return collections.Counter(content.canonical(row) for row in rows)


def validate_witness(data, expected_hash, decoded, expected, rows, pid, epoch):
    require(type(expected_hash) is str and re.fullmatch(r"[0-9a-f]{64}", expected_hash) is not None, "Caller-pinned witness SHA-256 required")
    require(0 < len(data) <= MAX_WITNESS and digest(data) == expected_hash, "Witness size/digest mismatch")
    witness = strict_json(data)
    require(type(witness) is dict and set(witness) == set(WITNESS_FIELDS), "Witness exact schema fields required")
    for key in WITNESS_FIELDS:
        if key in WITNESS_DIGESTS:
            require(type(witness[key]) is str and re.fullmatch(r"[0-9a-f]{64}", witness[key]) is not None, "Witness digest encoding")
        else:
            uint(witness[key], 32 if key in WITNESS_U32 else 64)
    canonical = json.dumps({key: witness[key] for key in WITNESS_FIELDS}, separators=(",", ":")).encode("ascii") + b"\n"
    require(data == canonical, "Witness differs from canonical transfer_witness_json bytes")
    require(witness["schema"] == 1 and witness["op"] == 1, "Expected completed source_fence witness")
    require(witness["preparation"] != "0" * 64 and witness["challenge"] != "0" * 64, "Missing transfer preparation/challenge")
    require(witness["approval"] == "0" * 64 and witness["approval_bytes"] == 0, "Source witness must precede host approval")
    for key in ("revision", "avatar_generation", "worker", "worker_generation", "session", "scene", "baseline", "pause_request", "input_epoch", "process_created"):
        require(witness[key] > 0, "Unassigned witness scope: " + key)
    require(witness["id"] == expected["request"] == witness["pause_request"] and witness["player"] == expected["canonical_player"] and witness["player"] in (1, 2), "Witness transfer/player/request mismatch")
    require(witness["process_id"] == pid and witness["scene"] == epoch, "Witness native process/scene mismatch")
    require(witness["artifact"] == decoded["artifact_sha256"] and witness["artifact_bytes"] == decoded["artifact_bytes"], "Witness artifact mismatch")
    for key in ("unique_game_id", "canonical_home"):
        require(witness[key] == decoded["root"][key], "Witness source identity mismatch")
    for prefix in ("source", "native"):
        require(witness[prefix + "_player"] == decoded["root"]["player_native_id"] and witness[prefix + "_avatar"] == decoded["root"]["avatar_native_id"] and witness[prefix + "_slot"] == expected["native_slot"], "Witness selected native root mismatch")
    require(witness["app_end"] > witness["app_begin"] and witness["ai_end"] == witness["ai_begin"] and witness["zero_deltas"] > 0 and witness["nonzero_deltas"] == 0 and witness["elapsed_ms"] >= 3000, "Witness lacks measured native quiescence")
    require(witness["pause_before"] == 0 and witness["pause_after"] == 1 and witness["remaining_imports"] in (0, 1), "Witness pause ownership/count")
    observed = only(rows, "native_quiescence_observed", witness["pause_request"])
    require(observed["epoch"] == epoch and observed["owns_pause"] is True and observed["gameplay_count"] == 1 and observed["pause_flags"] & 1 and observed["foreign_pause_calls"] == 0, "Native measured pause guard")
    for key in ("input_epoch", "app_begin", "ai_begin", "ai_end"):
        require(observed[key] == witness[key], "Native measured pause/witness mismatch")
    require(observed["app_end"] <= witness["app_end"] and 0 < observed["zero_native_deltas"] <= witness["zero_deltas"] and observed["nonzero_native_deltas"] == 0 and 3000 <= observed["elapsed_ms"] <= witness["elapsed_ms"], "Witness precedes measured pause window")
    receipt = [row for row in rows if row.get("event") == "network_native_transfer_receipt" and row.get("transfer") == witness["id"] and row.get("operation") == 1]
    require(len(receipt) == 1, "Expected exactly one completed source receipt trace")
    receipt = receipt[0]
    returned = only(rows, "native_player_graph_export_return", expected["request"])
    selected = only(rows, "native_player_graph_selection", expected["request"])
    require(observed["sequence"] < selected["sequence"] < returned["sequence"] < receipt["sequence"] and receipt["epoch"] == epoch and receipt["state_sha256"] == expected_hash and receipt["artifact_sha256"] == decoded["artifact_sha256"] and receipt["completed_native_work"] is True, "Native receipt correlation/order mismatch")
    invalid_events = {"native_quiescence_rejected", "native_quiescence_resume_return", "native_quiescence_reobserve", "native_quiescence_dispose_ownership_unresolved", "trace_stop"}
    require(not any(row.get("event") in invalid_events for row in rows if row["sequence"] > observed["sequence"]), "Paused producer released or invalidated its witness")
    return {"sha256": expected_hash, "receipt": witness}


def validate_provenance(decoded, trace_bytes, host_bytes, expected, witness_bytes=None):
    rows, hosts = jsonl(trace_bytes, native=True), jsonl(host_bytes)
    request = uint(expected["request"], 64)
    require(request > 0 and re.fullmatch(r"[0-9a-f]{32}", expected["generation"]) is not None, "Request/generation")
    require(uint(expected["canonical_player"], 64) in (1, 2) and expected["native_slot"] in (1, 2), "Selected identity")
    for key in ("unique_game_id", "canonical_home"):
        assigned(expected[key])
        require(decoded["root"][key] == expected[key], "Caller/root identity mismatch")
    require(decoded["artifact_sha256"] == expected["artifact_sha256"], "Caller artifact digest mismatch")
    mode = expected.get("producer_mode", "closed")
    require(mode in ("closed", "paused"), "Unknown producer mode")
    start = only(rows, "trace_start")
    require(start["sequence"] == 1, "Native trace must start at sequence one")
    if mode == "closed":
        require(witness_bytes is None, "Closed producer cannot claim a live pause witness")
        stop = only(rows, "trace_stop")
        require(stop is rows[-1] and stop["healthy"] is True and stop["detach_status"] == 0, "Unhealthy or incomplete closed producer")
    require(start["executable_sha256"] == EXE and start["sdk_commit"] == SDK, "Unknown original executable or SDK")
    pid, thread = assigned(start["pid"]), assigned(start["thread_id"])
    for n, row in enumerate(rows, 1):
        require(row.get("sequence") == n and row.get("pid") == pid and row.get("thread_id") == thread and row.get("foreign_callbacks") == 0 and row.get("evidence_class") == "NATIVE_PROBE", "Native trace identity/sequence/thread")
    supervisor = only(hosts, "worker_supervisor_started")
    require(supervisor["generation"] == expected["generation"], "Producer generation mismatch")
    if mode == "closed":
        for name in ("game_exited", "worker_exited"):
            event = only(hosts, name)
            require(event["exit_code"] == 0 and event["host_pid"] == supervisor["host_pid"], "Producer normal closure missing")
            if name == "game_exited":
                require(event["game_pid"] == pid, "Closed process differs from native producer")
    else:
        created = only(hosts, "created_suspended")
        require(created["game_pid"] == pid and created["host_pid"] == supervisor["host_pid"] and not any(row.get("event") in ("game_exited", "worker_exited") for row in hosts), "Paused producer process snapshot mismatch")
    selection = only(rows, "native_player_graph_selection", request)
    for key in ("canonical_player", "native_slot", "unique_game_id", "canonical_home"):
        require(type(selection[key]) is int and selection[key] == expected[key], "Selected export metadata mismatch")
    for key in ("player_native_id", "avatar_native_id"):
        require(selection[key] == decoded["root"][key], "Selected native root mismatch")
    entered = only(rows, "native_player_graph_export_enter", request)
    require(entered["home_planet"] == expected["canonical_home"], "Export canonical home mismatch")
    returned = only(rows, "native_player_graph_export_return", request)
    for key in ("initialized", "opened", "root_good", "classes_good", "closed", "source_identity_unchanged", "success"):
        require(returned[key] is True, "Native export failed: " + key)
    require(returned["bytes"] == decoded["artifact_bytes"] and returned["keys"] == 6 and returned["transfer_qualified"] is False, "Native export artifact envelope")
    require(selection["epoch"] == entered["epoch"] == returned["epoch"] and selection["sequence"] < returned["sequence"], "Export epoch/order mismatch")
    before, after = census(rows, "graph_before", request), census(rows, "graph_after", request)
    require(before and counted(before) == counted(after), "Whole source player census changed")
    players = [row for row in before if row["event"] == "native_checkpoint_player"]
    require(len(players) in (1, 2) and len({row["player_native_id"] for row in players}) == len(players), "Source player census identity")
    complete = only(before, "native_checkpoint_player_census")
    require(complete["complete"] is True and complete["players"] == len(players), "Incomplete source player census")
    chosen = [row for row in players if row["player_native_id"] == decoded["root"]["player_native_id"]]
    require(len(chosen) == 1, "Selected source player absent")
    chosen = chosen[0]
    require(chosen["owner"] == expected["native_slot"] and chosen["primary"] is (expected["native_slot"] == 1) and chosen["unique_game_id"] == expected["unique_game_id"], "Source slot/identity mismatch")
    require(bool(decoded["general_flags"] & 0x200) is (expected["native_slot"] == 1), "Source avatar local-primary flag mismatch")
    for bits, (_, key) in zip(decoded["progression_bits"], PROGRESS):
        # The native census prints binary32 values with nine significant digits.
        # Reconstruct binary32 before comparison instead of comparing JSON's
        # binary64 approximation to the expanded original float.
        require(type(chosen[key]) in (int, float) and struct.pack(">f", chosen[key]) == struct.pack(">I", bits), "Source progression mismatch")
    selected_rows = [row for row in before if row.get("player_native_id") == chosen["player_native_id"]]
    statuses = sorted((row["group"], row["instance"], row["status"]) for row in selected_rows if row["event"] == "native_checkpoint_item_status")
    unlocks = [row for row in selected_rows if row["event"] == "native_checkpoint_unlocked_item"]
    require([row["index"] for row in unlocks] == list(range(len(unlocks))), "Native unlock order incomplete")
    require(statuses == decoded["statuses"] and [(row["group"], row["instance"]) for row in unlocks] == decoded["unlocked"], "Source collection mismatch")
    require(chosen["item_statuses"] == len(statuses) and chosen["unlocked_items"] == len(unlocks), "Source collection count mismatch")
    stage_event = "native_primary_stage_export" if expected["native_slot"] == 1 else "native_selected_stage_export"
    stage = only(rows, stage_event, request)
    for key in ("player_native_id", "avatar_native_id", "unique_game_id"):
        require(stage[key] == decoded["root"][key], "Stage source identity mismatch")
    require(stage["serialization_version"] == VERSION and stage["epoch"] == selection["epoch"], "Stage version/epoch")
    require([stage[key] for key in STAGE_NAMES] == decoded["stage_words"], "Same-export stage fields mismatch")
    for key in ("source_stage_unchanged", "source_counter_idle", "original_write_return", "stream_good"):
        require(stage[key] is True, "Stage native guard failed")
    require(stage["source_world_pending"] is False and stage["transfer_qualified"] is False, "Pending stage progression")
    if expected["native_slot"] == 2:
        require(stage["native_slot"] == 2 and stage["destination_primary_stage_unchanged"] is True, "Secondary stage altered primary")
    scene = only(rows, "native_player_graph_scene_references", request)
    detach = only(rows, "native_player_graph_projection_detach", request)
    require(scene["unchanged"] is True and scene["source_herd_present"] is True, "Source scene references changed")
    require(detach["status"] == 0 and detach["neighbor_fields"] == detach["herd_fields"] == 1, "Scene projection binder count/detach")
    herd = only(rows, "native_player_graph_herd", request)
    assigned(herd["native_id"])
    require([herd["species_instance"], herd["species_type"], herd["species_group"]] == decoded["species"], "Source herd species mismatch")
    for key in ("archetype", "archetype_group", "personality"):
        uint(herd[key])
    require(type(herd["generation"]) is int and -(1 << 31) <= herd["generation"] < 1 << 31, "Herd generation range")
    witness = None
    if mode == "paused":
        require(witness_bytes is not None, "Paused producer requires caller-pinned source witness")
        witness = validate_witness(witness_bytes, expected.get("source_witness_sha256"), decoded, expected, rows, pid, selection["epoch"])
    return {"producer_mode": mode, "source_witness": witness, "selection": {key: selection[key] for key in ("canonical_player", "native_slot", "player_native_id", "avatar_native_id", "unique_game_id", "canonical_home")},
            "request": request, "generation": expected["generation"], "epoch": selection["epoch"], "source_census_rows": len(before), "herd": {key: herd[key] for key in ("archetype", "archetype_group", "generation", "personality")},
            "trace_sha256": digest(trace_bytes), "host_trace_sha256": digest(host_bytes), "game_pid": pid, "engine_thread": thread}


def approval_bytes(decoded, proof):
    root, herd = decoded["root"], proof["herd"]
    words = [decoded["artifact_bytes"], VERSION, root["player_native_id"], root["avatar_native_id"], root["unique_game_id"], root["canonical_home"], *decoded["species"], decoded["age"], decoded["animal_archetype"], herd["archetype"], herd["archetype_group"], herd["generation"] & 0xFFFFFFFF, herd["personality"], *decoded["progression_bits"], len(decoded["statuses"]), len(decoded["unlocked"]), decoded["unlock_points"], *decoded["stage_words"]]
    require(len(words) == 32, "Approval scalar layout")
    data = struct.pack("<II", 0x4630314D, 3) + decoded["artifact_sha256"].encode("ascii") + struct.pack("<32I", *words)
    for group, instance, status in decoded["statuses"]:
        data += struct.pack("<III", instance, group, status)
    for group, instance in decoded["unlocked"]:
        data += struct.pack("<II", instance, group)
    require(len(data) == 200 + len(decoded["statuses"]) * 12 + len(decoded["unlocked"]) * 8 <= MAX_APPROVAL, "Approval byte length")
    return data


def write_outputs(directory, report, approval):
    directory = content.safe_path(directory)
    require(not directory.exists(), "Fresh output directory required")
    directory.mkdir(parents=False)
    for name, data in (("player-import.expected", approval), ("approval.json", content.canonical(report) + b"\n")):
        path = directory / name
        with path.open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--host-trace", type=Path, required=True)
    parser.add_argument("--artifact-sha256", required=True)
    parser.add_argument("--generation", required=True)
    parser.add_argument("--request", type=int, required=True)
    parser.add_argument("--canonical-player", type=int, required=True)
    parser.add_argument("--native-slot", type=int, choices=(1, 2), required=True)
    parser.add_argument("--unique-game-id", type=int, required=True)
    parser.add_argument("--canonical-home", type=int, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--confirmed-closed", action="store_true", required=True, help="Acknowledge that the artifact and supplied trace snapshots are closed and immutable")
    parser.add_argument("--producer-mode", choices=("closed", "paused"), default="closed")
    parser.add_argument("--source-witness", type=Path)
    parser.add_argument("--source-witness-sha256", help="Exact witness digest pinned by the caller's authenticated source receipt")
    args = parser.parse_args(argv)
    try:
        require(re.fullmatch(r"[0-9a-f]{64}", args.artifact_sha256) is not None, "Lowercase SHA-256 required")
        raw = read_file(args.artifact, MAX_ARTIFACT)
        trace = read_file(args.trace, MAX_TRACE)
        host = read_file(args.host_trace, MAX_TRACE)
        require((args.source_witness is not None and args.source_witness_sha256 is not None) if args.producer_mode == "paused" else (args.source_witness is None and args.source_witness_sha256 is None), "Paused mode requires witness path and pinned SHA; closed mode accepts neither")
        witness = read_file(args.source_witness, MAX_WITNESS) if args.source_witness is not None else None
        decoded = decode_artifact(raw)
        expected = vars(args)
        proof = validate_provenance(decoded, trace, host, expected, witness)
        approval = approval_bytes(decoded, proof)
        require(read_file(args.artifact, MAX_ARTIFACT) == raw and read_file(args.trace, MAX_TRACE) == trace and read_file(args.host_trace, MAX_TRACE) == host, "Input changed during validation")
        require(witness is None or read_file(args.source_witness, MAX_WITNESS) == witness, "Witness changed during validation")
        report = {"schema_version": 1, "kind": "native-player-artifact-approval", "evidence_class": "HOST", "format_provenance_validated": True,
                  "native_transfer_qualified": False, "ownership_authorized": False, "artifact_sha256": decoded["artifact_sha256"], "artifact_bytes": len(raw),
                  "approval_sha256": digest(approval), "approval_bytes": len(approval), "m10f_schema": 3, "serialization_version": VERSION,
                  "selection": proof["selection"], "source": proof, "stage_persistent_words": decoded["stage_words"], "item_status_count": len(decoded["statuses"]), "unlocked_count": len(decoded["unlocked"]),
                  "limits": decoded["limits"] + ["Trace and witness provenance are checked structurally; receipt authentication, selected ownership, current coordinator phase, retained process identity, and continuous live pause remain caller gates.", "Closed-file approval does not qualify live transfer, destination original read, commit, or recovery."]}
        write_outputs(args.output_directory, report, approval)
        print(json.dumps({key: report[key] for key in ("format_provenance_validated", "native_transfer_qualified", "artifact_sha256", "approval_sha256", "approval_bytes", "selection")}, sort_keys=True))
        return 0
    except (ValueError, content.ContentError, OSError, KeyError, TypeError, OverflowError, struct.error, UnicodeError) as error:
        print(json.dumps({"format_provenance_validated": False, "error": str(error)}, sort_keys=True))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
