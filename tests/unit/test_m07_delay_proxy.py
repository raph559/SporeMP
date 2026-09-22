"""HOST real loopback/TLS checks; no SPORE, packet-loss or native claim."""
import asyncio
import importlib.util
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

PATH = Path(__file__).resolve().parents[2] / "tools/native/m07-delay-proxy.py"
spec = importlib.util.spec_from_file_location("m07_delay_proxy", PATH)
proxy_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(proxy_module)


class DelayProfileTests(unittest.TestCase):
    def test_rejects_unbounded_or_misleading_configuration(self):
        for delay, jitter, seed in ((-1, 0, 0), (1001, 0, 0), (1, 2, 0), (1, -1, 0),
                                  (float("nan"), 0, 0), (1, float("inf"), 0), (True, 0, 0),
                                  (1, 0, -1), (1, 0, 2**32), (1, 0, True)):
            with self.subTest(values=(delay, jitter, seed)), self.assertRaises(ValueError):
                proxy_module.profile(delay, jitter, seed)
        profile = proxy_module.profile(50, 10, 123)
        self.assertEqual(profile["packet_loss"], "NOT_IMPLEMENTED")
        self.assertEqual(profile["rtt_ms"], "NOT_INFERRED")
        self.assertFalse(profile["tls_decryption"])
        for port in (0, 65536, True):
            with self.assertRaises(ValueError):
                proxy_module.DelayProxy(port, 1, 0, 1, lambda _: None)
        for maximum in (0, 9, True):
            with self.assertRaises(ValueError):
                proxy_module.DelayProxy(27060, 1, 0, 1, lambda _: None, maximum)


class DelaySocketTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.server_tasks = set()
        async def echo(reader, writer):
            task = asyncio.current_task()
            self.server_tasks.add(task)
            try:
                # Respond only after client EOF: verifies half-close preservation.
                received = await reader.read()
                writer.write(received)
                await writer.drain()
                writer.write_eof()
            finally:
                writer.close()
                await asyncio.wait_for(writer.wait_closed(), 2)
                self.server_tasks.discard(task)
        self.server = await asyncio.start_server(echo, "127.0.0.1", 0)
        self.target = self.server.sockets[0].getsockname()[1]
        self.events = []
        self.proxy = proxy_module.DelayProxy(self.target, 8, 3, 734, self.events.append)
        self.port = await self.proxy.start(0)

    async def asyncTearDown(self):
        await self.proxy.close()
        self.server.close()
        tasks = list(self.server_tasks)
        for task in tasks:
            task.cancel()
        if tasks:
            await asyncio.wait_for(asyncio.gather(*tasks, return_exceptions=True), 3)
        await asyncio.wait_for(self.server.wait_closed(), 3)

    async def test_two_clients_preserve_bytes_order_eof_and_payload_secrecy(self):
        secret = b"credential=DO_NOT_LOG_TEST_SECRET"
        payloads = [secret + random.Random(seed).randbytes(70000) for seed in (3, 4)]
        async def exchange(payload):
            reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
            writer.write(payload)
            await writer.drain()
            writer.write_eof()
            observed = await asyncio.wait_for(reader.read(), 4)
            writer.close()
            await writer.wait_closed()
            return observed
        self.assertEqual(await asyncio.gather(*(exchange(x) for x in payloads)), payloads)
        await asyncio.sleep(.02)
        self.assertEqual(self.proxy.accepted, 2)
        self.assertEqual(self.proxy.bytes["client_to_server"], sum(map(len, payloads)))
        self.assertEqual(self.proxy.bytes["server_to_client"], sum(map(len, payloads)))
        chunks = [event for event in self.events if event["event"] == "chunk_relayed"]
        self.assertGreaterEqual(len(chunks), 20)
        self.assertTrue(all(5 <= event["requested_delay_ms"] <= 11 for event in chunks))
        self.assertTrue(all(event["observed_before_write_ms"] + .5 >= event["requested_delay_ms"] for event in chunks))
        encoded = json.dumps(self.events)
        self.assertNotIn(secret.decode(), encoded)
        self.assertNotIn("payload", encoded)
        self.assertEqual(self.proxy.errors, [])

    async def test_capacity_refusal_and_shutdown_close_owned_connections(self):
        self.proxy.maximum = 1
        reader1, writer1 = await asyncio.open_connection("127.0.0.1", self.port)
        await asyncio.sleep(.02)
        reader2, writer2 = await asyncio.open_connection("127.0.0.1", self.port)
        self.assertEqual(await asyncio.wait_for(reader2.read(1), 1), b"")
        self.assertEqual(self.proxy.refused, 1)
        writer2.close()
        await writer2.wait_closed()
        await self.proxy.close()
        self.assertEqual(await asyncio.wait_for(reader1.read(1), 1), b"")
        writer1.close()
        await writer1.wait_closed()
        self.assertEqual(self.proxy.active, 0)
        self.assertFalse(self.proxy.tasks)

    async def test_blocked_write_applies_backpressure_without_read_ahead(self):
        entered = asyncio.Event()
        release = asyncio.Event()
        class Reader:
            reads = 0
            async def read(self, maximum):
                self.reads += 1
                self.requested_maximum = maximum
                return b"x" * maximum if self.reads == 1 else b""
        class Writer:
            def write(self, data):
                self.data = data
            async def drain(self):
                entered.set()
                await release.wait()
            def can_write_eof(self):
                return False
        reader, writer = Reader(), Writer()
        task = asyncio.create_task(self.proxy.relay(reader, writer, 123, "client_to_server", random.Random(4)))
        try:
            await asyncio.wait_for(entered.wait(), 1)
            await asyncio.sleep(.02)
            self.assertEqual(reader.reads, 1)
            self.assertEqual(reader.requested_maximum, proxy_module.CHUNK_BYTES)
            self.assertEqual(len(writer.data), proxy_module.CHUNK_BYTES)
            self.assertEqual(self.proxy.bytes["client_to_server"], 0)
            release.set()
            await asyncio.wait_for(task, 1)
            self.assertEqual(reader.reads, 2)
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)

    async def test_relay_loop_and_invalid_listen_port_refused(self):
        for port in (self.target, -1, 65536, True):
            with self.assertRaises(ValueError):
                await self.proxy.start(port)

    async def test_cancel_during_cleanup_closes_both_transports_and_retires_task(self):
        entered = asyncio.Event()
        release = asyncio.Event()
        class Reader:
            async def read(self, maximum):
                return b""
        class Writer:
            def __init__(self, delayed=False):
                self.closed = False
                self.delayed = delayed
            def can_write_eof(self):
                return True
            def write_eof(self):
                pass
            async def drain(self):
                pass
            def close(self):
                self.closed = True
            async def wait_closed(self):
                if self.delayed:
                    entered.set()
                    await release.wait()
        client, upstream = Writer(delayed=True), Writer()
        with patch.object(proxy_module.asyncio, "open_connection", return_value=(Reader(), upstream)):
            task = asyncio.create_task(self.proxy.accept(Reader(), client))
            try:
                await asyncio.wait_for(entered.wait(), 1)
                task.cancel()
                await asyncio.wait_for(asyncio.gather(task, return_exceptions=True), 1)
                self.assertTrue(client.closed)
                self.assertTrue(upstream.closed)
                self.assertEqual(self.proxy.active, 0)
                self.assertNotIn(task, self.proxy.tasks)
            finally:
                release.set()
                task.cancel()
                await asyncio.gather(task, return_exceptions=True)

    async def test_empty_stop_request_seals_nonsecret_report(self):
        with tempfile.TemporaryDirectory(prefix="m07-delay-host-") as directory:
            output = Path(directory)
            (output / "stop.request").touch()
            args = SimpleNamespace(target_port=self.target, listen_port=0, delay_ms=5,
                                   jitter_ms=0, seed=1, seconds=2)
            self.assertEqual(await proxy_module.operate(args, output), 0)
            report = json.loads((output / "report.json").read_text())
            self.assertEqual(report["stop_reason"], "stop_request")
            self.assertEqual(report["connections_accepted"], 0)
            self.assertEqual(report["profile"]["packet_loss"], "NOT_IMPLEMENTED")
            self.assertTrue(report["all_connections_closed"])
            self.assertFalse(report["native_game_started"])
            self.assertEqual(report["milestone_acceptance"], "NOT_VERIFIED")


