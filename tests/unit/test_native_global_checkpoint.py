"""HOST/FIXTURE checks for closed owner projection; no native launch."""
import copy
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from test_spore_content import archive
from test_native_global_observer import packed_planet

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("native_global_checkpoint", REPO / "tools/native/native-global-checkpoint.py")
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)


def selected():
    metadata = dict(build="11" * 32, executable=tool.artifact.EXE, content="33" * 32, native_ids=[100, 200, 0, 0])
    members = {"spore/" + name: archive([]) for name in tool.WORLD}
    members[tool.HAAGOS] = archive([])
    raw = tool.encode_checkpoint(metadata, members, {"fixture": True})
    return tool.parse_checkpoint(raw), raw


def native_save():
    owner = dict(worker=12, worker_generation=9, location=[1, 3, 4, 5], native_load_slot=1)
    rows = [dict(event="trace_start", sequence=1, pid=11),
            dict(event="network_native_owner_mapping", sequence=2, slot1_owner=2, slot2_owner=1),
            dict(event="native_world_observed", sequence=3, epoch=3, home_world=True,
                 native_home_star_id_candidate=3, native_home_planet_id_candidate=4, generated_terrain_instance=5),
            dict(event="native_checkpoint_player", sequence=8, request=7, phase="before_save", primary=True, unique_game_id=200),
            dict(event="native_checkpoint_barrier", sequence=9, request=7, epoch=3, schema=3),
            dict(event="native_save_enter", sequence=10, request=7, epoch=3, fixture="Haagos.spo"),
            dict(event="native_checkpoint_extension_written", sequence=12, request=7, epoch=3, schema=3, success=True),
            dict(event="native_save_return", sequence=13, request=7, epoch=3, requested_epoch=3, observed_epoch=3, native_success=True),
            dict(event="native_checkpoint_save_complete", sequence=14, request=7, epoch=3, success=True, diagnostic_only=False)]
    hosts = [dict(event="module", path="C:/p/SporeMP.Bridge.dll", sha256="11" * 32, game_pid=11),
             dict(event="network_config_validated", role="authority")]
    return rows, hosts, owner


def native_effect():
    before = [0x004EF041, 2, 0, 1] + [0] * 12
    after = before.copy()
    after[4:7] = [10, 20, 30]
    change = dict(sequence=5, before_valid=True, after_valid=True, before=before, after=after,
                  caller_rva=0x8719A7, args=[0, 0, 0, 0])
    return dict(samples={"assign_planet_spice": [change], "generate_star": []},
                marks={1: dict(complete=True, phase="before_save", sequence=11, planets={0x004EF041: after})})


