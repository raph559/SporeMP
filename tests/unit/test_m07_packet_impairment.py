"""No driver load: packet scope, byte preservation, scheduling and cleanup."""
import ctypes
import importlib.util
from pathlib import Path
import queue
import struct
import tempfile
import time
import unittest
from unittest.mock import patch

PATH = Path(__file__).resolve().parents[2] / "tools/native/m07-packet-impairment.py"
spec = importlib.util.spec_from_file_location("m07_packet_impairment", PATH)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def packet(payload=b"private application bytes", source=49000, target=27160, sequence=1):
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 40 + len(payload), 1, 0, 64, 6, 0,
                     b"\x7f\0\0\1", b"\x7f\0\0\1")
    tcp = struct.pack("!HHIIBBHHH", source, target, sequence, 0, 0x50, 0x18, 32767, 0, 0)
    return ip + tcp + payload


class PacketPolicyTests(unittest.TestCase):
    def test_rejects_unbounded_nonfinite_and_ambiguous_inputs(self):
        baseline = [27160, 25, 5, 1, 731, 30]
        for index, values in ((0, [0, 80, 65536, True]), (1, [-1, 1001, float("nan"), True]),
                              (2, [-1, 26, float("inf")]), (3, [-1, 11, float("nan")]),
                              (4, [-1, 2**32, True]), (5, [0, 601, float("inf")])):
            for value in values:
                args = baseline.copy()
                args[index] = value
                with self.subTest(args=args), self.assertRaises(ValueError):
                    module.configuration(*args)
        profile = module.configuration(*baseline)
        self.assertIn("ip.SrcAddr == 127.0.0.1 and ip.DstAddr == 127.0.0.1", profile["filter"])
        self.assertIn("tcp.SrcPort == 27160 or tcp.DstPort == 27160", profile["filter"])
        self.assertEqual(profile["unit"], "IP_PACKET")
        self.assertFalse(profile["tls_decryption"])
        self.assertEqual(ctypes.sizeof(module.Address), 80)
        self.assertEqual(module.evidence_limits(), {"captured_packets": 1000000,
                         "pending_packets": 1024, "pending_bytes": 8 * 1024 * 1024,
                         "metadata_records": 1001024})

    def test_header_parser_refuses_foreign_addresses_ports_fragments_and_truncation(self):
        valid = packet()
        self.assertEqual(module.tcp_metadata(valid, 27160)["tcp_data_bytes"], 25)
        malformed = [valid[:39], packet(source=49001,target=49000), packet(source=27160,target=27160)]
        for offset, replacement in ((0,b"\x65"), (2,b"\0\x28"), (6,b"\x20\0"),
                                    (9,b"\x11"), (12,b"\xc0\xa8\0\1"), (32,b"\x10")):
            changed = bytearray(valid)
            changed[offset:offset + len(replacement)] = replacement
            malformed.append(bytes(changed))
        for invalid in malformed:
            with self.subTest(invalid=invalid[:20]), self.assertRaises(ValueError):
                module.tcp_metadata(invalid, 27160)
        self.assertNotIn("private", str(module.tcp_metadata(valid,27160)))

    def test_forced_data_drops_are_each_direction_once_and_retransmissions_observed(self):
        policy = module.PacketPolicy(module.configuration(27160,20,5,0,731,20), True)
        for source, target in ((49000,27160),(27160,49000)):
            ack = module.tcp_metadata(packet(b"",source,target),27160)
            self.assertFalse(policy.decide(ack)[0])
            data = module.tcp_metadata(packet(source=source,target=target),27160)
            dropped, delay, repeated, forced = policy.decide(data)
            self.assertTrue(dropped and forced)
            self.assertFalse(repeated)
            self.assertTrue(15 <= delay <= 25)
            dropped, _, repeated, forced = policy.decide(data)
            self.assertFalse(dropped or forced)
            self.assertTrue(repeated)

    def test_seeded_loss_profile_replays_decisions_without_touching_payload(self):
        profile = module.configuration(27160,50,10,2,721,60)
        first, second = module.PacketPolicy(profile), module.PacketPolicy(profile)
        outcomes = []
        for index in range(500):
            raw = packet(sequence=index)
            metadata = module.tcp_metadata(raw,27160)
            one = first.decide(metadata)
            self.assertEqual(one,second.decide(metadata))
            outcomes.append(one)
        self.assertTrue(0 < sum(item[0] for item in outcomes) < 50)
        self.assertTrue(all(40 <= item[1] <= 60 for item in outcomes))

    def test_queue_has_exact_packet_and_byte_bounds_and_deadline_order(self):
        pending = module.PendingPackets(maximum=2, maximum_bytes=5)
        pending.push(20,1,b"12",None,{})
        pending.push(10,2,b"345",None,{})
        with self.assertRaises(OverflowError):
            pending.push(0,3,b"",None,{})
        self.assertEqual(pending.pop()[1],2)
        with self.assertRaises(OverflowError):
            pending.push(0,3,b"1234",None,{})
        self.assertEqual(pending.pop()[2],b"12")
        self.assertEqual(pending.bytes,0)


