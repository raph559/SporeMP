"""C01: a player's camera keeps its direction while its creature moves.

From the checkpoint sealed by trial146. Up to bridge 0.0.224 player 2's camera turned by itself whenever its
creature moved (trials 270 to 295), and the creature, steered relative to that camera, walked a curve. Its
avatar, a wild creature restored while the original AI was walking it and then made the avatar, still held
that native walk on its own client, and the original camera turns toward the destination of its avatar's
walk. The client now clears such a request with the original stop.

The player's creature is moved without any key and without camera-relative steering, by the diagnostic
move intention (DIAGNOSTIC_NETWORK_INTENT: six units in a direction relative to the creature), then by a held
W and a held D (INPUT_IN_PROCESS). The camera is read each half second by pointing at the creature with no
button. It must not turn more than TURN_DEGREES in any phase, and the client's own avatar must end without
a native locomotion request. `--variant player-1` runs the same for player 1.
"""
import math

ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 150
PLAYER = 2
VARIANTS = {'player-1': {'PLAYER': 1}}
PHASE_SECONDS = 5
# Player 1's camera turned 0 degrees in every phase of trial296; player 2's 77 in one phase of trial279.
TURN_DEGREES = 5


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def unit(v):
    length = math.sqrt(dot(v, v)) or 1
    return [x / length for x in v]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def drive(trial):
    trial.wait_ready(stable_seconds=3)
    authority = trial.traces['authority']
    role = f'player-{PLAYER}'

    def owned():
        found = [r for r in authority.entities.values() if r['epoch'] == trial.scene and r.get('owner') == PLAYER]
        return found[0] if len(found) == 1 else None

    frame = {}

    def bearing():
        """Degrees the camera's forward direction has turned from its first reading, about the creature's up."""
        mark = trial.traces[role].sequence
        trial.click(role, owned()['remote_entity'], button='none', expect=None)
        row = trial.until(role, 'native_screen_point', where=lambda r: r['entity'] == owned()['remote_entity'], after=mark, timeout=5)
        up = unit(row['position'])
        forward = unit([c - u * dot(row['centre_ray'], up) for c, u in zip(row['centre_ray'], up)])
        if not frame:
            frame.update(forward=forward, side=cross(up, forward))
        return round(math.degrees(math.atan2(dot(forward, frame['side']), dot(forward, frame['forward']))))

    def watch(label, act=None):
        place, readings = owned()['position'], [bearing()]
        if act:
            act()
        for _ in range(PHASE_SECONDS * 2):
            trial.settle(0.5)
            readings.append(bearing())
        turned = max(abs((reading - readings[0] + 180) % 360 - 180) for reading in readings)
        trial.expect(turned <= TURN_DEGREES, label + ': its camera keeps its direction', camera_bearings=readings,
                     creature_moved=round(math.dist(owned()['position'], place), 1))

    trial.wait(lambda: owned() and owned()['velocity'] == [0, 0, 0], 30, 'the creature at rest')
    watch(f'player {PLAYER} idle')
    for direction in (0, 1, 2, 3):
        watch(f'player {PLAYER} moved by the diagnostic intention, direction {direction}',
              lambda: trial.control(role, 'move', trial.scene, 0, 0, direction, expect=None))
    trial.key(role, 'W', True)
    try:
        watch(f'player {PLAYER} holds W')
    finally:
        trial.key(role, 'W', False)
    watch(f'player {PLAYER} idle again')
    trial.key(role, 'D', True)
    try:
        watch(f'player {PLAYER} holds D')
    finally:
        trial.key(role, 'D', False)


def review(closed):
    role = f'player-{PLAYER}'
    cleared = closed.events(role, 'native_client_avatar_request_cleared')
    closed.expect(all(row['request_words_after'] == 0 for row in cleared),
                  f"player {PLAYER}: its own avatar keeps no native locomotion request", cleared=[(r['request_words'], r['request_words_after']) for r in cleared])
    for other in ('player-1', 'player-2'):
        closed.expect_none(other, 'native_client_avatar_request_unstoppable')
        closed.expect_none(other, 'network_rejected')
