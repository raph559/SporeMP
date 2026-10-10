"""C03 probe: does a player's creature get its health back at its own species' nest?

From the checkpoint sealed by trial146, where player 2's creature starts with 3.5 health and a fight costs it
3 (trial318: it died in its second fight). The player walks to the nearest nest of its own species with its
movement keys (INPUT_IN_PROCESS), stands there, and the authority's health for its creature is recorded every
two seconds. It judges nothing yet.
"""
import math

ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 200
PLAYER = 2
VARIANTS = {'player-1': {'PLAYER': 1}}
STEPS, WITHIN, UNITS_PER_SECOND = 24, 2.5, 2.5
STAND_SECONDS = 50


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def unit(v):
    length = math.sqrt(dot(v, v)) or 1
    return [x / length for x in v]


def camera_axes(position, centre_ray, right_ray):
    up = unit(position)
    forward = unit([c - u * dot(centre_ray, up) for c, u in zip(centre_ray, up)])
    right = [r - c * dot(right_ray, centre_ray) for r, c in zip(right_ray, centre_ray)]
    return forward, unit([r - u * dot(right, up) for r, u in zip(right, up)])


def drive(trial):
    trial.wait_ready(stable_seconds=3)
    authority = trial.traces['authority']
    role = f'player-{PLAYER}'

    def owned():
        found = [r for r in authority.entities.values() if r['epoch'] == trial.scene and r.get('owner') == PLAYER]
        return found[0] if len(found) == 1 else None

    def view():
        mark = trial.traces[role].sequence
        trial.click(role, owned()['remote_entity'], button='none', expect=None)
        return trial.until(role, 'native_screen_point', where=lambda r: r['entity'] == owned()['remote_entity'], after=mark, timeout=5)

    trial.wait(lambda: owned() and owned()['velocity'] == [0, 0, 0], 30, 'the creature at rest')
    start = owned()
    nests = [r for r in authority.retained if r['event'] == 'scene_native_nest_published' and r['species'] == start['species']]
    trial.expect(bool(nests), f"player {PLAYER}'s species has a nest in the scene", nests=len(nests))
    nest = min(nests, key=lambda r: math.dist(r['position'], start['position']))
    keys = []
    for _ in range(STEPS):
        here = owned()
        offset = [n - h for n, h in zip(nest['position'], here['position'])]
        if math.sqrt(dot(offset, offset)) <= WITHIN:
            break
        row = view()
        forward, right = camera_axes(here['position'], row['centre_ray'], row['right_ray'])
        ahead, aside = dot(offset, forward), dot(offset, right)
        key, units = (('W' if ahead > 0 else 'S'), abs(ahead)) if abs(ahead) >= abs(aside) else (('D' if aside > 0 else 'A'), abs(aside))
        keys.append(key)
        trial.hold(role, key, min(4.0, 0.4 + units / UNITS_PER_SECOND))
        trial.settle(1.5)
    apart = math.dist(owned()['position'], nest['position'])
    trial.expect(apart <= WITHIN + 1.5, f'player {PLAYER} walks to its own nest', keys=''.join(keys), units_from_nest=round(apart, 1),
                 units_walked=round(math.dist(start['position'], nest['position']), 1))
    readings = [round(owned()['health'], 2)]
    for _ in range(STAND_SECONDS // 2):
        trial.settle(2)
        readings.append(round(owned()['health'], 2))
    trial.expect(True, f'player {PLAYER} stands at its nest', health_every_two_seconds=readings, health_at_start=start['health'])
