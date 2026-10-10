# trial289 - held-soak.py

**Outcome: FAIL** (41 of 46 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

Error: player 2 held W: its creature went the way its keys point in its own view

## Identity

- Source commit `64f4df94ad0224e565f92fde605a0979da583ad8`; 15 frozen files
- player-1: bridge 0.0.222, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 5496 trace rows, trace SHA-256 `6f5a4e488f962db81a12dd50124cc50fbfc0b5200782d13fd447204f30d174d1`
- player-2: bridge 0.0.222, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 4673 trace rows, trace SHA-256 `4692f1ab7d9f1c8fe120c989775add3b80e7ae918cf2e61b9b3abe2253f44492`
- authority: bridge 0.0.222, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 13542 trace rows, trace SHA-256 `b3ed1844ea83682d290fcf5f1bb822f75f7d8fd520e78a619019d0e711351aac`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 6.0 |
| freeze | 0.1 |
| display | 0.0 |
| server | 0.4 |
| workers | 7.4 |
| park | 8.0 |
| captures | 0.0 |
| drive | 47.4 |
| close | 5.9 |
| server_stop | 0.0 |
| captures_finish | 0.8 |
| display_release | 0.0 |
| fixture_undo | 2.9 |
| review | 0.3 |
| total | 79.4 |

## Failed checks

- player 2 held W: its creature went the way its keys point in its own view
- player 1: every key posted down and up in its own process
- player 1: its window procedure saw each leg go down then up, in order
- player 2: every key posted down and up in its own process
- player 2: its window procedure saw each leg go down then up, in order

## Passed checks (41)

- player 1 held W: the authority received its steering without a gap and accepted all of it
- player 1 held W: its creature moved for most of the leg
- player 1 held W: its creature went the way its keys point in its own view
- player 2 held W: the authority received its steering without a gap and accepted all of it
- player 2 held W: its creature moved for most of the leg
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
- player 1 ends alive
- player 2 ends alive
- authority: no native_unowned_movement_unstoppable
- authority: no native_relationship_acceptance_failed
- authority: no network_rejected
- player-1: no native_relationship_acceptance_failed
- player-1: no network_rejected
- player-2: no native_relationship_acceptance_failed
- player-2: no network_rejected

Observation (ACCEPTED): A starting game was the foreground window when it appeared: player-1, player-2, authority. Known and left as is: happens when the user has been idle.

## Windows

- Parking display: [2560, 0, 4480, 1080]
- A game held the foreground when checked: True
- player-1: first visible at [2560, 0, 3846, 755], on the parking display True, held the foreground when first seen True (user idle 56.2 s)
- player-2: first visible at [2877, 162, 4163, 917], on the parking display True, held the foreground when first seen True (user idle 58.5 s)
- authority: first visible at [3194, 325, 4480, 1080], on the parking display True, held the foreground when first seen True (user idle 61.5 s)

