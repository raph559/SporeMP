"""C01, the ability interface: the abilities of the bar's middle group, which carry no number key.

From the checkpoint sealed by trial146. The bar's middle group holds the abilities that are neither social
nor attacks: for player 1 Jump and Mating Call, for player 2 Mating Call (trial268: the creatures' own
ability lists). Each player left-clicks each button of the group once (INPUT_IN_PROCESS at the button's own
place, read from the interface, UI_READBACK).

The Jump button must do what the Space key does: the authority receives that player's own jump, the
original jumps the creature there, and it lands. In trial274, before bridge 0.0.222, the original jumped
the client's own copy of the creature and the authority heard nothing. Every other button of the group
stays with the original on that client: the click must be named in the client's trace and must send the
authority nothing. What Mating Call then does belongs to mating (C09).
"""

ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 180
NEUTRAL_SLOTS = range(16850992, 16851008)
JUMP, MATING_CALL = 17, 16
# Pinned ability types of the two creatures in this checkpoint that have a button in the middle group.
EXPECTED = {1: [MATING_CALL, JUMP], 2: [MATING_CALL]}
SETTLE_SECONDS = 4


def centre(window):
    left, top, right, bottom = window['box']
    return round((left + right) / 2), round((top + bottom) / 2)


def drive(trial):
    trial.wait_ready(stable_seconds=3)
    authority = trial.traces['authority']

    def owned(player):
        found = [r for r in authority.entities.values() if r['epoch'] == trial.scene and r.get('owner') == player]
        return found[0] if len(found) == 1 else None

    trial.wait(lambda: all(owned(p) and owned(p)['velocity'] == [0, 0, 0] for p in (1, 2)), 30, 'both creatures at rest')
    for player in (1, 2):
        role, seen = f'player-{player}', []
        slots = sorted((w for w in trial.ui(role) if w['id'] in NEUTRAL_SLOTS), key=lambda w: w['id'])
        trial.expect(bool(slots), f"player {player}'s bar shows its middle group", slots=[(w['id'], w['box']) for w in slots])
        for slot in slots:
            client_mark, authority_mark = trial.traces[role].sequence, authority.sequence
            trial.click_at(role, *centre(slot))
            trial.settle(SETTLE_SECONDS)
            mine = [r for r in trial.traces[role].retained if r['sequence'] > client_mark]
            jumps = [r for r in mine if r['event'] == 'native_jump_ui_forwarded']
            local = [r for r in mine if r['event'] == 'native_ability_ui_local']
            commands = [r for r in authority.retained if r['sequence'] > authority_mark
                        and r['event'] == 'network_actor_command_queued' and r['owner'] == player]
            if jumps:
                seen.append(JUMP)
                jump = trial.until('authority', 'native_jump_return', where=lambda r: r['actor'] == player, after=authority_mark, timeout=10)
                trial.until('authority', 'native_landing', where=lambda r: r['owner'] == player, after=authority_mark, timeout=20)
                trial.expect(all(r['sent'] for r in jumps) and [(r['verb'], r['decision']) for r in commands] == [('jump', 'accepted')] * len(jumps)
                             and jump['accepted'] and not local,
                             f"player {player}'s Jump button reaches the authority as its own jump, and its creature jumps and lands",
                             button=slot['id'], dispatches=len(jumps), commands=[(r['verb'], r['decision']) for r in commands])
            elif not local:
                # A place of the group that holds no ability of this creature (trial277: player 2, which has no Jump).
                trial.expect(not commands, f"player {player}'s middle button {slot['id']} holds no ability: nothing is dispatched or sent")
            else:
                seen.extend(r['ability_type'] for r in local)
                trial.expect(len(local) == 1 and not commands,
                             f"player {player}'s middle button {slot['id']} stays with the original on its own game and sends the authority nothing",
                             abilities=[(r['ability_index'], r['ability_type']) for r in local], commands=[(r['verb'], r['decision']) for r in commands])
        trial.expect(sorted(seen) == sorted(EXPECTED[player]), f"player {player}'s middle buttons are its creature's own abilities of that group",
                     ability_types=sorted(seen), expected=sorted(EXPECTED[player]), buttons=len(slots))
    trial.not_run('What Mating Call does once the original has it', 'C09: mating')
    trial.settle(2)


def review(closed):
    for player in (1, 2):
        role = f'player-{player}'
        posted = closed.events(role, 'network_point_posted')
        closed.expect(bool(posted) and all(p['posted'] and p['input_class'] == 'INPUT_IN_PROCESS' for p in posted),
                      f'player {player}: pointer messages posted in its own process', posted=len(posted))
        readings = closed.events(role, 'native_ui_snapshot')
        closed.expect(bool(readings) and all(r['evidence_label'] == 'UI_READBACK' and not r['truncated'] for r in readings),
                      f'player {player}: its interface was read whole', readings=len(readings))
    for role in ('authority', 'player-1', 'player-2'):
        closed.expect_none(role, 'network_rejected')
        closed.expect_none(role, 'native_unowned_movement_unstoppable')
