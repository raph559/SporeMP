# trial317 - ability-key.py[player-2-kill]

**Outcome: PASS** (50 of 50 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `9e71cefdb544f2c9c3a54f2f699c6d0315525839`; 15 frozen files
- player-1: bridge 0.0.226, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 12771 trace rows, trace SHA-256 `77ea34642738c77f442a8f839356197d9901e9591e7af7e01b4eefa0d66d148a`
- player-2: bridge 0.0.226, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 18063 trace rows, trace SHA-256 `c423d8e3367b1bd0da27f0ccc6a0b473f82be2ff8c25fd65c52c505b25448d2b`
- authority: bridge 0.0.226, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 32953 trace rows, trace SHA-256 `d5ca1c45c7ba49bd36e6d09a37a59165e5b3076742a656fb3a71339618d0a99e`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.2 |
| fixture_restore | 6.9 |
| freeze | 0.1 |
| display | 0.1 |
| server | 0.4 |
| workers | 8.0 |
| park | 8.0 |
| captures | 0.0 |
| drive | 157.5 |
| close | 5.9 |
| server_stop | 0.0 |
| captures_finish | 1.9 |
| display_release | 0.0 |
| fixture_undo | 2.9 |
| review | 1.1 |
| total | 192.8 |

## Passed checks (50)

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
- player 2's attack ability 1, used by its key, reaches the authority as its own command aimed at a creature
- player 2 confirms what the original shows after the kill
- player 2's creature kills the target and its own HUD shows the DNA the original paid
- player 1's DNA is untouched by player 2's kill, on its own HUD and on the authority
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

Observation (ACCEPTED): A starting game was the foreground window when it appeared: player-1. Known and left as is: happens when the user has been idle.

## Not covered

- Right click, and clicks on nests, fossils and food: NOT_RUN; prerequisite: the gates that own those interactions (C02, C05, C08)

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: False
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen True (user idle 0.0 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen False (user idle 0.0 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen False (user idle 0.0 s)

