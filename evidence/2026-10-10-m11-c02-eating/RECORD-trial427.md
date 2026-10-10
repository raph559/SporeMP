# trial427 - share-kill.py[saved]

**Outcome: PASS** (39 of 39 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `ed3392033e5b7b28f52b5fc8497a9104da34df4c`; 15 frozen files
- player-1: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 24764 trace rows, trace SHA-256 `da3b0fccf3523cd19b4858f04dca40d73f25230d44787ff12ae5697bd650b0dc`
- player-2: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 23957 trace rows, trace SHA-256 `97cff0ba5b2263947efdfbfb2bc7af80c370c34db9b4128d5c2732fefe3657c3`
- authority: bridge 0.0.253, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 62793 trace rows, trace SHA-256 `fd6f60269f147034f010937608452915fb55a5e3bedb5e6145547ea6eb2934d6`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 5.0 |
| freeze | 0.1 |
| display | 0.0 |
| server | 0.5 |
| workers | 6.0 |
| park | 8.0 |
| captures | 0.0 |
| drive | 211.6 |
| close | 4.8 |
| server_stop | 0.0 |
| captures_finish | 2.3 |
| display_release | 0.0 |
| seal | 1.9 |
| fixture_undo | 2.0 |
| review | 2.4 |
| total | 244.8 |

## Passed checks (39)

- a weak creature of another species stands near player 1
- player 1 walks up to it and its click picks it
- player 1 orders its attack by a click and the authority accepts it
- player 1 kills it
- player 1 orders its creature to the corpse by a click and it is fed
- player 2 walks to the corpse with its movement keys
- player 1 emptied the corpse, and player 2, arriving after, is fed nothing from it and no order is taken
- the original Save completes with the two players side by side
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
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen True (user idle 761.5 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen True (user idle 763.5 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 765.7 s)

