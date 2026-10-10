# trial279 - camera-probe.py

**Outcome: PASS** (35 of 35 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `be977f13e19b9b570e40c4782bf8e8f8f2ea5fbc`; 15 frozen files
- player-1: bridge 0.0.222, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 6419 trace rows, trace SHA-256 `7ab0499ecf05247d1059ef383cc0a007cb0413478eb8bd1942fa31fa2d5f8318`
- player-2: bridge 0.0.222, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 7374 trace rows, trace SHA-256 `b5db3da17d5a750d93d3fdea10f74262af6f9310633704a35f01b9b961e8ec6f`
- authority: bridge 0.0.222, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 11105 trace rows, trace SHA-256 `4356eee64adfab7c29def7a247d34183cf267059a260ce1561da7d6f981a4c1b`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.2 |
| fixture_restore | 9.7 |
| freeze | 0.1 |
| display | 0.1 |
| server | 1.3 |
| workers | 8.4 |
| park | 8.1 |
| captures | 0.0 |
| drive | 81.1 |
| close | 8.6 |
| server_stop | 0.0 |
| captures_finish | 1.2 |
| display_release | 0.0 |
| fixture_undo | 3.0 |
| review | 0.2 |
| total | 122.2 |

## Passed checks (35)

- player 2 idle
- player 2 moved by the diagnostic intention, direction 0
- player 2 moved by the diagnostic intention, direction 1
- player 2 moved by the diagnostic intention, direction 2
- player 2 moved by the diagnostic intention, direction 3
- player 2 holds W
- player 2 idle again
- player 2 holds D
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

Observation (ACCEPTED): A starting game was the foreground window when it appeared: player-1, player-2, authority. Known and left as is: happens when the user has been idle.

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: True
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen True (user idle 744.5 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen True (user idle 746.8 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 750.1 s)

