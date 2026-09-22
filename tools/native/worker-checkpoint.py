"""Seal a closed M04 native checkpoint, then load and adopt its existing actors.

Developer-only: never launches SPORE, copies saves over a live profile, creates an
actor, sends desktop input, or retries a mutation. Native API returns and closed
file integrity remain separate evidence. Raw backups stay in ignored local/.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools/launcher'))
import launcher_service as service
import worker_manager as workers

FIELDS = ('native_id', 'herd_native_id', 'species_instance', 'species_type', 'species_group', 'archetype')
EXE_HASH = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
SDK = 'cbf9206b9a823f0911cd9be0217104a49d72380b'


def unsigned(value, bits, *, nonzero=False):
    return type(value) is int and int(nonzero) <= value < 1 << bits


def generation(value):
    if not isinstance(value, str) or len(value) != 32 or any(c not in '0123456789abcdef' for c in value) or int(value, 16) == 0:
        raise ValueError('Invalid checkpoint worker generation')
    return value


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def fingerprints(rows, request):
    if not unsigned(request, 64, nonzero=True) or not isinstance(rows, list) or not all(isinstance(r, dict) for r in rows):
        raise ValueError('Malformed native fingerprint request or trace')
    commits = [r for r in rows if r.get('event') == 'checkpoint_snapshot' and r.get('request') == request]
    actors = [r for r in rows if r.get('event') == 'checkpoint_actor' and r.get('request') == request]
    if len(commits) != 1 or commits[0].get('valid') is not True or len(actors) != 2:
        raise ValueError('Missing complete native actor fingerprint pair')
    if not unsigned(commits[0].get('epoch'), 64, nonzero=True) or type(commits[0].get('request')) is not int:
        raise ValueError('Malformed native snapshot epoch or request')
    if any(rows.index(actor) >= rows.index(commits[0]) for actor in actors):
        raise ValueError('Actor fingerprint was emitted after its snapshot commit')
    result = []
    for owner in (1, 2):
        matches = [a for a in actors if type(a.get('owner')) is int and a.get('owner') == owner]
        if len(matches) != 1: raise ValueError('Duplicate or missing actor owner')
        actor = {'owner': owner}
        for name in FIELDS:
            value = matches[0].get(name)
            if type(value) is not int or not (-0x80000000 if name == 'archetype' else 0) <= value <= 0xffffffff:
                raise ValueError('Malformed native actor fingerprint')
            actor[name] = value & 0xffffffff
        if actor['native_id'] in (0, 0xffffffff) or actor['herd_native_id'] in (0, 0xffffffff):
            raise ValueError('Missing native noun or herd identity')
        if type(matches[0].get('request')) is not int or not unsigned(matches[0].get('epoch'), 64, nonzero=True) or matches[0].get('epoch') != commits[0].get('epoch'):
            raise ValueError('Actor fingerprint crossed a scene boundary')
        result.append(actor)
    if result[0]['native_id'] == result[1]['native_id']:
        raise ValueError('A and B resolve to the same native noun')
    return result


def checked_trace(path, pid, closed=False, expected_sha256=None):
    if not unsigned(pid, 32, nonzero=True): raise ValueError('Invalid native trace PID')
    path = service.diag.no_reparse(path)
    if path.stat().st_size > 66 * 1024 * 1024: raise ValueError('Unexpected native trace size')
    data = path.read_bytes()
    if expected_sha256 is not None and hashlib.sha256(data).hexdigest() != expected_sha256:
        raise ValueError('Native source trace changed after checkpoint sealing')
    if closed and not data.endswith(b'\n'): raise ValueError('Incomplete closed trace')
    rows = [json.loads(line) for line in data[:data.rfind(b'\n') + 1].splitlines()]
    if not rows or not all(isinstance(row, dict) for row in rows) or rows[0].get('event') != 'trace_start': raise ValueError('Missing native provenance')
    if rows[0].get('executable_sha256') != EXE_HASH or rows[0].get('sdk_commit') != SDK or rows[0].get('bridge_version') != '0.0.14':
        raise ValueError('Wrong native persistence candidate')
    thread = rows[0].get('thread_id')
    if not unsigned(thread, 32, nonzero=True): raise ValueError('Invalid native thread provenance')
    for sequence, row in enumerate(rows, 1):
        if type(row.get('sequence')) is not int or row.get('sequence') != sequence or type(row.get('pid')) is not int or row.get('pid') != pid or type(row.get('foreign_callbacks')) is not int or row.get('foreign_callbacks') != 0 or type(row.get('thread_id')) is not int or row.get('thread_id') != thread:
            raise ValueError('Native trace process, sequence or thread mismatch')
    stops = [row for row in rows if row.get('event') == 'trace_stop']
    if closed and (len(stops) != 1 or stops[0] is not rows[-1] or stops[0].get('healthy') is not True or type(stops[0].get('detach_status')) is not int or stops[0].get('detach_status') != 0):
        raise ValueError('Closed native trace did not retire cleanly')
    return rows


def saved_fingerprints(rows):
    saves = [r for r in rows if r.get('event') == 'native_save_return']
    if not saves or saves[-1].get('native_success') is not True:
        raise ValueError('No successful final original save return')
    saved = saves[-1]
    actors = fingerprints(rows, saved.get('request'))
    commit = next(r for r in rows if r.get('event') == 'checkpoint_snapshot' and r.get('request') == saved['request'])
    if rows.index(commit) >= rows.index(saved): raise ValueError('Snapshot was captured after its original save')
    epoch = saved.get('epoch')
    if not unsigned(epoch, 64, nonzero=True) or epoch != commit['epoch'] or any(not unsigned(saved.get(key), 64, nonzero=True) or saved[key] != epoch for key in ('requested_epoch', 'observed_epoch')):
        raise ValueError('Scene changed between snapshot and save')
    return saved, actors


def account_paths(worker_id):
    account = workers.read_json(workers.root_for(worker_id) / 'account.json')
    expected = Path(f'C:/Users/SporeMP-M04-{worker_id}')
    if not account.get('prepared') or Path(account.get('profile', '')) != expected or account.get('user') != expected.name:
        raise ValueError('Prepared worker profile identity changed')
    return account, service.diag.no_reparse(expected / 'AppData/Roaming/Spore/Games')


def seal(worker_id, output):
    if service.diag.game_running(): raise ValueError('Close all SPORE processes normally before sealing')
    current = workers.current(worker_id)
    if workers.supervisor_running(current): raise ValueError('Worker supervisor is still running')
    status = workers.read_json(Path(current['run']) / 'worker-status.json')
    if status.get('generation') != current['generation'] or status.get('state') != 'stopped':
        raise ValueError('Checkpoint requires a cleanly stopped matching worker')
    trace = Path(current['run']) / f"actors-{status['game_pid']}.jsonl"
    trace_hash = service.diag.fingerprint(trace)['sha256']
    rows = checked_trace(trace, status['game_pid'], closed=True, expected_sha256=trace_hash)
    saved, actors = saved_fingerprints(rows)
    account, games = account_paths(worker_id)
    backup = REPO / 'local/worker-checkpoints' / worker_id / current['generation']
    artifact = service.diag.backup([games], backup)
    manifest = artifact['sources'][0]['snapshot']
    if not manifest['exists'] or not manifest['files']: raise ValueError('Empty native checkpoint tree')
    if service.diag.game_running(): raise ValueError('SPORE started during checkpoint sealing')
    if service.diag.fingerprint(trace)['sha256'] != trace_hash:
        raise ValueError('Native trace changed during checkpoint sealing')
    value = {'schema_version': 1, 'kind': 'm04-native-actor-checkpoint', 'worker_id': worker_id,
             'sid': account['sid'], 'generation': current['generation'], 'run': current['run'], 'game_pid': status['game_pid'],
             'save_request': saved['request'], 'save_epoch': saved['epoch'], 'actors': actors,
             'native_executable_sha256': EXE_HASH, 'sdk_commit': SDK, 'bridge_version': '0.0.14',
             'trace_sha256': trace_hash, 'backup': str(backup),
             'backup_manifest_sha256': service.diag.fingerprint(backup / 'backup-manifest.json')['sha256'],
             'games': manifest, 'games_sha256': digest(manifest), 'utc': service.diag.utc_now(),
             'native_save_returned_success': True, 'native_reload_and_identity_restore': 'NOT RUN',
             'scope': 'A/B native actor ownership only; B campaign player/rewards absent'}
    service.diag.write_json(output, value)
    return {'sealed': True, 'sidecar': str(output), 'games_sha256': value['games_sha256'], 'native_restore': 'NOT RUN'}


def validate_sidecar(value, worker_id, games, account):
    if not isinstance(value, dict): raise ValueError('Malformed checkpoint sidecar')
    if value.get('schema_version') != 1 or value.get('kind') != 'm04-native-actor-checkpoint' or value.get('worker_id') != worker_id or value.get('sid') != account['sid']:
        raise ValueError('Checkpoint belongs to a different worker/profile')
    if value.get('native_executable_sha256') != EXE_HASH or value.get('sdk_commit') != SDK or value.get('bridge_version') != '0.0.14':
        raise ValueError('Checkpoint native compatibility mismatch')
    manifest = value.get('games')
    if not isinstance(manifest, dict) or manifest.get('exists') is not True or not manifest.get('files') or digest(manifest) != value.get('games_sha256') or service.diag.tree_manifest(games) != manifest:
        raise ValueError('Native checkpoint differs from its sealed sidecar')
    # Reuse strict fingerprint validation, including widths and duplicate owners.
    actors = value.get('actors')
    if not isinstance(actors, list) or len(actors) != 2 or not all(isinstance(a, dict) for a in actors): raise ValueError('Missing or malformed actors')
    synthetic = [{**a, 'event': 'checkpoint_actor', 'request': 1, 'epoch': 1} for a in actors]
    synthetic += [{'event': 'checkpoint_snapshot', 'request': 1, 'valid': True, 'epoch': 1}]
    pair = fingerprints(synthetic, 1)
    nonce = generation(value.get('generation'))
    expected_run = Path(f'C:/ProgramData/SporeMP/M04/{worker_id}/runs') / nonce
    if Path(value.get('run', '')) != expected_run: raise ValueError('Checkpoint native trace escaped its source workspace')
    if not unsigned(value.get('game_pid'), 32, nonzero=True): raise ValueError('Invalid checkpoint source PID')
    source = service.diag.no_reparse(expected_run / f"actors-{value['game_pid']}.jsonl")
    source_hash = value.get('trace_sha256')
    if not isinstance(source_hash, str) or len(source_hash) != 64 or any(c not in '0123456789abcdef' for c in source_hash):
        raise ValueError('Malformed native source trace hash')
    source_rows = checked_trace(source, value['game_pid'], closed=True, expected_sha256=source_hash)
    saved, source_pair = saved_fingerprints(source_rows)
    if not unsigned(value.get('save_request'), 64, nonzero=True) or not unsigned(value.get('save_epoch'), 64, nonzero=True) or value['save_request'] != saved['request'] or value['save_epoch'] != saved['epoch'] or pair != source_pair or value.get('native_save_returned_success') is not True:
        raise ValueError('Sidecar actor identity disagrees with original save evidence')
    backup = service.diag.no_reparse(Path(value['backup']))
    expected = REPO / 'local/worker-checkpoints' / worker_id / nonce
    if backup != expected: raise ValueError('Checkpoint backup escaped its fixed workspace')
    if service.diag.fingerprint(backup / 'backup-manifest.json')['sha256'] != value.get('backup_manifest_sha256'):
        raise ValueError('Backup manifest changed after checkpoint sealing')
    service.diag.verify_backup(backup)
    backed = json.loads((backup / 'backup-manifest.json').read_text(encoding='utf-8'))
    if len(backed['sources']) != 1 or Path(backed['sources'][0]['source']) != games or backed['sources'][0]['snapshot'] != manifest:
        raise ValueError('Backup and sidecar disagree')
    return pair


def pack_actors(actors):
    return [actor[FIELDS[i]] | (actor[FIELDS[i + 1]] << 32) for actor in actors for i in (0, 2, 4)]


def restored_evidence(rows, request, status):
    if not unsigned(request, 64, nonzero=True): raise ValueError('Invalid adoption request')
    matches = [row for row in rows if row.get('event') == 'checkpoint_restore' and row.get('request') == request]
    if len(matches) != 1 or type(matches[0].get('request')) is not int or matches[0].get('valid') is not True or type(matches[0].get('created_nouns')) is not int or matches[0].get('created_nouns') != 0 or matches[0].get('reason') != 'adopted_existing_nouns':
        raise ValueError('Missing original noun adoption evidence')
    row = matches[0]
    if any(not unsigned(row.get(key), 64, nonzero=True) or not unsigned(status.get(key), 64, nonzero=True) or row[key] != status[key] for key in ('epoch', 'actor_a', 'actor_b')) or row['actor_a'] == row['actor_b']:
        raise ValueError('Adopted native identities disagree with worker status')
    return row


def load_restore(worker_id, sidecar, output):
    report = {'schema_version': 1, 'kind': 'm04-native-checkpoint-restore-test', 'worker_id': worker_id,
              'commands': [], 'restored': False, 'milestone_acceptance': 'NOT_VERIFIED'}
    try:
        value = workers.current(worker_id)  # Pin one generation across all requests.
        account, games = account_paths(worker_id)
        metadata = workers.read_json(sidecar)
        actors = validate_sidecar(metadata, worker_id, games, account)
        if value['generation'] == metadata['generation']: raise ValueError('A fresh process generation is required')
        report['generation'] = value['generation']
        def command(op, epoch=0, *parameters, expected='accepted'):
            argv = [str(REPO / 'build/win32/Release/SporeMP.WorkerControl.exe'), value['generation'], str(value['supervisor_pid']), op]
            if op != 'status': argv += [str(epoch), *map(str, parameters)]
            result = subprocess.run(argv, capture_output=True, text=True, timeout=13, creationflags=subprocess.CREATE_NO_WINDOW)
            report['commands'].append({'argv': argv, 'exit_code': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr})
            if result.returncode not in (0, 5): raise ValueError('IPC outcome unknown; mutation not retried')
            response = json.loads(result.stdout)
            if not isinstance(response, dict) or response.get('schema_version') != 1 or response.get('op') != op or response.get('result') != expected or not unsigned(response.get('epoch'), 64) or not unsigned(response.get('request'), 64, nonzero=op != 'status'): raise ValueError(f'{op} refused or malformed')
            return response
        live = command('status')
        if live['phase'] != 1 or live['actor_a'] or live['actor_b']:
            raise ValueError('Fresh original menu required, without actor setup')
        load = command('load', live['epoch'])
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            live = command('status')
            if live.get('persistence_request') == load['request'] and live.get('persistence_state') == 6: break
            if live.get('persistence_state') in (7, 8): raise ValueError('Original native loading failed')
            time.sleep(.25)
        else: raise ValueError('Original load did not complete within 120 seconds')
        if live['actor_a'] or live['actor_b']: raise ValueError('Actors were bound before explicit checkpoint adoption')
        # The save files must still match when actor ownership is about to mutate.
        validate_sidecar(metadata, worker_id, games, account)
        adoption = command('restore', live['epoch'], *pack_actors(actors))
        restored = command('status')
        if not restored['actor_a'] or not restored['actor_b'] or restored['actor_a'] == restored['actor_b'] or restored['epoch'] != live['epoch']:
            raise ValueError('Native A/B ownership adoption did not complete')
        status = workers.read_json(Path(value['run']) / 'worker-status.json')
        if status.get('generation') != value['generation']:
            raise ValueError('Destination native trace generation mismatch')
        trace = Path(value['run']) / f"actors-{status['game_pid']}.jsonl"
        adoption_record = restored_evidence(checked_trace(trace, status['game_pid']), adoption['request'], restored)
        command('jump', restored['epoch'], 1, restored['actor_b'], expected='invalid')
        command('jump', restored['epoch'] - 1, 2, restored['actor_b'], expected='stale')
        report.update(restored=True, status=restored, checkpoint_sha256=metadata['games_sha256'],
                      source_trace_sha256=metadata['trace_sha256'], adoption_record=adoption_record,
                      note='Matching loaded noun ownership observed; run --require-existing-actors action probe for native action outcomes.')
    except Exception as error:
        report['error'] = str(error)
    report['utc'] = service.diag.utc_now()
    service.diag.write_json(output, report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('seal', 'load-restore'))
    parser.add_argument('worker_id', choices=('01', '02'))
    parser.add_argument('--sidecar', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    output = service.diag.no_reparse(args.output)
    if output.exists(): raise ValueError('Evidence already exists')
    if args.action == 'seal': result = seal(args.worker_id, output)
    else:
        if args.sidecar is None: raise ValueError('--sidecar is required')
        result = load_restore(args.worker_id, service.diag.no_reparse(args.sidecar), output)
    print(json.dumps(result))
    return 0 if result.get('sealed') or result.get('restored') else 1


if __name__ == '__main__': raise SystemExit(main())
