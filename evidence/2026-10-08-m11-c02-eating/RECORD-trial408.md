# trial408 - share-kill.py

**Outcome: PASS** (38 of 38 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `a13b9dd03c4409d4fb5227d566b029d949bcf035`; 15 frozen files
- player-1: bridge 0.0.248, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 26606 trace rows, trace SHA-256 `fb2113647366774f471011f1e244ff5804289fe865f732827a1660ae0c952231`
- player-2: bridge 0.0.248, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 25919 trace rows, trace SHA-256 `6f5c99e230a0eb900296d39b40336f673dc4c743b9b4fb7a1514a23b371da773`
- authority: bridge 0.0.248, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 60435 trace rows, trace SHA-256 `55da6328ca8858dc8e86b72f625f8d307b4dfe5f11d3322992f9d9e8b066c60e`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 6.3 |
| freeze | 0.1 |
| display | 0.0 |
| server | 0.4 |
| workers | 7.5 |
| park | 8.0 |
| captures | 0.0 |
| drive | 218.3 |
| close | 5.7 |
| server_stop | 0.0 |
| captures_finish | 2.1 |
| display_release | 0.0 |
| fixture_undo | 2.8 |
| review | 2.4 |
| total | 253.7 |

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

## Not covered

- Two creatures eating one corpse at the same moment: NOT_RUN; prerequisite: this run has the meals follow one another; player 1 starves before player 2 can arrive

## Windows

- Parking display: [0, 0, 1920, 1080]
- A game held the foreground when checked: False
- player-1: first visible at [0, 6, 1286, 755], on the parking display True, held the foreground when first seen False (user idle 19242.3 s)
- player-2: first visible at [317, 168, 1603, 917], on the parking display True, held the foreground when first seen False (user idle 19244.5 s)
- authority: first visible at [634, 331, 1920, 1080], on the parking display True, held the foreground when first seen False (user idle 19247.5 s)

