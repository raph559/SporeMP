"""M08 HOST/FIXTURE parser and quarantine tests; no original gameplay."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("spore_content", REPO / "tools/content/spore_content.py")
content = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(content)
KEY = "40626200!12345678.2b978c46"
OTHER = "40626200!12345679.2b978c46"


def packed(decoded_size, commands):
    return b"\x10\xfb" + decoded_size.to_bytes(3, "big") + commands


def archive(records=None, *, big=False, flags=4, padding=b"", reverse=False):
    """Synthetic resources only; explicit DBPF/DBBF 0.3 index layout."""
    if records is None:
        records = [(KEY, b"ABCD", 4, 0)]
    records = list(reversed(records)) if reverse else records
    header = bytearray(120 if big else 96)
    header[:4] = b"DBBF" if big else b"DBPF"
    struct.pack_into("<I", header, 4, 2 if big else 3)
    struct.pack_into("<I", header, 36, len(records))
    struct.pack_into("<I", header, 52 if big else 60, 3)
    data = header + padding
    index = bytearray(struct.pack("<I", flags))
    if flags & 1:
        index.extend(struct.pack("<I", 0x2B978C46))
    if flags & 2:
        index.extend(struct.pack("<I", 0x40626200))
    index.extend(struct.pack("<I", 0))
    for key, blob, size, compression in records:
        group, tail = key.split("!")
        instance, kind = tail.split(".")
        if not flags & 1:
            index.extend(struct.pack("<I", int(kind, 16)))
        if not flags & 2:
            index.extend(struct.pack("<I", int(group, 16)))
        index.extend(struct.pack("<I", int(instance, 16)))
        index.extend(struct.pack("<Q" if big else "<I", len(data)))
        index.extend(struct.pack("<IIHBB", len(blob) | 0x80000000, size, compression, 1, 0))
        data.extend(blob)
    if big:
        struct.pack_into("<Q", data, 40, len(index))
        struct.pack_into("<Q", data, 56, len(data))
    else:
        struct.pack_into("<I", data, 44, len(index))
        struct.pack_into("<I", data, 64, len(data))
    return bytes(data + index)


def changed(data, offset, value, fmt="<I"):
    result = bytearray(data)
    struct.pack_into(fmt, result, offset, value)
    return bytes(result)


class ContentTests(unittest.TestCase):
    def assert_error(self, code, fn, *args):
        with self.assertRaises(content.ContentError) as caught:
            fn(*args)
        self.assertEqual(code, caught.exception.code)

    def test_both_container_formats_and_all_shared_key_flags(self):
        for big in (False, True):
            for flags in (4, 5, 6, 7):
                with self.subTest(big=big, flags=flags):
                    report = content.inspect_bytes(archive(big=big, flags=flags))
                    self.assertEqual(report["records"], [dict(key=KEY, size=4, sha256=content.digest(b"ABCD"))])
                    self.assertFalse(report["readiness"])
                    self.assertEqual(report["native_validation"], "NOT_RUN")

    def test_empty_native_header_and_empty_index(self):
        for big in (False, True):
            data = archive([], big=big)
            self.assertEqual(content.parse_archive(data), [])
            data = bytearray(data[:120 if big else 96])
            struct.pack_into("<Q" if big else "<I", data, 40 if big else 44, 0)
            struct.pack_into("<Q" if big else "<I", data, 56 if big else 64, 0)
            self.assertEqual(content.parse_archive(bytes(data)), [])

    def test_content_identity_ignores_order_offsets_container_and_compression(self):
        a = archive([(KEY, b"ABCD", 4, 0), (OTHER, b"AAA", 3, 0)])
        b = archive([(KEY, packed(4, b"\xe0ABCD\xfc"), 4, 0xFFFF),
                     (OTHER, packed(3, b"\xffAAA"), 3, 0xFFFF)], big=True, reverse=True, padding=b"unused hole")
        left, right = content.inspect_bytes(a), content.inspect_bytes(b)
        self.assertNotEqual(left["archive_sha256"], right["archive_sha256"])
        self.assertEqual(left["record_set_sha256"], right["record_set_sha256"])
        self.assertTrue(content.compare(left, right)["records_equal"])

    def test_comparison_reports_missing_changed_and_extra_keys(self):
        third = "40626200!1234567a.2b978c46"
        a = content.inspect_bytes(archive([(KEY, b"A", 1, 0), (OTHER, b"B", 1, 0)]))
        b = content.inspect_bytes(archive([(KEY, b"C", 1, 0), (third, b"D", 1, 0)]))
        report = content.compare(a, b)
        self.assertEqual(report["missing_keys"], [OTHER])
        self.assertEqual(report["changed_keys"], [KEY])
        self.assertEqual(report["unexpected_keys"], [third])
        self.assertFalse(report["records_equal"])
        self.assertFalse(report["readiness"])

    def test_container_and_version_refusals(self):
        data = archive()
        for payload, code in [(b"", "truncated"), (b"PNG!" + data[4:], "unsupported_container"),
                              (data[:50], "truncated"), (changed(data, 4, 1), "unsupported_version"),
                              (changed(data, 60, 9), "unsupported_index_version")]:
            self.assert_error(code, content.inspect_bytes, payload)

    def test_index_span_count_and_flag_refusals(self):
        data = archive()
        index = struct.unpack_from("<I", data, 64)[0]
        for payload, code in [(changed(data, 36, 16385), "count_limit"),
                              (changed(data, 64, 8), "invalid_index_span"),
                              (changed(data, 64, 0xFFFFFFF0), "invalid_index_span"),
                              (changed(data, 44, 0xFFFFFFFF), "invalid_index_span"),
                              (changed(data, 36, 2), "invalid_index_size"),
                              (changed(data, index, 0), "unsupported_index_flags"),
                              (changed(data, index, 12), "unsupported_index_flags"),
                              (changed(data, index + 4, 1), "unsupported_index_flags")]:
            self.assert_error(code, content.inspect_bytes, payload)

    def test_duplicate_keys_and_overlaps(self):
        self.assert_error("duplicate_key", content.inspect_bytes,
                          archive([(KEY, b"A", 1, 0), (KEY, b"B", 1, 0)]))
        data = archive([(KEY, b"A", 1, 0), (OTHER, b"B", 1, 0)])
        index = struct.unpack_from("<I", data, 64)[0]
        self.assert_error("overlapping_records", content.inspect_bytes, changed(data, index + 8 + 28 + 12, 96))
        self.assert_error("overlapping_records", content.inspect_bytes, changed(data, index + 8 + 12, index))
        self.assert_error("invalid_record_span", content.inspect_bytes, changed(data, index + 8 + 12, 2))

    def test_record_flags_sizes_and_64_bit_bounds(self):
        data = archive(big=True)
        index = struct.unpack_from("<Q", data, 56)[0]
        entry = index + 8
        for payload, code in [(changed(data, entry + 12, 1 << 40, "<Q"), "invalid_record_span"),
                              (changed(data, entry + 20, 9999), "invalid_record_span"),
                              (changed(data, entry + 24, 5), "expanded_size_mismatch"),
                              (changed(data, entry + 24, content.MAX_RECORD + 1), "size_limit"),
                              (changed(data, entry + 28, 0x1234, "<H"), "unsupported_compression"),
                              (changed(data, entry + 30, 0, "<B"), "unsupported_record_flags")]:
            self.assert_error(code, content.inspect_bytes, payload)
        with patch.object(content, "MAX_EXPANDED", 3):
            self.assert_error("size_limit", content.inspect_bytes, archive())
        with patch.object(content, "MAX_ARCHIVE", 10):
            self.assert_error("size_limit", content.inspect_bytes, archive())

    def test_every_refpack_command_form_and_overlapping_copies(self):
        cases = [(b"\xfc", b""), (b"\xffabc", b"abc"), (b"\xe0ABCD\xfc", b"ABCD"),
                 (b"\x01\x00A\xfc", b"AAAA"), (b"\x80\x40\x00A\xfc", b"AAAAA"),
                 (b"\xc1\x00\x00\x00A\xfc", b"AAAAAA"),
                 (b"\xe0abcd\x08\x03\xfc", b"abcdabcda")]
        for commands, expected in cases:
            with self.subTest(commands=commands):
                self.assertEqual(content.refpack(packed(len(expected), commands), len(expected)), expected)
        self.assertEqual(content.refpack(b"\x50\xfb\0\0\3\xffabc", 3), b"abc")

    def test_refpack_corruption_refusals(self):
        for blob, size, code in [(packed(4, b"\xe0ABCD\xfc"), 5, "expanded_size_mismatch"),
                                 (packed(4, b"\xe0ABC"), 4, "truncated"),
                                 (packed(4, b"\xe0ABCD"), 4, "truncated"),
                                 (packed(3, b"\x00\x00\xfc"), 3, "invalid_back_reference"),
                                 (packed(3, b"\xe0ABCD\xfc"), 3, "expanded_size_mismatch"),
                                 (packed(4, b"\xfc"), 4, "expanded_size_mismatch"),
                                 (packed(0, b"\xfcJUNK"), 0, "expanded_size_mismatch"),
                                 (b"\x11\xfb\0\0\0\xfc", 0, "unsupported_compression")]:
            self.assert_error(code, content.refpack, blob, size)

    def test_all_truncated_prefixes_reject(self):
        for big in (False, True):
            data = archive(big=big)
            for length in range(len(data)):
                with self.subTest(big=big, length=length), self.assertRaises(content.ContentError):
                    content.inspect_bytes(data[:length])


class QuarantineTests(unittest.TestCase):
    assert_error = ContentTests.assert_error
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sporemp-content-fixture-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cache = self.root / "local/cache"

    def test_quarantine_is_immutable_and_deduplicates_payloads(self):
        data = archive([(KEY, b"A", 1, 0), (OTHER, b"A", 1, 0)])
        result = content.quarantine(data, [OTHER, KEY], self.cache)
        again = content.quarantine(data, [KEY, OTHER], self.cache)
        self.assertEqual(result, again)
        target = self.cache / result["candidate_id"]
        self.assertEqual(len(list(target.iterdir())), 2)
        manifest = json.loads((target / "manifest.json").read_text())
        self.assertFalse(manifest["transfer_approved"])
        self.assertFalse(manifest["readiness"])
        self.assertEqual(manifest["dependency_status"], "UNKNOWN")

    def test_corrupt_cache_never_replaced(self):
        data = archive()
        result = content.quarantine(data, [KEY], self.cache)
        target = self.cache / result["candidate_id"]
        blob = next(target.glob("*.blob"))
        blob.write_bytes(b"bad!")
        self.assert_error("cache_corrupt", content.quarantine, data, [KEY], self.cache)
        self.assertEqual(blob.read_bytes(), b"bad!")

    def test_missing_or_unexpected_cache_file_refused(self):
        result = content.quarantine(archive(), [KEY], self.cache)
        target = self.cache / result["candidate_id"]
        (target / "untrusted.dll").write_bytes(b"FIXTURE")
        self.assert_error("cache_corrupt", content.quarantine, archive(), [KEY], self.cache)
        (target / "untrusted.dll").unlink()
        next(target.glob("*.blob")).unlink()
        self.assert_error("cache_corrupt", content.quarantine, archive(), [KEY], self.cache)

    def test_bad_selections_cannot_create_files(self):
        for selection, code in [([], "selection_limit"), ([KEY] * 129, "selection_limit"),
                                ([KEY, KEY], "duplicate_selection"), ([OTHER], "missing_record"),
                                (["../../evil.dll"], "invalid_key"), (["C:\\evil"], "invalid_key"),
                                ([KEY + ":stream"], "invalid_key"), ([KEY.upper()], "invalid_key")]:
            self.assert_error(code, content.quarantine, archive(), selection, self.cache)
            self.assertFalse(self.cache.exists())

    def test_invalid_unselected_payload_also_rejected(self):
        data = archive([(KEY, b"A", 1, 0), (OTHER, b"not-refpack", 2, 0xFFFF)])
        self.assert_error("unsupported_compression", content.quarantine, data, [KEY], self.cache)
        self.assertFalse(self.cache.exists())

    def test_interrupted_write_never_becomes_a_candidate(self):
        real_write = content.write_new
        def interrupted(path, data):
            if path.name == "manifest.json":
                raise OSError("fixture interruption before commit marker")
            real_write(path, data)
        with patch.object(content, "write_new", side_effect=interrupted), self.assertRaises(OSError):
            content.quarantine(archive(), [KEY], self.cache)
        self.assertTrue(all(path.name.startswith(".incomplete-") for path in self.cache.iterdir()))
        self.assertEqual(list(self.cache.rglob("manifest.json")), [])
        self.assertFalse(content.quarantine(archive(), [KEY], self.cache)["readiness"])

    def test_changed_content_gets_new_version(self):
        a = content.quarantine(archive(), [KEY], self.cache)
        b = content.quarantine(archive([(KEY, b"ABCE", 4, 0)]), [KEY], self.cache)
        self.assertNotEqual(a["candidate_id"], b["candidate_id"])
        self.assertEqual(len(list(self.cache.iterdir())), 2)

    def test_cli_private_outputs_no_overwrite_and_mismatch_exit(self):
        source = self.root / "fixture.package"
        actual = self.root / "actual.package"
        source.write_bytes(archive())
        actual.write_bytes(archive([(OTHER, b"A", 1, 0)]))
        output = self.root / "local/inventory.json"
        with patch.object(content, "REPO", self.root), contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(content.main(["inspect", str(source), "--output", str(output)]), 0)
            self.assertEqual(content.main(["inspect", str(source), "--output", str(output)]), 2)
            self.assertEqual(content.main(["inspect", str(source), "--output", str(self.root / "public.json")]), 2)
            self.assertEqual(content.main(["compare", str(source), str(actual), "--output",
                                           str(self.root / "local/comparison.json")]), 20)
        self.assertFalse((self.root / "public.json").exists())
        self.assertEqual(source.read_bytes(), archive())

    def test_read_limit_and_links(self):
        source = self.root / "source.package"
        source.write_bytes(archive())
        self.assert_error("size_limit", content.read_bounded, source, 8)
        self.assert_error("unsafe_path", content.safe_path, str(source) + ":payload")
        link = self.root / "link.package"
        try:
            os.symlink(source, link)
        except OSError:
            self.skipTest("OS symlink privilege unavailable; reparse fixture covered separately")
        self.assert_error("unsafe_path", content.read_bounded, link)

    def test_reparse_attribute_on_ancestor_refused(self):
        real_lstat = Path.lstat
        def reparse(path):
            if path == self.root:
                class Metadata:
                    st_mode = 0
                    st_file_attributes = 0x400
                return Metadata()
            return real_lstat(path)
        with patch.object(Path, "lstat", reparse):
            self.assert_error("unsafe_path", content.safe_path, self.root / "child/file")

    def test_file_changed_during_read_refused(self):
        source = self.root / "changing.package"
        source.write_bytes(archive())
        real_fstat = os.fstat
        calls = 0
        def change_file(fd):
            nonlocal calls
            calls += 1
            if calls == 2:
                with source.open("ab") as stream:
                    stream.write(b"concurrent fixture change")
            return real_fstat(fd)
        with patch.object(content.os, "fstat", change_file):
            self.assert_error("source_changed", content.read_bounded, source)

    def test_windows_handle_and_path_ctime_semantics_can_differ(self):
        source = self.root / "stable.package"
        source.write_bytes(archive())
        real_fstat = os.fstat
        def different_ctime(fd):
            s = real_fstat(fd)
            return SimpleNamespace(st_dev=s.st_dev, st_ino=s.st_ino, st_size=s.st_size,
                                   st_mtime_ns=s.st_mtime_ns, st_ctime_ns=123456789)
        with patch.object(content.os, "fstat", different_ctime):
            self.assertEqual(content.read_bounded(source), archive())


class CopiedTreeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.before = self.root / "local/before"
        self.after = self.root / "local/after"
        self.before.mkdir(parents=True)
        self.after.mkdir()
        self.repo_patch = patch.object(content, "REPO", self.root)
        self.repo_patch.start()
        self.addCleanup(self.repo_patch.stop)

    def compare(self):
        return content.compare_trees(content.inspect_tree(self.before), content.inspect_tree(self.after))

    def test_opaque_files_and_archive_repacking_have_distinct_results(self):
        (self.before / "EditorSaves.package").write_bytes(archive())
        (self.after / "EditorSaves.package").write_bytes(archive(big=True, padding=b"unused"))
        (self.before / "removed.png").write_bytes(b"opaque old bytes")
        (self.after / "new.png").write_bytes(b"opaque new bytes")
        report = self.compare()
        self.assertFalse(report["files_equal"])
        self.assertEqual(report["added_files"], ["new.png"])
        self.assertEqual(report["removed_files"], ["removed.png"])
        self.assertEqual(report["changed_files"], ["EditorSaves.package"])
        self.assertTrue(report["archive_changes"][0]["records_equal"])
        self.assertEqual(report["dependency_status"], "UNKNOWN")
        self.assertFalse(report["readiness"])

    def test_resource_changes_remain_scoped_to_each_archive(self):
        (self.before / "a.package").write_bytes(archive())
        (self.after / "a.package").write_bytes(archive([(KEY, b"ABCE", 4, 0)]))
        (self.after / "b.package").write_bytes(archive())
        report = self.compare()
        self.assertEqual(report["archive_changes"][0]["changed_keys"], [KEY])
        self.assertEqual(report["archive_changes"][1]["unexpected_keys"], [KEY])
        self.assertFalse(report["archive_changes"][1]["before_is_archive"])
        self.assertEqual(report["native_validation"], "NOT_RUN")

    def test_removed_archive_and_archive_to_opaque_are_explicit(self):
        for name in ("removed.package", "replaced.package"):
            (self.before / name).write_bytes(archive())
        (self.after / "replaced.package").write_bytes(b"opaque")
        for item in self.compare()["archive_changes"]:
            self.assertEqual(item["missing_keys"], [KEY])
            self.assertTrue(item["before_is_archive"])
            self.assertFalse(item["after_is_archive"])

    def test_empty_directory_changes_and_identical_copies(self):
        self.assertTrue(self.compare()["files_equal"])
        (self.after / "empty").mkdir()
        self.assertEqual(self.compare()["added_directories"], ["empty"])
        (self.before / "empty").mkdir()
        for root in (self.before, self.after):
            (root / "empty/same.png").write_bytes(b"same")
        report = self.compare()
        self.assertTrue(report["files_equal"])
        self.assertEqual(report["before"]["file_set_sha256"], report["after"]["file_set_sha256"])

    def test_archive_signature_is_checked_even_with_png_extension(self):
        (self.before / "disguised.png").write_bytes(b"DBPFcorrupt")
        with self.assertRaises(content.ContentError) as caught:
            content.inspect_tree(self.before)
        self.assertEqual(caught.exception.code, "truncated")

    def test_tree_budgets_are_enforced(self):
        (self.before / "one").write_bytes(b"1234")
        (self.before / "two").write_bytes(b"1234")
        for limit, value, error in (("MAX_TREE_FILES", 1, "count_limit"),
                                    ("MAX_TREE_BYTES", 7, "size_limit")):
            with self.subTest(limit=limit), patch.object(content, limit, value), \
                    self.assertRaises(content.ContentError) as caught:
                content.inspect_tree(self.before)
            self.assertEqual(caught.exception.code, error)
        (self.before / "a/b").mkdir(parents=True)
        for limit in ("MAX_TREE_DIRECTORIES", "MAX_TREE_DEPTH"):
            with self.subTest(limit=limit), patch.object(content, limit, 1), \
                    self.assertRaises(content.ContentError) as caught:
                content.inspect_tree(self.before)
            self.assertEqual(caught.exception.code, "count_limit")

    def test_decoded_aggregate_budget_is_independent_of_stored_bytes(self):
        data = archive([(KEY, packed(1028, b"\xe0AAAA\xcc\x00\x03\xfb\xfc"), 1028, 0xFFFF)])
        # Four literal bytes followed by a 1,024-byte overlapping back reference.
        for name in ("one.package", "two.package"):
            (self.before / name).write_bytes(data)
        with patch.object(content, "MAX_TREE_BYTES", 1500), \
                self.assertRaises(content.ContentError) as caught:
            content.inspect_tree(self.before)
        self.assertEqual(caught.exception.code, "size_limit")

    def test_new_file_during_inventory_refuses_report(self):
        (self.before / "one").write_bytes(b"source")
        real_read = content.read_bounded
        def changed_tree(path):
            data = real_read(path)
            (self.before / "two").write_bytes(b"appeared during inventory")
            return data
        with patch.object(content, "read_bounded", changed_tree), \
                self.assertRaises(content.ContentError) as caught:
            content.inspect_tree(self.before)
        self.assertEqual(caught.exception.code, "source_changed")

    def test_cli_protects_sources_and_reports_exits(self):
        output = self.root / "local/result.json"
        def run(before, after, target):
            return content.main(["compare-trees", str(before), str(after), "--output", str(target)])
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(run(self.before, self.after, output), 0)
            original = output.read_bytes()
            self.assertEqual(run(self.before, self.after, output), 2)
            self.assertEqual(output.read_bytes(), original)
            self.assertEqual(run(self.before, self.after, self.before / "output.json"), 2)
            self.assertEqual(run(self.root, self.after, self.root / "local/outside.json"), 2)
            (self.after / "added.png").write_bytes(b"fixture")
            self.assertEqual(run(self.before, self.after, self.root / "local/different.json"), 20)
        self.assertFalse((self.before / "output.json").exists())


if __name__ == "__main__":
    unittest.main()
