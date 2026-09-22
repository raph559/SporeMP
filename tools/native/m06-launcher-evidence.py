"""Archive bounded evidence from actual Home Join/Rejoin without copying secrets.

This is a developer evidence reader. It never starts or controls a game/launcher
and cannot grant milestone or visual acceptance.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys

REPO = Path(__file__).resolve().parents[2]
PINNED_EXE = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
PINNED_SDK = 'cbf9206b9a823f0911cd9be0217104a49d72380b'
MAX_BYTES = 66 * 1024 * 1024


def utc():
    return datetime.now(timezone.utc).isoformat()


def safe_path(path):
    path = Path(os.path.abspath(path))
    for part in [*reversed(path.parents), path]:
        try:
            value = part.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(value.st_mode) or getattr(value, 'st_file_attributes', 0) & 0x400:
            raise ValueError('Reparse paths are not evidence sources')
    return path


def strict_json(value):
    def pairs(items):
        result = {}
        for key, item in items:
            if key in result:
                raise ValueError('Duplicate JSON key')
            result[key] = item
        return result
    def invalid(_):
        raise ValueError('Nonfinite JSON number')
    return json.loads(value, object_pairs_hook=pairs, parse_constant=invalid)


def read_bounded(path):
    path = safe_path(path)
    with path.open('rb') as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size > MAX_BYTES:
            raise ValueError('Evidence exceeds regular-file size bound')
        data = stream.read(MAX_BYTES + 1)
        after = os.fstat(stream.fileno())
    current = path.stat()
    identity = lambda value: (value.st_size, value.st_mtime_ns, value.st_ino, value.st_dev)
    if len(data) > MAX_BYTES or identity(before) != identity(after) or identity(after) != identity(current):
        raise ValueError('Evidence changed during read; close the session and retry with a fresh destination')
    return data


def no_secret_content(data):
    # Never inspect a session configuration to obtain a token for redaction.
    if re.search(rb'sporemp://|(?:[?&]|["\s])(?:credential|token)(?:["\s]*[:=])', data, re.I):
        raise ValueError('Potential invitation/credential text refused from evidence')


def archive(path, destination):
    data = read_bounded(path)
    no_secret_content(data)
    if path.suffix == '.jsonl':
        if data and not data.endswith(b'\n'):
            raise ValueError('Incomplete JSONL tail; close the session before archiving')
        parsed = [strict_json(line) for line in data.splitlines() if line.strip()]
    else:
        parsed = strict_json(data)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('xb') as stream:
        stream.write(data)
    return parsed, {'source': str(path), 'archive': str(destination), 'bytes': len(data),
                    'sha256': hashlib.sha256(data).hexdigest()}


def analyze_run(host, actors, bridge, backend, expected_player):
    selected = lambda events, name: [event for event in events if event.get('event') == name]
    starts = selected(host, 'created_suspended')
    stops = selected(host, 'game_exited')
    trace_start = selected(actors, 'trace_start')
    trace_stop = selected(actors, 'trace_stop')
    statuses = selected(actors, 'network_status')
    connected = [event for event in statuses if event.get('state') == 'connected']
    samples = selected(actors, 'scene_native_replica_sample')
    avatars = [event for event in samples if event.get('is_native_avatar') is True]
    identities = sorted({(event.get('remote_entity'), event.get('entity_generation'), event.get('owner')) for event in avatars})
    native_ids = sorted({event.get('native_id') for event in avatars})
    applied = selected(actors, 'scene_baseline_applied')
    errors = [event for event in statuses if event.get('state') == 'error' and
              event.get('detail') != 'The native scene was closed. Rejoin from the launcher.']
    healthy = len(trace_stop) == 1 and trace_stop[0].get('healthy') is True and trace_stop[0].get('detach_status') == 0
    game_pid = starts[0].get('game_pid') if len(starts) == 1 else None
    guards = selected(host, 'guard_started')
    accounts = selected(host, 'player_account')
    result = backend.get('result', {}) if backend else {}
    gates = {
        'actual_current_user_join': len(guards) == 1 and guards[0].get('mode') == '--join' and len(accounts) == 1 and
            accounts[0].get('user') == 'SporeMP-M04-03' and accounts[0].get('profile_redirection') is False,
        'one_original_game': len(starts) == 1 and bool(game_pid) and all(event.get('pid') == game_pid for event in actors),
        'pinned_native_trace': len(trace_start) == 1 and trace_start[0].get('executable_sha256') == PINNED_EXE and
            trace_start[0].get('sdk_commit') == PINNED_SDK and trace_start[0].get('evidence_class') == 'NATIVE_PROBE',
        'baseline_applied_before_connected': bool(connected) and bool(applied) and
            min(event['sequence'] for event in applied) < min(event['sequence'] for event in connected) and
            all(event.get('player_id') == expected_player and event.get('baseline_sequence', 0) > 0 for event in connected),
        'one_owned_native_avatar': len(identities) == 1 and identities[0][2] == expected_player and len(native_ids) == 1 and
            all(event.get('is_native_avatar') is False for event in samples if event.get('owner') != expected_player),
        'no_material_network_error': not errors,
        'healthy_closed_native_lifecycle': healthy and len(stops) == 1 and stops[0].get('exit_code') == 0 and
            [event['event'] for event in bridge if event.get('event') in ('initialize', 'dispose')] == ['initialize', 'dispose'],
        'backend_confirms_completed_join': bool(backend) and backend.get('operation') == 'native_join' and backend.get('exit_code') == 0 and
            result.get('authenticated') is True and result.get('connected_baseline_observed') is True and result.get('clean_lifecycle') is True and
            result.get('native_host_exit') == 0 and result.get('launched_processes') == 1,
    }
    return {'game_pid': game_pid, 'host_pid': guards[0].get('host_pid') if len(guards) == 1 else None,
            'gate_results': {key: 'PASS' if value else 'FAIL' for key, value in gates.items()},
            'controlled_identities': identities, 'local_native_avatar_ids': native_ids,
            'connected_baselines': sorted({event['baseline_sequence'] for event in connected}),
            'native_replica_samples': len(samples), 'native_keyboard_intentions': selected(actors, 'network_intention'),
            'material_network_errors': errors, 'source_tick_range': [min((event['source_tick'] for event in samples), default=None),
                max((event['source_tick'] for event in samples), default=None)],
            'cross_process_timing': 'NOT_RUN: trace QPC origins are process-relative'}


def self_test():
    for data in ('{"a":1,"a":2}', '{"x":NaN}'):
        try:
            strict_json(data)
        except ValueError:
            continue
        raise AssertionError('Strict parsing failed')
    for data in (b'sporemp://join?host=private', b'{"credential":"private"}', b'token=private'):
        try:
            no_secret_content(b' '+data)
        except ValueError:
            continue
        raise AssertionError('Secret text was accepted')
    no_secret_content(b'{"event":"network_config_validated","role":"player"}')
    assert all(value == 'FAIL' for key, value in analyze_run([], [], [], None, 2)['gate_results'].items() if key != 'no_material_network_error')
    print(json.dumps({'evidence_class': 'HOST_SELF_TEST', 'checks': 7, 'native_game': 'NOT_RUN'}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path)
    parser.add_argument('--run', action='append', default=[], help='Closed Join run names, in Join then Rejoin order')
    parser.add_argument('--image', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path)
    parser.add_argument('--expected-player', type=int, choices=(1, 2), default=2)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.stage or not args.output or not args.run or len(set(args.run)) != len(args.run):
        parser.error('An explicit stage, fresh output and distinct ordered run names are required')
    stage = safe_path(args.stage)
    if stage.parent != Path('C:/ProgramData/SporeMP/M04/03/launcher-tests'):
        raise ValueError('Only the exact disposable launcher stage root is supported')
    output = safe_path(args.output)
    if output.exists():
        raise ValueError('Evidence output must be fresh')
    output.mkdir(parents=True)
    report = {'schema_version': 1, 'evidence_class': 'ACTUAL_LAUNCHER_NATIVE_ARCHIVE', 'started_utc': utc(),
              'milestone_acceptance': 'NOT_VERIFIED', 'native_game_started': False, 'desktop_input': False,
              'stage': str(stage), 'runs': [], 'images': [], 'files': []}
    for name in args.run:
        if not re.fullmatch(r'join-[a-f0-9]{16}', name):
            raise ValueError('Unexpected launcher run name')
        source = safe_path(stage / 'local/launcher/native-runs' / name)
        destination = output / name
        host, metadata = archive(source / 'native-host.jsonl', destination / 'native-host.jsonl')
        report['files'].append(metadata)
        starts = [row for row in host if row.get('event') == 'created_suspended']
        if len(starts) != 1 or type(starts[0].get('game_pid')) is not int:
            raise ValueError('One actual game PID is required before tracing files')
        pid = starts[0]['game_pid']
        rows = {}
        for filename in (f'actors-{pid}.jsonl', f'bridge-{pid}.jsonl', 'network-status.json'):
            rows[filename], metadata = archive(source / filename, destination / filename)
            report['files'].append(metadata)
        backend = None
        for candidate in sorted((stage / 'local/launcher/runs').glob('*/result.json')):
            value = strict_json(read_bounded(candidate))
            if value.get('operation') == 'native_join' and value.get('result', {}).get('evidence_directory') == str(source):
                if backend is not None:
                    raise ValueError('More than one backend report claims the same native run')
                backend, metadata = archive(candidate, destination / 'backend-result.json')
                report['files'].append(metadata)
        result = analyze_run(host, rows[f'actors-{pid}.jsonl'], rows[f'bridge-{pid}.jsonl'], backend, args.expected_player)
        report['runs'].append({'name': name, **result})
    sessions = safe_path(stage / 'local/launcher/sessions')
    # Names and counts only. Never read, hash, or copy any session config.
    remaining = list(sessions.iterdir()) if sessions.exists() else []
    report['session_config_cleanup'] = {'status': 'PASS' if not remaining else 'FAIL', 'remaining_entry_count': len(remaining), 'checked_utc': utc()}
    pairs = list(zip(report['runs'], report['runs'][1:]))
    report['rejoin_identity_and_fresh_baseline'] = 'PASS' if pairs and all(
        a['controlled_identities'] and a['controlled_identities'] == b['controlled_identities'] and
        a['game_pid'] != b['game_pid'] and a['connected_baselines'] and b['connected_baselines'] and
        max(a['connected_baselines']) < min(b['connected_baselines']) for a, b in pairs) else 'NOT_RUN' if not pairs else 'FAIL'
    for path in args.image:
        path = safe_path(path)
        if path.suffix.lower() not in ('.png', '.jpg', '.jpeg') or REPO / 'local' not in path.parents:
            raise ValueError('Only explicitly named PNG/JPEG evidence under ignored local/ is supported')
        data = read_bounded(path)
        # The desktop capture API can return JPEG bytes even when the supplied
        # evidence filename ends in .png. Record the actual format explicitly.
        actual_format = 'png' if data.startswith(b'\x89PNG\r\n\x1a\n') else 'jpeg' if data.startswith(b'\xff\xd8\xff') else None
        if not actual_format:
            raise ValueError('Unsupported image evidence header')
        report['images'].append({'path': str(path), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                                 'actual_format': actual_format, 'visual_acceptance': 'MANUAL_REVIEW_REQUIRED'})
    report['finished_utc'] = utc()
    report['numeric_failures'] = [f"{run['name']}:{key}" for run in report['runs'] for key, value in run['gate_results'].items() if value == 'FAIL']
    if report['session_config_cleanup']['status'] == 'FAIL':
        report['numeric_failures'].append('session_config_cleanup')
    if report['rejoin_identity_and_fresh_baseline'] == 'FAIL':
        report['numeric_failures'].append('rejoin_identity_and_fresh_baseline')
    (output / 'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'report': str(output / 'report.json'), 'numeric_failures': report['numeric_failures'], 'milestone_acceptance': 'NOT_VERIFIED'}))
    return 1 if report['numeric_failures'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
