# trial440 - eat-fruit.py[fed-and-saved]

**Outcome: PASS** (49 of 49 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `5fbc37508d192efa51ab771a1cb7dec9344c4980`; 15 frozen files
- player-1: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 8207 trace rows, trace SHA-256 `20c1d2ae565fd663d7fb2c60534f0d64b279b39b81b6da8a2bc963057def30b7`
- player-2: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 8946 trace rows, trace SHA-256 `f338784fb683f83dea83dba9569c0c8827d5262af52dfcad1130d6e95e621af8`
- authority: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 13362 trace rows, trace SHA-256 `05d88db1acffc30249d4fedc6ac2efcd7ea0ebbeb91ece676c79a4e5b2bb2f2c`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 5.4 |
| freeze | 0.1 |
| display | 0.1 |
| server | 0.4 |
| workers | 6.3 |
| park | 8.2 |
| captures | 0.0 |
| drive | 91.1 |
| close | 4.8 |
| server_stop | 0.0 |
| captures_finish | 1.2 |
| display_release | 0.0 |
| seal | 1.7 |
| fixture_undo | 2.1 |
| review | 0.5 |
| total | 122.0 |

## Passed checks (49)

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
- the authority feeds the orderer of a fruit only if its creature eats fruit
- at the start the three games hold the same fruits in the same places
- at the end every fruit lies in the same place and state in the three games, those the authority moved included
- player 2's hunger and health are the same in the three games at the end
- the other player's hunger only falls

Observation (ACCEPTED): A starting game was the foreground window when it appeared: player-1, player-2, authority. Known and left as is: happens when the user has been idle.

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: True
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen True (user idle 629.5 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen True (user idle 631.6 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 633.9 s)

