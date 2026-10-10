"""C02: a player walks to a fruit, clicks it, and its creature eats it on the authority.

From the distinct-species checkpoint (trial146), in which player 2's creature is a wounded, hungry herbivore
and player 1's a carnivore. For each fruit:

1. the authority's census of plant food gives the fruits near the players (read only);
2. the player walks to the nearest one with its movement keys;
3. the pointer is put on the fruit where that player's own window draws it, then clicked; the click must reach
   the authority as that creature's intention and the authority must give the original's own order;
4. if the original lets that creature's diet eat fruit, its hunger must rise, the fruit must leave the
   authority's world and both clients', and the three games must hold the same hunger for the creature.

Every key and click is posted in that player's own game (INPUT_IN_PROCESS). Hunger is each game's own value for
the creature (NATIVE state; the client's copy is what its HUD draws).

Variants: `player-1` is the carnivore, ordered to a fallen fruit: the original has it go to the fruit, begin to
eat and turn away, and feeds it nothing (trial434; the runs up to trial415 ordered it to a fruit on a tall plant).
`from-the-plant` eats a fallen fruit, then is ordered to one still on its plant, from which no creature of this
fixture is fed. `fed-and-saved` eats until fed, then requests the original Save; run with `--seal`. `restored`
starts from the checkpoint that run sealed (trial370) and checks what was saved, then eats again. `both-order`
has both players stand at one fallen fruit and each order its own creature to it, one click after the other: two
creatures and one food at the same moment.
"""
import math

ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 290
PLAYER = 2
# Whether the original lets that player's creature eat fruit.
EATS = True
# How many fruits to eat at most, and the hunger at which the creature has had enough.
FRUITS, FED = 1, 101.0
# Only fruits still on their plant (True), only fallen ones (False), or the nearest of either (None), from the
# fruit of that number on: the wounded herbivore eats a fallen one first to live long enough.
ON_HOLDER, ON_HOLDER_FROM = None, 1
# Whether to request the original Save at the end, and what a session restored from that save must show.
SAVE, RESTORED = False, None
# The hunger to wait for before the first fruit: trial373's restored creature, at 91, had not eaten its fruit in 25 seconds.
WAIT_FOR_HUNGER = None
# Another player that walks to the first fruit too and orders it at the same moment as this one.
RIVAL = None
VARIANTS = {'player-1': {'PLAYER': 1, 'EATS': False, 'ON_HOLDER': False},
            'both-order': {'RIVAL': 1, 'ON_HOLDER': False},
            'from-the-plant': {'FRUITS': 2, 'FED': 200.0, 'ON_HOLDER': True, 'ON_HOLDER_FROM': 2},
            'fed-and-saved': {'FRUITS': 4, 'FED': 95.0, 'SAVE': True},
            'restored': {'ARTIFACT': 'local/native-trials/trial370/sealed-checkpoint/checkpoint.smp', 'WAIT_FOR_HUNGER': 70.0, 
                         # What trial370 saved: the fruits left in the world, player 2's health and hunger, each player's DNA.
                         'RESTORED': {'fruits': 146, 'health': 7.25, 'hunger': 97.4, 'dna': {1: 14, 2: 7}}}}
KINDS = {25986217: 'plant', 48851090: 'fruit_group', 46779537: 'fruit'}
FRUIT = 46779537
STEPS = 24
UNITS_PER_SECOND = 3.5
NEARBY = 5.0
EAT_SECONDS = 14
REST_SECONDS = 22
# How near its own creature's place on screen, in pixels, a fruit is taken to be hidden by it.
OWN_BODY = 70
# How many pixels above the fruit's own place to click, in turn, until the original picks it.
AIM_LIFTS = (0, 8, 16, 26)
# The health number of the HUD: this window identity is also used over other creatures' heads, so the one in the
# HUD's corner is taken (trial363: box 1215,563 to 1270,576 of a 1280 by 720 view).
HEALTH_TEXT, HUD_CORNER = 80755112, (1150, 500)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def unit(v):
    length = math.sqrt(dot(v, v)) or 1
    return [x / length for x in v]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def camera_axes(position, centre_ray, right_ray):
    up = unit(position)
    forward = unit([c - u * dot(centre_ray, up) for c, u in zip(centre_ray, up)])
    right = [r - c * dot(right_ray, centre_ray) for r, c in zip(right_ray, centre_ray)]
    return forward, unit([r - u * dot(right, up) for r, u in zip(right, up)])


