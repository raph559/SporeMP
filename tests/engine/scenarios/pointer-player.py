"""C01, pointer input on the other player: each player walks up to the other's creature and clicks it.

From the checkpoint sealed by trial146, where the two creatures stand 57 units
apart. Every key and pointer action is an ordinary message posted to that
player's own game window (INPUT_IN_PROCESS). The camera never turns from
keys, so a player sidesteps and walks until the other player's creature
stands a few units beyond its own, straight ahead on screen, then left-clicks
it where its own window draws it. The original's own picking in that game
must name exactly that creature. A wild creature or a plant in front is
handled as a person would: wait for the creature to move on, click lower or
higher on the body, or step aside and click again.

The other player's creature does not wander, unlike the wild creatures of
pointer-target.py, whose outcome depended on where they happened to walk
(trials 182 to 190).
"""
import math

ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 300
STEPS = 34
# Trials 172 to 191: the original picked creatures clicked from 5 to 7 units away, and not one 66 units away.
# The other creature is wanted this far beyond the player along the camera's forward direction, and this close sideways.
# Trial193: six clicks from 18 units of camera distance picked nothing; every click that was picked was made from 9 to 16.
AHEAD, ASIDE = (2.0, 5.0), 1.5
# Held W covered 14 units in 4 seconds in trial165; the other keys start slower, which the fixed 0.4 seconds allows for.
UNITS_PER_SECOND = 3.5
STILL = 0.2
# Wild creatures looked at before a click, and how close to the aim point one is drawn to count as in the way.
NEIGHBOURS, CLEAR_PIXELS = 4, 110
# How high on the creature's own box each click aims, in percent. Trial195: an idle creature had lain down in tall
# grass and 18 clicks at mid-height passed over it. After a miss at each height the player steps aside.
HEIGHTS = (50, 20, 80)
# How far from its position a resting creature may be drawn, once the first samples after loading have passed.
MODEL_GAP, SETTLING_SAMPLES = 0.6, 3


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

    def owned(player):
        found = [r for r in creatures() if r.get('owner') == player]
        return found[0] if len(found) == 1 else None

    def at_rest(player):
        # Nearly still counts: on a slope a creature that has stopped walking keeps creeping (trial181).
        found = owned(player)
        return found if found and math.hypot(*found['velocity']) < STILL else None

    def shown(role, creature):
        """Where that player's window draws a creature: its screen-point row, pointing at it on the way."""
        mark = trial.traces[role].sequence
        trial.click(role, creature['remote_entity'], button='none', expect=None)
        return trial.until(role, 'native_screen_point', where=lambda r: r['entity'] == creature['remote_entity'], after=mark, timeout=5)

    def step(role, player, key, units, held):
        held.append(key)
        trial.hold(role, key, min(2.0, 0.4 + units / UNITS_PER_SECOND))
        trial.wait(lambda: at_rest(player), 12, f'player {player} at rest after moving')

    for player in (1, 2):
        role, partner = f'player-{player}', 3 - player
        trial.wait(lambda: at_rest(player) and at_rest(partner), 30, f'both creatures at rest before player {player} moves')
        apart = math.dist(owned(player)['position'], owned(partner)['position'])
        held, misses, waits, hit, aim, ahead, aside, height = [], [], 0, None, None, 0.0, 0.0, None
        for _ in range(STEPS):
            here, target = owned(player), owned(partner)
            view = shown(role, here)
            forward, right = camera_axes(here['position'], view['centre_ray'], view['right_ray'])
            offset = [t - h for t, h in zip(target['position'], here['position'])]
            ahead, aside = dot(offset, forward), dot(offset, right)
            if abs(aside) > ASIDE:
                step(role, player, 'D' if aside > 0 else 'A', abs(aside), held)
                continue
            if not AHEAD[0] <= ahead <= AHEAD[1]:
                goal = sum(AHEAD) / 2
                step(role, player, 'W' if ahead > goal else 'S', abs(ahead - goal), held)
                continue
            aim = shown(role, target)
            if not aim['inside']:
                step(role, player, 'S', 1.0, held)  # in place but under the HUD: one step back and look again
                continue
            wild = sorted((r for r in creatures() if not r.get('owner')), key=lambda r: math.dist(r['position'], here['position']))[:NEIGHBOURS]
            rows = [shown(role, r) for r in wild]
            reach = math.dist(aim['camera'], aim['position'])
            if any(n['projected'] and math.dist(n['point'], aim['point']) < CLEAR_PIXELS and math.dist(n['camera'], n['position']) < reach
                   for n in rows):
                # A wild creature is drawn in front of where the click would go (trial194 also counted those behind,
                # and never clicked). They wander: wait, and after three waits step aside.
                waits += 1
                held.append('.')
                if waits % 3:
                    trial.settle(1.5)
                else:
                    step(role, player, 'A' if waits % 2 else 'D', 2.0, held)
                continue
            mark = trial.traces[role].sequence
            height = HEIGHTS[len(misses) % len(HEIGHTS)]
            if trial.click(role, target['remote_entity'], expect=None, height=height).get('result') != 'accepted':
                continue
            trial.settle(1.5)
            picked = next((r for r in trial.traces[role].retained if r['sequence'] > mark and r['event'] == 'native_pickup_ui_observed'), None)
            if picked and picked['target_resolved'] and picked['target_native_id'] == aim['native_id']:
                hit = picked
                break
            # The original picks what is in front; a plant can cover a creature that is geometrically in view (trial186).
            misses.append('nothing' if not picked else 'another creature' if picked['target_resolved'] else 'not a creature')
            if len(misses) % len(HEIGHTS) == 0:
                step(role, player, 'A' if len(misses) % 2 else 'D', 2.0, held)
        walked = math.dist(owned(player)['position'], owned(partner)['position'])
        trial.expect(hit is not None, f"player {player} walks up to player {partner}'s creature with its own keys and its click picks exactly that creature",
                     keys=''.join(held), clicks=len(misses) + bool(hit), misses=misses, units_apart_before=round(apart, 1),
                     units_apart_after=round(walked, 1), ahead=round(ahead, 1), aside=round(aside, 1),
                     clicked_native_id=aim['native_id'] if aim else None, picked_native_id=hit['target_native_id'] if hit else None,
                     point=aim['point'] if aim else None, height_percent=height if hit else None,
                     camera_distance=round(math.dist(aim['camera'], aim['position']), 1) if aim else None)
        trial.settle(2)
    trial.not_run('Ability keys on the clicked creature, right click, clicks on nests, fossils and food', 'later C01 increments')
    trial.settle(2)


