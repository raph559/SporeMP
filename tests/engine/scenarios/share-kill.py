"""C02: two players of one species at one corpse.

From the one-species checkpoint (trial118): both players' creatures are carnivores, 118 units apart; player 1 has
no hunger left and loses health, so it hunts first.

1. player 1 walks to a weak creature of another species, orders its attack by a click and kills it;
2. player 1 orders its creature to eat the corpse by a click on it;
3. player 2 walks over with its movement keys and orders its own creature to the same corpse;
4. what each creature's hunger gains is recorded against what the corpse's food loses.

Every key and click is posted in that player's own game (INPUT_IN_PROCESS). Hunger and the corpse's food are the
authority's own values (NATIVE state). In this run the two meals follow one another: player 1 starves before
player 2 can arrive.

`together` starts from the checkpoint the `saved` run sealed, in which the two creatures stand side by side and
neither starves: player 1 kills a creature nearby, player 2 steps up to the corpse, and each player orders its own
creature to it within a second of the other. The original keeps one claimant on a corpse: the first eats, the
other is fed nothing meanwhile, then orders again and eats what is left.
"""
import math

ARTIFACT = 'local/m11-resume-2026-09-27/trial118/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 2}
DEADLINE_SECONDS = 300
BLATARDO = [106741522, 731352134, 1080189440]
STEPS = 30
AHEAD, ASIDE = (2.0, 6.5), 3.0
UNITS_PER_SECOND = 3.5
HEIGHTS = (50, 20, 80)
KILL_SECONDS = 45
# Player 2's bite takes 1 health (trial329). Player 1 leaves the creature with this much: at 1 it ran from both
# players and player 2 never reached it (trial334).
NEARLY_DEAD = 2.0
# Player 1 stops attacking once the creature has this much health or less, so that the last blow is player 2's.
# Its blows take 3 health from the Blatardo (trial337: 7.5, 4.5, 1.5), which at 1 health or less runs from both
# players (trial334): it stops after its first. They take 1 from the pack's alpha (trial342), which it fights
# down to 3. Up to bridge 0.0.234 the authority cancelled player 1's attack when player 2 joined.
PLAYER_1_STOPS = 5.0
NEARBY = 7.0
# The species hunted, whether its alpha is the target, and the kills each player has of it beforehand.
SPECIES, COUNTS = BLATARDO, {1: 2, 2: 1}
# Whether the original Save is requested at the end, with the two players side by side; run with `--seal`.
SAVE = False
# A creature player 1 can kill before it starves: the fixture has two with 3 health 31 to 36 units from it.
WEAK = 3.5
# Heights on a lying corpse to click, in turn, until the authority has the order to eat.
CORPSE_HEIGHTS = (20, 50, 10, 35, 65, 5)
MEAL_SECONDS = 25
# A hungry creature's meal at a corpse took 7 seconds (trial433).
FIRST_MEAL_SECONDS = 11
# Whether both players are ordered to the corpse at once, and how far apart the players may stand at the start.
TOGETHER, TOGETHER_APART = False, 20.0
VARIANTS = {'saved': {'SAVE': True},
            # The checkpoint trial427 sealed from `saved`.
            # The nearest creature of another species, of 6 health, stands 12 units away; the nearest of 3 stands 87
            # units away among others that killed player 1 while player 2 walked over (trial428).
            'together': {'ARTIFACT': 'local/native-trials/trial427/sealed-checkpoint/checkpoint.smp', 'TOGETHER': True, 'WEAK': 6.5}}
# The pack that lives between the two players (trial334: three steps for player 1); the other packs cost a
# minute of walking, which player 2's creature does not have (trial335).
PACK = [106739832, 731352134, 1080189440]
CINEMATIC_TEXT, CINEMATIC_CONFIRM = 71682475, 71682461
TUTORIAL_CONFIRM = (4294967281, 4294967282)
ATTACK_STANCE = 80092633
# The health number of the HUD: this window identity is also used over other creatures' heads, so the one in the
# HUD's corner is taken (trial363).
HEALTH_TEXT, HUD_CORNER = 80755112, (1150, 500)


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


def centre(window):
    left, top, right, bottom = window['box']
    return round((left + right) / 2), round((top + bottom) / 2)


def by_id(windows, ident):
    return next((w for w in windows if w['id'] == ident), None)


