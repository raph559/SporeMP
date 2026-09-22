"""Bounded unattended M03-native jumps through M04 IPC, once a fixture is loaded.

Does not launch a game, send desktop input, manufacture a scene, or retry failed
native actions. A report never grants whole-milestone acceptance.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools/launcher'))
import worker_manager
import launcher_service

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('worker_id', choices=('01', '02'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--require-existing-actors', action='store_true', help='Refuse setup; use after checkpoint adoption')
    parser.add_argument('--bridge-version', choices=('0.0.13', '0.0.14'), default='0.0.14')
    args = parser.parse_args()
    output = launcher_service.diag.no_reparse(args.output)
    if output.exists(): raise ValueError('Evidence already exists')
    value = worker_manager.current(args.worker_id)
    status = worker_manager.read_json(Path(value['run']) / 'worker-status.json')
    trace = Path(value['run']) / f"actors-{status['game_pid']}.jsonl"
    report = {'schema_version': 1, 'evidence_class': 'NATIVE_IPC_ACTION_TEST', 'native_actions_observed': False,
              'milestone_acceptance': 'NOT_VERIFIED', 'worker': value, 'commands': [], 'jumps': []}
    def command(op, epoch=0, *parameters, expected='accepted'):
        argv = [str(REPO / 'build/win32/Release/SporeMP.WorkerControl.exe'), value['generation'], str(value['supervisor_pid']), op]
        if op != 'status': argv += [str(epoch), *(str(p) for p in parameters)]
        result = subprocess.run(argv, capture_output=True, text=True, timeout=13, creationflags=subprocess.CREATE_NO_WINDOW)
        report['commands'].append({'argv': argv, 'exit_code': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr})
        if result.returncode not in (0, 5): raise ValueError('IPC request failed; no automatic retry')
        response = json.loads(result.stdout)
        if response.get('op') != op or response.get('result') != expected: raise ValueError(f'{op}: expected {expected}, got {response}')
        return response
    def rows():
        if trace.stat().st_size > 66 * 1024 * 1024: raise ValueError('Unexpected trace size')
        # Ignore only an unfinished last write, never a malformed complete record.
        data = trace.read_bytes(); complete = data[:data.rfind(b'\n') + 1]
        result = [json.loads(line) for line in complete.splitlines()]
        if not result or result[0].get('event') != 'trace_start': raise ValueError('Missing native trace provenance')
        if result[0].get('executable_sha256') != 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37': raise ValueError('Unknown native executable')
        if result[0].get('sdk_commit') != 'cbf9206b9a823f0911cd9be0217104a49d72380b' or result[0].get('bridge_version') != args.bridge_version:
            raise ValueError('Unqualified SDK or bridge version for this action probe')
        for index, row in enumerate(result, 1):
            if row.get('sequence') != index or row.get('foreign_callbacks') != 0 or row.get('pid') != status['game_pid']:
                raise ValueError('Trace sequence, callback thread, or process mismatch')
        return result
    try:
        live = command('status')
        if live['phase'] != 3: raise ValueError('The original Creature fixture is not loaded. Unattended native load is not qualified yet.')
        epoch = live['epoch']
        if not live['actor_b']:
            if args.require_existing_actors:
                raise ValueError('Existing checkpoint actors required; setup was not called')
            command('setup', epoch)
            end = time.monotonic() + 5
            while time.monotonic() < end:
                live = command('status')
                if live['actor_a'] and live['actor_b']: break
                time.sleep(0.1)
        if not live['actor_a'] or not live['actor_b'] or live['epoch'] != epoch:
            raise ValueError('Native actor setup did not complete in the same scene')
        command('jump', epoch, 1, live['actor_b'], expected='invalid')
        command('jump', epoch - 1, 2, live['actor_b'], expected='stale')
        for owner, actor in ((1, live['actor_a']), (2, live['actor_b'])):
            sent = command('jump', epoch, owner, actor)
            end = time.monotonic() + 12
            while time.monotonic() < end:
                records = rows()
                queued = [r for r in records if r['event'] == 'worker_command_queued' and r.get('request') == sent['request']]
                if len(queued) == 1:
                    native_command = queued[0]['command']
                    returned = [r for r in records if r['event'] == 'native_jump_return' and r['executing_command'] == native_command and r['actor'] == actor]
                    landed = [r for r in records if r['event'] == 'native_landing' and r['jump_command'] == native_command and r['actor'] == actor and r['epoch'] == epoch]
                    if returned and not returned[0]['accepted']: raise ValueError('Original DoJump rejected the request; it was not retried')
                    if len(returned) == len(landed) == 1 and landed[0]['sequence'] > returned[0]['sequence']:
                        report['jumps'].append({'owner': owner, 'actor': actor, 'request': sent['request'], 'command': native_command,
                                                'native_return_sequence': returned[0]['sequence'], 'landing_sequence': landed[0]['sequence']})
                        break
                time.sleep(0.1)
            else: raise ValueError('No matching original landing within the bounded action window')
        report['native_actions_observed'] = True
    except Exception as error:
        report['error'] = str(error)
    report['utc'] = launcher_service.diag.utc_now()
    launcher_service.diag.write_json(output, report)
    print(json.dumps({'native_actions_observed': report['native_actions_observed'], 'report': str(output), 'milestone_acceptance': 'NOT_VERIFIED'}))
    return 0 if report['native_actions_observed'] else 1

if __name__ == '__main__': raise SystemExit(main())
