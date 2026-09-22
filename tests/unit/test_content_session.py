"""HOST fixtures for the private editor RPC adapter; no game or desktop use."""
import io
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/"tools/content"))
import content_session as session


class CodecTests(unittest.TestCase):
    def test_independent_header_layout_and_roundtrip(self):
        raw = bytes.fromhex(session.encode("offer", 0xFEDCBA9876543210, 42, identity="ab"*32, total=68))
        self.assertEqual(len(raw), 512)
        self.assertEqual(raw[:16], bytes.fromhex("534d4338010000000200000000000000"))
        self.assertEqual(raw[16:24], bytes.fromhex("1032547698badcfe"))
        self.assertEqual(raw[24:32], struct.pack("<Q", 42))
        self.assertEqual(raw[40:72], b"\xab"*32)
        decoded = session.decode(raw.hex())
        self.assertEqual((decoded["op"], decoded["total"], decoded["transaction"]), ("offer", 68, 42))

    def test_chunk_limits_types_and_path_shaped_identities(self):
        good = session.encode("chunk", 1, 1, data=b"x"*420)
        self.assertEqual(session.decode(good)["data"], b"x"*420)
        for fields in (dict(data=b"x"*421), dict(data="abc"), dict(identity="../"+"a"*61),
                       dict(identity="A"*64), dict(offset=-1), dict(total=1 << 32)):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                session.encode("chunk", 1, 1, **fields)
        for value in (True, -1, 1 << 64, 1.5, "1"):
            with self.assertRaises(ValueError):
                session.encode("begin", value)
        with self.assertRaises(ValueError):
            session.encode("published_event", 1)

    def test_invalid_reply_schema_padding_and_enum_are_rejected(self):
        good = bytes.fromhex(session.encode("begin", 1))
        for offset, value in ((0, 0), (4, 2), (8, 99), (12, 99), (84, 6), (89, 255), (511, 1)):
            bad = bytearray(good); bad[offset] = value
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                session.decode(bad.hex())
        bad = bytearray(good); struct.pack_into("<I", bad, 88, 421)
        with self.assertRaises(ValueError):
            session.decode(bad.hex())
        for value in (good.hex()[:-1], "g"*1024, good.hex().upper(), None):
            with self.assertRaises(ValueError):
                session.decode(value)

    def test_unknown_outcome_freezes_mutations_without_retry(self):
        client = session.ContentSession.__new__(session.ContentSession)
        client.sequence = 0; client.uncertain = False; client.events = []
        client.process = SimpleNamespace(stdin=io.StringIO())
        client._receive = Mock(side_effect=ValueError("response deadline expired"))
        with self.assertRaises(ValueError):
            client.rpc("commit", 1)
        sent = client.process.stdin.getvalue()
        self.assertEqual(sent.count("\n"), 1)
        with self.assertRaisesRegex(ValueError, "Prior RPC outcome"):
            client.rpc("commit", 1)
        self.assertEqual(client.process.stdin.getvalue(), sent)
        self.assertEqual(client._receive.call_count, 1)

    def test_notifications_do_not_replace_correlated_policy_result(self):
        client = session.ContentSession.__new__(session.ContentSession)
        client.sequence = 0; client.uncertain = False; client.events = []
        client.process = SimpleNamespace(stdin=io.StringIO())
        notification = dict(op="load_event", transaction=7, request=0)
        result = dict(op="commit", transaction=7, request=1, failure="not_ready")
        client._receive = Mock(side_effect=[notification, result])
        self.assertEqual(client.rpc("commit", 7), result)
        self.assertEqual(client.events, [notification])
        self.assertFalse(client.uncertain)
        self.assertEqual(client.event("load_event", 7), notification)

    def test_wrong_response_correlation_is_uncertain(self):
        client = session.ContentSession.__new__(session.ContentSession)
        client.sequence = 0; client.uncertain = False; client.events = []
        client.process = SimpleNamespace(stdin=io.StringIO())
        client._receive = Mock(return_value=dict(op="cancel", transaction=7, request=1, failure="none"))
        with self.assertRaisesRegex(ValueError, "correlation"):
            client.rpc("commit", 7)
        self.assertTrue(client.uncertain)


if __name__ == "__main__":
    unittest.main()
