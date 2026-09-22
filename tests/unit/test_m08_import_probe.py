"""HOST/FIXTURE quarantine and native-import correlation, with no game launched."""
from copy import deepcopy
import importlib.util
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zlib

from test_m08_content_probe import fixture, encode, VALUES, KEY

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("m08_import_probe", ROOT/"tools/native/m08-import-probe.py")
driver = importlib.util.module_from_spec(spec); spec.loader.exec_module(driver)
c = driver.content


def chunk(kind, body):
    return struct.pack(">I",len(body))+kind+body+struct.pack(">I",zlib.crc32(kind+body))


def png(width=1, height=1, depth=8, color=6, interlace=0, pixels=bytes(5), compressed=None):
    header=struct.pack(">IIBBBBB",width,height,depth,color,0,0,interlace)
    return b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",header)+chunk(b"IDAT",zlib.compress(pixels) if compressed is None else compressed)+chunk(b"IEND",b"")


def imported_rows(data):
    old=fixture()
    rows=old[:2]+[dict(event="content_import_bindings_checked",code_prefixes=3,readiness=False),
        dict(event="content_import_begin",request=44,png_sha256=c.digest(data),png_bytes=len(data),readiness=False),
        dict(event="content_import_returned",request=44,native_result=True,instance=VALUES[0],type=VALUES[1],
             group=VALUES[2],native_commit_validation=False,readiness=False)]+old[2:]
    common={k:v for k,v in old[1].items() if k not in ("event","code_prefixes","native_execution_qualified","readiness")}
    rows=[{**common,**r,"sequence":i,"qpc":i} for i,r in enumerate(rows,1)]
    rows[0]["bridge_version"]=driver.VERSION
    return rows


class PngTests(unittest.TestCase):
    def test_native_shape_remains_unapproved(self):
        result=c.inspect_creation_png(png())
        self.assertEqual((result["width"],result["height"],result["format"]),(1,1,"RGBA8"))
        self.assertFalse(result["transfer_approved"])
        self.assertEqual(result["native_creation_validation"],"NOT_RUN")

    def test_every_truncation_and_single_byte_corruption(self):
        data=png()
        for i in range(len(data)):
            with self.subTest(i=i), self.assertRaises(c.ContentError): c.inspect_creation_png(data[:i])
            bad=bytearray(data); bad[i]^=1
            with self.subTest(i=i), self.assertRaises(c.ContentError): c.inspect_creation_png(bytes(bad))

    def test_crc_valid_unsupported_images_and_chunks(self):
        candidates=[png(width=0),png(width=513),png(height=513),png(depth=16),png(color=2),png(interlace=1),
                    png()+b"tail",png()[:33]+png()[8:], png()[:33]+chunk(b"tEXt",b"not admitted")+png()[33:],
                    bytes(c.MAX_CREATION_PNG+1)]
        for data in candidates:
            with self.subTest(size=len(data)), self.assertRaises(c.ContentError): c.inspect_creation_png(data)

    def test_bad_zlib_size_trailing_bomb_and_filters(self):
        candidates=[png(compressed=b"bad"),png(pixels=bytes(4)),png(pixels=bytes(6)),
                    png(compressed=zlib.compress(bytes(5))+b"extra"),png(pixels=bytes(2*1024*1024)),
                    png(pixels=b"\x05\0\0\0\0")]
        for data in candidates:
            with self.subTest(size=len(data)), self.assertRaises(c.ContentError): c.inspect_creation_png(data)


class CorrelationTests(unittest.TestCase):
    def test_import_and_inspection_require_matching_native_outcomes(self):
        data=png(); rows=imported_rows(data)
        result=driver.import_observation(rows,44,7,c.digest(data),len(data),3)
        self.assertEqual(result["native_key"],KEY)
        self.assertFalse(result["inspection"]["complete"]["readiness"])
        self.assertIsNone(driver.import_observation(rows[:-1],44,7,c.digest(data),len(data),3))

    def test_false_result_existing_key_corruption_and_unqualified_outputs(self):
        data=png()
        for index,field,value in ((3,"png_sha256","0"*64),(3,"png_bytes",1),(3,"epoch",8),
                                  (4,"native_result",False),(4,"native_result",1),(4,"epoch",8),
                                  (4,"type",0x1a99b06b),(4,"instance",0),(4,"readiness",True),
                                  (4,"native_commit_validation",True)):
            rows=imported_rows(data); rows[index][field]=value
            with self.subTest(field=field), self.assertRaises(ValueError):
                driver.import_observation(rows,44,7,c.digest(data),len(data),3)

    def test_duplicate_call_and_native_preflight_denial(self):
        data=png(); rows=imported_rows(data)
        for extra in (rows[3],rows[4],dict(event="content_import_rejected",sequence=4,request=44,reason="png_crc")):
            with self.assertRaises(ValueError):
                driver.import_observation(rows+[extra],44,7,c.digest(data),len(data),3)


class RunTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(); self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name); self.local=self.root/"local"; self.local.mkdir()
        self.source=self.local/"input.png"; self.data=png(); self.source.write_bytes(self.data)
        self.run=self.local/"worker-run"; self.run.mkdir()
        self.worker=dict(run=str(self.run),worker_id="01",generation="a"*32,supervisor_pid=111)
        self.trace=self.run/"actors-321.jsonl"; self.rows=imported_rows(self.data)
        self.trace.write_bytes(encode(self.rows[:3]))
        self.args=SimpleNamespace(output=self.local/"result",png=self.source,worker="01",version=driver.VERSION)
        self.calls=[]
        for manager in (patch.object(driver.probe,"REPO",self.root),
                        patch.object(driver.probe.workers,"current",return_value=self.worker),
                        patch.object(driver.probe,"live_identity",return_value=(321,{})),
                        patch.object(driver.probe,"payload_identity",return_value={"fixture":"HOST"})):
            manager.start(); self.addCleanup(manager.stop)

    def command(self,worker,operation,report,flush,epoch=0,values=()):
        self.calls.append(operation)
        report["commands"].append(dict(operation=operation,values=list(values)))
        if operation=="import_creation": self.trace.write_bytes(encode(self.rows))
        return dict(result="accepted",epoch=7,phase=1,request=44)

    def execute(self,command=None):
        with patch.object(driver.probe,"command",side_effect=command or self.command): return driver.run(self.args)

    def test_fixed_quarantine_import_and_copied_trace(self):
        report=self.execute()
        self.assertTrue(report["imported_and_inspected"])
        self.assertFalse(report["readiness"]); self.assertFalse(report["transfer_approved"])
        self.assertEqual(self.calls,["status","import_creation"])
        self.assertEqual((self.run/"m08-import.png").read_bytes(),self.data)
        self.assertEqual((self.args.output/"after-trace.jsonl").read_bytes(),encode(self.rows))
        self.assertEqual(struct.pack("<8I",*report["commands"][1]["values"]).hex(),c.digest(self.data))

    def test_bad_input_never_calls_native_or_stages(self):
        self.source.write_bytes(b"bad")
        report=self.execute()
        self.assertFalse(report["imported_and_inspected"]); self.assertEqual(self.calls,[])
        self.assertFalse((self.run/"m08-import.png").exists())

    def test_existing_quarantine_never_overwritten(self):
        (self.run/"m08-import.png").write_bytes(b"preserve")
        report=self.execute()
        self.assertFalse(report["imported_and_inspected"]); self.assertEqual(self.calls,[])
        self.assertEqual((self.run/"m08-import.png").read_bytes(),b"preserve")

    def test_unknown_outcome_is_not_retried(self):
        def uncertain(*args,**kwargs):
            reply=self.command(*args,**kwargs)
            if args[1]=="import_creation": raise ValueError("IPC outcome unknown")
            return reply
        report=self.execute(uncertain)
        self.assertFalse(report["imported_and_inspected"])
        self.assertEqual(self.calls.count("import_creation"),1)
        self.assertTrue((self.args.output/"after-trace.jsonl").is_file())

    def test_replaced_prefix_or_source_after_native_call_fails(self):
        def changed(*args,**kwargs):
            reply=self.command(*args,**kwargs)
            if args[1]=="import_creation": self.source.write_bytes(b"changed")
            return reply
        report=self.execute(changed)
        self.assertFalse(report["imported_and_inspected"])
        self.assertIn("PNG changed",report["error"])

    def test_binding_rejection_does_not_stage_or_call(self):
        rows=deepcopy(self.rows[:3]); rows[2]["event"]="content_import_binding_rejected"
        self.trace.write_bytes(encode(rows))
        report=self.execute()
        self.assertFalse(report["imported_and_inspected"]); self.assertEqual(self.calls,[])
        self.assertFalse((self.run/"m08-import.png").exists())


if __name__=="__main__": unittest.main()
