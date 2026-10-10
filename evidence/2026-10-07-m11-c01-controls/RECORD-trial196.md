# trial196 - pointer-player.py

**Outcome: PASS** (41 of 41 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `c168e9e825426fef3a4862385b02837485a497d7`; 15 frozen files
- player-1: bridge 0.0.203, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 10156 trace rows, trace SHA-256 `ee58908a7221b19a72f7a4b0b873c8344eb9a5780990b221890504952ff10e5c`
- player-2: bridge 0.0.203, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 8105 trace rows, trace SHA-256 `7e0b299c09de8f887e3a5aaa1fe78c803100534de81be8ca1a98c6ef8448f188`
- authority: bridge 0.0.203, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 20804 trace rows, trace SHA-256 `d43fe52fb8e2303f3d5c43fb506a0d3278c27cc3088638a1a39437433caff78c`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 4.8 |
| freeze | 0.1 |
| display | 0.0 |
| server | 0.4 |
| workers | 4.5 |
| park | 8.3 |
| captures | 0.0 |
| drive | 97.1 |
| close | 5.6 |
| server_stop | 0.0 |
| captures_finish | 1.2 |
| display_release | 0.0 |
| fixture_undo | 2.0 |
| review | 0.6 |
| total | 124.7 |

## Passed checks (41)

- player 1 walks up to player 2's creature with its own keys and its click picks exactly that creature
- player 2 walks up to player 1's creature with its own keys and its click picks exactly that creature
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
- player 1: pointer messages posted in its own process
- player 1: every accepted point lies in the part of its view the HUD leaves free
- authority ran player 1's steering from its held keys
- player 2: pointer messages posted in its own process
- player 2: every accepted point lies in the part of its view the HUD leaves free
- authority ran player 2's steering from its held keys
- authority: no native_relationship_acceptance_failed
- authority: no native_unowned_movement_unstoppable
- player-1: no native_relationship_acceptance_failed
- player-1: no native_unowned_movement_unstoppable
- player-2: no native_relationship_acceptance_failed
- player-2: no native_unowned_movement_unstoppable

Observation (ACCEPTED): A starting game was the foreground window when it appeared: player-1, player-2, authority. Known and left as is: happens when the user has been idle.

## Not covered

- Ability keys on the clicked creature, right click, clicks on nests, fossils and food: NOT_RUN; prerequisite: later C01 increments

## Windows

- Parking display: [0, 0, 1920, 1080]
- A game held the foreground when checked: True
- player-1: first visible at [0, 6, 1286, 755], on the parking display True, held the foreground when first seen True (user idle 5156.8 s)
- player-2: first visible at [317, 168, 1603, 917], on the parking display True, held the foreground when first seen True (user idle 5158.9 s)
- authority: first visible at [634, 331, 1920, 1080], on the parking display True, held the foreground when first seen True (user idle 5160.2 s)

