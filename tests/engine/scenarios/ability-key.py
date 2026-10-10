"""C01, the ability interface: a player uses the abilities of one stance on a creature, by key and by button.

From the checkpoint sealed by trial146. Every key and pointer action is an ordinary message posted to that
player's own game window (INPUT_IN_PROCESS); what the game shows is read from its own interface windows
(UI_READBACK). The player does what a person at the keyboard does:

1. walks up to the nearest creature of a species neither player belongs to and left-clicks it; the original's
   own picking must name exactly that creature;
2. confirms whatever first-contact cinematic and tutorial card the original then shows, each by its own button;
3. chooses a stance with its button and sees the number keys move to that stance's abilities;
4. uses the stance's abilities on the creature, one with its number key and one with its button (the same
   ability twice when the stance has only one).

In the social stance the player selects the creature again and jumps between the two abilities. In the attack
stance a click on a creature is itself the order to attack it, and the last attack is one the original only
starts from a distance: the player first walks away from the creature it has met, chooses the stance there,
opens with that attack's key and the pointer on the creature, uses the other attack by its button, sidesteps
during the fight and fights to the kill; its own HUD must then show the DNA the original paid.

The authority must receive each of these as that player's own command aimed at the creature and stay
connected throughout. The approach and click are those of pointer-player.py, repeated here because a scenario
is one frozen file. A restored authority runs for five minutes at most, so each player and stance is its own
run; the file as written is player 1's attack and `--variant player-2-attack` is player 2's, both in the
fixture where the two players are of different species.

The authority does not admit social abilities there (relationships between distinct species are gate C04's
open work). `player-1-social` and `player-2-social` therefore run in the fixture sealed by trial118, where
both players are of one species, and `player-1-social-distinct` and `player-2-social-distinct` record that
the original dispatches the ability and the authority answers with that refusal.
"""
import math

ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 300
PLAYER, STANCE = 1, 'attack'
# Species the player does not approach, and whether an attack run fights on to the kill.
SKIP_SPECIES, TO_THE_KILL = (), True
# C03: whether the other player's DNA, on its own HUD and on the authority, must be untouched by this player's kill.
OTHER_UNCHANGED = False
# C03: whether the player then turns to a second creature of the species it has just hunted and kills it too.
SECOND_KILL = False
# C02/C03: whether the player eats its kill before anything else, and what the authority's health and hunger do.
EAT_KILL = False
# Whether the original lets this player's creature eat meat: player 2's in the distinct-species fixture is a
# herbivore, which goes through the motions at a corpse and eats nothing (trial323).
EATS_MEAT = True
# C03: whether the other player then hunts too, in the same session: first contact, attack stance, the click that
# orders the attack, the kill and its payout, with the first hunter's DNA untouched by it.
THEN_OTHER = False
# In this checkpoint player 2 has already hunted one Blatardo. The authority admits its attack only on a species
# it has a fresh relationship with (trial261: refused, "bounded_branch" false; the open C03 item on its second
# kill), so player 2 goes to another species, and its kill and payout are left to C03.
BLATARDO = [106741522, 731352134, 1080189440]
# Whether the authority admits the stance's abilities in the fixture's configuration.
ADMITTED = True
SAME_SPECIES = {'ARTIFACT': 'local/m11-resume-2026-09-27/trial118/sealed-checkpoint/checkpoint.smp', 'AUTHORITY': {'schema': 2}}
VARIANTS = {'player-2-attack': {'PLAYER': 2, 'SKIP_SPECIES': (BLATARDO,), 'TO_THE_KILL': False},
            'player-1-kill': {'OTHER_UNCHANGED': True},
            'player-2-kill': {'PLAYER': 2, 'SKIP_SPECIES': (BLATARDO,), 'OTHER_UNCHANGED': True},
            'player-2-two-kills': {'PLAYER': 2, 'SKIP_SPECIES': (BLATARDO,), 'OTHER_UNCHANGED': True, 'SECOND_KILL': True, 'EAT_KILL': True},
            'both-kill': {'PLAYER': 2, 'SKIP_SPECIES': (BLATARDO,), 'OTHER_UNCHANGED': True, 'THEN_OTHER': True},
            'player-2-eats': {'PLAYER': 2, 'SKIP_SPECIES': (BLATARDO,), 'OTHER_UNCHANGED': True, 'EAT_KILL': True, 'EATS_MEAT': False},
            'player-1-eats': {'OTHER_UNCHANGED': True, 'EAT_KILL': True},
            # From the checkpoint trial370 sealed after player 2's meal of fruit: with 7.25 health its creature lives
            # through the fight that the fixture's 3.5 loses two times in five (trials 355, 358, 401).
            'player-2-eats-fed': {'PLAYER': 2, 'SKIP_SPECIES': (BLATARDO,), 'OTHER_UNCHANGED': True, 'EAT_KILL': True, 'EATS_MEAT': False,
                                  'ARTIFACT': 'local/native-trials/trial370/sealed-checkpoint/checkpoint.smp'},
            'player-1-social': {'STANCE': 'social', **SAME_SPECIES},
            'player-2-social': {'PLAYER': 2, 'STANCE': 'social', **SAME_SPECIES},
            'player-1-social-distinct': {'STANCE': 'social', 'ADMITTED': False},
            'player-2-social-distinct': {'PLAYER': 2, 'STANCE': 'social', 'SKIP_SPECIES': (BLATARDO,), 'ADMITTED': False}}
