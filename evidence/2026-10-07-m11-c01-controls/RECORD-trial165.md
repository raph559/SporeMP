# trial165 - held-controls.py

**Outcome: PASS** (82 of 82 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `864c74e85512c7be1d18a145b96c8e58d003e1aa`; 15 frozen files
- player-1: bridge 0.0.194, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 7714 trace rows, trace SHA-256 `161943104f31ed870abe4ad6dd3db9d397a22c9aa1c0b4fbe981b43048a4ba51`
- player-2: bridge 0.0.194, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 6930 trace rows, trace SHA-256 `5e36cc0d9d363d8df5dd3d4c48ae4e54e9535ebb9d56bcd8fdf057d1b480038e`
- authority: bridge 0.0.194, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 14701 trace rows, trace SHA-256 `e9df62fcb129a58f48abfee2f85276b2b34b1888b08387997862ce746116d4a5`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 5.4 |
| freeze | 0.1 |
| display | 0.1 |
| server | 0.4 |
| workers | 4.8 |
| park | 8.5 |
| captures | 0.0 |
| drive | 81.4 |
| close | 4.8 |
| server_stop | 0.0 |
| captures_finish | 1.0 |
| display_release | 0.0 |
| fixture_undo | 2.2 |
| review | 0.4 |
| total | 109.3 |

## Passed checks (82)

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
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen False (user idle 1.4 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen False (user idle 3.8 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen False (user idle 5.4 s)

