"""Explicit native dual-save probe. No launch, backup, retry, stop, or crash."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools/launcher'))
import launcher_service as service
import worker_manager as workers
spec = importlib.util.spec_from_file_location('checkpoint', REPO / 'tools/native/worker-checkpoint.py')
checkpoint = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checkpoint)
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
for name in ('QueryPerformanceCounter', 'QueryPerformanceFrequency'):
    getattr(kernel, name).argtypes = [ctypes.POINTER(ctypes.c_longlong)]
    getattr(kernel, name).restype = ctypes.c_int

def counter(name='QueryPerformanceCounter'):
    value = ctypes.c_longlong()
    if not getattr(kernel, name)(ctypes.byref(value)): raise ctypes.WinError(ctypes.get_last_error())
    return value.value

def ready(value):
    if value.get('phase') != 3 or value.get('mode') != 23415809 or not value.get('epoch'):
        raise ValueError('Original controlled Creature scene is not ready')
    if not value.get('actor_a') or not value.get('actor_b') or value['actor_a'] == value['actor_b']:
        raise ValueError('Exactly two distinct owned A/B actors are required')
    if value.get('persistence_state') in (1, 2, 4, 5, 7, 8):
        raise ValueError('Native persistence is busy or failed')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = service.diag.no_reparse(args.output)
    journal_path = output.with_suffix('.commands.jsonl')
    trace_paths = {worker: output.with_name(output.stem + '-trace-' + worker + '.jsonl') for worker in ('01', '02')}
    for path in (output, journal_path, *trace_paths.values()):
        if service.diag.no_reparse(path).exists(): raise ValueError('Fresh evidence paths required')
    output.parent.mkdir(parents=True, exist_ok=True)
    journal = journal_path.open('x', encoding='utf-8')
    report = {'schema_version': 1, 'kind': 'native-concurrent-save-probe', 'utc': service.diag.utc_now(),
              'native_acceptance': 'NOT_VERIFIED', 'commands': [], 'save_mutations_attempted': [],
              'durability': 'NOT_VERIFIED_LIVE_FILES', 'qpc_frequency': counter('QueryPerformanceFrequency'),
              'script_sha256': service.diag.fingerprint(Path(__file__))['sha256']}
    def log(row):
        journal.write(json.dumps(row) + '\n'); journal.flush()
    pinned = {}
    def launch(worker, op, epoch=0):
        current = pinned[worker]['current']
        argv = [str(REPO / 'build/win32/Release/SporeMP.WorkerControl.exe'), current['generation'], str(current['supervisor_pid']), op]
        if op != 'status': argv.append(str(epoch))
        item = {'worker': worker, 'op': op, 'argv': argv, 'started_utc': service.diag.utc_now(), 'qpc_before': counter()}
        log({'event': 'command_start', **item})
        if op == 'save': report['save_mutations_attempted'].append(worker)
        process = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        return process, item
    def finish(ticket):
        process, item = ticket
        try:
            stdout, stderr = process.communicate(timeout=13)
            item.update(qpc_after=counter(), ended_utc=service.diag.utc_now(), exit_code=process.returncode, stdout=stdout, stderr=stderr)
        except subprocess.TimeoutExpired:
            # Terminate only this disposable IPC client; never its target/descendants.
            process.kill(); stdout, stderr = process.communicate()
            item.update(qpc_after=counter(), ended_utc=service.diag.utc_now(), exit_code=process.returncode, stdout=stdout, stderr=stderr, outcome='UNKNOWN_NO_RETRY')
        return item
    def accept(item):
        report['commands'].append(item); log({'event': 'command_end', **item})
        if item.get('outcome') or item['exit_code'] != 0: raise ValueError('IPC failed or outcome unknown; no mutation retry')
        response = json.loads(item['stdout'])
        if response.get('schema_version') != 1 or response.get('op') != item['op'] or response.get('result') != 'accepted':
            raise ValueError('Unexpected control response')
        if item['op'] == 'save' and not checkpoint.unsigned(response.get('request'), 64, nonzero=True):
            raise ValueError('Missing authoritative save request ID')
        return response
    def command(worker, op='status', epoch=0):
        item = finish(launch(worker, op, epoch))
        return accept(item), item
    def census():
        games = workers.checked_games()
        if {g['pid'] for g in games} != {p['game_pid'] for p in pinned.values()} or len(games) != 2:
            raise ValueError('Only the two pinned registered game processes may be running')
        for worker, value in pinned.items():
            if workers.current(worker) != value['current'] or not workers.supervisor_running(value['current']):
                raise ValueError('Pinned worker generation or supervisor changed')
        return games
    def manifests(paths):
        result = []
        for owner, path in paths:
            tree = service.diag.tree_manifest(path)
            result.append({'owner': owner, 'source': str(path), 'snapshot': tree, 'snapshot_sha256': checkpoint.digest(tree)})
        return result
    try:
        paths = []
        for worker in ('01', '02'):
            current = workers.current(worker)
            account, _ = checkpoint.account_paths(worker)
            disk = workers.read_json(Path(current['run']) / 'worker-status.json')
            if disk.get('generation') != current['generation'] or disk.get('supervisor_pid') != current['supervisor_pid'] or disk.get('state') != 'ready':
                raise ValueError('Matching ready supervisor metadata required')
            pinned[worker] = {'current': current, 'account': account, 'game_pid': disk['game_pid']}
            live, _ = command(worker); ready(live)
            pinned[worker]['before_status'] = live
            profile = service.diag.no_reparse(Path(account['profile']))
            paths += [(worker, profile / 'AppData/Roaming/Spore'), (worker, profile / 'Documents/My Spore Creations')]
            checkpoint.checked_trace(Path(current['run']) / f"actors-{disk['game_pid']}.jsonl", disk['game_pid'])
        report['pinned_workers'] = pinned
        report['os_games_before'] = census()
        paths += [('personal', source) for source in service.save_sources()]
        report['before_manifests'] = manifests(paths)
        # Revalidate immediately before admitting either irreversible native save.
        for worker in pinned:
            live, _ = command(worker); ready(live)
            before = pinned[worker]['before_status']
            if any(live[key] != before[key] for key in ('epoch', 'actor_a', 'actor_b')): raise ValueError('Scene or owned actor identity changed')
        census()
        tickets = []
        # Start both clients before waiting for either reply. Exactly one save per worker.
        with ThreadPoolExecutor(max_workers=2) as pool:
            for worker in ('01', '02'):
                ticket = launch(worker, 'save', pinned[worker]['before_status']['epoch'])
                tickets.append((worker, pool.submit(finish, ticket)))
            completed = [(worker, future.result()) for worker, future in tickets]
        saves = {}
        for worker, item in completed:
            response = accept(item)
            saves[worker] = {'response': response, 'command': item}
        report['saves'] = saves
        deadline = time.monotonic() + 120
        pending = set(pinned)
        while pending and time.monotonic() < deadline:
            for worker in sorted(pending):
                live, _ = command(worker)
                if live['epoch'] != pinned[worker]['before_status']['epoch']: raise ValueError('Save crossed a scene epoch')
                if live.get('persistence_state') in (7, 8): raise ValueError('Native save failed')
                if live.get('persistence_request') == saves[worker]['response']['request'] and live.get('persistence_state') == 3:
                    ready(live); saves[worker]['after_status'] = live; pending.remove(worker)
            if pending: time.sleep(.05)
        if pending: raise ValueError('Native save completion exceeded 120 seconds')
        # Trace output is buffered; allow it to flush after native scalar completion.
        pending = set(pinned)
        while pending and time.monotonic() < deadline:
            for worker in sorted(pending):
                pin, save = pinned[worker], saves[worker]
                path = Path(pin['current']['run']) / f"actors-{pin['game_pid']}.jsonl"
                rows = checkpoint.checked_trace(path, pin['game_pid'])
                request = save['response']['request']
                matches = {name: [row for row in rows if row.get('event') == name and row.get('request') == request]
                           for name in ('native_save_queued', 'native_save_enter', 'native_save_return')}
                if not matches['native_save_return']: continue
                if any(len(value) != 1 for value in matches.values()): raise ValueError('Save event count mismatch')
                queued, enter, returned = (matches[name][0] for name in matches)
                epoch = pin['before_status']['epoch']
                if returned.get('native_success') is not True or returned.get('durable') is not False or any(returned.get(key) != epoch for key in ('epoch', 'requested_epoch', 'observed_epoch')):
                    raise ValueError('Native save return or scene evidence is inconsistent')
                actors = checkpoint.fingerprints(rows, request)
                commit = next(row for row in rows if row.get('event') == 'checkpoint_snapshot' and row.get('request') == request)
                if not queued['sequence'] < commit['sequence'] < enter['sequence'] < returned['sequence']: raise ValueError('Snapshot/native save ordering is invalid')
                if any(row['qpc_frequency'] != report['qpc_frequency'] for row in (queued, enter, returned)): raise ValueError('QPC frequency mismatch')
                origin_low = save['command']['qpc_before'] - queued['qpc']
                origin_high = save['command']['qpc_after'] - queued['qpc']
                if origin_low > origin_high or not queued['qpc'] <= enter['qpc'] < returned['qpc']: raise ValueError('Invalid native save time interval')
                save.update(actors=actors, snapshot=commit, native_queued=queued, native_enter=enter, native_return=returned,
                            qpc_origin_bounds=[origin_low, origin_high],
                            guaranteed_native_interval_qpc=[origin_high + enter['qpc'], origin_low + returned['qpc']])
                trace_data = ''.join(json.dumps(row, separators=(',', ':')) + '\n' for row in rows).encode()
                trace_paths[worker].write_bytes(trace_data)
                save['trace_snapshot'] = {'path': str(trace_paths[worker]), 'sha256': hashlib.sha256(trace_data).hexdigest(), 'rows': len(rows), 'representation': 'parsed complete records, JSON re-encoded'}
                pending.remove(worker)
            if pending: time.sleep(.1)
        if pending: raise ValueError('Native save trace did not flush before the deadline')
        report['after_manifests'] = manifests(paths)
        report['os_games_after'] = census()
        report['personal_sources_unchanged'] = [row for row in report['before_manifests'] if row['owner'] == 'personal'] == [row for row in report['after_manifests'] if row['owner'] == 'personal']
        if not report['personal_sources_unchanged']: raise ValueError('Personal files changed during worker saves')
        intervals = [save['guaranteed_native_interval_qpc'] for save in saves.values()]
        overlap = min(end for _, end in intervals) - max(start for start, _ in intervals)
        report['native_overlap'] = {'proven': overlap > 0, 'guaranteed_overlap_ms': max(0, overlap) * 1000 / report['qpc_frequency'],
                                    'method': 'Each relative native QPC origin is bounded by the host QPC timestamps bracketing its save command/ack. Intersect latest possible enters with earliest possible returns; no point-estimate clock alignment.',
                                    'result': 'PROVEN' if overlap > 0 else 'NOT_PROVEN'}
        report['both_native_saves_completed'] = True
        report['native_acceptance'] = 'SAVE_OVERLAP_OBSERVED' if overlap > 0 else 'SAVES_COMPLETED_OVERLAP_NOT_PROVEN'
    except Exception as error:
        report['error'] = str(error)
    finally:
        report['ended_utc'] = service.diag.utc_now()
        report['exit_code'] = 0 if report.get('both_native_saves_completed') and report.get('native_overlap', {}).get('proven') else 1
        service.diag.write_json(output, report)
        journal.close()
    print(json.dumps({'output': str(output), 'exit_code': report['exit_code'], 'native_acceptance': report['native_acceptance'], 'error': report.get('error'), 'native_overlap': report.get('native_overlap')}))
    return report['exit_code']

if __name__ == '__main__': raise SystemExit(main())
