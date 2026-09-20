"""Bounded M05 original-worker capture/replay over the existing private IPC.

Never starts SPORE, creates fake worker state, or grants milestone acceptance.
Use only already prepared, running --authority-probe / --replica-probe workers.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools/launcher'))
import worker_manager as workers
import launcher_service as service

EXE = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
SDK = 'cbf9206b9a823f0911cd9be0217104a49d72380b'
VERSION = '0.0.29'
MAX_TRACE = 34 * 1024 * 1024


def uint(value, bits=64, nonzero=False):
    if type(value) is not int or not int(nonzero) <= value < 1 << bits:
        raise ValueError('Invalid unsigned scalar')
    return value


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result: raise ValueError('Duplicate JSON key')
        result[key] = value
    return result


def read_json(raw):
    def constant(_): raise ValueError('Nonfinite JSON')
    return json.loads(raw, object_pairs_hook=unique, parse_constant=constant)


def trace_rows(raw, pid, role):
    if len(raw) > MAX_TRACE: raise ValueError('Oversized trace')
    complete = raw[:raw.rfind(b'\n') + 1]
    rows = [read_json(line) for line in complete.splitlines()]
    if any(not isinstance(row, dict) for row in rows): raise ValueError('Trace record is not an object')
    if not rows or rows[0].get('event') != 'trace_start': raise ValueError('Missing original trace header')
    first = rows[0]
    uint(first.get('thread_id'), 32, True)
    uint(first.get('qpc_frequency'), nonzero=True)
    # These source versions share the exact four-scalar authority capture ABI.
    # Retain actual native outcomes when changing only receiver guards/probes.
    versions = (VERSION, '0.0.16', '0.0.17', '0.0.18', '0.0.19', '0.0.20', '0.0.21', '0.0.22', '0.0.23', '0.0.24', '0.0.25', '0.0.26', '0.0.27', '0.0.28') if role == 'authority' else (VERSION,)
    if (first.get('evidence_class'), first.get('executable_sha256'), first.get('sdk_commit')) != ('NATIVE_PROBE', EXE, SDK) or first.get('bridge_version') not in versions:
        raise ValueError('Wrong source build or evidence class')
    ready = [r for r in rows if r.get('event') == 'replica_policy_ready']
    if len(ready) != 1 or ready[0].get('role') != role: raise ValueError('Wrong original process role')
    previous = -1
    for i, row in enumerate(rows, 1):
        if row.get('schema_version') != 1: raise ValueError('Unknown trace schema')
        if type(row.get('sequence')) is not int or row['sequence'] != i or row.get('pid') != pid or type(row.get('pid')) is not int:
            raise ValueError('Trace sequence/process mismatch')
        if type(row.get('thread_id')) is not int or type(row.get('foreign_callbacks')) is not int or row.get('thread_id') != first['thread_id'] or row.get('foreign_callbacks') != 0:
            raise ValueError('Foreign callback or thread mismatch')
        qpc = uint(row.get('qpc'))
        if qpc < previous or row.get('qpc_frequency') != first['qpc_frequency']: raise ValueError('Native clock mismatch')
        previous = qpc
    return complete, rows


def sample_values(row):
    if row.get('event') != 'replica_source_sample' or row.get('native_state_sample') is not True:
        raise ValueError('Not an original source sample')
    for field in ('worker_low', 'worker_high'): uint(row.get(field))
    if not (row['worker_low'] or row['worker_high']): raise ValueError('Zero source worker')
    for field in ('source_scene', 'source_entity', 'entity_generation', 'sample'): uint(row.get(field), nonzero=True)
    values = []
    for field, maximum in (('health_bits', 1e6), ('energy_bits', 1e6), ('hunger_bits', 1e6), ('dna_bits', 1e9)):
        bits = uint(row.get(field), 32)
        value = struct.unpack('<f', struct.pack('<I', bits))[0]
        if not math.isfinite(value) or not 0 <= value <= maximum or (field == 'health_bits' and not value > 0):
            raise ValueError('Unsupported living-avatar state')
        values.append(bits)
    return values


def frame(operation, *values):
    result = [uint(operation), *(uint(x) for x in values)]
    if len(result) > 10: raise ValueError('Oversized replica control request')
    return result + [0] * (10 - len(result))


def source_from_report(path):
    path = service.diag.no_reparse(Path(path).absolute())
    if path.stat().st_size > 65536: raise ValueError('Oversized source report')
    report = read_json(path.read_bytes())
    if report.get('schema_version') != 1 or report.get('operation') != 'capture' or report.get('probe_checks_passed') is not True:
        raise ValueError('Source capture did not pass')
    trace = service.diag.no_reparse(Path(report['source_trace']))
    count = uint(report.get('source_prefix_bytes'), nonzero=True)
    if count > MAX_TRACE: raise ValueError('Oversized source prefix')
    with trace.open('rb') as stream: raw = stream.read(count)
    if len(raw) != count or hashlib.sha256(raw).hexdigest() != report['source_prefix_sha256']:
        raise ValueError('Changed original source evidence')
    _, rows = trace_rows(raw, uint(report.get('source_pid'), 32, True), 'authority')
    sample = report['sample']
    if sum(row == sample for row in rows) != 1: raise ValueError('Source sample not present exactly once in original trace')
    sample_values(sample)
    worker = workers.validate_current(report['worker']['worker_id'], report['worker'])
    if trace != Path(worker['run']) / f"actors-{report['source_pid']}.jsonl":
        raise ValueError('Source trace escaped its worker run')
    if struct.unpack('<QQ', bytes.fromhex(worker['generation'])) != (sample['worker_low'], sample['worker_high']):
        raise ValueError('Source worker generation mismatch')
    return sample


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('capture', 'replay'))
    parser.add_argument('--worker', choices=('01', '02'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--baseline', type=int, default=1)
    parser.add_argument('--challenge', action='store_true', help='Attempt guarded native mutations while connected and disconnected')
    args = parser.parse_args()
    output = service.diag.no_reparse(args.output.absolute())
    if output.exists(): raise ValueError('Evidence already exists')
    # Reserve evidence before sending any command; partial failures are retained.
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x', encoding='utf-8') as sink:
        report = {'schema_version': 1, 'operation': args.operation, 'commands': [], 'probe_checks_passed': False,
                  'native_acceptance': 'NOT_VERIFIED', 'outcome_causality': 'NOT_EVALUATED'}
        try:
            value = workers.current(args.worker)
            report['worker'] = value
            if not workers.supervisor_running(value): raise ValueError('Original supervisor is not running')
            status = workers.read_json(Path(value['run']) / 'worker-status.json')
            if status['generation'] != value['generation']: raise ValueError('Stale status generation')
            pid = uint(status['game_pid'], 32, True)
            trace = service.diag.no_reparse(Path(value['run']) / f'actors-{pid}.jsonl')

            def rows():
                if trace.stat().st_size > MAX_TRACE: raise ValueError('Oversized native trace')
                return trace_rows(trace.read_bytes(), pid, 'authority' if args.operation == 'capture' else 'replica')

            def command(op, epoch=0, values=None, expected='accepted'):
                argv = [str(REPO / 'build/win32/Release/SporeMP.WorkerControl.exe'), value['generation'], str(value['supervisor_pid']), op]
                if op != 'status': argv.append(str(uint(epoch)))
                if values is not None: argv.extend(str(x) for x in values)
                entry = {'argv': argv, 'expected_result': expected}
                report['commands'].append(entry)
                result = subprocess.run(argv, capture_output=True, text=True, timeout=13, creationflags=subprocess.CREATE_NO_WINDOW)
                entry.update(exit_code=result.returncode, stdout=result.stdout, stderr=result.stderr)
                if result.returncode not in (0, 5): raise ValueError('Unknown command outcome; no automatic retry')
                response = read_json(result.stdout)
                if response.get('schema_version') != 1 or response.get('op') != op or response.get('result') != expected:
                    raise ValueError(f'Unexpected {op} result: {response}')
                if result.returncode != (0 if expected == 'accepted' else 5): raise ValueError('Exit code contradicts reply')
                return response

            rows()  # Reject incorrect native mode/provenance before mutations.
            live = command('status')
            if live.get('phase') != 3 or not live.get('actor_a'): raise ValueError('Load and adopt the original fixture before this probe')
            epoch = uint(live['epoch'], nonzero=True)
            if args.operation == 'capture':
                reply = command('replica', epoch, frame(5, live['actor_a']))
                deadline = time.monotonic() + 3
                while True:
                    raw, events = rows()
                    samples = [r for r in events if r.get('event') == 'replica_source_sample' and r.get('request') == reply['request']]
                    if samples: break
                    if time.monotonic() >= deadline: raise ValueError('No matching flushed native sample')
                    time.sleep(0.1)
                if len(samples) != 1: raise ValueError('Ambiguous source sample')
                sample_values(samples[0])
                if struct.unpack('<QQ', bytes.fromhex(value['generation'])) != (samples[0]['worker_low'], samples[0]['worker_high']):
                    raise ValueError('Source generation mismatch')
                report.update(source_pid=pid, source_trace=str(trace), source_prefix_bytes=len(raw),
                              source_prefix_sha256=hashlib.sha256(raw).hexdigest(), sample=samples[0])
            else:
                if args.source is None: raise ValueError('Source report required')
                sample = source_from_report(args.source)
                baseline = uint(args.baseline, nonzero=True)
                if struct.unpack('<QQ', bytes.fromhex(value['generation'])) == (sample['worker_low'], sample['worker_high']):
                    raise ValueError('Source and replica must be different process generations')
                _, before = rows()
                first_sequence = before[-1]['sequence']
                command('replica', epoch, frame(1, sample['worker_low'], sample['worker_high'], sample['source_scene'], baseline))
                command('replica', epoch, frame(2, baseline, sample['source_entity'], sample['entity_generation'], live['actor_a']))
                application = frame(3, baseline, sample['source_entity'], sample['entity_generation'], sample['sample'], *sample_values(sample))
                command('replica', epoch, application)
                command('replica', epoch, application, expected='stale')
                if args.challenge: command('replica', epoch, frame(7))
                time.sleep(1)  # bounded intervening native updates, not timing acceptance
                command('replica', epoch, frame(6))
                command('replica', epoch, frame(4, baseline))
                if args.challenge: command('replica', epoch, frame(7))
                command('replica', epoch, application, expected='invalid')
                command('replica', epoch, frame(5, live['actor_a']), expected='unavailable')
                time.sleep(0.4)
                raw, events = rows()
                recent = [r for r in events if r['sequence'] > first_sequence]
                applies = [r for r in recent if r.get('event') == 'replica_vitals_applied']
                audits = [r for r in recent if r.get('event') == 'replica_projection_audit']
                if len(applies) != 1 or applies[0].get('matched') is not True or len(audits) < 2 or any(r.get('matches') is not True for r in audits):
                    raise ValueError('Missing, duplicated or divergent native projection evidence')
                if any(r.get('event') == 'replica_drift_quarantined' for r in recent): raise ValueError('Uncovered native mutation changed the replica')
                challenges = [r for r in recent if r.get('event') == 'replica_challenge_result']
                if args.challenge and (len(challenges) != 2 or any(r.get('passed') is not True for r in challenges)):
                    raise ValueError('Missing or failed native connected/disconnected challenge evidence')
                report.update(source_report=str(args.source.resolve()), replica_trace=str(trace), replica_prefix_bytes=len(raw),
                              replica_prefix_sha256=hashlib.sha256(raw).hexdigest(), application=applies[0], audits=audits,
                              challenges=challenges,
                              changed_health_or_dna=applies[0]['before_health'] != applies[0]['health'] or applies[0]['before_dna'] != applies[0]['dna'])
            report['probe_checks_passed'] = True
        except Exception as error:
            report['error'] = str(error)
        finally:
            report['utc'] = service.diag.utc_now()
            json.dump(report, sink, indent=2, allow_nan=False)
    print(json.dumps({'probe_checks_passed': report['probe_checks_passed'], 'report': str(output), 'native_acceptance': 'NOT_VERIFIED'}))
    return 0 if report['probe_checks_passed'] else 1


if __name__ == '__main__': raise SystemExit(main())
