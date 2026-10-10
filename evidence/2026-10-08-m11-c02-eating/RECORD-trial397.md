# trial397 - eat-fruit.py[player-1]

**Outcome: PASS** (46 of 46 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `4ffbacde4cf6b358ee44cfbf2889324b1a5d3526`; 15 frozen files
- player-1: bridge 0.0.246, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 5465 trace rows, trace SHA-256 `021b6a2c029f0f0d89487fad4de7ab29ada15b8124136a6543c854e65cddd157`
- player-2: bridge 0.0.246, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 5247 trace rows, trace SHA-256 `4ebe53a369977997f70d66a31e29113b2bf9ffb8b7d8f7d67da98e87462e31dd`
- authority: bridge 0.0.246, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 7310 trace rows, trace SHA-256 `719c8995e4570b7da3d3a79c7a20c0dfdb047553749a01e03c4d17f1452894f7`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 6.5 |
| freeze | 0.1 |
| display | 0.0 |
| server | 0.4 |
| workers | 7.6 |
| park | 7.9 |
| captures | 0.0 |
| drive | 56.2 |
| close | 5.9 |
| server_stop | 0.0 |
| captures_finish | 0.7 |
| display_release | 0.0 |
| fixture_undo | 2.8 |
| review | 0.2 |
| total | 88.5 |

## Passed checks (46)

- the authority holds fruit near the players
- fruit 1: player 1 walks to it, and it lies in the free part of its view by a projection that places its own creature to 3 pixels
- fruit 1: the click reaches the authority, which gives the creature the original's order
- fruit 1: the original lets this creature eat none of it: the fruit stays and its hunger keeps falling
- fruit 1: player 1's own HUD shows its health before and after
- the creature has eaten nothing
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
- authority: no network_rejected
- authority: no native_relationship_acceptance_failed
- player-1: no network_rejected
- player-1: no native_relationship_acceptance_failed
- player-2: no network_rejected
- player-2: no native_relationship_acceptance_failed
- the authority sees an ordered fruit leave its world only if the creature could eat it
- player-1: each eaten fruit leaves this client's world too, through the original's own removal
- player-2: each eaten fruit leaves this client's world too, through the original's own removal
- at the start the three games hold the same fruits in the same places
- at the end the fruits no game re-placed are still the same in the three games
- player 1's hunger and health are the same in the three games at the end
- the other player's hunger only falls

## Windows

- Parking display: [0, 0, 1920, 1080]
- A game held the foreground when checked: False
- player-1: first visible at [0, 6, 1286, 755], on the parking display True, held the foreground when first seen False (user idle 15958.1 s)
- player-2: first visible at [317, 168, 1603, 917], on the parking display True, held the foreground when first seen False (user idle 15960.3 s)
- authority: first visible at [634, 331, 1920, 1080], on the parking display True, held the foreground when first seen False (user idle 15963.3 s)

