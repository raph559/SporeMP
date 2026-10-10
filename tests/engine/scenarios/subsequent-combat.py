"""A's subsequent kill from its earned state, B moving meanwhile, then original Save.

Runner form of trial146 (.181): from the fed checkpoint sealed by trial143, A's
diagnostic combat intention reaches six original Bites on a different adult,
A's native DNA goes 7 -> 14 while B keeps 7 and is steered, and the original
Save completes with an unchanged 679-row player/inventory census.
A live run still needs the closed checkpoint sealed afterwards for the restore check.
"""
import collections
import json
import math

ARTIFACT = 'local/m11-resume-2026-09-27/trial143/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 150
TARGET_NATIVE_ID = 89
SPECIES = [106741522, 731352134, 1080189440]
ROLES = ('authority', 'player-1', 'player-2')
ENVELOPE = {'schema_version', 'evidence_class', 'harness', 'sequence', 'pid', 'thread_id', 'qpc', 'qpc_frequency',
            'epoch', 'executing_command', 'foreign_callbacks', 'ability_scope', 'strike_scope',
            'animal_damage_scope', 'pickup_scope', 'request', 'phase'}
CENSUS = {'native_checkpoint_player', 'native_checkpoint_unlocked_item', 'native_checkpoint_item_status'}


def census(rows, phase):
    return collections.Counter(json.dumps({k: v for k, v in r.items() if k not in ENVELOPE}, sort_keys=True)
                               for r in rows if r['event'] in CENSUS and r.get('phase') == phase and r.get('request') == 1)


def steering(position, axis=1):
    """A unit direction along `axis`, tangent to the planet at `position`, packed as the steering verb expects."""
    length = math.sqrt(sum(v * v for v in position))
    normal = [v / length for v in position]
    wanted = [int(i == axis) for i in range(3)]
    dot = sum(a * b for a, b in zip(wanted, normal))
    direction = [a - dot * b for a, b in zip(wanted, normal)]
    length = math.sqrt(sum(v * v for v in direction))
    parts = [int(math.floor(abs(v / length) * 511 + 0.5)) * (1 if v >= 0 else -1) for v in direction]
    return (parts[0] + 511) | ((parts[1] + 511) << 10) | ((parts[2] + 511) << 20)


def drive(trial):
    trial.wait_ready(stable_seconds=3)
    authority = trial.traces['authority']

    def entity(**wanted):
        found = [r for r in authority.entities.values() if r['epoch'] == trial.scene
                 and all(r.get(k) == v for k, v in wanted.items())]
        return found[0] if len(found) == 1 else None

    target = trial.wait(lambda: entity(native_id=TARGET_NATIVE_ID), 20, 'the saved third-species adult')
    actor = trial.wait(lambda: entity(owner=1), 20, "A's creature")
    peer = trial.wait(lambda: entity(owner=2), 20, "B's creature")
    trial.expect(not target['owner'] and not target['dead'] and target['source_age'] == 1
                 and target['health_context']['species'] == SPECIES, 'target is the expected ownerless adult')
    submitted = trial.traces['player-1'].sequence
    trial.control('player-1', 'network_action', trial.scene, 1, actor['remote_entity'], 9, 5,
                  target['remote_entity'], target['entity_generation'])
    accepted = trial.until('player-1', 'network_action_result', after=submitted, timeout=15)
    trial.expect(accepted['error'] == 'none', "A's combat intention is accepted")
    packed = steering(peer['position'])
    try:
        for _ in range(30):
            trial.control('player-2', 'network_action', trial.scene, 2, peer['remote_entity'], 10, packed, 0, 0)
            trial.settle(0.067)
    finally:
        trial.control('player-2', 'network_action', trial.scene, 2, peer['remote_entity'], 11, 0, 0, 0)
    reward = trial.until('authority', 'native_global_dna_return', where=lambda r: r['avatar'] == 1 and r['after'] > 7,
                         timeout=60)
    trial.expect(reward['after'] == 14, 'A reaches 14 native DNA', after=reward['after'])

    def settled():
        dead = authority.entities.get(target['remote_entity'], {}).get('dead')
        owners = [entity(owner=n) for n in (1, 2)]
        return dead and all(o and not o['dead'] and o['health'] > 0 for o in owners)
    trial.wait(settled, 30, 'target dead with both owners alive')
    trial.settle(2)
    before = authority.sequence
    trial.control('authority', 'save', trial.scene)
    saved = trial.until('authority', 'native_checkpoint_save_complete', after=before, timeout=30)
    trial.expect(saved['success'] and not saved['diagnostic_only'], 'original Save completes')
    trial.not_run('Sealing the closed checkpoint for the restore check', 'a post-closure seal step in the runner')
    trial.not_run('Physical input', 'combat and steering are diagnostic network intentions')
    trial.settle(5)


