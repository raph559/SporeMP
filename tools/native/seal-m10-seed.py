"""Seal the reviewed two-location original Creature world as an M10 seed.

The fixed source and native traces are private. This command reads closed
original-game files; it never launches SPORE or publishes a coordinator head.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import sys

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools/launcher"))
import launcher_service as service
import multiplayer_session as multiplayer

SOURCE = REPO / "local/m10-native-2026-09-22/second-location-01/closed-haagos"
NATIVE = REPO / "local/m10-native-2026-09-24/native-primary-scene-10"
RUNS = (
    ("01", "b22675eea3ab4287a3c47ea6f206e97f", 13516, 1),
    ("02", "ba5c9a5f7de34f0ca6d143abe7090b08", 19424, 2),
)
MAGIC = 0x39424D53
MAX_BYTES = 80 * 1024 * 1024
EXE = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37"
SDK = "cbf9206b9a823f0911cd9be0217104a49d72380b"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def native_proof(worker: str, generation: str, pid: int, owner: int) -> tuple[int, str, tuple[int, int, int]]:
    path = Path(f"C:/ProgramData/SporeMP/M04/{worker}/runs/{generation}/actors-{pid}.jsonl")
    data = service.diag.no_reparse(path).read_bytes()
    if not data.endswith(b"\n") or len(data) > 66 * 1024 * 1024:
        raise ValueError("Native trace is incomplete or oversized")
    rows = [json.loads(line) for line in data.splitlines()]
    if not rows or any(row.get("sequence") != n or row.get("pid") != pid or
                       row.get("thread_id") != rows[0].get("thread_id") or
                       row.get("foreign_callbacks") != 0 for n, row in enumerate(rows, 1)):
        raise ValueError("Native trace provenance/ordering differs")
    first, last = rows[0], rows[-1]
    if first.get("event") != "trace_start" or first.get("executable_sha256") != EXE or first.get("sdk_commit") != SDK or \
       last.get("event") != "trace_stop" or last.get("healthy") is not True or last.get("detach_status") != 0:
        raise ValueError("Native original-game trace did not close healthy")
    def one(event: str):
        found = [row for row in rows if row.get("event") == event]
        if len(found) != 1:
            raise ValueError("Native witness is missing or duplicated: " + event)
        return found[0]
    loaded = one("native_load_complete")
    world = one("native_world_observed")
    surface = one("native_surface_candidate")
    census = one("native_primary_census")
    binding = one("native_primary_owner_probe")
    scene = one("native_primary_scene_probe")
    if loaded.get("identities_restored") is not False or census.get("owner") != owner or \
       census.get("native_players") != 1 or census.get("avatar_flags") != 1 or \
       census.get("primary_in_census") is not True or census.get("avatar_in_census") is not True or \
       binding.get("owner") != owner or binding.get("primary_player") is not True or \
       binding.get("second_player") is not False or scene.get("owner") != owner or \
       scene.get("result") != "accepted" or scene.get("owned") != 1 or \
       scene.get("foreign_owned") != 0 or not scene.get("entities") or \
       not loaded["sequence"] < census["sequence"] < binding["sequence"] < scene["sequence"] < last["sequence"]:
        raise ValueError("Original primary or scene census failed")
    game_id = census.get("primary_unique_game_id")
    if type(game_id) is not int or not 0 < game_id < 0xFFFFFFFF:
        raise ValueError("Invalid original player game ID")
    planet = world.get("native_home_planet_id_candidate")
    star = world.get("native_home_star_id_candidate")
    area = world.get("generated_terrain_instance")
    if any(type(value) is not int or value <= 0 for value in (planet, star, area)) or \
       star != planet & 0x00ffffff or world.get("native_planet_index_candidate") != planet >> 24 or \
       world.get("home_world") is not True or surface.get("matches_home_generated_terrain") is not True or \
       (surface.get("surface_instance"), surface.get("surface_type"), surface.get("surface_group")) != \
       (area, world.get("generated_terrain_type"), world.get("generated_terrain_group")) or \
       not loaded["sequence"] < world["sequence"] < surface["sequence"] < binding["sequence"]:
        raise ValueError("Original home planet and active Creature surface do not agree")
    return game_id, sha(data), (star, planet, area)


def seal(output: Path) -> dict:
    output = service.diag.no_reparse(output.absolute())
    if not output.is_relative_to(REPO / "local") or output.exists():
        raise ValueError("A new ignored local output directory is required")
    if service.diag.game_running():
        raise ValueError("Close original SPORE before sealing a closed source")
    prepared = json.loads((NATIVE / "prepared.json").read_text(encoding="utf-8"))
    restored = json.loads((NATIVE / "restored.json").read_text(encoding="utf-8"))
    manifest_path = service.diag.no_reparse(SOURCE / "closed-manifest.json")
    manifest = manifest_path.read_bytes()
    rows = json.loads(manifest)
    if len(rows) != 30 or prepared.get("input_manifest_sha256") != sha(manifest) or \
       prepared.get("verified") is not True or restored.get("protected_personal_files") != "PASS":
        raise ValueError("Closed source preparation/restoration is not verified")
    native = [native_proof(*run) for run in RUNS]
    ids = [entry[0] for entry in native]
    locations = [entry[2] for entry in native]
    if ids[0] == ids[1] or locations[0] == locations[1]:
        raise ValueError("Original primary player or location IDs are not distinct")
    members: dict[str, bytes] = {}
    for row in rows:
        path = service.diag.no_reparse(Path(row["backup"]))
        relative = path.relative_to(SOURCE)
        if len(relative.parts) < 2 or relative.parts[0] not in ("Spore", "My Spore Creations") or \
           not path.is_file() or path.stat().st_nlink != 1:
            raise ValueError("Closed native member is unsafe")
        name = ("spore" if relative.parts[0] == "Spore" else "creations") + "/" + "/".join(relative.parts[1:])
        if len(name) > 240 or name.casefold() in (key.casefold() for key in members):
            raise ValueError("Closed member path is duplicate or oversized")
        content = path.read_bytes()
        if sha(content) != row["sha256"].lower():
            raise ValueError("Closed original-game member changed")
        members[name] = content
    world = multiplayer.world_identity_fields(SOURCE / "Spore", service.diag.no_reparse)
    for field, relative in multiplayer.WORLD_FILES:
        if "spore/" + relative not in members or sha(members["spore/" + relative]) != world[field]:
            raise ValueError("Closed world identity is incomplete")
    if "spore/Games/Game0/Haagos.spo" not in members:
        raise ValueError("The original second save is missing")
    bridge = (REPO / "build/win32/Release/SporeMP.Bridge.dll").read_bytes()
    candidate = (REPO / "config/compatibility.candidate.json").read_bytes()
    build_hash, content_hash = sha(bridge), sha(candidate)
    proof = {"source_manifest_sha256": sha(manifest), "native_trace_sha256": [entry[1] for entry in native],
             "primary_game_ids": ids, "home_locations": locations, "build_sha256": build_hash,
             "secondary_save_sha256": sha(members["spore/Games/Game0/Haagos.spo"])}
    provenance = sha(json.dumps(proof, sort_keys=True, separators=(",", ":")).encode())
    data = bytearray(struct.pack("<II", MAGIC, 2))
    for value in (build_hash, EXE, content_hash):
        data.extend(bytes.fromhex(value))
    for field, _ in multiplayer.WORLD_FILES:
        data.extend(bytes.fromhex(world[field]))
    data.extend(bytes.fromhex(provenance))
    data.extend(struct.pack("<QI4II", 0, 0, ids[0], ids[1], 0, 0, len(members)))
    for name, content in sorted(members.items()):
        encoded = name.encode("ascii")
        data.extend(struct.pack("<I", len(encoded))); data.extend(encoded)
        data.extend(bytes.fromhex(sha(content)))
        data.extend(struct.pack("<I", len(content))); data.extend(content)
        if len(data) > MAX_BYTES:
            raise ValueError("Native seed exceeds the bounded artifact size")
    output.mkdir()
    with (output / "checkpoint.smp").open("xb") as stream:
        stream.write(data); stream.flush(); os.fsync(stream.fileno())
    plan = ("schema=1\nlocation_count=2\n"
            f"location.1=1,{locations[0][0]},{locations[0][1]},{locations[0][2]},11,5,0\n"
            f"location.2=1,{locations[1][0]},{locations[1][1]},{locations[1][2]},12,9,1\n"
            "player.1=1\nplayer.2=2\nglobal.economy=1\nglobal.diplomacy=1\n"
            "global.galaxy_ai=1\nglobal.disasters=1\n")
    (output / "plan.conf").write_bytes(plan.encode("ascii"))
    receipt = {"schema": 2, "artifact_sha256": sha(data), "artifact_bytes": len(data),
               "members": len(members), "provenance_sha256": provenance,
               "build_sha256": build_hash, "content_sha256": content_hash,
               "world": world, "primary_game_ids": ids, "home_locations": locations,
               "location_plan_sha256": sha(plan.encode("ascii")),
               "secondary_save_sha256": proof["secondary_save_sha256"],
               "native_acceptance": "SEED_ONLY", "network_acceptance": "NOT_RUN"}
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return receipt


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: seal-m10-seed.py NEW_LOCAL_DIRECTORY")
    print(json.dumps(seal(Path(sys.argv[1])), separators=(",", ":")))
