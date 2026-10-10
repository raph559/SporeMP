# trial453 - eat-fruit.py[both-order]

**Outcome: PASS** (42 of 42 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `f528461a5a78976ceeb34b71c8b4a5b41b70766a`; 15 frozen files
- player-1: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 12589 trace rows, trace SHA-256 `f833fa6fb5471bbf328c9a1b1a0b2910a930fdd91a9e51ed4afb1a4d3a4580ad`
- player-2: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 12392 trace rows, trace SHA-256 `57e6f8c6034be3da68aa809811ef0becfadad6960ebafe9cf7986c56326c328f`
- authority: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 24861 trace rows, trace SHA-256 `2666ef75588350dcbef6cc4847fd817219c396c4a38b7ef01d92f834f9703ff4`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 4.6 |
| freeze | 0.1 |
| display | 0.0 |
| server | 0.4 |
| workers | 5.6 |
| park | 7.8 |
| captures | 0.0 |
| drive | 128.2 |
| close | 4.9 |
| server_stop | 0.0 |
| captures_finish | 1.4 |
| display_release | 0.0 |
| fixture_undo | 1.9 |
| review | 0.8 |
| total | 155.9 |

## Passed checks (42)

- the authority holds fruit near the players
- both players walk to one fruit and each finds it in its own view
- each player's click on the one fruit reaches the authority, which takes both orders
- the fruit is taken from its place once, by the first creature to reach it; the creature that eats no fruit is fed nothing, whichever took it
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
- the fruit two players ordered leaves its place on the authority once, and at most one creature is fed from it
- at the start the three games hold the same fruits in the same places
- at the end every fruit lies in the same place and state in the three games, those the authority moved included
- player 2's hunger and health are the same in the three games at the end
- the other player's hunger only falls

Observation (ACCEPTED): A starting game was the foreground window when it appeared: player-1, player-2, authority. Known and left as is: happens when the user has been idle.

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: True
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen True (user idle 1080.3 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen True (user idle 1082.0 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 1084.0 s)

