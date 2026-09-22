"""M08 offline DBPF/DBBF inventory and private quarantine, never native import.

The manifest identifies decoded records, not a verified creation/dependency graph.
No engine calls, archive extraction paths, resource overrides, or network service.
Format provenance and supported limits: docs/m08-content.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys
import uuid
import zlib

REPO = Path(__file__).resolve().parents[2]
MAX_ARCHIVE = 128 * 1024 * 1024
MAX_RECORD = 32 * 1024 * 1024
MAX_EXPANDED = 256 * 1024 * 1024
MAX_RECORDS = 16384
MAX_SELECTION = 128
MAX_TREE_FILES = 512
MAX_TREE_DIRECTORIES = 512
MAX_TREE_DEPTH = 16
MAX_TREE_BYTES = 512 * 1024 * 1024
MAX_CREATION_PNG = 4 * 1024 * 1024
KEY_PATTERN = re.compile(r"[0-9a-f]{8}![0-9a-f]{8}\.[0-9a-f]{8}\Z")


class ContentError(ValueError):
    def __init__(self, code: str, detail: str):
        self.code = code
        super().__init__(f"{code}: {detail}")


def require(condition, code, detail):
    if not condition:
        raise ContentError(code, detail)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inspect_creation_png(data):
    """Bound the original RGBA8 container before any native Model-in-Picture call.

    This validates CRCs, dimensions, exact zlib output length and PNG row filters.
    It does not decode or authorize the creation hidden in the image pixels.
    """
    require(isinstance(data, bytes) and 57 <= len(data) <= MAX_CREATION_PNG,
            "png_size_limit", "creation PNG exceeds the qualified input budget")
    require(data[:8] == b"\x89PNG\r\n\x1a\n", "png_signature", "PNG signature required")
    offset, chunks, idat = 8, 0, bytearray()
    width = height = 0
    ended = False
    while offset < len(data):
        chunks += 1
        require(chunks <= 128 and len(data) - offset >= 12, "png_chunk_bounds", "truncated or too many chunks")
        length = struct.unpack_from(">I", data, offset)[0]
        require(length <= len(data) - offset - 12, "png_chunk_bounds", "chunk exceeds input")
        kind, body = data[offset+4:offset+8], data[offset+8:offset+8+length]
        crc = struct.unpack_from(">I", data, offset+8+length)[0]
        require(zlib.crc32(kind+body) == crc, "png_crc", "chunk checksum mismatch")
        if chunks == 1:
            require(kind == b"IHDR" and length == 13, "png_header", "exact IHDR required first")
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", body)
            require(0 < width <= 512 and 0 < height <= 512 and
                    (depth, color, compression, filtering, interlace) == (8, 6, 0, 0, 0),
                    "png_format_unqualified", "only bounded noninterlaced RGBA8 is qualified")
        elif kind == b"IDAT":
            require(length, "png_empty_pixels", "empty IDAT refused")
            idat.extend(body)
        elif kind == b"IEND":
            require(not length and idat and offset+12 == len(data),
                    "png_end_or_trailing_bytes", "IEND requires pixels and exact input consumption")
            ended = True
            break
        else:
            raise ContentError("png_chunk_unqualified", "only original IHDR/IDAT/IEND chunks are admitted")
        offset += 12+length
    require(ended, "png_missing_end", "IEND required")
    expected = (1+4*width)*height
    try:
        inflater = zlib.decompressobj()
        pixels = inflater.decompress(bytes(idat), expected+1)
        require(len(pixels) == expected and inflater.eof and not inflater.unused_data and not inflater.unconsumed_tail,
                "png_pixels", "decoded pixels have an incorrect size or trailing compressed data")
    except zlib.error as error:
        raise ContentError("png_pixels", "invalid compressed pixel stream") from error
    require(all(pixels[row*(1+4*width)] <= 4 for row in range(height)),
            "png_filter", "unsupported row filter")
    return dict(sha256=digest(data), size=len(data), width=width, height=height,
                format="RGBA8", native_creation_validation="NOT_RUN", transfer_approved=False)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii")


def safe_path(path):
    """Local operator paths only; reject existing links/junctions and ADS syntax."""
    path = Path(os.path.abspath(path))
    for item in [*reversed(path.parents), path]:
        if item != Path(item.anchor):
            require(":" not in item.name, "unsafe_path", "alternate data stream refused")
        try:
            metadata = item.lstat()
        except FileNotFoundError:
            continue
        require(not stat.S_ISLNK(metadata.st_mode) and
                not getattr(metadata, "st_file_attributes", 0) & 0x400,
                "unsafe_path", "symlink or reparse point refused")
    return path


def private_output(path):
    path = safe_path(path)
    root = safe_path(REPO / "local")
    require(root in path.parents, "unsafe_output", "output must be inside repository local/")
    return path


def read_bounded(path, limit=MAX_ARCHIVE):
    path = safe_path(path)
    require(stat.S_ISREG(path.stat().st_mode), "unsafe_path", "regular file required")
    with path.open("rb") as stream:
        before = os.fstat(stream.fileno())
        require(before.st_size <= limit, "size_limit", "file exceeds byte budget")
        data = stream.read(limit + 1)
        after = os.fstat(stream.fileno())
    current = safe_path(path).stat()
    # On this Windows Python 3.14 runtime fstat ctime is change time while stat
    # ctime is birth time. Compare ctime only between the two handle queries.
    identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns)
    require(len(data) == before.st_size and identity(before) == identity(after) == identity(current)
            and before.st_ctime_ns == after.st_ctime_ns,
            "source_changed", "file changed during read")
    return data


class Cursor:
    def __init__(self, data):
        self.data = memoryview(data)
        self.offset = 0

    def take(self, count):
        require(count >= 0 and count <= len(self.data) - self.offset,
                "truncated", "read extends beyond declared record")
        result = self.data[self.offset:self.offset + count]
        self.offset += count
        return result

    def number(self, count=4):
        return int.from_bytes(self.take(count), "little")


def refpack(data, expected):
    """Bounded 10FB/50FB decoder with an explicit end marker and exact consumption."""
    require(0 <= expected <= MAX_RECORD, "size_limit", "expanded record exceeds byte budget")
    cursor = Cursor(data)
    signature = bytes(cursor.take(2))
    require(signature in (b"\x10\xfb", b"\x50\xfb"),
            "unsupported_compression", "expected a qualified 10FB/50FB header")
    require(int.from_bytes(cursor.take(3), "big") == expected,
            "expanded_size_mismatch", "RefPack header and index disagree")
    output = bytearray()
    while True:
        command = cursor.number(1)
        copied = distance = 0
        if command >= 0xFC:
            plain = command & 3
        elif command >= 0xE0:
            plain = ((command & 0x1F) + 1) * 4
        elif command >= 0xC0:
            a, b, c = cursor.take(3)
            plain = command & 3
            copied = ((command & 0x0C) << 6) + c + 5
            distance = ((command & 0x10) << 12) + (a << 8) + b + 1
        elif command >= 0x80:
            a, b = cursor.take(2)
            plain = a >> 6
            copied = (command & 0x3F) + 4
            distance = ((a & 0x3F) << 8) + b + 1
        else:
            a = cursor.number(1)
            plain = command & 3
            copied = ((command & 0x1C) >> 2) + 3
            distance = ((command & 0x60) << 3) + a + 1
        require(len(output) + plain + copied <= expected,
                "expanded_size_mismatch", "RefPack command exceeds declared size")
        output.extend(cursor.take(plain))
        if copied:
            require(0 < distance <= len(output), "invalid_back_reference", "RefPack offset precedes output")
            # A repeated slice also handles overlapping LZ matches without quadratic copies.
            start = len(output) - distance
            pattern = output[start:start + min(distance, copied)]
            output.extend((pattern * ((copied + distance - 1) // distance))[:copied])
        if command >= 0xFC:
            require(len(output) == expected and cursor.offset == len(cursor.data),
                    "expanded_size_mismatch", "RefPack end/length/trailing bytes disagree")
            return bytes(output)


def key_text(group, instance, kind):
    return f"{group:08x}!{instance:08x}.{kind:08x}"


def parse_archive(data):
    """Return validated index entries. No payload is decoded before aggregate checks."""
    require(len(data) <= MAX_ARCHIVE, "size_limit", "archive exceeds byte budget")
    require(len(data) >= 12, "truncated", "archive header missing")
    magic = data[:4]
    require(magic in (b"DBPF", b"DBBF"), "unsupported_container", "expected DBPF or DBBF")
    big = magic == b"DBBF"
    header_size = 120 if big else 96
    require(len(data) >= header_size, "truncated", "archive header incomplete")
    word = lambda offset: struct.unpack_from("<I", data, offset)[0]
    require((word(4), word(8)) == ((2, 0) if big else (3, 0)),
            "unsupported_version", "only DBPF 3.0 and DBBF 2.0 are qualified for inspection")
    require(word(32) == 0 and word(52 if big else 60) == 3,
            "unsupported_index_version", "expected index 0.3")
    count = word(36)
    index_size = struct.unpack_from("<Q", data, 40)[0] if big else word(44)
    index_offset = struct.unpack_from("<Q", data, 56)[0] if big else word(64)
    require(count <= MAX_RECORDS, "count_limit", "too many indexed records")
    if count == index_size == index_offset == 0:
        return []
    require(index_offset >= header_size and 4 <= index_size <= len(data) - index_offset,
            "invalid_index_span", "index overlaps header or exceeds file")
    index = Cursor(memoryview(data)[index_offset:index_offset + index_size])
    flags = index.number()
    # Spore uses a shared zero word (bit 2); per-entry/nonzero variants are unqualified.
    require(flags in (4, 5, 6, 7), "unsupported_index_flags", "shared zero word required")
    shared_type = index.number() if flags & 1 else None
    shared_group = index.number() if flags & 2 else None
    require(index.number() == 0, "unsupported_index_flags", "nonzero shared index word")
    width = (32 if big else 28) - 4 * bool(flags & 1) - 4 * bool(flags & 2)
    require(index.offset + width * count == index_size,
            "invalid_index_size", "count and entry widths do not match index size")
    entries, keys = [], set()
    spans = [(0, header_size), (index_offset, index_offset + index_size)]
    expanded = 0
    for _ in range(count):
        kind = shared_type if shared_type is not None else index.number()
        group = shared_group if shared_group is not None else index.number()
        instance = index.number()
        offset = index.number(8 if big else 4)
        stored = index.number() & 0x7FFFFFFF
        size = index.number()
        compression = index.number(2)
        saved, padding = index.take(2)
        key = key_text(group, instance, kind)
        require(key not in keys, "duplicate_key", key)
        keys.add(key)
        require(compression in (0, 0xFFFF), "unsupported_compression", key)
        require(saved == 1 and padding == 0, "unsupported_record_flags", key)
        require(size <= MAX_RECORD and stored <= MAX_RECORD, "size_limit", key)
        expanded += size
        require(expanded <= MAX_EXPANDED, "size_limit", "aggregate expanded records exceed budget")
        require(offset >= header_size and offset <= len(data) and stored <= len(data) - offset,
                "invalid_record_span", key)
        require(compression != 0 or size == stored, "expanded_size_mismatch", key)
        if stored:
            spans.append((offset, offset + stored))
        entries.append(dict(key=key, type_id=kind, group_id=group, instance_id=instance,
                            offset=offset, stored_size=stored, size=size, compression=compression))
    spans.sort()
    require(all(a[1] <= b[0] for a, b in zip(spans, spans[1:])),
            "overlapping_records", "active records, header, and index must not overlap")
    return entries


def record_bytes(data, entry):
    payload = data[entry["offset"]:entry["offset"] + entry["stored_size"]]
    return refpack(payload, entry["size"]) if entry["compression"] else payload


def inspect_bytes(data):
    entries = parse_archive(data)
    records = []
    for entry in entries:
        try:
            payload = record_bytes(data, entry)
        except ContentError as error:
            raise ContentError(error.code, entry["key"] + ": " + str(error)) from error
        records.append(dict(key=entry["key"], size=len(payload), sha256=digest(payload)))
    records.sort(key=lambda entry: entry["key"])
    return dict(schema_version=1, kind="spore-record-inventory", evidence_class="HOST",
                archive_sha256=digest(data), archive_size=len(data),
                container=data[:4].decode("ascii"), records=records, record_count=len(records),
                compressed_record_count=sum(entry["compression"] != 0 for entry in entries),
                expanded_size=sum(entry["size"] for entry in entries),
                record_set_sha256=digest(canonical(records)),
                dependency_status="UNKNOWN", native_validation="NOT_RUN", readiness=False)


def compare(expected, actual):
    left = {entry["key"]: entry for entry in expected["records"]}
    right = {entry["key"]: entry for entry in actual["records"]}
    return dict(kind="spore-record-comparison", evidence_class="HOST",
                records_equal=left == right,
                missing_keys=sorted(left.keys() - right.keys()),
                unexpected_keys=sorted(right.keys() - left.keys()),
                changed_keys=sorted(key for key in left.keys() & right.keys() if left[key] != right[key]),
                native_validation="NOT_RUN", readiness=False)


def tree_listing(root):
    """Bound a private copied tree before reading bytes; never follow reparse points."""
    root = private_output(root)
    require(root.is_dir(), "unsafe_path", "copied directory required")
    files, directories = {}, []
    total = 0
    def walk_error(error):
        raise error
    for parent, children, names in os.walk(root, followlinks=False, onerror=walk_error):
        for name in children:
            path = safe_path(Path(parent) / name)
            relative = path.relative_to(root).as_posix()
            require(len(path.relative_to(root).parts) <= MAX_TREE_DEPTH,
                    "count_limit", "copied tree exceeds depth budget")
            directories.append(relative)
            require(len(directories) <= MAX_TREE_DIRECTORIES,
                    "count_limit", "copied tree exceeds directory budget")
        for name in names:
            path = safe_path(Path(parent) / name)
            metadata = path.stat()
            require(stat.S_ISREG(metadata.st_mode), "unsafe_path", "regular file required")
            require(metadata.st_size <= MAX_ARCHIVE, "size_limit", "copied file exceeds byte budget")
            total += metadata.st_size
            require(total <= MAX_TREE_BYTES, "size_limit", "copied tree exceeds byte budget")
            relative = path.relative_to(root).as_posix()
            files[relative] = (metadata.st_dev, metadata.st_ino, metadata.st_size, metadata.st_mtime_ns)
            require(len(files) <= MAX_TREE_FILES, "count_limit", "copied tree exceeds file budget")
    names = [*directories, *files]
    require(len({name.casefold() for name in names}) == len(names),
            "duplicate_path", "case-insensitive path collision")
    return files, sorted(directories)


def inspect_tree(root):
    """Hash every file in an operator-supplied closed copy; decode archive signatures.

    This does not create a snapshot or establish that the original game was closed.
    No filename, PNG, or changed resource is inferred to be a creation dependency.
    """
    root = private_output(root)
    initial = tree_listing(root)
    files, archives = [], {}
    expanded = 0
    for relative in sorted(initial[0]):
        data = read_bounded(root / relative)
        files.append(dict(path=relative, size=len(data), sha256=digest(data)))
        if data[:4] in (b"DBPF", b"DBBF"):
            expanded += sum(entry["size"] for entry in parse_archive(data))
            require(expanded <= MAX_TREE_BYTES, "size_limit", "decoded tree exceeds byte budget")
            archives[relative] = inspect_bytes(data)
    require(tree_listing(root) == initial, "source_changed", "copied tree changed during inventory")
    return dict(files=files, directories=initial[1], archives=archives,
                file_set_sha256=digest(canonical(dict(files=files, directories=initial[1]))))


def compare_trees(expected, actual):
    """Keep byte changes separate from decoded-resource changes and opaque files."""
    left = {entry["path"]: entry for entry in expected["files"]}
    right = {entry["path"]: entry for entry in actual["files"]}
    added = sorted(right.keys() - left.keys())
    removed = sorted(left.keys() - right.keys())
    changed = sorted(path for path in left.keys() & right.keys() if left[path] != right[path])
    archive_changes = []
    for path in sorted(set(added + removed + changed)):
        before = expected["archives"].get(path)
        after = actual["archives"].get(path)
        if before is not None or after is not None:
            archive_changes.append(dict(path=path, before_is_archive=before is not None,
                                        after_is_archive=after is not None,
                                        **compare(before or {"records": []}, after or {"records": []})))
    return dict(schema_version=1, kind="spore-copied-tree-comparison", evidence_class="HOST",
                files_equal=left == right and expected["directories"] == actual["directories"],
                added_files=added, removed_files=removed, changed_files=changed,
                added_directories=sorted(set(actual["directories"]) - set(expected["directories"])),
                removed_directories=sorted(set(expected["directories"]) - set(actual["directories"])),
                archive_changes=archive_changes, before=expected, after=actual,
                dependency_status="UNKNOWN", native_validation="NOT_RUN", readiness=False)


def write_new(path, data):
    path = safe_path(path)
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def quarantine(data, selected_keys, cache_root):
    """Keep an explicitly selected candidate in a local cache; it cannot grant readiness.

    The cache root must be private to the operator. Same-user concurrent path replacement
    is outside this offline tool's threat model; it is not an Internet import boundary.
    """
    require(0 < len(selected_keys) <= MAX_SELECTION, "selection_limit", "select 1..128 records")
    require(all(isinstance(key, str) and KEY_PATTERN.fullmatch(key) for key in selected_keys),
            "invalid_key", "use lowercase group!instance.type, eight hex digits per component")
    require(len(set(selected_keys)) == len(selected_keys), "duplicate_selection", "select each key once")
    inventory = inspect_bytes(data)
    indexed = {entry["key"]: entry for entry in parse_archive(data)}
    for key in selected_keys:
        require(key in indexed, "missing_record", key)
    records = [entry for entry in inventory["records"] if entry["key"] in selected_keys]
    manifest = dict(schema_version=1, kind="spore-quarantined-record-candidate", records=records,
                    dependency_status="UNKNOWN", native_validation="NOT_RUN",
                    transfer_approved=False, readiness=False)
    manifest_bytes = canonical(manifest)
    candidate_id = digest(manifest_bytes)
    cache_root = safe_path(cache_root)
    cache_root.mkdir(parents=True, exist_ok=True)
    final = safe_path(cache_root / candidate_id)

    def verify():
        require(read_bounded(final / "manifest.json", 1024 * 1024) == manifest_bytes,
                "cache_corrupt", "candidate manifest differs")
        expected_names = {"manifest.json"} | {entry["sha256"] + ".blob" for entry in records}
        require({item.name for item in final.iterdir()} == expected_names,
                "cache_corrupt", "missing or unexpected cache files")
        for entry in records:
            blob = read_bounded(final / (entry["sha256"] + ".blob"), MAX_RECORD)
            require(len(blob) == entry["size"] and digest(blob) == entry["sha256"],
                    "cache_corrupt", entry["key"])

    if final.exists():
        verify()
    else:
        stage = safe_path(cache_root / (".incomplete-" + uuid.uuid4().hex))
        stage.mkdir()
        # An interrupted operation deliberately leaves an uncommitted private directory.
        written = set()
        for entry in records:
            if entry["sha256"] not in written:
                write_new(stage / (entry["sha256"] + ".blob"), record_bytes(data, indexed[entry["key"]]))
                written.add(entry["sha256"])
        write_new(stage / "manifest.json", manifest_bytes)
        try:
            stage.rename(final)
        except FileExistsError:
            # Another writer won. Verify it; never replace it or delete someone else's work.
            verify()
        verify()
    return dict(candidate_id=candidate_id, records=len(records),
                source_archive_sha256=inventory["archive_sha256"],
                native_validation="NOT_RUN", transfer_approved=False, readiness=False)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    inspect_parser = sub.add_parser("inspect", help="hash and validate every active package record")
    inspect_parser.add_argument("archive", type=Path)
    compare_parser = sub.add_parser("compare", help="compare decoded resource sets of two archives")
    compare_parser.add_argument("archive", type=Path)
    compare_parser.add_argument("actual", type=Path)
    cache_parser = sub.add_parser("quarantine", help="store explicitly selected unapproved records privately")
    cache_parser.add_argument("archive", type=Path)
    cache_parser.add_argument("--key", action="append", required=True)
    tree_parser = sub.add_parser("compare-trees", help="compare closed copied trees under local/, including PNGs and archives")
    tree_parser.add_argument("archive", type=Path, help="before copied directory under local/")
    tree_parser.add_argument("actual", type=Path, help="after copied directory under local/")
    for command in (inspect_parser, compare_parser, cache_parser, tree_parser):
        command.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        output = private_output(args.output)
        require(not output.exists(), "output_exists", "use a fresh report path")
        if args.command == "compare-trees":
            before_root, after_root = private_output(args.archive), private_output(args.actual)
            require(all(root != output and root not in output.parents for root in (before_root, after_root)),
                    "unsafe_output", "report must be outside both copied trees")
            result = compare_trees(inspect_tree(before_root), inspect_tree(after_root))
        else:
            data = read_bounded(args.archive)
            if args.command == "quarantine":
                result = quarantine(data, args.key, private_output(REPO / "local/m08-content/cache"))
            else:
                result = inspect_bytes(data)
                if args.command == "compare":
                    result = compare(result, inspect_bytes(read_bounded(args.actual)))
        output.parent.mkdir(parents=True, exist_ok=True)
        write_new(output, canonical(result) + b"\n")
        print(json.dumps({key: value for key, value in result.items() if key not in ("records", "before", "after")}, sort_keys=True))
        return 20 if (args.command == "compare" and not result["records_equal"] or
                      args.command == "compare-trees" and not result["files_equal"]) else 0
    except (ContentError, OSError) as error:
        print(json.dumps(dict(error=getattr(error, "code", "io_error"), detail=str(error),
                              native_validation="NOT_RUN", readiness=False)), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
