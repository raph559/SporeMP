# trial308 - neutral-abilities.py

**Outcome: PASS** (45 of 45 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `10044f9aae7fdcc2b04d15d02a67e52fac9a9aea`; 15 frozen files
- player-1: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 4116 trace rows, trace SHA-256 `ef58d8b769bd03826315e9f3aa52d4f1eed94db64f74bf8fa6b58b7b7b6e7268`
- player-2: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 4334 trace rows, trace SHA-256 `3226263ae33509ebece75dd5734d12c5c1f541b742ae88d65262bcd508af333a`
- authority: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 4529 trace rows, trace SHA-256 `31b5147049ef8fc136491c71b9fc871dcde806dd9d8d1186230ae666d7199fb6`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 6.3 |
| freeze | 0.1 |
| display | 0.0 |
| server | 0.4 |
| workers | 6.8 |
| park | 8.0 |
| captures | 0.0 |
| drive | 51.9 |
| close | 6.8 |
| server_stop | 0.0 |
| captures_finish | 0.7 |
| display_release | 0.0 |
| fixture_undo | 2.6 |
| review | 0.2 |
| total | 84.0 |

## Passed checks (45)

- player 1's bar shows its middle group
- player 1's middle button 16850992 stays with the original on its own game and sends the authority nothing
- player 1's Jump button reaches the authority as its own jump, and its creature jumps and lands
- player 1's middle buttons are its creature's own abilities of that group
- player 2's bar shows its middle group
- player 2's middle button 16850992 stays with the original on its own game and sends the authority nothing
- player 2's middle button 16850993 holds no ability: nothing is dispatched or sent
- player 2's middle buttons are its creature's own abilities of that group
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
- player 1: its interface was read whole
- player 2: pointer messages posted in its own process
- player 2: its interface was read whole
- authority: no network_rejected
- authority: no native_unowned_movement_unstoppable
- player-1: no network_rejected
- player-1: no native_unowned_movement_unstoppable
- player-2: no network_rejected
- player-2: no native_unowned_movement_unstoppable

Observation (ACCEPTED): A starting game was the foreground window when it appeared: player-1, player-2, authority. Known and left as is: happens when the user has been idle.

## Not covered

- What Mating Call does once the original has it: NOT_RUN; prerequisite: C09: mating

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: True
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen True (user idle 160.6 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen True (user idle 162.9 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 165.5 s)