STEPS = 30
# Picked clicks were made from 9 to 16 units of camera distance; never from 18 (trial193).
AHEAD, ASIDE = (2.0, 6.5), 3.0
# Held W covered 14 units in 4 seconds in trial165; the other keys start slower, which the fixed 0.4 seconds allows for.
UNITS_PER_SECOND = 3.5
# A creature that moves less than this in half a second is at rest.
STILL = 0.1
NEIGHBOURS, CLEAR_PIXELS = 4, 110
HEIGHTS = (50, 20, 80)
# Thirteen strikes took the target from 7.5 to 0.5 in about eight seconds in trial231.
KILL_SECONDS = 60
# The original dispatched the last attack (Charge) from 15.7, 16.1 and 20.3 units and never from 4 to 14.8
# (trials 253 to 261).
OPENING_TRIES, OPENING_UNITS = 6, 17.0
# Identities of the original's interface windows, read in trials 242 and 244 (the same for both players).
CINEMATIC_TEXT, CINEMATIC_CONFIRM = 71682475, 71682461
TUTORIAL_CONFIRM = (4294967281, 4294967282)  # control id and command id of a tutorial card's confirm button
STANCE_BUTTON = {'social': 80091984, 'attack': 80092633}
# Ability slots are numbered by group: a stance's abilities are the slots that carry a number-key label
# (trial246: one social ability at 16850960; trial245: two attacks at 16850976 and 16850977). A third group
# in the middle of the bar, from 16850992, carries no key label in either stance and is not used here.
ABILITY_SLOTS = {'social': range(16850960, 16850976), 'attack': range(16850976, 16850992)}
KEY_LABEL = 103301336
DISPATCH = {'social': 'native_social_ui_forwarded', 'attack': 'native_combat_ui_forwarded'}
VERB = {'social': 'social', 'attack': 'combat'}


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


def centre(window):
    left, top, right, bottom = window['box']
    return round((left + right) / 2), round((top + bottom) / 2)


def by_id(windows, ident):
    return next((w for w in windows if w['id'] == ident), None)


def shown_dna(windows):
    """The DNA count the HUD shows: its text is the DNA glyph followed by the number."""
    text = next((w['text'] for w in windows if w['text'][:1] == '⚋'), None)
    return float(text[1:]) if text else None


