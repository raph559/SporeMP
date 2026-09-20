"""Strict invite parsing and short-lived, current-owner-only session credentials.

No invitation/token is included in a command argument, saved preference or report.
The launcher keeps the invite in memory for Rejoin and passes it on stdin.
"""
from __future__ import annotations

from contextlib import contextmanager
import ctypes
from ctypes import wintypes
import ipaddress
import json
import os
from pathlib import Path
import re
from urllib.parse import parse_qsl, urlsplit
from uuid import uuid4

MAX_INVITATION = 2048


def parse_invitation(value):
    message = "Paste the complete invitation supplied by your server host."
    if not isinstance(value, str) or not 1 <= len(value) <= MAX_INVITATION:
        raise ValueError(message)
    value = value.strip()
    if any(ord(char) <= 32 or ord(char) > 126 for char in value):
        raise ValueError(message)
    try:
        url = urlsplit(value)
        if url.scheme != "sporemp" or url.netloc != "join" or url.path not in ("", "/") or url.fragment:
            raise ValueError(message)
        pairs = parse_qsl(url.query, keep_blank_values=True, strict_parsing=True, max_num_fields=4)
        fields = dict(pairs)
        if len(pairs) != 4 or set(fields) != {"host", "port", "cert", "token"}:
            raise ValueError(message)
        host = fields["host"]
        if len(host) > 253 or not host or any(ord(char) <= 32 or ord(char) > 126 for char in host):
            raise ValueError(message)
        try:
            host = str(ipaddress.ip_address(host))
        except ValueError:
            if not re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?", host):
                raise ValueError(message) from None
            if any(not 1 <= len(label) <= 63 or label.startswith("-") or label.endswith("-") for label in host.split(".")):
                raise ValueError(message)
            host = host.lower()
        if not re.fullmatch(r"[0-9]{1,5}", fields["port"]) or not 1 <= int(fields["port"]) <= 65535:
            raise ValueError(message)
        for name in ("cert", "token"):
            if not re.fullmatch(r"[A-Fa-f0-9]{64}", fields[name]) or fields[name] == "0" * 64:
                raise ValueError(message)
        return {"schema": 1, "host": host, "port": int(fields["port"]),
                "certificate_sha256": fields["cert"].lower(), "credential": fields["token"].lower()}
    except (ValueError, UnicodeError):
        # Never reflect malformed, potentially secret input into diagnostics.
        raise ValueError(message) from None


def private_write(path, data):
    """Create the file with its restricted DACL before writing any secret bytes."""
    if os.name != "nt":
        raise OSError("Multiplayer sessions require Windows.")
    class SecurityAttributes(ctypes.Structure):
        _fields_ = [("length", wintypes.DWORD), ("descriptor", ctypes.c_void_p), ("inherit", wintypes.BOOL)]

    advapi = ctypes.WinDLL("advapi32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    convert = advapi.ConvertStringSecurityDescriptorToSecurityDescriptorW
    convert.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.DWORD)]
    convert.restype = wintypes.BOOL
    create = kernel.CreateFileW
    create.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(SecurityAttributes), wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    create.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    write = kernel.WriteFile
    write.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    write.restype = wintypes.BOOL
    descriptor = ctypes.c_void_p()
    # Protected DACL grants only the owner; inherited Users/Authenticated Users
    # permissions on a development checkout cannot expose this token.
    if not convert("D:P(A;;FA;;;OW)", 1, ctypes.byref(descriptor), None):
        raise ctypes.WinError(ctypes.get_last_error())
    handle = None
    try:
        attributes = SecurityAttributes(ctypes.sizeof(SecurityAttributes), descriptor, False)
        handle = create(str(path), 0x40000000, 0, ctypes.byref(attributes), 1, 0x80, None)
        if handle == ctypes.c_void_p(-1).value:
            handle = None
            raise ctypes.WinError(ctypes.get_last_error())
        written = wintypes.DWORD()
        buffer = ctypes.create_string_buffer(data)
        if not write(handle, buffer, len(data), ctypes.byref(written), None) or written.value != len(data):
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        if handle is not None:
            kernel.CloseHandle(handle)
        kernel.LocalFree(descriptor)


@contextmanager
def session_file(directory: Path, config):
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / ("session-" + uuid4().hex + ".conf")
    try:
        private_write(path, "".join(f"{key}={value}\n" for key, value in config.items()).encode("utf-8"))
        yield path
    finally:
        path.unlink(missing_ok=True)


def connection_state(path):
    """A handshake alone never establishes a native scene connection."""
    try:
        with path.open("rb") as stream:
            raw = stream.read(4097)
        if len(raw) > 4096:
            return None
        value = json.loads(raw)
        state = value["state"]
        if state not in ("connecting", "connected", "disconnected", "error"):
            return None
        if state == "connected" and (type(value.get("player_id")) is not int or value["player_id"] not in (1, 2) or
                                     type(value.get("baseline_sequence")) is not int or not 0 < value["baseline_sequence"] < 2 ** 64):
            return None
        return state
    except (OSError, ValueError, KeyError, TypeError):
        return None
