# trial300 - held-soak.py

**Outcome: PASS** (109 of 109 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `10044f9aae7fdcc2b04d15d02a67e52fac9a9aea`; 15 frozen files
- player-1: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 24721 trace rows, trace SHA-256 `763eb178e16f3aa6d80acfdedb23ec00e9eb17b4c29809a3de7bd0f813428baa`
- player-2: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 22386 trace rows, trace SHA-256 `3be357a872afa02392b96729a84dcab71212bf7978577ebadee83ab26339fe64`
- authority: bridge 0.0.225, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 94651 trace rows, trace SHA-256 `7cfedf527550a6ef68d60ec9b03b0acf9704c972a0d63e3e1a53582820db6457`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.2 |
| fixture_restore | 6.5 |
| freeze | 0.1 |
| display | 0.1 |
| server | 0.4 |
| workers | 7.7 |
| park | 8.0 |
| captures | 0.0 |
| drive | 214.0 |
| close | 6.7 |
| server_stop | 0.0 |
| captures_finish | 2.7 |
| display_release | 0.0 |
| fixture_undo | 2.7 |
| review | 2.6 |
| total | 251.8 |

## Passed checks (109)

- player 1 held W: the authority received its steering without a gap and accepted all of it
- player 1 held W: its creature moved for most of the leg
- player 1 held W: its creature went the way its keys point in its own view
- player 2 held W: the authority received its steering without a gap and accepted all of it
- player 2 held W: its creature moved for most of the leg
- player 2 held W: its creature went the way its keys point in its own view
- player 1 jumps with W held, lands and goes on walking
- player 1 stays where it stopped after W
- player 2 stays where it stopped after W
- player 1 held S: the authority received its steering without a gap and accepted all of it
- player 1 held S: its creature moved for most of the leg
- player 1 held S: its creature went the way its keys point in its own view
- player 2 held S: the authority received its steering without a gap and accepted all of it
- player 2 held S: its creature moved for most of the leg
- player 2 held S: its creature went the way its keys point in its own view
- player 1 stays where it stopped after S
- player 2 stays where it stopped after S
- player 1 held A: the authority received its steering without a gap and accepted all of it
- player 1 held A: its creature moved for most of the leg
- player 1 held A: its creature went the way its keys point in its own view
- player 2 held A: the authority received its steering without a gap and accepted all of it
- player 2 held A: its creature moved for most of the leg
- player 2 held A: its creature went the way its keys point in its own view
- player 1 stays where it stopped after A
- player 2 stays where it stopped after A
- player 1 held D: the authority received its steering without a gap and accepted all of it
- player 1 held D: its creature moved for most of the leg
- player 1 held D: its creature went the way its keys point in its own view
- player 2 held D: the authority received its steering without a gap and accepted all of it
- player 2 held D: its creature moved for most of the leg
- player 2 held D: its creature went the way its keys point in its own view
- player 1 stays where it stopped after D
- player 2 stays where it stopped after D
- player 1 held WD: the authority received its steering without a gap and accepted all of it
- player 1 held WD: its creature moved for most of the leg
- player 1 held WD: its creature went the way its keys point in its own view
- player 2 held WD: the authority received its steering without a gap and accepted all of it
- player 2 held WD: its creature moved for most of the leg
- player 2 held WD: its creature went the way its keys point in its own view
- player 1 stays where it stopped after WD
- player 2 stays where it stopped after WD
- player 1 held SA: the authority received its steering without a gap and accepted all of it
- player 1 held SA: its creature moved for most of the leg
- player 1 held SA: its creature went the way its keys point in its own view
- player 2 held SA: the authority received its steering without a gap and accepted all of it
- player 2 held SA: its creature moved for most of the leg
- player 2 held SA: its creature went the way its keys point in its own view
- player 1 stays where it stopped after SA
- player 2 stays where it stopped after SA
- player 1 held WA: the authority received its steering without a gap and accepted all of it
- player 1 held WA: its creature moved for most of the leg
- player 1 held WA: its creature went the way its keys point in its own view
- player 2 held WA: the authority received its steering without a gap and accepted all of it
- player 2 held WA: its creature moved for most of the leg
- player 2 held WA: its creature went the way its keys point in its own view
- player 1 stays where it stopped after WA
- player 2 stays where it stopped after WA
- player 1 held SD: the authority received its steering without a gap and accepted all of it
- player 1 held SD: its creature moved for most of the leg
- player 1 held SD: its creature went the way its keys point in its own view
- player 2 held SD: the authority received its steering without a gap and accepted all of it
- player 2 held SD: its creature moved for most of the leg
- player 2 held SD: its creature went the way its keys point in its own view
- player 1 stays where it stopped after SD
- player 2 stays where it stopped after SD
- player 1's creature moved for nearly all of the time its keys were held
- player 2's creature moved for nearly all of the time its keys were held
- player 1 does not move while both players are idle
- player 2 does not move while both players are idle
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
- player 1: every key posted down and up in its own process
- player 1: its window procedure saw each leg go down then up, in order
- player 1 ends alive
- player 2: every key posted down and up in its own process
- player 2: its window procedure saw each leg go down then up, in order
- player 2 ends alive
- authority: no native_unowned_movement_unstoppable
- authority: no native_relationship_acceptance_failed
- authority: no network_rejected
- player-1: no native_relationship_acceptance_failed
- player-1: no network_rejected
- player-2: no native_relationship_acceptance_failed
- player-2: no network_rejected

## Not covered

- B's jump and landing: NOT_RUN; prerequisite: foreign B has no jump ability in this fixture

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: False
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen False (user idle 0.1 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen False (user idle 0.1 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen False (user idle 0.5 s)

