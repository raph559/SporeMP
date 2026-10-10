# trial370 - eat-fruit.py[fed-and-saved]

**Outcome: PASS** (47 of 47 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `dbd8c7d05fb08ea0dd768e8bcf8698816e6413a1`; 15 frozen files
- player-1: bridge 0.0.242, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 6133 trace rows, trace SHA-256 `a046538a83344850fcd61189d5fed169f954ee9bb899dc101337a106b1fdd049`
- player-2: bridge 0.0.242, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 6497 trace rows, trace SHA-256 `ad720b5c97df0c578876747e9a2450bc6628cf822e86f67512521e304b19bc7f`
- authority: bridge 0.0.242, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 10527 trace rows, trace SHA-256 `0366893e0cd8129b40dcdadeb92775e34d75cfec63198cf17299c0499dc274b8`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.2 |
| fixture_restore | 25.7 |
| freeze | 0.2 |
| display | 0.1 |
| server | 5.1 |
| workers | 16.5 |
| park | 50.1 |
| captures | 0.0 |
| drive | 122.0 |
| close | 6.9 |
| server_stop | 0.0 |
| captures_finish | 1.6 |
| display_release | 0.0 |
| seal | 3.2 |
| fixture_undo | 2.9 |
| review | 0.3 |
| total | 234.7 |

## Passed checks (47)

- the authority holds fruit near the players
- fruit 1: player 2 walks to it, and it lies in the free part of its view by a projection that places its own creature to 3 pixels
- fruit 1: the click reaches the authority, which gives the creature the original's order
- fruit 1: the creature eats it: the fruit leaves the world and its hunger rises
- fruit 2: player 2 walks to it, and it lies in the free part of its view by a projection that places its own creature to 3 pixels
- fruit 2: the click reaches the authority, which gives the creature the original's order
- fruit 2: the creature eats it: the fruit leaves the world and its hunger rises
- the authority counts as many fruits fewer as were eaten
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
- player 2's hunger and health are the same in the three games at the end
- the other player's hunger only falls

## Windows

- Parking display: [0, 0, 1920, 1080]
- A game held the foreground when checked: False
- player-1: first visible at [0, 6, 1286, 755], on the parking display True, held the foreground when first seen False (user idle 10842.3 s)
- player-2: first visible at [317, 168, 1603, 917], on the parking display True, held the foreground when first seen False (user idle 10845.4 s)
- authority: first visible at [634, 331, 1920, 1080], on the parking display True, held the foreground when first seen False (user idle 10848.7 s)

