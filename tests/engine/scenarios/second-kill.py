"""C03: player 2's second kill of a species it has already hunted, finishing a creature player 1 has wounded.

From the checkpoint sealed by trial146, in which player 2 has killed one Blatardo and player 1 two; three make
that species extinct for a player. The one Blatardo in the scene is its pack's alpha. Player 2's creature is a
wounded herbivore that a fight of its own would kill, and it starves within three minutes (trials 318 to 336),
so the two players hunt together, as two players would:

1. player 2 walks to the Blatardo, selects it by a click and chooses the attack stance;
2. player 1 meets it and orders its attack by a click;
3. once player 1 has struck, player 2 joins with its first attack's key; player 1 breaks off with a movement
   key before a blow of its own could be the last, and player 2 kills the creature.

A wounded creature left alone by its attacker walks off, faster than player 2 can follow (trials 334 to 337),
which is why player 2 is already fighting when player 1 leaves.

Every key and click is posted in that player's own game (INPUT_IN_PROCESS) and DNA is read from each HUD
(UI_READBACK). The kill must be paid to player 2, on its HUD and on the authority, and nothing to player 1. The
authority must count it as player 2's second Blatardo and leave player 1's count at two.

`--variant first-kill-shared` is the same hunt on the alpha of a pack neither player has hunted: player 2's
first kill of a species, made while player 1 fights the same creature. Up to bridge 0.0.233 the authority
refused player 2 both an alpha and a species player 1 had touched.

`--variant first-kill-saved` is that hunt followed by the original Save; run with `--seal`, the runner seals
the closed save as a checkpoint. `--variant restored` starts from that checkpoint and checks what player 2's
kill left: each HUD's DNA, the history and relationships the authority reads back, and, by one attack order of
player 2 on another member of the pack, that the authority counts one kill of that species for it.
"""
import math

ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
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
# Whether the original Save is requested after the kill, and what a session restored from that save must show.
SAVE, RESTORED = False, None
VARIANTS = {'first-kill-shared': {'SPECIES': [106739832, 731352134, 1080189440], 'COUNTS': {1: 0, 2: 0}, 'PLAYER_1_STOPS': 3.0},
            'first-kill-saved': {'SPECIES': [106739832, 731352134, 1080189440], 'COUNTS': {1: 0, 2: 0}, 'PLAYER_1_STOPS': 3.0,
                                 'SAVE': True},
            # The checkpoint trial345 sealed from `first-kill-saved`, and what that trial's save held: the DNA on
            # each HUD, the number and digest of player 2's relationships as written, its history entries.
            'restored': {'ARTIFACT': 'local/native-trials/trial345/sealed-checkpoint/checkpoint.smp',
                         'SPECIES': [106739832, 731352134, 1080189440], 'COUNTS': {1: 0, 2: 1},
                         'RESTORED': {'dna': {1: 14.0, 2: 15.0}, 'relationships': [2, 3192634933270002723], 'history': 2}}}
