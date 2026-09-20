"""HOST/FIXTURE launcher authentication, secret lifetime and native readiness gates."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SERVICE = Path(__file__).resolve().parents[2] / "tools/launcher/launcher_service.py"
spec = importlib.util.spec_from_file_location("m06_launcher_service", SERVICE)
service = importlib.util.module_from_spec(spec)
spec.loader.exec_module(service)
sessions = service.multiplayer
INVITE = "sporemp://join?host=127.0.0.1&port=27060&cert=" + "a" * 64 + "&token=" + "b" * 64


class MultiplayerLauncherTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="sporemp-m06-launcher-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.addCleanup(patch.stopall)
        patch.object(service, "REPO", self.root).start()

    def fixture(self):
        for relative, content in {
            "build/win32/Release/SporeMP.Coordinator.exe": b"HOST fixture, never executed",
            "mLibs/SporeMP.Bridge.dll": b"HOST bridge fixture, never loaded",
            "config/compatibility.candidate.json": json.dumps({"executable": {"sha256": "c" * 64}}).encode(),
            "fixture-appdata/Spore/Games/Game0/Satiria.spo": b"HOST native scene fixture, never loaded",
        }.items():
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        patch.object(service, "prepare", return_value=({"native": {"available": True}, "installation": {"root": str(self.root)}}, 22)).start()
        patch.object(service, "stage_player_payload", return_value=self.root).start()
        patch.object(service.display, "resolve", return_value={"arguments": []}).start()
        patch.object(service.diag, "known_folders", return_value={"errors": {}, "shell_folders": {"appdata": str(self.root / "fixture-appdata")}}).start()

    def test_invite_parser_normalizes_endpoint_and_never_reflects_secret(self):
        config = sessions.parse_invitation(INVITE)
        self.assertEqual(config["host"], "127.0.0.1")
        self.assertEqual(config["port"], 27060)
        self.assertEqual(config["credential"], "b" * 64)
        for invalid in (INVITE + "&token=" + "b" * 64, INVITE + "#fragment", INVITE.replace("27060", "65536"),
                        INVITE.replace("127.0.0.1", "host%0Arole=authority"), INVITE.replace("127.0.0.1", "-bad.host"),
                        INVITE.replace("b" * 64, "0" * 64), INVITE.replace("sporemp", "https"), "x" * 2049):
            with self.subTest(invalid=invalid[:40]):
                with self.assertRaises(ValueError) as error: sessions.parse_invitation(invalid)
                self.assertNotIn("b" * 64, str(error.exception))

    def test_private_file_has_protected_owner_acl_and_is_removed_after_failure(self):
        path = None
        with self.assertRaisesRegex(RuntimeError, "fixture failure"):
            with sessions.session_file(self.root, sessions.parse_invitation(INVITE)) as path:
                self.assertIn("credential=" + "b" * 64, path.read_text())
                script = self.root / "check-acl.ps1"
                script.write_text("param([string]$SessionPath)\n(Get-Acl -LiteralPath $SessionPath).Sddl")
                command = ["pwsh", "-NoProfile", "-File", str(script), str(path)]
                result = subprocess.run(command, capture_output=True, text=True, check=True)
                self.assertIn("D:P(A;;FA;;;OW)", result.stdout)
                raise RuntimeError("fixture failure")
        self.assertFalse(path.exists())

    def test_changed_or_unsupported_installation_never_reaches_network(self):
        with patch.object(service, "prepare", return_value=({"native": {"available": False}}, 20)), \
             patch.object(service.subprocess, "run") as connect:
            result, code = service.native_join(self.root, INVITE)
        self.assertEqual(code, 20)
        connect.assert_not_called()

    def test_authentication_rejection_prevents_game_and_removes_secret(self):
        self.fixture()
        paths = []
        def reject(command, **kwargs):
            self.assertEqual(command[1:4], ["--client-probe", "--config", command[3]])
            self.assertNotIn("b" * 64, str(command))
            paths.append(Path(command[3]))
            self.assertIn("role=player", paths[-1].read_text())
            self.assertIn("fixture_sha256=" + service.diag.fingerprint(self.root / "fixture-appdata/Spore/Games/Game0/Satiria.spo")["sha256"], paths[-1].read_text())
            return SimpleNamespace(returncode=5, stdout=json.dumps({"error": "unsupported_build " + "b" * 64}))
        with patch.object(service.subprocess, "run", side_effect=reject), patch.object(service.subprocess, "Popen") as game:
            report, code = service.perform("native_join", self.root, invitation=INVITE)
        self.assertEqual(code, 32)
        game.assert_not_called()
        self.assertFalse(paths[0].exists())
        self.assertIn("unsupported_build", report["result"]["error"])
        self.assertNotIn("b" * 64, Path(report["report_path"]).read_text())

    def test_missing_native_fixture_never_reaches_network_or_replaces_saves(self):
        self.fixture()
        fixture = self.root / "fixture-appdata/Spore/Games/Game0/Satiria.spo"
        fixture.unlink()
        with patch.object(service.subprocess, "run") as connect, patch.object(service.subprocess, "Popen") as game:
            result, code = service.native_join(self.root, INVITE)
        self.assertEqual(code, 32)
        self.assertIn("Satiria", result["error"])
        connect.assert_not_called()
        game.assert_not_called()
        self.assertFalse(fixture.exists())

    def test_probe_timeout_remains_bounded_and_prevents_game(self):
        self.fixture()
        with patch.object(service.subprocess, "run", side_effect=subprocess.TimeoutExpired("fixture", 25)), patch.object(service.subprocess, "Popen") as game:
            result, code = service.native_join(self.root, INVITE)
        self.assertEqual(code, 32)
        self.assertIn("in time", result["error"])
        game.assert_not_called()
        self.assertEqual(list((self.root / "local/launcher/sessions").glob("*")), [])

    def test_native_readiness_needs_applied_baseline_and_proper_lifetime(self):
        self.fixture()
        owner = self
        class HostFixture:
            returncode = 0
            def __init__(self, command, **kwargs):
                owner.assertEqual(command[1], "--join")
                owner.assertNotIn("b" * 64, str(command))
                self.session = Path(command[5])
                owner.assertTrue(self.session.exists())
                self.root = Path(command[4])
                (self.root / "native-host.jsonl").write_text('\n'.join(json.dumps(value) for value in (
                    {"event": "created_suspended", "game_pid": 42}, {"event": "game_exited", "exit_code": 0})))
                (self.root / "bridge-42.jsonl").write_text('\n'.join(json.dumps({"event": event}) for event in ("initialize", "dispose")))
                (self.root / "network-status.json").write_text(json.dumps({"state": "connected", "player_id": 2, "baseline_sequence": 1 if owner.baseline else 0}))
            def __enter__(self): return self
            def __exit__(self, *args): owner.assertTrue(self.session.exists())
            def communicate(self, timeout):
                owner.assertTrue(self.session.exists())
                return "", ""
        with patch.object(service.subprocess, "run", return_value=SimpleNamespace(returncode=0, stdout='{"authenticated":true}')), \
             patch.object(service.subprocess, "Popen", HostFixture):
            for self.baseline in (False, True):
                phases = []
                report, code = service.perform("native_join", self.root, invitation=INVITE, progress=lambda phase, _: phases.append(phase))
                self.assertEqual(code, 0 if self.baseline else 31)
                self.assertEqual("connected" in phases, self.baseline)
                self.assertEqual(report["native_tests"], "LIFECYCLE_PASSED" if self.baseline else "FAILED")
                self.assertNotIn("b" * 64, json.dumps(report))
        self.assertEqual(list((self.root / "local/launcher/sessions").glob("*")), [])

    def test_untrusted_status_cannot_claim_connected(self):
        path = self.root / "network-status.json"
        for value in ({"state": "connected"}, {"state": "connected", "player_id": 3, "baseline_sequence": 1},
                      {"state": "connected", "player_id": True, "baseline_sequence": 1},
                      {"state": "connected", "player_id": 1, "baseline_sequence": 0}):
            path.write_text(json.dumps(value))
            self.assertIsNone(sessions.connection_state(path))
        path.write_text("x" * 4097)
        self.assertIsNone(sessions.connection_state(path))


if __name__ == "__main__": unittest.main()