class FakeDriver:
    def __init__(self, packets):
        self.incoming = queue.Queue()
        for raw in packets:
            self.incoming.put((raw,None))
        self.sent = []
        self.closed = False
    def open(self, filter_text):
        self.filter = filter_text
    def recv(self):
        return self.incoming.get(timeout=2)
    def send(self, packet, address):
        self.sent.append(packet)
    def shutdown_receive(self):
        self.incoming.put(None)
    def close(self):
        self.closed = True


class PacketEngineTests(unittest.TestCase):
    def test_bidirectional_packet_bytes_delay_and_shutdown_drain(self):
        originals = [packet(sequence=1),packet(source=27160,target=49000,sequence=2)]
        backend, events = FakeDriver(originals), []
        engine = module.PacketEngine(backend,module.configuration(27160,8,2,0,731,1),events.append)
        with tempfile.TemporaryDirectory() as directory:
            try:
                engine.start()
                engine.pump(time.monotonic()+.05,Path(directory)/"stop.request")
            finally:
                engine.close()
        self.assertCountEqual(backend.sent,originals)
        self.assertTrue(backend.closed)
        self.assertFalse(engine.receiver.is_alive())
        self.assertEqual(engine.errors,[])
        self.assertEqual(engine.counts["captured"],2)
        self.assertEqual(engine.counts["reinjected"],2)
        self.assertTrue(all(event["observed_delay_ms"] >= event["requested_delay_ms"] for event in events))
        observed = [event["observed_delay_ms"] for event in events]
        self.assertEqual(engine.minimum_delay_ms, min(observed))
        self.assertEqual(engine.maximum_delay_ms, max(observed))
        self.assertNotIn("private application",str(events))

    def test_capture_budget_exhaustion_fails_and_flushes_without_hidden_loss(self):
        originals = [packet(sequence=index) for index in range(3)]
        backend, events = FakeDriver(originals), []
        engine = module.PacketEngine(backend,module.configuration(27160,1000,0,0,731,1),events.append)
        with tempfile.TemporaryDirectory() as directory, patch.object(module,"MAX_PACKETS",2):
            try:
                engine.start()
                engine.pump(time.monotonic()+.1,Path(directory)/"stop.request")
            finally:
                engine.close()
        self.assertEqual(engine.errors,["OverflowError"])
        self.assertTrue(engine.stop.is_set())
        self.assertEqual(engine.counts["captured"],3)
        self.assertEqual(engine.counts["dropped"],0)
        self.assertEqual(engine.counts["overflow_bypass"],0)
        self.assertEqual(engine.counts["reinjected"],3)
        self.assertCountEqual(backend.sent,originals)
        self.assertEqual(len(events),3)
        self.assertTrue(all(event["shutdown_flush"] for event in events))
        self.assertEqual(engine.pending.bytes,0)
        self.assertFalse(engine.receiver.is_alive())
        self.assertTrue(backend.closed)

    def test_queue_overflow_bypasses_and_marks_failed_instead_of_silent_drop(self):
        originals = [packet(sequence=1),packet(sequence=2)]
        backend, events = FakeDriver(originals), []
        engine = module.PacketEngine(backend,module.configuration(27160,1000,0,0,731,1),events.append)
        engine.pending = module.PendingPackets(maximum=1)
        with tempfile.TemporaryDirectory() as directory:
            try:
                engine.start()
                engine.pump(time.monotonic()+.1,Path(directory)/"stop.request")
            finally:
                engine.close()
        self.assertCountEqual(backend.sent,originals)
        self.assertEqual(engine.errors,["OverflowError"])
        self.assertEqual(engine.counts["overflow_bypass"],1)
        self.assertEqual(engine.counts["dropped"],0)
        self.assertEqual(engine.pending.bytes,0)
        self.assertTrue(backend.closed)

    def test_invalid_packet_fails_and_always_closes_driver(self):
        backend = FakeDriver([packet(target=80)])
        engine = module.PacketEngine(backend,module.configuration(27160,0,0,0,1,1),lambda _: None)
        with tempfile.TemporaryDirectory() as directory:
            try:
                engine.start()
                engine.pump(time.monotonic()+.1,Path(directory)/"stop.request")
            finally:
                engine.close()
        self.assertEqual(engine.errors,["ValueError"])
        self.assertTrue(backend.closed)
        self.assertFalse(engine.receiver.is_alive())


if __name__ == "__main__":
    unittest.main()
