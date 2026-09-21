"""Check MSVC build inputs for Detours 4; this is not native game acceptance.

The manifest prevents accidentally reusing stale SDK artifacts. It is local build
provenance, not a signed attestation or a license audit of arbitrary binaries.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import ntpath
from pathlib import Path
import subprocess
import sys


SCHEMA_VERSION = 1
EVIDENCE_KIND = "BUILD_TLOG_PROVENANCE_NOT_NATIVE_ACCEPTANCE"
DEPENDENCIES = {
    "modapi": "external/Spore-ModAPI",
    "native_loader": "external/ModAPI-Launcher-Kit",
}
DETOURS_HEADER = "external/ModAPI-Launcher-Kit/ModAPI.DLLInjector/Detours/src/detours.h"


class ProvenanceError(ValueError):
    """A build input or stored artifact does not meet the pinned configuration."""


def manifest_path(repo, configuration):
    return repo / "build/sdk" / configuration / "detours-provenance.json"


def artifact_paths(configuration):
    return [
        f"build/sdk/{configuration}/SporeModAPI.dll",
        f"build/sdk-base/{configuration}/SporeModAPIBase.lib",
        f"build/detours4/{configuration}/sporemp_detours.lib",
        DETOURS_HEADER,
        "config/dependencies.lock.json",
        "tools/build/sdk-detours4.targets",
        "tools/build/detours4-guard.h",
        "tools/build/detours4/CMakeLists.txt",
        "tools/build/build.ps1",
        "tools/build/verify-sdk-detours.py",
    ]


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def dependency_revision(path):
    result = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=False,
    )
    if result.returncode:
        raise ProvenanceError(f"Cannot read pinned dependency revision: {path}")
    return result.stdout.strip()


def current_inputs(repo, configuration):
    lock = json.loads((repo / "config/dependencies.lock.json").read_text(encoding="utf-8"))
    revisions = {}
    for name, relative in DEPENDENCIES.items():
        expected = lock[name]["commit"]
        actual = dependency_revision(repo / relative)
        if actual != expected:
            raise ProvenanceError(f"Pinned {name} revision mismatch: expected {expected}, got {actual}")
        revisions[name] = actual
    hashes = {relative: file_hash(repo / relative) for relative in artifact_paths(configuration)}
    return revisions, hashes


def windows_path(value):
    # MSVC tlogs use Windows absolute paths, even when fixture tests run elsewhere.
    return ntpath.normcase(ntpath.normpath(value.strip().lstrip("^").strip('"')))


def inside(path, root):
    return path == root or path.startswith(root.rstrip("\\") + "\\")


def read_dependencies(directory, prefix):
    logs = sorted(path for path in directory.rglob("*.tlog")
                  if path.name.lower().startswith(prefix.lower()))
    if not logs:
        raise ProvenanceError(f"Missing {prefix}*.tlog build input logs under {directory}")
    dependencies = set()
    for log in logs:
        raw = log.read_bytes()
        try:
            # MSBuild normally writes a BOM; also accept UTF-16 LE without one.
            encoding = "utf-16" if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-16-le"
            content = raw.decode(encoding)
        except UnicodeError as error:
            raise ProvenanceError(f"Invalid UTF-16 build input log: {log}") from error
        for line in content.splitlines():
            # A caret introduces one or more source paths, separated by pipes.
            dependencies.update(windows_path(item) for item in line.split("|") if item.strip())
    if not dependencies:
        raise ProvenanceError(f"Empty {prefix}*.tlog build input logs under {directory}")
    return logs, dependencies


def reject_legacy(repo, dependencies):
    sdk_root = windows_path(str(repo / DEPENDENCIES["modapi"]))
    legacy_root = windows_path(str(repo / DEPENDENCIES["modapi"] / "Detours"))
    for path in sorted(dependencies):
        if (inside(path, legacy_root) or inside(path, r"e:\detours")
                or (inside(path, sdk_root) and "libdetours" in path.split("\\"))
                or ntpath.basename(path) in {"detours.lib", "libdetours.lib"}):
            raise ProvenanceError(f"Legacy or ambiguous Detours dependency in build input log: {path}")


def verify_build_logs(repo, configuration):
    sdk_obj = repo / "build/sdk" / configuration / "obj-detours4"
    base_obj = repo / "build/sdk-base" / configuration / "obj-detours4"
    expected_header = windows_path(str(repo / DETOURS_HEADER))
    expected_library = windows_path(str(repo / f"build/detours4/{configuration}/sporemp_detours.lib"))
    checked_logs = {}
    for name, directory in [("sdk_compile", sdk_obj), ("base_compile", base_obj)]:
        logs, dependencies = read_dependencies(directory, "CL.read.")
        reject_legacy(repo, dependencies)
        if expected_header not in dependencies:
            raise ProvenanceError(f"Pinned Detours 4 header missing from {name} build inputs")
        checked_logs[name] = [path.relative_to(repo).as_posix() for path in logs]
    logs, dependencies = read_dependencies(sdk_obj, "link.read.")
    reject_legacy(repo, dependencies)
    detours_libraries = {path for path in dependencies if ntpath.basename(path) == "sporemp_detours.lib"}
    if detours_libraries != {expected_library}:
        raise ProvenanceError("SDK link inputs must use only the absolute, configuration-specific sporemp_detours.lib")
    checked_logs["sdk_link"] = [path.relative_to(repo).as_posix() for path in logs]
    return checked_logs


def verify(repo, configuration, check=False):
    repo = Path(repo).resolve()
    if configuration not in {"Release", "Debug"}:
        raise ProvenanceError(f"Unsupported configuration: {configuration}")
    target = manifest_path(repo, configuration)
    if check:
        if not target.is_file():
            raise ProvenanceError(f"Missing SDK Detours provenance; rebuild the SDK: {target}")
        manifest = json.loads(target.read_text(encoding="utf-8"))
        if (manifest.get("schema_version") != SCHEMA_VERSION
                or manifest.get("configuration") != configuration
                or manifest.get("evidence_kind") != EVIDENCE_KIND):
            raise ProvenanceError("SDK Detours provenance schema or configuration mismatch; rebuild the SDK")
        revisions, hashes = current_inputs(repo, configuration)
        if manifest.get("dependency_revisions") != revisions:
            raise ProvenanceError("SDK Detours provenance revisions are stale; rebuild the SDK")
        if manifest.get("sha256") != hashes:
            raise ProvenanceError("SDK Detours provenance artifact hashes are stale; rebuild the SDK")
        return target
    checked_logs = verify_build_logs(repo, configuration)
    revisions, hashes = current_inputs(repo, configuration)
    manifest = {
        "schema_version": SCHEMA_VERSION,
        "evidence_kind": EVIDENCE_KIND,
        "configuration": configuration,
        "verified_at_utc": datetime.now(timezone.utc).isoformat(),
        "dependency_revisions": revisions,
        "sha256": hashes,
        "build_input_logs": checked_logs,
    }
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    temporary.replace(target)
    return target


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--configuration", choices=["Release", "Debug"], required=True)
    parser.add_argument("--check", action="store_true", help="Validate stored hashes and pins without reading tlogs")
    arguments = parser.parse_args(argv)
    try:
        target = verify(arguments.repo, arguments.configuration, arguments.check)
    except (ProvenanceError, OSError, ValueError, KeyError, TypeError) as error:
        print(json.dumps({"status": "FAIL", "error": str(error)}), file=sys.stderr)
        return 2
    print(json.dumps({"status": "PASS", "manifest": str(target), "mode": "check" if arguments.check else "record"}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
