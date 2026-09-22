"""Extract a closed native editor candidate for independent authority validation."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/"tools/content"))
import native_creation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("spore-copy", "closed-run", "config", "output"):
        parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--inspection-request", type=int, required=True)
    parser.add_argument("--version", default="0.0.49")
    args = parser.parse_args()
    try:
        report = native_creation.prepare(args.spore_copy, args.closed_run, args.config, args.key,
            args.inspection_request, args.output, args.version)
        print(json.dumps(dict(source_closed=True, source_key=report["source_key"], png_sha256=report["png"]["sha256"],
            native_parts=report["native_parts"], authority_validation="REQUIRED_NOT_PERFORMED", output=str(args.output))))
        return 0
    except Exception as error:
        print(json.dumps(dict(error=str(error), multiplayer_publication=False)))
        return 2


if __name__ == "__main__":
    sys.exit(main())
