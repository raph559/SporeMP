# trial454 - eat-fruit.py[from-the-plant]

**Outcome: PASS** (48 of 48 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `cd9835116dfaba621272a946f51f0bd48c974b6e`; 15 frozen files
- player-1: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 10824 trace rows, trace SHA-256 `c479de634468550ed9280aca49136af0ae23cff705c14ee88a7bef8078ce921e`
- player-2: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 12144 trace rows, trace SHA-256 `5a5a56ff758c0c72ff42f5d790f5faf5629e46cf833d8927659d434684882125`
- authority: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 19524 trace rows, trace SHA-256 `15b58f17df9692b7354ab83b94ee4eb99fb65728f3884d2992a1371e58ff184c`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 4.8 |
| freeze | 0.1 |
| display | 0.1 |
| server | 0.4 |
| workers | 5.8 |
| park | 7.8 |
| captures | 0.0 |
| drive | 118.2 |
| close | 5.0 |
| server_stop | 0.0 |
| captures_finish | 1.4 |
| display_release | 0.0 |
| fixture_undo | 2.1 |
| review | 0.8 |
| total | 146.5 |

## Passed checks (48)

- the authority holds fruit near the players
- fruit 1: player 2 walks to it, and it lies in the free part of its view by a projection that places its own creature to 3 pixels
- fruit 1: the click reaches the authority, which gives the creature the original's order
- fruit 1: the creature eats it: the fruit leaves the world and its hunger rises
- fruit 1: player 2's own HUD shows its health before and after
- fruit 2: player 2 walks to it, and it lies in the free part of its view by a projection that places its own creature to 3 pixels
- fruit 2: the click reaches the authority, which gives the creature the original's order
- fruit 2: the original feeds this creature nothing from it: its hunger keeps falling
- fruit 2: player 2's own HUD shows its health before and after
- the creature has eaten
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
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen True (user idle 1246.5 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen True (user idle 1248.3 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 1250.2 s)

