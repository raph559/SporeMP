"""M01 read-only compatibility checks and disposable workspace/backup tooling.

No DLL injection, game launch, registry writes, or personal-save restore occurs here.
Exit codes: 0 tooling passed; 2 input/I/O failure; 20 compatibility mismatch;
21 environment-only isolation unproven; 22 native launch prerequisites missing.
"""
from __future__ import annotations

import argparse
import csv
import ctypes
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
from datetime import datetime, timezone
from uuid import uuid4

REPO = Path(__file__).resolve().parents[2]
GAME_ROOTS = ("Data", "DataEP1", "bp1content", "SporebinEP1")
EXE_RELATIVE = "SporebinEP1/SporeApp.exe"


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def no_reparse(path: Path):
    """Reject symlinks/junctions, including existing ancestors of a new path."""
    path = Path(os.path.abspath(path))
    for item in [*reversed(path.parents), path]:
        try:
            metadata = item.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(metadata.st_mode) or getattr(metadata, "st_file_attributes", 0) & 0x400:
            raise ValueError(f"Reparse point/symlink refused: {item}")
    return path


def fingerprint(path: Path):
    path = no_reparse(path)
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise ValueError(f"Not a regular file: {path}")
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
        after = os.fstat(stream.fileno())
    current = path.stat()
    state = lambda s: (s.st_size, s.st_mtime_ns, s.st_ino, s.st_dev)
    if state(before) != state(after) or state(after) != state(current):
        raise ValueError(f"File changed during fingerprint: {path}")
    return {"size": before.st_size, "sha256": digest.hexdigest()}


def pe_identity(path: Path):
    path = no_reparse(path)
    with path.open("rb") as stream:
        size = os.fstat(stream.fileno()).st_size
        header = stream.read(64)
        if len(header) != 64 or header[:2] != b"MZ":
            raise ValueError("Invalid DOS header")
        offset = struct.unpack_from("<I", header, 0x3C)[0]
        if offset < 64 or offset > min(size - 24, 16 * 1024 * 1024):
            raise ValueError("PE header offset out of bounds")
        stream.seek(offset)
        header = stream.read(24)
        if len(header) != 24 or header[:4] != b"PE\0\0":
            raise ValueError("Invalid PE signature")
        machine, sections, timestamp, _, _, optional_size, characteristics = struct.unpack_from("<HHIIIHH", header, 4)
        if not 1 <= sections <= 96 or optional_size < 96 or offset + 24 + optional_size + sections * 40 > size:
            raise ValueError("Truncated or invalid PE headers")
        optional = stream.read(optional_size)
        magic = struct.unpack_from("<H", optional)[0]
        if magic not in (0x10B, 0x20B):
            raise ValueError("Unsupported PE optional header")
    return {**fingerprint(path), "machine": f"0x{machine:04x}", "optional_magic": f"0x{magic:04x}",
            "is_dll": bool(characteristics & 0x2000), "coff_timestamp": timestamp}


def tree_manifest(root: Path):
    root = no_reparse(root)
    if not root.exists():
        return {"exists": False, "directories": [], "files": []}
    if not root.is_dir():
        raise ValueError(f"Directory required: {root}")
    files = []
    directory_names = []
    for parent, directories, names in os.walk(root, followlinks=False):
        for name in directories:
            no_reparse(Path(parent) / name)
            directory_names.append((Path(parent) / name).relative_to(root).as_posix())
        for name in names:
            path = Path(parent) / name
            files.append({"path": path.relative_to(root).as_posix(), **fingerprint(path)})
    files.sort(key=lambda item: item["path"].casefold())
    if len({entry["path"].casefold() for entry in files}) != len(files):
        raise ValueError("Case-insensitive duplicate content paths")
    return {"exists": True, "directories": sorted(directory_names, key=str.casefold), "files": files}


def game_inventory(root: Path):
    root = no_reparse(root)
    return {"schema_version": 1, "profile_id": "gog-ga-3.1.0.29-observed",
            "native_status": "NOT_RUN", "content_provenance": "Local inventory; official clean-content equivalence NOT VERIFIED",
            "executable_relative": EXE_RELATIVE, "executable": pe_identity(root / EXE_RELATIVE),
            "content": {folder: tree_manifest(root / folder) for folder in GAME_ROOTS}}


