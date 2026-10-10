# trial306 - ability-key.py[player-2-social]

**Outcome: PASS** (47 of 47 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `10044f9aae7fdcc2b04d15d02a67e52fac9a9aea`; 15 frozen files
- player-1: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 14293 trace rows, trace SHA-256 `3490a883b2374be00bf382969257db883417002ea85d470bf18c2fa48d28c662`
- player-2: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 16949 trace rows, trace SHA-256 `3abb48b8d04732e128811030e572e38d31ee7e8492e543b8c4a2d74afdd35263`
- authority: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 52967 trace rows, trace SHA-256 `b5a893a9c1e239219d7df69b6c4f7779db6aa9d397d6b3c32806055b63725691`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.3 |
| fixture_restore | 19.9 |
| freeze | 0.2 |
| display | 0.1 |
| server | 0.8 |
| workers | 13.6 |
| park | 10.4 |
| captures | 0.0 |
| drive | 150.7 |
| close | 12.5 |
| server_stop | 0.0 |
| captures_finish | 2.2 |
| display_release | 0.0 |
| fixture_undo | 8.6 |
| review | 2.6 |
| total | 222.0 |

## Passed checks (47)

- player 2's own HUD shows its DNA
- player 2 walks up to a creature of another species and its click picks exactly that creature
- player 2 confirms what the original shows on this contact
- player 2 chooses the social stance with its button and the number keys are on the social abilities
- player 2 selects a creature of that species again
- player 2's social ability 1, used by its key, reaches the authority as its own command aimed at a creature
- the authority gives player 2's social ability to the original as that player's own input
- player 2's jump key reaches the authority during the social exchange
- player 2 selects a creature of that species to use an ability by its button
- player 2's social ability 1, used by its button, reaches the authority as its own command aimed at a creature
- player 2's social abilities are the ones their slots stand for
- closure clean
- player-1: trace starts and stops healthy
- player-1: one contiguous single-thread trace
- player-1: no network error or trace limit
- player-1: game and worker exit 0
- player-1: personal files unchanged
- player-1: capture finished normally
- player-1: capture shows a picture
- player-2: trace starts and stops healthy
- player-2: one contiguous single-thread trace
- player-2: no network error or trace limit
- player-2: game and worker exit 0
- player-2: personal files unchanged
- player-2: capture finished normally
- player-2: capture shows a picture
- authority: trace starts and stops healthy
- authority: one contiguous single-thread trace
- authority: no network error or trace limit
- authority: game and worker exit 0
- authority: personal files unchanged
- authority: capture finished normally
- authority: capture shows a picture
- all peers ran one bridge, SDK and executable
- frozen sources and binaries match their hashes
- every game window opened on the parking display
- the bridge redirected the centering request in every game
- disposable profiles restored to their originals
- player 2: pointer messages posted in its own process
- player 2: every accepted creature point lies in the part of its view the HUD leaves free
- player 2: its interface was read whole each time
- authority: no native_relationship_acceptance_failed
- authority: no native_unowned_movement_unstoppable
- player-1: no native_relationship_acceptance_failed
- player-1: no native_unowned_movement_unstoppable
- player-2: no native_relationship_acceptance_failed
- player-2: no native_unowned_movement_unstoppable

Observation (ACCEPTED): A starting game was the foreground window when it appeared: player-1, player-2, authority. Known and left as is: happens when the user has been idle.

## Not covered

- Right click, and clicks on nests, fossils and food: NOT_RUN; prerequisite: the gates that own those interactions (C02, C05, C08)

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: True
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen True (user idle 29.6 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen True (user idle 33.2 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 36.5 s)