def review(closed):
    source = closed.rows('authority')

    def events(name):
        return [r for r in source if r['event'] == name]
    commands = events('command_begin')
    closed.expect(len(commands) >= 3 and commands[0]['owner'] == 1 and commands[0]['verb'] == 'combat'
                  and all(c['owner'] == 2 and c['verb'] == 'steer' for c in commands[1:-1])
                  and commands[-1]['owner'] == 2 and commands[-1]['verb'] == 'steer_stop',
                  "authority ran A's combat, then B's steering and its release", commands=len(commands))
    bites = events('native_combat_input')
    damage = [r for r in events('native_damage_enter') if r['attacker'] == 1 and r['receiver'] == 3]
    closed.expect(len(bites) == 6 and all(b['owner'] == 1 and b['ability_index'] == 5 for b in bites),
                  'six original Bites by A', bites=len(bites))
    closed.expect([d['receiver_health'] for d in damage] == [6, 5, 4, 3, 2, 1] and all(d['damage'] == 1 for d in damage),
                  'each Bite removes one health from the target')
    entered, returned = closed.one('authority', 'native_global_dna_enter'), closed.one('authority', 'native_global_dna_return')
    closed.expect(entered and returned and entered['avatar'] == returned['avatar'] == 1 and entered['before'] == 7
                  and entered['amount'] == 7 and returned['after'] == 14, 'exactly one original reward: A 7 -> 14 DNA')
    for role in ROLES:
        for event in ('native_first_dna_tutorial_deferred', 'native_tutorial_client_cycle'):
            closed.expect_none(role, event)
        restored, adopted, nouns = (closed.one(role, e) for e in (
            'native_checkpoint_extension_restored', 'native_player_adopted', 'checkpoint_restore'))
        closed.expect(restored and restored['success'] and adopted and adopted['created_players'] == 0
                      and nouns and nouns['created_nouns'] == 0, f'{role}: saved pair adopted, nothing created')
    before, after = census(source, 'before_save'), census(source, 'after_save')
    closed.expect(before == after and sum(before.values()) == 679, 'Save leaves all 679 player/inventory rows unchanged',
                  rows=sum(before.values()))
    saved, native = closed.one('authority', 'native_checkpoint_save_complete'), closed.one('authority', 'native_save_return')
    closed.expect(saved and saved['success'] and not saved['diagnostic_only'] and native and native['native_success'],
                  'original Save returned success')
    history = closed.one('authority', 'native_history_checkpoint_written')
    closed.expect(history and history['success'] and history['primary_unchanged'] and history['entries'] == 1,
                  "B's one-entry history is written and A's primary history is left unchanged")
    for player, dna in ((1, 14), (2, 7)):
        views = [r for r in closed.events(f'player-{player}', 'scene_native_replica_sample') if r['owner'] == player]
        closed.expect(views and views[-1]['native_dna'] == dna and not views[-1]['dead'],
                      f'player {player} ends alive with {dna} DNA')