def compare_candidate(root: Path, candidate: dict):
    if candidate.get("schema_version") != 1 or candidate.get("executable_relative") != EXE_RELATIVE:
        raise ValueError("Invalid candidate schema/executable path")
    if set(candidate.get("content", {})) != set(GAME_ROOTS):
        raise ValueError("Invalid candidate content roots")
    current = game_inventory(root)
    issues = []
    exe = current["executable"]
    if exe["machine"] != "0x014c" or exe["optional_magic"] != "0x010b" or exe["is_dll"]:
        issues.append("EXECUTABLE_NOT_WIN32_APPLICATION")
    if exe != candidate["executable"]:
        issues.append("EXECUTABLE_FINGERPRINT_MISMATCH")
    for folder in GAME_ROOTS:
        if not current["content"][folder]["exists"]:
            issues.append(f"CONTENT_ROOT_MISSING:{folder}")
        if current["content"][folder] != candidate["content"][folder]:
            issues.append(f"CONTENT_MISMATCH:{folder}")
    return {"schema_version": 1, "checked_utc": utc_now(), "candidate_match": not issues,
            "issues": issues, "executable": exe, "native_status": "NOT_RUN", "launch_allowed": False,
            "note": "Matching an observed inventory does not qualify native gameplay or save isolation."}


def write_json(path: Path, value):
    path = no_reparse(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    # Never overwrite prior evidence, a candidate, or a backup manifest implicitly.
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False)
        stream.write("\n")


