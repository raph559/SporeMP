"""HOST/FIXTURE parser and evidence-boundary tests; never starts original SPORE."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

PATH = Path(__file__).resolve().parents[2] / 'tools/native/replica-probe.py'
spec = importlib.util.spec_from_file_location('replica_probe', PATH)
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def bits(value): return struct.unpack('<I', struct.pack('<f', value))[0]


def fixture():
    common = dict(schema_version=1, evidence_class='NATIVE_PROBE', pid=321, thread_id=654, foreign_callbacks=0, qpc_frequency=1000)
    rows = [{**common, 'sequence': 1, 'qpc': 0, 'event': 'trace_start', 'executable_sha256': probe.EXE, 'sdk_commit': probe.SDK, 'bridge_version': probe.VERSION},
            {**common, 'sequence': 2, 'qpc': 1, 'event': 'replica_policy_ready', 'role': 'authority'},
            {**common, 'sequence': 3, 'qpc': 2, 'event': 'replica_source_sample', 'native_state_sample': True,
             'worker_low': 1, 'worker_high': 2, 'source_scene': 3, 'source_entity': 4, 'entity_generation': 5, 'sample': 6,
             'health_bits': bits(7), 'energy_bits': bits(8), 'hunger_bits': bits(9), 'dna_bits': bits(10)}]
    return rows


def encode(rows): return b''.join(json.dumps(row).encode() + b'\n' for row in rows)


class ReplicaProbeTests(unittest.TestCase):
    def test_trace_requires_native_identity_role_sequence_thread_and_clock(self):
        for index, field, value in ((0, 'evidence_class', 'HOST_FIXTURE'), (0, 'executable_sha256', '0'*64),
                (0, 'bridge_version', '0.0.14'), (1, 'role', 'replica'), (2, 'sequence', 4), (2, 'pid', 322),
                (2, 'thread_id', 655), (2, 'foreign_callbacks', 1), (2, 'foreign_callbacks', False),
                (2, 'qpc', 0), (2, 'qpc_frequency', 999), (2, 'schema_version', 2)):
            rows=fixture(); rows[index][field]=value
            with self.subTest(field=field), self.assertRaises(ValueError): probe.trace_rows(encode(rows),321,'authority')

    def test_live_trace_ignores_only_incomplete_last_line(self):
        data=encode(fixture())
        complete, rows=probe.trace_rows(data+b'{"event":',321,'authority')
        self.assertEqual(complete,data); self.assertEqual(len(rows),3)
        for suffix in (b'{bad}\n', b'{}\n', b'[]\n', b'{"x":1,"x":2}\n'):
            with self.assertRaises(ValueError): probe.trace_rows(data+suffix,321,'authority')

    def test_source_vitals_are_full_width_finite_living_values(self):
        sample=fixture()[-1]
        self.assertEqual(probe.sample_values(sample),[bits(x) for x in (7,8,9,10)])
        for field in ('health_bits','energy_bits','hunger_bits','dna_bits'):
            for bad in (True, -1, 1<<32, bits(float('nan')), bits(float('inf')), bits(-1)):
                with self.subTest(field=field,bad=bad), self.assertRaises(ValueError): probe.sample_values({**sample,field:bad})
        for field, bad in (('health_bits',bits(0)),('source_scene',0),('source_entity',False),('entity_generation',0),('sample',0),('native_state_sample',False)):
            with self.assertRaises(ValueError): probe.sample_values({**sample,field:bad})

    def test_bounded_scalar_request_rejects_bad_width_and_extra_data(self):
        self.assertEqual(len(probe.frame(3,1,2,3,4,5,6,7,8)),10)
        for values in ((-1,), (True,), (1<<64,), tuple(range(11))):
            with self.assertRaises(ValueError): probe.frame(1,*values)

    def test_replay_revalidates_exact_original_prefix_and_sample(self):
        with tempfile.TemporaryDirectory(prefix='m05-fixture-') as temporary:
            root=Path(temporary); trace=root/'actors-321.jsonl'; raw=encode(fixture()); trace.write_bytes(raw)
            worker={'worker_id':'01','run':str(root),'generation':struct.pack('<QQ',1,2).hex()}
            report={'schema_version':1,'operation':'capture','probe_checks_passed':True,'source_trace':str(trace),
                    'source_prefix_bytes':len(raw),'source_prefix_sha256':hashlib.sha256(raw).hexdigest(),
                    'source_pid':321,'worker':worker,'sample':fixture()[-1]}
            path=root/'capture.json'; path.write_text(json.dumps(report))
            with patch.object(probe.workers,'validate_current',return_value=worker):
                self.assertEqual(probe.source_from_report(path),fixture()[-1])
                report['sample']['health_bits']=bits(50); path.write_text(json.dumps(report))
                with self.assertRaisesRegex(ValueError,'not present'): probe.source_from_report(path)
                report['sample']=fixture()[-1]; path.write_text(json.dumps(report)); trace.write_bytes(raw.replace(probe.VERSION.encode(),b'0.0.14'))
                with self.assertRaisesRegex(ValueError,'Changed'): probe.source_from_report(path)


if __name__ == '__main__': unittest.main()
