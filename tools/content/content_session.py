"""Private developer RPC adapter; original native validation remains on authority.

The C++ client owns authenticated TLS. This adapter sends bounded value messages
through private pipes and retains exact outcomes without retrying mutations.
It is not a native gameplay client and never launches or controls the desktop.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import queue
import re
import struct
import subprocess
import threading
import time

import spore_content as content

ROOT = Path(__file__).resolve().parents[2]
HEADER = struct.Struct("<IIIIQQII32sIIIII")
OPS = {name: n for n, name in enumerate(("begin", "offer", "chunk", "seal", "fetch", "observation_begin",
    "observation_chunk", "observation_end", "commit", "cancel", "current", "validate_event", "load_event",
    "published_event", "cancelled_event", "dependencies", "dependency_failure", "dependency_event"), 1)}
ERRORS = ("none", "unknown_connection", "wrong_owner", "wrong_authority", "authority_unavailable", "stale_base",
    "transaction_exists", "unknown_transaction", "expired", "wrong_phase", "capacity", "invalid_size", "invalid_offset",
    "hash_mismatch", "invalid_png", "invalid_native_observation", "missing_part", "installed_profile_mismatch",
    "world_mismatch", "properties_mismatch", "not_ready", "unchanged_content", "internal_hash", "stale_request", "malformed")


def require(value, message):
    if not value:
        raise ValueError(message)


def integer(value, bits=64):
    require(type(value) is int and 0 <= value < 1 << bits, "Invalid unsigned scalar")
    return value


def private(path):
    p = content.safe_path(Path(path).absolute())
    require(ROOT / "local" in p.parents, "Private session files must stay under repository local/")
    return p


def encode(op, request, transaction=0, *, identity="0"*64, offset=0, total=0, data=b""):
    require(op in OPS and (OPS[op] <= OPS["current"] or op == "dependencies"), "Unknown request operation")
    require(isinstance(identity, str) and re.fullmatch("[0-9a-f]{64}", identity), "Invalid content identity")
    require(type(data) is bytes and len(data) <= 420, "Content chunk limit exceeded")
    head = HEADER.pack(0x38434d53, 1, OPS[op], 0, integer(request), integer(transaction), integer(offset, 32),
        integer(total, 32), bytes.fromhex(identity), 0, 0, 0, 0, len(data))
    return (head+data).ljust(512, b"\0").hex()


def decode(payload):
    require(isinstance(payload, str) and re.fullmatch("[0-9a-f]{1024}", payload), "Invalid content response encoding")
    raw = bytes.fromhex(payload)
    magic, schema, op, error, request, transaction, offset, total, identity, group, instance, kind, world_index, size = HEADER.unpack_from(raw)
    require(magic == 0x38434d53 and schema == 1 and op in OPS.values() and error < len(ERRORS), "Unknown content response schema")
    require(size <= 420 and world_index < 6 and not any(raw[HEADER.size+size:]), "Invalid content response bounds or reserved bytes")
    return dict(op=next(k for k, v in OPS.items() if v == op), failure=ERRORS[error], request=request, transaction=transaction,
        offset=offset, total=total, identity=identity.hex(), missing_key=content.key_text(group, instance, kind),
        world_index=world_index, data=raw[HEADER.size:HEADER.size+size])


def reportable(value):
    return {k: v.hex() if type(v) is bytes else v for k, v in value.items()}


class ContentSession:
    def __init__(self, config, output, *, fixture_authority=False):
        self.output = private(output)
        self.output.mkdir(parents=True, exist_ok=False)
        self.config = private(config)
        self.sequence = 0
        self.uncertain = False
        self.events = []
        self.queue = queue.Queue(maxsize=512)
        self.reader_error = None
        self.stdout_log = (self.output/"client-output.jsonl").open("x", encoding="utf-8")
        self.stderr_log = (self.output/"client-error.log").open("x", encoding="utf-8")
        command = [str(ROOT/"build/win32/Release/SporeMP.ContentClient.exe"), "--config", str(self.config)]
        if fixture_authority:
            command.insert(1,"--fixture-authority")
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.stderr_log,
            text=True, encoding="utf-8", bufsize=1, creationflags=subprocess.CREATE_NO_WINDOW)
        content.write_new(self.output/"process.json", content.canonical(dict(command=command, pid=self.process.pid,
            native_gameplay_client=False, credential_in_arguments=False)))
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        try:
            self.authenticated = self._receive(time.monotonic()+15, "authenticated")
        except Exception:
            self.close()
            raise

    def _read(self):
        try:
            for line in self.process.stdout:
                require(len(line) <= 2048, "Oversized client output")
                self.stdout_log.write(line); self.stdout_log.flush()
                self.queue.put_nowait(json.loads(line))
        except Exception as error:
            self.reader_error = str(error)
        finally:
            self.stdout_log.close()

    def _receive(self, deadline, expected=None):
        while True:
            require(self.reader_error is None, "Client output failed: " + str(self.reader_error))
            remaining = deadline-time.monotonic()
            require(remaining > 0, "Unknown RPC outcome: response deadline expired; mutation was not retried")
            try:
                record = self.queue.get(timeout=min(remaining, .2))
            except queue.Empty:
                require(self.process.poll() is None, "Client exited before the operation outcome was received")
                continue
            require(record.get("event") not in ("error", "closed"), "Client stopped: " + str(record.get("code", record.get("event"))))
            if record.get("event") == "content":
                return decode(record["payload"])
            if record.get("event") == expected:
                return record

    def rpc(self, op, transaction=0, **fields):
        require(not self.uncertain, "Prior RPC outcome is unknown; close this connection before further operations")
        self.sequence += 1
        payload = encode(op, self.sequence, transaction, **fields)
        try:
            self.process.stdin.write(payload+"\n"); self.process.stdin.flush()
            deadline = time.monotonic()+15
            while True:
                frame = self._receive(deadline)
                if frame["request"] == 0:
                    require(len(self.events) < 128, "Notification queue exceeded its bound")
                    self.events.append(frame)
                    continue
                require(frame["request"] == self.sequence and frame["op"] == op, "RPC response correlation mismatch")
                return frame
        except Exception:
            self.uncertain = True
            raise

    def success(self, op, transaction=0, **fields):
        result = self.rpc(op, transaction, **fields)
        require(result["failure"] == "none", "Content operation refused: " + result["failure"])
        return result

    def event(self, op, transaction, seconds=90):
        deadline = time.monotonic()+seconds
        while True:
            for n, frame in enumerate(self.events):
                if frame["op"] == op and frame["transaction"] == transaction:
                    return self.events.pop(n)
            frame = self._receive(deadline)
            require(frame["request"] == 0 and len(self.events) < 128, "Unexpected response or notification overflow")
            self.events.append(frame)

    def upload(self, transaction, path):
        data = content.read_bounded(private(path), content.MAX_CREATION_PNG)
        shape = content.inspect_creation_png(data)
        self.success("offer", transaction, identity=shape["sha256"], total=len(data))
        for offset in range(0, len(data), 420):
            self.success("chunk", transaction, offset=offset, data=data[offset:offset+420])
        result = self.success("seal", transaction)
        require(content.read_bounded(private(path), content.MAX_CREATION_PNG) == data, "Source PNG changed during upload")
        return dict(**reportable(result), png_sha256=shape["sha256"], png_bytes=len(data), publication=False)

    def ready(self, transaction, path):
        data = content.read_bounded(private(path), 64*1024)
        require(len(data) >= 292 and data[:8] == struct.pack("<II", 0x384f4d53, 1), "Invalid native observation envelope")
        self.success("observation_begin", transaction, identity=content.digest(data), total=len(data))
        for offset in range(0, len(data), 420):
            self.success("observation_chunk", transaction, offset=offset, data=data[offset:offset+420])
        return self.rpc("observation_end", transaction)

    def close(self):
        if self.process.poll() is None:
            try:
                self.process.stdin.write("quit\n"); self.process.stdin.flush(); self.process.stdin.close()
                self.process.wait(timeout=15)
            except (OSError, subprocess.TimeoutExpired):
                self.process.kill(); self.process.wait(timeout=5)
        self.reader.join(timeout=3)
        self.stderr_log.close()
        content.write_new(self.output/"closed.json", content.canonical(dict(pid=self.process.pid, exit_code=self.process.returncode,
            reader_error=self.reader_error, requests=self.sequence, native_gameplay_client=False)))


def execute(session, command):
    require(isinstance(command, dict), "Command object required")
    op = command.get("op")
    transaction = integer(command.get("transaction", 0))
    if op == "begin":
        require(set(command) <= {"op", "base"}, "Unexpected begin field")
        base = command.get("base")
        if base is None:
            base = session.success("current")["identity"]
        return reportable(session.rpc("begin", identity=base))
    if op in ("current", "commit", "cancel"):
        require(set(command) <= {"op", "transaction"}, "Unexpected control field")
        return reportable(session.rpc(op, transaction))
    if op in ("upload", "ready"):
        require(set(command) == {"op", "transaction", "path"}, "Content file command fields differ")
        result = getattr(session, op)(transaction, command["path"])
        return reportable(result)
    if op == "wait":
        require(set(command) <= {"op", "transaction", "event", "seconds"}, "Unexpected wait field")
        seconds = integer(command.get("seconds", 90), 32)
        require(1 <= seconds <= 90 and command.get("event") in ("load_event", "published_event", "cancelled_event"), "Invalid event wait")
        return reportable(session.event(command["event"], transaction, seconds))
    raise ValueError("Unsupported private editor control command")
