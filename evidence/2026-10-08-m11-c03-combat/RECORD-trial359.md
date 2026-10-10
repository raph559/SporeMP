# trial359 - ability-key.py[player-1-eats]

**Outcome: PASS** (52 of 52 checks passed). Evidence NATIVE_AND_NETWORK_REAL; input INPUT_IN_PROCESS; visual review NOT_INSPECTED; milestone acceptance NONE.

## Identity

- Source commit `ddecd026150d7c8b23f495229344dec246a8c93d`; 15 frozen files
- player-1: bridge 0.0.238, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 18425 trace rows, trace SHA-256 `9c5280d004974bf19ec684e21a0ede37752513247f965ed3cd1df8796c2bbd00`
- player-2: bridge 0.0.238, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 14151 trace rows, trace SHA-256 `80eb8a7df51f55acac7a4dbbd8096433e8d6e3b668e7ceb269c9e848f9ad6d3f`
- authority: bridge 0.0.238, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, executable `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, 35018 trace rows, trace SHA-256 `c742a0d7eb63f0f8f6d4f523c61ef2773db7d893425ca71083ffcfcb5c25a6d1`

## Phases

| Phase | Seconds |
|---|---|
| preflight | 0.1 |
| fixture_restore | 7.5 |
| freeze | 0.1 |
| display | 0.1 |
| server | 0.5 |
| workers | 7.8 |
| park | 8.1 |
| captures | 0.1 |
| drive | 173.5 |
| close | 5.7 |
| server_stop | 0.0 |
| captures_finish | 1.7 |
| display_release | 0.0 |
| fixture_undo | 2.8 |
| review | 1.2 |
| total | 209.2 |

## Passed checks (52)

- player 1's own HUD shows its DNA
- player 1 walks up to a creature of another species and its click picks exactly that creature
- player 1 confirms what the original shows on this contact
- player 1 walks away from the creature it has met before choosing to attack
- player 1 chooses the attack stance with its button and the number keys are on the attack abilities
- player 1's attack ability 2, used by its key, reaches the authority as its own command aimed at a creature
- player 1's attack ability 1, used by its button, reaches the authority as its own command aimed at a creature
- the authority runs player 1's original attack and the target loses health
- player 1 sidesteps with its own key during the fight
- player 1's attack abilities are the ones their slots stand for
- player 1 confirms what the original shows after the kill
- player 1's creature kills the target and its own HUD shows the DNA the original paid
- player 2's DNA is untouched by player 1's kill, on its own HUD and on the authority
- player 1 eats its kill by a click on it, and the authority's creature is the better for it
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
- player 1: its interface was read whole each time
- player 1: each attack it used recharges on its own client's ability bar, and a recharge ends there
- player 1: the original charges its attacks nothing, and every game holds the same energy for it
- authority: no native_relationship_acceptance_failed
- authority: no native_unowned_movement_unstoppable
- player-1: no native_relationship_acceptance_failed
- player-1: no native_unowned_movement_unstoppable
- player-2: no native_relationship_acceptance_failed
- player-2: no native_unowned_movement_unstoppable

## Not covered

- Right click, and clicks on nests, fossils and food: NOT_RUN; prerequisite: the gates that own those interactions (C02, C05, C08)

## Windows

- Parking display: [0, 0, 1920, 1080]
- A game held the foreground when checked: False
- player-1: first visible at [0, 6, 1286, 755], on the parking display True, held the foreground when first seen False (user idle 7131.9 s)
- player-2: first visible at [317, 168, 1603, 917], on the parking display True, held the foreground when first seen False (user idle 7134.7 s)
- authority: first visible at [634, 331, 1920, 1080], on the parking display True, held the foreground when first seen False (user idle 7136.9 s)