def drive(trial):
    trial.wait_ready(stable_seconds=3)
    authority = trial.traces['authority']
    player, role, stance = PLAYER, f'player-{PLAYER}', STANCE

    def creatures():
        return [r for r in authority.entities.values()
                if r['epoch'] == trial.scene and r['event'] == 'scene_native_authority_sample' and not r['dead']]

    def owned(who):
        found = [r for r in creatures() if r.get('owner') == who]
        return found[0] if len(found) == 1 else None

    def rest(who, seconds, label):
        """Wait until a player's creature stays where it is for half a second, and return its sample.

        Judged by position: a creature standing on a slope keeps a reported speed (trial181: up to 0.1;
        trials 249 and 250: 0.22 for as long as it stood there) while it does not move at all.
        """
        for _ in range(int(seconds) * 2):
            before = owned(who)['position']
            trial.settle(0.5)
            if math.dist(owned(who)['position'], before) < STILL:
                return owned(who)
        trial.expect(False, label)
        return owned(who)

    def shown(creature):
        """Where the player's window draws a creature: its screen-point row, pointing at it on the way.

        None when that game does not have the creature at this moment (trial287: a wild creature the authority
        still listed); a creature outside the clickable view still has its row.
        """
        mark = trial.traces[role].sequence
        if trial.click(role, creature['remote_entity'], button='none', expect=None).get('result') == 'accepted':
            return trial.until(role, 'native_screen_point', where=lambda r: r['entity'] == creature['remote_entity'], after=mark, timeout=5)
        trial.settle(0.5)
        return next((r for r in trial.traces[role].retained if r['sequence'] > mark and r['event'] == 'native_screen_point'
                     and r['entity'] == creature['remote_entity']), None)

    def step(key, units, held):
        held.append(key)
        trial.hold(role, key, min(3.0, 0.4 + units / UNITS_PER_SECOND))
        rest(player, 12, f'player {player} at rest after moving')

    def select(prefer=None):
        """Walk up to a creature of another species and click it until the original picks exactly it.

        With `prefer`, that creature while it lives, else the nearest of its species: the nearest one can change
        from step to step, and trial245 spent 21 sidesteps going from one to the other.
        """
        held, misses, waits = [], [], 0
        ours = [owned(1)['species'], owned(2)['species']]
        for _ in range(STEPS):
            here = owned(player)
            others = [r for r in creatures() if not r.get('owner') and r['species'] not in ours and r['species'] not in SKIP_SPECIES
                      and (prefer is None or r['species'] == prefer['species'])]
            if not others:
                break
            same = [r for r in others if prefer and r['remote_entity'] == prefer['remote_entity']]
            target = same[0] if same else min(others, key=lambda r: math.dist(r['position'], here['position']))
            view = shown(here)
            if view is None:
                trial.settle(1)
                continue
            forward, right = camera_axes(here['position'], view['centre_ray'], view['right_ray'])
            offset = [t - h for t, h in zip(target['position'], here['position'])]
            ahead, aside = dot(offset, forward), dot(offset, right)
            if abs(aside) > ASIDE:
                step('D' if aside > 0 else 'A', abs(aside), held)
                continue
            if not AHEAD[0] <= ahead <= AHEAD[1]:
                goal = sum(AHEAD) / 2
                step('W' if ahead > goal else 'S', abs(ahead - goal), held)
                continue
            aim = shown(target)
            if aim is None:
                held.append('?')
                trial.settle(1)
                continue
            if not aim['inside']:
                step('S', 1.0, held)
                continue
            near = sorted((r for r in creatures() if not r.get('owner') and r['remote_entity'] != target['remote_entity']),
                          key=lambda r: math.dist(r['position'], here['position']))[:NEIGHBOURS]
            reach = math.dist(aim['camera'], aim['position'])
            rows = [row for row in (shown(r) for r in near) if row]
            if any(n['projected'] and math.dist(n['point'], aim['point']) < CLEAR_PIXELS and math.dist(n['camera'], n['position']) < reach
                   for n in rows):
                waits += 1
                held.append('.')
                if waits % 3:
                    trial.settle(1.5)
                else:
                    step('A' if waits % 2 else 'D', 2.0, held)
                continue
            mark = trial.traces[role].sequence
            height = HEIGHTS[len(misses) % len(HEIGHTS)]
            if trial.click(role, target['remote_entity'], expect=None, height=height).get('result') != 'accepted':
                continue
            trial.settle(1.5)
            picked = next((r for r in trial.traces[role].retained if r['sequence'] > mark and r['event'] == 'native_pickup_ui_observed'), None)
            if picked and picked['target_resolved'] and picked['target_native_id'] == aim['native_id']:
                return target, dict(keys=''.join(held), clicks=len(misses) + 1, misses=misses, picked_native_id=picked['target_native_id'],
                                    camera_distance=round(reach, 1), height_percent=height)
            misses.append('nothing' if not picked else 'another creature' if picked['target_resolved'] else 'not a creature')
            if len(misses) % len(HEIGHTS) == 0:
                step('A' if len(misses) % 2 else 'D', 2.0, held)
        return None, dict(keys=''.join(held), clicks=len(misses), misses=misses)

    def withdraw(creature, units, held):
        """Walk away from a creature until it is `units` away, and return the distance; None once it is gone.

        The camera does not turn, so the key is the one that leads away from where the creature is in the view.
        """
        for _ in range(STEPS):
            here = owned(player)
            there = [r for r in creatures() if r['remote_entity'] == creature['remote_entity']]
            if not there:
                return None
            apart = math.dist(here['position'], there[0]['position'])
            if apart >= units:
                return apart
            view = shown(here)
            if view is None:
                trial.settle(1)
                continue
            forward, right = camera_axes(here['position'], view['centre_ray'], view['right_ray'])
            offset = [t - h for t, h in zip(there[0]['position'], here['position'])]
            ahead, aside = dot(offset, forward), dot(offset, right)
            key = ('S' if ahead > 0 else 'W') if abs(ahead) >= abs(aside) else ('A' if aside > 0 else 'D')
            step(key, units + 1 - apart, held)
        return None

    def confirm_what_the_original_shows():
        """Press the confirm button of each cinematic or tutorial card the original puts up, until none is left."""
        confirmed, quiet, waited = [], 0, 0
        for _ in range(10):
            windows = trial.ui(role)
            cinematic = by_id(windows, CINEMATIC_TEXT)
            button = by_id(windows, CINEMATIC_CONFIRM) if cinematic and cinematic['text'] else None
            title = cinematic['text'][:48] if button else None
            if button is None:
                button = next((w for w in windows if (w['id'], w['command']) == TUTORIAL_CONFIRM), None)
                if button:
                    title = next((w['text'] for w in windows if w['text'] and w['parent'] == button['parent']), 'a tutorial card')[:48]
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
                trial.settle(2)
                continue
            quiet = 0
            confirmed.append(title)
            trial.click_at(role, *centre(button))
            trial.settle(2.5)
        return confirmed

    def attempt(ability, how, on, seconds=4):
        """Use one ability on the selected creature, by its number key or by its button, and say what followed."""
        client_mark, authority_mark = trial.traces[role].sequence, authority.sequence
        if how == 'key':
            # A player presses an ability key with the pointer on the creature it has just clicked.
            aimed = [r for r in creatures() if r['remote_entity'] == on['remote_entity']]
            if aimed:
                shown(aimed[0])
            trial.hold(role, ability['text'], 0.2)
        else:
            trial.click_at(role, *centre(by_id(trial.ui(role), ability['parent'])))
        trial.settle(seconds)
        mine = [r for r in trial.traces[role].retained if r['sequence'] > client_mark]
        forwarded = [r for r in mine if r['event'] == DISPATCH[stance]]
        queued = [r for r in authority.retained if r['sequence'] > authority_mark and r['event'] == 'network_actor_command_queued'
                  and r['owner'] == player and r['verb'] == VERB[stance]]
        aimed = [r for r in queued if r.get('target') and r['decision'] == 'accepted']
        return authority_mark, forwarded, queued, aimed

    def use(ability, how, on):
        """Use one ability of the stance on the selected creature and require that it becomes the player's command."""
        slot = int(ability['text']) - 1
        authority_mark, forwarded, queued, aimed = attempt(ability, how, on)
        trial.expect(bool(forwarded) and bool(aimed),
                     f"player {player}'s {stance} ability {slot + 1}, used by its {how}, reaches the authority as its own command aimed at a creature",
                     original_dispatches=[(r['ability_index'], r['sent']) for r in forwarded][:3],
                     decisions=sorted({r['decision'] for r in queued}), targets=sorted({r['target'] for r in aimed}))
        return authority_mark, (forwarded[0]['ability_index'] if forwarded else None)

    rest(1, 30, f'both creatures at rest before player {player} moves'); rest(2, 30, f'both creatures at rest before player {player} moves')
    before = shown_dna(trial.ui(role))
    other = 3 - player
    other_before = shown_dna(trial.ui(f'player-{other}')) if OTHER_UNCHANGED else None
    # The HUD shows whole DNA: 42 for the authority's 42.75 (trial256).
    trial.expect(before is not None and before == math.floor(owned(player)['native_dna']), f"player {player}'s own HUD shows its DNA",
                 shown=before, authority=owned(player)['native_dna'])
    met, evidence = select()
    trial.expect(met is not None, f'player {player} walks up to a creature of another species and its click picks exactly that creature', **evidence)
    if met is None:
        return
    trial.settle(3)
    trial.expect(True, f"player {player} confirms what the original shows on this contact", confirmed=confirm_what_the_original_shows())

    held = []
    if stance == 'attack':
        # In the attack stance a left click on a creature is itself the order to attack it (trials 263 and
        # 264: the original dispatched Bite on the click, and the creature then stayed on the player). The
        # player therefore walks away while nothing is fighting yet, and chooses the stance from there.
        apart = withdraw(met, OPENING_UNITS, held)
        trial.expect(apart is not None, f'player {player} walks away from the creature it has met before choosing to attack',
                     keys=''.join(held), units_apart=apart and round(apart, 1))
        if apart is None:
            return
    windows = trial.ui(role)
    trial.click_at(role, *centre(by_id(windows, STANCE_BUTTON[stance])))
    trial.settle(1.5)
    abilities = sorted((w for w in trial.ui(role) if w['id'] == KEY_LABEL), key=lambda w: w['text'])
    trial.expect(bool(abilities) and all(w['parent'] in ABILITY_SLOTS[stance] for w in abilities),
                 f"player {player} chooses the {stance} stance with its button and the number keys are on the {stance} abilities",
                 key_labels=[(w['text'], w['parent']) for w in abilities])
    if not abilities:
        return

    target = met
    if stance == 'social':
        target, evidence = select(prefer=met)
        trial.expect(target is not None, f'player {player} selects a creature of that species again', **evidence)
    if target is None or not [r for r in creatures() if r['remote_entity'] == target['remote_entity']]:
        return
    health = next(r for r in creatures() if r['remote_entity'] == target['remote_entity'])['health']
    if not ADMITTED:
        client_mark = trial.traces[role].sequence
        _, forwarded, queued, _ = attempt(abilities[0], 'key', target)
        answers = [r['error'] for r in trial.traces[role].retained
                   if r['sequence'] > client_mark and r['event'] == 'network_action_result' and r['error'] != 'none']
        trial.expect(bool(forwarded) and all(r['sent'] for r in forwarded) and bool(answers) and not queued,
                     f"player {player}'s {stance} ability 1, used by its key, is dispatched by the original and refused by the authority",
                     original_dispatches=[(r['ability_index'], r['sent']) for r in forwarded][:3], answers=sorted(set(answers)))
        trial.not_run(f"Player {player}'s {stance} exchange with a creature of a third species",
                      'C04: the authority does not yet admit social abilities between distinct species')
        return
    if stance == 'social':
        social_mark, first = use(abilities[0], 'key', target)
        given = [r for r in authority.retained if r['sequence'] > social_mark and r['event'] == 'network_social_native_input' and r['owner'] == player]
        trial.expect(bool(given) and all(r['queued'] for r in given),
                     f"the authority gives player {player}'s social ability to the original as that player's own input",
                     inputs=[(r['ability'], r['queued']) for r in given][:4])
        mark = authority.sequence
        trial.hold(role, ' ', 0.3)
        jump = trial.until('authority', 'network_actor_command_queued', where=lambda r: r['owner'] == player and r['verb'] == 'jump',
                           after=mark, timeout=10)
        trial.expect(jump['decision'] == 'accepted', f"player {player}'s jump key reaches the authority during the social exchange")
        trial.settle(2)
        again, evidence = select(prefer=met)
        trial.expect(again is not None, f"player {player} selects a creature of that species to use an ability by its button", **evidence)
        if again is None:
            return
        # The last ability of the stance by its button: another ability when the stance has two, else the same one.
        _, second = use(abilities[-1], 'button', again)
    else:
        # A fight ends in seconds (trial247: the target died while the player was still choosing its second
        # attack), so both attacks come before anything else.
        # The player opens with the last attack's key from where it stands, the pointer on the creature. A
        # wild creature wanders: when it has come closer the player walks away again, and when the key
        # brought nothing it sidesteps for another line of sight. The distances are recorded.
        tried, authority_mark, second = [], authority.sequence, None
        for number in range(OPENING_TRIES):
            apart = withdraw(target, OPENING_UNITS, held)
            if apart is None:
                break
            authority_mark, forwarded, queued, aimed = attempt(abilities[-1], 'key', target, seconds=1.5)
            tried.append((round(apart, 1), bool(forwarded), bool(aimed)))
            if forwarded and aimed:
                second = forwarded[0]['ability_index']
                break
            step('A' if number % 2 else 'D', 2.0, held)
        trial.expect(second is not None,
                     f"player {player}'s attack ability {abilities[-1]['text']}, used by its key, reaches the authority as its own command aimed at a creature",
                     units_apart_dispatched_accepted=tried, keys=''.join(held))
        if second is None:
            return
        # Wild creatures strike each other too; the strike wanted is this player's own.
        strike = trial.until('authority', 'native_strike_enter', where=lambda r: r['owner'] == player, after=authority_mark, timeout=25)
        _, first = use(abilities[0], 'button', target)
        now = [r for r in authority.entities.values() if r.get('remote_entity') == target['remote_entity']]
        trial.expect((now[0]['health'] if now else 0.0) < health, f"the authority runs player {player}'s original attack and the target loses health",
                     health_before=health, health_after=now[0]['health'] if now else 0.0)
        # Movement during the fight: a sidestep must reach the authority as steering while the attack is on.
        place, mark = owned(player)['position'], authority.sequence
        trial.hold(role, 'A', 1.0)
        steers = [r for r in authority.retained if r['sequence'] > mark and r['event'] == 'network_actor_command_queued'
                  and r['owner'] == player and r['verb'] == 'steer' and r['decision'] == 'accepted']
        rest(player, 12, f'player {player} at rest after sidestepping')
        moved = math.dist(owned(player)['position'], place)
        # The creature it is fighting can stand in the way: 0.42 units in trial322.
        trial.expect(bool(steers) and moved > 0.25, f"player {player} sidesteps with its own key during the fight", steers=len(steers), units=round(moved, 2))
    trial.expect(first is not None and second is not None and (len(abilities) == 1) == (first == second),
                 f"player {player}'s {stance} abilities are the ones their slots stand for",
                 abilities=len(abilities), ability_indexes=[first, second])

    if stance == 'attack' and not TO_THE_KILL:
        trial.not_run(f"Player {player}'s kill and its payout", 'C03: the authority does not yet admit every kill of the second player')
    if stance == 'attack' and TO_THE_KILL:
        # The attack goes on by itself until one of the two is dead. The original then pays out the kill;
        # trial233 lost the session at that moment.
        def struck_by_me(row):
            return row['actor'] in {s['target'] for s in authority.retained if s['event'] == 'native_strike_enter' and s['owner'] == player}
        # A sidestep ends the attack order, as it does in the original; attack again to finish the fight.
        # Only while that creature lives: once it is dead the same selection would pick another of its species
        # (trial321).
        if any(r['remote_entity'] == target['remote_entity'] for r in creatures()):
            mark = authority.sequence
            again, evidence = select(prefer=met)
            # In the attack stance that click is already the order to attack; the key is pressed only when the
            # authority has not taken one from it (trials 321 and 326: the key brought nothing after such a click).
            ordered = [r for r in authority.retained if r['sequence'] > mark and r['event'] == 'network_actor_command_queued'
                       and r['owner'] == player and r['verb'] == 'combat' and r['decision'] == 'accepted']
            if again is not None and again['remote_entity'] == target['remote_entity'] and not ordered:
                use(abilities[0], 'key', again)
        killed = trial.until('authority', 'damage_state', where=lambda r: r['health'] <= 0 and struck_by_me(r),
                             after=strike['sequence'], timeout=KILL_SECONDS)
        trial.settle(5)
        paid = [r for r in authority.retained if r['sequence'] > killed['sequence'] and r['event'] == 'native_global_dna_return']
        trial.expect(True, f"player {player} confirms what the original shows after the kill", confirmed=confirm_what_the_original_shows())
        after = shown_dna(trial.ui(role))
        trial.expect(after is not None and after > before and after == math.floor(owned(player)['native_dna']),
                     f"player {player}'s creature kills the target and its own HUD shows the DNA the original paid",
                     shown_before=before, shown_after=after, authority=owned(player)['native_dna'], payouts=[r['after'] for r in paid])
        if OTHER_UNCHANGED:
            other_after = shown_dna(trial.ui(f'player-{other}'))
            trial.expect(other_before is not None and other_after == other_before == math.floor(owned(other)['native_dna']),
                         f"player {other}'s DNA is untouched by player {player}'s kill, on its own HUD and on the authority",
                         shown_before=other_before, shown_after=other_after, authority=owned(other)['native_dna'])
        if EAT_KILL:
            # A left click on the corpse is the order to eat it. The corpse lies down: aim low on it.
            mine, mark = owned(player), authority.sequence
            # Two heights reached player 1's kills; player 2's clicks at them picked nothing in trial388. Further
            # heights are tried until the authority has the order.
            answers = []
            sent = lambda: [r for r in authority.retained if r['sequence'] > mark and r['event'] == 'network_actor_command_queued'
                            and r['owner'] == player and r['verb'] == 'pickup']
            for view in range(2):
                if view and not sent():
                    # Six clicks that pick nothing (trials 479 and 491: the same corpse, seen from above and behind
                    # the player's own creature): the player steps back and aside for another view of it.
                    trial.hold(role, 'S', 0.7)
                    trial.hold(role, 'D', 0.5)
                    trial.settle(0.8)
                for height in (20, 50, 10, 35, 65, 5):
                    if len(answers) >= 2 and sent():
                        break
                    answers.append(trial.click(role, target['remote_entity'], expect=None, height=height).get('result'))
                    trial.settle(1.5)
            trial.settle(18)
            orders = sent()
            fed = owned(player)
            better = fed['health'] > mine['health'] or fed['hunger'] > mine['hunger']
            trial.expect(any(r['decision'] == 'accepted' for r in orders) and better == EATS_MEAT,
                         f"player {player} eats its kill by a click on it, and the authority's creature is the better for it" if EATS_MEAT else
                         f"player {player}'s order to eat its kill reaches the authority, and the original lets its diet eat none of it",
                         clicks=answers, orders=[r['decision'] for r in orders][:4], health=[mine['health'], fed['health']],
                         hunger=[round(mine['hunger'], 1), round(fed['hunger'], 1)])
        if SECOND_KILL:
            # The attack stance is still chosen: the click on the next creature is the order to attack it, and the
            # first attack's key repeats it.
            mark = authority.sequence
            next_one, evidence = select(prefer=met)
            trial.expect(next_one is not None and next_one['remote_entity'] != target['remote_entity'],
                         f'player {player} turns to another creature of the species it has just hunted', **evidence)
            if next_one is None:
                return
            use(abilities[0], 'key', next_one)
            second = trial.until('authority', 'damage_state', after=mark, timeout=KILL_SECONDS,
                                 where=lambda r: r['health'] <= 0 and r['actor'] != killed['actor'] and struck_by_me(r))
            trial.settle(5)
            trial.expect(True, f'player {player} confirms what the original shows after its second kill', confirmed=confirm_what_the_original_shows())
            again_dna = shown_dna(trial.ui(role))
            trial.expect(again_dna is not None and again_dna > after and again_dna == math.floor(owned(player)['native_dna']),
                         f"player {player}'s second kill of that species is paid too, and its own HUD shows it",
                         shown_after_first=after, shown_after_second=again_dna, authority=owned(player)['native_dna'], killed=second['actor'])
            if OTHER_UNCHANGED:
                other_again = shown_dna(trial.ui(f'player-{other}'))
                trial.expect(other_again == other_before == math.floor(owned(other)['native_dna']),
                             f"player {other}'s DNA is untouched by player {player}'s second kill", shown=other_again, authority=owned(other)['native_dna'])
    if stance == 'attack' and TO_THE_KILL and THEN_OTHER:
        # The helpers above act for whoever `player` and `role` name: from here on, the other player.
        first_hunter, first_dna = player, shown_dna(trial.ui(role))
        player, role = other, f'player-{other}'
        mine_before = shown_dna(trial.ui(role))
        prey, evidence = select()
        trial.expect(prey is not None, f'player {player} then walks up to a creature of another species and picks it', **evidence)
        if prey is None:
            return
        trial.settle(3)
        trial.expect(True, f'player {player} confirms what the original shows on this contact', confirmed=confirm_what_the_original_shows())
        trial.click_at(role, *centre(by_id(trial.ui(role), STANCE_BUTTON['attack'])))
        trial.settle(1.5)
        mark = authority.sequence
        again, evidence = select(prefer=prey)
        trial.expect(again is not None, f'player {player} orders its attack by a click in the attack stance', **evidence)
        if again is None:
            return
        orders = [r for r in authority.retained if r['sequence'] > mark and r['event'] == 'network_actor_command_queued'
                  and r['owner'] == player and r['verb'] == 'combat']
        trial.expect(any(r['decision'] == 'accepted' for r in orders), f"the authority accepts player {player}'s attack after player {first_hunter}'s fight is over",
                     decisions=[r['decision'] for r in orders][:4])
        trial.until('authority', 'damage_state', where=lambda r: r['health'] <= 0 and struck_by_me(r), after=mark, timeout=KILL_SECONDS)
        trial.settle(5)
        trial.expect(True, f'player {player} confirms what the original shows after its kill', confirmed=confirm_what_the_original_shows())
        mine_after, first_after = shown_dna(trial.ui(role)), shown_dna(trial.ui(f'player-{first_hunter}'))
        trial.expect(mine_after is not None and mine_after > mine_before and mine_after == math.floor(owned(player)['native_dna']),
                     f"player {player}'s kill is paid to it, and its own HUD shows it", shown_before=mine_before, shown_after=mine_after,
                     authority=owned(player)['native_dna'])
        trial.expect(first_after == first_dna == math.floor(owned(first_hunter)['native_dna']),
                     f"player {first_hunter}'s DNA is untouched by player {player}'s kill, on its own HUD and on the authority",
                     shown_before=first_dna, shown_after=first_after, authority=owned(first_hunter)['native_dna'])
    trial.not_run('Right click, and clicks on nests, fossils and food', 'the gates that own those interactions (C02, C05, C08)')
    trial.settle(2)