def drive(trial):
    trial.wait_ready(stable_seconds=3)
    authority = trial.traces['authority']

    def role(player):
        return f'player-{player}'

    def samples():
        return [r for r in authority.entities.values() if r['epoch'] == trial.scene and r['event'] == 'scene_native_authority_sample']

    def living():
        return [r for r in samples() if not r['dead']]

    def owned(player):
        found = [r for r in living() if r.get('owner') == player]
        return found[0] if len(found) == 1 else None

    def by_native(native_id):
        return next((r for r in samples() if r['native_id'] == native_id), None)

    def dna(player):
        text = next((w['text'] for w in trial.ui(role(player)) if w['text'][:1] == '⚋'), None)
        return float(text[1:]) if text else None

    def shown(player, creature):
        mark = trial.traces[role(player)].sequence
        if trial.click(role(player), creature['remote_entity'], button='none', expect=None).get('result') == 'accepted':
            return trial.until(role(player), 'native_screen_point', where=lambda r: r['entity'] == creature['remote_entity'], after=mark, timeout=5)
        trial.settle(0.5)
        return next((r for r in trial.traces[role(player)].retained if r['sequence'] > mark and r['event'] == 'native_screen_point'
                     and r['entity'] == creature['remote_entity']), None)

    def step(player, key, units, held):
        held.append(key)
        trial.hold(role(player), key, min(4.0, 0.4 + units / UNITS_PER_SECOND))
        trial.settle(0.8)

    def click_on(player, choose):
        """Walk up to the creature `choose()` names and click it until the original picks exactly it."""
        held, misses = [], []
        for _ in range(STEPS):
            here, target = owned(player), choose()
            if here is None or target is None:
                break
            view = shown(player, here)
            if view is None:
                trial.settle(1)
                continue
            forward, right = camera_axes(here['position'], view['centre_ray'], view['right_ray'])
            offset = [t - h for t, h in zip(target['position'], here['position'])]
            ahead, aside = dot(offset, forward), dot(offset, right)
            if abs(aside) > ASIDE:
                step(player, 'D' if aside > 0 else 'A', abs(aside), held)
                continue
            if not AHEAD[0] <= ahead <= AHEAD[1]:
                goal = sum(AHEAD) / 2
                step(player, 'W' if ahead > goal else 'S', abs(ahead - goal), held)
                continue
            aim = shown(player, target)
            if aim is None or not aim['inside']:
                step(player, 'S', 1.0, held)
                continue
            mark = trial.traces[role(player)].sequence
            height = HEIGHTS[len(misses) % len(HEIGHTS)]
            if trial.click(role(player), target['remote_entity'], expect=None, height=height).get('result') != 'accepted':
                continue
            trial.settle(1.5)
            picked = next((r for r in trial.traces[role(player)].retained if r['sequence'] > mark and r['event'] == 'native_pickup_ui_observed'), None)
            if picked and picked['target_resolved'] and picked['target_native_id'] == target['native_id']:
                return target, dict(keys=''.join(held), clicks=len(misses) + 1, picked_native_id=target['native_id'])
            misses.append(1)
            # A click that lands on another creature can open a card or a cinematic of the original, which then
            # takes every later click (trial344: player 1 met player 2's species on its way to the pack).
            confirm(player)
            if len(misses) % len(HEIGHTS) == 0:
                step(player, 'A' if len(misses) % 2 else 'D', 2.0, held)
        return None, dict(keys=''.join(held), clicks=len(misses))

    def confirm(player):
        confirmed, quiet, waited = [], 0, 0
        for _ in range(8):
            windows = trial.ui(role(player))
            cinematic = by_id(windows, CINEMATIC_TEXT)
            button = by_id(windows, CINEMATIC_CONFIRM) if cinematic and cinematic['text'] else None
            title = cinematic['text'][:40] if button else None
            if button is None:
                button = next((w for w in windows if (w['id'], w['command']) == TUTORIAL_CONFIRM), None)
                title = 'a tutorial card' if button else None
            if button is None and cinematic and cinematic['text'] and waited < 6:
                # A cinematic shows its text before its button (trial480: taken for quiet, it stayed open and the
                # stance button behind it was not there to click).
                waited += 1
                trial.settle(1.0)
                continue
            if button is None:
                quiet += 1
                if quiet >= 2:
                    break
                trial.settle(1.0)
                continue
            quiet = 0
            confirmed.append(title)
            trial.click_at(role(player), *centre(button))
            trial.settle(2.5)
        return confirmed

    def walk_near(player, position, within):
        """Walk with the movement keys until the player's creature is `within` units of a place."""
        held = []
        for _ in range(STEPS):
            here = owned(player)
            offset = [t - h for t, h in zip(position, here['position'])]
            apart = math.sqrt(dot(offset, offset))
            if apart <= within:
                break
            view = shown(player, here)
            if view is None:
                trial.settle(1)
                continue
            forward, right = camera_axes(here['position'], view['centre_ray'], view['right_ray'])
            ahead, aside = dot(offset, forward), dot(offset, right)
            key, units = (('W' if ahead > 0 else 'S'), abs(ahead)) if abs(ahead) >= abs(aside) else (('D' if aside > 0 else 'A'), abs(aside))
            step(player, key, max(1.0, units - within / 2), held)
        return ''.join(held)

    def attack_stance(player):
        trial.click_at(role(player), *centre(by_id(trial.ui(role(player)), ATTACK_STANCE)))
        trial.settle(1.5)

    def orders(player, after):
        return [r for r in authority.retained if r['sequence'] > after and r['event'] == 'network_actor_command_queued'
                and r['owner'] == player and r['verb'] == 'combat']

    def attack(player, choose):
        """Order an attack on a creature by clicking it in the attack stance.

        The first click on a creature the player has not selected only selects it (trial332); the next one is
        the order.
        """
        mark, evidence = authority.sequence, {}
        for clicks in (1, 2, 3):
            picked, evidence = click_on(player, choose)
            if picked is None:
                break
            if any(r['decision'] == 'accepted' for r in orders(player, mark)):
                return mark, dict(evidence, clicks_on_it=clicks)
        return None, evidence

    def hud_health(player):
        """The health number on that player's own HUD (UI_READBACK), or None."""
        found = [w['text'] for w in trial.ui(role(player)) if w['id'] == HEALTH_TEXT and w['text'].isdigit()
                 and w['box'][0] >= HUD_CORNER[0] and w['box'][1] >= HUD_CORNER[1]]
        return int(found[0]) if len(found) == 1 else None

    def death_of(native_id, after, timeout):
        return trial.until('authority', 'damage_state', where=lambda r: r['native_id'] == native_id and r['health'] <= 0, after=after, timeout=timeout)



    ours = owned(1)['species']

    def weak_prey():
        me = owned(1)
        found = [r for r in living() if not r.get('owner') and r['species'] != ours and r['health'] <= WEAK]
        return min(found, key=lambda r: math.dist(r['position'], me['position']), default=None)
    if TOGETHER:
        apart = owned(1) and owned(2) and math.dist(owned(1)['position'], owned(2)['position'])
        trial.expect(apart is not None and apart <= TOGETHER_APART and owned(1)['species'] == owned(2)['species'],
                     'the two players, of one species, stand together at the start', apart=apart and round(apart, 1),
                     player_1=dict(health=owned(1)['health'], hunger=round(owned(1)['hunger'], 1)),
                     player_2=dict(health=owned(2)['health'], hunger=round(owned(2)['hunger'], 1)))
    target = weak_prey()
    trial.expect(target is not None, 'a weak creature of another species stands near player 1',
                 apart=target and round(math.dist(target['position'], owned(1)['position']), 1), health=target and target['health'],
                 players_apart=round(math.dist(owned(1)['position'], owned(2)['position'])),
                 player_1=dict(health=owned(1)['health'], hunger=round(owned(1)['hunger'], 1)),
                 player_2=dict(health=owned(2)['health'], hunger=round(owned(2)['hunger'], 1)))
    chosen = lambda: next((r for r in living() if r['native_id'] == target['native_id']), None)

    # 1. Player 1 kills it.
    picked, evidence = click_on(1, chosen)
    trial.expect(picked is not None, 'player 1 walks up to it and its click picks it', **evidence)
    trial.settle(1.5)
    confirm(1)
    attack_stance(1)
    mark, evidence = attack(1, chosen)
    trial.expect(mark is not None, 'player 1 orders its attack by a click and the authority accepts it', **evidence)
    killed = death_of(target['native_id'], mark, KILL_SECONDS)
    trial.settle(3)
    confirm(1)
    corpse = by_native(target['native_id'])
    trial.expect(killed['last_attacker'] == 1 and corpse['dead'], 'player 1 kills it', food=corpse.get('native_food'), player_1_health=owned(1)['health'])

    def order(player):
        """Click the corpse in that player's own view until the authority has its order to eat."""
        mark, clicks, stepped = authority.sequence, 0, ''
        for height in CORPSE_HEIGHTS:
            seen = trial.traces[role(player)].sequence
            trial.click(role(player), corpse['remote_entity'], expect=None, height=height)
            clicks += 1
            trial.settle(1.5)
            # A creature standing over a small corpse is what the pointer hits (trial406: six clicks picked player
            # 1's own creature): the player steps back off it.
            picked = [r['target_native_id'] for r in trial.traces[role(player)].retained
                      if r['sequence'] > seen and r['event'] == 'native_pickup_ui_observed' and r['target_resolved']]
            if picked and picked[-1] != target['native_id']:
                stepped += 'S'
                trial.hold(role(player), 'S', 0.7)
                trial.settle(0.8)
            elif not picked:
                # Clicks that pick nothing at all (trial447: six on the corpse at the creature's feet in tall
                # grass; its frames show no card open). A card of the original is confirmed if one is open
                # (trial344), and the player steps back and aside for another view of the corpse.
                confirm(player)
                stepped += 'SD'
                trial.hold(role(player), 'S', 0.7)
                trial.hold(role(player), 'D', 0.5)
                trial.settle(0.8)
            decisions = [r['decision'] for r in authority.retained if r['sequence'] > mark and r['event'] == 'network_actor_command_queued'
                         and r['owner'] == player and r['verb'] == 'pickup']
            if 'accepted' in decisions:
                return dict(ordered=True, clicks=clicks, decisions=decisions, stepped_back=stepped)
        return dict(ordered=False, clicks=clicks, decisions=decisions, stepped_back=stepped)

    state = lambda: {p: dict(hunger=round(owned(p)['hunger'], 1), health=owned(p)['health']) for p in (1, 2) if owned(p)}
    if TOGETHER:
        # 2. Player 2 steps up to the corpse, and each player orders its own creature to it, turn about, until the
        #    authority has both orders.
        # Player 1's creature stands over its kill and is what a click there picks, in its own view and in player
        # 2's (trial431: player 2's six clicks picked it): it steps back off the corpse first, and aside, since
        # straight back the corpse stays behind its own body in its own view (trial437: ten clicks picked it).
        trial.hold(role(1), 'S', 1.0)
        trial.hold(role(1), 'D', 0.7)
        trial.settle(0.8)
        keys = walk_near(2, corpse['position'], 5.0)
        apart = {p: round(math.dist(owned(p)['position'], corpse['position']), 1) for p in (1, 2)}
        trial.expect(max(apart.values()) <= 12, 'both creatures stand at the corpse', keys=keys, apart=apart)
        before, food = state(), [by_native(target['native_id']).get('native_food')]
        mark = authority.sequence
        # Player 1, hurt in its fight, clicks first: its meal heals it before the dead creature's companion comes
        # back for it (trial435: ordered second, it stood by for the first meal and was killed before its own).
        given = {p: dict(ordered=False, clicks=0, stepped_aside='') for p in (1, 2)}

        def taken(player):
            return [r for r in authority.retained if r['sequence'] > mark and r['event'] == 'network_actor_command_queued'
                    and r['owner'] == player and r['verb'] == 'pickup' and r['decision'] == 'accepted' and r.get('target_native_id') == target['native_id']]
        for aside in ('', 'D', 'D'):
            for player in (1, 2):
                if aside and not given[player]['ordered']:
                    given[player]['stepped_aside'] += aside
                    trial.hold(role(player), aside, 0.7)
                    trial.settle(0.8)
            for height in CORPSE_HEIGHTS:
                for player in (1, 2):
                    if not given[player]['ordered']:
                        trial.click(role(player), corpse['remote_entity'], expect=None, height=height)
                        given[player]['clicks'] += 1
                trial.settle(0.8)
                for player in (1, 2):
                    accepted = taken(player)
                    if accepted and not given[player]['ordered']:
                        given[player].update(ordered=True, at=round(accepted[0]['qpc'] / accepted[0]['qpc_frequency'], 2))
                if all(v['ordered'] for v in given.values()):
                    break
            if all(v['ordered'] for v in given.values()):
                break
        both = all(v['ordered'] for v in given.values())
        trial.expect(both, 'each player orders its own creature to the corpse by a click in its own game, and the authority takes both orders',
                     seconds_between_the_orders=both and round(abs(given[1]['at'] - given[2]['at']), 2), player_1=given[1], player_2=given[2])
        # 3. The original keeps one claimant on a corpse (M07): the creature whose order came first eats, and the
        #    other's order ends there. The bridge keeps the authority's own avatar from taking the claim over.
        first = min(given, key=lambda p: given[p]['at'])
        second = 3 - first
        trial.settle(FIRST_MEAL_SECONDS)
        between = state()
        food.append(by_native(target['native_id']) and by_native(target['native_id']).get('native_food'))
        honoured = [r for r in authority.retained if r['sequence'] > mark and r['event'] == 'native_pickup_existing_claim_honored']
        trial.expect(between[first]['hunger'] > before[first]['hunger'] and between[second]['hunger'] <= before[second]['hunger'] and food[1] < food[0],
                     'the creature whose order came first eats; the other, ordered within a second of it, is fed nothing while it does',
                     first=first, hunger_before=before, hunger_after_the_first_meal=between, corpse_food=food,
                     claim_kept_for_the_first=[dict(of=r.get('claimant_owner'), against=r.get('owner')) for r in honoured][:3])
        # 4. The first steps off the corpse, where it is what a click picks, and the other player orders again
        #    and eats what is left.
        trial.hold(role(first), 'S', 1.0)
        trial.hold(role(first), 'D', 0.7)
        trial.settle(0.8)
        again = order(second) if food[1] is not None and food[1] > 0 else dict(ordered=False, clicks=0, decisions=[], stepped_back='')
        trial.settle(FIRST_MEAL_SECONDS)
        after = state()
        food.append(by_native(target['native_id']) and by_native(target['native_id']).get('native_food'))
        if food[1] is not None and food[1] > 0:
            trial.expect(again['ordered'] and after[second]['hunger'] > between[second]['hunger'] and food[2] < food[1],
                         'the first having eaten, the other player orders again and its creature is fed from what is left',
                         second=second, hunger=[between[second]['hunger'], after[second]['hunger']], health=[between[second]['health'], after[second]['health']],
                         corpse_food=food, **again)
        else:
            trial.expect(not again['ordered'] and after[second]['hunger'] <= between[second]['hunger'],
                         'the first emptied the corpse and the other is fed nothing from it', second=second, corpse_food=food)
        shown = {p: hud_health(p) for p in (1, 2)}
        trial.expect(all(shown[p] == math.ceil(after[p]['health']) for p in (1, 2)), "each player's own HUD shows its creature's health after the meals",
                     shown=shown, authority={p: after[p]['health'] for p in (1, 2)})
        trial.settle(2)
        return

    # 2. Player 1 eats: it was starving.
    before = state()
    food = [corpse.get('native_food')]
    first = order(1)
    trial.settle(MEAL_SECONDS)
    between = state()
    food.append(by_native(target['native_id']) and by_native(target['native_id']).get('native_food'))
    trial.expect(first['ordered'] and between[1]['hunger'] > before[1]['hunger'], 'player 1 orders its creature to the corpse by a click and it is fed',
                 hunger=[before[1]['hunger'], between[1]['hunger']], health=[before[1]['health'], between[1]['health']], corpse_food=food, **first)

    # 3. Player 2 comes over and orders its own creature to the same corpse.
    keys = walk_near(2, corpse['position'], 6.0)
    apart = math.dist(owned(2)['position'], corpse['position'])
    trial.expect(apart <= 12, 'player 2 walks to the corpse with its movement keys', keys=keys, apart=round(apart, 1))
    arrived = state()
    food.append(by_native(target['native_id']) and by_native(target['native_id']).get('native_food'))
    second = order(2)
    trial.settle(MEAL_SECONDS)
    after = state()
    food.append(by_native(target['native_id']) and by_native(target['native_id']).get('native_food'))

    # 4. What each gained against what the corpse lost. The original gave a starving player 1 the whole of a small
    #    corpse and more (trial407: food 50 to -40), which leaves the second player nothing to be ordered to.
    left = food[2]
    fed = after[2]['hunger'] > arrived[2]['hunger']
    if left is not None and left > 0:
        trial.expect(second['ordered'] and fed, "player 2's order to the same corpse reaches the authority and its creature is fed from what player 1 left",
                     player_2_hunger=[arrived[2]['hunger'], after[2]['hunger']], player_1_hunger=[between[1]['hunger'], arrived[1]['hunger'], after[1]['hunger']],
                     corpse_food=food, **second)
    else:
        trial.expect(not fed and not second['ordered'], 'player 1 emptied the corpse, and player 2, arriving after, is fed nothing from it and no order is taken',
                     player_2_hunger=[arrived[2]['hunger'], after[2]['hunger']], player_1_hunger=[between[1]['hunger'], arrived[1]['hunger'], after[1]['hunger']],
                     corpse_food=food, **second)
    trial.not_run('Two creatures eating one corpse at the same moment', 'this run has the meals follow one another; player 1 starves before player 2 can arrive')
    if SAVE:
        mark = authority.sequence
        trial.control('authority', 'save', trial.scene)
        saved = trial.until('authority', 'native_checkpoint_save_complete', after=mark, timeout=30)
        trial.expect(saved['success'] and not saved['diagnostic_only'], 'the original Save completes with the two players side by side',
                     apart=round(math.dist(owned(1)['position'], owned(2)['position']), 1), **{f'player_{p}': v for p, v in state().items()})
    trial.settle(2)


