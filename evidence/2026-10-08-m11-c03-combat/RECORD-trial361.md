# trial361 - second-kill.py[first-kill-saved]

**Outcome: PASS** (52 of 52 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `ddecd026150d7c8b23f495229344dec246a8c93d`; 15 frozen files
- player-1: bridge 0.0.238, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 20617 trace rows, trace SHA-256 `d042f4e2e40b87f0dadc08780825459614f1b73bde282c6747b1f0df9f261240`
- player-2: bridge 0.0.238, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 21351 trace rows, trace SHA-256 `47a14fb877816c65b4a9ac24bf4273e8dcf4ba67f625d0f02e00021d39a6e201`
- authority: bridge 0.0.238, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 51718 trace rows, trace SHA-256 `e89a2ff42020c805487c70da2eed94d0c4f209345089a8ec615a42909ba79070`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 6.3 |
| freeze | 0.1 |
| display | 0.0 |
| server | 0.4 |
| workers | 7.5 |
| park | 7.9 |
| captures | 0.0 |
| drive | 201.1 |
| close | 5.8 |
| server_stop | 0.0 |
| captures_finish | 2.0 |
| display_release | 0.0 |
| seal | 2.3 |
| fixture_undo | 2.8 |
| review | 1.7 |
| total | 238.3 |

## Passed checks (52)

- the pack's alpha is in the scene
- each player's HUD shows its DNA
- player 2 walks to the Blatardo and its click selects it
- player 1 walks up to it and its click picks it
- player 1 orders its attack by a click and the authority accepts it
- player 2 joins the fight by its attack key and the authority accepts it; player 1 breaks off with a movement key
- player 2's second kill of the species is paid to it, and its own HUD shows it
- player 1 is paid nothing for the blows it struck
- the original Save completes after player 2's kill
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
- player 1: the original charges its attacks nothing, and every game holds the same energy for it
- player 2: the original charges its attacks nothing, and every game holds the same energy for it
- player 1's attack is not cancelled by player 2's fight
- authority: no native_relationship_acceptance_failed
- authority: no native_history_acceptance_failed
- authority: no network_rejected
- player-1: no native_relationship_acceptance_failed
- player-1: no native_history_acceptance_failed
- player-1: no network_rejected
- player-2: no native_relationship_acceptance_failed
- player-2: no native_history_acceptance_failed
- player-2: no network_rejected
- player 1: its interface was read whole each time
- player 2: its interface was read whole each time
- the save holds player 2's relationships and its history of two first kills, player 1's untouched
- the authority counts the kill as player 2's next of the species and leaves player 1's count as it was

## Not covered

- The kill that makes a species extinct for player 2, and its hunt goal's reward: NOT_RUN; prerequisite: C06: the second player's own goals

## Windows

- Parking display: [0, 0, 1920, 1080]
- A game held the foreground when checked: False
- player-1: first visible at [0, 6, 1286, 755], on the parking display True, held the foreground when first seen False (user idle 7713.7 s)
- player-2: first visible at [317, 168, 1603, 917], on the parking display True, held the foreground when first seen False (user idle 7715.9 s)
- authority: first visible at [634, 331, 1920, 1080], on the parking display True, held the foreground when first seen False (user idle 7718.9 s)

