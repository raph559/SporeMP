"""Reuse a verified quiescent backup and compare the personal sources after a native run."""
import argparse
import importlib.util
import json
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("launcher_service", repo / "tools/launcher/launcher_service.py")
service = importlib.util.module_from_spec(spec)
spec.loader.exec_module(service)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("action", choices=("before", "after"))
parser.add_argument("--report", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
if args.action == "before":
    result = service.ensure_backup()
else:
    before = json.loads(args.report.read_text(encoding="utf-8-sig"))
    backup = Path(before["backup_directory"])
    service.diag.verify_backup(backup)
    manifest = json.loads((backup / "backup-manifest.json").read_text(encoding="utf-8"))
    matches = [service.diag.tree_manifest(Path(source["source"])) == source["snapshot"] for source in manifest["sources"]]
    result = {"personal_sources_unchanged": all(matches), "source_matches": matches,
              "backup_directory": str(backup), "file_count": sum(len(x["snapshot"]["files"]) for x in manifest["sources"]),
              "game_running": service.diag.game_running(), "utc": service.diag.utc_now()}
service.diag.write_json(args.output, result)
if args.action == "after" and (not result["personal_sources_unchanged"] or result["game_running"]):
    raise SystemExit(33)
