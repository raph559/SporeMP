"""Read-only correlation of original M04 .14 traces; never grants acceptance.

Only --output writes a new metadata report. No native process, control request,
desktop input, save mutation, backup restore or source edit is performed.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import stat

EXE = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
SDK = 'cbf9206b9a823f0911cd9be0217104a49d72380b'
VERSION = '0.0.14'


def require(condition, message):
    if not condition: raise ValueError(message)


def uint(value, bits=64):
    return type(value) is int and 0 <= value < 1 << bits


def local(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.exists():
            require(not part.is_symlink() and not getattr(part.stat(), 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT,
                    f'Reparse path refused: {part}')
    return path


def sha(path):
    with local(path).open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256')
    return digest.hexdigest()


def json_file(path):
    path = local(path)
    require(path.stat().st_size <= 16 * 1024 * 1024, f'Oversized metadata: {path}')
    value = json.loads(path.read_text(encoding='utf-8-sig'))
    require(isinstance(value, dict), f'Expected metadata object: {path}')
    return value


def json_lines(path, live=False):
    path = local(path)
    require(path.stat().st_size <= 40 * 1024 * 1024, f'Oversized trace: {path}')
    data = path.read_bytes()
    require(live or not data or data.endswith(b'\n'), f'Unfinished closed trace: {path}')
    rows = [json.loads(line) for line in data[:data.rfind(b'\n') + 1].splitlines()]
    require(all(isinstance(row, dict) for row in rows), f'Non-object trace record: {path}')
    return rows


def matching(rows, event, request=None):
    return [row for row in rows if row.get('event') == event and (request is None or row.get('request') == request)]


def exactly(rows, event, request=None):
    matches = matching(rows, event, request)
    require(len(matches) == 1, f'Expected one {event}, request={request}; got {len(matches)}')
    return matches[0]


def expected_artifacts(provenance):
    value = json_file(provenance)
    require(value.get('sdk_commit') == SDK and value.get('executable', {}).get('sha256') == EXE,
            'Provenance does not identify the pinned original executable/SDK')
    wanted = ('SporeMP.NativeHost.exe', 'SporeMP.Bridge.dll', 'SporeModAPI.dll', 'ModAPI.DLLInjector.dll')
    result = {}
    for name in wanted:
        entries = [row for row in value.get('artifacts', []) if Path(row.get('path', '')).name.lower() == name.lower()]
        require(len(entries) == 1, f'Expected one provenance artifact: {name}')
        digest = entries[0].get('sha256', '')
        require(isinstance(digest, str) and len(digest) == 64 and all(c in '0123456789abcdef' for c in digest), 'Invalid artifact hash')
        result[name.lower()] = digest
    return result


def analyze_run(directory, expected, allow_live):
    directory = local(directory)
    status_path = directory / 'worker-status.json'
    status = json_file(status_path)
    require(status.get('schema_version') == 1, 'Unknown worker status schema')
    generation = status.get('generation', '')
    require(isinstance(generation, str) and len(generation) == 32 and all(c in '0123456789abcdef' for c in generation) and int(generation, 16), 'Invalid generation')
    require(directory.name == generation, 'Run directory differs from worker generation')
    pid, supervisor = status.get('game_pid'), status.get('supervisor_pid')
    require(uint(pid, 32) and pid and uint(supervisor, 32) and supervisor, 'Invalid process identity')
    closed = status.get('state') in ('stopped', 'stopped_forced', 'crashed')
    require(closed or allow_live, 'Run remains active; use --allow-live for an explicitly incomplete snapshot')
    trace_path = directory / f'actors-{pid}.jsonl'
    rows = json_lines(trace_path, live=not closed)
    require(rows and rows[0].get('event') == 'trace_start', 'Missing native trace start')
    first = rows[0]
    require(first.get('executable_sha256') == EXE and first.get('sdk_commit') == SDK and first.get('bridge_version') == VERSION,
            'Wrong native executable/SDK/bridge identity; fixtures and older traces are refused')
    thread, frequency = first.get('thread_id'), first.get('qpc_frequency')
    require(uint(thread, 32) and thread and uint(frequency) and frequency, 'Invalid native thread/QPC provenance')
    previous_qpc = -1
    for sequence, row in enumerate(rows, 1):
        require(row.get('schema_version') == 1 and row.get('evidence_class') == 'NATIVE_PROBE' and row.get('harness') == 'M03', 'Trace is not original native evidence')
        require(type(row.get('sequence')) is int and row['sequence'] == sequence and type(row.get('pid')) is int and row['pid'] == pid,
                'Native trace sequence/PID mismatch')
        require(type(row.get('thread_id')) is int and row['thread_id'] == thread and type(row.get('foreign_callbacks')) is int and row['foreign_callbacks'] == 0,
                'Foreign native callback or wrong engine thread')
        require(uint(row.get('epoch')) and uint(row.get('qpc')) and row['qpc'] >= previous_qpc and row.get('qpc_frequency') == frequency,
                'Invalid native epoch/QPC ordering')
        previous_qpc = row['qpc']
    host_path = directory / 'native-host.jsonl'
    host = json_lines(host_path, live=not closed)
    require(all(row.get('host_pid') == supervisor for row in host), 'Host log PID mismatch')
    require(exactly(host, 'guard_started').get('mode') == '--worker', 'Host was not an original worker launch')
    require(exactly(host, 'created_suspended').get('game_pid') == pid, 'Host created another game PID')
    require(exactly(host, 'worker_supervisor_started').get('generation') == generation, 'Host generation mismatch')
    modules = []
    for name in ('sporemp.bridge.dll', 'sporemodapi.dll', 'modapi.dllinjector.dll'):
        entries = [row for row in matching(host, 'module') if Path(row.get('path', '')).name.lower() == name and row.get('game_pid') == pid]
        require(len(entries) == 1 and entries[0].get('sha256') == expected[name], f'Loaded module hash mismatch: {name}')
        module = entries[0]
        require(sha(module['path']) == expected[name], f'Preserved payload differs from loaded module: {name}')
        modules.append({'name': name, 'path': module['path'], 'sha256': module['sha256']})
    bridge_module = next(module for module in modules if module['name'] == 'sporemp.bridge.dll')
    host_exe = Path(bridge_module['path']).parent.parent / 'SporeMP.NativeHost.exe'
    require(sha(host_exe) == expected['sporemp.nativehost.exe'], 'Preserved native supervisor hash mismatch')
    lifecycle = json_lines(directory / f'bridge-{pid}.jsonl', live=not closed)
    initialized = exactly(lifecycle, 'initialize')
    require(initialized.get('pid') == pid and initialized.get('thread_id') == thread and initialized.get('bridge_version') == VERSION and
            initialized.get('executable_sha256') == EXE and initialized.get('sdk_commit') == SDK, 'Bridge lifecycle identity mismatch')
    clean = status.get('state') == 'stopped'
    if clean:
        stopped = exactly(rows, 'trace_stop')
        require(stopped is rows[-1] and stopped.get('healthy') is True and stopped.get('detach_status') == 0, 'Unclean native trace disposal')
        disposed = exactly(lifecycle, 'dispose')
        require(disposed.get('thread_id') == thread and disposed.get('pid') == pid, 'Bridge disposed on another thread/PID')
        require(exactly(host, 'game_exited').get('exit_code') == 0 and exactly(host, 'worker_exited').get('requested') is True, 'Clean stop lacks original game exit 0')

    def acknowledged(request, op):
        sent = exactly(host, 'worker_command_sent', request)
        result = exactly(host, 'worker_command_result', request)
        require(sent.get('op') == op and result.get('op') == op and result.get('result') == 'accepted', f'{op} request lacks matching accepted host reply')
        return sent

    jumps = []
    for queued in matching(rows, 'worker_command_queued'):
        if queued.get('verb') != 'jump' or queued.get('decision') != 'accepted': continue
        request, command, actor = queued['request'], queued['command'], queued['actor']
        acknowledged(request, 'jump')
        returned = [row for row in matching(rows, 'native_jump_return') if row.get('executing_command') == command and row.get('actor') == actor]
        landed = [row for row in matching(rows, 'native_landing') if row.get('jump_command') == command and row.get('actor') == actor and row.get('epoch') == queued['epoch']]
        complete = len(returned) == len(landed) == 1 and returned[0].get('accepted') is True and queued['sequence'] < returned[0]['sequence'] < landed[0]['sequence']
        jumps.append({'request': request, 'command': command, 'actor': actor, 'owner': queued['owner'], 'epoch': queued['epoch'],
                      'native_return_sequence': returned[0]['sequence'] if len(returned) == 1 else None,
                      'landing_sequence': landed[0]['sequence'] if len(landed) == 1 else None, 'complete': complete,
                      'return_to_landing_seconds': (landed[0]['qpc'] - returned[0]['qpc']) / frequency if complete else None})
    saves = []
    for returned in matching(rows, 'native_save_return'):
        request = returned['request']; acknowledged(request, 'save')
        queued, entered = exactly(rows, 'native_save_queued', request), exactly(rows, 'native_save_enter', request)
        captured = exactly(rows, 'checkpoint_snapshot', request)
        actors = matching(rows, 'checkpoint_actor', request)
        require(captured.get('valid') is True and len(actors) == 2 and sorted(actor.get('owner') for actor in actors) == [1, 2], 'Save lacks complete actor snapshot')
        require(queued['sequence'] < min(actor['sequence'] for actor in actors) and max(actor['sequence'] for actor in actors) < captured['sequence'] < entered['sequence'] < returned['sequence'], 'Snapshot/save event order mismatch')
        epoch_matches = returned.get('epoch') == returned.get('requested_epoch') == returned.get('observed_epoch') == captured['epoch']
        saves.append({'request': request, 'epoch': returned['epoch'], 'snapshot_sequence': captured['sequence'], 'return_sequence': returned['sequence'],
                      'native_success': returned.get('native_success') is True, 'epoch_matches': epoch_matches, 'elapsed_ms': returned.get('elapsed_ms'),
                      'durable': False, 'actors': actors})
    loads = []
    for completed in matching(rows, 'native_load_complete'):
        request = completed['request']; acknowledged(request, 'load')
        queued, dispatched = exactly(rows, 'native_load_queued', request), exactly(rows, 'native_load_requested', request)
        require(queued['sequence'] < dispatched['sequence'] < completed['sequence'] and completed['requested_epoch'] != completed['observed_epoch'] == completed['epoch'], 'Load epoch/order mismatch')
        require(completed['native_ai_after'] > completed['scene_native_ai_before'] and completed['native_ai_after'] > completed['native_ai_before'], 'Load lacks new-scene native AI progress')
        loads.append(completed)
    restores = []
    for restored in matching(rows, 'checkpoint_restore'):
        if restored.get('valid') is True:
            acknowledged(restored['request'], 'restore')
            require(restored.get('created_nouns') == 0 and restored.get('reason') == 'adopted_existing_nouns' and
                    restored.get('actor_a') and restored.get('actor_b') and restored['actor_a'] != restored['actor_b'], 'Invalid adoption evidence')
            require(any(load['epoch'] == restored['epoch'] and load['sequence'] < restored['sequence'] for load in loads), 'Adoption has no prior completed load in this epoch')
        restores.append(restored)
    return {'run': str(directory), 'generation': generation, 'game_pid': pid, 'supervisor_pid': supervisor,
            'native_structure_verified': True, 'live_snapshot': not closed, 'clean_native_exit': clean,
            'status': status, 'actor_records': len(rows), 'actor_trace_sha256': sha(trace_path), 'host_trace_sha256': sha(host_path),
            'host_executable_sha256': expected['sporemp.nativehost.exe'], 'loaded_payloads': modules,
            'event_counts': dict(Counter(row['event'] for row in rows)), 'jumps': jumps, 'saves': saves, 'loads': loads, 'restores': restores,
            'rejections': [row for row in matching(host, 'worker_command_result') if row.get('result') != 'accepted'],
            'persistence_failures': matching(rows, 'native_persistence_failed'),
            'pending_at_disposal': matching(rows, 'native_persistence_disposed_pending'),
            'native_paths': [row for row in lifecycle if row.get('event') == 'native_path']}


def related(path, runs):
    value = json_file(path)
    generation = value.get('generation') or value.get('worker', {}).get('generation')
    run = next((run for run in runs if run.get('generation') == generation and run.get('native_structure_verified')), None)
    require(run is not None, f'Related report has no verified matching run: {path}')
    if value.get('evidence_class') == 'NATIVE_IPC_ACTION_TEST':
        require(value.get('native_actions_observed') is True and value.get('jumps'), 'Action report did not observe actions')
        for jump in value['jumps']:
            require(any(native['complete'] and all(native[key] == jump.get(key) for key in
                ('request', 'command', 'actor', 'owner', 'native_return_sequence', 'landing_sequence')) for native in run['jumps']), 'Action report/native trace mismatch')
    elif value.get('kind') == 'm04-native-checkpoint-restore-test':
        require(value.get('restored') is True and value.get('adoption_record') in run['restores'], 'Restore report/native trace mismatch')
    elif value.get('kind') == 'm04-native-actor-checkpoint':
        fields = ('owner', 'native_id', 'herd_native_id', 'species_instance', 'species_type', 'species_group', 'archetype')
        matched_saves = [save for save in run['saves'] if save['request'] == value.get('save_request') and
                         save['epoch'] == value.get('save_epoch') and save['native_success'] and save['epoch_matches']]
        require(value.get('trace_sha256') == run['actor_trace_sha256'] and len(matched_saves) == 1, 'Sidecar/source save correlation mismatch')
        pair = sorted(({key: actor[key] for key in fields} for actor in matched_saves[0]['actors']), key=lambda actor: actor['owner'])
        require(pair == value.get('actors'), 'Sidecar actor fingerprints differ from original save trace')
    else: raise ValueError(f'Unknown related report schema: {path}')
    return {'path': str(local(path)), 'sha256': sha(path), 'generation': generation, 'correlated': True,
            'backup_or_native_checkpoint_files_verified_by_this_helper': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir', type=Path, action='append', required=True)
    parser.add_argument('--provenance', type=Path, required=True)
    parser.add_argument('--related-report', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--allow-live', action='store_true')
    args = parser.parse_args()
    output = local(args.output)
    require(not output.exists(), 'Evidence output already exists')
    expected = expected_artifacts(args.provenance)
    runs = []
    for directory in args.run_dir:
        try: runs.append(analyze_run(directory, expected, args.allow_live))
        except (OSError, ValueError, KeyError, TypeError) as error:
            runs.append({'run': str(directory), 'native_structure_verified': False, 'error': str(error)})
    reports = []
    for path in args.related_report:
        try: reports.append(related(path, runs))
        except (OSError, ValueError, KeyError, TypeError) as error: reports.append({'path': str(path), 'correlated': False, 'error': str(error)})
    valid = all(run['native_structure_verified'] for run in runs) and all(item['correlated'] for item in reports)
    result = {'schema_version': 1, 'evidence_class': 'NATIVE_TRACE_CORRELATION', 'native_acceptance': 'NOT_VERIFIED',
              'structural_correlation_passed': valid, 'utc': datetime.now(timezone.utc).isoformat(),
              'provenance': str(local(args.provenance)), 'provenance_sha256': sha(args.provenance), 'expected_artifacts': expected,
              'runs': runs, 'related_reports': reports,
              'limits': ['Original runtime outcomes only; no native compatibility acceptance from code prefixes or HOST ABI tests.',
                         'Live snapshots and deliberate crashes do not establish clean native disposal.',
                         'Native save return is not a durable acknowledgement; closed artifact integrity requires separate sealing evidence.',
                         'File/registry isolation, player and launcher lifetime, desktop behavior and visual outcomes require separate evidence.',
                         'No capacity, complete campaign ownership or whole M04 acceptance is inferred.']}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x', encoding='utf-8') as stream: json.dump(result, stream, indent=2); stream.write('\n')
    print(json.dumps({'structural_correlation_passed': valid, 'run_count': len(runs), 'output': str(output), 'native_acceptance': 'NOT_VERIFIED'}))
    return 0 if valid else 1


if __name__ == '__main__':
    raise SystemExit(main())
