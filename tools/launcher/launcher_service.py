"""Player launcher operations: automatic discovery and guarded normal-account startup.

Reports are exclusive-create, local, and explicitly retain the native evidence gate.
The UI sends argument arrays, never shell commands. Explicit Join connects only
to the server named in the invitation. Diagnostic exports stay local.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
from pathlib import Path
import sys
from uuid import uuid4
import zipfile

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("sporemp_diag", REPO / "tools/diagnostics/sporemp_diag.py")
diag = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diag)
spec = importlib.util.spec_from_file_location("game_discovery", REPO / "tools/launcher/game_discovery.py")
discovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(discovery)
spec = importlib.util.spec_from_file_location("display_settings", REPO / "tools/launcher/display_settings.py")
display = importlib.util.module_from_spec(spec)
spec.loader.exec_module(display)
spec = importlib.util.spec_from_file_location("worker_manager", REPO / "tools/launcher/worker_manager.py")
workers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(workers)
spec = importlib.util.spec_from_file_location("multiplayer_session", REPO / "tools/launcher/multiplayer_session.py")
multiplayer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(multiplayer)


def load_settings():
    path = diag.no_reparse(REPO / "local/launcher/settings.json")
    if not path.exists(): return {}
    if path.stat().st_size > 65536: raise ValueError("Launcher settings are too large")
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schema_version") != 1: raise ValueError("Unsupported launcher settings")
    return value


def save_settings(settings):
    path = diag.no_reparse(REPO / "local/launcher/settings.json")
    temporary = path.with_name("settings-" + uuid4().hex + ".json")
    diag.write_json(temporary, {**settings, "schema_version": 1})
    os.replace(temporary, path)


def display_options():
    modes = display.display_modes()
    try:
        selected = display.preferences(load_settings().get("display"))
        warning = ""
        try:
            display.resolve(selected, modes)
        except ValueError as error:
            warning = str(error)
    except ValueError as error:
        selected = display.preferences()
        warning = str(error)
    return {"display": selected, **modes, "warning": warning}


def save_display(mode, resolution):
    selected = display.preferences({"mode": mode, "resolution": resolution})
    modes = display.display_modes()
    display.resolve(selected, modes)
    with preparation_lock():
        settings = load_settings()
        settings["display"] = selected
        save_settings(settings)
    return {"display": selected, **modes, "warning": ""}


@contextmanager
def preparation_lock():
    path = diag.no_reparse(REPO / "local/launcher/preparation.lock")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as stream:
        if stream.tell() == 0: stream.write(b"0"); stream.flush()
        stream.seek(0)
        if os.name == "nt":
            import msvcrt
            try: msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as error: raise ValueError("Another SporeMP window is already preparing this installation") from error
        try:
            yield
        finally:
            if os.name == "nt":
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)


def emit_progress(phase, message):
    print(json.dumps({"kind": "sporemp-launcher-progress", "phase": phase, "message": message}), flush=True)


def save_sources():
    folders = diag.known_folders()
    if folders["errors"]:
        raise ValueError("Windows could not resolve your save folders: " + str(folders["errors"]))
    return [Path(folders["shell_folders"]["appdata"]) / "Spore",
            Path(folders["shell_folders"]["documents"]) / "My Spore Creations"]


def ensure_backup():
    if diag.game_running(): raise ValueError("Close SPORE so your saves can be backed up safely")
    sources = save_sources()
    snapshots = [diag.tree_manifest(source) for source in sources]
    identity = hashlib.sha256(json.dumps([{"source": str(source), "snapshot": snapshot} for source, snapshot in zip(sources, snapshots)], sort_keys=True).encode()).hexdigest()
    destination = diag.no_reparse(REPO / "local/backups" / ("automatic-" + identity[:32]))
    reused = False
    repaired = False
    alternatives = [destination]
    if destination.parent.exists():
        alternatives += sorted(destination.parent.glob(destination.name + "-*"))[:64]
    for existing in alternatives:
        if not existing.exists(): continue
        try:
            diag.verify_backup(existing)
            manifest = json.loads((existing / "backup-manifest.json").read_text(encoding="utf-8"))
            if [x["snapshot"] for x in manifest["sources"]] != snapshots or [x["source"] for x in manifest["sources"]] != [str(x) for x in sources]:
                raise ValueError("Backup source identity mismatch")
            reused = True
            destination = existing
            break
        except (OSError, ValueError, KeyError, TypeError):
            # Preserve the damaged backup; create a new independent verified copy.
            repaired = True
    if not reused and destination.exists():
        destination = destination.with_name(destination.name + "-" + uuid4().hex[:8])
    if not reused: diag.backup(sources, destination)
    diag.verify_backup(destination)
    if diag.game_running() or [diag.tree_manifest(source) for source in sources] != snapshots:
        raise ValueError("SPORE started or saves changed during preparation; retry after closing the game")
    return {"verified": True, "reused": reused, "previous_copy_damaged": repaired,
            "backup_directory": str(destination), "file_count": sum(len(x["files"]) for x in snapshots),
            "bytes": sum(file["size"] for x in snapshots for file in x["files"]),
            "native_restore_status": "NOT_RUN"}


def ensure_workspace():
    root = diag.no_reparse(REPO / "local/profiles/launcher-client")
    if root.exists():
        profile = json.loads(diag.no_reparse(root / "profile.json").read_text(encoding="utf-8"))
        if profile.get("schema_version") != 1 or Path(profile["root"]) != root or profile.get("kind") != "disposable-directory-workspace":
            raise ValueError("The launcher workspace has invalid metadata")
        for folder in ("appdata", "localappdata", "documents", "temp", "logs"):
            if not diag.no_reparse(root / folder).is_dir(): raise ValueError("The launcher workspace is incomplete")
        return {"root": str(root), "reused": True, "native_write_isolation": "NOT_RUN", "launch_allowed": False}
    profile = diag.create_profile(root.parent, root.name)
    return {"root": profile["root"], "reused": False, "native_write_isolation": "NOT_RUN", "launch_allowed": False}

def native_availability():
    """Availability is implementation readiness, never native qualification evidence."""
    required = [
        REPO / "build/win32/Release/SporeMP.NativeHost.exe",
        REPO / "build/win32/Release/SporeMP.Bridge.dll",
        REPO / "build/sdk/Release/SporeModAPI.dll",
        REPO / "build/injector/Release/ModAPI.DLLInjector.dll",
    ]
    return {"available": all(diag.no_reparse(p).is_file() for p in required),
            "join_available": all(diag.no_reparse(p).is_file() for p in required) and diag.no_reparse(REPO / "build/win32/Release/SporeMP.Coordinator.exe").is_file(),
            "mode": "current-user", "multiplayer": False,
            "note": "Play uses the player's Windows account and existing SPORE saves. Compiled fingerprints are checked before injection."}


def prepare(game_root=None, progress=None):
    progress = progress or (lambda phase, message: None)
    with preparation_lock():
        settings = load_settings()
        preferred = game_root or settings.get("game_root")
        progress("discover", "Finding your SPORE installation…")
        found = discovery.discover(diag, preferred)
        installations = found["installations"]
        base = {**found, "launch_allowed": False, "launched_processes": 0}
        if not installations:
            return {**base, "state": "game_not_found", "message": "SPORE was not found. Locate an existing installation to continue."}, 23
        selected = None
        if preferred:
            path = discovery.normalize_root(preferred, diag.no_reparse)
            selected = next((x for x in installations if path is not None and Path(x["root"]) == path), None)
            if game_root and selected is None:
                return {**base, "state": "game_not_found", "message": "That folder does not contain SPORE. Choose its installation folder."}, 23
        candidate = json.loads((REPO / "config/compatibility.candidate.json").read_text(encoding="utf-8"))
        if selected is None:
            matching = [x for x in installations if x["has_ga_runtime"] and x["executable"] == candidate["executable"]]
            choices = matching if matching else installations
            if len(choices) > 1:
                return {**base, "state": "choose_installation", "message": "More than one SPORE installation was found. Choose which one to use."}, 23
            selected = choices[0]
        base["installation"] = selected
        settings["game_root"] = selected["root"]
        save_settings(settings)
        if not selected["has_ga_runtime"]:
            return {**base, "state": "unsupported_installation", "message": "This development build needs the Galactic Adventures runtime alongside SPORE."}, 20
        progress("verify", "Checking your game files…")
        check, code = installation_check(Path(selected["root"]))
        base["compatibility"] = check
        if not check["candidate_match"]:
            return {**base, "state": "unsupported_installation", "message": "This installation does not match the current development build."}, 20
        if diag.game_running():
            if not workers.only_registered_workers_running():
                return {**base, "state": "game_running", "message": "SPORE is already running."}, 24
        base["native"] = native_availability()
        # Automatic preparation cannot manufacture the missing native acceptance evidence.
        return {**base, "state": "development_build", "message": "SPORE is ready. Multiplayer is still in development."}, 22


def stage_player_payload():
    """Keep project DLLs in launcher-owned storage, without installing into SPORE."""
    host = diag.no_reparse(REPO / "build/win32/Release/SporeMP.NativeHost.exe")
    identity = diag.fingerprint(host)["sha256"]
    destination = diag.no_reparse(REPO / "local/launcher/native-payload" / identity)
    with preparation_lock():
        for source, relative in ((host, "SporeMP.NativeHost.exe"),
                                 (REPO / "build/injector/Release/ModAPI.DLLInjector.dll", "ModAPI.DLLInjector.dll"),
                                 (REPO / "build/sdk/Release/SporeModAPI.dll", "mLibs/SporeModAPI.dll"),
                                 (REPO / "build/win32/Release/SporeMP.Bridge.dll", "mLibs/SporeMP.Bridge.dll")):
            source, target = diag.no_reparse(source), diag.no_reparse(destination / relative)
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.open("rb") as incoming, target.open("xb") as outgoing:
                    shutil.copyfileobj(incoming, outgoing)
            if diag.fingerprint(target) != diag.fingerprint(source):
                raise ValueError("A launcher component changed. Rebuild or repair this development installation.")
    return destination


def native_launch(game_root, progress=None):
    progress = progress or (lambda phase, message: None)
    prepared, code = prepare(game_root, progress)
    if code != 22 or not prepared.get("native", {}).get("available"):
        return {**prepared, "error": "This installation is not ready to launch."}, code if code != 22 else 22
    # Resolve Desktop again for every Play, including monitor changes since saving.
    selected_display = display.resolve(load_settings().get("display"))
    run_name = "player-" + uuid4().hex[:16]
    run_root = diag.no_reparse(REPO / "local/launcher/native-runs" / run_name)
    run_root.mkdir(parents=True)
    payload = stage_player_payload()
    command = [str(payload / "SporeMP.NativeHost.exe"), "--play", prepared["installation"]["root"], str(payload), str(run_root), *selected_display["arguments"]]
    progress("native", "Starting SPORE…")
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    events = []
    host = run_root / "native-host.jsonl"
    if host.exists():
        events = [json.loads(line) for line in host.read_text(encoding="utf-8").splitlines() if line.strip()]
    started = [x for x in events if x["event"] == "created_suspended"]
    exited = [x for x in events if x["event"] == "game_exited"]
    lifecycle = []
    if len(started) == 1:
        bridge = run_root / ("bridge-" + str(started[0]["game_pid"]) + ".jsonl")
        if bridge.exists():
            lifecycle = [json.loads(line) for line in bridge.read_text(encoding="utf-8").splitlines() if line.strip()]
    clean = completed.returncode == 0 and len(exited) == 1 and exited[0]["exit_code"] == 0
    clean = clean and [x["event"] for x in lifecycle if x["event"] in ("initialize", "dispose")] == ["initialize", "dispose"]
    result = {"mode": "current-user", "profile_redirection": False, "multiplayer": False, "run_name": run_name,
              "evidence_directory": str(run_root), "launched_processes": len(started), "clean_lifecycle": bool(clean),
              "native_host_exit": completed.returncode, "lifecycle": lifecycle, "display": selected_display}
    if not clean:
        result["error"] = next((x["reason"] for x in reversed(events) if x["event"] == "rejected"),
                               completed.stderr.strip() or "SPORE did not complete a normal initialization and shutdown. See the local native evidence.")
    return result, 0 if clean else (completed.returncode or 31)


def native_join(game_root, invitation, progress=None):
    progress = progress or (lambda phase, message: None)
    config = multiplayer.parse_invitation(invitation)
    prepared, code = prepare(game_root, progress)
    if code != 22 or not prepared.get("native", {}).get("available"):
        return {"error": "Your SPORE installation needs attention before joining.", "launched_processes": 0}, code
    coordinator = diag.no_reparse(REPO / "build/win32/Release/SporeMP.Coordinator.exe")
    if not coordinator.is_file():
        return {"error": "The multiplayer component is missing from this development build.", "launched_processes": 0}, 22
    folders = diag.known_folders()
    if folders["errors"]:
        return {"error": "Windows could not locate your SPORE saves for this multiplayer session.", "launched_processes": 0}, 32
    fixture = diag.no_reparse(Path(folders["shell_folders"]["appdata"]) / "Spore/Games/Game0/Satiria.spo")
    if not fixture.is_file():
        return {"error": "This experimental server requires the Satiria Creature world in your existing SPORE saves. No save was replaced.", "launched_processes": 0}, 32
    try:
        world_identity = multiplayer.world_identity_fields(fixture.parents[2], diag.no_reparse)
    except (OSError, ValueError) as error:
        return {"error": str(error), "launched_processes": 0}, 32
    fixture_sha256 = world_identity["world_satiria_sha256"]
    selected_display = display.resolve(load_settings().get("display"))
    payload = stage_player_payload()
    candidate_path = diag.no_reparse(REPO / "config/compatibility.candidate.json")
    candidate = json.loads(candidate_path.read_text(encoding="utf-8"))
    config.update(role="player", build_sha256=diag.fingerprint(payload / "mLibs/SporeMP.Bridge.dll")["sha256"],
                  executable_sha256=candidate["executable"]["sha256"],
                  content_sha256=diag.fingerprint(candidate_path)["sha256"], fixture_sha256=fixture_sha256)
    config.update(world_identity)
    run_name = "join-" + uuid4().hex[:16]
    run_root = diag.no_reparse(REPO / "local/launcher/native-runs" / run_name)
    run_root.mkdir(parents=True)
    with multiplayer.session_file(diag.no_reparse(REPO / "local/launcher/sessions"), config) as session_path:
        progress("authenticate", "Checking the server and your invitation…")
        try:
            probe = subprocess.run([str(coordinator), "--client-probe", "--config", str(session_path)],
                                   capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=25)
            if len(probe.stdout) > 16384:
                raise ValueError("The server check returned an invalid result.")
            checked = json.loads(probe.stdout)
            if not isinstance(checked, dict):
                raise ValueError("The server check returned an invalid result.")
            if probe.returncode != 0 or checked.get("authenticated") is not True:
                # Coordinator's fixed protocol error is helpful in Settings;
                # scrub credentials defensively before report/export storage.
                reason = str(checked.get("error", "The server did not accept this invitation."))[:1024]
                reason = reason.replace(config["credential"], "[private]").replace(invitation, "[private invitation]")
                return {"error": reason, "authenticated": False, "launched_processes": 0}, 32
        except subprocess.TimeoutExpired:
            return {"error": "The server did not answer in time. Check that it is running and reachable.", "authenticated": False, "launched_processes": 0}, 32
        except (ValueError, TypeError):
            return {"error": "The server check returned an invalid result. Update this development build.", "authenticated": False, "launched_processes": 0}, 32
        command = [str(payload / "SporeMP.NativeHost.exe"), "--join", prepared["installation"]["root"],
                   str(payload), str(run_root), str(session_path), *selected_display["arguments"]]
        progress("native", "Starting your shared scene in SPORE…")
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                              encoding="utf-8", errors="replace") as process:
            previous = None
            connected = False
            while True:
                status = multiplayer.connection_state(run_root / "network-status.json")
                if status and status != previous:
                    previous = status
                    connected = connected or status == "connected"
                    progress(status, {"connecting": "Connecting your shared scene…", "connected": "Connected to your shared scene",
                                      "disconnected": "Connection lost. Waiting for the server…", "error": "The connection needs attention."}[status])
                try:
                    _, error_output = process.communicate(timeout=.25)
                    break
                except subprocess.TimeoutExpired:
                    continue
            # The final atomic write can race process exit between two polls.
            connected = connected or multiplayer.connection_state(run_root / "network-status.json") == "connected"
            exit_code = process.returncode
    # Credentials are already removed before reading, saving or exporting reports.
    events = []
    host_log = run_root / "native-host.jsonl"
    if host_log.exists():
        events = [json.loads(line) for line in host_log.read_text(encoding="utf-8").splitlines() if line.strip()]
    started = [event for event in events if event.get("event") == "created_suspended"]
    exited = [event for event in events if event.get("event") == "game_exited"]
    lifecycle = []
    if len(started) == 1:
        trace = run_root / ("bridge-" + str(started[0]["game_pid"]) + ".jsonl")
        if trace.exists():
            lifecycle = [json.loads(line) for line in trace.read_text(encoding="utf-8").splitlines() if line.strip()]
    clean = exit_code == 0 and len(exited) == 1 and exited[0]["exit_code"] == 0 and [
        event["event"] for event in lifecycle if event.get("event") in ("initialize", "dispose")] == ["initialize", "dispose"]
    result = {"mode": "current-user", "profile_redirection": False, "multiplayer": True,
              "authenticated": True, "connected_baseline_observed": connected, "run_name": run_name,
              "evidence_directory": str(run_root), "launched_processes": len(started), "clean_lifecycle": clean,
              "native_host_exit": exit_code, "display": selected_display}
    if not clean or not connected:
        reason = next((event["reason"] for event in reversed(events) if event.get("event") == "rejected"),
                      "The shared scene did not become ready. The session result is recorded in Settings." if not connected else
                      "SPORE did not close normally. The session result is recorded in Settings.")
        result["error"] = str(reason).replace(config["credential"], "[private]").replace(invitation, "[private invitation]")[:1024]
    return result, 0 if clean and connected else exit_code or 31


def new_run():
    root = diag.no_reparse(REPO / "local/launcher/runs")
    root.mkdir(parents=True, exist_ok=True)
    run = root / (diag.utc_now()[:19].replace(":", "-") + "-" + uuid4().hex[:8])
    run.mkdir()
    return run


def installation_check(game_root: Path):
    candidate = json.loads((REPO / "config/compatibility.candidate.json").read_text(encoding="utf-8"))
    report = diag.preflight(game_root, candidate, None, None)
    report["game_root"] = str(game_root.absolute())
    report["bridge_artifacts"] = {}
    for name, path in (("bridge", REPO / "build/win32/Release/SporeMP.Bridge.dll"),
                       ("core", REPO / "build/sdk/Release/SporeModAPI.dll")):
        report["bridge_artifacts"][name] = diag.fingerprint(path) if path.is_file() else None
    return report, 22 if report["candidate_match"] else 20


def personal_backup():
    folders = diag.known_folders()
    if folders["errors"]:
        raise ValueError("Windows could not resolve personal save folders: " + str(folders["errors"]))
    # Shell APIs are used, not environment-variable guesses or literal user names.
    sources = [Path(folders["shell_folders"]["appdata"]) / "Spore",
               Path(folders["shell_folders"]["documents"]) / "My Spore Creations"]
    destination = REPO / "local/backups" / ("launcher-" + uuid4().hex)
    manifest = diag.backup(sources, destination)
    result = diag.verify_backup(destination)
    return {**result, "backup_directory": str(destination),
            "file_count": sum(len(record["snapshot"]["files"]) for record in manifest["sources"]),
            "bytes": sum(item["size"] for record in manifest["sources"] for item in record["snapshot"]["files"]),
            "missing_sources": [record["source"] for record in manifest["sources"] if not record["snapshot"]["exists"]]}, 0


def export_report(report_path: Path):
    allowed = diag.no_reparse(REPO / "local/launcher/runs")
    report_path = diag.no_reparse(report_path)
    if allowed not in report_path.parents or report_path.name != "result.json":
        raise ValueError("Only a launcher run report can be exported")
    if report_path.stat().st_size > 4 * 1024 * 1024:
        raise ValueError("Report exceeds export limit")
    content = report_path.read_bytes()
    report = json.loads(content)
    if report.get("kind") != "sporemp-launcher-operation" or report.get("schema_version") != 1:
        raise ValueError("Invalid launcher report")
    root = diag.no_reparse(REPO / "local/launcher/exports")
    root.mkdir(parents=True, exist_ok=True)
    destination = root / ("SporeMP-diagnostics-" + uuid4().hex[:12] + ".zip")
    # Explicit allowlist: no directory traversal, saves, credentials, logs or game assets.
    with zipfile.ZipFile(destination, "x", compression=zipfile.ZIP_DEFLATED) as bundle:
        bundle.writestr("result.json", content)
        bundle.writestr("README.txt", "Local diagnostic report only. May contain local paths and file hashes.\n"
                        "No saves, game binaries, credentials or automatic upload. Native status remains in result.json.\n")
    return {"export_path": str(destination), "files": ["result.json", "README.txt"],
            "sha256": diag.fingerprint(destination)["sha256"], "uploaded": False}, 0


def perform(action: str, game_root: Path | None = None, report_path: Path | None = None, progress=None,
            display_mode=None, resolution=None, worker_id=None, invitation=None):
    run = new_run()
    code = 2
    try:
        if action == "prepare":
            result, code = prepare(game_root, progress)
        elif action in ("worker_list", "worker_start", "worker_stop"):
            if action == "worker_list": result = workers.worker_list()
            elif action == "worker_start": result = workers.start(worker_id)
            else: result = workers.stop(worker_id)
            code = 0
        elif action == "display_settings":
            result, code = display_options(), 0
        elif action == "save_display":
            result, code = save_display(display_mode, resolution), 0
        elif action == "native_launch":
            result, code = native_launch(game_root, progress)
        elif action == "native_join":
            result, code = native_join(game_root, invitation, progress)
        elif action == "check":
            if game_root is None:
                raise ValueError("Select the original game installation first")
            result, code = installation_check(game_root)
        elif action == "backup":
            result, code = personal_backup()
        elif action == "workspace":
            result = diag.create_profile(REPO / "local/profiles", "launcher-" + uuid4().hex[:12])
            code = 0
        elif action == "export":
            if report_path is None:
                raise ValueError("Run a check or backup before exporting")
            result, code = export_report(report_path)
        else:
            raise ValueError("Unknown launcher operation")
    except (OSError, ValueError, KeyError, TypeError, diag.subprocess.SubprocessError) as error:
        result = {"error": str(error), "launch_allowed": False, "launched_processes": 0}
    envelope = {"schema_version": 1, "kind": "sporemp-launcher-operation", "operation": action,
                "utc": diag.utc_now(), "exit_code": code, "report_path": str(run / "result.json"),
                "native_tests": ("LIFECYCLE_PASSED" if result.get("clean_lifecycle") and code == 0 else "FAILED")
                    if action in ("native_launch", "native_join") and result.get("launched_processes", 0) else "NOT_RUN",
                "result": result}
    diag.write_json(run / "result.json", envelope)
    return envelope, code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "check", "backup", "workspace", "export", "native_launch", "native_join", "display_settings", "save_display", "worker_list", "worker_start", "worker_stop"))
    parser.add_argument("--worker-id", choices=("01", "02", "03"))
    parser.add_argument("--game-root", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--progress", action="store_true")
    parser.add_argument("--display-mode", choices=("game", "fullscreen", "windowed"))
    parser.add_argument("--resolution")
    args = parser.parse_args()
    try:
        if args.action != "save_display" and (args.display_mode is not None or args.resolution is not None):
            parser.error("Display choices are only accepted by save_display")
        if (args.action in ("worker_start", "worker_stop")) != (args.worker_id is not None):
            parser.error("Worker selection is required only for worker start/stop")
        invitation = sys.stdin.read(multiplayer.MAX_INVITATION + 1) if args.action == "native_join" else None
        result, code = perform(args.action, args.game_root, args.report, emit_progress if args.progress else None,
                               args.display_mode, args.resolution, args.worker_id, invitation)
        print(json.dumps(result, ensure_ascii=True))
        return code
    except (OSError, ValueError) as error:
        print(json.dumps({"error": str(error), "exit_code": 2}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