COORDINATOR = PATH.parents[2] / "build/win32/Release/SporeMP.Coordinator.exe"


@unittest.skipUnless(os.name == "nt" and COORDINATOR.is_file(), "Built Windows coordinator required for real Schannel HOST check")
class DelayTlsTests(unittest.IsolatedAsyncioTestCase):
    async def test_existing_schannel_peer_authenticates_through_opaque_proxy(self):
        with tempfile.TemporaryDirectory(prefix="m07-delay-tls-host-") as directory:
            root = Path(directory)
            config, private = root / "server.conf", root / "private"
            config.write_text("schema=2\nhost=127.0.0.1\nport=0\n" + "".join(
                f"{name}_sha256={digit * 64}\n" for name, digit in
                (("build", "1"), ("executable", "2"), ("content", "3"), ("fixture", "4"),
         ("world_satiria", "4"), ("world_planet_records", "4"), ("world_planet_records_temp", "4"),
         ("world_planet_scripts", "4"), ("world_stars", "4"), ("world_planets", "4"))))
            server = await asyncio.create_subprocess_exec(
                str(COORDINATOR), "--serve", "--config", str(config), "--output", str(private),
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
                creationflags=subprocess.CREATE_NO_WINDOW)
            relay = None
            client = None
            try:
                ready = json.loads(await asyncio.wait_for(server.stdout.readline(), 10))
                self.assertTrue(ready["ready"])
                events = []
                relay = proxy_module.DelayProxy(ready["port"], 25, 5, 731, events.append)
                proxy_port = await relay.start(0)
                original = (private / "player-1.conf").read_text()
                rewritten = "\n".join(f"port={proxy_port}" if line.startswith("port=") else line
                                      for line in original.splitlines()) + "\n"
                peer = private / "proxy-player.conf"
                peer.write_text(rewritten)
                client = await asyncio.create_subprocess_exec(
                    str(COORDINATOR), "--client-probe", "--config", str(peer),
                    stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
                    creationflags=subprocess.CREATE_NO_WINDOW)
                stdout, stderr = await asyncio.wait_for(client.communicate(), 15)
                self.assertEqual(client.returncode, 0, stderr.decode(errors="replace"))
                result = json.loads(stdout)
                self.assertTrue(result["authenticated"])
                self.assertEqual(result["player"], 1)
                self.assertEqual(result["tls"], "1.2")
                self.assertFalse(result["native_baseline_applied"])
                self.assertTrue(all(count > 0 for count in relay.bytes.values()))
                self.assertGreater(len([x for x in events if x["event"] == "chunk_relayed"]), 2)
                credentials = [line.split("=", 1)[1] for line in original.splitlines() if line.startswith("credential=")]
                self.assertTrue(credentials)
                self.assertTrue(all(value not in json.dumps(events) for value in credentials))
            finally:
                if client is not None and client.returncode is None:
                    client.kill()
                    await client.wait()
                if relay is not None:
                    await relay.close()
                if server.returncode is None:
                    if private.is_dir():
                        (private / "stop.request").touch()
                    try:
                        await asyncio.wait_for(server.wait(), 10)
                    except asyncio.TimeoutError:
                        server.kill()
                        await server.wait()
            self.assertEqual(server.returncode, 0)


if __name__ == "__main__":
    unittest.main()
