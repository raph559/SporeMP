"""Bounded loopback TCP byte relay for developer delay/jitter experiments.

This delays TCP read chunks in each direction, preserving their order and bytes.
It does NOT simulate packet loss, TCP retransmission, or an Internet path. It
never decrypts TLS, logs payloads, launches SPORE, or changes network settings.
"""
from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import math
from pathlib import Path
import random
import sys
import time

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools/diagnostics"))
import sporemp_diag as diag

CHUNK_BYTES = 16384
MAX_EVENTS = 100000


def bounded_number(value, minimum, maximum, name):
    if type(value) not in (int, float) or not math.isfinite(value) or not minimum <= value <= maximum:
        raise ValueError(f"Invalid bounded {name}")
    return value


def profile(delay_ms, jitter_ms, seed):
    bounded_number(delay_ms, 0, 1000, "one-way read-chunk delay")
    bounded_number(jitter_ms, 0, delay_ms, "uniform jitter")
    if type(seed) is not int or not 0 <= seed < 2**32:
        raise ValueError("Seed must be a uint32")
    return {"delay_ms_per_direction": delay_ms, "jitter_ms_uniform_plus_minus": jitter_ms,
            "seed": seed, "unit": "TCP_READ_CHUNK", "packet_loss": "NOT_IMPLEMENTED",
            "rtt_ms": "NOT_INFERRED", "ordering": "PRESERVED", "tls_decryption": False}


class DelayProxy:
    def __init__(self, target_port, delay_ms, jitter_ms, seed, record, max_connections=8):
        if type(target_port) is not int or not 1 <= target_port <= 65535:
            raise ValueError("Invalid target port")
        if type(max_connections) is not int or not 1 <= max_connections <= 8:
            raise ValueError("Invalid bounded connection limit")
        self.profile = profile(delay_ms, jitter_ms, seed)
        self.target_port = target_port
        self.record = record
        self.maximum = max_connections
        self.server = None
        self.tasks = set()
        self.active = 0
        self.accepted = 0
        self.refused = 0
        self.errors = []
        self.stopping = False
        self.bytes = {"client_to_server": 0, "server_to_client": 0}
        self.started_ns = time.perf_counter_ns()
        self.failed = asyncio.Event()

    def emit(self, event, **fields):
        try:
            self.record({"schema_version": 1, "event": event,
                         "proxy_elapsed_ns": time.perf_counter_ns() - self.started_ns, **fields})
        except (OSError, ValueError):
            self.failed.set()
            raise

    async def start(self, listen_port):
        if type(listen_port) is not int or not 0 <= listen_port <= 65535 or listen_port == self.target_port:
            raise ValueError("Invalid listen port or relay loop")
        self.server = await asyncio.start_server(self.accept, "127.0.0.1", listen_port, limit=CHUNK_BYTES)
        actual = self.server.sockets[0].getsockname()[1]
        self.emit("listening", listen_host="127.0.0.1", listen_port=actual,
                  target_host="127.0.0.1", target_port=self.target_port, profile=self.profile)
        return actual

    async def relay(self, reader, writer, connection, direction, rng):
        index = 0
        while True:
            data = await reader.read(CHUNK_BYTES)
            if not data:
                if writer.can_write_eof():
                    writer.write_eof()
                    await asyncio.wait_for(writer.drain(), 5)
                self.emit("direction_eof", connection=connection, direction=direction, chunks=index)
                return
            index += 1
            received_ns = time.perf_counter_ns()
            configured = self.profile["delay_ms_per_direction"]
            jitter = self.profile["jitter_ms_uniform_plus_minus"]
            requested_ms = rng.uniform(configured - jitter, configured + jitter)
            # Windows event-loop timers may wake before a high-resolution
            # deadline. Recheck QPC, recording actual delay rather than assuming
            # that one asyncio sleep establishes the requested minimum.
            due_ns = received_ns + int(requested_ms * 1e6)
            while (remaining_ns := due_ns - time.perf_counter_ns()) > 0:
                await asyncio.sleep(remaining_ns / 1e9)
            write_ns = time.perf_counter_ns()
            writer.write(data)
            await asyncio.wait_for(writer.drain(), 5)
            self.bytes[direction] += len(data)
            self.emit("chunk_relayed", connection=connection, direction=direction, chunk=index,
                      bytes=len(data), requested_delay_ms=requested_ms,
                      observed_before_write_ms=(write_ns - received_ns) / 1e6,
                      observed_through_drain_ms=(time.perf_counter_ns() - received_ns) / 1e6)

    async def accept(self, client_reader, client_writer):
        task = asyncio.current_task()
        self.tasks.add(task)
        upstream_writer = None
        relays = []
        admitted = False
        connection = 0
        try:
            if self.stopping or self.active >= self.maximum:
                self.refused += 1
                self.emit("connection_refused", reason="stopping" if self.stopping else "connection_limit")
                return
            self.active += 1
            admitted = True
            self.accepted += 1
            connection = self.accepted
            self.emit("connection_accepted", connection=connection)
            upstream_reader, upstream_writer = await asyncio.wait_for(
                asyncio.open_connection("127.0.0.1", self.target_port, limit=CHUNK_BYTES), 3)
            for direction, reader, writer, bit in (
                    ("client_to_server", client_reader, upstream_writer, 0),
                    ("server_to_client", upstream_reader, client_writer, 1)):
                rng = random.Random((self.profile["seed"] << 32) + connection * 2 + bit)
                relays.append(asyncio.create_task(self.relay(reader, writer, connection, direction, rng)))
            await asyncio.gather(*relays)
            self.emit("connection_closed", connection=connection, reason="both_directions_eof")
        except asyncio.CancelledError:
            try:
                self.emit("connection_closed", connection=connection, reason="bounded_proxy_shutdown")
            except (OSError, ValueError):
                pass
            raise
        except (OSError, ValueError, RuntimeError, asyncio.TimeoutError) as error:
            # Only the exception class is retained; no payload/credential text.
            self.errors.append({"connection": connection, "error_type": type(error).__name__})
            self.failed.set()
            try:
                self.emit("connection_error", **self.errors[-1])
            except (OSError, ValueError):
                pass
        finally:
            for relay in relays:
                if not relay.done():
                    relay.cancel()
            # Close both transports before the first await. close() may cancel
            # this handler while an earlier EOF/error is already unwinding it;
            # awaiting the client before closing upstream would leak upstream.
            for writer in (client_writer, upstream_writer):
                if writer is not None:
                    writer.close()
            try:
                if relays:
                    await asyncio.gather(*relays, return_exceptions=True)
                for writer in (client_writer, upstream_writer):
                    if writer is not None:
                        try:
                            await asyncio.wait_for(writer.wait_closed(), 2)
                        except (OSError, asyncio.TimeoutError):
                            pass
            finally:
                if admitted:
                    self.active -= 1
                self.tasks.discard(task)

    async def close(self):
        self.stopping = True
        if self.server is not None:
            self.server.close()
            # Python 3.12+ wait_closed also waits for accepted transports.
            # Stop/cancel their owners first; waiting here deadlocks while a
            # connected client is legitimately awaiting its peer's EOF.
            await asyncio.sleep(0)
        tasks = list(self.tasks)
        for task in tasks:
            task.cancel()
        if tasks:
            await asyncio.wait_for(asyncio.gather(*tasks, return_exceptions=True), 6)
        if self.server is not None:
            await asyncio.wait_for(self.server.wait_closed(), 3)


