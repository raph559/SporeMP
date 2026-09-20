"""Automatic launch preparation: fixtures plus real filesystem operations, never SPORE."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from test_launcher_service import service
from test_diagnostics import synthetic_pe


class LauncherAutomationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sporemp-auto-fixture-")
        self.root = Path(self.temp.name)
        self.repo = patch.object(service, "REPO", self.root)
        self.repo.start()
        self.game = self.root / "custom game"
        for folder in service.diag.GAME_ROOTS: (self.game / folder).mkdir(parents=True)
        synthetic_pe(self.game / service.diag.EXE_RELATIVE)
        (self.game / "Data/test.package").write_bytes(b"fixture")
        (self.root / "config").mkdir()
        self.candidate = service.diag.game_inventory(self.game)
        (self.root / "config/compatibility.candidate.json").write_text(json.dumps(self.candidate))
        self.saves = self.root / "personal/Spore"
        self.saves.mkdir(parents=True)
        (self.saves / "fixture.save").write_bytes(b"preserve this original")
        self.sources = patch.object(service, "save_sources", return_value=[self.saves])
        self.sources.start()
        self.running = patch.object(service.diag, "game_running", return_value=False)
        self.running.start()
        self.discovered = service.discovery.discover(service.diag, records=[{"path": str(self.game), "store": "GOG", "source": "fixture registry"}])
        self.discover = patch.object(service.discovery, "discover", return_value=self.discovered)
        self.discover.start()

    def tearDown(self):
        self.discover.stop(); self.running.stop(); self.sources.stop(); self.repo.stop(); self.temp.cleanup()

    def test_cold_and_warm_start_require_no_backup_or_profile_setup(self):
        first, first_code = service.prepare()
        second, second_code = service.prepare()
        self.assertEqual((first_code, second_code), (22, 22))
        self.assertEqual(first["state"], "development_build")
        self.assertFalse(first["launch_allowed"])
        self.assertEqual(first["launched_processes"], 0)
        self.assertNotIn("backup", first)
        self.assertNotIn("workspace", second)
        self.assertFalse((self.root / "local/native-account").exists())
        self.assertFalse((self.root / "local/backups").exists())
        self.assertFalse((self.root / "local/profiles").exists())
        self.assertEqual((self.saves / "fixture.save").read_bytes(), b"preserve this original")
        self.assertEqual(service.load_settings()["game_root"], str(self.game))

    def test_changed_saves_get_a_new_backup(self):
        first = service.ensure_backup()
        (self.saves / "fixture.save").write_bytes(b"new progress")
        second = service.ensure_backup()
        self.assertNotEqual(first["backup_directory"], second["backup_directory"])
        self.assertTrue(service.diag.verify_backup(Path(first["backup_directory"]))["verified"])
        self.assertTrue(service.diag.verify_backup(Path(second["backup_directory"]))["verified"])

    def test_damaged_backup_is_preserved_and_replacement_reused(self):
        first = service.ensure_backup()
        bad = Path(first["backup_directory"]) / "source-0/fixture.save"
        bad.write_bytes(b"corrupt")
        second = service.ensure_backup()
        third = service.ensure_backup()
        self.assertTrue(second["previous_copy_damaged"])
        self.assertNotEqual(first["backup_directory"], second["backup_directory"])
        self.assertEqual(second["backup_directory"], third["backup_directory"])
        self.assertTrue(third["reused"])
        self.assertEqual(bad.read_bytes(), b"corrupt")

    def test_game_running_stops_before_backups(self):
        with patch.object(service.diag, "game_running", return_value=True), patch.object(service.workers, "only_registered_workers_running", return_value=False), patch.object(service, "ensure_backup") as backup:
            result, code = service.prepare()
        self.assertEqual(code, 24)
        self.assertEqual(result["state"], "game_running")
        backup.assert_not_called()

    def test_registered_workers_allow_normal_player_preparation_without_backup(self):
        with patch.object(service.diag, "game_running", return_value=True), patch.object(service.workers, "only_registered_workers_running", return_value=True), patch.object(service, "ensure_backup") as backup:
            result, code = service.prepare()
        self.assertEqual(code, 22)
        self.assertEqual(result['state'], 'development_build')
        backup.assert_not_called()

    def test_mismatch_stops_before_backups(self):
        (self.game / "Data/test.package").write_bytes(b"changed")
        with patch.object(service, "ensure_backup") as backup:
            result, code = service.prepare()
        self.assertEqual(code, 20)
        self.assertFalse(result["compatibility"]["candidate_match"])
        backup.assert_not_called()

    def test_no_game_requests_folder_and_does_not_prepare(self):
        with patch.object(service.discovery, "discover", return_value={"installations": [], "discovery_issues": []}), patch.object(service, "ensure_backup") as backup:
            result, code = service.prepare()
        self.assertEqual(code, 23)
        self.assertEqual(result["state"], "game_not_found")
        backup.assert_not_called()

    def test_ambiguous_matching_installs_require_one_selection(self):
        alternate = {**self.discovered["installations"][0], "root": str(self.root / "second game")}
        self.discovered["installations"].append(alternate)
        first, code = service.prepare()
        self.assertEqual(code, 23)
        self.assertEqual(first["state"], "choose_installation")
        second, code = service.prepare(self.game)
        self.assertEqual(code, 22)
        self.assertEqual(second["installation"]["root"], str(self.game))

    def test_workspace_flags_do_not_grant_native_launch(self):
        first = service.ensure_workspace()
        path = Path(first["root"]) / "profile.json"
        value = json.loads(path.read_text())
        value["launch_allowed"] = True
        value["native_write_isolation"] = "VERIFIED"
        path.write_text(json.dumps(value))
        second = service.ensure_workspace()
        self.assertFalse(second["launch_allowed"])
        self.assertEqual(second["native_write_isolation"], "NOT_RUN")

    @unittest.skipUnless(service.os.name == "nt", "Windows file-lock test")
    def test_second_preparer_cannot_write_concurrently(self):
        with service.preparation_lock():
            with self.assertRaisesRegex(ValueError, "already preparing"):
                with service.preparation_lock(): pass


class DiscoveryTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sporemp-discovery-fixture-")
        self.root = Path(self.temp.name)

    def tearDown(self): self.temp.cleanup()

    def test_registry_root_executable_and_data_paths_deduplicate(self):
        root = self.root / "SPORE"
        synthetic_pe(root / service.diag.EXE_RELATIVE)
        records = [{"path": str(root / tail), "source": "fixture", "store": "EA"} for tail in ("", "DataEP1", "SporebinEP1/SporeApp.exe")]
        result = service.discovery.discover(service.diag, records=records)
        self.assertEqual(len(result["installations"]), 1)
        self.assertEqual(len(result["installations"][0]["sources"]), 3)

    def test_steam_secondary_library_manifest_is_followed(self):
        steam = self.root / "Steam"
        library = self.root / "Another drive with spaces"
        (steam / "steamapps").mkdir(parents=True)
        (library / "steamapps").mkdir(parents=True)
        quoted = str(library).replace("\\", "\\\\")
        (steam / "steamapps/libraryfolders.vdf").write_text('"libraryfolders" { // comment\n "0" { "label" "" "path" "' + quoted + '" } }')
        (library / "steamapps/appmanifest_24720.acf").write_text('"AppState" { "appid" "24720" "installdir" "Spore Galactic Adventures" }')
        game = library / "steamapps/common/Spore Galactic Adventures"
        synthetic_pe(game / service.diag.EXE_RELATIVE)
        result = service.discovery.discover(service.diag, records=[], steam_roots=[steam])
        self.assertEqual(len(result["installations"]), 1)
        self.assertEqual(result["installations"][0]["root"], str(game))
        self.assertEqual(result["installations"][0]["store"], "Steam")

    def test_steam_manifest_cannot_escape_library(self):
        (self.root / "steamapps").mkdir()
        (self.root / "steamapps/appmanifest_24720.acf").write_text('"AppState" { "installdir" "../../other" }')
        records, issues = service.discovery.steam_records([self.root], service.diag.no_reparse)
        self.assertEqual(records, [])
        self.assertTrue(issues)

    def test_broken_and_oversized_steam_metadata_is_rejected(self):
        path = self.root / "libraryfolders.vdf"
        path.write_text('"libraryfolders" { "0" { "path" "broken" }')
        with self.assertRaises(ValueError): service.discovery.read_vdf(path)
        path.write_bytes(b"x" * (service.discovery.MAX_VDF_BYTES + 1))
        with self.assertRaises(ValueError): service.discovery.read_vdf(path)
