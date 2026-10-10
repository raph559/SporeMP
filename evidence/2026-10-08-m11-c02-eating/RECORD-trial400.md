# trial400 - eat-fruit.py[fed-and-saved]

**Outcome: PASS** (51 of 51 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `6c28a76d3d23b1b1806ee7a77beaddbfdfc00c2f`; 15 frozen files
- player-1: bridge 0.0.246, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 6226 trace rows, trace SHA-256 `7e75d383e483de5aaa636dc39c015fadc83d8af33820fd197ed1264316fd64ca`
- player-2: bridge 0.0.246, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 7148 trace rows, trace SHA-256 `0ed59e9ddc4d8413c3183c7b30e8a607b0297dece5e1fce0a6a0aa1fd20caabe`
- authority: bridge 0.0.246, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 10650 trace rows, trace SHA-256 `017cccaf802ea4c6355202c6231d71838d26cc91cecebfe9c1da0e919791ed21`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 6.4 |
| freeze | 0.1 |
| display | 0.0 |
| server | 0.4 |
| workers | 7.5 |
| park | 7.9 |
| captures | 0.0 |
| drive | 69.0 |
| close | 5.7 |
| server_stop | 0.0 |
| captures_finish | 0.8 |
| display_release | 0.0 |
| fixture_undo | 2.8 |
| review | 0.3 |
| total | 101.3 |

## Passed checks (51)

- the authority holds fruit near the players
- fruit 1: player 2 walks to it, and it lies in the free part of its view by a projection that places its own creature to 3 pixels
- fruit 1: the click reaches the authority, which gives the creature the original's order
- fruit 1: the creature eats it: the fruit leaves the world and its hunger rises
- fruit 1: player 2's own HUD shows its health before and after
- fruit 2: player 2 walks to it, and it lies in the free part of its view by a projection that places its own creature to 3 pixels
- fruit 2: the click reaches the authority, which gives the creature the original's order
- fruit 2: the creature eats it: the fruit leaves the world and its hunger rises
- fruit 2: player 2's own HUD shows its health before and after
- the creature has eaten
- the original Save completes after the meal
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
- player 2's hunger and health are the same in the three games at the end
- the other player's hunger only falls

## Windows

- Parking display: [0, 0, 1920, 1080]
- A game held the foreground when checked: False
- player-1: first visible at [0, 6, 1286, 755], on the parking display True, held the foreground when first seen False (user idle 16395.1 s)
- player-2: first visible at [317, 168, 1603, 917], on the parking display True, held the foreground when first seen False (user idle 16397.4 s)
- authority: first visible at [634, 331, 1920, 1080], on the parking display True, held the foreground when first seen False (user idle 16400.4 s)