async def operate(args, output):
    report = {"schema_version": 1, "evidence_class": "HOST_TCP_READ_CHUNK_DELAY_PROXY",
              "native_game_started": False, "desktop_input": False, "milestone_acceptance": "NOT_VERIFIED",
              "profile": profile(args.delay_ms, args.jitter_ms, args.seed),
              "seconds_limit": args.seconds, "operation_completed": False,
              "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    count = 0
    with (output / "events.jsonl").open("x", encoding="utf-8", newline="\n") as stream:
        def record(event):
            nonlocal count
            if count >= MAX_EVENTS:
                raise ValueError("Proxy evidence budget exhausted")
            count += 1
            stream.write(json.dumps(event, allow_nan=False, separators=(",", ":")) + "\n")
            stream.flush()
        proxy = DelayProxy(args.target_port, args.delay_ms, args.jitter_ms, args.seed, record)
        try:
            report["listen_port"] = await proxy.start(args.listen_port)
            report["target_port"] = args.target_port
            print(json.dumps({"listening": "127.0.0.1", "port": report["listen_port"],
                              "packet_loss": "NOT_IMPLEMENTED", "output": str(output)}), flush=True)
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline and not proxy.failed.is_set():
                stop = diag.no_reparse(output / "stop.request")
                if stop.exists():
                    if not stop.is_file() or stop.stat().st_size:
                        raise ValueError("Stop request must be an empty regular file")
                    report["stop_reason"] = "stop_request"
                    break
                await asyncio.sleep(min(.1, max(0, deadline - time.monotonic())))
            report.setdefault("stop_reason", "connection_error" if proxy.failed.is_set() else "duration_limit")
            report["operation_completed"] = not proxy.failed.is_set()
        except (OSError, ValueError, RuntimeError) as error:
            report["error_type"] = type(error).__name__
        finally:
            await proxy.close()
            report["operation_completed"] = report["operation_completed"] and not proxy.failed.is_set()
            report.update(connections_accepted=proxy.accepted, connections_refused=proxy.refused,
                          bytes_relayed=proxy.bytes, errors=proxy.errors, event_count=count,
                          all_connections_closed=not proxy.tasks and proxy.active == 0)
    raw = (output / "events.jsonl").read_bytes()
    report["events_sha256"] = hashlib.sha256(raw).hexdigest()
    report["events_bytes"] = len(raw)
    (output / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return 0 if report["operation_completed"] and report["all_connections_closed"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen-port", type=int, required=True)
    parser.add_argument("--target-port", type=int, required=True)
    parser.add_argument("--delay-ms", type=float, required=True)
    parser.add_argument("--jitter-ms", type=float, default=0)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--seconds", type=float, default=180)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        profile(args.delay_ms, args.jitter_ms, args.seed)
        bounded_number(args.seconds, 1, 600, "run duration")
        for port in (args.listen_port, args.target_port):
            if not 1024 <= port <= 65535:
                raise ValueError("CLI ports must be in 1024..65535")
        if args.listen_port == args.target_port:
            raise ValueError("Listen and target ports must differ")
        output = diag.no_reparse(args.output.absolute())
        if not output.is_relative_to(REPO / "local"):
            raise ValueError("Proxy output must stay in the ignored repository local directory")
        output.mkdir(parents=True, exist_ok=False)
        return asyncio.run(operate(args, output))
    except (OSError, ValueError) as error:
        parser.exit(2, str(error) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
