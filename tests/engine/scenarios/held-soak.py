"""C01, soak: both players hold movement keys at the same time, leg after leg, for two minutes.

From the checkpoint sealed by trial146. Every key is an ordinary key message posted to that player's own
game window (INPUT_IN_PROCESS). In each leg both players press the same key, or the same two keys, keep
them down for LEG_SECONDS and release them. For the whole of a leg the authority must keep receiving that
player's steering without a gap and accept all of it; after the release the creature must come to rest and
stay there. Legs come in opposite pairs, so the creatures stay in the part of the world the fixture was
sealed in. Four legs hold two keys at once. Movement is relative to the player's camera: second by second,
the creature must go the way that player's own game asked for, and that must be the way its held keys point
in its own view. In the first leg player 1 also presses Space with W still down: its creature must jump,
land and go on walking. Neither creature may move while both players are idle at the end.

The world can stop a creature whose key is still down: in trial267 player 1's walked into a tree after ten
seconds of S and stood against it while its steering went on arriving. A creature must therefore move for
part of each leg, and for most of the time its keys are held.

Up to bridge 0.0.224 this scenario failed four runs in five in the distinct-species fixture: on player 2's
client the original camera turned toward a native walk its avatar still held, and steering relative to that
camera walked the creature in a curve or a circle (trials 270 to 294; camera-probe.py is the direct check).
`--variant same-species` runs the same legs in the one-species fixture. How far each camera turns in each
leg is recorded.
"""
import math

ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 300
# The fixture sealed by trial118, where both players are of one species and each client plays the avatar
# it loaded; in the fixture above player 2's client has had its avatar switched to its own creature.
# There player 1's creature was sealed starving: it loses health from the start and dies about 150 seconds after the
# scene opens (trials 293 and 302), so the legs are shorter there.
VARIANTS = {'same-species': {'ARTIFACT': 'local/m11-resume-2026-09-27/trial118/sealed-checkpoint/checkpoint.smp',
                             'AUTHORITY': {'schema': 2}, 'LEG_SECONDS': 9}}
LEGS = ('W', 'S', 'A', 'D', 'WD', 'SA', 'WA', 'SD')
LEG_SECONDS = 15
# A creature that covers less than this in a second is not moving (trial267: player 1 walks 4.5 units a
# second and player 2 2.5).
MOVING = 0.5
# The world can stop a creature whose key is still down, and it then stays stopped to the end of the leg (trials 267
# and 301: a tree behind player 1's starting place). Apart from such an end of a leg, a creature may stand still for
# STALLED seconds of a leg, the first while it turns round; it must move for MOVED seconds of every leg and for three
# quarters of all the time its keys are held.
STALLED, MOVED, MOVED_SHARE = 3, 5, 0.75
# Steering arrived 12 times a second in trial267.
STEERS_PER_SECOND, STEERING_GAP_SECONDS = 3, 1.0
# Cosine above which a creature goes the way that was asked, and seconds of a leg in which it may not: the
# first, while it turns round, and one against an obstacle.
ALONG, ASTRAY_SECONDS = 0.7, 3
FORWARD, SIDEWAYS = {'W': 1, 'S': -1}, {'D': 1, 'A': -1}
JUMP_AT_SECOND = 5
# A creature that moves less than STILL between two samples is at rest, whatever speed the game reports: one standing
# on a slope kept a reported speed of 0.03 to 0.11 for eight seconds (trial282) and slid a little after the game
# first reported it still (trial283). It must then stay within SETTLED of where it came to rest.
STILL, SETTLED = 0.1, 0.25
IDLE_SECONDS = 8


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def unit(v):
    length = math.sqrt(dot(v, v))
    return [x / length for x in v]


def camera_axes(position, centre_ray, right_ray):
    """The camera's forward and right directions along the ground at `position` (positions are planet-centred)."""
    up = unit(position)
    forward = unit([c - u * dot(centre_ray, up) for c, u in zip(centre_ray, up)])
    right = [r - c * dot(right_ray, centre_ray) for r, c in zip(right_ray, centre_ray)]
    return forward, unit([r - u * dot(right, up) for r, u in zip(right, up)])


