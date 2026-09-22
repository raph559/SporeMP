"""Closed-source candidate HOST fixtures; synthetic records never enter SPORE."""
from copy import deepcopy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/"tools/content"))
import native_creation as native
from test_m08_content_probe import fixture, save_fixture, encode, KEY, VALUES
from test_m08_import_probe import png
from test_spore_content import archive


class SourceTests(unittest.TestCase):
    def setup_source(self, root):
        copied, closed = root/"source", root/"closed"
        copied.mkdir(); closed.mkdir()
        fields = dict(schema="2", role="player", build_sha256="11"*32, executable_sha256=native.probe.EXE,
            content_sha256=native.content.digest((native.ROOT/"config/compatibility.candidate.json").read_bytes()), fixture_sha256="22"*32)
        for n, (name, path) in enumerate(native.multiplayer_session.WORLD_FILES):
            target = copied/path; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(bytes([n]))
            fields[name] = native.content.digest(target.read_bytes())
        config = root/"player.conf"; config.write_text("\n".join(k+"="+v for k, v in fields.items()))
        saved, inspected = save_fixture(), fixture()
        base = saved[0]
        lookup = dict(event="content_record_observed", purpose="rigblock", request=44, group=1, instance=2, type=0x00b1b104,
            resolved_group=1, resolved_instance=2, resolved_type=0x00b1b104, exact_found=True, mapped_found=True,
            exact_location_qualified=True, mapped_location_qualified=True, readiness=False)
        rows = saved[:4]+inspected[1:3]+[{**lookup, "manager":"app"},{**lookup,"manager":"creation"}]+inspected[3:]+saved[4:]
        common = {k: base[k] for k in ("schema_version", "evidence_class", "pid", "thread_id", "foreign_callbacks", "qpc_frequency", "epoch")}
        rows = [{**common, **row, "sequence":n, "qpc":n} for n, row in enumerate(rows, 1)]
        trace = closed/"actors-321.jsonl"; trace.write_bytes(encode(rows))
        host = [dict(event="module", path="C:/fixture/mLibs/SporeMP.Bridge.dll", sha256=fields["build_sha256"]),
            dict(event="game_exited", game_pid=321, exit_code=0), dict(event="worker_exited", exit_code=0, requested=True)]
        (closed/"native-host.jsonl").write_bytes(encode(host))
        data = png(); (copied/"EditorSaves.package").write_bytes(archive([
            (KEY.replace("2b978c46", "1a99b06b"), b"BEM!", 4, 0),
            (KEY.replace("2b978c46", "2f7d0004"), data, len(data), 0)]))
        return copied, closed, config, rows, fields

    def test_closed_candidate_keeps_authority_gate_open(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); copied, closed, config, rows, fields = self.setup_source(root)
            with patch.object(native, "private", side_effect=lambda p: Path(p)), patch.object(native.probe, "private_path", side_effect=lambda p: Path(p)):
                report = native.prepare(copied, closed, config, KEY, 44, root/"output", native.probe.VERSION)
            self.assertFalse(report["multiplayer_publication"])
            self.assertFalse(report["readiness"])
            self.assertEqual(report["authority_validation"], "REQUIRED_NOT_PERFORMED")
            self.assertEqual((root/"output/creation.png").read_bytes(), png())
            observed = (root/"output/native-observation.bin").read_bytes()
            self.assertEqual(len(observed), 352)
            self.assertEqual(observed[:8], struct.pack("<II", 0x384f4d53, 1))
            self.assertEqual(observed[40:52], struct.pack("<III", VALUES[2], VALUES[0], VALUES[1]))
            self.assertEqual(observed[56:88], bytes.fromhex(fields["content_sha256"]))
            self.assertEqual(struct.unpack_from("<III", observed, 280), (1,1,1))

    def test_source_rejects_unclosed_wrong_build_world_and_substituted_parts(self):
        cases = ("footer", "host_exit", "bridge", "world", "substitute", "missing_lookup", "inspection_before_save", "png_absent")
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temp:
                root=Path(temp); copied, closed, config, rows, fields=self.setup_source(root)
                if case == "footer": rows.pop()
                if case == "host_exit": (closed/"native-host.jsonl").write_bytes(encode([dict(event="game_exited",game_pid=321,exit_code=1)]))
                if case == "bridge": config.write_text(config.read_text().replace("build_sha256="+"11"*32, "build_sha256="+"33"*32))
                if case == "world": (copied/native.multiplayer_session.WORLD_FILES[3][1]).write_bytes(b"wrong terrain, unchanged name")
                if case == "substitute": next(r for r in rows if r.get("purpose")=="rigblock")["resolved_instance"]=999
                if case == "missing_lookup": rows=[r for r in rows if r.get("manager")!="app"]
                if case == "inspection_before_save":
                    saved=next(r for r in rows if r.get("event")=="editor_result_observed"); rows.remove(saved); rows.insert(-2,saved)
                if case == "png_absent": (copied/"EditorSaves.package").write_bytes(archive([(KEY.replace("2b978c46","1a99b06b"),b"BEM!",4,0)]))
                rows=[{**r,"sequence":n,"qpc":n} for n,r in enumerate(rows,1)]
                (closed/"actors-321.jsonl").write_bytes(encode(rows))
                with patch.object(native,"private",side_effect=lambda p:Path(p)), patch.object(native.probe,"private_path",side_effect=lambda p:Path(p)), self.assertRaises(ValueError):
                    native.prepare(copied,closed,config,KEY,44,root/"output",native.probe.VERSION)
                self.assertFalse((root/"output").exists())


if __name__ == "__main__":
    unittest.main()
