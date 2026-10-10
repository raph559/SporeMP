# trial367 - fruit-probe.py[player-1]

**Outcome: PASS** (39 of 39 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `a527fa78545c56ab5980c9668dd75924b945ae9b`; 15 frozen files
- player-1: bridge 0.0.241, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 5570 trace rows, trace SHA-256 `f36175b8641708b1491092f5868dc5ab4050b719ae1823d4a367cbdff14c60f0`
- player-2: bridge 0.0.241, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 5407 trace rows, trace SHA-256 `96d668f12c4915735fe911832c70c82c9fe8a72a2e1e59a34c134d9d23db4d61`
- authority: bridge 0.0.241, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 7922 trace rows, trace SHA-256 `7d850fadf05684b5d245108ce97e53e785adb30058db3ebdec32666323a855a1`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.3 |
| fixture_restore | 15.0 |
| freeze | 0.2 |
| display | 0.1 |
| server | 0.7 |
| workers | 13.9 |
| park | 9.9 |
| captures | 0.1 |
| drive | 66.1 |
| close | 8.4 |
| server_stop | 0.0 |
| captures_finish | 1.2 |
| display_release | 0.0 |
| fixture_undo | 4.9 |
| review | 0.4 |
| total | 121.2 |

## Passed checks (39)

- the authority holds plants; fruit groups and fruits counted
- player 1 walks to the nearest fruit
- the fruit lies in the free part of the view, by a projection that places the creature itself to 3 pixels
- player 1's click on the fruit reaches the authority, which gives its creature the original's order
- what the original lets this creature do with the fruit
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
- the authority sees the ordered fruit leave its world only if the creature could eat it
- player-1: the eaten fruit leaves this client's world too, through the original's own removal
- player-2: the eaten fruit leaves this client's world too, through the original's own removal
- player 1's hunger is the same in the three games at the end

## Windows

- Parking display: [0, 0, 1920, 1080]
- A game held the foreground when checked: False
- player-1: first visible at [0, 6, 1286, 755], on the parking display True, held the foreground when first seen False (user idle 10118.6 s)
- player-2: first visible at [317, 168, 1603, 917], on the parking display True, held the foreground when first seen False (user idle 10122.0 s)
- authority: first visible at [634, 331, 1920, 1080], on the parking display True, held the foreground when first seen False (user idle 10125.2 s)

