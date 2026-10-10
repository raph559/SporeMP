# trial298 - camera-probe.py

**Outcome: PASS** (40 of 40 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `10044f9aae7fdcc2b04d15d02a67e52fac9a9aea`; 15 frozen files
- player-1: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 6605 trace rows, trace SHA-256 `339cb5010c9f0321643c83192882efd1d5247d035f0f026194d7885169bb6b5a`
- player-2: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 7387 trace rows, trace SHA-256 `cab0d89d244593279f0aded03f62e6cde224ce78a9d0cc1fbaea6e0834920ea8`
- authority: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 10704 trace rows, trace SHA-256 `32a9f49d82f22c9d172de0474328a55946a7b417fd5b34c4531685deb5f5b997`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.3 |
| fixture_restore | 20.0 |
| freeze | 0.2 |
| display | 0.1 |
| server | 0.9 |
| workers | 17.6 |
| park | 10.6 |
| captures | 0.0 |
| drive | 81.3 |
| close | 9.1 |
| server_stop | 0.0 |
| captures_finish | 1.0 |
| display_release | 0.0 |
| fixture_undo | 3.0 |
| review | 0.3 |
| total | 144.6 |

## Passed checks (40)

- player 2 idle: its camera keeps its direction
- player 2 moved by the diagnostic intention, direction 0: its camera keeps its direction
- player 2 moved by the diagnostic intention, direction 1: its camera keeps its direction
- player 2 moved by the diagnostic intention, direction 2: its camera keeps its direction
- player 2 moved by the diagnostic intention, direction 3: its camera keeps its direction
- player 2 holds W: its camera keeps its direction
- player 2 idle again: its camera keeps its direction
- player 2 holds D: its camera keeps its direction
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
- player 2: its own avatar keeps no native locomotion request
- player-1: no native_client_avatar_request_unstoppable
- player-1: no network_rejected
- player-2: no native_client_avatar_request_unstoppable
- player-2: no network_rejected

Observation (ACCEPTED): A starting game was the foreground window when it appeared: player-2, authority. Known and left as is: happens when the user has been idle.

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: True
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen False (user idle 10.0 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen True (user idle 13.0 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 17.5 s)

