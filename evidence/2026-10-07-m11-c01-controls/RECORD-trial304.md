# trial304 - ability-key.py[player-2-attack]

**Outcome: PASS** (46 of 46 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `10044f9aae7fdcc2b04d15d02a67e52fac9a9aea`; 15 frozen files
- player-1: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 11428 trace rows, trace SHA-256 `a1cdb36ff0ce9e9a8a0b2568e4743d22dfbee03a372d51f717017986f4df8777`
- player-2: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 16251 trace rows, trace SHA-256 `f1e0e5686aaa103546ae4970a7a938034204e03fa69efc04db9a673167d9c7bf`
- authority: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 29863 trace rows, trace SHA-256 `e59b57c5003fbbe528040cf92b7cbf34b9609a840639ba12cc6669575b720889`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 6.2 |
| freeze | 0.1 |
| display | 0.1 |
| server | 0.4 |
| workers | 7.5 |
| park | 8.0 |
| captures | 0.0 |
| drive | 144.5 |
| close | 13.3 |
| server_stop | 0.0 |
| captures_finish | 3.0 |
| display_release | 0.0 |
| fixture_undo | 9.2 |
| review | 1.8 |
| total | 194.2 |

## Passed checks (46)

- player 2's own HUD shows its DNA
- player 2 walks up to a creature of another species and its click picks exactly that creature
- player 2 confirms what the original shows on this contact
- player 2 walks away from the creature it has met before choosing to attack
- player 2 chooses the attack stance with its button and the number keys are on the attack abilities
- player 2's attack ability 2, used by its key, reaches the authority as its own command aimed at a creature
- player 2's attack ability 1, used by its button, reaches the authority as its own command aimed at a creature
- the authority runs player 2's original attack and the target loses health
- player 2 sidesteps with its own key during the fight
- player 2's attack abilities are the ones their slots stand for
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

- Player 2's kill and its payout: NOT_RUN; prerequisite: C03: the authority does not yet admit every kill of the second player
- Right click, and clicks on nests, fossils and food: NOT_RUN; prerequisite: the gates that own those interactions (C02, C05, C08)

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: True
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen True (user idle 46.0 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen True (user idle 48.3 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 51.4 s)

