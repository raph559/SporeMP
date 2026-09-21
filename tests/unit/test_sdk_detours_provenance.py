"""Synthetic MSVC-log/artifact fixtures only; no original-game or native build test."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


MODULE_PATH = Path(__file__).resolve().parents[2] / "tools/build/verify-sdk-detours.py"
SPEC = importlib.util.spec_from_file_location("sdk_detours_provenance", MODULE_PATH)
provenance = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(provenance)


class SdkDetoursProvenanceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sporemp-synthetic-detours-")
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name).resolve()
        self.configuration = "Release"
        self.pins = {"modapi": {"commit": "a" * 40}, "native_loader": {"commit": "b" * 40}}
        for relative in provenance.artifact_paths(self.configuration):
            path = self.repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(("synthetic fixture: " + relative).encode())
        (self.repo / "config/dependencies.lock.json").write_text(json.dumps(self.pins), encoding="utf-8")
        self.revision_patch = patch.object(provenance, "dependency_revision", side_effect=self.fixture_revision)
        self.revision_patch.start()
        self.addCleanup(self.revision_patch.stop)
        self.header = str(self.repo / provenance.DETOURS_HEADER)
        self.library = str(self.repo / "build/detours4/Release/sporemp_detours.lib")
        self.sdk_log = self.repo / "build/sdk/Release/obj-detours4/sdk.tlog/CL.read.1.tlog"
        self.base_log = self.repo / "build/sdk-base/Release/obj-detours4/base.tlog/CL.read.1.tlog"
        self.link_log = self.sdk_log.with_name("link.read.1.tlog")
        self.write_log(self.sdk_log, ["^" + str(self.repo / "external/Spore-ModAPI/Spore ModAPI/source.cpp"), self.header])
        self.write_log(self.base_log, [self.header])
        self.write_log(self.link_log, [self.library])

    def fixture_revision(self, path):
        name = "modapi" if path.name == "Spore-ModAPI" else "native_loader"
        return self.pins[name]["commit"]

    def write_log(self, path, entries):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("\r\n".join(entries) + "\r\n", encoding="utf-16")

    def record(self):
        return provenance.verify(self.repo, self.configuration)

    def test_valid_inputs_record_pins_and_hashes_check_survives_log_removal(self):
        target = self.record()
        manifest = json.loads(target.read_text(encoding="utf-8"))
        self.assertEqual(manifest["dependency_revisions"]["modapi"], "a" * 40)
        self.assertEqual(set(manifest["sha256"]), set(provenance.artifact_paths("Release")))
        self.assertEqual(set(manifest["build_input_logs"]), {"sdk_compile", "base_compile", "sdk_link"})
        for path in [self.sdk_log, self.base_log, self.link_log]:
            path.unlink()
        self.assertEqual(provenance.verify(self.repo, "Release", check=True), target)

    def test_each_required_log_family_must_exist(self):
        for log in [self.sdk_log, self.base_log, self.link_log]:
            with self.subTest(log=log.name + str(log.parent)):
                payload = log.read_bytes()
                log.unlink()
                with self.assertRaisesRegex(provenance.ProvenanceError, "Missing"):
                    self.record()
                log.write_bytes(payload)

    def test_both_compile_targets_must_read_the_pinned_header(self):
        for log in [self.sdk_log, self.base_log]:
            with self.subTest(log=log):
                payload = log.read_bytes()
                self.write_log(log, [str(self.repo / "external/another/detours.h")])
                with self.assertRaisesRegex(provenance.ProvenanceError, "header missing"):
                    self.record()
                log.write_bytes(payload)

    def test_legacy_paths_are_rejected_in_each_log_family(self):
        legacy_paths = [
            str(self.repo / "external/Spore-ModAPI/Detours/include/detours.h"),
            str(self.repo / "external/Spore-ModAPI/libdetours/detours.lib"),
            str(self.repo / "external/Spore-ModAPI/Spore ModAPI/libdetours/old.obj"),
            r"E:\Detours\include\detours.h",
            r"e:/DETOURS/lib.X86/detours.lib",
            str(self.repo / "unexpected/detours.lib"),
        ]
        for log in [self.sdk_log, self.base_log, self.link_log]:
            for legacy in legacy_paths:
                with self.subTest(log=log, legacy=legacy):
                    payload = log.read_bytes()
                    with log.open("a", encoding="utf-16") as stream:
                        stream.write("^" + legacy + "|" + self.header + "\n")
                    with self.assertRaisesRegex(provenance.ProvenanceError, "Legacy or ambiguous"):
                        self.record()
                    log.write_bytes(payload)

    def test_sdk_link_requires_exact_configuration_and_unique_library(self):
        alternatives = [
            [str(self.repo / "build/detours4/Debug/sporemp_detours.lib")],
            ["build/detours4/Release/sporemp_detours.lib"],
            [self.library, str(self.repo / "another/sporemp_detours.lib")],
            [str(self.repo / "system/kernel32.lib")],
        ]
        for entries in alternatives:
            with self.subTest(entries=entries):
                self.write_log(self.link_log, entries)
                with self.assertRaisesRegex(provenance.ProvenanceError, "configuration-specific"):
                    self.record()

    def test_windows_paths_are_case_and_separator_insensitive(self):
        self.write_log(self.sdk_log, [self.header.upper().replace("\\", "/")])
        self.write_log(self.base_log, [self.header.upper().replace("/", "\\")])
        self.write_log(self.link_log, ['"' + self.library.upper() + '"'])
        self.record()

    def test_check_refuses_missing_manifest(self):
        with self.assertRaisesRegex(provenance.ProvenanceError, "Missing SDK Detours provenance"):
            provenance.verify(self.repo, "Release", check=True)

    def test_check_refuses_each_stale_artifact_or_build_input(self):
        self.record()
        for relative in provenance.artifact_paths("Release"):
            if relative == "config/dependencies.lock.json":
                continue
            with self.subTest(relative=relative):
                path = self.repo / relative
                original = path.read_bytes()
                path.write_bytes(original + b" changed")
                with self.assertRaisesRegex(provenance.ProvenanceError, "hashes are stale"):
                    provenance.verify(self.repo, "Release", check=True)
                path.write_bytes(original)

    def test_check_refuses_dependency_revision_drift(self):
        self.record()
        with patch.object(provenance, "dependency_revision", return_value="c" * 40):
            with self.assertRaisesRegex(provenance.ProvenanceError, "revision mismatch"):
                provenance.verify(self.repo, "Release", check=True)

    def test_check_refuses_changed_lock_and_missing_hash_record(self):
        target = self.record()
        lock_path = self.repo / "config/dependencies.lock.json"
        lock_path.write_text(json.dumps({**self.pins, "changed": True}), encoding="utf-8")
        with self.assertRaisesRegex(provenance.ProvenanceError, "hashes are stale"):
            provenance.verify(self.repo, "Release", check=True)
        lock_path.write_text(json.dumps(self.pins), encoding="utf-8")
        manifest = json.loads(target.read_text(encoding="utf-8"))
        manifest["sha256"].pop("build/sdk/Release/SporeModAPI.dll")
        target.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(provenance.ProvenanceError, "hashes are stale"):
            provenance.verify(self.repo, "Release", check=True)


if __name__ == "__main__":
    unittest.main()
