"""Recompute the closed native observations; this is analysis, not native execution."""
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def rows(path):
    return [json.loads(line) for line in path.read_text(encoding='utf-8-sig').splitlines()]


def identity(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return {'path': str(path.resolve()), 'bytes': path.stat().st_size,
            'sha256': digest.hexdigest()}


def percentile(values, fraction):
    at = (len(values) - 1) * fraction
    lo = int(at)
    hi = min(lo + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (at - lo)


def trace(directory):
    archive = read(directory / 'archive.json')
    paths = list(directory.glob('actors-*.jsonl'))
    if len(paths) != 1:
        raise ValueError('Exactly one native actor trace required')
    path = paths[0]
    records = rows(path)
    counters = Counter(row['event'] for row in records)
    projections = [r for r in records if r['event'] == 'replica_projection_audit']
    challenges = [r for r in records if r['event'] == 'replica_challenge_result']
    states = [r for r in records if r['event'] == 'actor_state' and r.get('avatar')]
    applications = [r for r in records if r['event'] == 'replica_vitals_applied']
    late_states = []
    late_dna = []
    projected = None
    for record in records:
        if record['event'] == 'replica_vitals_applied':
            projected = record
        if projected is None or record['epoch'] != projected['epoch']:
            continue
        if record['event'] == 'actor_state' and record.get('actor') == projected['actor']:
            late_states.append((record, all(record[k] == projected[k] for k in ('health', 'energy', 'hunger'))))
        if record['event'] == 'player_context' and record.get('avatar') == projected['actor']:
            late_dna.append(record['global_dna'] == projected['dna'])
    dead = [r for r in states if r['dead']]
    after_dead = [r for r in states if dead and r['qpc'] > dead[0]['qpc'] and not r['dead']]
    last = lambda name: next((r for r in reversed(records) if r['event'] == name), None)
    paths = {r['path']: r for r in records if r['event'] == 'replica_path_audit'}
    domains = {r['domain']: r for r in records if r['event'] == 'replica_mutation_audit'}
    sample = lambda r: {k: r[k] for k in ('sequence', 'qpc', 'epoch', 'actor', 'native_id',
                                       'health', 'hunger', 'energy', 'dead', 'position')}
    return {'artifact': identity(path), 'game_exit': archive['game_exit'],
            'sealed_games_unchanged': archive['sealed_games_unchanged'],
            'build': records[0].get('bridge_version'), 'records': len(records),
            'sequences_contiguous': all(r['sequence'] == i + 1 for i, r in enumerate(records)),
            'qpc_monotonic': all(a['qpc'] <= b['qpc'] for a, b in zip(records, records[1:])),
            'engine_threads': sorted({r['thread_id'] for r in records}),
            'foreign_callbacks_max': max(r.get('foreign_callbacks', 0) for r in records),
            'event_counts': dict(counters), 'stop': last('trace_stop'),
            'final_audit': last('replica_audit'), 'final_paths': paths, 'final_domains': domains,
            'challenges': challenges, 'charm_probes': [r for r in records if r['event'] == 'replica_charm_probe'],
            'applications': applications,
            'post_application_samples': {'avatar_count': len(late_states), 'dna_count': len(late_dna),
                                         'all_vitals_match': bool(late_states) and all(pair[1] for pair in late_states),
                                         'all_dna_match': bool(late_dna) and all(late_dna),
                                         'avatar_seconds_after_first_apply': (late_states[-1][0]['qpc']-applications[0]['qpc'])/records[0]['qpc_frequency'] if late_states else None},
            'selection_views': [r for r in records if r['event'] == 'replica_selection_view'],
            'social_readouts': [r for r in records if r['event'] == 'replica_social_readout'],
            'projections': {'count': len(projections), 'all_match': bool(projections) and all(r['matches'] for r in projections)},
            'avatar_states': {'count': len(states), 'first': sample(states[0]) if states else None,
                              'last': sample(states[-1]) if states else None,
                              'first_dead': sample(dead[0]) if dead else None,
                              'first_alive_after_death': sample(after_dead[0]) if after_dead else None},
            'jump_and_land_events': [r for r in records if 'jump' in r['event'] or r['event'] == 'native_landing'],
            'scene_events': [r for r in records if r['event'] in ('scene_exit', 'scene_enter', 'replica_disconnected')],
            'limits': 'Selected trace fields/counters only. Blocked calls are not unique actions. Native capture/visual/audio acceptance is separate.'}


def timing(directory):
    request = read(directory / 'frames-request.json')
    with (directory / 'frames.csv').open(newline='', encoding='utf-8-sig') as stream:
        frames = list(csv.DictReader(stream))
    valid = [r for r in frames if r['Application'] == 'SporeApp.exe' and int(r['ProcessID']) == request['game_pid']
             and r['Runtime'] == 'D3D9' and float(r['msBetweenPresents']) > 0]
    values = sorted(float(r['msBetweenPresents']) for r in valid)
    if not values or len(valid) != len(frames):
        raise ValueError('Missing, foreign or invalid native D3D9 frame rows')
    return {'artifact': identity(directory / 'frames.csv'), 'request': request, 'samples': len(values),
            'median_ms': statistics.median(values), 'p95_ms': percentile(values, .95),
            'p99_ms': percentile(values, .99), 'max_ms': max(values),
            'over_33_333_ms': sum(v > 33.333 for v in values),
            'over_50_ms': sum(v > 50 for v in values),
            'mean_effective_cpu_cores': request['process_cpu_seconds'] / request['wall_seconds'],
            'limit': 'Same machine/build/save and recorders; different camera/actions, uncontrolled native NPC timing. This is descriptive timing, not matched replay or an overhead bound.'}


def media(base):
    if base is None:
        return []
    results = []
    for path in sorted(base.iterdir()):
        if path.is_file() and path.suffix in ('.wav', '.png'):
            results.append(identity(path))
        if path.is_dir() and path.name.startswith('capture-'):
            item = {'name': path.name, 'request': read(path / 'request.json'),
                    'completion': read(path / 'completion.json')}
            movie = path / 'capture.mkv'
            if movie.exists():
                probe = subprocess.run(['ffprobe', '-v', 'error', '-show_entries',
                                        'stream=index,codec_name,width,height,duration:format=duration,size',
                                        '-of', 'json', str(movie)], capture_output=True, text=True)
                item.update(artifact=identity(movie), ffprobe_argv=probe.args,
                            ffprobe_exit=probe.returncode, ffprobe_stderr=probe.stderr,
                            probe=json.loads(probe.stdout) if probe.returncode == 0 else None)
            results.append(item)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, required=True)
    parser.add_argument('--media-dir', type=Path, help='Optional private capture directory; uses ffprobe for videos')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.input_dir.resolve()
    if not root.is_dir():
        parser.error('The input archive directory does not exist')
    audio_results = {}
    for log in sorted(root.glob('*audio*.log')):
        candidates = [json.loads(line) for line in log.read_text(encoding='utf-8-sig').splitlines() if line.startswith('{')]
        captures = [r for r in candidates if r.get('capture_mode') == 'include_target_process_tree']
        if captures:
            audio_results[log.name] = captures

    report = {'schema_version': 1, 'evidence_class': 'ANALYSIS_OF_RETAINED_NATIVE_ARTIFACTS',
              'runs': {p.name: trace(p) for p in sorted(root.glob('native-*')) if p.is_dir() and (p/'archive.json').exists()},
              'timing': {p.name: timing(p) for p in sorted(root.glob('timing-*')) if p.is_dir()},
              'audio_capture_results': audio_results,
              'media': media(args.media_dir), 'full_m05_acceptance': 'NOT_VERIFIED'}
    if not report['runs']:
        raise ValueError('No native-*/archive.json runs found in the supplied archive')
    destination = args.output.absolute()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2)
    print(json.dumps({'report': str(destination), 'runs': len(report['runs']),
                      'timing': {k: {x: v[x] for x in ('samples', 'median_ms', 'p99_ms', 'max_ms', 'over_33_333_ms')}
                                 for k, v in report['timing'].items()},
                      'native_exits': {k: v['game_exit']['exit_code'] for k,v in report['runs'].items()}}))


if __name__ == "__main__":
    main()
