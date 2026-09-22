"""HOST/FIXTURE inspection-driver admission and trace-correlation tests; no game."""
from copy import deepcopy
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("m08_content_probe", REPO / "tools/native/m08-content-probe.py")
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)
KEY = "40626200!12345678.2b978c46"
VALUES = (0x12345678, probe.CRT, 0x40626200)


def archive(record_type=probe.CRT):
    data = bytearray(96)
    data[:4] = b"DBPF"
    for offset, value in ((4, 3), (36, 1), (44, 36), (60, 3), (64, 100)):
        struct.pack_into("<I", data, offset, value)
    data.extend(b"CRT!")  # Synthetic opaque payload; never presented as native content.
    # Index header (8) and record (28).
    data[100:] = struct.pack("<II", 4, 0) + struct.pack("<IIIIIIHBB", record_type, VALUES[2], VALUES[0],
                                                     96, 0x80000004, 4, 0, 1, 0)
    return bytes(data)


def fixture():
    common = dict(schema_version=1, evidence_class="NATIVE_PROBE", pid=321, thread_id=654,
                  foreign_callbacks=0, qpc_frequency=1000, epoch=7)
    events = [dict(event="trace_start", executable_sha256=probe.EXE, sdk_commit=probe.SDK,
                   bridge_version=probe.VERSION),
              dict(event="content_bindings_checked", code_prefixes=2, native_execution_qualified=False, readiness=False),
              dict(event="content_inspection_begin", request=44, instance=VALUES[0], type=VALUES[1], group=VALUES[2],
                   may_generate_native_cache=True),
              dict(event="content_rigblock", request=44, ordinal=0, group=1, instance=2, index=0, parent=-1,
                   symmetric=-1, flags=0, block_type=3, capability_start=0, capability_count=1),
              dict(event="content_capability", request=44, ordinal=0, tag_hex="01020304", native_level=5),
              dict(event="content_inspection_complete", request=44, instance=VALUES[0], type=probe.DERIVED,
                   group=VALUES[2], model_type=1, rigblocks=1, capabilities=1, owned_release_completed=True,
                   gameplay_validation=False, readiness=False)]
    return [{**common, **event, "sequence": i, "qpc": i} for i, event in enumerate(events, 1)]


def encode(rows):
    return b"".join(json.dumps(row).encode() + b"\n" for row in rows)


def save_fixture():
    common = {k: v for k, v in fixture()[0].items() if k not in
              ("event", "sequence", "qpc", "executable_sha256", "sdk_commit", "bridge_version")}
    events = [fixture()[0],
              dict(event="editor_observer_registered", producer_prefixes=2, request_message=0xB03BC30C,
                   result_message=0x030C11C7, observation_only=True, native_execution_qualified=False, readiness=False),
              dict(event="editor_request_observed", message=0xB03BC30C, routing_id=0,
                   instance=0, type=0, group=0, observation_only=True, editor_landing_verified=False, readiness=False),
              dict(event="editor_result_observed", message=0x030C11C7, routing_id=0, model_type=1,
                   instance=VALUES[0], type=VALUES[1], group=VALUES[2], outcome="accepted", play=0,
                   observation_only=True, native_commit_validation=False, readiness=False),
              dict(event="editor_observer_disposed", listeners_removed=True, foreign_thread_callbacks=0, readiness=False),
              dict(event="trace_stop", detach_status=0, healthy=True)]
    return [{**common, **event, "sequence": i, "qpc": i} for i, event in enumerate(events, 1)]


