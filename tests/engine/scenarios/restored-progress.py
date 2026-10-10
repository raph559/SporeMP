"""Unequal progress survives a fresh three-process restore.

Runner form of trial147 (.181): restore the checkpoint sealed by trial146 and
require both clients to join with A at 14 DNA and B at 7, the full 679-row
player/inventory census loaded identically everywhere, B's one-entry history
adopted, and no reward, history or tutorial replay. Whole-primary history
preservation is not claimed: fresh loads see 6 categories / 5 entries.
"""
import collections
import json

ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 150
ROLES = ('authority', 'player-1', 'player-2')
ENVELOPE = {'schema_version', 'evidence_class', 'harness', 'sequence', 'pid', 'thread_id', 'qpc', 'qpc_frequency',
            'epoch', 'executing_command', 'foreign_callbacks', 'ability_scope', 'strike_scope',
            'animal_damage_scope', 'pickup_scope', 'request', 'phase'}
CENSUS = {'native_checkpoint_player', 'native_checkpoint_unlocked_item', 'native_checkpoint_item_status'}
REPLAYS = ('native_global_dna_enter', 'native_award_return', 'native_history_event_return',
           'native_primary_history_event_return', 'native_first_dna_tutorial_deferred')


def census(rows, phase):
    return collections.Counter(json.dumps({k: v for k, v in r.items() if k not in ENVELOPE}, sort_keys=True)
                               for r in rows if r['event'] in CENSUS and r.get('phase') == phase and r.get('request') == 1)


def drive(trial):
    trial.wait_ready(stable_seconds=3)
    for player, dna in ((1, 14), (2, 7)):
        def own():
            found = [r for r in trial.traces[f'player-{player}'].entities.values() if r.get('owner') == player]
            return found[0] if len(found) == 1 else None
        view = trial.wait(own, 30, f'player {player} sees its own creature')
        trial.expect(view['native_dna'] == dna and not view['dead'], f'player {player} restored alive with {dna} DNA',
                     native_dna=view['native_dna'])
    trial.not_run("A's normal jump and landing", 'desktop or in-process input')
    trial.not_run('Whole-primary history preservation', 'observation at the original serialization point')
    trial.settle(10)


def review(closed):
    loaded = {role: census(closed.rows(role), 'loaded') for role in ROLES}
    closed.expect(all(sum(c.values()) == 679 for c in loaded.values()) and loaded['authority'] == loaded['player-1']
                  == loaded['player-2'], 'all three peers load the same 679 player/inventory rows',
                  rows={r: sum(c.values()) for r, c in loaded.items()})
    for role in ROLES:
        restored, adopted, nouns = (closed.one(role, e) for e in (
            'native_checkpoint_extension_restored', 'native_player_adopted', 'checkpoint_restore'))
        closed.expect(restored and restored['success'] and adopted and adopted['created_players'] == 0
                      and nouns and nouns['created_nouns'] == 0, f'{role}: saved pair adopted, nothing created')
        history, read = closed.one(role, 'native_history_checkpoint_adopted'), closed.one(role, 'native_history_checkpoint_read')
        closed.expect(history and history['entries'] == 1 and read and read['exact_state'],
                      f"{role}: B's one-entry history adopted exactly")
        closed.expect(history and history['primary_categories'] == 6 and history['primary_entries'] == 5,
                      f'{role}: primary history loads as 6 categories / 5 entries (known limit)')
        for event in REPLAYS:
            closed.expect_none(role, event)
    for player, dna in ((1, 14), (2, 7)):
        views = [r for r in closed.events(f'player-{player}', 'scene_native_replica_sample') if r['owner'] == player]
        closed.expect(views and views[-1]['native_dna'] == dna and not views[-1]['dead'],
                      f'player {player} ends alive with {dna} DNA')