# The pack that lives between the two players (trial334: three steps for player 1); the other packs cost a
# minute of walking, which player 2's creature does not have (trial335).
PACK = [106739832, 731352134, 1080189440]
CINEMATIC_TEXT, CINEMATIC_CONFIRM = 71682475, 71682461
TUTORIAL_CONFIRM = (4294967281, 4294967282)
ATTACK_STANCE = 80092633


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

    def death_of(native_id, after, timeout):
        return trial.until('authority', 'damage_state', where=lambda r: r['native_id'] == native_id and r['health'] <= 0, after=after, timeout=timeout)


    if RESTORED:
        return restored(trial, authority, role, living, owned, by_native, dna, shown, click_on, confirm, attack_stance, orders)
    blatardo = lambda: next((r for r in living() if r['species'] == SPECIES and not r.get('owner') and r['source_alpha']), None)
    target = blatardo()
    trial.expect(target is not None, "the pack's alpha is in the scene", species=SPECIES[0])
    before = {p: dna(p) for p in (1, 2)}
    trial.expect(all(before[p] == math.floor(owned(p)['native_dna']) for p in (1, 2)), "each player's HUD shows its DNA", shown=before)

    # 1. Player 2 comes over, selects it and chooses the attack stance: its next click on it will be the order.
    keys = walk_near(2, target['position'], NEARBY)
    picked, evidence = click_on(2, blatardo)
    trial.expect(picked is not None, 'player 2 walks to the Blatardo and its click selects it', walked=keys, **evidence)
    trial.settle(1.5)
    confirm(2)
    attack_stance(2)

    # 2. Player 1 wounds it and breaks off.
    picked, evidence = click_on(1, blatardo)
    trial.expect(picked is not None, 'player 1 walks up to it and its click picks it', **evidence)
    trial.settle(2)
    confirm(1)
    attack_stance(1)
    mark, evidence = attack(1, blatardo)
    trial.expect(mark is not None, 'player 1 orders its attack by a click and the authority accepts it', **evidence)
    full, between = target['health'], before
    trial.until('authority', 'native_strike_enter', where=lambda r: r['owner'] == 1, after=mark, timeout=30)

    # 3. Player 2 joins by its attack key, pointer on the creature; player 1 stops with a movement key once the
    #    creature is weak enough, at once if its first blow did that; player 2 kills it.
    joined = authority.sequence
    weak = lambda: by_native(target['native_id'])['health'] <= PLAYER_1_STOPS
    stopped = weak()
    if stopped:
        trial.hold(role(1), 'S', 0.6)
    if blatardo() is not None:
        shown(2, blatardo())
    trial.hold(role(2), '1', 0.2)
    if not stopped:
        trial.wait(weak, 40, 'the creature weakened by both players')
        trial.hold(role(1), 'S', 0.6)
    wounded = by_native(target['native_id'])
    trial.expect(any(r['decision'] == 'accepted' for r in orders(2, joined)),
                 "player 2 joins the fight by its attack key and the authority accepts it; player 1 breaks off with a movement key",
                 health=[full, wounded['health']], player_1_health=owned(1) and owned(1)['health'], player_2_health=owned(2) and owned(2)['health'])
    killed = death_of(target['native_id'], joined, KILL_SECONDS)
    trial.settle(4)
    confirm(2)
    last = {p: dna(p) for p in (1, 2)}
    trial.expect(killed['last_attacker'] == 2 and last[2] > between[2] and last[2] == math.floor(owned(2)['native_dna']),
                 "player 2's second kill of the species is paid to it, and its own HUD shows it", before=between, after=last, health=owned(2) and owned(2)['health'])
    trial.expect(last[1] == before[1] == math.floor(owned(1)['native_dna']), 'player 1 is paid nothing for the blows it struck',
                 shown=[before[1], last[1]], authority=owned(1)['native_dna'])
    trial.not_run("The kill that makes a species extinct for player 2, and its hunt goal's reward", "C06: the second player's own goals")
    trial.settle(2)
    if SAVE:
        mark = authority.sequence
        trial.control('authority', 'save', trial.scene)
        saved = trial.until('authority', 'native_checkpoint_save_complete', after=mark, timeout=30)
        trial.expect(saved['success'] and not saved['diagnostic_only'], "the original Save completes after player 2's kill",
                     dna={p: owned(p)['native_dna'] for p in (1, 2)}, player_2_health=owned(2)['health'], player_2_hunger=round(owned(2)['hunger'], 1))
        trial.settle(2)


def restored(trial, authority, role, living, owned, by_native, dna, shown, click_on, confirm, attack_stance, orders):
    """From the save made after player 2's first kill of the pack: what that kill left, and one order on the pack."""
    seen = {p: dna(p) for p in (1, 2)}
    trial.expect(seen == RESTORED['dna'] and all(seen[p] == math.floor(owned(p)['native_dna']) for p in (1, 2)),
                 "each player's HUD shows the DNA it had when the world was saved", shown=seen, saved=RESTORED['dna'],
                 player_2_health=owned(2)['health'], player_2_hunger=round(owned(2)['hunger'], 1))

    def apart(creature):
        offset = [a - b for a, b in zip(creature['position'], owned(2)['position'])]
        return dot(offset, offset)
    pack = lambda: [r for r in living() if r['species'] == SPECIES and not r.get('owner')]
    nearest = lambda: min(pack(), key=apart, default=None)
    picked, evidence = click_on(2, nearest)
    trial.expect(picked is not None, "player 2's click selects a living member of the pack it hunted", pack=len(pack()), **evidence)
    if picked is None:
        return
    trial.settle(1.5)
    confirm(2)
    attack_stance(2)
    chosen = lambda: next((r for r in living() if r['native_id'] == picked['native_id']), None)
    mark = authority.sequence
    for _ in range(3):
        if chosen() is None or any(r['decision'] == 'accepted' for r in orders(2, mark)):
            break
        shown(2, chosen())
        trial.hold(role(2), '1', 0.2)
        trial.settle(1.5)
    state = next((r for r in authority.retained if r['sequence'] > mark and r['event'] == 'native_relationship_state'
                  and r['phase'] == 'preflight'), None)
    trial.expect(state is not None and state['species'] == SPECIES and state['b_words'][3] == COUNTS[2] and state['a_words'][3] == COUNTS[1],
                 "player 2 orders an attack by its key and the authority holds its one kill of that species, and none for player 1",
                 orders=[r['decision'] for r in orders(2, mark)], later_kill=state and state['later_kill'],
                 player_2=state and state['b_words'], player_1=state and state['a_words'])
    # Its creature is starving and wounded: it leaves the fight at once rather than die in it (C10).
    trial.hold(role(2), 'S', 1.5)
    trial.not_run("Player 2's next kill from the restored state", 'its creature starves within a minute of the restore; it needs food (C02)')
    trial.settle(1)


