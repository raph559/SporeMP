# trial362 - second-kill.py[restored]

**Outcome: PASS** (45 of 45 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `ddecd026150d7c8b23f495229344dec246a8c93d`; 15 frozen files
- player-1: bridge 0.0.238, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 6233 trace rows, trace SHA-256 `4dd0e58e70b446b3e90a61de4e45b2a8ac62c4d405296fec7ff41d541a93602a`
- player-2: bridge 0.0.238, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 7725 trace rows, trace SHA-256 `dc6cb9cca9e1c9512e3b36f9aedc7f1c577b6dae0d303b284cef8f650b2436a9`
- authority: bridge 0.0.238, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 20103 trace rows, trace SHA-256 `0eb2b718019dac20d7c6f362f6ea612cb3304f9937988b71a3af653bfbeedb9d`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 6.4 |
| freeze | 0.1 |
| display | 0.0 |
| server | 0.4 |
| workers | 7.4 |
| park | 8.0 |
| captures | 0.0 |
| drive | 77.2 |
| close | 5.7 |
| server_stop | 0.0 |
| captures_finish | 0.9 |
| display_release | 0.0 |
| fixture_undo | 2.8 |
| review | 0.5 |
| total | 109.7 |

## Passed checks (45)

- each player's HUD shows the DNA it had when the world was saved
- player 2's click selects a living member of the pack it hunted
- player 2 orders an attack by its key and the authority holds its one kill of that species, and none for player 1
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
- player 2: the original charges its attacks nothing, and every game holds the same energy for it
- player 1's attack is not cancelled by player 2's fight
- authority: no native_relationship_acceptance_failed
- authority: no native_history_acceptance_failed
- authority: no network_rejected
- player-1: no native_relationship_acceptance_failed
- player-1: no native_history_acceptance_failed
- player-1: no network_rejected
- player-2: no native_relationship_acceptance_failed
- player-2: no native_history_acceptance_failed
- player-2: no network_rejected
- player 1: its interface was read whole each time
- player 2: its interface was read whole each time
- the authority reads back player 2's relationships exactly as they were written
- the authority reads back player 2's history with both of its first kills

## Not covered

- Player 2's next kill from the restored state: NOT_RUN; prerequisite: its creature starves within a minute of the restore; it needs food (C02)

## Windows

- Parking display: [0, 0, 1920, 1080]
- A game held the foreground when checked: False
- player-1: first visible at [0, 6, 1286, 755], on the parking display True, held the foreground when first seen False (user idle 7591.2 s)
- player-2: first visible at [317, 168, 1603, 917], on the parking display True, held the foreground when first seen False (user idle 7593.4 s)
- authority: first visible at [634, 331, 1920, 1080], on the parking display True, held the foreground when first seen False (user idle 7596.3 s)

