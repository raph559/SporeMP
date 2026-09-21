"""Analyze closed M05 ability archives without controlling a game.

The input uses the historical native-*/archive.json layout. Output remains
observational; native, visual and milestone acceptance require separate review.
"""
import argparse
import importlib.util
import json
from pathlib import Path

previous = Path(__file__).with_name('analyze-m05-presentation.py')
spec = importlib.util.spec_from_file_location('m05_presentation', previous)
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, required=True)
    parser.add_argument('--media-dir', type=Path, help='Optional private capture directory')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.input_dir.resolve()
    if not root.is_dir():
        parser.error('The input archive directory does not exist')
    report = {'schema_version':1, 'evidence_class':'ANALYSIS_OF_RETAINED_NATIVE_ARTIFACTS',
              'source_analyzer':shared.identity(previous), 'runs':{}, 'media':[],
              'full_m05_acceptance':'NOT_INFERRED_BY_ANALYZER'}
    for directory in sorted(root.glob('native-*')):
        if not (directory/'archive.json').is_file():
            continue
        value = shared.trace(directory)
        records = shared.rows(next(directory.glob('actors-*.jsonl')))
        arms = [r for r in records if r['event']=='replica_request' and r.get('operation')==1 and r.get('decision')=='accepted']
        value['attack_timer_probes'] = [r for r in records if r['event']=='replica_attack_timer_probe']
        value['requests'] = [r for r in records if r['event']=='replica_request']
        value['native_path_denials'] = [r for r in records if r['event']=='replica_path_denied']
        value['ability_before_arm'] = None
        if arms:
            arm = arms[0]
            abilities = [r for r in records if r['event']=='native_ability_enter' and r.get('actor')==2
                         and r['qpc']<arm['qpc'] and r.get('executing_command')]
            if abilities:
                ability = abilities[-1]
                last_qpc = ability['qpc']+3*records[0]['qpc_frequency']
                value['ability_before_arm'] = {
                    'entry':ability, 'arm':arm,
                    'seconds_until_arm':(arm['qpc']-ability['qpc'])/records[0]['qpc_frequency'],
                    'nearby_actor_events':[r for r in records if ability['qpc']<=r['qpc']<=last_qpc
                                          and r.get('actor')==2 and r['event']!='actor_state'],
                    'nearby_states':[r for r in records if ability['qpc']<=r['qpc']<=last_qpc
                                     and r.get('actor')==2 and r['event']=='actor_state'],
                    'active_after_arm':[r for r in records if arm['qpc']<r['qpc']<=last_qpc
                                        and r.get('actor')==2 and r['event']=='sampled_attack_context'
                                        and r.get('current_attack_index')==ability['index']],
                    'first_idle_context_after_arm':next((r for r in records if r['qpc']>arm['qpc']
                        and r.get('actor')==2 and r['event']=='ability_return_context'
                        and r.get('current_attack_index')==4294967295),None),
                    'limit':'Entry before arming alone does not establish that an animation was still active at arming.'}
            value['post_arm_native_effect_events'] = [r for r in records if r['qpc']>arm['qpc'] and
                r['event'] in ('native_damage_enter','native_damage_return','native_strike_enter','native_strike_return',
                               'native_award_enter','native_award_return','native_ability_enter','native_ability_return')]
        value['personal_after'] = shared.read(root/f'personal-after-{directory.name.removeprefix("native-")}.json')
        report['runs'][directory.name] = value

    for path in sorted(args.media_dir.rglob('*')) if args.media_dir else []:
        if path.is_file() and path.suffix in ('.mkv','.png','.json'):
            report['media'].append(shared.identity(path))

    if not report['runs']:
        raise ValueError('No native-*/archive.json runs found in the supplied archive')
    destination = args.output.absolute()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('x', encoding='utf-8') as out:
        json.dump(report,out,indent=2,allow_nan=False)
    print(json.dumps({'report':str(destination),'runs':{name:{
        'build':v['build'],'exit':v['game_exit']['exit_code'],'records':v['records'],
        'trace_ok':v['sequences_contiguous'] and v['qpc_monotonic'] and not v['foreign_callbacks_max'] and bool(v['stop']),
        'challenges':len(v['challenges']),'challenges_passed':all(r['passed'] for r in v['challenges']),
        'social_readouts':len(v['social_readouts']),'post_application':v['post_application_samples'],
        'ability_arm_delay':v['ability_before_arm']['seconds_until_arm'] if v['ability_before_arm'] else None,
        'sealed_games_unchanged':v['sealed_games_unchanged']}
        for name,v in report['runs'].items()}}))


if __name__ == "__main__":
    main()
