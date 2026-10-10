# trial241 - ability-key.py

**Outcome: PASS** (40 of 40 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `74ede8d5f339f42bd64b735222603dfc7ac1fe62`; 15 frozen files
- player-1: bridge 0.0.218, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 22934 trace rows, trace SHA-256 `f262b5f1a075b3087452d4b0978e53716e0044d33e0bcbb84e97f1fd063520f6`
- player-2: bridge 0.0.218, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 17142 trace rows, trace SHA-256 `8f623f88b959d178bffe38a871f3169951ea6c3195d07d08a44975d3b9a62295`
- authority: bridge 0.0.218, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 55251 trace rows, trace SHA-256 `049e4d680e77e331ad513f051ae6d6f2208ac9bd579e760ccbe299c8a5b0d49b`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 4.9 |
| freeze | 0.1 |
| display | 0.0 |
| server | 0.4 |
| workers | 4.5 |
| park | 8.1 |
| captures | 0.0 |
| drive | 214.7 |
| close | 4.8 |
| server_stop | 0.0 |
| captures_finish | 2.2 |
| display_release | 0.0 |
| fixture_undo | 2.1 |
| review | 1.6 |
| total | 243.5 |

## Passed checks (40)

- player 1 walks up to a creature of another species and its click picks exactly that creature
- after the first-contact cinematic and tutorial player 1 selects a creature of that species again
- player 1's ability key reaches the authority as its own command aimed at a creature
- the authority runs player 1's original attack and the target loses health
- player 1's creature kills the target and the original pays out the kill
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
- player 1: pointer messages posted in its own process
- player 1: every accepted creature point lies in the part of its view the HUD leaves free
- authority: no native_relationship_acceptance_failed
- authority: no native_unowned_movement_unstoppable
- player-1: no native_relationship_acceptance_failed
- player-1: no native_unowned_movement_unstoppable
- player-2: no native_relationship_acceptance_failed
- player-2: no native_unowned_movement_unstoppable

Observation (ACCEPTED): A starting game was the foreground window when it appeared: player-1, player-2, authority. Known and left as is: happens when the user has been idle.

## Not covered

- Player 2's ability key, the other ability keys, the social stance, right click, nests, fossils and food: NOT_RUN; prerequisite: later C01 increments

## Windows

- Parking display: [0, 0, 1920, 1080]
- A game held the foreground when checked: True
- player-1: first visible at [0, 6, 1286, 755], on the parking display True, held the foreground when first seen True (user idle 7088.1 s)
- player-2: first visible at [317, 168, 1603, 917], on the parking display True, held the foreground when first seen True (user idle 7089.6 s)
- authority: first visible at [634, 331, 1920, 1080], on the parking display True, held the foreground when first seen True (user idle 7091.2 s)

