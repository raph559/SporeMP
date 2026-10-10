"""HOST-only regression for patched SDK source provenance; starts no game."""
from __future__ import annotations

import difflib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("prepare_sdk_source", HERE / "prepare-sdk-source.py")
PREPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREPARE)


class SourceProvenance(unittest.TestCase):
    def setUp(self):
        self.workspace = HERE.parents[1]
        self.temp = tempfile.TemporaryDirectory(prefix="sdk-source-test-", dir=self.workspace / "local")
        self.repo = Path(self.temp.name)
        self.sdk = self.repo / "external/Spore-ModAPI"
        self.sdk.mkdir(parents=True)
        self.git(self.repo, "init", "--quiet")  # Exercises nested repository discovery.
        self.git(self.sdk, "init", "--quiet")
        self.target = "Spore ModAPI/SourceCode/DLL/TextureOverride.cpp"
        self.original = b"void complete() {\n    Model* model = original;\n    use(model);\n}\n"
        self.updated = self.original.replace(b"Model* model", b"ModelPtr model").replace(b"use(model)", b"use(model.get())")
        (self.sdk / self.target).parent.mkdir(parents=True)
        (self.sdk / self.target).write_bytes(self.original)
        (self.sdk / "unchanged.h").write_bytes(b"// pinned fixture\n")
        self.git(self.sdk, "add", ".")
        self.git(self.sdk, "-c", "user.name=Build fixture", "-c", "user.email=fixture@example.invalid", "commit", "--quiet", "-m", "Fixture")
        commit = self.git(self.sdk, "rev-parse", "HEAD").strip()
        # Archive/patch output must not depend on the checkout's text settings.
        self.git(self.sdk, "config", "core.eol", "crlf")
        self.patch = self.repo / "tools/build/patches/lifetime.patch"
        self.patch.parent.mkdir(parents=True)
        patch = "".join(difflib.unified_diff(self.original.decode().splitlines(True), self.updated.decode().splitlines(True), fromfile="a/" + self.target, tofile="b/" + self.target)).encode()
        self.patch.write_bytes(patch)
        self.lock = {"modapi": {"commit": commit, "source_patch": {
            "path": self.patch.relative_to(self.repo).as_posix(), "sha256": PREPARE.sha(patch),
            "target": self.target, "before_sha256": PREPARE.sha(self.original), "after_sha256": PREPARE.sha(self.updated)}}}
        (self.repo / "config").mkdir()
        self.write_lock()

    def tearDown(self):
        # Only the uniquely created fixture below this workspace may be removed.
        if not self.repo.resolve().is_relative_to((self.workspace / "local").resolve()):
            raise AssertionError("Unexpected temporary test directory")
        self.temp.cleanup()

    @staticmethod
    def git(root, *args):
        return subprocess.check_output(["git", "-C", str(root), *args], stderr=subprocess.DEVNULL, text=True)

    def write_lock(self):
        (self.repo / "config/dependencies.lock.json").write_text(json.dumps(self.lock), encoding="utf-8")

    def test_exact_archive_patch_and_reuse_without_external_mutation(self):
        source = PREPARE.prepare(self.repo)
        self.assertEqual((source / self.target).read_bytes(), self.updated)
        self.assertEqual((source / "unchanged.h").read_bytes(), b"// pinned fixture\n")
        self.assertEqual(PREPARE.prepare(self.repo), source)
        self.assertEqual((self.sdk / self.target).read_bytes(), self.original)
        self.assertEqual(self.git(self.sdk, "status", "--porcelain"), "")

    def test_changed_tracked_dependency_is_refused(self):
        (self.sdk / self.target).write_bytes(self.original + b"// changed\n")
        with self.assertRaisesRegex(ValueError, "checkout has modifications"):
            PREPARE.prepare(self.repo)

    def test_untracked_dependency_is_refused(self):
        (self.sdk / "unreviewed.cpp").write_bytes(b"// unknown\n")
        with self.assertRaisesRegex(ValueError, "checkout has modifications"):
            PREPARE.prepare(self.repo)

    def test_changed_patch_is_refused(self):
        self.patch.write_bytes(self.patch.read_bytes() + b"\n")
        with self.assertRaisesRegex(ValueError, "patch hash"):
            PREPARE.prepare(self.repo)

    def test_wrong_before_hash_is_refused(self):
        self.lock["modapi"]["source_patch"]["before_sha256"] = "00" * 32
        self.write_lock()
        with self.assertRaisesRegex(ValueError, "before hash"):
            PREPARE.prepare(self.repo)

    def test_wrong_after_hash_is_refused_and_cache_stays_incomplete(self):
        self.lock["modapi"]["source_patch"]["after_sha256"] = "00" * 32
        self.write_lock()
        with self.assertRaisesRegex(ValueError, "bytes differ"):
            PREPARE.prepare(self.repo)
        with self.assertRaisesRegex(ValueError, "unknown or incomplete"):
            PREPARE.prepare(self.repo)

    def test_cached_patched_source_modification_is_refused(self):
        source = PREPARE.prepare(self.repo)
        (source / self.target).write_bytes(self.original)
        with self.assertRaisesRegex(ValueError, "bytes differ"):
            PREPARE.prepare(self.repo)

    def test_cached_unrelated_source_modification_is_refused(self):
        source = PREPARE.prepare(self.repo)
        (source / "unchanged.h").write_bytes(b"// changed\n")
        with self.assertRaisesRegex(ValueError, "bytes differ"):
            PREPARE.prepare(self.repo)

    def test_unknown_cached_file_is_refused(self):
        source = PREPARE.prepare(self.repo)
        (source / "unreviewed.h").write_bytes(b"// unknown\n")
        with self.assertRaisesRegex(ValueError, "bytes differ"):
            PREPARE.prepare(self.repo)

    def test_unknown_receipt_is_refused(self):
        source = PREPARE.prepare(self.repo)
        (source.parent / "source.json").write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "unknown or incomplete"):
            PREPARE.prepare(self.repo)


if __name__ == "__main__":
    unittest.main(verbosity=2)
