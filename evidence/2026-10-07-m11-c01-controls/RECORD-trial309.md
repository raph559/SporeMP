# trial309 - held-controls.py

**Outcome: PASS** (85 of 85 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `10044f9aae7fdcc2b04d15d02a67e52fac9a9aea`; 15 frozen files
- player-1: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 7768 trace rows, trace SHA-256 `42404736d444fc078920d7203bf9c3d7bbcc5cde03a86ebac9baed76eeefec64`
- player-2: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 7085 trace rows, trace SHA-256 `35437828f099d392f0c320c54e93b8c7290995376943b3ea475aad5ee7fb73f2`
- authority: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 14983 trace rows, trace SHA-256 `4d9da379d291489455944ed7b1e9f9bde49a40062a9a595aa42bf33dbbb95b4b`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.2 |
| fixture_restore | 9.0 |
| freeze | 0.2 |
| display | 0.1 |
| server | 0.7 |
| workers | 10.3 |
| park | 8.5 |
| captures | 0.0 |
| drive | 81.7 |
| close | 6.8 |
| server_stop | 0.0 |
| captures_finish | 1.1 |
| display_release | 0.0 |
| fixture_undo | 2.7 |
| review | 0.4 |
| total | 121.6 |

## Passed checks (85)

- player 1 held W: its creature moved
- player 1 W stayed down for the whole hold
- player 1 stays where it stopped after W
- player 2 was not moved by player 1's W
- player 1 held A: its creature moved
- player 1 A stayed down for the whole hold
- player 1 stays where it stopped after A
- player 2 was not moved by player 1's A
- player 1 held S: its creature moved
- player 1 S stayed down for the whole hold
- player 1 stays where it stopped after S
- player 2 was not moved by player 1's S
- player 1 held D: its creature moved
- player 1 D stayed down for the whole hold
- player 1 stays where it stopped after D
- player 2 was not moved by player 1's D
- player 1's jump key reaches the authority as its own jump
- A jumps
- A lands
- player 2 held W: its creature moved
- player 2 W stayed down for the whole hold
- player 2 stays where it stopped after W
- player 1 was not moved by player 2's W
- player 2 held A: its creature moved
- player 2 A stayed down for the whole hold
- player 2 stays where it stopped after A
- player 1 was not moved by player 2's A
- player 2 held S: its creature moved
- player 2 S stayed down for the whole hold
- player 2 stays where it stopped after S
- player 1 was not moved by player 2's S
- player 2 held D: its creature moved
- player 2 D stayed down for the whole hold
- player 2 stays where it stopped after D
- player 1 was not moved by player 2's D
- player 2's jump key reaches the authority as its own jump
- player 1 does not move while both players are idle
- player 2 does not move while both players are idle
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
- player 1: five keys posted down and up in its own process
- player 1: its window procedure saw each key go down then up, in order
- authority ran player 1's steering from its held keys
- player 2: five keys posted down and up in its own process
- player 2: its window procedure saw each key go down then up, in order
- authority ran player 2's steering from its held keys
- player 1's creature does not move before its owner's first command
- player 2's creature does not move before its owner's first command
- authority: no native_unowned_movement_unstoppable
- authority: no native_global_dna_enter
- authority: no native_award_return
- authority: no native_relationship_acceptance_failed
- player-1: no native_global_dna_enter
- player-1: no native_award_return
- player-1: no native_relationship_acceptance_failed
- player-2: no native_global_dna_enter
- player-2: no native_award_return
- player-2: no native_relationship_acceptance_failed
- player 1 ends alive with 14 DNA
- player 2 ends alive with 7 DNA

## Not covered

- Ability keys, mouse targeting and movement during an interaction: NOT_RUN; prerequisite: later C01 increments
- B's jump and landing: NOT_RUN; prerequisite: foreign B has no jump ability in this fixture
- Fossils the original creates while a player walks: NOT_RUN; prerequisite: a qualified client-side fossil factory (C07/C08)

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: False
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen False (user idle 0.8 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen False (user idle 0.9 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen False (user idle 0.2 s)

