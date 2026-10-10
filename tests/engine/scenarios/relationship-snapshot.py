"""Owner B's species relationships reach B's client as a complete live snapshot.

From the checkpoint sealed by trial146: the authority publishes B's private
relationship values, only B's client receives and applies them, and they equal
what that client had already adopted from the checkpoint, so nothing changes,
no reward runs and B's Hunt 1/3 card stays as restored. This qualifies the
channel only. A relationship that changes during play needs a second kill by B,
which the authority does not admit yet.
"""
ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 150
ROLES = ('authority', 'player-1', 'player-2')
REPLAYS = ('native_global_dna_enter', 'native_award_return', 'native_history_event_return',
           'native_first_dna_tutorial_deferred')


def drive(trial):
    trial.wait_ready(stable_seconds=3)
    published = trial.until('authority', 'network_relationship_published', timeout=30)
    trial.expect(published['player'] == 2 and published['revision'] == 1 and published['entries'] == 1
                 and published['full_snapshot'], "authority publishes B's one relationship as revision 1")
    applied = trial.until('player-2', 'network_relationship_applied', timeout=30)
    trial.expect(applied['player'] == 2 and applied['entity'] == published['entity']
                 and applied['generation'] == published['generation'] and applied['revision'] == 1
                 and applied['entries'] == 1 and not applied['reward_executed'],
                 "B's client applies the same snapshot")
    trial.expect(applied['changed'] is False, "the snapshot equals what B's client adopted from the checkpoint")
    card = trial.until('player-2', 'native_relationship_goal_presented', timeout=30)
    trial.expect(card['after'] == [0, 1, 1] and not card['reward_executed'], 'B still shows Hunt 1/3', after=card['after'])
    for player, dna in ((1, 14), (2, 7)):
        def own():
            found = [r for r in trial.traces[f'player-{player}'].entities.values() if r.get('owner') == player]
            return found[0] if len(found) == 1 else None
        view = trial.wait(own, 30, f'player {player} sees its own creature')
        trial.expect(view['native_dna'] == dna and not view['dead'], f'player {player} keeps {dna} DNA and is alive')
    trial.not_run('A relationship changing during play', "the authority admitting B's second kill and a multi-entry history")
    trial.not_run('Physical input', 'not part of this saved-state check')
    # Several one-second republications must not be applied again.
    trial.settle(8)


def review(closed):
    published = closed.events('authority', 'network_relationship_published')
    closed.expect(len(published) == 1, 'an unchanged snapshot is published as one revision', published=len(published))
    applied = closed.events('player-2', 'network_relationship_applied')
    closed.expect(len(applied) == 1, 'repeated republications are applied once', applied=len(applied))
    native = closed.one('player-2', 'native_relationship_snapshot_applied')
    copied = closed.events('player-2', 'native_relationship_goal_display_copied')
    closed.expect(native and native['entries'] == 1 and native['exact'] and not native['changed']
                  and native['replayed_actions'] == 0 and copied and copied[0]['hash'] == native['hash'],
                  "B's client holds exactly the published values, unchanged from its checkpoint")
    for role in ('authority', 'player-1'):
        for event in ('network_relationship_applied', 'native_relationship_snapshot_applied'):
            closed.expect_none(role, event)
    closed.expect_none('authority', 'network_relationship_unpublishable')
    for role in ROLES:
        for event in REPLAYS + ('native_relationship_acceptance_failed', 'relationship_binding_rejected'):
            closed.expect_none(role, event)
    for player, dna in ((1, 14), (2, 7)):
        views = [r for r in closed.events(f'player-{player}', 'scene_native_replica_sample') if r['owner'] == player]
        closed.expect(views and views[-1]['native_dna'] == dna and not views[-1]['dead'],
                      f'player {player} ends alive with {dna} DNA')
