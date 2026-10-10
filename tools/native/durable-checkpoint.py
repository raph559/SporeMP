"""M09 closed native checkpoint sealing, validated export and protected restore.

Developer recovery commands for the qualified disposable Creature profiles.
Never launches a game, replays a reward, migrates a schema or writes personal saves.
The coordinator exclusively publishes the artifact and metadata in one revision.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import sys
from uuid import uuid4

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools/launcher'))
import launcher_service as service
import worker_manager as workers
import multiplayer_session as multiplayer

MAX_BYTES = 80 * 1024 * 1024
EXE = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
SDK = 'cbf9206b9a823f0911cd9be0217104a49d72380b'
MAGIC = 0x39424D53
ROLLBACK_MS = 300000


def sha(data):
    return hashlib.sha256(data).hexdigest()


def atomic_json(path, value):
    path = service.diag.no_reparse(path)
    temporary = path.with_name(path.name + '.' + uuid4().hex + '.tmp')
    with temporary.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2); stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
    os.replace(temporary, path)


def hex_digest(value):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{64}', value) or value == '0' * 64:
        raise ValueError('Invalid checkpoint digest')
    return bytes.fromhex(value)


def member_name(value):
    if not isinstance(value, str) or len(value) > 240 or not value.startswith(('spore/', 'creations/')):
        raise ValueError('Invalid checkpoint member root')
    for part in value.split('/'):
        if not part or part in ('.', '..') or part[-1] in '. ' or any(ord(c) < 32 or ord(c) > 126 or c in '\\:*?"<>|' for c in part):
            raise ValueError('Unsafe checkpoint member')
        stem = part.split('.')[0].upper()
        if stem in ('CON', 'PRN', 'AUX', 'NUL') or re.fullmatch('(COM|LPT)[0-9]', stem):
            raise ValueError('Reserved checkpoint member')
    return value


def read_artifact(data):
    if len(data) > MAX_BYTES:
        raise ValueError('Checkpoint size limit')
    pos = 0
    def take(n):
        nonlocal pos
        if n < 0 or n > len(data) - pos:
            raise ValueError('Truncated checkpoint')
        result = data[pos:pos+n]; pos += n
        return result
    def word(): return struct.unpack('<I', take(4))[0]
    def blob(limit):
        count = word()
        if count > limit: raise ValueError('Checkpoint member size limit')
        return take(count)
    if word() != MAGIC or word() != 1:
        raise ValueError('Checkpoint schema migration required')
    result = {k: take(32).hex() for k in ('build_sha256', 'executable_sha256', 'content_sha256')}
    result['world'] = {field: take(32).hex() for field, _ in multiplayer.WORLD_FILES}
    result['provenance_sha256'] = take(32).hex()
    result['save_request'] = struct.unpack('<Q', take(8))[0]
    result['rollback_ms'] = word()
    result['native_ids'] = [word() for _ in range(4)]
    count = word()
    for value in [result[k] for k in ('build_sha256', 'executable_sha256', 'content_sha256', 'provenance_sha256')] + list(result['world'].values()):
        hex_digest(value)
    ids = result['native_ids']
    if result['rollback_ms'] != ROLLBACK_MS or not result['save_request'] or not 6 <= count <= 512 or any(i in (0, 0xffffffff) for i in ids) or ids[0] == ids[1] or ids[2] == ids[3]:
        raise ValueError('Invalid checkpoint metadata')
    members, names = {}, set()
    for _ in range(count):
        name = member_name(blob(240).decode('ascii'))
        if name.casefold() in names: raise ValueError('Duplicate checkpoint member')
        names.add(name.casefold())
        expected = take(32).hex(); content = blob(MAX_BYTES)
        if sha(content) != expected: raise ValueError('Checkpoint member hash mismatch')
        members[name] = content
    if pos != len(data): raise ValueError('Trailing checkpoint bytes')
    for field, relative in multiplayer.WORLD_FILES:
        if 'spore/' + relative not in members or sha(members['spore/' + relative]) != result['world'][field]:
            raise ValueError('Checkpoint world is missing or mismatched')
    result['artifact_sha256'] = sha(data)
    return result, members


def closed_trace(current):
    status = workers.read_json(Path(current['run']) / 'worker-status.json')
    if status.get('generation') != current['generation'] or status.get('state') != 'stopped' or workers.supervisor_running(current):
        raise ValueError('Checkpoint requires a cleanly closed matching worker')
    path = service.diag.no_reparse(Path(current['run']) / f"actors-{status['game_pid']}.jsonl")
    if path.stat().st_size > 66 * 1024 * 1024: raise ValueError('Trace size limit')
    data = path.read_bytes()
    if not data.endswith(b'\n'): raise ValueError('Incomplete trace')
    rows = [json.loads(line) for line in data.splitlines()]
    if not rows or rows[0].get('event') != 'trace_start' or rows[0].get('executable_sha256') != EXE or rows[0].get('sdk_commit') != SDK:
        raise ValueError('Native checkpoint provenance mismatch')
    thread = rows[0].get('thread_id')
    for n, r in enumerate(rows, 1):
        if r.get('sequence') != n or r.get('pid') != status['game_pid'] or r.get('thread_id') != thread or r.get('foreign_callbacks') != 0:
            raise ValueError('Native trace sequence/thread/process mismatch')
    if rows[-1].get('event') != 'trace_stop' or rows[-1].get('healthy') is not True or rows[-1].get('detach_status') != 0:
        raise ValueError('Native checkpoint did not close cleanly')
    return rows, sha(data)


def saved_metadata(rows):
    complete = [r for r in rows if r.get('event') == 'native_checkpoint_save_complete']
    if not complete or complete[-1].get('success') is not True or complete[-1].get('diagnostic_only') is not False:
        raise ValueError('No complete production native checkpoint save')
    end = complete[-1]; request = end['request']; epoch = end['epoch']
    def one(event):
        selected = [r for r in rows if r.get('event') == event and r.get('request') == request]
        if len(selected) != 1 or selected[0].get('epoch') != epoch: raise ValueError('Missing native save barrier witness: ' + event)
        return selected[0]
    barrier = one('native_checkpoint_barrier')
    snapshot = one('checkpoint_probe_snapshot')
    extension = one('native_checkpoint_extension_written')
    saved = one('native_save_return')
    schema = barrier.get('schema')
    if not snapshot.get('valid') or not extension.get('success') or not saved.get('native_success') or schema not in (3, 4) or extension.get('schema') != schema:
        raise ValueError('Original save/extension did not complete')
    if not snapshot['sequence'] < barrier['sequence'] < extension['sequence'] < saved['sequence'] < end['sequence'] or saved.get('requested_epoch') != epoch or saved.get('observed_epoch') != epoch:
        raise ValueError('Native save barrier crossed a scene or ordering boundary')
    if schema == 4:
        # Extra native roots must actually have been written inside this save.
        # Neither successful IPC nor a previous save's receipt can seal them.
        previous = barrier['sequence']
        for name, replay in [('native_relationship_checkpoint_written', 'replayed_actions'),
                             ('native_history_checkpoint_written', 'replayed_events')]:
            matches = [r for r in rows if r.get('event') == name and
                       barrier['sequence'] < r.get('sequence', 0) < extension['sequence']]
            if len(matches) != 1:
                raise ValueError('Missing private native checkpoint root: ' + name)
            row = matches[0]
            if (row.get('epoch') != epoch or row.get('owner') != 2 or row.get('success') is not True or
                    row.get('primary_unchanged') is not True or row.get(replay) != 0 or
                    row['sequence'] <= previous):
                raise ValueError('Private native checkpoint root did not preserve ownership: ' + name)
            previous = row['sequence']
    pair = [r for r in rows if r.get('event') == 'checkpoint_actor' and r.get('request') == request]
    actors = [next((r['native_id'] for r in pair if r.get('owner') == owner), None) for owner in (1, 2)]
    players = [r for r in rows if r.get('event') == 'native_checkpoint_player' and r.get('request') == request and r.get('phase') == 'before_save']
    # Native census naming is explicit and verified below; never guess an ID.
    ids = actors + [next((r['player_native_id'] for r in players if r.get('primary') is True), None), extension['player_native_id']]
    if len(pair) != 2 or any(type(x) is not int or not 0 < x < 0xffffffff for x in ids):
        raise ValueError('Missing native actor/player identity mapping')
    return {'request': request, 'epoch': epoch, 'native_ids': ids, 'bridge_version': rows[0]['bridge_version']}


def profile_paths(worker):
    if worker not in ('01', '02', '03'): raise ValueError('Only verified disposable workers 01-03 are supported')
    account = workers.read_json(workers.root_for(worker) / 'account.json')
    root = Path(f'C:/Users/SporeMP-M04-{worker}')
    if not account.get('prepared') or Path(account.get('profile', '')) != root or account.get('user') != root.name:
        raise ValueError('Prepared disposable profile changed')
    return [service.diag.no_reparse(root / 'AppData/Roaming/Spore'), service.diag.no_reparse(root / 'Documents/My Spore Creations')]


def no_games():
    if service.diag.game_running(): raise ValueError('Close all original SPORE processes before checkpoint file operations')


def new_output(path):
    path = service.diag.no_reparse(Path(path).absolute())
    if not path.is_relative_to(REPO / 'local'): raise ValueError('Raw checkpoints must stay under ignored local/')
    path.mkdir()
    return path


def seal(worker, config_path, output):
    no_games(); current = workers.current(worker)
    rows, trace_hash = closed_trace(current); metadata = saved_metadata(rows)
    config = dict(line.split('=', 1) for line in Path(config_path).read_text().splitlines())
    bridge = service.diag.no_reparse(Path(current['payload']) / 'mLibs/SporeMP.Bridge.dll')
    if service.diag.fingerprint(bridge)['sha256'] != config.get('build_sha256') or config.get('executable_sha256') != EXE:
        raise ValueError('Native payload does not match the coordinator build')
    output = new_output(output); paths = profile_paths(worker)
    service.diag.backup(paths, output / 'protected-backup'); service.diag.verify_backup(output / 'protected-backup')
    world = multiplayer.world_identity_fields(paths[0], service.diag.no_reparse)
    members = {}
    for prefix, root in zip(('spore', 'creations'), paths):
        for path in sorted(root.rglob('*')):
            service.diag.no_reparse(path)
            if path.is_dir(): continue
            if not path.is_file() or path.stat().st_nlink != 1: raise ValueError('Unsafe native artifact member')
            name = member_name(prefix + '/' + path.relative_to(root).as_posix())
            if path.stat().st_size > MAX_BYTES: raise ValueError('Native artifact size limit')
            members[name] = path.read_bytes()
            if sum(len(v) for v in members.values()) > MAX_BYTES: raise ValueError('Native artifact size limit')
    no_games()
    for path, source in zip(paths, workers.read_json(output / 'protected-backup/backup-manifest.json')['sources']):
        if service.diag.tree_manifest(path) != source['snapshot']: raise ValueError('Native files changed during seal')
    proof = dict(metadata=metadata, trace_sha256=trace_hash, worker_generation=current['generation'], world=world)
    provenance = sha(json.dumps(proof, sort_keys=True, separators=(',', ':')).encode())
    data = bytearray(struct.pack('<II', MAGIC, 1))
    for field in ('build_sha256', 'executable_sha256', 'content_sha256'): data.extend(hex_digest(config[field]))
    for field, _ in multiplayer.WORLD_FILES: data.extend(hex_digest(world[field]))
    data.extend(hex_digest(provenance)); data.extend(struct.pack('<QI4II', metadata['request'], ROLLBACK_MS, *metadata['native_ids'], len(members)))
    def blob(content): data.extend(struct.pack('<I', len(content))); data.extend(content)
    for name, content in sorted(members.items()):
        blob(name.encode('ascii')); data.extend(bytes.fromhex(sha(content))); blob(content)
    result, _ = read_artifact(data)
    with (output / 'checkpoint.smp').open('xb') as stream:
        stream.write(data); stream.flush(); os.fsync(stream.fileno())
    service.diag.write_json(output / 'native-proof.json', proof)
    service.diag.write_json(output / 'seal.json', result)
    return dict(sealed=True, durable=False, artifact=str(output / 'checkpoint.smp'), **result)


def restore(artifact, selected, output):
    no_games(); path = service.diag.no_reparse(Path(artifact).absolute())
    if path.stat().st_size > MAX_BYTES or path.stat().st_nlink != 1: raise ValueError('Unsafe checkpoint artifact')
    metadata, members = read_artifact(path.read_bytes())
    if metadata['executable_sha256'] != EXE or len(selected) != len(set(selected)): raise ValueError('Unsupported restore target')
    roots = [(worker, profile_paths(worker)) for worker in selected]
    output = new_output(output); journal = []
    service.diag.write_json(output / 'restore.json', dict(schema_version=1,state='preparing', artifact=metadata, entries=journal))
    # Prepare and verify every replacement before switching the first profile.
    for worker, paths in roots:
        # A freshly prepared worker has no current.json until its first start;
        # no supervisor can be running. Invalid metadata still refuses.
        try: current = workers.current(worker)
        except FileNotFoundError: current = None
        if current is not None and workers.supervisor_running(current): raise ValueError('A target supervisor is still running')
        service.diag.backup(paths, output / ('backup-' + worker)); service.diag.verify_backup(output / ('backup-' + worker))
        for prefix, root in zip(('spore', 'creations'), paths):
            staged = root.with_name(root.name + '.m09-staged-' + uuid4().hex)
            retained = root.with_name(root.name + '.m09-retained-' + uuid4().hex)
            staged.mkdir()
            for name, content in members.items():
                if not name.startswith(prefix + '/'): continue
                target = staged / name[len(prefix)+1:]
                target.parent.mkdir(parents=True, exist_ok=True)
                with target.open('xb') as stream:
                    stream.write(content); stream.flush(); os.fsync(stream.fileno())
                if service.diag.fingerprint(target)['sha256'] != sha(content): raise ValueError('Staged restore hash mismatch')
            journal.append(dict(worker=worker, active=str(root), staged=str(staged), retained=str(retained), old=service.diag.tree_manifest(root), new=service.diag.tree_manifest(staged), applied=False))
    def record(state): atomic_json(output / 'restore.json', dict(schema_version=1,state=state, artifact=metadata, entries=journal))
    record('prepared')
    try:
        for entry in journal:
            no_games(); active, staged, retained = (Path(entry[k]) for k in ('active', 'staged', 'retained'))
            if active not in profile_paths(entry['worker']) or service.diag.tree_manifest(active) != entry['old'] or service.diag.tree_manifest(staged) != entry['new'] or retained.exists():
                raise ValueError('Profile changed before atomic switch')
            active.rename(retained); entry['retained_moved'] = True; record('switching')
            staged.rename(active); entry['applied'] = True; record('switching')
        record('restored')
    except Exception:
        undo(output / 'restore.json')
        raise
    return dict(restored=True, native_load='NOT_RUN', protected_backups=str(output), **metadata)


def undo(journal_path):
    no_games(); journal_path = service.diag.no_reparse(Path(journal_path).absolute()); value = workers.read_json(journal_path)
    if not journal_path.is_relative_to(REPO / 'local') or value.get('state') not in ('prepared', 'switching', 'restored'): raise ValueError('Unsupported restoration journal')
    for entry in reversed(value['entries']):
        root, retained = Path(entry['active']), Path(entry['retained'])
        if root not in profile_paths(entry['worker']) or retained.parent != root.parent or not retained.name.startswith(root.name + '.m09-retained-'): raise ValueError('Restoration target mismatch')
        if not retained.exists():
            if service.diag.tree_manifest(root) != entry['old']: raise ValueError('Unswitched original tree changed')
            continue
        if service.diag.tree_manifest(retained) != entry['old']: raise ValueError('Protected original tree changed')
        no_games()
        if root.exists():
            # Keep all post-restore work, even when the tested game changed it.
            retained_result = journal_path.parent / (entry['worker'] + '-' + root.name + '-after-' + uuid4().hex)
            root.rename(retained_result); entry['post_restore'] = str(retained_result)
        retained.rename(root)
        if service.diag.tree_manifest(root) != entry['old']: raise ValueError('Protected original restore hash mismatch')
    value['state'] = 'originals_restored'; atomic_json(journal_path, value)
    return dict(originals_restored=True, journal=str(journal_path))


def main():
    parser = argparse.ArgumentParser(description=__doc__); sub = parser.add_subparsers(dest='op', required=True)
    seal_cli = sub.add_parser('seal'); seal_cli.add_argument('--worker', required=True); seal_cli.add_argument('--config', type=Path, required=True); seal_cli.add_argument('--output', type=Path, required=True)
    restore_cli = sub.add_parser('restore'); restore_cli.add_argument('--artifact', type=Path, required=True); restore_cli.add_argument('--workers', nargs='+', required=True); restore_cli.add_argument('--output', type=Path, required=True)
    undo_cli = sub.add_parser('undo'); undo_cli.add_argument('--journal', type=Path, required=True)
    inspect_cli = sub.add_parser('inspect'); inspect_cli.add_argument('--artifact', type=Path, required=True)
    args = parser.parse_args()
    if args.op == 'seal': result = seal(args.worker, args.config, args.output)
    elif args.op == 'restore': result = restore(args.artifact, args.workers, args.output)
    elif args.op == 'undo': result = undo(args.journal)
    else:
        if args.artifact.stat().st_size > MAX_BYTES: raise ValueError('Checkpoint size limit')
        result, _ = read_artifact(args.artifact.read_bytes())
    print(json.dumps(result))


if __name__ == '__main__':
    try: main()
    except (ValueError, OSError, KeyError) as error:
        print(json.dumps(dict(error=str(error)))); raise SystemExit(1)