def review(closed):
    for role in ('authority', 'player-1', 'player-2'):
        closed.expect_none(role, 'network_rejected')
    # Each game holds the same hunger for the two players' creatures at the end. A client's state rows number the
    # owners and the creatures its own way in this fixture (trial407); its own creature is the one it marks as avatar.
    last = {}
    for role, own in (('authority', None), ('player-1', 1), ('player-2', 2)):
        held = {}
        for r in closed.events(role, 'actor_state'):
            if r.get('owner'):
                player = r['owner'] if own is None else own if r['avatar'] else 3 - own
                held[player] = r['hunger']
        last[role] = held
    reference = last['authority']
    closed.expect(len(reference) == 2 and all(set(v) == {1, 2} and all(abs(v[p] - reference[p]) < 2.0 for p in (1, 2)) for v in last.values()),
                  "each player's hunger is the same in the three games at the end",
                  hunger={role: {str(p): round(h, 1) for p, h in v.items()} for role, v in last.items()})
    if TOGETHER:
        # The moments at which the authority's original raised each player's hunger: one meal, then the other.
        rose = {1: [], 2: []}
        held = {}
        for r in closed.events('authority', 'scene_native_authority_sample'):
            player = r.get('owner')
            if player in rose:
                if player in held and r['hunger'] > held[player] + 0.5:
                    rose[player].append(round(r['qpc'] / r['qpc_frequency'], 2))
                held[player] = r['hunger']
        spans = {p: (v[0], v[-1]) for p, v in rose.items() if v}
        closed.expect(len(spans) == 2 and min(b for _, b in spans.values()) < max(a for a, _ in spans.values()),
                      'the two creatures, ordered at the same moment, eat one after the other: the times over which each is fed do not overlap',
                      fed_from_to={str(p): v for p, v in spans.items()}, bites={str(p): len(v) for p, v in rose.items()})
        # The corpse they ate, by the identity the three games share, and the food each game holds for it at the end.
        dead = [r['native_id'] for r in closed.events('authority', 'damage_state') if r['health'] <= 0]
        entity, food = None, {}
        for role, event in (('authority', 'scene_native_authority_sample'), ('player-1', 'scene_native_replica_sample'), ('player-2', 'scene_native_replica_sample')):
            for r in closed.events(role, event):
                if role == 'authority' and dead and r['native_id'] == dead[-1]:
                    entity = r['remote_entity']
                if entity is not None and r['remote_entity'] == entity:
                    food[role] = r['native_food']
        closed.expect(len(food) == 3 and max(food.values()) - min(food.values()) < 1.0, "the corpse's food is the same in the three games at the end",
                      food={k: round(v, 1) for k, v in food.items()})