def level(offset, position):
    """The part of an offset along the ground at a place: a fruit hangs above where the creature can stand."""
    up = unit(position)
    return [o - u * dot(offset, up) for o, u in zip(offset, up)]


def projector(view):
    """World place to client pixel, from one screen-point row: its camera, the ray through the middle of the view
    and the ray a quarter of the view's width to the right of it. The row's own creature fixes which way is up."""
    left, top, width, height = view['viewport']
    centre, quarter = unit(view['centre_ray']), unit(view['right_ray'])
    focal = (width / 4) / math.tan(math.acos(max(-1.0, min(1.0, dot(centre, quarter)))))
    right = unit([q - c * dot(quarter, centre) for q, c in zip(quarter, centre)])

    def project(place, up):
        ray = [p - c for p, c in zip(place, view['camera'])]
        depth = dot(ray, centre)
        if depth <= 0.1:
            return None
        return [left + width / 2 + focal * dot(ray, right) / depth, top + height / 2 - focal * dot(ray, up) / depth]

    raised = [p + u * view['raise'] for p, u in zip(view['position'], unit(view['position']))]
    choices = []
    for up in (cross(right, centre), cross(centre, right)):
        seen = project(raised, up)
        choices.append((math.dist(seen, view['point']) if seen else float('inf'), up))
    error, up = min(choices, key=lambda c: c[0])
    return (lambda place: project(place, up)), error


