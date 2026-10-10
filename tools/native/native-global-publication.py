"""Approve a closed paired native owner Save for guarded coordinator publication.

This HOST validator emits an exact M10G scalar envelope. It never publishes a
checkpoint or retires a player artifact. The coordinator independently checks
current ownership, retained process death, selected revision and player heads.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import struct

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("global_publication_checkpoint", HERE / "native-global-checkpoint.py")
checkpoint = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checkpoint)
observer, artifact, content = checkpoint.observer, checkpoint.artifact, checkpoint.content
require, digest, uint = artifact.require, artifact.digest, artifact.uint
ZERO = "0" * 64
HEADER = "01897c18!01897c18.01897c18"
STAGE_INDICES = (16, 17, 18, 25, 26, 28, 29, 30, 31, 32)


def sha(value):
    require(type(value) is str and re.fullmatch(r"[0-9a-f]{64}", value) and value != ZERO, "Nonzero SHA256 required")
    return value


def header_tables(data):
    # Full saved-game headers contain many nested subsystem tables. Keep the
    # same strict table decoder as the portable artifact, with a separate cap.
    found, position = [], 0
    while True:
        at = data.find(bytes.fromhex("01a80d26"), position)
        if at < 0:
            return found
        position = at + 4
        try:
            fields, end = artifact.attributes(data, at)
        except (artifact.ArtifactError, struct.error):
            continue
        found.append((at, end, fields))
        require(len(found) <= 2048, "Saved-game header table budget")


def class_frames(data, expected):
    require(0 < expected <= 16384, "Class object count")
    result, tokens, position = [], set(), 0
    for _ in range(expected):
        require(len(data) - position >= 12, "Truncated original class frame")
        size, checksum, token = struct.unpack_from(">III", data, position)
        require(12 <= size <= len(data) - position and token not in tokens, "Original class frame bounds/token")
        artifact.assigned(token)
        tokens.add(token)
        result.append(data[position:position + size])
        position += size
    require(position == len(data), "Trailing original class frames")
    return result


def collection(frame):
    tables = frame["tables"]
    statuses, items = artifact.field(tables, 0x03A3B3D4), artifact.field(tables, 0x06676971)
    ns, ni = artifact.be32(statuses), artifact.be32(items)
    require(ns <= 1024 and ni <= 1024 and len(statuses) == 4 + ns * 9 and len(items) == 4 + ni * 8, "Collection dimensions")
    status = [dict(zip(("group", "instance", "status"), struct.unpack_from(">IIB", statuses, 4 + i * 9))) for i in range(ns)]
    unlocked = [dict(zip(("group", "instance"), struct.unpack_from(">II", items, 4 + i * 8))) for i in range(ni)]
    require(len({(r["group"], r["instance"]) for r in status}) == ns, "Duplicate collection status")
    return {"statuses": status, "unlocked_items": unlocked, "unlock_points": artifact.number(tables, 0x03A3B30D),
            "collection_body_sha256": digest(frame["body"])}


def saved_players(raw):
    records = {e["key"]: content.record_bytes(raw, e) for e in content.parse_archive(raw)}
    index = records[artifact.INDEX]
    require(artifact.be32(index) == 1 and 3 <= artifact.be32(index, 4) <= 128, "Native class index")
    count, total = artifact.be32(index, 4), artifact.be32(index, 8)
    require(len(index) == 12 + count * 8 and total <= 16384, "Native class index size")
    counts = dict(struct.unpack_from(">II", index, 12 + i * 8) for i in range(count))
    require(len(counts) == count and sum(counts.values()) == total and counts.get(artifact.PLAYER) == 2
            and counts.get(artifact.ITEMS) == 2, "Exactly two original players and collections required")
    streams = {kind: class_frames(records[f"00000000!0179d304.{kind:08x}"], n) for kind, n in counts.items()}
    collections = {f["token"]: f for f in (artifact.object_frame(raw, artifact.ITEMS) for raw in streams[artifact.ITEMS])}
    players = {}
    for raw_player in streams[artifact.PLAYER]:
        frame = artifact.object_frame(raw_player, artifact.PLAYER)
        tables = frame["tables"]
        native_id = artifact.number(tables, 0x03CDD54B)
        token, kind = struct.unpack(">II", artifact.field(tables, 0x03A3A4B4, 8))
        require(native_id not in players and kind == artifact.ITEMS and token in collections, "Original player collection reference")
        players[native_id] = {"native_id": native_id, "unique_game_id": artifact.number(tables, 0x05EE017B),
                              "token": frame["token"], "collection_token": token, "player_body_sha256": digest(frame["body"]),
                              "progression_bits": {name: artifact.number(tables, key) for key, name in artifact.PROGRESS},
                              **collection(collections[token])}
    require(len({p["unique_game_id"] for p in players.values()}) == 2
            and len({p["collection_token"] for p in players.values()}) == 2, "Independent original player identities/collections")
    tables = header_tables(records[HEADER])
    extensions = [fields[0x01001000] for _, _, fields in tables if 0x01001000 in fields
                  and bytes.fromhex("39435053") in fields[0x01001000]]
    require(len(extensions) == 1, "One original schema3 strategy extension required")
    extension = extensions[0]
    require(len(extension) == 16548 and struct.unpack_from(">III", extension) == (1, 0x53504D39, 16536), "Native extension frame")
    words = list(struct.unpack_from(">4132I", extension, 12))
    require(words[:3] == [0x39435053, 3, 4132] and words[3] in players and not any(words[36:]), "Native extension version/root/pending grants")
    visitor = players[words[3]]
    require(struct.unpack_from(">II", extension, 12 + 4132 * 4) == (visitor["token"], artifact.PLAYER), "Extension roots the saved visitor")
    require(words[27] == 0, "Pending secondary world evolution is outside publication qualification")
    require(words[16] <= 3 and words[18] <= 1000 and words[19] <= 15 and words[20] <= 3 and words[25] <= 1,
            "Saved native award checkpoint ranges")
    for index in (17, 22, 23, 24, 26, 33, 34, 35):
        artifact.finite(words[index])
    require(artifact.finite(words[17]) > 0 and artifact.finite(words[26]) >= 0 and not words[32] & 0xFF000000,
            "Saved native award checkpoint values")
    primary_stage = artifact.decode_stage(artifact.field(tables, 0x061AFC2E, 188))
    avatars = {}
    wanted = {words[4], words[10]}
    for raw_avatar in streams[artifact.AVATAR]:
        tables = artifact.blocks(raw_avatar[12:])
        native_id = artifact.number(tables, 0x03CDD54B)
        if native_id in wanted:
            require(native_id not in avatars, "Duplicate saved avatar native ID")
            frame = artifact.object_frame(raw_avatar, artifact.AVATAR)
            avatars[native_id] = {"body_sha256": digest(frame["body"]), "tables": tables}
    require(set(avatars) == wanted and len(wanted) == 2, "Both extension avatars must exist exactly once")
    return {"players": players, "avatars": avatars, "extension_words": words,
            "extension_sha256": digest(extension), "primary_stage_words": primary_stage,
            "secondary_stage_words": [words[i] for i in STAGE_INDICES], "archive_sha256": digest(raw)}


def native_save_inclusion(rows, saved, save, native_ids):
    request, epoch = save["request"], save["epoch"]
    before = [r for r in rows if r.get("request") == request and r.get("phase") == "before_save" and r["event"].startswith("native_checkpoint_")]
    after = [r for r in rows if r.get("request") == request and r.get("phase") == "after_save" and r["event"].startswith("native_checkpoint_")]
    require(artifact.counted([artifact.normalized(r) for r in before]) == artifact.counted([artifact.normalized(r) for r in after]), "Player state changed across original Save")
    census = artifact.only(before, "native_checkpoint_player_census")
    require(census.get("players") == 2 and census.get("complete") is True, "Complete paired native Save census")
    barrier = artifact.only(rows, "native_checkpoint_barrier", request)
    require(all(r["epoch"] == epoch and r["sequence"] < barrier["sequence"] for r in before), "Native before-Save census ordering")
    require(all(r["epoch"] == epoch and save["return_sequence"] < r["sequence"] < save["complete_sequence"] for r in after), "Native after-Save census ordering")
    by_slot = {}
    for state in (r for r in before if r["event"] == "native_checkpoint_player"):
        slot = state["owner"]
        require(slot in (1, 2) and slot not in by_slot and state["primary"] == (slot == 1), "Native Save player slots")
        player = saved["players"].get(state["player_native_id"])
        require(player and player["unique_game_id"] == state["unique_game_id"] == native_ids[2 - slot], "Saved native/canonical player identity")
        require(all(struct.unpack(">I", struct.pack(">f", state[k]))[0] == v for k, v in player["progression_bits"].items()), "Saved original progression mismatch")
        own = [r for r in before if r.get("player_native_id") == player["native_id"]]
        statuses = [{k: r[k] for k in ("group", "instance", "status")} for r in own if r["event"] == "native_checkpoint_item_status"]
        unlocked_rows = [r for r in own if r["event"] == "native_checkpoint_unlocked_item"]
        require([r["index"] for r in unlocked_rows] == list(range(len(unlocked_rows))), "Native unlock order")
        unlocked = [{k: r[k] for k in ("group", "instance")} for r in unlocked_rows]
        require(artifact.counted(statuses) == artifact.counted(player["statuses"]) and unlocked == player["unlocked_items"]
                and state["item_statuses"] == len(statuses) and state["unlocked_items"] == len(unlocked), "Saved original collection mismatch")
        actor = [r for r in rows if r["event"] == "checkpoint_actor" and r.get("request") == request and r.get("owner") == slot]
        require(len(actor) == 1 and actor[0]["epoch"] == epoch and actor[0]["sequence"] < before[0]["sequence"], "Same-Save actor fingerprint")
        actor = actor[0]
        fields = ("native_id", "herd_native_id", "species_instance", "species_type", "species_group", "archetype")
        n = 4 + (slot - 1) * 6
        require([actor[k] for k in fields] == saved["extension_words"][n:n + 6], "Original extension actor fingerprint mismatch")
        avatar = saved["avatars"][actor["native_id"]]
        require(struct.unpack(">III", artifact.field(avatar["tables"], 0x03F64A78, 12)) ==
                (actor["species_group"], actor["species_type"], actor["species_instance"]), "Saved avatar species mismatch")
        by_slot[slot] = {"canonical_player": 3 - slot, "player_native_id": player["native_id"],
                         "avatar_native_id": actor["native_id"], "herd_native_id": actor["herd_native_id"],
                         "unique_game_id": player["unique_game_id"], "player_body_sha256": player["player_body_sha256"],
                         "collection_body_sha256": player["collection_body_sha256"], "avatar_body_sha256": avatar["body_sha256"],
                         "stage_words": saved["primary_stage_words" if slot == 1 else "secondary_stage_words"]}
    require(set(by_slot) == {1, 2} and saved["extension_words"][3] == by_slot[2]["player_native_id"], "Saved visitor/native slot mismatch")
    native_ids = [by_slot[s][key] for s in (1, 2) for key in ("player_native_id", "avatar_native_id")]
    require(len(set(native_ids)) == 4 and all(artifact.assigned(value) for value in native_ids), "Saved owned noun identity collision")
    extension = artifact.only(rows, "native_checkpoint_extension_written", request)
    require(extension["player_native_id"] == by_slot[2]["player_native_id"], "Extension trace/root mismatch")
    rewards = [r for r in rows if r["event"] == "native_owned_reward_state" and census["sequence"] < r["sequence"] < barrier["sequence"]]
    require(len(rewards) == 1 and rewards[0]["queue_empty"] is True and rewards[0]["brain"] == saved["secondary_stage_words"][0]
            and struct.unpack(">I", struct.pack(">f", rewards[0]["dna"]))[0] == saved["secondary_stage_words"][4], "Saved secondary award boundary")
    return by_slot


def witness(row, expected_hash):
    require(type(row) is dict and set(row) == set(artifact.WITNESS_FIELDS), "Exact completed witness fields")
    for key in artifact.WITNESS_FIELDS:
        if key in artifact.WITNESS_DIGESTS:
            require(type(row[key]) is str and re.fullmatch(r"[0-9a-f]{64}", row[key]), "Witness digest")
        else:
            uint(row[key], 32 if key in artifact.WITNESS_U32 else 64)
    raw = json.dumps({key: row[key] for key in artifact.WITNESS_FIELDS}, separators=(",", ":")).encode("ascii") + b"\n"
    require(digest(raw) == sha(expected_hash) and row["schema"] == 1, "Completed native witness hash")
    return row


def activation_receipt(spec, receipt):
    final = witness(receipt["last_receipt"], receipt["last_receipt_sha256"])
    if final["op"] == 5:
        require("activation_receipt" not in spec, "Redundant activation receipt")
        return final, receipt["last_receipt_sha256"]
    require(final["op"] == 7 and "activation_receipt" in spec,
            "Secondary cleanup completion requires a separately pinned destination-active status receipt")
    earlier = artifact.strict_json(observer.pinned_file(spec["activation_receipt"], 65536))
    require(earlier.get("schema") == 1 and earlier.get("ok") is True and earlier.get("native_phase") == 8
            and earlier.get("native_completion") is False and earlier.get("ownership_committed") is True
            and earlier.get("id") == receipt["id"] and earlier.get("player") == receipt["player"]
            and 0 < earlier["durable_revision"] < receipt["durable_revision"]
            and earlier.get("avatar_generation") == receipt.get("avatar_generation")
            and earlier.get("destination_witness_sha256") == receipt["destination_witness_sha256"]
            and earlier.get("destination_witness") == receipt["destination_witness"], "Earlier activation status does not match terminal head")
    active = witness(earlier["last_receipt"], earlier["last_receipt_sha256"])
    require(active["op"] == 5, "Earlier status lacks completed destination activation")
    for key in ("id", "player", "preparation", "artifact", "approval", "artifact_bytes", "approval_bytes", "unique_game_id", "canonical_home"):
        require(active[key] == final[key], "Cleanup/activation identity differs: " + key)
    require(final["native_player"] == final["native_avatar"] == final["remaining_imports"] == 0
            and final["source_slot"] == 2 and final["pause_before"] == 1 and final["pause_after"] == 0
            and final["app_end"] > final["app_begin"] and final["ai_end"] > final["ai_begin"]
            and final["zero_deltas"] > 0 and final["elapsed_ms"] >= 3000
            and final["process_id"] != active["process_id"], "Terminal secondary cleanup witness incomplete")
    return active, earlier["last_receipt_sha256"]


def superseded_head(spec, rows, slots, owner, save, expected_revision):
    fields = {"player", "receipt", "artifact", "approval"}
    require(type(spec) is dict and set(spec) in (fields, fields | {"activation_receipt"}) and spec["player"] in (1, 2), "Player head specification")
    raw = observer.pinned_file(spec["artifact"], artifact.MAX_ARTIFACT)
    approval = observer.pinned_file(spec["approval"], artifact.MAX_APPROVAL)
    decoded = artifact.decode_artifact(raw)
    receipt = artifact.strict_json(observer.pinned_file(spec["receipt"], 65536))
    require(receipt.get("schema") == 1 and receipt.get("ok") is True and receipt.get("native_phase") == 15
            and receipt.get("native_completion") is True and receipt.get("ownership_committed") is True
            and receipt.get("player") == spec["player"] and 0 < receipt["durable_revision"] <= expected_revision, "Completed current player head receipt")
    restored = witness(receipt["destination_witness"], receipt["destination_witness_sha256"])
    active, active_hash = activation_receipt(spec, receipt)
    slot = 3 - spec["player"]
    saved = slots[slot]
    require(restored["op"] == 2 and active["op"] == 5, "Native destination restore/activation witnesses required")
    for key in ("id", "player", "worker", "worker_generation", "session", "scene", "baseline", "preparation", "artifact", "approval",
                "artifact_bytes", "approval_bytes", "unique_game_id", "canonical_home", "native_player", "native_avatar", "native_slot", "process_id", "process_created"):
        require(restored[key] == active[key], "Restore/activation witness identity differs: " + key)
    require(active["id"] == receipt["id"] and active["player"] == spec["player"] and active["native_slot"] == slot
            and active["worker"] == owner["worker"] and active["worker_generation"] == owner["worker_generation"]
            and active["scene"] == save["epoch"] and active["process_id"] == rows[0]["pid"] and active["process_created"] > 0
            and active["unique_game_id"] == decoded["root"]["unique_game_id"] == saved["unique_game_id"]
            and active["canonical_home"] == decoded["root"]["canonical_home"]
            and active["native_player"] == saved["player_native_id"] and active["native_avatar"] == saved["avatar_native_id"], "Saved player does not match completed transfer identity")
    require(active["artifact"] == digest(raw) and active["approval"] == digest(approval)
            and active["artifact_bytes"] == len(raw) and active["approval_bytes"] == len(approval), "Retained player artifact/approval mismatch")
    # Regenerate the M10F scalar contract directly from the immutable artifact.
    require(len(approval) >= 200 and struct.unpack_from("<II", approval) == (0x4630314D, 3)
            and approval[8:72] == digest(raw).encode("ascii"), "Native player approval header/artifact")
    approval_words = struct.unpack_from("<32I", approval, 72)
    herd = dict(zip(("archetype", "archetype_group", "generation", "personality"), approval_words[11:15]))
    require(approval == artifact.approval_bytes(decoded, {"herd": herd}), "M10F contract differs from retained native graph")
    require(restored["pause_before"] == 0 and restored["pause_after"] == 1 and restored["app_end"] > restored["app_begin"]
            and restored["ai_end"] == restored["ai_begin"] and restored["zero_deltas"] > 0 and not restored["nonzero_deltas"]
            and restored["elapsed_ms"] >= 3000, "Completed restore pause witness")
    require(active["pause_before"] == 1 and active["pause_after"] == 0 and active["app_end"] > active["app_begin"]
            and active["ai_end"] > active["ai_begin"] and active["elapsed_ms"] >= 3000, "Completed activation witness")
    observed = []
    for op, h in ((2, receipt["destination_witness_sha256"]), (5, active_hash)):
        matching = [r for r in rows if r["event"] == "network_native_transfer_receipt" and r.get("transfer") == active["id"] and r.get("operation") == op]
        require(len(matching) == 1 and matching[0]["epoch"] == save["epoch"] and matching[0]["state_sha256"] == h
                and matching[0]["artifact_sha256"] == digest(raw) and matching[0]["completed_native_work"] is True, "Native completed receipt missing from owner trace")
        observed.append(matching[0])
    imports = [r for r in rows if r["event"] in ("native_player_import_return", "native_player_primary_restore_return", "native_player_secondary_restore_return") and r.get("request") == active["id"]]
    require(len(imports) == 1 and imports[0]["player_native_id"] == saved["player_native_id"]
            and imports[0]["avatar_native_id"] == saved["avatar_native_id"] and imports[0]["unique_game_id"] == saved["unique_game_id"]
            and imports[0]["epoch"] == save["epoch"] and imports[0]["sequence"] < observed[0]["sequence"] < observed[1]["sequence"] < save["enter_sequence"], "Original import/activation/Save order")
    if imports[0]["event"] == "native_player_import_return":
        require(imports[0].get("destination_primary_unchanged") is True and imports[0].get("global_color_unchanged") is True
                and imports[0].get("stage_restored") is True and imports[0].get("destination_stage_unchanged") is True,
                "Original imported-player preservation failed")
    else:
        require(imports[0].get("stage_applied") is True and imports[0].get("global_color_unchanged") is True
                and imports[0].get("created_nouns") == 0 and imports[0].get("noun_ids_preserved") == 2,
                "Original existing-player restore preservation failed")
        binding_guard = "primary_binding_unchanged" if slot == 1 else "bindings_unchanged"
        require(imports[0].get(binding_guard) is True, "Original restored-player binding changed")
    forbidden = {"native_player_import_return", "native_player_primary_restore_return", "native_player_secondary_restore_return", "native_player_retired", "native_player_transfer_retired"}
    require(not any(r["event"] in forbidden and r.get("player_native_id") == saved["player_native_id"]
                    and imports[0]["sequence"] < r["sequence"] < save["complete_sequence"] for r in rows), "Intervening player replacement/retirement")
    return {"player": spec["player"], "transfer_id": active["id"], "artifact_sha256": digest(raw), "approval_sha256": digest(approval),
            "restore_witness_sha256": receipt["destination_witness_sha256"], "activation_witness_sha256": active_hash,
            "process_created": active["process_created"], "import_sequence": imports[0]["sequence"], "activation_sequence": observed[1]["sequence"]}


def encode_approval(proof, proof_hash):
    require(0 < uint(proof["expected_revision"], 64) < 0xFFFFFFFFFFFFFFFF
            and uint(proof["event_id"], 64) == proof["expected_revision"] + 1, "Publication event must identify its durable revision")
    raw = bytearray(struct.pack("<II", 0x4730314D, 1))
    for value in (proof["universe"], proof["selected_artifact_sha256"], proof["candidate_artifact_sha256"], proof["candidate_provenance"], proof_hash):
        raw.extend(bytes.fromhex(sha(value)))
    owner, save = proof["owner"], proof["save"]
    raw.extend(struct.pack("<10Q", proof["expected_revision"], proof["selected_checkpoint_revision"], owner["worker"], owner["worker_generation"],
                           *owner["location"], save["request"], save["epoch"]))
    raw.extend(struct.pack("<IIQQ", proof["process_id"], 6, proof["process_created"], proof["event_id"]))
    slots = proof["saved_slots"]
    raw.extend(struct.pack("<8I", slots["1"]["player_native_id"], slots["1"]["avatar_native_id"], slots["2"]["player_native_id"], slots["2"]["avatar_native_id"],
                           slots["2"]["unique_game_id"], slots["1"]["unique_game_id"], 2, 1))
    for player in (1, 2):
        head = next((h for h in proof["superseded_heads"] if h["player"] == player), None)
        raw.extend(struct.pack("<Q", head["transfer_id"] if head else 0))
        raw.extend(bytes.fromhex(head["artifact_sha256"] if head else ZERO))
        raw.extend(bytes.fromhex(head["approval_sha256"] if head else ZERO))
    raw.extend(struct.pack("<4I", proof["effect"]["record"]["planet_id"], *proof["effect"]["record"]["spice"]))
    require(len(raw) == 464, "M10G envelope size")
    return bytes(raw)


def approve(spec):
    require(type(spec) is dict and set(spec) == {"schema_version", "candidate_spec", "candidate", "universe", "expected_revision", "selected_checkpoint_revision", "event_id", "heads"}
            and spec["schema_version"] == 1, "Publication specification fields")
    candidate_spec_raw = observer.pinned_file(spec["candidate_spec"], 65536)
    candidate_spec = artifact.strict_json(candidate_spec_raw)
    candidate, candidate_proof = checkpoint.build_candidate(candidate_spec)
    require(candidate == observer.pinned_file(spec["candidate"], checkpoint.MAX_BYTES), "Rebuilt native owner candidate differs")
    require(digest(candidate_spec_raw) == spec["candidate_spec"]["sha256"], "Candidate specification changed")
    previous, _ = checkpoint.parse_checkpoint(observer.pinned_file(candidate_spec["selected"], checkpoint.MAX_BYTES))
    header, members = checkpoint.parse_checkpoint(candidate)
    rows = artifact.jsonl(observer.pinned_file(candidate_spec["run"]["trace"], artifact.MAX_TRACE), native=True)
    saved = saved_players(members[checkpoint.HAAGOS])
    slots = native_save_inclusion(rows, saved, candidate_proof["save"], previous["native_ids"])
    require(uint(spec["expected_revision"], 64) and uint(spec["selected_checkpoint_revision"], 64)
            and spec["selected_checkpoint_revision"] <= spec["expected_revision"] < 0xFFFFFFFFFFFFFFFF
            and uint(spec["event_id"], 64) == spec["expected_revision"] + 1, "Publication revisions/event ID")
    require(type(spec["heads"]) is list and 1 <= len(spec["heads"]) <= 2, "At least one completed native imported player head required")
    heads = [superseded_head(h, rows, slots, candidate_spec["owner"], candidate_proof["save"], spec["expected_revision"]) for h in spec["heads"]]
    require(len({h["player"] for h in heads}) == len(heads) and len({h["process_created"] for h in heads}) == 1, "Duplicate player heads or mixed original processes")
    proof = {"schema_version": 1, "kind": "closed-native-owner-publication-approval", "universe": sha(spec["universe"]),
             "expected_revision": spec["expected_revision"], "selected_checkpoint_revision": spec["selected_checkpoint_revision"], "event_id": spec["event_id"],
             "selected_artifact_sha256": previous["artifact_sha256"], "candidate_artifact_sha256": digest(candidate), "candidate_provenance": header["provenance"],
             "owner": candidate_spec["owner"], "save": candidate_proof["save"], "process_id": rows[0]["pid"], "process_created": heads[0]["process_created"],
             "saved_slots": {str(k): v for k, v in slots.items()}, "superseded_heads": heads,
             "native_archive_sha256": saved["archive_sha256"], "extension_sha256": saved["extension_sha256"],
             "effect": candidate_proof["effect"], "resident_canonical_mask": 6,
             "canonical_owner_authorized": False, "process_death_coordinator_verified": False, "durable_publication": False}
    proof_raw = content.canonical(proof) + b"\n"
    approval = encode_approval(proof, digest(proof_raw))
    require(artifact.read_file(spec["candidate_spec"]["path"], 65536) == candidate_spec_raw, "Candidate specification changed during approval")
    for path, limit, expected in observer.INPUTS:
        require(digest(artifact.read_file(path, limit)) == expected, "Pinned publication input changed")
    return proof_raw, approval


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--confirmed-closed", action="store_true", required=True)
    args = parser.parse_args(argv)
    try:
        import sys
        sys.path.insert(0, str(checkpoint.REPO / "tools/launcher"))
        import launcher_service as service
        require(not service.diag.game_running(), "Close all original SPORE processes before approval")
        spec_raw = artifact.read_file(args.spec, 65536)
        proof, approval = approve(artifact.strict_json(spec_raw))
        require(not service.diag.game_running() and artifact.read_file(args.spec, 65536) == spec_raw, "Original process/specification changed")
        output = content.private_output(args.output)
        require(not output.exists() and output.parent.is_dir(), "New private output directory required")
        output.mkdir()
        for name, data in (("global-checkpoint-proof.json", proof), ("global-checkpoint.expected", approval)):
            with (output / name).open("xb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
        print(json.dumps({"approved": True, "approval_bytes": len(approval), "approval_sha256": digest(approval), "proof_sha256": digest(proof), "durable_publication": False}, sort_keys=True))
        return 0
    except (ValueError, OSError, KeyError, TypeError, OverflowError, struct.error, UnicodeError) as error:
        print(json.dumps({"approved": False, "error": str(error)}, sort_keys=True))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
