# trial441 - eat-fruit.py[restored]

**Outcome: PASS** (45 of 45 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `5fbc37508d192efa51ab771a1cb7dec9344c4980`; 15 frozen files
- player-1: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 15004 trace rows, trace SHA-256 `abcefce73b652c3fd90d7e188477428a792c2c942a7889657867a2e293e90212`
- player-2: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 15416 trace rows, trace SHA-256 `eb170dcce9aceb1b99fee438bdc95431e91b0ef5648e6bad89e33c6a345c5d6b`
- authority: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 20400 trace rows, trace SHA-256 `55732a5f50033bc8a68c27c5eb831b13b1adba14fc8b004eba203e3f0c0a95c0`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 6.8 |
| freeze | 0.1 |
| display | 0.1 |
| server | 0.4 |
| workers | 6.7 |
| park | 8.2 |
| captures | 0.0 |
| drive | 159.6 |
| close | 5.1 |
| server_stop | 0.0 |
| captures_finish | 1.8 |
| display_release | 0.0 |
| fixture_undo | 2.1 |
| review | 1.1 |
| total | 192.1 |

## Passed checks (45)

- the authority holds fruit near the players
- the restored player has the health and hunger it was saved with, less the hunger since, and each player its DNA
- fruit 1: player 2 walks to it, and it lies in the free part of its view by a projection that places its own creature to 3 pixels
- fruit 1: the click reaches the authority, which gives the creature the original's order
- fruit 1: the creature eats it: the fruit leaves the world and its hunger rises
- fruit 1: player 2's own HUD shows its health before and after
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

Observation (ACCEPTED): A starting game was the foreground window when it appeared: authority. Known and left as is: happens when the user has been idle.

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: False
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen False (user idle 3.2 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen False (user idle 5.4 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 0.0 s)

