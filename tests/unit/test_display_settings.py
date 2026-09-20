"""Display enumeration, persistence and launch boundaries; no original-game execution."""
import ctypes
import json
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

from test_launcher_service import service


class DisplaySettingsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sporemp-display-fixture-")
        self.root = Path(self.temp.name)
        self.repo = patch.object(service, "REPO", self.root)
        self.repo.start()
        self.modes = {"desktop": "2560x1440", "resolutions": ["2560x1440", "1920x1080", "1280x720"]}
        self.enumeration = patch.object(service.display, "display_modes", return_value=self.modes)
        self.enumeration.start()

    def tearDown(self):
        self.enumeration.stop()
        self.repo.stop()
        self.temp.cleanup()

    def test_first_open_preserves_existing_game_settings(self):
        result, code = service.perform("display_settings")
        self.assertEqual(code, 0)
        self.assertEqual(result["result"]["display"], {"mode": "game", "resolution": "desktop"})
        self.assertFalse((self.root / "local/launcher/settings.json").exists())
        self.assertEqual(service.display.resolve(None)["arguments"], [])

    def test_save_reopen_preserves_installation_and_works_while_playing(self):
        service.save_settings({"game_root": "C:/fixture/SPORE", "extra": "preserve"})
        with patch.object(service.diag, "game_running", return_value=True), patch.object(service.subprocess, "run") as spawn:
            saved, code = service.perform("save_display", display_mode="windowed", resolution="1920x1080")
        self.assertEqual(code, 0)
        self.assertEqual(service.display_options()["display"], saved["result"]["display"])
        self.assertEqual(service.load_settings()["game_root"], "C:/fixture/SPORE")
        self.assertEqual(service.load_settings()["extra"], "preserve")
        spawn.assert_not_called()
        self.assertFalse((self.root / "local/backups").exists())

    def test_desktop_is_resolved_again_after_monitor_change(self):
        service.save_display("fullscreen", "desktop")
        self.modes.update(desktop="1920x1080", resolutions=["1920x1080", "1280x720"])
        result = service.display.resolve(service.load_settings()["display"])
        self.assertEqual(result["arguments"], ["--display-mode", "fullscreen", "--resolution", "1920x1080"])

    def test_bad_dimensions_modes_and_argument_injection_are_refused(self):
        for mode, resolution in [("borderless", "desktop"), ("windowed", "1920x1080 -safe"),
                                 ("fullscreen", "1920x1080\n-w"), ("windowed", "0x480"),
                                 ("windowed", "8193x1080"), ("windowed", "1280x0479"),
                                 ("windowed", "1920x+1080"), ("windowed", None)]:
            with self.subTest(mode=mode, resolution=resolution):
                report, code = service.perform("save_display", display_mode=mode, resolution=resolution)
                self.assertEqual(code, 2)
                self.assertIn("error", report["result"])
                self.assertFalse((self.root / "local/launcher/settings.json").exists())

    def test_unavailable_choice_leaves_previous_settings_intact(self):
        service.save_display("windowed", "1280x720")
        before = service.load_settings()
        report, code = service.perform("save_display", display_mode="fullscreen", resolution="3840x2160")
        self.assertEqual(code, 2)
        self.assertIn("unavailable", report["result"]["error"])
        self.assertEqual(service.load_settings(), before)

    def test_unavailable_saved_mode_blocks_before_payload_or_process(self):
        service.save_display("fullscreen", "2560x1440")
        self.modes.update(desktop="1920x1080", resolutions=["1920x1080"])
        self.assertIn("unavailable", service.display_options()["warning"])
        prepared = {"native": {"available": True}, "installation": {"root": str(self.root)}}
        with patch.object(service, "prepare", return_value=(prepared, 22)), patch.object(service, "stage_player_payload") as stage, patch.object(service.subprocess, "run") as spawn:
            report, code = service.perform("native_launch", self.root)
        self.assertEqual(code, 2)
        self.assertIn("Settings > Display", report["result"]["error"])
        stage.assert_not_called()
        spawn.assert_not_called()

    def test_saved_choice_reaches_actual_host_argument_boundary(self):
        prepared = {"native": {"available": True}, "installation": {"root": str(self.root)}}
        for mode, resolution, arguments in [("windowed", "1280x720", ["--display-mode", "windowed", "--resolution", "1280x720"]),
                                             ("fullscreen", "desktop", ["--display-mode", "fullscreen", "--resolution", "2560x1440"]),
                                             ("game", "desktop", [])]:
            service.save_display(mode, resolution)
            def fake_process(command, **kwargs):
                self.assertEqual(command[1:3], ["--play", str(self.root)])
                self.assertEqual(command[5:], arguments)
                run = Path(command[4])
                (run / "native-host.jsonl").write_text('\n'.join(json.dumps(x) for x in [
                    {"event": "created_suspended", "game_pid": 42}, {"event": "game_exited", "exit_code": 0}]))
                (run / "bridge-42.jsonl").write_text('\n'.join(json.dumps({"event": e}) for e in ["initialize", "dispose"]))
                return SimpleNamespace(returncode=0, stderr="")
            with self.subTest(mode=mode), patch.object(service, "prepare", return_value=(prepared, 22)), patch.object(service, "stage_player_payload", return_value=self.root), patch.object(service.subprocess, "run", side_effect=fake_process):
                result, code = service.native_launch(self.root)
                self.assertEqual(code, 0)
                self.assertEqual(result["display"]["mode"], mode)

    def test_malformed_saved_display_can_be_replaced_without_losing_game_root(self):
        service.save_settings({"game_root": "C:/fixture", "display": {"mode": "nonsense"}})
        self.assertIn("invalid", service.display_options()["warning"])
        service.save_display("game", "desktop")
        self.assertEqual(service.load_settings()["game_root"], "C:/fixture")
        self.assertEqual(service.display.resolve(service.load_settings()["display"])["arguments"], [])

    def test_devmode_layout_matches_windows_abi(self):
        self.assertEqual(ctypes.sizeof(service.display.DevMode), 220)
        self.assertEqual(service.display.DevMode.dmPelsWidth.offset, 172)
        self.assertEqual(service.display.DevMode.dmPelsHeight.offset, 176)