def drive(trial):
    trial.wait_ready(stable_seconds=3)
    authority = trial.traces['authority']
    role = f'player-{PLAYER}'

    def samples():
        return [r for r in authority.entities.values() if r['epoch'] == trial.scene and r['event'] == 'scene_native_authority_sample']

    def owned(player):
        found = [r for r in samples() if not r['dead'] and r.get('owner') == player]
        return found[0] if len(found) == 1 else None

    def shown(creature, player=None):
        who = f'player-{player or PLAYER}'
        mark = trial.traces[who].sequence
        if trial.click(who, creature['remote_entity'], button='none', expect=None).get('result') == 'accepted':
            return trial.until(who, 'native_screen_point', where=lambda r: r['entity'] == creature['remote_entity'], after=mark, timeout=5)
        return None

    def census():
        """The authority's next census of plant food: its totals and the fruits it lists, nearest to the player first."""
        mark = authority.sequence
        done = trial.until('authority', 'native_plant_food_census_complete', after=mark, timeout=25)
        rows = [r for r in authority.retained if r['event'] == 'native_plant_food_census' and r['sample'] == done['sample'] and r['noun_id'] == FRUIT]
        me = owned(PLAYER)
        # Each game re-places part of its fruit on its own (trial378). A player can only be fed from a fruit its own
        # world shows where the authority's holds it: the player's latest census says which those are.
        mine = {}
        for r in trial.traces[role].retained:
            if r['event'] == 'native_plant_food_census' and r['noun_id'] == FRUIT:
                mine[r['native_id']] = (r['sample'], r['position'])
        latest = max((sample for sample, _ in mine.values()), default=None)
        if latest is not None:
            for r in rows:
                seen = mine.get(r['native_id'])
                r['same_in_my_world'] = bool(seen and seen[0] == latest and math.dist(seen[1], r['position']) <= 1.0)
        return done, sorted(rows, key=lambda r: math.dist(r['position'], me['position']))

    def hud_health(player):
        """The health number on that player's own HUD (UI_READBACK), or None."""
        found = [w['text'] for w in trial.ui(f'player-{player}') if w['id'] == HEALTH_TEXT and w['text'].isdigit()
                 and w['box'][0] >= HUD_CORNER[0] and w['box'][1] >= HUD_CORNER[1]]
        return int(found[0]) if len(found) == 1 else None

    def since(source, mark, *events):
        return [r for r in trial.traces[source].retained if r['sequence'] > mark and r['event'] in events]

    def approach(food, player):
        """Walk a player's creature to a fruit and find the fruit in that player's own view."""
        who, held = f'player-{player}', []
        for _ in range(STEPS):
            here = owned(player)
            offset = level([t - h for t, h in zip(food['position'], here['position'])], here['position'])
            if math.sqrt(dot(offset, offset)) <= NEARBY:
                break
            view = shown(here, player)
            if view is None:
                trial.settle(1)
                continue
            forward, right = camera_axes(here['position'], view['centre_ray'], view['right_ray'])
            ahead, aside = dot(offset, forward), dot(offset, right)
            key, units = (('W' if ahead > 0 else 'S'), abs(ahead)) if abs(ahead) >= abs(aside) else (('D' if aside > 0 else 'A'), abs(aside))
            held.append(key)
            trial.hold(who, key, min(4.0, 0.4 + max(1.0, units - NEARBY / 2) / UNITS_PER_SECOND))
            trial.settle(0.8)
        here = owned(player)
        # A fruit on its plant hangs above the view of a creature standing under it (trial372): step back until
        # the player's own view shows it.
        for _ in range(6):
            view = shown(here, player)
            project, error = projector(view) if view else (None, float('inf'))
            point = project(food['position']) if project else None
            left, top, right, bottom = view['clickable'] if view else (0, 0, 0, 0)
            inside = point is not None and left <= point[0] <= right and top <= point[1] <= bottom
            if inside or view is None:
                break
            held.append('S')
            trial.hold(who, 'S', 1.2)
            trial.settle(0.8)
            here = owned(player)
        # A fruit the creature walked straight up to lies behind its own body in its player's view (trials 420 and
        # 432: the clicks there picked the creature, or a fruit hanging beyond it): it steps aside until the fruit
        # shows clear of it.
        for _ in range(3):
            if not (view and point and inside and math.dist(point, view['point']) < OWN_BODY):
                break
            held.append('D')
            trial.hold(who, 'D', 0.8)
            trial.settle(0.8)
            here = owned(player)
            view = shown(here, player)
            project, error = projector(view) if view else (None, float('inf'))
            point = project(food['position']) if project else None
            left, top, right, bottom = view['clickable'] if view else (0, 0, 0, 0)
            inside = point is not None and left <= point[0] <= right and top <= point[1] <= bottom
        offset = level([t - h for t, h in zip(food['position'], here['position'])], here['position'])
        return dict(here=here, keys=''.join(held), apart=math.sqrt(dot(offset, offset)), error=error, point=point, inside=inside)

    def contend(food):
        """Both players stand at one fruit and each orders its own creature to it, one click after the other."""
        both = (PLAYER, RIVAL)
        near = {p: approach(food, p) for p in (RIVAL, PLAYER)}
        # The first to arrive may have been turned by the other's arrival: its view is taken again.
        near[RIVAL] = approach(food, RIVAL)
        trial.expect(all(n['inside'] and n['error'] < 3 for n in near.values()), 'both players walk to one fruit and each finds it in its own view',
                     native_id=food['native_id'], on_holder=food.get('on_holder'),
                     **{f'player_{p}': dict(keys=n['keys'], apart=round(n['apart'], 1), point=n['point'] and [round(v) for v in n['point']]) for p, n in near.items()})
        if not all(n['inside'] for n in near.values()):
            return None
        # A creature walking up to a fruit can push it along (trial439: two units, and 24 clicks at where it had
        # lain picked nothing): the fruit's place is read again from the authority's next census.
        _, now = census()
        moved = next((r for r in now if r['native_id'] == food['native_id']), None)
        if moved is not None:
            food = moved
        before = {p: owned(p) for p in both}
        mark = {'authority': authority.sequence, **{p: trial.traces[f'player-{p}'].sequence for p in both}}
        sent = lambda p: [r['fruit_native_id'] for r in since(f'player-{p}', mark[p], 'native_plant_food_ui_forwarded') if r['sent']]
        left = lambda: since('authority', mark['authority'], 'native_plant_food_eaten')
        clicks = {p: 0 for p in both}
        for aside in (None, 'D', 'A'):
            for p in both:
                # A player whose own creature hides the fruit steps aside and looks again (trial420).
                if aside and not sent(p) and not left():
                    trial.hold(f'player-{p}', aside, 1.0)
                    trial.settle(0.8)
                    again = approach(food, p)
                    near[p] = again if again['inside'] else near[p]
            for lift in AIM_LIFTS:
                # A view still turns for seconds after its creature stops (trial425: four clicks at a place found
                # before the other player's turn picked nothing): each looks again just before the two clicks.
                for p in (RIVAL, PLAYER):
                    if not sent(p) and not left():
                        again = approach(food, p)
                        near[p] = again if again['inside'] else near[p]
                for p in both:
                    if not sent(p) and not left():
                        trial.click_at(f'player-{p}', round(near[p]['point'][0]), round(near[p]['point'][1]) - lift, expect=None)
                        clicks[p] += 1
                trial.settle(0.6)
                if all(sent(p) for p in both) or left():
                    break
            if all(sent(p) for p in both) or left():
                break
        trial.settle(EAT_SECONDS)
        orders = {p: [r for r in since('authority', mark['authority'], 'network_actor_command_queued') if r['verb'] == 'forage' and r['owner'] == p] for p in both}
        at = {p: round(v[0]['qpc'] / v[0]['qpc_frequency'], 2) for p, v in orders.items() if v}
        picked = {p: sent(p) for p in both}
        trial.expect(all(picked[p] and set(picked[p]) == set(picked[PLAYER]) and any(r['decision'] == 'accepted' for r in orders[p]) for p in both),
                     "each player's click on the one fruit reaches the authority, which takes both orders",
                     picked=picked, clicks=clicks, decisions={p: [r['decision'] for r in v] for p, v in orders.items()},
                     seconds_between_the_orders=len(at) == 2 and round(abs(at[PLAYER] - at[RIVAL]), 2))
        after = {p: owned(p) for p in both}
        fed = {p: after[p] is not None and after[p]['hunger'] > before[p]['hunger'] for p in both}
        rows = [dict(fruit=r['fruit_native_id'], actor=r.get('actor'), fed=r.get('fed'), noun_removed=r.get('noun_removed')) for r in left()]
        _, now = census()
        still_there = [r['native_id'] for r in now if picked[PLAYER] and r['native_id'] == picked[PLAYER][0]
                       and math.dist(r['position'], food['position']) <= 1.5]
        # The original has the first creature to reach the fruit take it from its place, removing it or dealing it
        # out elsewhere; the other's order ends there. Only one that eats fruit is fed: the herbivore reached it
        # first in trial430 and was fed, the carnivore in trial449 and no one was. The authority's row for a fruit
        # that left names it only when its noun went or its orderer was fed.
        trial.expect(not still_there and not fed[RIVAL] and len({r['fruit'] for r in rows}) <= 1 and fed[PLAYER] == any(r['fed'] for r in rows),
                     'the fruit is taken from its place once, by the first creature to reach it; the creature that eats no fruit is fed nothing, whichever took it',
                     taken=rows, fed=fed, hunger={p: [round(before[p]['hunger'], 1), after[p] and round(after[p]['hunger'], 1)] for p in both})
        return after[PLAYER]

    def eat(food, number):
        """Walk to one fruit, click it, and report what followed."""
        near = approach(food, PLAYER)
        here, apart, error, point, inside = near['here'], near['apart'], near['error'], near['point'], near['inside']
        trial.expect(apart <= NEARBY * 5 and error < 3 and inside,
                     f'fruit {number}: player {PLAYER} walks to it, and it lies in the free part of its view by a projection that places its own creature to 3 pixels',
                     keys=near['keys'], apart=round(apart, 1), native_id=food['native_id'], on_holder=food.get('on_holder'),
                     creature_error=round(error, 2), point=point and [round(v) for v in point])
        if not inside:
            return None
        shown_before = hud_health(PLAYER)
        before = dict(client=trial.traces[role].sequence, authority=authority.sequence)
        # The fruit's own place is its foot; seen from close above, the body the original picks lies a little higher
        # on screen (trial395: three clicks at the foot picked nothing).
        # From very close the creature's own body can lie between the pointer and the fruit (trial399: four clicks
        # from 1.8 units picked nothing): the player then steps back and aims again. Where the fruit lies in line
        # with the creature that does not help (trial420: eight clicks picked the creature): it then steps aside.
        for retreat in (None, 'S', 'D', 'A'):
            if retreat:
                trial.hold(role, retreat, 1.0)
                trial.settle(0.8)
                again = approach(food, PLAYER)
                if not again['inside']:
                    break
                point = again['point']
            for lift in AIM_LIFTS:
                answer = trial.click_at(role, round(point[0]), round(point[1]) - lift, expect=None)
                trial.settle(1.0)
                if any(r['sent'] for r in since(role, before['client'], 'native_plant_food_ui_forwarded')):
                    break
            if any(r['sent'] for r in since(role, before['client'], 'native_plant_food_ui_forwarded')):
                break
        # Fruits of one plant hang close together: the original may pick a neighbour of the one aimed at (trial374).
        # Whichever it picks must be a fruit the authority holds, and is the one followed from here.
        chosen = [r['fruit_native_id'] for r in since(role, before['client'], 'native_plant_food_ui_forwarded') if r['sent']]
        picked = chosen[0] if chosen else food['native_id']
        eaten, clicks = None, 1
        # Whether the fruit picked still hangs on its plant, by the authority's census. No creature of this fixture
        # is fed from one there: ordered through its holder it runs its eating for four seconds and the fruit
        # stays (trials 375, 416). Whether the original lets a creature this small reach those plants is not
        # established.
        hanging = any(r['native_id'] == picked and r.get('on_holder') for r in fruits)
        eats = EATS and not hanging
        if chosen and not eats:
            # The carnivore goes to a fallen fruit, begins to eat and turns away for seven seconds, fed nothing;
            # the fruit is taken from its place (trials 419, 434, 449).
            trial.settle(EAT_SECONDS)
        elif chosen:
            try:
                eaten = trial.until('authority', 'native_plant_food_eaten', where=lambda r: r['fruit_native_id'] == picked and r.get('fed', True) == EATS,
                                    after=before['authority'], timeout=EAT_SECONDS)
            except Exception:
                eaten = None
            # Trial379: the creature walked to the fruit and stood by it without eating for 25 seconds. A player
            # would click again; whether that helps is recorded.
            while eaten is None and clicks < 3:
                clicks += 1
                again = approach(food, PLAYER)
                if not again['inside']:
                    break
                trial.click_at(role, round(again['point'][0]), round(again['point'][1]), expect=None)
                try:
                    eaten = trial.until('authority', 'native_plant_food_eaten', where=lambda r: r['fruit_native_id'] == picked and r.get('fed', True) == EATS,
                                        after=before['authority'], timeout=EAT_SECONDS)
                except Exception:
                    eaten = None
            trial.settle(3)
        forwarded = since(role, before['client'], 'native_plant_food_ui_forwarded')
        orders = [r for r in since('authority', before['authority'], 'network_actor_command_queued') if r['verb'] not in ('steer', 'steer_stop')]
        given = since('authority', before['authority'], 'native_plant_food_order_dispatched')
        after = owned(PLAYER)
        # The fruit the client's original picked under the pointer must be the one the authority holds at that place.
        trial.expect(answer.get('result') == 'accepted' and len(chosen) >= 1 and len(set(chosen)) == 1 and picked in known
                     and any(r['verb'] == 'forage' and r['decision'] == 'accepted' for r in orders) and any(r['order_returned'] for r in given),
                     f"fruit {number}: the click reaches the authority, which gives the creature the original's order",
                     forwarded=[(r['fruit_native_id'], r['sent']) for r in forwarded], lift=lift, authority_orders=[(r['verb'], r['decision']) for r in orders],
                     order=[{k: r.get(k) for k in ('through_holder', 'slot', 'order_returned', 'reason')} for r in given])
        gone = [dict(fed=r.get('fed'), noun_removed=r.get('noun_removed')) for r in since('authority', before['authority'], 'native_plant_food_eaten')
                if r['fruit_native_id'] == picked]
        trial.expect(after is not None and (eaten is not None) == eats and (after['hunger'] > here['hunger']) == eats,
                     f'fruit {number}: ' + ('the creature eats it: the fruit leaves the world and its hunger rises' if eats else
                                            'the original feeds this creature nothing from it: its hunger keeps falling'),
                     fruit=picked, still_on_its_plant=hanging, fruit_left_the_world=gone,
                     hunger=[round(here['hunger'], 1), after and round(after['hunger'], 1)], health=[here['health'], after and after['health']],
                     moved=after and round(math.dist(after['position'], here['position']), 1), clicks=clicks)
        # The HUD shows health as a whole number, rounded up (frames of trial382: 4 at 3.5, 6 at 5.375).
        shown_after = hud_health(PLAYER)
        trial.expect(after is not None and shown_before == math.ceil(here['health']) and shown_after == math.ceil(after['health']),
                     f"fruit {number}: player {PLAYER}'s own HUD shows its health before and after", shown=[shown_before, shown_after],
                     authority=[here['health'], after and after['health']])
        return after

    # 1. The census, taken every ten seconds by the authority.
    done, fruits = census()
    known = {r['native_id'] for r in fruits}
    me = owned(PLAYER)
    trial.expect(done['fruits'] > 0 and bool(fruits), 'the authority holds fruit near the players', plants=done['plants'],
                 fruit_groups=done['fruit_groups'], fruits=done['fruits'], listed=len(fruits),
                 nearest=[dict(native_id=r['native_id'], apart=round(math.dist(r['position'], me['position']), 1), on_holder=r.get('on_holder'),
                               same_in_my_world=r.get('same_in_my_world')) for r in fruits[:5]],
                 elsewhere_in_my_world=sum(1 for r in fruits if r.get('same_in_my_world') is False),
                 hunger=round(me['hunger'], 1), health=me['health'])
    if RESTORED is not None:
        other = owned(3 - PLAYER)
        # The original grows fruit anew when it loads a world: trial371 restored 157 fruits from a save made with 146.
        trial.expect(abs(me['health'] - RESTORED['health']) < 0.6 and RESTORED['hunger'] - 15 < me['hunger'] <= RESTORED['hunger'] + 1
                     and math.floor(me['native_dna']) == RESTORED['dna'][PLAYER] and math.floor(other['native_dna']) == RESTORED['dna'][3 - PLAYER],
                     'the restored player has the health and hunger it was saved with, less the hunger since, and each player its DNA',
                     fruits_now_and_at_save=[done['fruits'], RESTORED['fruits']], health=[me['health'], RESTORED['health']], hunger=[round(me['hunger'], 1), RESTORED['hunger']],
                     dna=[me['native_dna'], other['native_dna']])

    if WAIT_FOR_HUNGER is not None:
        trial.wait(lambda: owned(PLAYER) and owned(PLAYER)['hunger'] < WAIT_FOR_HUNGER, 150, f'hunger under {WAIT_FOR_HUNGER}')
        done, fruits = census()
        known |= {r['native_id'] for r in fruits}
        me = owned(PLAYER)

    # 2 to 4. Eat.
    started, eaten, tried = done['fruits'], 0, set()
    for number in range(1, FRUITS + 1):
        # A fruit the original does not let be picked is left out (trial424: a fallen one in state 4 took 16 clicks
        # and none picked it); of the fallen ones, those lying on the ground, state 2.
        wanted = [r for r in fruits if r['native_id'] not in tried and r.get('same_in_my_world', True) and r.get('pickable', True)
                  and (ON_HOLDER is None or number < ON_HOLDER_FROM or (r.get('on_holder') == ON_HOLDER and (ON_HOLDER or r.get('state', 2) == 2)))]
        if not wanted:
            trial.not_run(f'Fruit {number}', 'the census lists no further fruit of the kind asked for within reach')
            break
        if ON_HOLDER is False and number >= ON_HOLDER_FROM:
            # A fallen fruit under its plant lies in line with the ones still hanging, and the original picks one of
            # those (trial429: aimed at fallen fruit 4960, the clicks picked 11456 and 11446 on the plant above).
            clear = [r for r in wanted if not any(o.get('on_holder') and math.dist(o['position'], r['position']) <= 8.0 for o in fruits)]
            wanted = clear or wanted
        if RIVAL and number == 1:
            # Two players must pick the same fruit: one with no other within three units, where the original may
            # pick the neighbour (trial425: fruits 2804 and 2805, a unit apart, one each).
            alone = [r for r in wanted if not any(o is not r and math.dist(o['position'], r['position']) <= 3.0 for o in fruits)]
            wanted = alone or wanted
        tried.add(wanted[0]['native_id'])
        after = contend(wanted[0]) if RIVAL and number == 1 else eat(wanted[0], number)
        if after is None:
            break
        eaten += after['hunger'] > me['hunger'] or 0
        me = after
        if me['hunger'] >= FED or number == FRUITS:
            break
        done, fruits = census()
        known |= {r['native_id'] for r in fruits}
    done, fruits = census()
    final = owned(PLAYER)
    # The original takes an eaten fruit out of the world by removing its noun, or by re-placing the noun elsewhere
    # (trial383): the count of fruits is recorded, not required to fall.
    if not RIVAL:
        trial.expect((eaten > 0) == EATS, 'the creature has eaten' if EATS else 'the creature has eaten nothing',
                     meals=eaten, fruits=[started, done['fruits']], hunger=round(final['hunger'], 1), health=final['health'])
    if SAVE:
        mark = authority.sequence
        trial.control('authority', 'save', trial.scene)
        saved = trial.until('authority', 'native_checkpoint_save_complete', after=mark, timeout=30)
        final = owned(PLAYER)
        trial.expect(saved['success'] and not saved['diagnostic_only'], 'the original Save completes after the meal',
                     fruits=done['fruits'], hunger=round(final['hunger'], 1), health=final['health'],
                     dna={p: owned(p)['native_dna'] for p in (1, 2)})
        trial.settle(2)
    # The players stand still while each game takes two more censuses of its fruit: the authority's original deals
    # the fruit on plants out anew as its own avatar walks (trial424: one fruit at three plants within 30 seconds).
    trial.settle(REST_SECONDS)