def drive(trial):
    trial.wait_ready(stable_seconds=3)
    authority = trial.traces['authority']
    players = (1, 2)

    def owned(player):
        found = [r for r in authority.entities.values() if r['epoch'] == trial.scene and r.get('owner') == player]
        return found[0] if len(found) == 1 else None

    def at_rest(player):
        sample = owned(player)
        return sample if sample and sample['velocity'] == [0, 0, 0] else None

    def rest(player, seconds, label):
        """Wait until three successive samples of a creature are less than STILL apart, and return the last one.

        The authority samples a creature about once a second: reading its position twice within one sample
        would call a walking creature at rest (trial288).
        """
        last, still = owned(player), 0
        for _ in range(seconds * 4):
            trial.settle(0.25)
            now = owned(player)
            if now['sequence'] == last['sequence']:
                continue
            # Twice in a row: a creature stopped against a tree moved 0.7 units once its key was released (trial291).
            still = still + 1 if math.dist(now['position'], last['position']) < STILL else 0
            if still == 2:
                return now
            last = now
        trial.expect(False, label)

    def queued(player, after, verb):
        return [r for r in authority.retained if r['sequence'] > after and r['event'] == 'network_actor_command_queued'
                and r['owner'] == player and r['verb'] == verb]

    def asked(player, after):
        """The latest steering that player's own game derived from its camera and held keys."""
        for row in reversed(trial.traces[f'player-{player}'].retained):
            if row['sequence'] <= after:
                return None
            if row['event'] == 'native_camera_input':
                return row

    trial.wait(lambda: at_rest(1) and at_rest(2), 30, 'both creatures at rest before the first leg')
    stalled_in_all = {p: 0 for p in players}
    for keys in LEGS:
        path, moves, mark, jumped = {p: 0.0 for p in players}, {p: [] for p in players}, authority.sequence, None
        astray, views = {p: 0 for p in players}, {p: [] for p in players}
        client_mark = {p: trial.traces[f'player-{p}'].sequence for p in players}
        try:
            for key in keys:
                for player in players:
                    trial.key(f'player-{player}', key, True)
            for second in range(LEG_SECONDS):
                before = {p: owned(p)['position'] for p in players}
                if keys == LEGS[0] and second == JUMP_AT_SECOND:
                    jumped = authority.sequence
                    trial.hold('player-1', ' ', 0.3)
                trial.settle(1)
                for player in players:
                    covered = math.dist(owned(player)['position'], before[player])
                    path[player] += covered
                    moves[player].append(covered >= MOVING)
                    view = asked(player, client_mark[player])
                    if view:
                        views[player].append(view)
                    if covered >= MOVING:
                        went = unit([e - s for e, s in zip(owned(player)['position'], before[player])])
                        astray[player] += not view or dot(went, view['direction']) < ALONG
            released = authority.sequence
        finally:
            for key in keys:
                for player in players:
                    trial.key(f'player-{player}', key, False)
        for player in players:
            steers = [r for r in queued(player, mark, 'steer') if r['sequence'] <= released]
            refused = sorted({r['decision'] for r in steers} - {'accepted'})
            gaps = [(b['qpc'] - a['qpc']) / b['qpc_frequency'] for a, b in zip(steers, steers[1:])]
            trial.expect(len(steers) >= LEG_SECONDS * STEERS_PER_SECOND and not refused and max(gaps) <= STEERING_GAP_SECONDS,
                         f'player {player} held {keys}: the authority received its steering without a gap and accepted all of it',
                         steers=len(steers), refused=refused, longest_gap_seconds=round(max(gaps, default=0), 2), seconds=LEG_SECONDS)
            stalled = moves[player].count(False)
            blocked = next((i for i, moved in enumerate(reversed(moves[player])) if moved), LEG_SECONDS)
            stalled_in_all[player] += stalled
            trial.expect(stalled - blocked <= STALLED and LEG_SECONDS - stalled >= MOVED,
                         f'player {player} held {keys}: its creature moved until the leg ended or the world stopped it',
                         units=round(path[player], 1), seconds_not_moving=stalled, of_them_stopped_at_the_end=blocked, seconds=LEG_SECONDS)
            # What the player's own game asked for: the direction its held keys point in its own view.
            pointed = []
            for view in views[player]:
                forward, right = camera_axes(view['position'], view['center_ray'], view['right_ray'])
                wanted = unit([f * sum(FORWARD.get(k, 0) for k in keys) + r * sum(SIDEWAYS.get(k, 0) for k in keys) for f, r in zip(forward, right)])
                pointed.append(dot(wanted, view['direction']))
            turned = math.degrees(math.acos(max(-1.0, min(1.0, dot(views[player][0]['center_ray'], views[player][-1]['center_ray']))))) if views[player] else None
            trial.expect(len(pointed) >= LEG_SECONDS - 1 and min(pointed) > 0.95 and astray[player] <= ASTRAY_SECONDS,
                         f'player {player} held {keys}: its creature went the way its keys point in its own view',
                         seconds_astray=astray[player], least_cosine_of_asked_with_keys=round(min(pointed, default=0), 3),
                         camera_turned_degrees=None if turned is None else round(turned))
        if jumped is not None:
            jump = trial.until('authority', 'native_jump_return', where=lambda r: r['actor'] == 1, after=jumped, timeout=10)
            landing = trial.until('authority', 'native_landing', where=lambda r: r['owner'] == 1, after=jumped, timeout=20)
            later = [r for r in queued(1, landing['sequence'], 'steer') if r['decision'] == 'accepted']
            trial.expect(jump['accepted'] and bool(later), 'player 1 jumps with W held, lands and goes on walking',
                         steers_after_landing=len(later))
        resting = {p: rest(p, 10, f'player {p} comes to rest after releasing {keys}')['position'] for p in players}
        trial.settle(1)
        for player in players:
            drift = math.dist(owned(player)['position'], resting[player])
            trial.expect(drift < SETTLED, f'player {player} stays where it stopped after {keys}', drift=round(drift, 3))
    for player in players:
        trial.expect(stalled_in_all[player] <= (1 - MOVED_SHARE) * LEG_SECONDS * len(LEGS), f"player {player}'s creature moved for most of the time its keys were held",
                     seconds_not_moving=stalled_in_all[player], seconds=LEG_SECONDS * len(LEGS))
    resting = {p: owned(p)['position'] for p in players}
    trial.settle(IDLE_SECONDS)
    for player in players:
        idle = math.dist(owned(player)['position'], resting[player])
        trial.expect(idle < SETTLED, f'player {player} does not move while both players are idle', units=round(idle, 3), seconds=IDLE_SECONDS)
    trial.not_run("B's jump and landing", 'foreign B has no jump ability in this fixture')


def review(closed):
    transitions = sum(len(keys) for keys in LEGS) * 2
    for player in (1, 2):
        role = f'player-{player}'
        expected = transitions + (2 if player == 1 else 0)
        posted = closed.events(role, 'network_input_posted')
        closed.expect(len(posted) == expected and all(p['posted'] and p['input_class'] == 'INPUT_IN_PROCESS' for p in posted),
                      f'player {player}: every key posted down and up in its own process', posted=len(posted), expected=expected)
        keys = [k for k in closed.events(role, 'network_key') if k['key'] != ord(' ')]
        closed.expect([(k['key'], k['down']) for k in keys] == [(ord(c), d) for leg in LEGS for d in (True, False) for c in leg],
                      f'player {player}: its window procedure saw each leg go down then up, in order')
        views = [r for r in closed.events(role, 'scene_native_replica_sample') if r['owner'] == player]
        closed.expect(bool(views) and not views[-1]['dead'], f'player {player} ends alive')
    closed.expect_none('authority', 'native_unowned_movement_unstoppable')
    for role in ('authority', 'player-1', 'player-2'):
        closed.expect_none(role, 'native_relationship_acceptance_failed')
        closed.expect_none(role, 'network_rejected')
