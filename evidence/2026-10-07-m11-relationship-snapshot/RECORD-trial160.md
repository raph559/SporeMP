# trial160 - relationship-snapshot.py

**Outcome: PASS** (58 of 58 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input DIAGNOSTIC_NETWORK_INTENT; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `71368dafb5b8a6dfe411d12ff58055e34f38a8bc`; 15 frozen files
- player-1: bridge 0.0.189, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 2925 trace rows, trace SHA-256 `5d0f265f46390a2d086d70830155ad314a053529f5ae6b235a48f6ad28b96cd2`
- player-2: bridge 0.0.189, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 3174 trace rows, trace SHA-256 `3f1be34adf2bab140676a1b0e74a20bbb9b5b3a15c688b52c491cf0b3d59a207`
- authority: bridge 0.0.189, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 3109 trace rows, trace SHA-256 `69b749e7e0b15b4cff8d2ae6ba545800beaa2b8dc51ac6dbdf0d8076c7b8d21d`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 4.9 |
| freeze | 0.1 |
| display | 0.1 |
| server | 0.4 |
| workers | 4.4 |
| park | 7.8 |
| captures | 0.0 |
| drive | 39.7 |
| close | 5.1 |
| server_stop | 0.0 |
| captures_finish | 0.6 |
| display_release | 0.0 |
| fixture_undo | 2.0 |
| review | 0.1 |
| total | 65.1 |

## Passed checks (58)

- authority publishes B's one relationship as revision 1
- B's client applies the same snapshot
- the snapshot equals what B's client adopted from the checkpoint
- B still shows Hunt 1/3
- player 1 keeps 14 DNA and is alive
- player 2 keeps 7 DNA and is alive
- closure clean
- player-1: trace starts and stops healthy
- player-1: one contiguous single-thread trace
- player-1: no network error or trace limit
- player-1: game and worker exit 0
- player-1: personal files unchanged
- player-1: capture finished normally
- player-2: trace starts and stops healthy
- player-2: one contiguous single-thread trace
- player-2: no network error or trace limit
- player-2: game and worker exit 0
- player-2: personal files unchanged
- player-2: capture finished normally
- authority: trace starts and stops healthy
- authority: one contiguous single-thread trace
- authority: no network error or trace limit
- authority: game and worker exit 0
- authority: personal files unchanged
- authority: capture finished normally
- all peers ran one bridge, SDK and executable
- frozen sources and binaries match their hashes
- every game window opened on the parking display
- the bridge redirected the centering request in every game
- disposable profiles restored to their originals
- an unchanged snapshot is published as one revision
- repeated republications are applied once
- B's client holds exactly the published values, unchanged from its checkpoint
- authority: no network_relationship_applied
- authority: no native_relationship_snapshot_applied
- player-1: no network_relationship_applied
- player-1: no native_relationship_snapshot_applied
- authority: no network_relationship_unpublishable
- authority: no native_global_dna_enter
- authority: no native_award_return
- authority: no native_history_event_return
- authority: no native_first_dna_tutorial_deferred
- authority: no native_relationship_acceptance_failed
- authority: no relationship_binding_rejected
- player-1: no native_global_dna_enter
- player-1: no native_award_return
- player-1: no native_history_event_return
- player-1: no native_first_dna_tutorial_deferred
- player-1: no native_relationship_acceptance_failed
- player-1: no relationship_binding_rejected
- player-2: no native_global_dna_enter
- player-2: no native_award_return
- player-2: no native_history_event_return
- player-2: no native_first_dna_tutorial_deferred
- player-2: no native_relationship_acceptance_failed
- player-2: no relationship_binding_rejected
- player 1 ends alive with 14 DNA
- player 2 ends alive with 7 DNA

Observation (ACCEPTED): A starting game was the foreground window when it appeared: player-1, player-2, authority. Known and left as is: happens when the user has been idle.

## Not covered

- A relationship changing during play: NOT_RUN; prerequisite: the authority admitting B's second kill and a multi-entry history
- Physical input: NOT_RUN; prerequisite: not part of this saved-state check

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: True
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen True (user idle 185.8 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen True (user idle 187.3 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 188.6 s)

