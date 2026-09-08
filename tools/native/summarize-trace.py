"""Summarize TDH-decoded original-process file and registry evidence; retain unknowns."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path

def integer(prop):
    return int.from_bytes(bytes.fromhex(prop.get("hex", "")), "little") if prop else None

def summarize(path, personal_profile):
    files, keys, pending = {}, {}, {}
    operations = Counter()
    opens = Counter()
    completed_writes = Counter()
    requested_writes = Counter()
    registry_changes = Counter()
    unresolved = Counter()
    losses = Counter()
    pids = set()
    failed_io = Counter()
    for line in Path(path).open(encoding="utf-8"):
        event = json.loads(line)
        pids.add(event["pid"])
        task, operation = event.get("task", "UNKNOWN"), event.get("opcode_name", "UNKNOWN")
        operations[(task, operation)] += 1
        props = {p["name"]: p for p in event.get("properties", [])}
        text = lambda name: props.get(name, {}).get("text", "")
        raw = lambda name: props.get(name, {}).get("hex", "")
        if task.strip() == "Lost Event":
            losses[text("SessionName") or "unknown"] += 1
        if task == "FileIo":
            file_object = raw("FileObject")
            file_key = raw("FileKey")
            if operation == "Create" and text("OpenPath"):
                files[file_object] = text("OpenPath")
                opens[text("OpenPath")] += 1
            name = files.get(file_object) or files.get(file_key)
            if operation in ("Name", "FileCreate", "FileRundown") and text("FileName"):
                files[file_object] = text("FileName")
            if operation in ("Write", "SetInfo", "Delete", "Rename"):
                name = name or "<unresolved>"
                requested_writes[(operation, name)] += 1
                if name == "<unresolved>": unresolved["file_" + operation] += 1
                pending[raw("IrpPtr")] = (operation, name)
            elif operation == "OperationEnd":
                previous = pending.pop(raw("IrpPtr"), None)
                if previous:
                    status = integer(props.get("NtStatus"))
                    if status == 0: completed_writes[previous] += 1
                    else: failed_io[(previous[0], previous[1], str(status))] += 1
        elif task == "Registry":
            handle, name = raw("KeyHandle"), text("KeyName")
            if name.startswith("\\REGISTRY\\"):
                keys[handle] = name
            if operation in ("SetValue", "DeleteValue", "Delete", "SetInformation"):
                status = integer(props.get("Status"))
                if status == 0:
                    base = keys.get(handle, "<unresolved>")
                    full = base + ("\\" + name if name and not name.startswith("\\REGISTRY\\") else "")
                    registry_changes[(operation, full)] += 1
                    if base == "<unresolved>": unresolved["registry_" + operation] += 1
    rows = lambda counts: [{"operation": k[0], "path": k[1], "count": v} for k, v in sorted(counts.items())]
    protected_path = "\\" + str(personal_profile).replace("/", "\\").split(":", 1)[-1].strip("\\").lower() + "\\"
    protected_mentions = [p for p in opens if protected_path in p.lower()]
    protected_writes = [row for row in rows(completed_writes) if protected_path in row["path"].lower()]
    return {
        "schema_version": 1, "kind": "native-etw-file-registry-summary", "pids": sorted(pids),
        "operation_counts": [{"task": k[0], "operation": k[1], "count": v} for k, v in operations.most_common()],
        "file_open_paths": [{"path": k, "count": v} for k, v in sorted(opens.items())],
        "requested_mutations": rows(requested_writes), "completed_mutations": rows(completed_writes),
        "successful_registry_changes": rows(registry_changes),
        "failed_io_mutations": [{"operation": k[0], "path": k[1], "ntstatus": k[2], "count": v} for k, v in sorted(failed_io.items())],
        "unresolved_requests": dict(unresolved), "uncompleted_mutation_requests": len(pending),
        "loss_markers_by_session": dict(losses), "personal_profile_open_mentions": protected_mentions,
        "personal_profile_completed_writes": protected_writes,
        "note": "Paths are kernel device paths. Only recorded process events are summarized. Unresolved correlations are retained; OS denial probes and complete before/after personal manifests independently establish personal-save protection."
    }

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--personal-profile", type=Path, required=True)
    args = parser.parse_args()
    result = summarize(args.input, args.personal_profile)
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(result, output, indent=2)
    print(json.dumps({key: result[key] for key in ("pids", "unresolved_requests", "loss_markers_by_session", "personal_profile_open_mentions", "personal_profile_completed_writes")}))