class TraceTests(unittest.TestCase):
    def test_complete_correlated_native_return_retains_no_acceptance(self):
        raw, rows = probe.trace_rows(encode(fixture()), 321, probe.VERSION)
        probe.adapter_ready(rows)
        result = probe.inspection(rows, 44, 7, VALUES, 2)
        self.assertEqual(result["rigblocks"][0]["instance"], 2)
        self.assertIs(result["complete"]["readiness"], False)
        self.assertIs(result["complete"]["gameplay_validation"], False)

    def test_trace_provenance_sequence_pid_thread_clock_are_strict(self):
        for index, field, value in ((0, "executable_sha256", "0" * 64), (0, "sdk_commit", "unknown"),
                (0, "bridge_version", "0.0.45"), (0, "evidence_class", "HOST_FIXTURE"), (4, "schema_version", True),
                (4, "sequence", 7), (4, "pid", 999), (4, "pid", True), (4, "thread_id", 655),
                (4, "foreign_callbacks", True), (4, "foreign_callbacks", 1), (4, "qpc_frequency", 1001),
                (4, "qpc", 0), (4, "epoch", True)):
            rows = fixture(); rows[index][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                probe.trace_rows(encode(rows), 321, probe.VERSION)
        with patch.object(probe, "MAX_TRACE", 10), self.assertRaises(ValueError):
            probe.trace_rows(encode(fixture()), 321, probe.VERSION)

    def test_partial_final_line_never_supplies_a_completion(self):
        prefix = encode(fixture()[:-1])
        raw, rows = probe.trace_rows(prefix + b'{"event":"content_inspection_complete"', 321, probe.VERSION)
        self.assertEqual(raw, prefix)
        self.assertIsNone(probe.inspection(rows, 44, 7, VALUES, 2))
        for suffix in (b"{}\n", b"[]\n", b'{"x":1,"x":2}\n', b'{"x":NaN}\n', b'{"x":1e999}\n'):
            with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                probe.trace_rows(prefix + suffix, 321, probe.VERSION)

    def test_missing_forged_duplicate_and_reordered_content_rejected(self):
        cases = []
        for remove in (2, 3, 4):
            rows = fixture(); del rows[remove]; cases.append(rows)
        rows = fixture(); rows[3], rows[4] = rows[4], rows[3]; cases.append(rows)
        rows = fixture(); rows.insert(3, deepcopy(rows[2])); cases.append(rows)
        for index, field, value in ((2, "instance", 9), (2, "may_generate_native_cache", False),
                (3, "ordinal", True), (3, "capability_count", 2), (3, "capability_start", -1),
                (3, "instance", 1 << 32), (4, "tag_hex", "../../xx"), (4, "native_level", 128),
                (4, "native_level", float("inf")), (5, "type", probe.CRT), (5, "epoch", 8),
                (5, "rigblocks", 513), (5, "capabilities", 4097), (5, "owned_release_completed", False),
                (5, "gameplay_validation", True), (5, "readiness", True)):
            rows = fixture(); rows[index][field] = value; cases.append(rows)
        for rows in cases:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                probe.inspection(rows, 44, 7, VALUES, 2)
        with self.assertRaisesRegex(ValueError, "Stale"):
            probe.inspection(fixture(), 44, 7, VALUES, 3)

    def test_failed_and_unqualified_adapter_cannot_pass(self):
        rows = fixture()
        rows[-1] = {**rows[-1], "event": "content_inspection_failed", "reason": "native_load_failed"}
        with self.assertRaisesRegex(ValueError, "native_load_failed"):
            probe.inspection(rows, 44, 7, VALUES, 2)
        for changed in ({"code_prefixes": True}, {"native_execution_qualified": True}, {"readiness": True},
                        {"event": "content_binding_rejected"}):
            rows = fixture(); rows[1].update(changed)
            with self.assertRaises(ValueError): probe.adapter_ready(rows)


class DriverTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="m08-probe-fixture-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.local = self.root / "local"; self.local.mkdir()
        self.source = self.local / "closed.package"; self.source.write_bytes(archive())
        self.run = self.local / "worker-run"; self.run.mkdir()
        self.trace = self.run / "actors-321.jsonl"; self.trace.write_bytes(encode(fixture()[:2]))
        self.worker = dict(schema_version=1, worker_id="02", generation="a" * 32, supervisor_pid=100,
                           run=str(self.run), payload=str(self.local / "payload"),
                           supervisor_sha256=probe.content.digest(b"HOST FIXTURE"))
        for relative in ("SporeMP.NativeHost.exe", "mLibs/SporeMP.Bridge.dll", "mLibs/SporeModAPI.dll",
                         "ModAPI.DLLInjector.dll"):
            target = Path(self.worker["payload"]) / relative
            target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(b"HOST FIXTURE")
        control = self.root / "build/win32/Release/SporeMP.WorkerControl.exe"
        control.parent.mkdir(parents=True); control.write_bytes(b"HOST FIXTURE CONTROL")
        self.args = SimpleNamespace(worker="02", source_archive=self.source, key=KEY,
                                    output=self.local / "output", version=probe.VERSION, save_trace=None)
        self.repo_patch = patch.object(probe, "REPO", self.root); self.repo_patch.start()
        self.addCleanup(self.repo_patch.stop)
        self.calls = []

    def subprocess(self, argv, **kwargs):
        self.assertTrue((self.args.output / "report.json").exists())
        self.calls.append(argv)
        op = argv[3]
        if op == "inspect_creation":
            self.assertEqual(argv[1:3], ["a" * 32, "100"])
            self.assertEqual(argv[4:], ["7", *(str(value) for value in VALUES)])
            self.trace.write_bytes(encode(fixture()))
        reply = dict(schema_version=1, op=op, result="accepted", epoch=7, phase=1, request=44)
        return SimpleNamespace(returncode=0, stdout=json.dumps(reply), stderr="")

    def execute(self, responder=None):
        with patch.object(probe.workers, "current", return_value=self.worker) as current, \
                patch.object(probe, "live_identity", return_value=(321, {"generation": "a" * 32})), \
                patch.object(probe.subprocess, "run", side_effect=responder or self.subprocess):
            report = probe.run(self.args)
            self.current_calls = current.call_count
        return report

    def test_driver_freezes_identity_reserves_output_and_preserves_raw_trace(self):
        report = self.execute()
        self.assertTrue(report["probe_checks_passed"])
        self.assertTrue(report["native_return_observed"])
        self.assertEqual(self.current_calls, 1)
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(report["source"]["record"]["sha256"], probe.content.digest(b"CRT!"))
        self.assertEqual((self.args.output / "after-trace.jsonl").read_bytes(), encode(fixture()))
        self.assertEqual(report["native_byte_equivalence"], "NOT_VERIFIED")
        self.assertFalse(report["readiness"])
        self.assertFalse(report["transfer_approved"])
        self.assertEqual(json.loads((self.args.output / "report.json").read_text())["observation"], report["observation"])

    def test_saved_bem_requires_matching_closed_native_save_witness(self):
        self.source.write_bytes(archive(probe.BEM))
        with self.assertRaisesRegex(ValueError, "absent"):
            probe.source_record(self.source, KEY)
        witness = self.local / "closed-save.jsonl"
        witness.write_bytes(encode(save_fixture()))
        self.args.save_trace = witness
        report = self.execute()
        self.assertTrue(report["probe_checks_passed"])
        source = report["source"]
        self.assertEqual(source["logical_key"], KEY)
        self.assertEqual(source["record"]["key"], "40626200!12345678.1a99b06b")
        self.assertEqual(source["representation"], "bem_with_closed_native_editor_result")
        self.assertEqual(source["save_witness"]["sha256"], probe.content.digest(witness.read_bytes()))
        self.assertFalse(source["save_witness"]["native_commit_validation"])
        self.assertEqual(source["native_byte_equivalence"], "NOT_VERIFIED")

    def test_cancelled_unclosed_unhealthy_or_wrong_key_witness_cannot_send_request(self):
        self.source.write_bytes(archive(probe.BEM))
        witness = self.local / "closed-save.jsonl"
        self.args.save_trace = witness
        cases = []
        for index, field, value in ((0, "sdk_commit", "unknown"), (0, "bridge_version", "0.0.45"),
                (1, "producer_prefixes", True), (1, "request_message", 1), (1, "observation_only", False),
                (2, "routing_id", 1), (2, "foreign_callbacks", 1), (3, "outcome", "cancelled"),
                (3, "instance", 99), (3, "native_commit_validation", True), (3, "play", True),
                (4, "listeners_removed", False), (4, "foreign_thread_callbacks", 1), (5, "healthy", False),
                (5, "detach_status", 1), (5, "detach_status", False)):
            rows = save_fixture(); rows[index][field] = value; cases.append(encode(rows))
        cases.extend((encode(save_fixture()[:-1]), encode(save_fixture()) + b'{"unfinished":',
                      encode(save_fixture()[0:2] + save_fixture()[3:])))
        for index, raw in enumerate(cases):
            with self.subTest(index=index):
                self.args.output = self.local / f"bad-witness-{index}"
                witness.write_bytes(raw)
                report = self.execute()
                self.assertFalse(report["probe_checks_passed"])
                self.assertEqual(report["commands"], [])
                self.assertEqual(self.current_calls, 0)
        self.assertEqual(self.calls, [])

    def test_duplicate_overlapping_or_unpaired_editor_results_are_ambiguous(self):
        witness = self.local / "closed-save.jsonl"
        cases = []
        for index, extra in ((3, [save_fixture()[2]]), (4, [save_fixture()[3]]),
                             (4, save_fixture()[2:4]), (4, [dict(save_fixture()[3], event="editor_observation_rejected")])):
            rows = save_fixture(); rows[index:index] = extra
            cases.append([{**row, "sequence": i, "qpc": i} for i, row in enumerate(rows, 1)])
        for rows in cases:
            witness.write_bytes(encode(rows))
            with self.assertRaises(ValueError):
                probe.closed_save_witness(witness, VALUES, probe.VERSION)

    def test_changed_save_witness_after_request_cannot_pass(self):
        self.source.write_bytes(archive(probe.BEM))
        self.args.save_trace = self.local / "closed-save.jsonl"
        self.args.save_trace.write_bytes(encode(save_fixture()))
        def changed(argv, **kwargs):
            result = self.subprocess(argv, **kwargs)
            if argv[3] == "inspect_creation":
                rows = save_fixture(); rows[3]["outcome"] = "cancelled"
                self.args.save_trace.write_bytes(encode(rows))
            return result
        report = self.execute(changed)
        self.assertFalse(report["probe_checks_passed"])
        self.assertFalse(report["native_return_observed"])
        self.assertEqual(len(self.calls), 2)

    def test_invalid_archive_key_or_path_cannot_send_any_request(self):
        for name, change in (("corrupt", lambda: self.source.write_bytes(b"DBPFbad")),
                ("missing", lambda: setattr(self.args, "key", "40626200!12345679.2b978c46")),
                ("unsafe", lambda: setattr(self.args, "key", "../../evil.dll")),
                ("wrongtype", lambda: setattr(self.args, "key", "40626200!12345678.00e6bce5")),
                ("outside", lambda: setattr(self.args, "source_archive", self.root / "outside.package"))):
            self.source.write_bytes(archive()); self.args.key = KEY; self.args.source_archive = self.source
            self.args.output = self.local / name; change()
            report = self.execute()
            self.assertFalse(report["probe_checks_passed"])
            self.assertEqual(report["commands"], [])
            self.assertEqual(self.current_calls, 0)
        self.assertEqual(self.calls, [])

    def test_bad_native_provenance_refused_before_inspection(self):
        rows = fixture()[:2]; rows[0]["bridge_version"] = "0.0.45"
        self.trace.write_bytes(encode(rows))
        report = self.execute()
        self.assertFalse(report["probe_checks_passed"])
        self.assertEqual(self.calls, [])
        self.assertEqual((self.args.output / "before-trace.jsonl").read_bytes(), encode(rows))

    def test_changed_supervisor_payload_refused_before_any_request(self):
        (Path(self.worker["payload"]) / "SporeMP.NativeHost.exe").write_bytes(b"changed fixture")
        report = self.execute()
        self.assertFalse(report["probe_checks_passed"])
        self.assertEqual(self.calls, [])
        self.assertIn("payload hash mismatch", report["error"])

    def test_existing_or_public_output_refused_before_any_request(self):
        for output in (self.local, self.root / "public"):
            self.args.output = output
            with patch.object(probe.subprocess, "run") as command, self.assertRaises((ValueError, FileExistsError)):
                probe.run(self.args)
            command.assert_not_called()

    def test_unknown_request_outcome_is_preserved_without_retry(self):
        def timeout(argv, **kwargs):
            if argv[3] == "status": return self.subprocess(argv, **kwargs)
            self.calls.append(argv)
            raise subprocess.TimeoutExpired(argv, 13)
        report = self.execute(timeout)
        self.assertFalse(report["probe_checks_passed"])
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(report["commands"][-1]["outcome"], "UNKNOWN_NO_AUTOMATIC_RETRY")
        self.assertIn("no automatic retry", report["error"])
        self.assertTrue((self.args.output / "after-trace.jsonl").is_file())

    def test_loading_stopping_and_noninteger_phase_cannot_send_inspection(self):
        for phase in (0, 2, 4, 5, True, 1.0):
            self.args.output = self.local / ("phase-" + str(phase))
            self.calls.clear()
            def unavailable(argv, **kwargs):
                result = self.subprocess(argv, **kwargs)
                reply = json.loads(result.stdout); reply["phase"] = phase
                result.stdout = json.dumps(reply)
                return result
            report = self.execute(unavailable)
            self.assertFalse(report["probe_checks_passed"])
            self.assertEqual([call[3] for call in self.calls], ["status"])

    def test_reply_without_trace_and_rewritten_prefix_cannot_pass(self):
        for mode in ("missing", "rewrite"):
            self.args.output = self.local / mode
            self.trace.write_bytes(encode(fixture()[:2]))
            def incomplete(argv, **kwargs):
                result = self.subprocess(argv, **kwargs)
                if argv[3] == "inspect_creation":
                    rows = fixture()[:2] if mode == "missing" else fixture()
                    if mode == "rewrite": rows[0]["qpc"] = 0
                    self.trace.write_bytes(encode(rows))
                return result
            with patch.object(probe.time, "monotonic", side_effect=[0, 4]):
                report = self.execute(incomplete)
            self.assertFalse(report["probe_checks_passed"])
            self.assertFalse(report["native_return_observed"])
            self.assertTrue((self.args.output / "after-trace.jsonl").exists())

    def test_forged_reply_does_not_match_native_return(self):
        for field, value in (("epoch", 8), ("request", 0), ("request", 45), ("op", "save"), ("result", "failed")):
            self.args.output = self.local / (field + "-" + str(value))
            self.trace.write_bytes(encode(fixture()[:2]))
            def forged(argv, **kwargs):
                result = self.subprocess(argv, **kwargs)
                if argv[3] == "inspect_creation":
                    reply = json.loads(result.stdout); reply[field] = value
                    result.stdout = json.dumps(reply)
                return result
            with patch.object(probe.time, "monotonic", side_effect=[0, 4]):
                report = self.execute(forged)
            self.assertFalse(report["probe_checks_passed"])
            self.assertFalse(report["native_return_observed"])
            self.assertTrue((self.args.output / "after-trace.jsonl").exists())

    def test_live_identity_requires_fresh_status_and_exact_os_process(self):
        path = self.run / "worker-status.json"
        path.write_text(json.dumps(dict(schema_version=1, generation="a" * 32, game_pid=321)))
        account = dict(prepared=True, user="SporeMP-M04-02", sid="fixture-sid", game_root="C:/fixture-game")
        game = dict(pid=321, parent=100, sid="fixture-sid", path="C:\\fixture-game\\SporebinEP1\\SporeApp.exe")
        # Platform-native path spelling is what worker_manager.os_games supplies on Windows.
        game["path"] = str(Path(account["game_root"]) / "SporebinEP1/SporeApp.exe")
        with patch.object(probe.workers, "supervisor_running", return_value=True), \
                patch.object(probe.workers, "read_json", return_value=account), \
                patch.object(probe.workers, "os_games", return_value=[game]):
            self.assertEqual(probe.live_identity(self.worker)[0], 321)
            game["parent"] = 101
            with self.assertRaisesRegex(ValueError, "OS process"): probe.live_identity(self.worker)
            game["parent"] = 100
            os.utime(path, (1, 1))
            with self.assertRaisesRegex(ValueError, "Stale"): probe.live_identity(self.worker)


if __name__ == "__main__":
    unittest.main()
