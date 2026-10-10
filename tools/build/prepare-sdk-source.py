"""Verify the pinned SDK and one reviewed patch in a private build-source copy.

The external checkout is read only. Every cached source byte is compared with
the pinned git archive plus the exact patched target before a build may use it.
"""
from __future__ import annotations

import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import stat
import subprocess
import sys
import zipfile


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def git(sdk: Path, *args: str) -> bytes:
    # Pin archive text bytes independently of the developer's Git EOL settings.
    result = subprocess.run(["git", "-c", "core.autocrlf=false", "-c", "core.eol=lf", "-C", str(sdk), *args], capture_output=True)
    if result.returncode:
        raise ValueError("SDK git command failed: " + " ".join(args[:2]))
    return result.stdout


def no_links(path: Path) -> Path:
    path = path.absolute()
    for parent in (path, *path.parents):
        if parent.exists() or parent.is_symlink():
            info = parent.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                raise ValueError("SDK build-source reparse path refused")
    return path


def prepare(repo: Path) -> Path:
    repo = no_links(repo)
    lock = json.loads((repo / "config/dependencies.lock.json").read_text(encoding="utf-8"))["modapi"]
    sdk = no_links(repo / "external/Spore-ModAPI")
    commit = lock["commit"]
    if git(sdk, "rev-parse", "HEAD").decode().strip() != commit:
        raise ValueError("SDK revision differs from the dependency lock")
    if git(sdk, "status", "--porcelain", "--untracked-files=all").strip():
        raise ValueError("Pinned SDK checkout has modifications or untracked files")
    patch = lock["source_patch"]
    patch_relative = PurePosixPath(patch["path"])
    if patch_relative.is_absolute() or ".." in patch_relative.parts:
        raise ValueError("SDK patch path is outside the repository")
    patch_path = no_links(repo / patch_relative)
    if sha(patch_path.read_bytes()) != patch["sha256"]:
        raise ValueError("SDK patch hash differs from the dependency lock")
    target = patch["target"]
    members: dict[str, bytes] = {}
    with zipfile.ZipFile(io.BytesIO(git(sdk, "archive", "--format=zip", commit))) as archive:
        for entry in archive.infolist():
            path = PurePosixPath(entry.filename)
            if path.is_absolute() or ".." in path.parts or stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError("Unsafe path in pinned SDK archive")
            if not entry.is_dir():
                if entry.filename in members:
                    raise ValueError("Duplicate SDK archive member")
                members[entry.filename] = archive.read(entry)
    if target not in members or sha(members[target]) != patch["before_sha256"]:
        raise ValueError("SDK patch target does not match its pinned before hash")
    expected = {name: sha(data) for name, data in members.items()}
    expected[target] = patch["after_sha256"]
    identity = {"schema": 1, "sdk_commit": commit, "patch_sha256": patch["sha256"], "files": expected}
    # The full identities in source.json detect any shortened-key collision.
    cache = no_links(repo / "build/sdk-source" / patch["sha256"][:16])
    source = cache / "source"
    metadata = cache / "source.json"
    if cache.exists():
        if not metadata.is_file() or json.loads(metadata.read_text(encoding="utf-8")) != identity:
            raise ValueError("SDK build-source cache has an unknown or incomplete patch state")
    else:
        source.mkdir(parents=True)
        for name, data in members.items():
            output = source / name
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(data)
        environment = dict(os.environ, GIT_CEILING_DIRECTORIES=str(source.parent))
        # This archive is nested beneath SporeMP's repository. Stop discovery
        # above the source copy so git apply cannot treat paths as root-relative
        # and silently skip a patch outside its inherited repository prefix.
        apply = ["git", "-c", "core.autocrlf=false", "-c", "core.eol=lf", "apply", "--no-index"]
        result = subprocess.run([*apply, "--check", "--whitespace=error-all", str(patch_path)], cwd=source, env=environment, capture_output=True)
        if result.returncode:
            raise ValueError("Reviewed SDK patch does not apply exactly: " + result.stderr.decode(errors="replace").strip())
        result = subprocess.run([*apply, "--whitespace=error-all", str(patch_path)], cwd=source, env=environment, capture_output=True)
        if result.returncode:
            raise ValueError("Reviewed SDK patch application failed")
    actual = {}
    for path in source.rglob("*"):
        no_links(path)
        if path.is_file():
            actual[path.relative_to(source).as_posix()] = sha(path.read_bytes())
    if actual != expected:
        raise ValueError("SDK build-source bytes differ from the pinned archive and reviewed patch")
    if not metadata.exists():
        with metadata.open("x", encoding="utf-8", newline="\n") as output:
            json.dump(identity, output, sort_keys=True, indent=2)
            output.write("\n")
    return source


if __name__ == "__main__":
    try:
        if len(sys.argv) != 2:
            raise ValueError("usage: prepare-sdk-source.py REPOSITORY")
        print(prepare(Path(sys.argv[1])))
    except (ValueError, OSError, KeyError, json.JSONDecodeError, zipfile.BadZipFile) as error:
        print("SDK source verification failed: " + str(error), file=sys.stderr)
        raise SystemExit(1)
