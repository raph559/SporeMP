"""Launcher reports, backup and export boundary; synthetic inputs, never SPORE."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile
from types import SimpleNamespace

SERVICE = Path(__file__).resolve().parents[2] / "tools/launcher/launcher_service.py"
spec = importlib.util.spec_from_file_location("launcher_service", SERVICE)
service = importlib.util.module_from_spec(spec)
spec.loader.exec_module(service)


class LauncherServiceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sporemp-launcher-fixture-")
        self.root = Path(self.temp.name)
        self.repo = patch.object(service, "REPO", self.root)
        self.repo.start()

    def tearDown(self):
        self.repo.stop()
        self.temp.cleanup()

    def test_matching_candidate_is_reported_blocked(self):
        with patch.object(service, "installation_check", return_value=({"candidate_match": True, "launch_allowed": False, "launched_processes": 0}, 22)):
            report, code = service.perform("check", self.root)
        self.assertEqual(code, 22)
        self.assertFalse(report["result"]["launch_allowed"])
        self.assertEqual(json.loads(Path(report["report_path"]).read_text()), report)

    def test_native_start_requires_preparation_before_any_process(self):
        with patch.object(service, "prepare", return_value=({"native": {"available": False}}, 22)), patch.object(service.subprocess, "run") as spawn:
            result, code = service.native_launch(self.root)
        self.assertEqual(code, 22)
        self.assertIn("not ready", result["error"])
        spawn.assert_not_called()

    def test_player_launch_uses_current_account_and_requires_disposal(self):
        prepared = {"native": {"available": True}, "installation": {"root": str(self.root)}}
        def fake_process(command, **kwargs):
            self.assertEqual(command[1], "--play")
            self.assertEqual(len(command), 5)
            run = Path(command[-1])
            (run / "native-host.jsonl").write_text('\n'.join(json.dumps(x) for x in [{"event": "created_suspended", "game_pid": 42}, {"event": "game_exited", "exit_code": 0}]))
            (run / "bridge-42.jsonl").write_text('\n'.join(json.dumps({"event": e}) for e in events))
            return SimpleNamespace(returncode=0, stderr="")
        with patch.object(service, "prepare", return_value=(prepared, 22)), patch.object(service, "stage_player_payload", return_value=self.root), patch.object(service.subprocess, "run", side_effect=fake_process):
            for events, expected in [(["initialize"], 31), (["initialize", "dispose"], 0)]:
                report, code = service.perform("native_launch", self.root)
                self.assertEqual(code, expected)
                self.assertEqual(report["native_tests"], "LIFECYCLE_PASSED" if expected == 0 else "FAILED")
                self.assertEqual(report["result"]["clean_lifecycle"], expected == 0)
                self.assertFalse(report["result"]["profile_redirection"])
        self.assertFalse((self.root / "local/native-account").exists())

    def test_player_payload_staging_needs_no_account_or_game_install_writes(self):
        files = ("build/win32/Release/SporeMP.NativeHost.exe", "build/win32/Release/SporeMP.Bridge.dll",
                 "build/sdk/Release/SporeModAPI.dll", "build/injector/Release/ModAPI.DLLInjector.dll")
        for name in files:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(("synthetic " + name).encode())
        self.assertTrue(service.native_availability()["available"])
        staged = service.stage_player_payload()
        self.assertEqual(staged, service.stage_player_payload())
        (staged / "mLibs/SporeMP.Bridge.dll").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "component changed"):
            service.stage_player_payload()
        self.assertFalse((self.root / "local/native-account").exists())

    def test_failures_remain_exportable_and_never_success(self):
        with patch.object(service, "installation_check", side_effect=ValueError("bad PE")):
            report, code = service.perform("check", self.root)
        self.assertEqual(code, 2)
        self.assertEqual(report["result"]["error"], "bad PE")
        export, code = service.perform("export", report_path=Path(report["report_path"]))
        self.assertEqual(code, 0)
        with zipfile.ZipFile(export["result"]["export_path"]) as bundle:
            self.assertEqual(set(bundle.namelist()), {"result.json", "README.txt"})

    def test_export_does_not_follow_paths_in_report_or_include_saves(self):
        report, _ = service.perform("workspace")
        secret = self.root / "private-save.bin"
        secret.write_bytes(b"not exportable")
        path = Path(report["report_path"])
        value = json.loads(path.read_text())
        value["extra_path"] = str(secret)
        path.write_text(json.dumps(value))
        export, _ = service.export_report(path)
        with zipfile.ZipFile(export["export_path"]) as bundle:
            self.assertEqual(len(bundle.namelist()), 2)
            self.assertNotIn(b"not exportable", bundle.read("result.json"))
        with self.assertRaises(ValueError):
            service.export_report(secret)

    def test_repeat_workspaces_and_reports_are_distinct(self):
        first, _ = service.perform("workspace")
        second, _ = service.perform("workspace")
        self.assertNotEqual(first["report_path"], second["report_path"])
        self.assertNotEqual(first["result"]["root"], second["result"]["root"])
        self.assertEqual(first["result"]["native_write_isolation"], "NOT_RUN")

    def test_backup_uses_shell_folders_and_preserves_original(self):
        appdata = self.root / "shell-appdata"
        documents = self.root / "shell-documents"
        (appdata / "Spore").mkdir(parents=True)
        (appdata / "Spore" / "fixture.save").write_bytes(b"original")
        with patch.object(service.diag, "known_folders", return_value={"errors": {}, "shell_folders": {"appdata": str(appdata), "documents": str(documents)}}), patch.object(service.diag, "game_running", return_value=False):
            result, code = service.personal_backup()
        self.assertEqual(code, 0)
        self.assertTrue(result["verified"])
        self.assertEqual(result["file_count"], 1)
        self.assertEqual(len(result["missing_sources"]), 1)
        self.assertEqual((appdata / "Spore/fixture.save").read_bytes(), b"original")

    def test_running_game_rejects_backup_before_copy(self):
        with patch.object(service.diag, "known_folders", return_value={"errors": {}, "shell_folders": {"appdata": str(self.root), "documents": str(self.root)}}), patch.object(service.diag, "game_running", return_value=True):
            result, code = service.perform("backup")
        self.assertEqual(code, 2)
        self.assertIn("running", result["result"]["error"])
        self.assertFalse((self.root / "local/backups").exists())
