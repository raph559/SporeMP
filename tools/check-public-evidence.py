"""Keep public Git evidence limited to explicitly reviewed, compact reports."""
import argparse
from pathlib import Path, PurePosixPath
import subprocess
import sys


ALLOWLIST = "docs/public-evidence-files.txt"
MAX_FILE_BYTES = 64 * 1024
MAX_TOTAL_BYTES = 2 * 1024 * 1024
ALLOWED_SUFFIXES = {".md", ".json"}


def valid_evidence_path(value):
    parts = value.split("/")
    return (len(parts) > 1 and parts[0] == "evidence"
            and not any(part in ("", ".", "..") for part in parts)
            and not any(character in value for character in "\\:\0"))


def check(repo):
    repo = Path(repo).resolve()
    errors = []
    try:
        output = subprocess.run(
            ["git", "-C", str(repo), "ls-files", "-z"],
            check=True, capture_output=True,
        ).stdout
        tracked = set(output.decode("utf-8", errors="surrogateescape").split("\0")) - {""}
    except (OSError, subprocess.CalledProcessError) as error:
        return [f"Cannot list tracked files: {error}"], 0, 0

    try:
        lines = (repo / ALLOWLIST).read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        return [f"Cannot read {ALLOWLIST}: {error}"], 0, 0

    allowed = set()
    for number, line in enumerate(lines, start=1):
        path = line.strip()
        if not path or path.startswith("#"):
            continue
        if not valid_evidence_path(path):
            errors.append(f"{ALLOWLIST}:{number}: invalid evidence path: {path}")
        elif path in allowed:
            errors.append(f"{ALLOWLIST}:{number}: duplicate path: {path}")
        else:
            allowed.add(path)

    evidence = {path for path in tracked if path == "evidence" or path.startswith("evidence/")}
    for path in sorted(allowed - evidence):
        errors.append(f"Allowlisted evidence is not tracked: {path}")
    for path in sorted(evidence - allowed):
        errors.append(f"Tracked evidence is not allowlisted: {path}")

    total = 0
    for path in sorted(evidence):
        if not valid_evidence_path(path):
            errors.append(f"Invalid tracked evidence path: {path}")
            continue
        if PurePosixPath(path).suffix not in ALLOWED_SUFFIXES:
            errors.append(f"Evidence must be .md or .json: {path}")
        file = repo / path
        if file.is_symlink() or not file.resolve().is_relative_to(repo / "evidence"):
            errors.append(f"Evidence must be a regular file within evidence/: {path}")
            continue
        if not file.is_file():
            errors.append(f"Tracked evidence file is missing: {path}")
            continue
        size = file.stat().st_size
        total += size
        if size > MAX_FILE_BYTES:
            errors.append(f"Evidence exceeds {MAX_FILE_BYTES} bytes: {path} ({size} bytes)")
    if total > MAX_TOTAL_BYTES:
        errors.append(f"Evidence exceeds total limit of {MAX_TOTAL_BYTES} bytes: {total} bytes")
    return errors, len(evidence), total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    errors, count, total = check(args.repo)
    if errors:
        print("Public evidence check failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print(f"Public evidence check passed: {count} reviewed files, {total} bytes.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
