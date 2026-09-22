"""Developer-only bounded IPv4 localhost TCP packet delay/loss via WinDivert 2.2.

Run through m07-packet-impairment.ps1, which verifies the signed dependency,
refuses preexisting driver services, enforces a process deadline, and removes
the owned driver afterward. Importing this module never loads a driver.
Payloads and credentials are never logged. This does not launch native SPORE.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import heapq
import json
import math
import os
from pathlib import Path
import random
import socket
import struct
import subprocess
import sys
import threading
import time

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools/diagnostics"))
import sporemp_diag as diag

DLL_SHA256 = "c1e060ee19444a259b2162f8af0f3fe8c4428a1c6f694dce20de194ac8d7d9a2"
DRIVER_SHA256 = "8da085332782708d8767bcace5327a6ec7283c17cfb85e40b03cd2323a90ddc2"
MAX_PACKETS = 1000000
MAX_PENDING = 1024
MAX_PENDING_BYTES = 8 * 1024 * 1024


def evidence_limits():
    return {"captured_packets": MAX_PACKETS, "pending_packets": MAX_PENDING,
            "pending_bytes": MAX_PENDING_BYTES, "metadata_records": MAX_PACKETS + MAX_PENDING}


def configuration(port, delay_ms, jitter_ms, loss_percent, seed, seconds):
    if type(port) is not int or not 1024 <= port <= 65535:
        raise ValueError("A dedicated unprivileged TCP port is required")
    for name, value, lo, hi in (("delay", delay_ms, 0, 1000),
                               ("jitter", jitter_ms, 0, delay_ms),
                               ("loss", loss_percent, 0, 10),
                               ("seconds", seconds, 1, 600)):
        if type(value) not in (int, float) or not math.isfinite(value) or not lo <= value <= hi:
            raise ValueError("Invalid bounded " + name)
    if type(seed) is not int or not 0 <= seed < 2**32:
        raise ValueError("Seed must be uint32")
    return {"port": port, "delay_ms": delay_ms, "jitter_ms": jitter_ms,
            "loss_percent": loss_percent, "seed": seed, "seconds": seconds,
            "filter": ("outbound and loopback and ip and tcp and "
                       "ip.SrcAddr == 127.0.0.1 and ip.DstAddr == 127.0.0.1 and "
                       f"(tcp.SrcPort == {port} or tcp.DstPort == {port})"),
            "unit": "IP_PACKET", "layer": "WINDIVERT_LAYER_NETWORK",
            "rtt_ms": "NOT_INFERRED", "tls_decryption": False,
            "packet_order": "JITTER_MAY_REORDER", "native_game_started": False}


def tcp_metadata(packet, port):
    """Parse only bounded IPv4/TCP headers; never expose body bytes or hashes."""
    if len(packet) < 40 or packet[0] >> 4 != 4:
        raise ValueError("Expected IPv4 TCP packet")
    ip_size = (packet[0] & 15) * 4
    total = struct.unpack_from("!H", packet, 2)[0]
    fragment = struct.unpack_from("!H", packet, 6)[0]
    if (ip_size < 20 or total != len(packet) or ip_size + 20 > total or
            packet[9] != 6 or fragment & 0x3fff or
            packet[12:20] != b"\x7f\0\0\1\x7f\0\0\1"):
        raise ValueError("Packet outside bounded loopback TCP contract")
    source, target, sequence, acknowledgment = struct.unpack_from("!HHII", packet, ip_size)
    tcp_size = (packet[ip_size + 12] >> 4) * 4
    if tcp_size < 20 or ip_size + tcp_size > total or (source == port) == (target == port):
        raise ValueError("Packet outside dedicated-port contract")
    return {"direction": "server_to_client" if source == port else "client_to_server",
            "source_port": source, "target_port": target, "tcp_sequence": sequence,
            "tcp_acknowledgment": acknowledgment, "tcp_flags": packet[ip_size + 13],
            "tcp_data_bytes": total - ip_size - tcp_size, "ip_bytes": total}


class PacketPolicy:
    def __init__(self, profile, force_first_data_drop=False):
        self.profile = profile
        self.rng = random.Random(profile["seed"])
        self.forced = set()
        self.force_first_data_drop = force_first_data_drop
        self.dropped_ranges = set()

    def decide(self, metadata):
        # Identical sequence/length reappearance is a conservative retransmission
        # observation, not a claim that all segmented retransmissions are counted.
        key = tuple(metadata[name] for name in ("source_port", "target_port", "tcp_sequence", "tcp_data_bytes"))
        repeated = bool(metadata["tcp_data_bytes"] and key in self.dropped_ranges)
        # Test each distinct TCP connection, including the Schannel connection
        # after the byte-echo check. Directions alone would only impair echo.
        flow_direction = (metadata["source_port"], metadata["target_port"])
        forced = bool(self.force_first_data_drop and metadata["tcp_data_bytes"] and flow_direction not in self.forced)
        if forced:
            self.forced.add(flow_direction)
        dropped = forced or self.rng.random() * 100 < self.profile["loss_percent"]
        delay = self.profile["delay_ms"] + self.rng.uniform(-self.profile["jitter_ms"], self.profile["jitter_ms"])
        if dropped and metadata["tcp_data_bytes"]:
            self.dropped_ranges.add(key)
        return dropped, delay, repeated, forced


class PendingPackets:
    """Bounded scheduler; overflow is an explicit failed run, never hidden loss."""
    def __init__(self, maximum=MAX_PENDING, maximum_bytes=MAX_PENDING_BYTES):
        self.heap = []
        self.bytes = 0
        self.maximum = maximum
        self.maximum_bytes = maximum_bytes

    def push(self, deadline_ns, index, packet, address, metadata):
        if len(self.heap) >= self.maximum or self.bytes + len(packet) > self.maximum_bytes:
            raise OverflowError("Packet queue bound exhausted")
        heapq.heappush(self.heap, (deadline_ns, index, packet, address, metadata))
        self.bytes += len(packet)

    def pop(self):
        item = heapq.heappop(self.heap)
        self.bytes -= len(item[2])
        return item


class Address(ctypes.Structure):
    # Pinned windivert.h: INT64, UINT32 bitfield storage, UINT32, 64-byte union.
    _fields_ = [("timestamp", ctypes.c_int64), ("flags", ctypes.c_uint32),
                ("reserved", ctypes.c_uint32), ("data", ctypes.c_ubyte * 64)]


class WinDivert:
    def __init__(self, package):
        if os.name != "nt" or ctypes.sizeof(ctypes.c_void_p) != 8 or ctypes.sizeof(Address) != 80:
            raise ValueError("64-bit Windows/Python and 80-byte address ABI required")
        package = diag.no_reparse(package)
        for name, expected in (("WinDivert.dll", DLL_SHA256), ("WinDivert64.sys", DRIVER_SHA256)):
            if diag.fingerprint(package / "x64" / name)["sha256"] != expected:
                raise ValueError("Pinned WinDivert dependency mismatch")
        self.dll = ctypes.WinDLL(str(package / "x64/WinDivert.dll"), use_last_error=True)
        signatures = {
            "WinDivertOpen": (ctypes.c_void_p, [ctypes.c_char_p, ctypes.c_int, ctypes.c_int16, ctypes.c_uint64]),
            "WinDivertRecv": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(Address)]),
            "WinDivertSend": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(Address)]),
            "WinDivertShutdown": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_int]),
            "WinDivertClose": (ctypes.c_int, [ctypes.c_void_p]),
            "WinDivertSetParam": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint64]),
            "WinDivertHelperCalcChecksums": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(Address), ctypes.c_uint64]),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self.dll, name)
            function.restype, function.argtypes = result, arguments
        self.handle = None

    def open(self, filter_text):
        self.handle = self.dll.WinDivertOpen(filter_text.encode("ascii"), 0, 0, 0)
        if self.handle in (None, ctypes.c_void_p(-1).value):
            self.handle = None
            raise ctypes.WinError(ctypes.get_last_error())
        # Maximize the kernel receive buffer while bounding our own user queue.
        # WinDivert exposes no kernel-overflow counter; do not infer its absence.
        for parameter, value in ((0, 16384), (1, 16000), (2, 33554432)):
            if not self.dll.WinDivertSetParam(self.handle, parameter, value):
                raise ctypes.WinError(ctypes.get_last_error())

    def recv(self):
        packet = ctypes.create_string_buffer(65535)
        size, address = ctypes.c_uint(), Address()
        if not self.dll.WinDivertRecv(self.handle, packet, len(packet), ctypes.byref(size), ctypes.byref(address)):
            error = ctypes.get_last_error()
            if error in (232, 995):  # drained shutdown, or cancellation during close
                return None
            raise ctypes.WinError(error)
        if not address.flags & (1 << 17) or not address.flags & (1 << 18) or address.flags & (1 << 20):
            raise ValueError("Unexpected direction/loopback/address family")
        return packet.raw[:size.value], address

    def send(self, packet, address):
        buffer, sent = ctypes.create_string_buffer(packet, len(packet)), ctypes.c_uint()
        # Only checksum fields change; TCP payload/order are untouched. Required
        # because outbound checksum offload may leave invalid captured checksums.
        if not self.dll.WinDivertHelperCalcChecksums(buffer, len(packet), ctypes.byref(address), 0):
            raise ValueError("Could not calculate packet checksums")
        if not self.dll.WinDivertSend(self.handle, buffer, len(packet), ctypes.byref(sent), ctypes.byref(address)):
            raise ctypes.WinError(ctypes.get_last_error())
        if sent.value != len(packet):
            raise OSError("Incomplete packet reinjection")

    def shutdown_receive(self):
        if self.handle and not self.dll.WinDivertShutdown(self.handle, 1):
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.handle:
            handle, self.handle = self.handle, None
            if not self.dll.WinDivertClose(handle):
                raise ctypes.WinError(ctypes.get_last_error())


class PacketEngine:
    def __init__(self, backend, profile, record, force_first_data_drop=False):
        self.backend, self.profile, self.record = backend, profile, record
        self.policy = PacketPolicy(profile, force_first_data_drop)
        self.condition = threading.Condition()
        self.pending = PendingPackets()
        self.stop = threading.Event()
        self.receiver = None
        self.errors = []
        self.counts = {name: 0 for name in ("captured", "dropped", "reinjected", "retransmitted_dropped_data", "shutdown_flush", "overflow_bypass")}
        self.directions = {name: {"captured": 0, "dropped": 0, "reinjected": 0} for name in ("client_to_server", "server_to_client")}
        # Every delay remains in the streamed packet log. Only extrema are
        # needed in the final report; do not retain a million float samples.
        self.minimum_delay_ms = None
        self.maximum_delay_ms = None

    def fail(self, error):
        self.errors.append(type(error).__name__)
        self.stop.set()
        with self.condition:
            self.condition.notify_all()

    def receive(self):
        try:
            while True:
                received = self.backend.recv()
                if received is None:
                    break
                packet, address = received
                now = time.perf_counter_ns()
                metadata = tcp_metadata(packet, self.profile["port"])
                self.counts["captured"] += 1
                index = self.counts["captured"]
                self.directions[metadata["direction"]]["captured"] += 1
                dropped, delay, repeated, forced = self.policy.decide(metadata)
                self.counts["retransmitted_dropped_data"] += int(repeated)
                metadata.update(index=index, received_ns=now, requested_delay_ms=delay,
                                repeats_dropped_data_range=repeated, forced_host_drop=forced)
                if index > MAX_PACKETS:
                    self.fail(OverflowError("Evidence bound exhausted"))
                # During shutdown all captured kernel-queue packets are flushed.
                if dropped and not self.stop.is_set():
                    self.counts["dropped"] += 1
                    self.directions[metadata["direction"]]["dropped"] += 1
                    self.record({"event": "packet_dropped", **metadata})
                    continue
                with self.condition:
                    try:
                        self.pending.push(now + int(delay * 1e6), index, packet, address, metadata)
                    except OverflowError as error:
                        self.fail(error)
                        self.backend.send(packet, address)
                        self.counts["reinjected"] += 1
                        self.directions[metadata["direction"]]["reinjected"] += 1
                        self.counts["overflow_bypass"] += 1
                    self.condition.notify_all()
        except BaseException as error:
            self.fail(error)
        finally:
            with self.condition:
                self.condition.notify_all()

    def start(self):
        self.backend.open(self.profile["filter"])
        self.receiver = threading.Thread(target=self.receive, name="bounded-windivert-receiver", daemon=True)
        self.receiver.start()

    def deliver(self, item, shutdown=False):
        _, _, packet, address, metadata = item
        self.backend.send(packet, address)
        observed = (time.perf_counter_ns() - metadata["received_ns"]) / 1e6
        self.counts["reinjected"] += 1
        self.directions[metadata["direction"]]["reinjected"] += 1
        self.counts["shutdown_flush"] += int(shutdown)
        self.minimum_delay_ms = observed if self.minimum_delay_ms is None else min(self.minimum_delay_ms, observed)
        self.maximum_delay_ms = observed if self.maximum_delay_ms is None else max(self.maximum_delay_ms, observed)
        self.record({"event": "packet_reinjected", **metadata,
                     "observed_delay_ms": observed, "shutdown_flush": shutdown})

    def pump(self, deadline, stop_file):
        while time.monotonic() < deadline and not self.stop.is_set():
            if stop_file.exists():
                diag.no_reparse(stop_file)
                if not stop_file.is_file() or stop_file.stat().st_size:
                    raise ValueError("Stop request must be an empty regular file")
                return "stop_request"
            with self.condition:
                now = time.perf_counter_ns()
                if self.pending.heap and self.pending.heap[0][0] <= now:
                    item = self.pending.pop()
                else:
                    wait = min(.05, max(0, deadline - time.monotonic()))
                    if self.pending.heap:
                        wait = min(wait, max(0, (self.pending.heap[0][0] - now) / 1e9))
                    self.condition.wait(wait)
                    continue
            self.deliver(item)
        return "error" if self.errors else "duration_limit"

    def close(self):
        self.stop.set()
        try:
            self.backend.shutdown_receive()
            # Shutdown RECV prevents new captures; queued kernel packets remain
            # readable until NO_DATA. The receiver must drain them before close.
            if self.receiver:
                self.receiver.join(3)
                if self.receiver.is_alive():
                    raise TimeoutError("Packet receiver failed to stop")
            while self.pending.heap:
                self.deliver(self.pending.pop(), shutdown=True)
        finally:
            self.backend.close()


def host_exchange(port, stop_file, result, include_tls=False):
    """Actual TCP self-test; first data drop each way forces retransmission."""
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    server_errors = []
    def echo():
        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(18)
                chunks = []
                while data := connection.recv(16384):
                    chunks.append(data)
                connection.sendall(b"".join(chunks))
                connection.shutdown(socket.SHUT_WR)
        except BaseException as error:
            server_errors.append(type(error).__name__)
    try:
        listener.bind(("127.0.0.1", port))
        listener.listen(1)
        listener.settimeout(20)
        server = threading.Thread(target=echo, daemon=True)
        server.start()
        payload = random.Random(834).randbytes(128 * 1024)
        started = time.monotonic()
        with socket.create_connection(("127.0.0.1", port), timeout=18) as client:
            client.settimeout(18)
            client.sendall(payload)
            client.shutdown(socket.SHUT_WR)
            chunks = []
            while data := client.recv(16384):
                chunks.append(data)
        server.join(2)
        result.update(tcp_equal=b"".join(chunks) == payload, tcp_bytes=len(payload),
                      half_close=True, tcp_elapsed_seconds=time.monotonic() - started,
                      server_closed=not server.is_alive(), server_errors=server_errors)
        listener.close()
        if include_tls:
            result["schannel"] = schannel_exchange(port, stop_file.parent)
    except BaseException as error:
        result["error_type"] = type(error).__name__
    finally:
        listener.close()
        stop_file.touch()


def schannel_exchange(port, output):
    coordinator = REPO / "build/win32/Release/SporeMP.Coordinator.exe"
    config, private = output / "host-server.conf", output / "host-private"
    config.write_text(f"schema=2\nhost=127.0.0.1\nport={port}\n" + "".join(
        f"{name}_sha256={digit * 64}\n" for name, digit in
        (("build", "1"), ("executable", "2"), ("content", "3"), ("fixture", "4"),
         ("world_satiria", "4"), ("world_planet_records", "4"), ("world_planet_records_temp", "4"),
         ("world_planet_scripts", "4"), ("world_stars", "4"), ("world_planets", "4"))))
    server = subprocess.Popen([str(coordinator), "--serve", "--config", str(config), "--output", str(private)],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
    result = {}
    try:
        # File readiness avoids an unbounded readline; credentials stay private.
        deadline = time.monotonic() + 8
        while not (private / "player-1.conf").is_file():
            if server.poll() is not None or time.monotonic() > deadline:
                raise TimeoutError("HOST coordinator readiness")
            time.sleep(.02)
        probe = subprocess.run([str(coordinator), "--client-probe", "--config", str(private / "player-1.conf")],
                               capture_output=True, timeout=18, creationflags=subprocess.CREATE_NO_WINDOW)
        reply = json.loads(probe.stdout)
        result.update(exit_code=probe.returncode, authenticated=reply.get("authenticated"),
                      tls=reply.get("tls"), native_baseline_applied=reply.get("native_baseline_applied"))
    finally:
        if private.is_dir():
            (private / "stop.request").touch()
        try:
            server.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.communicate(timeout=3)
        result["server_exit_code"] = server.returncode
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--delay-ms", type=float, required=True)
    parser.add_argument("--jitter-ms", type=float, default=0)
    parser.add_argument("--loss-percent", type=float, required=True)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--seconds", type=float, required=True)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--host-self-test", action="store_true")
    args = parser.parse_args()
    profile = configuration(args.port, args.delay_ms, args.jitter_ms, args.loss_percent, args.seed, args.seconds)
    output = diag.no_reparse(args.output.absolute())
    if not output.is_relative_to(REPO / "local") or not output.is_dir():
        raise ValueError("Wrapper-created output under ignored local required")
    report = {"schema_version": 1, "profile": profile, "evidence_class": "HOST_PACKET_IMPAIRMENT",
              "milestone_acceptance": "NOT_VERIFIED", "tool_sha256": diag.fingerprint(Path(__file__))["sha256"],
              "limits": evidence_limits(),
              "kernel_queue_overflow_counter": "UNAVAILABLE_IN_WINDIVERT_API",
              "forced_first_data_drop_each_connection_direction": args.host_self_test}
    record_lock = threading.Lock()
    count = 0
    with (output / "packets.jsonl").open("x", encoding="utf-8", newline="\n") as stream:
        def record(event):
            nonlocal count
            with record_lock:
                count += 1
                if count > MAX_PACKETS + MAX_PENDING:
                    raise OverflowError("Evidence limit")
                stream.write(json.dumps(event, separators=(",", ":"), allow_nan=False) + "\n")
        engine = PacketEngine(WinDivert(args.package), profile, record, args.host_self_test)
        host = None
        try:
            engine.start()
            (output / "ready.json").write_text(json.dumps({"ready": True, "filter": profile["filter"], "pid": os.getpid(), "limits": evidence_limits()}))
            if args.host_self_test:
                report["host_test"] = {}
                host = threading.Thread(target=host_exchange, args=(args.port, output / "stop.request", report["host_test"], True), daemon=True)
                host.start()
            report["stop_reason"] = engine.pump(time.monotonic() + args.seconds, output / "stop.request")
        except BaseException as error:
            engine.fail(error)
        finally:
            try:
                engine.close()
            except BaseException as error:
                engine.fail(error)
            if host:
                host.join(1)
                if host.is_alive():
                    engine.fail(TimeoutError("HOST thread deadline"))
            report.update(counts=engine.counts, directions=engine.directions, errors=engine.errors,
                          pending_packets=len(engine.pending.heap), receiver_closed=not engine.receiver or not engine.receiver.is_alive(),
                          observed_delay_ms={"minimum": engine.minimum_delay_ms if engine.minimum_delay_ms is not None else 0,
                                             "maximum": engine.maximum_delay_ms if engine.maximum_delay_ms is not None else 0})
    report["events_sha256"] = diag.fingerprint(output / "packets.jsonl")["sha256"]
    report["captured_packet_accounting_balanced"] = engine.counts["captured"] == engine.counts["dropped"] + engine.counts["reinjected"]
    passed = not engine.errors and report["receiver_closed"] and not report["pending_packets"] and report["captured_packet_accounting_balanced"]
    if args.host_self_test:
        host_result = report["host_test"]
        tls = host_result.get("schannel", {})
        passed = passed and host_result.get("tcp_equal") and host_result.get("server_closed") and not host_result.get("server_errors") and tls.get("authenticated") and tls.get("tls") == "1.2" and tls.get("exit_code") == 0 and tls.get("server_exit_code") == 0 and engine.counts["dropped"] >= 2 and engine.counts["retransmitted_dropped_data"] >= 2
    report["completed"] = bool(passed)
    (output / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
