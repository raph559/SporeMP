"""C01, pointer input: each player points at and clicks creatures in its own game.

From the checkpoint sealed by trial146. Every pointer action is an ordinary
mouse message posted to that player's own game window at the place where that
window draws the creature (INPUT_IN_PROCESS). A player first points at its
own creature, which its camera always shows. It then walks with its own held
keys until the nearest other creature is close and its window shows it clear
of the HUD (trial170: at rest after restore player 1's camera shows none),
and left-clicks it. When the original picks something else, because a plant
or another creature is in front, the player steps aside and clicks again. The original's
own picking in that game must then name exactly the creature that was clicked
(trial171: without a pointer position the process itself sees, it named none).
"""
import math

ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 300
STEPS = 26
# Trials 172 and 176: the original picked creatures clicked from 5 and 7 units away, and not one 66 units away.
# The creature is wanted this far beyond the player along the camera's forward direction, and this close to it sideways.
AHEAD, ASIDE = (3.0, 9.0), 2.0
# Held W covered 14 units in 4 seconds in trial165; the other keys start slower, which the fixed 0.4 seconds allows for.
UNITS_PER_SECOND = 3.5
STILL = 0.2
# Other creatures looked at before a click, and how far from the aim point one must be drawn.
NEIGHBOURS, CLEAR_PIXELS = 4, 110
REACTIONS = ('native_target_enter', 'network_intention', 'replica_target_denied', 'network_pickup_ui')


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

    def creatures():
        return [r for r in authority.entities.values()
                if r['epoch'] == trial.scene and r['event'] == 'scene_native_authority_sample' and not r['dead']]

    def at_rest(player):
        found = [r for r in creatures() if r.get('owner') == player]
        # Nearly still counts: on a slope a creature that has stopped walking keeps creeping for many
        # seconds (trial181: 0.01 to 0.1 units per second, never exactly zero).
        return found[0] if len(found) == 1 and math.hypot(*found[0]['velocity']) < STILL else None

    def shown(creature):
        """Where the acting player's window draws a creature: its screen-point row, pointing at it on the way."""
        mark = trial.traces[role].sequence
        trial.click(role, creature['remote_entity'], button='none', expect=None)
        return trial.until(role, 'native_screen_point', where=lambda r: r['entity'] == creature['remote_entity'], after=mark, timeout=5)

    for player in (1, 2):
        role = f'player-{player}'
        me = trial.wait(lambda: at_rest(player), 30, f'player {player} at rest')
        own = trial.click(role, me['remote_entity'], button='none', expect=None)
        trial.expect(own.get('result') == 'accepted', f"player {player}'s window shows its own creature and the pointer reaches it")
        held, misses, clicked, picked, aim = [], [], None, None, None
        for _ in range(STEPS):
            # The camera never turns from keys: movement is camera-relative and the camera only follows. So the
            # player sidesteps and walks until the creature stands a few units beyond it, straight ahead on screen.
            here = at_rest(player) or me
            other = min((r for r in creatures() if not r.get('owner')), key=lambda r: math.dist(r['position'], here['position']))
            mark = trial.traces[role].sequence
            trial.click(role, here['remote_entity'], button='none', expect=None)
            view = trial.until(role, 'native_screen_point', where=lambda r: r['entity'] == here['remote_entity'], after=mark, timeout=5)
            forward, right = camera_axes(here['position'], view['centre_ray'], view['right_ray'])
            offset = [t - h for t, h in zip(other['position'], here['position'])]
            ahead, aside = dot(offset, forward), dot(offset, right)
            if abs(aside) <= ASIDE and AHEAD[0] <= ahead <= AHEAD[1]:
                aim = shown(other)
                if aim and aim['inside']:
                    # The original picks whatever is nearest under the pointer (trial182: a second creature stood
                    # in front), so look at where the other nearby creatures are drawn before clicking.
                    rest = sorted((r for r in creatures() if not r.get('owner') and r['remote_entity'] != other['remote_entity']),
                                  key=lambda r: math.dist(r['position'], here['position']))[:NEIGHBOURS]
                    group = [(r, n) for r, n in ((r, shown(r)) for r in rest)
                             if n['projected'] and math.dist(n['point'], aim['point']) < CLEAR_PIXELS]
                    if group:
                        # Several drawn together (trial187: a herd that never parted): click the one nearest the camera.
                        other, aim = min(group + [(other, aim)], key=lambda pair: math.dist(pair[1]['camera'], pair[1]['position']))
                        if not aim['inside']:
                            held.append('.')
                            trial.settle(1.0)
                            continue
                    mark = trial.traces[role].sequence
                    if trial.click(role, other['remote_entity'], expect=None).get('result') == 'accepted':
                        trial.settle(1.5)
                        picked = next((r for r in trial.traces[role].retained
                                       if r['sequence'] > mark and r['event'] == 'native_pickup_ui_observed'), None)
                        if picked and picked['target_resolved'] and picked['target_native_id'] == aim['native_id']:
                            clicked = other
                            break
                        # The original picks what is in front: a plant, a rock or nothing can cover a creature
                        # that is geometrically in view (trial186: a bush). Like a person, step aside and click again.
                        misses.append('nothing' if not picked else 'another creature' if picked['target_resolved'] else 'not a creature')
                        key, units = ('A' if len(misses) % 2 else 'D'), 2.0
                        held.append(key)
                        trial.hold(role, key, 0.4 + units / UNITS_PER_SECOND)
                        trial.wait(lambda: at_rest(player), 12, f'player {player} at rest after stepping aside')
                        continue
                key, units = 'S', 1.0  # in place but under the HUD or hidden: one step back and look again
            elif abs(aside) > ASIDE:
                key, units = ('D' if aside > 0 else 'A'), abs(aside)
            else:
                goal = sum(AHEAD) / 2
                key, units = ('W' if ahead > goal else 'S'), abs(ahead - goal)
            held.append(key)
            trial.hold(role, key, min(2.0, 0.4 + units / UNITS_PER_SECOND))
            trial.wait(lambda: at_rest(player), 8, f'player {player} at rest after moving')
        trial.expect(clicked is not None, f"player {player} walks up to another creature with its own keys and clicks it",
                     keys=''.join(held), entity=clicked['remote_entity'] if clicked else None, ahead=round(ahead, 1), aside=round(aside, 1),
                     clicks=len(misses) + bool(clicked), misses=misses)
        if clicked is None:
            continue
        trial.expect(picked['picked_present'] and picked['target_resolved'] and picked['target_native_id'] == aim['native_id'],
                     f"the original in player {player}'s game picks exactly the creature that was clicked",
                     clicked_native_id=aim['native_id'], picked_native_id=picked['target_native_id'], point=aim['point'])
        trial.settle(2)
        seen = sorted({r['event'] for r in trial.traces[role].retained if r['sequence'] > mark and r['event'] in REACTIONS})
        trial.expect(bool(seen), f"player {player}'s game reacts to the click", reactions=seen, entity=clicked['remote_entity'])
    trial.not_run('Ability keys on the clicked target, right click, clicks on nests, fossils and food', 'later C01 increments')
    trial.settle(3)


def review(closed):
    for player in (1, 2):
        role = f'player-{player}'
        posted = closed.events(role, 'network_pointer_posted')
        closed.expect(len([p for p in posted if p['action'] != 1]) >= 1 and all(p['posted'] and p['input_class'] == 'INPUT_IN_PROCESS' for p in posted),
                      f'player {player}: pointer messages posted in its own process', posted=len(posted))
        points = [(p['point'], p['clickable']) for p in closed.events(role, 'native_screen_point') if p['inside']]
        closed.expect(bool(points) and all(left <= x <= right and top <= y <= bottom for (x, y), (left, top, right, bottom) in points),
                      f'player {player}: every accepted point lies in the part of its view the HUD leaves free',
                      points=[point for point, _ in points[:4]])
    for role in ('authority', 'player-1', 'player-2'):
        closed.expect_none(role, 'native_relationship_acceptance_failed')