class NativeGlobalCheckpointTests(unittest.TestCase):
    def test_schema2_roundtrip_preserves_native_identities_and_other_save(self):
        (metadata, members), raw = selected()
        self.assertEqual(metadata["native_ids"], [100, 200, 0, 0])
        candidate = dict(members)
        candidate[tool.PLANETS] = packed_planet()
        projected = tool.encode_checkpoint(metadata, candidate, {"owner": 12, "selected": tool.digest(raw)})
        decoded, data = tool.parse_checkpoint(projected)
        self.assertEqual(decoded["native_ids"], metadata["native_ids"])
        self.assertEqual(data[tool.SATIRIA], members[tool.SATIRIA])
        self.assertNotEqual(decoded["world"][1], metadata["world"][1])
        self.assertNotEqual(decoded["provenance"], metadata["provenance"])

    def test_unknown_corrupt_or_ambiguous_checkpoint_rejected(self):
        (_, _), raw = selected()
        for bad in (b"", raw[:50], raw[:-1], raw + b"x", raw[:4] + struct.pack("<I", 1) + raw[8:],
                    raw[:-1] + bytes([raw[-1] ^ 1])):
            with self.subTest(size=len(bad)), self.assertRaises(ValueError):
                tool.parse_checkpoint(bad)
        for name in ("spore/../x", "spore/CON.spo", "spore/a:b", "spore/foo.", "creations//x"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                tool.member_name(name)

    def test_original_save_scope_and_bridge_are_required(self):
        (metadata, _), _ = selected()
        rows, hosts, owner = native_save()
        result = tool.saved_owner(rows, hosts, metadata, 7, owner)
        self.assertEqual(result["resident_unique_game_id"], 200)
        for target, key, value in ((-1, "success", False), (-1, "diagnostic_only", True),
                                   (-2, "observed_epoch", 4), (5, "fixture", "Satiria.spo"),
                                   (3, "unique_game_id", 100), (4, "schema", 2)):
            changed = copy.deepcopy(rows)
            changed[target][key] = value
            with self.subTest(field=key), self.assertRaises(ValueError):
                tool.saved_owner(changed, hosts, metadata, 7, owner)
        changed = copy.deepcopy(hosts)
        changed[0]["sha256"] = "ff" * 32
        with self.assertRaises(ValueError):
            tool.saved_owner(rows, changed, metadata, 7, owner)
        with self.assertRaises(ValueError):
            tool.saved_owner(rows, hosts, metadata, 8, owner)

    def test_exact_original_consequence_must_reach_closed_archive(self):
        (_, previous), _ = selected()
        current = dict(previous, **{tool.PLANETS: packed_planet()})
        internal = native_effect()
        saved = dict(enter_sequence=10, return_sequence=13)
        result = tool.owner_effect(internal, previous, current, 0x004EF041, saved)
        self.assertEqual(result["owner_spice_changes"], 1)
        self.assertFalse(result["fresh_reader_qualified"])
        for change in (lambda v: v["samples"]["assign_planet_spice"].append(copy.deepcopy(v["samples"]["assign_planet_spice"][0])),
                       lambda v: v["samples"]["assign_planet_spice"][0].update(caller_rva=0),
                       lambda v: v["samples"]["assign_planet_spice"][0].update(args=[1, 0, 0, 0]),
                       lambda v: v["marks"][1].update(sequence=9),
                       lambda v: v["marks"][1].update(complete=False)):
            other = copy.deepcopy(internal)
            change(other)
            with self.assertRaises(ValueError):
                tool.owner_effect(other, previous, current, 0x004EF041, saved)
        with self.assertRaises(ValueError):
            tool.owner_effect(internal, current, current, 0x004EF041, saved)
        with self.assertRaises(ValueError):
            tool.owner_effect(internal, previous, dict(current, **{tool.PLANETS: packed_planet(spice=(0, 0, 0))}), 0x004EF041, saved)

    def test_repeated_save_requires_guarded_same_owner_and_preserves_selected_outcome(self):
        (_, previous), _ = selected()
        previous[tool.PLANETS] = packed_planet()
        internal = native_effect()
        sample = internal['samples']['assign_planet_spice'][0]
        sample['before'] = sample['after'].copy()
        sample.update(global_guard_active=True, global_guard_owner=True, grant_nonce=1)
        grant = dict(event='native_global_grant_installed', sequence=2, grant_nonce=1,
                     native_role=2, producer_mask=15, worker=12, global_owner_worker=12,
                     worker_generation=9, global_owner_generation=9, checkpoint_sha256='aa'*32,
                     global_guard_armed=True)
        internal['rows'] = [grant]
        owner = dict(worker=12, worker_generation=9)
        save = dict(enter_sequence=10, return_sequence=13)
        effect = tool.owner_effect(internal, previous, previous, 0x004EF041, save, owner, 'aa'*32)
        self.assertEqual(effect['kind'], 'native_planet_spice_preserved')
        self.assertEqual(effect['owner_spice_changes'], 0)
        for change in (lambda x: x['rows'][0].update(producer_mask=0),
                       lambda x: x['rows'][0].update(worker_generation=10),
                       lambda x: x['rows'][0].update(checkpoint_sha256='bb'*32),
                       lambda x: x['rows'].clear(),
                       lambda x: x['samples']['assign_planet_spice'][0].update(global_guard_active=False),
                       lambda x: x['samples']['assign_planet_spice'][0].update(global_guard_owner=False),
                       lambda x: x['samples']['assign_planet_spice'][0]['after'].__setitem__(4, 99)):
            other = copy.deepcopy(internal);change(other)
            with self.assertRaises(ValueError):
                tool.owner_effect(other, previous, previous, 0x004EF041, save, owner, 'aa'*32)
        with self.assertRaises(ValueError):
            tool.owner_effect(internal, previous, previous, 0x004EF041, save)

    def test_complete_profile_manifest_and_retained_save_guard(self):
        (metadata, previous), raw = selected()
        rows, hosts, owner = native_save()
        internal = native_effect()
        internal["rows"] = rows
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            profile = root / "local/owner"
            (profile / "Spore").mkdir(parents=True)
            (profile / "My Spore Creations").mkdir()
            current = dict(previous, **{tool.PLANETS: packed_planet()})
            for name, data in current.items():
                path = profile / "Spore" / name[len("spore/"):]
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            with patch.object(tool.content, "REPO", root):
                _, _, manifest = tool.read_owner_profile(profile)
                spec = dict(schema_version=1, selected={"path": "selected", "sha256": tool.digest(raw)}, owner=owner,
                            run={"host_trace": {}}, owner_profile={"path": str(profile), "manifest_sha256": tool.digest(tool.content.canonical(manifest))},
                            save_request=7, planet_id=0x004EF041)
                def pinned(descriptor, limit):
                    return raw if descriptor is spec["selected"] else b"\n".join(tool.content.canonical(r) for r in hosts) + b"\n"
                with patch.object(tool.observer, "pinned_file", side_effect=pinned), \
                        patch.object(tool.observer, "analyze_run", return_value=({"pid": 11}, internal)):
                    result, proof = tool.build_candidate(spec)
                    self.assertEqual(tool.parse_checkpoint(result)[1][tool.SATIRIA], previous[tool.SATIRIA])
                    self.assertFalse(proof["durable_publication"])
                    path = profile / "Spore/Games/Game0/Satiria.spo"
                    path.write_bytes(archive([("00000001!00000001.00000001", b"new", 3, 0)]))
                    _, _, manifest = tool.read_owner_profile(profile)
                    spec["owner_profile"]["manifest_sha256"] = tool.digest(tool.content.canonical(manifest))
                    with self.assertRaisesRegex(ValueError, "Non-owner"):
                        tool.build_candidate(spec)


if __name__ == "__main__":
    unittest.main()