def review(closed):
    # C03, costs: the original charges an ability's energy when the ability ends (static C19900). For every attack
    # these creatures have it charges nothing, to players as to wild creatures (trials 345, 350, 351), and each game
    # holds the same energy for the player.
    for player in (1, 2):
        charged = [r['amount'] for r in closed.events('authority', 'native_energy') if r['owner'] == player]
        held = {who: sorted({r['energy'] for r in closed.events(who, 'actor_state') if r.get('owner') == player})
                for who in ('authority', 'player-1', 'player-2')}
        if charged:
            closed.expect(not any(charged) and len({tuple(v) for v in held.values()}) == 1 and len(held['authority']) == 1,
                          f"player {player}: the original charges its attacks nothing, and every game holds the same energy for it",
                          charges=len(charged), charged=sorted(set(charged)), energy=held)
    cancelled = [r['reason'] for r in closed.events('authority', 'intention_cancelled') if r['owner'] == 1]
    closed.expect('foreign_combat_preflight' not in cancelled, "player 1's attack is not cancelled by player 2's fight", cancelled=cancelled)
    for role in ('authority', 'player-1', 'player-2'):
        closed.expect_none(role, 'native_relationship_acceptance_failed')
        closed.expect_none(role, 'native_history_acceptance_failed')
        closed.expect_none(role, 'network_rejected')
    for player in (1, 2):
        readings = closed.events(f'player-{player}', 'native_ui_snapshot')
        closed.expect(bool(readings) and all(r['evidence_label'] == 'UI_READBACK' and not r['truncated'] for r in readings),
                      f'player {player}: its interface was read whole each time', readings=len(readings))
    if RESTORED:
        relationships = closed.one('authority', 'native_relationship_checkpoint_read')
        history = closed.one('authority', 'native_history_checkpoint_read')
        closed.expect(bool(relationships) and relationships['success'] and relationships['exact_state']
                      and [relationships['entries'], relationships['hash']] == RESTORED['relationships'],
                      "the authority reads back player 2's relationships exactly as they were written",
                      read=relationships and [relationships['entries'], relationships['hash']], written=RESTORED['relationships'])
        closed.expect(bool(history) and history['success'] and history['exact_state'] and history['entries'] == RESTORED['history'],
                      "the authority reads back player 2's history with both of its first kills", entries=history and history['entries'])
        return
    if SAVE:
        relationships = closed.one('authority', 'native_relationship_checkpoint_written')
        history = closed.one('authority', 'native_history_checkpoint_written')
        closed.expect(bool(relationships) and relationships['success'] and relationships['primary_unchanged']
                      and bool(history) and history['success'] and history['primary_unchanged'] and history['entries'] == 2,
                      "the save holds player 2's relationships and its history of two first kills, player 1's untouched",
                      relationships=relationships and [relationships['entries'], relationships['hash']], history=history and history['entries'])
    states = closed.events('authority', 'native_relationship_state')
    kills = [r for r in states if r['phase'] == 'action_enter' and r['action'] == 0]
    after = [r for r in states if r['phase'] == 'action_return' and r['action'] == 0]
    closed.expect(len(kills) == 1 and kills[0]['later_kill'] == bool(COUNTS[2]) and kills[0]['b_words'][3] == COUNTS[2] and
                  [r['b_words'][3] for r in after] == [COUNTS[2] + 1] and all(r['a_words'][3] == COUNTS[1] for r in kills + after),
                  "the authority counts the kill as player 2's next of the species and leaves player 1's count as it was",
                  later=[r['later_kill'] for r in kills], player_2_counts=[r['b_words'][3] for r in kills + after],
                  player_1_counts=[r['a_words'][3] for r in kills + after])
