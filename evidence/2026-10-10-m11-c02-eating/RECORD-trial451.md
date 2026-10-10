# trial451 - share-kill.py

**Outcome: PASS** (38 of 38 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `c103838d140805266202b2bb354b7db7478d104b`; 15 frozen files
- player-1: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 26510 trace rows, trace SHA-256 `57bcbd32c000f9140d85b8d2958b7060289f9acf7b49689878720c9a0f746f26`
- player-2: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 27712 trace rows, trace SHA-256 `f7e5b32b2832c3c238997ea2102ad9a7b7fdcd9c5421bf86de067ccd61755117`
- authority: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 63551 trace rows, trace SHA-256 `d2e5da1c5e8cdd984e0a51e932c21771268d1ff0837eb848ec056f38a4ea35cc`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 4.6 |
| freeze | 0.1 |
| display | 0.0 |
| server | 0.4 |
| workers | 5.5 |
| park | 7.8 |
| captures | 0.0 |
| drive | 225.6 |
| close | 4.7 |
| server_stop | 0.0 |
| captures_finish | 2.2 |
| display_release | 0.0 |
| fixture_undo | 1.9 |
| review | 2.5 |
| total | 255.4 |

## Passed checks (38)

- a weak creature of another species stands near player 1
- player 1 walks up to it and its click picks it
- player 1 orders its attack by a click and the authority accepts it
- player 1 kills it
- player 1 orders its creature to the corpse by a click and it is fed
- player 2 walks to the corpse with its movement keys
- player 1 emptied the corpse, and player 2, arriving after, is fed nothing from it and no order is taken
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

Observation (ACCEPTED): A starting game was the foreground window when it appeared: player-1, player-2, authority. Known and left as is: happens when the user has been idle.

## Not covered

- Two creatures eating one corpse at the same moment: NOT_RUN; prerequisite: this run has the meals follow one another; player 1 starves before player 2 can arrive

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: True
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen True (user idle 229.4 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen True (user idle 231.2 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 233.1 s)

