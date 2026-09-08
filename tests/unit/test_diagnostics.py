"""Synthetic file/PE fixtures and real host filesystem checks; NEVER game verification."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

MODULE_PATH = Path(__file__).resolve().parents[2] / "tools/diagnostics/sporemp_diag.py"
spec = importlib.util.spec_from_file_location("diag", MODULE_PATH)
diag = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diag)


def synthetic_pe(path, machine=0x14C, magic=0x10B, dll=False):
    payload = bytearray(512)
    payload[:2] = b"MZ"
    struct.pack_into("<I", payload, 0x3C, 128)
    payload[128:132] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", payload, 132, machine, 1, 123, 0, 0, 224, 0x2000 if dll else 0x102)
    struct.pack_into("<H", payload, 152, magic)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


class DiagnosticsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sporemp-fixture-")
        self.root = Path(self.temp.name)
        self.game = self.root / "synthetic-game"
        for folder in diag.GAME_ROOTS:
            (self.game / folder).mkdir(parents=True)
        synthetic_pe(self.game / diag.EXE_RELATIVE)
        (self.game / "Data" / "fixture.package").write_bytes(b"synthetic content, no EA assets")
        self.candidate = diag.game_inventory(self.game)

    def tearDown(self):
        self.temp.cleanup()

    def test_matching_inventory_does_not_grant_launch(self):
        report = diag.compare_candidate(self.game, self.candidate)
        self.assertTrue(report["candidate_match"])
        self.assertFalse(report["launch_allowed"])
        self.assertEqual(report["native_status"], "NOT_RUN")

    def test_same_size_executable_mutation_rejected(self):
        exe = self.game / diag.EXE_RELATIVE
        payload = bytearray(exe.read_bytes())
        payload[-1] ^= 1
        exe.write_bytes(payload)
        result = diag.compare_candidate(self.game, self.candidate)
        self.assertIn("EXECUTABLE_FINGERPRINT_MISMATCH", result["issues"])

    def test_wrong_architecture_rejected_even_if_manifest_matches(self):
        synthetic_pe(self.game / diag.EXE_RELATIVE, 0x8664, 0x20B)
        report = diag.compare_candidate(self.game, diag.game_inventory(self.game))
        self.assertIn("EXECUTABLE_NOT_WIN32_APPLICATION", report["issues"])

    def test_dll_disguised_as_executable_rejected(self):
        synthetic_pe(self.game / diag.EXE_RELATIVE, dll=True)
        self.assertIn("EXECUTABLE_NOT_WIN32_APPLICATION", diag.compare_candidate(self.game, diag.game_inventory(self.game))["issues"])

    def test_malformed_pe_headers_rejected(self):
        exe = self.game / diag.EXE_RELATIVE
        for data in (b"", b"MZ", b"MZ" + b"\0" * 58 + b"\xff" * 4, b"MZ" + b"\0" * 510):
            with self.subTest(length=len(data)):
                exe.write_bytes(data)
                with self.assertRaises(ValueError):
                    diag.pe_identity(exe)

    def test_added_modified_and_missing_content_rejected(self):
        file = self.game / "Data" / "fixture.package"
        original = file.read_bytes()
        file.write_bytes(b"changed")
        self.assertFalse(diag.compare_candidate(self.game, self.candidate)["candidate_match"])
        file.write_bytes(original)
        extra = self.game / "DataEP1" / "unapproved.package"
        extra.write_bytes(b"extra")
        self.assertIn("CONTENT_MISMATCH:DataEP1", diag.compare_candidate(self.game, self.candidate)["issues"])
        extra.unlink()
        file.unlink()
        self.assertIn("CONTENT_MISMATCH:Data", diag.compare_candidate(self.game, self.candidate)["issues"])

    def test_missing_content_root_rejected(self):
        (self.game / "bp1content").rmdir()
        self.assertIn("CONTENT_ROOT_MISSING:bp1content", diag.compare_candidate(self.game, self.candidate)["issues"])

    def test_manifest_cannot_redirect_executable(self):
        self.candidate["executable_relative"] = "../../other.exe"
        with self.assertRaises(ValueError):
            diag.compare_candidate(self.game, self.candidate)

    def test_profile_names_cannot_escape_or_overwrite(self):
        profiles = self.root / "profiles"
        for name in ("../escape", "a/b", "C:\\escape", "NUL", "con", "", "bad name"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                diag.create_profile(profiles, name)
        first = diag.create_profile(profiles, "client-a")
        second = diag.create_profile(profiles, "worker-a")
        self.assertNotEqual(first["profile_id"], second["profile_id"])
        self.assertFalse(first["launch_allowed"])
        with self.assertRaises(FileExistsError):
            diag.create_profile(profiles, "client-a")

    def test_preflight_cannot_be_enabled_by_editing_a_boolean(self):
        profile = diag.create_profile(self.root / "profiles", "worker-a")
        path = Path(profile["root"]) / "profile.json"
        profile["native_write_isolation"] = "VERIFIED"
        profile["launch_allowed"] = True
        path.write_text(json.dumps(profile), encoding="utf-8")
        report = diag.preflight(self.game, self.candidate, path, None)
        self.assertEqual(report["launched_processes"], 0)
        self.assertEqual(report["result"], "NOT_RUN")
        self.assertIn("NATIVE_SAVE_CONFIG_ISOLATION_NOT_VERIFIED", report["blockers"])

    @patch.object(diag, "game_running", return_value=False)
    def test_backup_integrity_and_tamper_detection(self, _):
        source = self.root / "personal-fixture"
        source.mkdir()
        (source / "empty").mkdir()
        (source / "save.bin").write_bytes(b"precious original")
        original = diag.tree_manifest(source)
        destination = self.root / "backup"
        diag.backup([source], destination)
        self.assertTrue(diag.verify_backup(destination)["verified"])
        self.assertEqual(original, diag.tree_manifest(source))
        self.assertTrue((destination / "source-0/empty").is_dir())
        with self.assertRaises(FileExistsError):
            diag.backup([source], destination)
        (destination / "source-0/save.bin").write_bytes(b"corrupt")
        with self.assertRaises(ValueError):
            diag.verify_backup(destination)

    @patch.object(diag, "game_running", return_value=False)
    def test_backup_rejects_overlapping_destination(self, _):
        with self.assertRaises(ValueError):
            diag.backup([self.game], self.game / "nested-backup")

    @patch.object(diag, "game_running", return_value=True)
    def test_backup_refuses_running_game(self, _):
        with self.assertRaises(ValueError):
            diag.backup([self.game], self.root / "backup")
        self.assertFalse((self.root / "backup").exists())

    @patch.object(diag, "game_running", side_effect=[False, True])
    def test_interrupted_backup_never_commits(self, _):
        destination = self.root / "backup"
        with self.assertRaises(ValueError):
            diag.backup([self.game], destination)
        self.assertFalse((destination / "backup-manifest.json").exists())

    def test_evidence_is_exclusive_create(self):
        file = self.root / "evidence.json"
        diag.write_json(file, {"preserve": True})
        with self.assertRaises(FileExistsError):
            diag.write_json(file, {"preserve": False})
        self.assertEqual(json.loads(file.read_text()), {"preserve": True})

    def test_symlink_content_rejected(self):
        link = self.game / "Data" / "linked"
        try:
            os.symlink(self.root, link, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"Host cannot create symlink fixture: {error}")
        with self.assertRaises(ValueError):
            diag.game_inventory(self.game)


if __name__ == "__main__":
    unittest.main()