def game_running():
    if os.name != "nt":
        raise ValueError("Native process quiescence check requires Windows")
    result = subprocess.run(["tasklist.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True, check=True)
    return any(row and row[0].casefold() == "sporeapp.exe" for row in csv.reader(io.StringIO(result.stdout)))


def backup(sources: list[Path], destination: Path):
    if not sources:
        raise ValueError("At least one source required")
    if game_running():
        raise ValueError("SPORE is running; close it normally before a quiescent backup")
    sources = [no_reparse(source) for source in sources]
    destination = no_reparse(destination)
    for source in sources:
        if destination == source or source in destination.parents or destination in source.parents:
            raise ValueError("Backup destination and sources must be disjoint")
    if len(set(sources)) != len(sources):
        raise ValueError("Duplicate backup source")
    before = [tree_manifest(source) for source in sources]
    destination.mkdir(parents=True, exist_ok=False)
    records = []
    # Failure intentionally leaves an INCOMPLETE directory with no commit manifest.
    for index, source in enumerate(sources):
        subfolder = f"source-{index}"
        target = destination / subfolder
        if before[index]["exists"]:
            target.mkdir()
            for relative in before[index]["directories"]:
                (target / relative).mkdir(parents=True, exist_ok=True)
            for entry in before[index]["files"]:
                relative = Path(entry["path"])
                no_reparse(source / relative)
                (target / relative).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source / relative, target / relative, follow_symlinks=False)
            if tree_manifest(target) != before[index]:
                raise ValueError("Backup content verification failed")
        records.append({"source": str(source), "folder": subfolder, "snapshot": before[index]})
    if game_running() or [tree_manifest(source) for source in sources] != before:
        raise ValueError("Source changed or game started during backup; snapshot not committed")
    manifest = {"schema_version": 1, "kind": "quiescent-file-backup", "committed_utc": utc_now(), "sources": records}
    write_json(destination / "backup-manifest.json", manifest)
    return manifest


def verify_backup(destination: Path):
    destination = no_reparse(destination)
    manifest = json.loads((destination / "backup-manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1 or not manifest.get("sources"):
        raise ValueError("Invalid backup manifest")
    for index, record in enumerate(manifest["sources"]):
        if record["folder"] != f"source-{index}":
            raise ValueError("Invalid backup subfolder")
        if tree_manifest(destination / record["folder"]) != record["snapshot"]:
            raise ValueError(f"Backup mismatch: {record['folder']}")
    return {"verified": True, "kind": "file-integrity-only", "sources": len(manifest["sources"]),
            "native_restore_status": "NOT_RUN"}


def create_profile(root: Path, name: str):
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,47}", name) or name.upper() in {"CON", "PRN", "AUX", "NUL", *[f"COM{i}" for i in range(10)], *[f"LPT{i}" for i in range(10)]}:
        raise ValueError("Invalid disposable profile name")
    root = no_reparse(root)
    path = root / name
    path.mkdir(parents=True, exist_ok=False)
    for folder in ("appdata", "localappdata", "documents", "temp", "logs"):
        (path / folder).mkdir()
    profile = {"schema_version": 1, "profile_id": str(uuid4()), "name": name, "root": str(path),
               "created_utc": utc_now(), "kind": "disposable-directory-workspace",
               "native_write_isolation": "NOT_RUN", "launch_allowed": False,
               "note": "Not a Windows user profile. Shell folders/registry/game file access remain unverified."}
    write_json(path / "profile.json", profile)
    return profile


def known_folders():
    if os.name != "nt":
        raise ValueError("Known-folder probe requires Windows")
    function = ctypes.WinDLL("shell32", use_last_error=True).SHGetFolderPathW
    function.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_wchar_p]
    function.restype = ctypes.c_long
    paths = {}
    errors = {}
    for name, csidl in (("appdata", 0x001A), ("localappdata", 0x001C), ("documents", 0x0005)):
        buffer = ctypes.create_unicode_buffer(260)
        result = function(None, csidl, None, 0, buffer)
        if result != 0:
            errors[name] = f"HRESULT {result} (0x{result & 0xffffffff:08x})"
            paths[name] = None
        else:
            paths[name] = buffer.value
    return {"shell_folders": paths, "errors": errors,
            "environment": {key: os.environ.get(key) for key in ("APPDATA", "LOCALAPPDATA", "USERPROFILE")}}


def probe_isolation(profile_path: Path):
    profile = json.loads(no_reparse(profile_path).read_text(encoding="utf-8"))
    root = no_reparse(Path(profile["root"]))
    original = known_folders()
    variants = []
    for replace_userprofile in (False, True):
        env = dict(os.environ, APPDATA=str(root / "appdata"), LOCALAPPDATA=str(root / "localappdata"))
        if replace_userprofile:
            env["USERPROFILE"] = str(root)
        command = [sys.executable, str(Path(__file__).resolve()), "known-folders"]
        result = subprocess.run(command, env=env, capture_output=True, text=True)
        child = json.loads(result.stdout) if result.stdout.strip() else {"error": result.stderr}
        variants.append({"replace_userprofile": replace_userprofile, "command": command,
                         "exit_code": result.returncode, "child": child,
                         "shell_folders_unchanged": child.get("shell_folders") == original["shell_folders"]})
    return {"schema_version": 1, "kind": "host-shell-folder-probe", "utc": utc_now(),
            "parent": original, "environment_variants": variants,
            "native_test": "NOT_RUN", "isolation_verified": False,
            "note": "A host API probe cannot prove SPORE file isolation. Do not launch into a personal profile."}


def preflight(root: Path, candidate: dict, profile_path: Path | None, launcher: Path | None):
    report = compare_candidate(root, candidate)
    blockers = list(report["issues"])
    if launcher is None or not launcher.is_file():
        blockers.append("MODAPI_LAUNCHER_NOT_CONFIGURED")
    else:
        blockers.append("LOADER_AND_CORE_HASHES_NOT_QUALIFIED")
    if profile_path is None:
        blockers.append("DISPOSABLE_OS_OR_NATIVE_PATH_ISOLATION_NOT_CONFIGURED")
    else:
        profile = json.loads(no_reparse(profile_path).read_text(encoding="utf-8"))
        if profile.get("schema_version") != 1:
            raise ValueError("Invalid profile schema")
        # Deliberately no editable boolean bypass. An evidence-backed launch adapter is pending.
        blockers.append("NATIVE_SAVE_CONFIG_ISOLATION_NOT_VERIFIED")
    blockers.append("NATIVE_LIFECYCLE_QUALIFICATION_NOT_RUN")
    return {**report, "blockers": blockers, "result": "NOT_RUN", "launched_processes": 0,
            "next_step": "Qualify the loader and an isolated OS/native path adapter; collect file-access and lifecycle evidence."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("inventory", "validate", "preflight"):
        command = sub.add_parser(name)
        command.add_argument("--game-root", type=Path, required=True)
        command.add_argument("--output", type=Path)
        if name != "inventory":
            command.add_argument("--candidate", type=Path, default=REPO / "config/compatibility.candidate.json")
        if name == "preflight":
            command.add_argument("--profile", type=Path)
            command.add_argument("--launcher", type=Path)
    command = sub.add_parser("backup")
    command.add_argument("--source", type=Path, action="append", required=True)
    command.add_argument("--destination", type=Path, required=True)
    command = sub.add_parser("verify-backup")
    command.add_argument("--destination", type=Path, required=True)
    command = sub.add_parser("new-profile")
    command.add_argument("--root", type=Path, required=True)
    command.add_argument("--name", required=True)
    sub.add_parser("known-folders")
    command = sub.add_parser("probe-isolation")
    command.add_argument("--profile", type=Path, required=True)
    command.add_argument("--output", type=Path)
    args = parser.parse_args()
    code = 0
    try:
        if args.command == "inventory":
            report = game_inventory(args.game_root)
        elif args.command in ("validate", "preflight"):
            candidate = json.loads(args.candidate.read_text(encoding="utf-8"))
            if args.command == "validate":
                report = compare_candidate(args.game_root, candidate)
                code = 0 if report["candidate_match"] else 20
            else:
                report = preflight(args.game_root, candidate, args.profile, args.launcher)
                code = 22 if report["candidate_match"] else 20
        elif args.command == "backup":
            report = backup(args.source, args.destination)
        elif args.command == "verify-backup":
            report = verify_backup(args.destination)
        elif args.command == "new-profile":
            report = create_profile(args.root, args.name)
        elif args.command == "known-folders":
            report = known_folders()
            code = 2 if report["errors"] else 0
        else:
            report = probe_isolation(args.profile)
            code = 21
        if getattr(args, "output", None):
            write_json(args.output, report)
            print(json.dumps({"output": str(args.output), "exit_code": code}))
        else:
            print(json.dumps(report, indent=2))
        return code
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(json.dumps({"error": str(error), "launch_allowed": False}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
