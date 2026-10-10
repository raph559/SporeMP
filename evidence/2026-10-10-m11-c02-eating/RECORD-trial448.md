# trial448 - share-kill.py[together]

**Outcome: PASS** (43 of 43 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `c103838d140805266202b2bb354b7db7478d104b`; 15 frozen files
- player-1: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 11683 trace rows, trace SHA-256 `3a710d82600ad97eba314c81e653bc17cd61edede30faffa41c33fb4d8ecb48c`
- player-2: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 10809 trace rows, trace SHA-256 `5363c2b76754757b89cb52204f0b1412768cb758d9be27664dea33bae4a29393`
- authority: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 21141 trace rows, trace SHA-256 `972eb2a05ee40e099a00a1e3498a64ce050313415c50ff33c1d63a7ccf8f1013`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 5.3 |
| freeze | 0.1 |
| display | 0.0 |
| server | 0.4 |
| workers | 6.5 |
| park | 8.2 |
| captures | 0.0 |
| drive | 102.5 |
| close | 4.7 |
| server_stop | 0.0 |
| captures_finish | 1.2 |
| display_release | 0.0 |
| fixture_undo | 1.9 |
| review | 0.7 |
| total | 131.8 |

## Passed checks (43)

- the two players, of one species, stand together at the start
- a weak creature of another species stands near player 1
- player 1 walks up to it and its click picks it
- player 1 orders its attack by a click and the authority accepts it
- player 1 kills it
- both creatures stand at the corpse
- each player orders its own creature to the corpse by a click in its own game, and the authority takes both orders
- the creature whose order came first eats; the other, ordered within a second of it, is fed nothing while it does
- the first having eaten, the other player orders again and its creature is fed from what is left
- each player's own HUD shows its creature's health after the meals
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
- player-1: no network_rejected
- player-2: no network_rejected
- each player's hunger is the same in the three games at the end
- the two creatures, ordered at the same moment, eat one after the other: the times over which each is fed do not overlap
- the corpse's food is the same in the three games at the end

Observation (ACCEPTED): A starting game was the foreground window when it appeared: player-1, player-2, authority. Known and left as is: happens when the user has been idle.

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: True
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen True (user idle 189.8 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen True (user idle 192.0 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 194.4 s)

