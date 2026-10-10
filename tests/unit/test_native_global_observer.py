"""HOST/FIXTURE rejection tests; all trace and archive values are synthetic."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

from test_spore_content import archive
from test_native_player_artifact import attrs

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("native_global_observer", REPO / "tools/native/native-global-observer.py")
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)


def fixture():
    rows = [dict(event="trace_start", executable_sha256=tool.artifact.EXE, sdk_commit=tool.artifact.SDK),
            dict(event="native_global_observer_attached", hooks=8, pass_through=True)]
    def mark(number, phase, attached=True):
        for producer, rva in tool.PRODUCERS.items():
            rows.append(dict(event="native_global_observer_summary", mark=number, request=0, observed_epoch=3,
                             producer=producer, rva=rva, entries=0, returns=0, creature_entries=0, other_mode_entries=0,
                             app_ticks=number * 100, elapsed_ms=number * 5000, foreign_thread_entries=0,
                             dropped_samples=0, suppressed_calls=0, global_ownership_qualified=False, phase=phase,
                             attached=attached))
        if not attached:
            return
        rows.append(dict(event="native_global_ticker_census", mark=number, request=0, observed_epoch=3,
                         complete=True, deferred_removals_excluded=True, active=0, pending=0, universe_count=0))
        rows.append(dict(event="native_global_record_census", mark=number, request=0, observed_epoch=3,
                         scope=tool.STATE_SCOPE, whole_galaxy=False, manager_seen=True, complete_within_scope=True,
                         stars=0, cached_planets=0))
        rows.append(dict(event="native_global_universe_state", mark=number, singleton_readable=True, present=False))
    mark(1, "before_load")
    mark(2, "scene_ready")
    mark(3, "end")
    mark(3, "end", False)
    rows += [dict(event="native_global_observer_detached", detached=True), dict(event="trace_stop", healthy=True, detach_status=0)]
    rows = [dict(sequence=i, pid=11, thread_id=12, foreign_callbacks=0, evidence_class="NATIVE_PROBE", **r)
            for i, r in enumerate(rows, 1)]
    hosts = [dict(event="worker_supervisor_started", generation="a" * 32, host_pid=10),
             dict(event="game_exited", exit_code=0, host_pid=10, game_pid=11), dict(event="worker_exited", exit_code=0, host_pid=10)]
    return rows, hosts


def packed_planet(prefix=bytes(16), duplicate=False, spice=(10, 20, 30)):
    body = prefix + attrs({0x03D58B9C: struct.pack(">III", spice[2], spice[1], spice[0]),
                           0x03D58B9B: struct.pack(">III", 60, 50, 40),
                           0x0417A8E9: struct.pack(">3f", 0, 0, 1), 0x0417A902: struct.pack(">f", 300)})
    if duplicate:
        body += attrs({0x03D58B9C: bytes(12)})
    inner = archive([("00000001!00000001.00000001", body, len(body), 0)])
    value = bytes.fromhex("00250003") + inner
    return archive([("00000001!004ef041.05220cb8", value, len(value), 0)])


def effect_fixture(root):
    def pinned(name, data):
        path = root / name
        path.write_bytes(data)
        return dict(path=str(path), sha256=tool.digest(data))
    planets = packed_planet()
    spec = dict(kind="native_planet_spice_initialization", owner="owner", reader="reader", planet_id=0x004EF041,
                owner_input_planets=pinned("source.pkp", archive()), owner_planets=pinned("owner.pkp", planets),
                reader_input_planets=pinned("reader.pkp", planets), owner_stars=pinned("owner.db", archive()),
                reader_input_stars=pinned("reader.db", archive()), owner_input_location=pinned("source.spo", archive()),
                owner_temp_planets=pinned("owner.pkt", archive([])), reader_input_temp_planets=pinned("reader.pkt", archive([])),
                owner_retained_location=pinned("retained.spo", archive()), reader_input_location=pinned("reader.spo", archive()))
    reports = {"owner": dict(pid=1, generation="a" * 32), "reader": dict(pid=2, generation="b" * 32)}
    before = [0x004EF041, 2, 0, 1] + [0] * 12
    after = before.copy()
    after[4:7] = [10, 20, 30]
    sample = dict(sequence=20, before_valid=True, after_valid=True, before=before, after=after,
                  caller_rva=0x8719A7, args=[0, 0, 0, 0])
    def mark(phase, sequence):
        return dict(complete=True, phase=phase, sequence=sequence, planets={0x004EF041: after.copy()},
                    stars={0x004EF041: [0x004EF041, 5] + [0] * 14})
    owner = dict(samples={"assign_planet_spice": [sample], "generate_star": []}, marks={3: mark("before_save", 40)},
                 rows=[dict(event="native_save_return", native_success=True, sequence=50)])
    reader = dict(samples={"assign_planet_spice": [], "generate_star": []}, marks={4: mark("scene_ready", 50)},
                  rows=[dict(event="native_load_complete", sequence=51)])
    return spec, reports, dict(owner=owner, reader=reader)


class GlobalObserverTests(unittest.TestCase):
    def evaluate(self, rows, hosts):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def pinned(name, data):
                raw = b"".join(json.dumps(row).encode() + b"\n" for row in data)
                path = root / name
                path.write_bytes(raw)
                return dict(path=str(path), sha256=tool.digest(raw))
            return tool.analyze_run(dict(label="fixture", generation="a" * 32,
                                         trace=pinned("trace.jsonl", rows), host_trace=pinned("host.jsonl", hosts)))

    def test_closed_window_does_not_grant_ownership(self):
        report, _ = self.evaluate(*fixture())
        self.assertTrue(report["observation_validated"])
        self.assertEqual(report["mutation_candidates"], [])

    def test_missing_mutation_detail_or_dropped_ring_is_rejected(self):
        for dropped in (0, 1):
            with self.subTest(dropped=dropped):
                rows, hosts = fixture()
                for row in rows:
                    if row.get("producer") == "assign_planet_spice" and row["mark"] > 1:
                        row.update(entries=1, returns=1, creature_entries=1, dropped_samples=dropped)
                with self.assertRaises(ValueError):
                    self.evaluate(rows, hosts)

    def test_mismatched_original_returns_or_foreign_thread_is_rejected(self):
        for field, value in (("returns", 1), ("foreign_thread_entries", 1)):
            with self.subTest(field=field):
                rows, hosts = fixture()
                next(r for r in rows if r.get("producer") == "universe_update")[field] = value
                with self.assertRaises(ValueError):
                    self.evaluate(rows, hosts)

    def test_partial_ticker_or_missing_producer_is_rejected(self):
        rows, hosts = fixture()
        next(r for r in rows if r["event"] == "native_global_ticker_census")["complete"] = False
        with self.assertRaises(ValueError):
            self.evaluate(rows, hosts)
        rows, hosts = fixture()
        rows.pop(2)
        for i, row in enumerate(rows, 1):
            row["sequence"] = i
        with self.assertRaises(ValueError):
            self.evaluate(rows, hosts)

    def test_live_or_failed_process_cannot_be_approved(self):
        for mutate in (lambda h: h.pop(), lambda h: h[1].update(exit_code=5), lambda h: h[1].update(game_pid=99)):
            rows, hosts = fixture()
            mutate(hosts)
            with self.assertRaises(ValueError):
                self.evaluate(rows, hosts)

    def test_nested_native_resource_byte_order(self):
        record = tool.planet_record(packed_planet(), 0x004EF041)
        self.assertEqual(record["spice"], [10, 20, 30])
        self.assertEqual(record["terrain"], [40, 50, 60])
        self.assertEqual(record["rotation_period"], 0x43960000)

    def test_populated_graph_ambiguous_attribute_and_wrong_id_rejected(self):
        for raw, planet in ((packed_planet(bytes(15) + b"\x01"), 0x004EF041),
                            (packed_planet(duplicate=True), 0x004EF041), (packed_planet(), 0x00914013)):
            with self.subTest(planet=planet, size=len(raw)), self.assertRaises(ValueError):
                tool.planet_record(raw, planet)

    def test_effect_rejects_rewritten_global_file_before_native_claims(self):
        with tempfile.TemporaryDirectory() as directory:
            spec, reports, internals = effect_fixture(Path(directory))
            changed = packed_planet() + b"changed"
            Path(spec["reader_input_planets"]["path"]).write_bytes(changed)
            spec["reader_input_planets"]["sha256"] = tool.digest(changed)
            with self.assertRaisesRegex(ValueError, "byte-identical"):
                tool.correlate_effect(spec, reports, internals)

    def test_effect_joins_native_change_save_archive_and_fresh_reader(self):
        with tempfile.TemporaryDirectory() as directory:
            spec, reports, internals = effect_fixture(Path(directory))
            result = tool.correlate_effect(spec, reports, internals)
            self.assertTrue(result["correlation_validated"])
            self.assertFalse(result["owner_input_record_present"])
            self.assertFalse(result["durable_publication_qualified"])
            self.assertEqual(result["selected_owner_spice_changes"], 1)
            self.assertEqual(result["record"]["spice"], [10, 20, 30])

    def test_initialization_chain_rejects_unsaved_wrong_caller_and_repeated_effect(self):
        for failure in ("unsaved", "callsite", "force", "repeated", "cache", "reader_regenerated"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                spec, reports, internals = effect_fixture(Path(directory))
                owner, reader = internals["owner"], internals["reader"]
                if failure == "unsaved":
                    owner["rows"].clear()
                elif failure == "callsite":
                    owner["samples"]["assign_planet_spice"][0]["caller_rva"] = 0x7A8ADF
                elif failure == "force":
                    owner["samples"]["assign_planet_spice"][0]["args"][0] = 1
                elif failure == "repeated":
                    owner["samples"]["assign_planet_spice"] *= 2
                elif failure == "cache":
                    owner["marks"][3]["planets"][0x004EF041][4] = 0
                else:
                    reader["samples"]["assign_planet_spice"] = copy.deepcopy(owner["samples"]["assign_planet_spice"])
                with self.assertRaises(ValueError):
                    tool.correlate_effect(spec, reports, internals)

    def test_closed_archive_must_contain_observed_value_and_preserve_other_location(self):
        for failure in ("spice_zero", "source_assigned", "other_location_changed"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                spec, reports, internals = effect_fixture(Path(directory))
                keys, value = (("owner_planets", "reader_input_planets"), packed_planet(spice=(0, 0, 0)))
                if failure == "source_assigned":
                    keys, value = ("owner_input_planets",), packed_planet()
                elif failure == "other_location_changed":
                    keys, value = ("reader_input_location",), archive() + b"changed"
                for key in keys:
                    Path(spec[key]["path"]).write_bytes(value)
                    spec[key]["sha256"] = tool.digest(value)
                with self.assertRaises(ValueError):
                    tool.correlate_effect(spec, reports, internals)


if __name__ == "__main__":
    unittest.main()