def review(closed):
    for player in (1, 2):
        role = f'player-{player}'
        posted = closed.events(role, 'network_pointer_posted')
        closed.expect(any(p['action'] == 2 for p in posted) and all(p['posted'] and p['input_class'] == 'INPUT_IN_PROCESS' for p in posted),
                      f'player {player}: pointer messages posted in its own process', posted=len(posted))
        points = [(p['point'], p['clickable']) for p in closed.events(role, 'native_screen_point') if p['inside']]
        closed.expect(bool(points) and all(left <= x <= right and top <= y <= bottom for (x, y), (left, top, right, bottom) in points),
                      f'player {player}: every accepted point lies in the part of its view the HUD leaves free', accepted=len(points))
        steers = [r for r in closed.events('authority', 'command_begin') if r['owner'] == player and r['verb'] == 'steer']
        closed.expect(bool(steers), f"authority ran player {player}'s steering from its held keys", steers=len(steers))
        # Trial207: on each client the other player's resting creature was drawn up to 6.5 units from where it stood.
        partner = 3 - player
        gaps = [math.dist(r['position'], r['presentation_context']['model_position'])
                for r in closed.events(role, 'scene_native_replica_sample')
                if r.get('owner') == partner and r['velocity'] == [0, 0, 0] and 'model_position' in r.get('presentation_context', {})]
        # One sample right after a creature stops may still show its model catching up (trial212: 0.74 once);
        # a gap that lasts over two samples, a second apart, is the defect.
        settled = gaps[SETTLING_SAMPLES:]
        lasting = [min(pair) for pair in zip(settled, settled[1:])]
        closed.expect(bool(lasting) and max(lasting) <= MODEL_GAP,
                      f"player {player}'s game draws player {partner}'s resting creature where it stands",
                      largest_lasting_gap=round(max(lasting), 2) if lasting else None,
                      largest_single_gap=round(max(settled), 2) if settled else None, samples=len(settled))
    for role in ('authority', 'player-1', 'player-2'):
        closed.expect_none(role, 'native_relationship_acceptance_failed')
        closed.expect_none(role, 'native_unowned_movement_unstoppable')
