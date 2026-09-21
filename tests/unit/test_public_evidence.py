"""Exercise public evidence policy against temporary Git indexes and files."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "tools/check-public-evidence.py"
SPEC = importlib.util.spec_from_file_location("public_evidence", SCRIPT)
policy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(policy)


class PublicEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name)
        self.git("init", "--quiet")

    def git(self, *arguments):
        return subprocess.run(["git", "-C", str(self.repo), *arguments],
                              check=True, capture_output=True)

    def write(self, path, content, tracked=True):
        file = self.repo / path
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(content.encode("utf-8") if isinstance(content, str) else content)
        if tracked:
            self.git("add", "--", path)
        return file

    def allow(self, *paths):
        self.write(policy.ALLOWLIST, "\n".join(paths) + "\n")

    def assert_failure(self, expected):
        errors, _, _ = policy.check(self.repo)
        self.assertTrue(any(expected in error for error in errors), errors)

    def test_reviewed_reports_pass_and_untracked_outputs_are_ignored(self):
        self.allow("# Reviewed reports", "evidence/run/SESSION.md", "evidence/run/result.json")
        self.write("evidence/run/SESSION.md", "Native execution: NOT RUN.\n")
        self.write("evidence/run/result.json", "{}\n")
        self.write("evidence/run/raw.jsonl", b"x" * (policy.MAX_FILE_BYTES + 1), tracked=False)
        self.write("local/private/raw.jsonl", "private fixture", tracked=False)
        errors, count, total = policy.check(self.repo)
        self.assertEqual(errors, [])
        self.assertEqual(count, 2)
        self.assertEqual(total, 30)

    def test_missing_allowlist_is_rejected(self):
        self.assert_failure("Cannot read docs/public-evidence-files.txt")

    def test_allowlisted_untracked_report_is_rejected(self):
        self.allow("evidence/SESSION.md")
        self.write("evidence/SESSION.md", "report", tracked=False)
        self.assert_failure("Allowlisted evidence is not tracked")

    def test_tracked_report_missing_from_worktree_is_rejected(self):
        self.allow("evidence/SESSION.md")
        self.write("evidence/SESSION.md", "report").unlink()
        self.assert_failure("Tracked evidence file is missing")

    def test_unlisted_tracked_report_is_rejected(self):
        self.allow()
        self.write("evidence/SESSION.md", "report")
        self.assert_failure("Tracked evidence is not allowlisted")

    def test_raw_extension_cannot_be_allowlisted(self):
        self.allow("evidence/raw.jsonl")
        self.write("evidence/raw.jsonl", "{}\n")
        self.assert_failure("Evidence must be .md or .json")

    def test_oversized_report_is_rejected(self):
        self.allow("evidence/large.json")
        self.write("evidence/large.json", b" " * (policy.MAX_FILE_BYTES + 1))
        self.assert_failure("Evidence exceeds 65536 bytes")

    def test_combined_size_limit_is_enforced(self):
        paths = [f"evidence/report-{number}.json" for number in range(33)]
        self.allow(*paths)
        for path in paths:
            self.write(path, b" " * policy.MAX_FILE_BYTES)
        self.assert_failure("Evidence exceeds total limit")

    def test_allowlist_rejects_traversal_and_paths_outside_evidence(self):
        for path in ("evidence/../private.json", "docs/report.md", "/evidence/report.md",
                     "evidence\\report.md", "evidence//report.md"):
            with self.subTest(path=path):
                self.allow(path)
                self.assert_failure("invalid evidence path")

    def test_duplicate_allowlist_entry_is_rejected(self):
        self.allow("evidence/SESSION.md", "evidence/SESSION.md")
        self.write("evidence/SESSION.md", "report")
        self.assert_failure("duplicate path")


if __name__ == "__main__":
    unittest.main()
