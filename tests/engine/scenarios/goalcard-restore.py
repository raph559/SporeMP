"""Owner B sees its own Creature goal card after a fresh distinct-species restore.

Runner form of trial149 (.183): restore sealed146, join both clients, and require
B's client alone to copy, present and refresh B's Hunt 1/3 goal while A keeps 14 DNA
and B keeps 7. Saved state only; no input is sent and no reward may run.
"""
ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 150
SPECIES = [106741522, 731352134, 1080189440]
GOAL_EVENTS = ('native_relationship_goal_display_copied', 'native_relationship_goal_presented',
               'native_relationship_goal_display_refreshed')
REPLAY_EVENTS = ('native_global_dna_enter', 'native_award_return', 'native_history_event_return',
                 'native_first_dna_tutorial_deferred')


def owned(trial, player):
    return [r for r in trial.traces[f'player-{player}'].entities.values() if r.get('owner') == player]


def drive(trial):
    trial.wait_ready(stable_seconds=3)
    copied = trial.until('player-2', GOAL_EVENTS[0], timeout=60)
    shown = trial.until('player-2', GOAL_EVENTS[1], timeout=30)
    refreshed = trial.until('player-2', GOAL_EVENTS[2], where=lambda r: r.get('cards_populated', 0) == 1, timeout=30)
    trial.expect(copied['owner'] == 2 and copied['entries'] == 1 and copied['exact'] and copied['source_unchanged'],
                 "B's relationship copy is exact and leaves the source unchanged")
    trial.expect(shown['species'] == SPECIES and shown['entry_present'] and shown['before'] == [0, 2, 1]
                 and shown['after'] == [0, 1, 1] and shown['extinction_threshold'] == 3 and shown['exact']
                 and not shown['reward_executed'], 'B goal evaluates to Hunt 1/3 with no reward', after=shown['after'])
    trial.expect(refreshed['goals'] == 1 and refreshed['refreshed'] == 1 and refreshed['presented_total'] == 1,
                 "B's original goal card is populated once")
    for player, dna in ((1, 14), (2, 7)):
        trial.wait(lambda: len(owned(trial, player)) == 1, 20, f'player {player} sees its own creature')
        view = owned(trial, player)[0]
        trial.expect(view['native_dna'] == dna and not view['dead'], f'player {player} keeps {dna} DNA and is alive',
                     native_dna=view['native_dna'])
    trial.not_run('Live relationship change reaching the owner client', 'authority-to-client relationship replication')
    trial.not_run('Physical input', 'not part of this saved-state check')
    trial.settle(10)  # leave both cards on screen for the captures


def review(closed):
    for role in ('authority', 'player-1'):
        for event in GOAL_EVENTS + ('replica_social_readout',):
            closed.expect_none(role, event)
    for role in ('authority', 'player-1', 'player-2'):
        restored, adopted, nouns = (closed.one(role, e) for e in (
            'native_checkpoint_extension_restored', 'native_player_adopted', 'checkpoint_restore'))
        closed.expect(restored and restored['success'] and adopted and adopted['created_players'] == 0
                      and nouns and nouns['created_nouns'] == 0, f'{role}: saved pair adopted, nothing created')
        for event in REPLAY_EVENTS + ('native_relationship_acceptance_failed', 'relationship_binding_rejected'):
            closed.expect_none(role, event)
    copied, shown, refreshed, readout = (closed.one('player-2', e) for e in GOAL_EVENTS + ('replica_social_readout',))
    connected = [r for r in closed.events('player-2', 'network_status') if r['state'] == 'connected']
    closed.expect(all((copied, shown, refreshed, readout)) and len(connected) == 1
                  and connected[0]['qpc'] < copied['qpc'] <= shown['qpc'] <= readout['qpc'] <= refreshed['qpc'],
                  'B connects, then copies, presents, reads out and refreshes in order')
    closed.expect(readout and readout['award_unchanged'], "B's award state is unchanged by the readout")
    closed.expect(refreshed and refreshed.get('cards_populated', 0) == 1, "B's original card text was repopulated")
