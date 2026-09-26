"""Inspect the pinned social reward caller in closed actor traces; never certify M11."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "sporemp_actor_context_audit", Path(__file__).with_name("analyze-actor-context.py")
)
_context = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_context)

# Pinned PE32 preferred VA D9BD16 calls original AddEvolutionPoints D2E8A0.
# The return address is D9BD1B; subtract preferred image base 400000.
# Static caller D9BBD0 passes true to C042A0 and uses species relationships.
# This identifies a candidate call path, not a proven social outcome or owner.
SOCIAL_REWARD_CALLER_RVA = 0x99BD1B


def analyze(path):
    path = Path(path)
    context = _context.analyze(path)
    errors = list(context["errors"])
    with path.open("rb") as stream:
        raw = stream.read(_context.MAX_TRACE + 1)
    if len(raw) > _context.MAX_TRACE or hashlib.sha256(raw).hexdigest() != context["sha256"]:
        raise ValueError("Trace changed during analysis or exceeds the probe bound")
    if not context["context_observers_present"]:
        errors.append("paired DNA observer schema is missing")
    observations = []
    # The shared validator checks strict JSON, complete lifecycle, process/thread,
    # pinned native provenance, scene epochs, unique call IDs and matching returns.
    # Do not reduce an invalid trace into apparently usable observations.
    if not errors:
        events = [json.loads(line) for line in raw.decode("utf-8-sig").splitlines()]
        pending = {}
        for row in events:
            if row["event"] == "native_global_dna_enter" and row.get("caller_rva") == SOCIAL_REWARD_CALLER_RVA:
                pending[row["call"]] = row
            elif row["event"] == "native_global_dna_return" and row["call"] in pending:
                entry = pending.pop(row["call"])
                values = (entry.get("amount"), entry.get("before"), row.get("after"))
                if any(type(value) not in (int, float) or not math.isfinite(value) for value in values):
                    errors.append(f"call {row['call']}: missing or non-finite DNA scalar")
                    continue
                avatar = entry.get("avatar")
                if type(avatar) is not int or avatar <= 0:
                    errors.append(f"call {row['call']}: no resolved campaign avatar")
                    continue
                amount, before, after = values
                delta = after - before
                if not math.isfinite(delta):
                    errors.append(f"call {row['call']}: non-finite DNA difference")
                    continue
                observations.append({
                    "call": row["call"], "epoch": entry["epoch"],
                    "enter_sequence": entry["sequence"], "return_sequence": row["sequence"],
                    "avatar": avatar, "requested_amount": amount,
                    "global_dna_before": before, "global_dna_after": after,
                    "global_dna_delta_at_return": delta,
                    "ownership": "NOT_ESTABLISHED",
                })
    if errors:
        observations = []
    return {
        "trace": str(path), "sha256": context["sha256"],
        "evidence_class": context["evidence_class"],
        "structural_valid": not errors, "errors": list(dict.fromkeys(errors)),
        "candidate_caller_rva": SOCIAL_REWARD_CALLER_RVA,
        "candidate_calls": len(observations), "observations": observations,
        "native_acceptance": "NOT_VERIFIED", "m11_status": "IN_PROGRESS",
        "limits": [
            "The caller classification is static evidence pending an inspected original social interaction.",
            "A returned global DNA call may enqueue a later award; its immediate delta is not the completed reward.",
            "The campaign avatar is not proof of the initiating player, independent species ownership or relationship outcome.",
            "Trace headers and parser success do not establish provenance, visual behavior, protected saves or M11 acceptance.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--require-social", action="store_true",
                        help="Fail when no complete call from the candidate social reward path is observed.")
    args = parser.parse_args()
    report = analyze(args.trace)
    if args.output.resolve() == args.trace.resolve():
        raise ValueError("The report must not overwrite the input trace")
    # Keep both existing evidence and the input trace immutable.
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    print(json.dumps({key: report[key] for key in
                      ("structural_valid", "candidate_calls", "native_acceptance")}))
    return 0 if report["structural_valid"] and (report["candidate_calls"] or not args.require_social) else 1


if __name__ == "__main__":
    raise SystemExit(main())
