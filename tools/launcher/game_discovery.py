"""Bounded, read-only discovery from installed-game records and Steam libraries.

Registry paths/DataDir semantics: pinned ModAPI.Common/SporePath.cs.
Steam discovery reads libraryfolders.vdf and matching app manifests; it never
executes uninstall strings, scans entire drives, follows junctions or changes stores.
"""
from __future__ import annotations

import ctypes
import json
import os
from pathlib import Path
import re

MAX_VDF_BYTES = 2 * 1024 * 1024


def read_vdf(path: Path):
    """Parse the quoted KeyValues subset used by Steam's local install records."""
    if path.stat().st_size > MAX_VDF_BYTES:
        raise ValueError("Steam metadata exceeds the read limit")
    content = path.read_text(encoding="utf-8-sig")
    tokens = re.finditer(r'"((?:\\.|[^"\\])*)"|([{}])|(?://[^\n]*)', content)
    values = []
    for token in tokens:
        quoted, brace = token.groups()
        if brace:
            values.append((brace, brace))
        elif quoted is not None:
            values.append(("text", re.sub(r'\\([\\"])', r'\1', quoted)))
    index = 0

    def parse(depth=0):
        nonlocal index
        if depth > 12:
            raise ValueError("Steam metadata nesting exceeds the limit")
        result = {}
        while index < len(values):
            kind, key = values[index]
            index += 1
            if kind == "}":
                if depth == 0:
                    raise ValueError("Unbalanced Steam metadata")
                return result
            if kind != "text" or index >= len(values):
                raise ValueError("Malformed Steam metadata")
            kind, value = values[index]
            index += 1
            if kind == "{":
                result[key.casefold()] = parse(depth + 1)
            elif kind == "text":
                result[key.casefold()] = value
            else:
                raise ValueError("Malformed Steam metadata")
        if depth:
            raise ValueError("Unclosed Steam metadata")
        return result
    return parse()