def review(closed):
    role = f'player-{PLAYER}'
    posted = closed.events(role, 'network_pointer_posted') + closed.events(role, 'network_point_posted')
    closed.expect(any(p['action'] == 2 for p in posted) and all(p['posted'] and p['input_class'] == 'INPUT_IN_PROCESS' for p in posted),
                  f'player {PLAYER}: pointer messages posted in its own process', posted=len(posted))
    points = [(p['point'], p['clickable']) for p in closed.events(role, 'native_screen_point') if p['inside']]
    closed.expect(bool(points) and all(left <= x <= right and top <= y <= bottom for (x, y), (left, top, right, bottom) in points),
                  f'player {PLAYER}: every accepted creature point lies in the part of its view the HUD leaves free', accepted=len(points))
    readings = closed.events(role, 'native_ui_snapshot')
    closed.expect(bool(readings) and all(r['evidence_label'] == 'UI_READBACK' and not r['truncated'] for r in readings),
                  f'player {PLAYER}: its interface was read whole each time', readings=len(readings))
    # C03: an attack the authority ran for this player recharges on its own ability bar (the yellow wipe of the
    # original; up to bridge 0.0.237 the owner's client drew none).
    used = sorted({r['ability_index'] for r in closed.events('authority', 'native_combat_input') if r['owner'] == PLAYER})
    drawn = [r for r in closed.events(role, 'scene_recharge_presented') if r['owner'] == PLAYER]
    begun = sorted({r['ability_index'] for r in drawn if r['recharging']})
    if used:
        closed.expect(all(index in begun for index in used) and any(not r['recharging'] for r in drawn),
                      f"player {PLAYER}: each attack it used recharges on its own client's ability bar, and a recharge ends there",
                      used=used, begun=begun, ended=sorted({r['ability_index'] for r in drawn if not r['recharging']}), rows=len(drawn))
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
    for other in ('authority', 'player-1', 'player-2'):
        closed.expect_none(other, 'native_relationship_acceptance_failed')
        closed.expect_none(other, 'native_unowned_movement_unstoppable')
