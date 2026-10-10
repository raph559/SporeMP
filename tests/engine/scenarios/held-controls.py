"""C01, first part: each player's own held movement keys and jump key.

From the checkpoint sealed by trial146. Every key is an ordinary key message
posted to that player's own game window (INPUT_IN_PROCESS): held W, then A, S
and D, each released, then Space. The authority must move that player's
creature while the key is held and bring it to rest after release, and the
other player's creature must not be moved by it. Neither creature may move
before its owner's first command, nor while both players are idle at the end.
Foreign B has no jump ability in this fixture, so only the arrival of its
jump request is required.
"""
import math

ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 240
FORWARD_SECONDS, SIDE_SECONDS = 4, 2
# Diagnostic steering covered about 8 units in 2 seconds (trial137).
FORWARD_UNITS, SIDE_UNITS = 3.0, 1.0
IDLE_SECONDS = 10
# Trial163: the original AI walked B 7.4 units before any input. A stop takes effect within a frame or two.
UNOWNED_UNITS = 0.25


def drive(trial):
    trial.wait_ready(stable_seconds=3)
    authority = trial.traces['authority']

    def owned(player):
        found = [r for r in authority.entities.values() if r['epoch'] == trial.scene and r.get('owner') == player]
        return found[0] if len(found) == 1 else None

    def at_rest(player):
        sample = owned(player)
        return sample if sample and sample['velocity'] == [0, 0, 0] else None

    for player in (1, 2):
        role, other = f'player-{player}', 3 - player
        trial.wait(lambda: at_rest(player) and at_rest(other), 30, f'both creatures at rest before player {player} moves')
        for key, seconds, minimum in (('W', FORWARD_SECONDS, FORWARD_UNITS), ('A', SIDE_SECONDS, SIDE_UNITS),
                                      ('S', SIDE_SECONDS, SIDE_UNITS), ('D', SIDE_SECONDS, SIDE_UNITS)):
            start, bystander = owned(player), owned(other)
            mark = trial.traces[role].sequence
            trial.hold(role, key, seconds)
            moved = math.dist(owned(player)['position'], start['position'])
            trial.expect(moved >= minimum, f'player {player} held {key}: its creature moved', units=round(moved, 3), seconds=seconds)
            down = trial.until(role, 'network_key', where=lambda r: r['key'] == ord(key) and r['down'], after=mark, timeout=5)
            up = trial.until(role, 'network_key', where=lambda r: r['key'] == ord(key) and not r['down'], after=down['sequence'], timeout=5)
            held = (up['qpc'] - down['qpc']) / up['qpc_frequency']
            trial.expect(held >= seconds - 0.5, f'player {player} {key} stayed down for the whole hold', held_seconds=round(held, 2))
            trial.until(role, 'network_keyboard_release', after=down['sequence'], timeout=5)
            rest = trial.wait(lambda: at_rest(player), 8, f'player {player} comes to rest after releasing {key}')
            trial.settle(1)
            drift = math.dist(owned(player)['position'], rest['position'])
            trial.expect(drift < 0.001, f'player {player} stays where it stopped after {key}', drift=round(drift, 6))
            other_moved = math.dist(owned(other)['position'], bystander['position'])
            trial.expect(other_moved < 0.001, f"player {other} was not moved by player {player}'s {key}", units=round(other_moved, 6))
        mark = authority.sequence
        trial.hold(role, ' ', 0.3)
        queued = trial.until('authority', 'network_actor_command_queued',
                             where=lambda r: r['owner'] == player and r['verb'] == 'jump', after=mark, timeout=10)
        trial.expect(queued['decision'] == 'accepted', f"player {player}'s jump key reaches the authority as its own jump")
        if player == 1:
            jump = trial.until('authority', 'native_jump_return', where=lambda r: r['actor'] == 1, after=mark, timeout=10)
            trial.expect(jump['accepted'], 'A jumps')
            trial.until('authority', 'native_landing', where=lambda r: r['owner'] == 1, after=mark, timeout=20)
            trial.expect(True, 'A lands')
    trial.not_run('Ability keys, mouse targeting and movement during an interaction', 'later C01 increments')
    trial.not_run("B's jump and landing", 'foreign B has no jump ability in this fixture')
    trial.not_run('Fossils the original creates while a player walks', 'a qualified client-side fossil factory (C07/C08)')
    resting = {player: trial.wait(lambda: at_rest(player), 8, f'player {player} at rest before the idle period') for player in (1, 2)}
    trial.settle(IDLE_SECONDS)
    for player in (1, 2):
        idle = math.dist(owned(player)['position'], resting[player]['position'])
        trial.expect(idle < 0.001, f'player {player} does not move while both players are idle', units=round(idle, 6), seconds=IDLE_SECONDS)


def review(closed):
    for player in (1, 2):
        role = f'player-{player}'
        posted = closed.events(role, 'network_input_posted')
        closed.expect(len(posted) == 10 and all(p['posted'] and p['input_class'] == 'INPUT_IN_PROCESS' for p in posted),
                      f'player {player}: five keys posted down and up in its own process', posted=len(posted))
        keys = closed.events(role, 'network_key')
        closed.expect([(k['key'], k['down']) for k in keys] == [(ord(c), d) for c in 'WASD ' for d in (True, False)],
                      f"player {player}: its window procedure saw each key go down then up, in order")
        steers = [r for r in closed.events('authority', 'command_begin') if r['owner'] == player and r['verb'] == 'steer']
        closed.expect(len(steers) >= 40, f"authority ran player {player}'s steering from its held keys", steers=len(steers))
    samples = closed.events('authority', 'scene_native_authority_sample')
    for player in (1, 2):
        first = next((r['sequence'] for r in closed.events('authority', 'network_actor_command_queued') if r['owner'] == player), math.inf)
        before = [r['position'] for r in samples if r['owner'] == player and r['sequence'] < first]
        strayed = max((math.dist(p, before[0]) for p in before), default=None)
        closed.expect(strayed is not None and strayed < UNOWNED_UNITS,
                      f"player {player}'s creature does not move before its owner's first command",
                      units=None if strayed is None else round(strayed, 3), samples=len(before),
                      stops=sum(r['owner'] == player for r in closed.events('authority', 'native_unowned_movement_stopped')))
    closed.expect_none('authority', 'native_unowned_movement_unstoppable')
    for role in ('authority', 'player-1', 'player-2'):
        for event in ('native_global_dna_enter', 'native_award_return', 'native_relationship_acceptance_failed'):
            closed.expect_none(role, event)
    for player, dna in ((1, 14), (2, 7)):
        views = [r for r in closed.events(f'player-{player}', 'scene_native_replica_sample') if r['owner'] == player]
        closed.expect(views and views[-1]['native_dna'] == dna and not views[-1]['dead'],
                      f'player {player} ends alive with {dna} DNA')