def review(closed):
    for role in ('authority', 'player-1', 'player-2'):
        closed.expect_none(role, 'network_rejected')
        closed.expect_none(role, 'native_relationship_acceptance_failed')
    # A fruit two players ordered leaves the world once; from bridge 0.0.243 each row says whether that orderer was fed.
    eaten = [r['fruit_native_id'] for r in closed.events('authority', 'native_plant_food_eaten') if r.get('fed', True)]
    unfed = [r['fruit_native_id'] for r in closed.events('authority', 'native_plant_food_eaten') if not r.get('fed', True)]
    if RIVAL:
        closed.expect(len(set(eaten + unfed)) <= 1 and len(eaten) <= 1, "the fruit two players ordered leaves its place on the authority once, and at most one creature is fed from it",
                      fed=eaten, unfed=unfed)
    else:
        closed.expect(bool(eaten) == EATS, 'the authority feeds the orderer of a fruit only if its creature eats fruit', eaten=eaten, taken_unfed=unfed)
    # From bridge 0.0.250 a fruit is a shared entity and a client no longer runs the original's fruit update, which it
    # records. Before, each game moved its own fruit (trial378) and an eaten one was removed from the clients by name.
    shared = all(closed.events(role, 'native_plant_food_update_fenced') for role in ('player-1', 'player-2'))
    if not shared:
        for role in ('player-1', 'player-2'):
            gone = [r['native_id'] for r in closed.events(role, 'scene_plant_food_removed') if r['reason'] == 'eaten_on_the_authority']
            closed.expect(gone == eaten, f"{role}: each eaten fruit leaves this client's world too, through the original's own removal", removed=gone)

    def worlds(pick):
        """Each game's own census of its fruit at one of its samples: native number to place and state."""
        found = {}
        for role in ('authority', 'player-1', 'player-2'):
            by_sample = {}
            for r in closed.events(role, 'native_plant_food_census'):
                if r['noun_id'] == FRUIT:
                    by_sample.setdefault(r['sample'], {})[r['native_id']] = (r['position'], r.get('state'))
            found[role] = by_sample[pick(by_sample)] if by_sample else None
        return found

    def censuses(role):
        """A game's censuses of its fruit in turn: the moment of each on the machine's clock, and each fruit's place and state."""
        origin = next((r['qpc_origin'] for r in closed.rows(role) if 'qpc_origin' in r), 0)
        by_sample = {}
        for r in closed.events(role, 'native_plant_food_census'):
            if r['noun_id'] == FRUIT:
                taken = by_sample.setdefault(r['sample'], dict(at=(origin + r['qpc']) / r['qpc_frequency'], fruits={}))
                taken['fruits'][r['native_id']] = (r['position'], r.get('state'))
        return [by_sample[k] for k in sorted(by_sample)]

    def differences(held, among=None, states=False):
        reference, found = held['authority'], {}
        for role in ('player-1', 'player-2'):
            mine = held[role]
            wanted = lambda i: among is None or i in among
            found[role] = dict(
                missing=sorted(i for i in reference if i not in mine and i not in eaten and wanted(i))[:8],
                extra=sorted(i for i in mine if i not in reference and i not in eaten and wanted(i))[:8],
                elsewhere=sorted(i for i in reference if i in mine and wanted(i) and math.dist(reference[i][0], mine[i][0]) > 1.0)[:8])
            if states:
                found[role]['other_state'] = sorted(i for i in reference if i in mine and wanted(i) and reference[i][1] != mine[i][1])[:8]
        return found
    first, last = worlds(min), worlds(max)
    if all(first.values()) and all(last.values()):
        found = differences(first)
        closed.expect(all(not any(d.values()) for d in found.values()), 'at the start the three games hold the same fruits in the same places',
                      fruits={role: len(v) for role, v in first.items()}, differences=found)
        if shared:
            # Each game takes its census at its own moment, seconds from the others', and the authority's original
            # keeps moving fruit (trial419: it moved two between a client's last census and its own). A client's
            # last census that lies between two of the authority's is compared with them, for the fruit the
            # authority held still from the one to the other. A meal in between sets a quarter of the fruit in
            # motion (trial422: 41 of 157); the comparison needs half of it still.
            timed = {role: censuses(role) for role in last}
            found, compared = {}, {}
            for role in ('player-1', 'player-2'):
                pick = None
                for mine in reversed(timed[role]):
                    earlier = [a for a in timed['authority'] if a['at'] <= mine['at']]
                    later = [a for a in timed['authority'] if a['at'] >= mine['at']]
                    if earlier and later:
                        pick = (mine['fruits'], earlier[-1]['fruits'], later[0]['fruits'], [round(mine['at'] - earlier[-1]['at'], 1), round(later[0]['at'] - mine['at'], 1)])
                        break
                if pick is None:
                    found[role] = dict(no_census_between_two_of_the_authority=[True])
                    continue
                mine, a, b, apart = pick
                still = {i for i in a if i in b and a[i][1] == b[i][1] and math.dist(a[i][0], b[i][0]) <= 0.01}
                found[role] = dict(missing=sorted(i for i in still if i not in mine)[:8],
                                   extra=sorted(i for i in mine if i not in a and i not in b)[:8],
                                   elsewhere=sorted(i for i in still if i in mine and math.dist(a[i][0], mine[i][0]) > 1.0)[:8],
                                   other_state=sorted(i for i in still if i in mine and a[i][1] != mine[i][1])[:8],
                                   too_few_held_still=[len(still)] if len(still) < 0.5 * len(a) else [])
                compared[role] = dict(fruits=len(still), in_motion_on_the_authority=len(set(a) | set(b)) - len(still), seconds_after_and_before_the_authority=apart,
                                      moved_by_the_authority_since_the_start=sum(1 for i in still if i in first['authority']
                                                                                 and math.dist(first['authority'][i][0], a[i][0]) > 0.5))
            closed.expect(all(not any(d.values()) for d in found.values()),
                          'at the end every fruit lies in the same place and state in the three games, those the authority moved included',
                          fruits={role: len(v) for role, v in last.items()}, compared=compared, differences=found)
        else:
            replaced = {role: {i for i in first[role] if i in last[role] and math.dist(first[role][i][0], last[role][i][0]) > 0.5} for role in first}
            settled = {i for i in first['authority'] if not any(i in moved for moved in replaced.values())}
            found = differences(last, settled)
            closed.expect(all(not any(d.values()) for d in found.values()), 'at the end the fruits no game re-placed are still the same in the three games',
                          settled=len(settled), replaced_by_each_game={role: len(v) for role, v in replaced.items()}, differences=found)
    # Hunger and health as each game holds them for the player's creature at the end: the client's own copy feeds its HUD.
    last = {}
    for role in ('authority', 'player-1', 'player-2'):
        rows = [r for r in closed.events(role, 'actor_state') if r.get('owner') == PLAYER]
        last[role] = (rows[-1]['hunger'], rows[-1]['health']) if rows else None
    closed.expect(all(v is not None for v in last.values()) and max(v[0] for v in last.values()) - min(v[0] for v in last.values()) < 2.0
                  and max(v[1] for v in last.values()) - min(v[1] for v in last.values()) < 0.6,
                  f"player {PLAYER}'s hunger and health are the same in the three games at the end",
                  hunger={k: v and round(v[0], 1) for k, v in last.items()}, health={k: v and round(v[1], 2) for k, v in last.items()})
    # The other player's creature is fed nothing by this player's meal.
    other = [r['hunger'] for r in closed.events('authority', 'actor_state') if r.get('owner') == 3 - PLAYER]
    closed.expect(bool(other) and all(b <= a + 0.01 for a, b in zip(other, other[1:])), "the other player's hunger only falls", first=other and round(other[0], 1),
                  last=other and round(other[-1], 1))
