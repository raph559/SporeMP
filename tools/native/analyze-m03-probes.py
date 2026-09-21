"""Summarize preserved M03 native probes. Structural success never grants M03 acceptance."""
import collections
import hashlib
import json
import math
import argparse
from pathlib import Path

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--runs',type=int,default=5)
    parser.add_argument('--input-dir',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    options=parser.parse_args()
    if options.runs<1:
        raise ValueError('Use a positive run count')
    base = options.input_dir.resolve()
    if not base.is_dir():
        raise ValueError('The input archive directory does not exist')
    read = lambda p: json.loads(p.read_text(encoding='utf-8-sig'))
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    def slim(row):
        return {k:v for k,v in row.items() if k not in ('schema_version','evidence_class','harness','pid','thread_id','qpc_frequency','foreign_callbacks')}

    reports = []
    for number in range(1, options.runs+1):
        name = f'probe-{number:02}'
        folder = base/name
        path = next((folder/'native').glob('actors-*.jsonl'))
        rows, bad = [], []
        for line_number, line in enumerate(path.read_text().splitlines(), 1):
            try:
                row = json.loads(line, parse_constant=lambda _: (_ for _ in ()).throw(ValueError('nonfinite')))
                rows.append(row)
            except ValueError:
                bad.append(line_number)
        events = collections.defaultdict(list)
        for row in rows:
            events[row['event']].append(row)
        sequence_errors = [i for i,r in enumerate(rows,1) if r['sequence'] != i]
        jumps = []
        for command in events['command_begin']:
            if command['verb'] != 'jump':
                continue
            cmd, actor = command['command'], command['actor']
            returns = [r for r in events['native_jump_return'] if r['executing_command']==cmd and r['actor']==actor]
            landings = [r for r in events['native_landing'] if r['jump_command']==cmd and r['actor']==actor and r['owner']==command['owner'] and r['epoch']==command['epoch']]
            next_states = [r for r in events['actor_state'] if r['actor']==actor and r['qpc']>=command['qpc']]
            first_state = next_states[0] if next_states else None
            landed_state = next((r for r in next_states if landings and r['qpc']>=landings[0]['qpc']),None)
            jumps.append(dict(command=cmd, actor=actor, owner=command['owner'], epoch=command['epoch'],
                return_count=len(returns), accepted=returns[0]['accepted'] if returns else None,
                landing_count=len(landings), begin_sequence=command['sequence'],
                landing_sequence=landings[0]['sequence'] if landings else None,
                latency_seconds=(landings[0]['qpc']-command['qpc'])/command['qpc_frequency'] if landings else None,
                intervening_ai_ticks=(sum(landed_state[k]-first_state[k] for k in ('npc_ticks','avatar_ticks')) if first_state and landed_state else None)))
        walks = []
        for request in events['native_walk_request']:
            samples = [r for r in events['actor_state'] if r['actor']==request['actor'] and r['epoch']==request['epoch'] and 0 <= r['qpc']-request['qpc'] <= 5*request['qpc_frequency']]
            closest = min(samples,key=lambda r:math.dist(r['position'],request['destination']),default=None)
            walks.append(dict(command=request['executing_command'],actor=request['actor'],destination=request['destination'],
                request_sequence=request['sequence'], sample_count=len(samples),
                closest_sequence=closest['sequence'] if closest else None,
                closest_distance=math.dist(closest['position'],request['destination']) if closest else None))
        damage = []
        groups = collections.defaultdict(list)
        for row in events['native_damage_enter']:
            groups[(row['attacker'],row['receiver'],row['attacker_owner'])].append(row)
        for (attacker,receiver,owner),group in groups.items():
            damage.append(dict(attacker=attacker,receiver=receiver,owner=owner,count=len(group),
                total_requested_damage=sum(r['damage'] for r in group),
                receiver_health_before_first=group[0]['receiver_health'],receiver_health_before_last=group[-1]['receiver_health'],
                first_sequence=group[0]['sequence'],last_sequence=group[-1]['sequence']))
        pairs = {}
        for family in ('native_damage','native_ability','native_target'):
            entered=collections.Counter(r['call'] for r in events[family+'_enter'])
            returned=collections.Counter(r['call'] for r in events[family+'_return'])
            pairs[family]=dict(entries=sum(entered.values()),returns=sum(returned.values()),
                exactly_paired=entered==returned and all(v==1 for v in entered.values()))
        resources=[]
        for command in events['command_begin']:
            if command['verb'] not in ('attack','engage'):continue
            samples=collections.defaultdict(list)
            for r in events['actor_state']:
                if r['executing_command']==command['command']:samples[r['actor']].append(r)
            resources.append(dict(command=command['command'],actor=command['actor'],by_actor=[
                dict(actor=actor,before_sequence=g[0]['sequence'],after_sequence=g[-1]['sequence'],
                     before={k:g[0][k] for k in ('energy','inventory_count','active_bits','recharge_bits')},
                     after={k:g[-1][k] for k in ('energy','inventory_count','active_bits','recharge_bits')}) for actor,g in samples.items()]))
        host=[json.loads(l) for l in (folder/'native/native-host.jsonl').read_text().splitlines()]
        provenance=read(base/f'provenance-{number:02}.json')
        payload_names={'sporemp.dll','sporemp.native.dll','sporemodapi.dll','sporemodapi.dll.core','sporeapp.exe'}
        payloads=[r for r in host if r['event']=='module' and (Path(r['path']).name.lower() in payload_names or 'sporemp' in Path(r['path']).name.lower())]
        known_hashes={h['Hash'].lower() for h in provenance['hashes']}
        reports.append(dict(run=name,trace=str(path.relative_to(base)),trace_sha256=sha(path),
            provenance=f'provenance-{number:02}.json',provenance_sha256=sha(base/f'provenance-{number:02}.json'),
            record_count=len(rows),malformed_lines=bad,sequence_errors=sequence_errors,
            event_counts={k:len(v) for k,v in events.items()},
            thread_ids=sorted({r['thread_id'] for r in rows}),foreign_callbacks=max((r['foreign_callbacks'] for r in rows),default=0),
            trace_stop=[slim(r) for r in events['trace_stop']],
            completion=read(folder/'completion.json'),personal_after=read(folder/'native/personal-after.json'),
            game_exit=[r for r in host if r['event']=='game_exited'],
            loaded_payloads=[dict(path=r['path'],sha256=r['sha256'],matches_provenance=r['sha256'] in known_hashes) for r in payloads],
            bindings=[slim(r) for r in events['bound']],
            lifecycle=[slim(r) for r in rows if r['event'] in ('invalidated','scene_exit','binding_missing')],
            commands=[slim(r) for r in events['command_queued']], jumps=jumps,walks=walks,damage=damage,call_pairs=pairs,
            global_dna=[slim(r) for r in rows if r['event'] in ('native_global_dna_enter','native_global_dna_return')],
            native_energy_amounts=sorted({r['amount'] for r in events['native_energy']}),command_resources=resources,
            campaign_context=[slim(r) for r in events['campaign_context']],
            intentions=[slim(r) for r in rows if r['event'].startswith('intention_')]))

    def files(snapshot):
        return {(s['source'],f['path']): f for s in snapshot['manifest']['sources'] for f in s['snapshot']['files']}
    start=files(read(base/'fixture-start.json'))
    fixture_diffs=[]
    for number in range(1,options.runs+1):
        end=files(read(base/f'fixture-probe-{number:02}-end.json'))
        changed=[list(k) for k in sorted(start.keys() | end.keys()) if start.get(k)!=end.get(k)]
        protected=[k for k in changed if k[1].startswith('Games/') or 'My Spore Creations' in k[0]]
        fixture_diffs.append(dict(run=f'probe-{number:02}',changed=changed,protected_game_or_creation_changes=protected))

    result=dict(schema_version=1,evidence_class='ANALYSIS_OF_NATIVE_PROBES',
        milestone_acceptance='NOT_GRANTED',
        scope='Observed Creature harness only; failed and incomplete probes remain unqualified. Probe 08 records a completed native B kill; this analysis does not grant independent player context, B reward progression or full M03 acceptance.',
        runs=reports,fixture_comparison=fixture_diffs)
    target=options.output.absolute()
    target.parent.mkdir(parents=True,exist_ok=True)
    with target.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2);stream.write('\n')
    print(json.dumps([dict(run=r['run'],records=r['record_count'],exit=r['completion']['observed_exit'],
        jumps=r['jumps'],walks=r['walks'],damage=r['damage'],dna=r['global_dna']) for r in reports],indent=2))


if __name__ == "__main__":
    main()