def registry_records():
    if os.name != "nt":
        return [], []
    import winreg
    candidates, steam_roots = [], []
    fields = ("InstallLoc", "InstallLocation", "Install Dir", "InstallPath", "DataDir", "path", "exe")

    def values(hive, key, view):
        try:
            with winreg.OpenKey(hive, key, 0, winreg.KEY_READ | view) as handle:
                output = {}
                for index in range(min(winreg.QueryInfoKey(handle)[1], 100)):
                    name, value, _ = winreg.EnumValue(handle, index)
                    if isinstance(value, str): output[name.casefold()] = value
                return output
        except OSError:
            return {}

    def subkeys(hive, key, view):
        try:
            with winreg.OpenKey(hive, key, 0, winreg.KEY_READ | view) as handle:
                return [winreg.EnumKey(handle, i) for i in range(min(winreg.QueryInfoKey(handle)[0], 4096))]
        except OSError:
            return []

    for hive, hive_name in ((winreg.HKEY_LOCAL_MACHINE, "HKLM"), (winreg.HKEY_CURRENT_USER, "HKCU")):
        for view, bits in ((winreg.KEY_WOW64_32KEY, "32"), (winreg.KEY_WOW64_64KEY, "64")):
            for prefix, store in ((r"SOFTWARE\Electronic Arts", "EA"), (r"SOFTWARE\GOG.com\Games", "GOG"),
                                  (r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall", "Installed apps")):
                for child in subkeys(hive, prefix, view):
                    key = prefix + "\\" + child
                    record = values(hive, key, view)
                    label = " ".join((child, record.get("displayname", ""), record.get("gamename", "")))
                    if not re.search(r"\bspore(?:_|\b)", label, re.IGNORECASE): continue
                    for field in fields:
                        path = record.get(field.casefold())
                        if path:
                            candidates.append({"path": path, "store": store, "source": f"{hive_name}/{bits}/{key}:{field}"})
            for key in (r"SOFTWARE\Valve\Steam",):
                record = values(hive, key, view)
                for field in ("steampath", "installpath"):
                    if record.get(field): steam_roots.append(Path(record[field]))
    return candidates, steam_roots


def common_records():
    if os.name != "nt": return [], []
    records, steam = [], []
    for name in ("ProgramFiles", "ProgramFiles(x86)"):
        value = os.environ.get(name)
        if value:
            steam.append(Path(value) / "Steam")
            for suffix in ("Electronic Arts/Spore", "Electronic Arts/SPORE_EP1", "EA Games/SPORE", "Origin Games/SPORE", "GOG Galaxy/Games/SPORE"):
                records.append({"path": str(Path(value) / suffix), "store": "Local install", "source": "Common install directory"})
    function = ctypes.WinDLL("kernel32", use_last_error=True).GetDriveTypeW
    function.argtypes = [ctypes.c_wchar_p]
    function.restype = ctypes.c_uint
    for drive in "CDEFGHIJKLMNOPQRSTUVWXYZ":
        root = f"{drive}:\\"
        if function(root) != 3: continue  # Fixed local drives only, no network scans.
        for suffix in ("Games/SPORE", "GOG Games/SPORE", "GOG Games/SPORE Collection", "EA Games/SPORE", "Origin Games/SPORE"):
            records.append({"path": str(Path(root) / suffix), "store": "Local install", "source": "Common install directory"})
    return records, steam


def steam_records(roots, no_reparse):
    libraries = {}
    issues = []
    for root in roots:
        try:
            root = no_reparse(root)
            if not root.is_dir(): continue
            libraries[str(root).casefold()] = root
            path = no_reparse(root / "steamapps/libraryfolders.vdf")
            if not path.is_file(): continue
            data = read_vdf(path).get("libraryfolders", {})
            if not isinstance(data, dict): raise ValueError("Invalid Steam library list")
            for index, record in data.items():
                if not index.isdigit(): continue
                value = record.get("path") if isinstance(record, dict) else record
                if value:
                    library = no_reparse(Path(value))
                    if library.is_absolute(): libraries[str(library).casefold()] = library
        except (OSError, ValueError) as error:
            issues.append(str(error))
    candidates = []
    for library in list(libraries.values())[:64]:
        for app_id in ("17390", "24720"):  # SPORE and Galactic Adventures; pinned SporePath.cs.
            path = library / "steamapps" / f"appmanifest_{app_id}.acf"
            try:
                path = no_reparse(path)
                if not path.is_file(): continue
                state = read_vdf(path).get("appstate", {})
                directory = state.get("installdir", "")
                if not directory or Path(directory).name != directory or directory in (".", "..") or any(c in directory for c in "/\\:"):
                    raise ValueError("Unsafe Steam installation directory")
                candidates.append({"path": str(library / "steamapps/common" / directory), "store": "Steam", "source": str(path)})
            except (OSError, ValueError, AttributeError) as error:
                issues.append(str(error))
    return candidates, issues


def normalize_root(value, no_reparse):
    text = os.path.expandvars(str(value)).strip().strip('"').rstrip("\\/")
    if not text: return None
    path = Path(text)
    if not path.is_absolute(): return None
    if path.name.casefold() == "sporeapp.exe": path = path.parent
    if path.name.casefold() in ("sporebin", "sporebinep1", "data", "dataep1"): path = path.parent
    path = no_reparse(path)
    if (path / "SporebinEP1/SporeApp.exe").is_file() or (path / "SporeBin/SporeApp.exe").is_file(): return path
    return None


def discover(diag, preferred=None, records=None, steam_roots=None):
    issues = []
    if records is None:
        records, registry_steam = registry_records()
        common, common_steam = common_records()
        records += common
        steam_roots = [*registry_steam, *common_steam]
    steam, steam_issues = steam_records(steam_roots or [], diag.no_reparse)
    records = [*records, *steam]
    issues += steam_issues
    if preferred:
        records.insert(0, {"path": str(preferred), "store": "Selected install", "source": "Saved or selected installation"})
    installations = {}
    for record in records[:1024]:
        try:
            root = normalize_root(record["path"], diag.no_reparse)
            if root is None: continue
            key = str(root).casefold()
            if key not in installations:
                relative = "SporebinEP1/SporeApp.exe" if (root / "SporebinEP1/SporeApp.exe").is_file() else "SporeBin/SporeApp.exe"
                identity = diag.pe_identity(root / relative)
                installations[key] = {"root": str(root), "store": record["store"], "sources": [],
                                      "has_ga_runtime": relative.startswith("SporebinEP1"), "executable": identity}
            installations[key]["sources"].append(record["source"])
            if record["store"] == "GOG": installations[key]["store"] = "GOG"
        except (OSError, ValueError) as error:
            issues.append(f"{record['source']}: {error}")
    return {"installations": list(installations.values()), "discovery_issues": issues,
            "method": "Registry, Steam library manifests and bounded common install paths"}
