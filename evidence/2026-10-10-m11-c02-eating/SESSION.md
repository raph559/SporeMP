# M11 C02 eating, second increment and closure - 2026-10-10

Follows the [first increment](../2026-10-08-m11-c02-eating/SESSION.md). Bridge 0.0.249 to 0.0.253 (wire schema 32),
trials 409 to 456 (2026-10-10), the distinct-species fixture (trial146), the checkpoint sealed by trial370, the
one-species fixture (trial118) and the checkpoint trial427 sealed from it.
No person took part: every key and click is posted in the player's own game (INPUT_IN_PROCESS); the eater's health is
read from its own HUD (UI_READBACK); hunger, a fruit's place and state and a corpse's food are each game's own values
(NATIVE state); frames were inspected by the assistant where stated.
**C02 is CLOSED**; M11 is IN_PROGRESS with 3/12 complete gates. What the gate leaves to later gates is listed under
Limits.

## Result

**Fruit is one shared thing.** Each game used to run the original's fruit routine for its own avatar, so a growing
share of the fruit a player saw was not where the authority held it. Now the authority's place, turn, model and
state for every fruit reach the clients as scene entities, a client no longer runs the routine, and it puts back a
fruit its own game still moves. At the end of each run below, with the players at rest, every fruit at rest lay in
the same place and state in the three games: 143 to 151 fruits compared a run, 42 to 63 of them moved by the
authority since the start (`eat-fruit.py`; before, trial409: 85 of 148 elsewhere on a client after three minutes).

**A herbivore player eats, is saved and restored, on the shared fruit.** One fruit, hunger 52.7 to 76.0, health 3.5
to 5.4, its HUD 4 to 6, the fruit gone from all three worlds (trial438, 44/44, [record](RECORD-trial438.md)). Two
fruits until fed, hunger to 97.6, health to 7.25, HUD to 8, then the original Save, sealed (trial440, 49/49,
[record](RECORD-trial440.md)). Restored from the earlier seal with the health and DNA it was saved with, it eats
again, 64.5 to 87.4 (trial441, 45/45, [record](RECORD-trial441.md)).

**A carnivore player is fed nothing from fruit.** Ordered to a fruit, player 1's creature goes to it, begins to
eat and turns away; its hunger keeps falling and its HUD is unchanged (trial436, 44/44,
[record](RECORD-trial436.md), a fruit on a plant; trial456, 44/44, a fallen one). A fallen fruit it was ordered to
is taken from its place all the same (trials 419, 434, 449).

**Two players order one fruit at the same moment.** Both stand three to five units from a fallen fruit and each
clicks it in its own game; the two orders reach the authority 0.37 seconds apart and it gives both. The first
creature to reach the fruit takes it. The herbivore got there first: it is fed, 39.0 to 58.9, the carnivore is fed
nothing, the fruit is gone from its place in all three worlds, and the three games hold the same hunger and health
for the eater (`both-order`, trial452, 42/42, [record](RECORD-trial452.md); the same in trial430, the orders 0.54
seconds apart, where frames of both players' views show the fruit there before the orders and gone in both within
the same second). In the next run the carnivore, standing nearer, got there first: the original took the fruit from
its place, fed no one and ended the herbivore's order, and the three games held the same fruit afterwards
(trial453, 42/42, [record](RECORD-trial453.md); in trial449 the fruit was dealt out again 40 units away).

**Two players order one corpse at the same moment.** In the one-species fixture, from the checkpoint trial427
sealed with the two carnivores side by side, player 1 kills a creature and both players click the corpse, each in
its own game; the two orders reach the authority 0.42 seconds apart and it gives both. The original keeps one
claimant on a corpse: player 1's creature, first, eats, hunger 35.8 to 98.7, health 4 to 10, the corpse's food
100 to 11.4; player 2's is fed nothing meanwhile. Player 1 steps off, player 2 orders again and eats what is left,
10.8 to 47.7, the food going to -32. Each HUD shows its creature's health, each player's hunger and the corpse's
food are the same in the three games, and the two meals do not overlap (`share-kill.py` `together`, trial448,
43/43, [record](RECORD-trial448.md)). The claim went to player 2 when its order came first (trials 433, 435).

**The earlier meals hold on this bridge.** Player 1 eats its kill by a click, health 5 to 10, hunger 61.5 to 94.5
(`ability-key.py` `player-1-eats`, trial442, 52/52, [record](RECORD-trial442.md)); the herbivore's order to eat its
kill reaches the authority and the original lets it eat none (`player-2-eats-fed`, trial443, 53/53,
[record](RECORD-trial443.md)); of two carnivores at one corpse a minute apart the first, starving, empties it,
food 50 to -36, and the second is taken no order (`share-kill.py`, trial451, 38/38, [record](RECORD-trial451.md));
a herbivore ordered to a fruit still on its plant is fed nothing, the fallen one before it having fed it
(`from-the-plant`, trial454, 48/48, [record](RECORD-trial454.md)).

**The closed gates' runs pass on wire schema 32**: the three-minute held-controls soak (trial444, 109/109), the
ability keys (trial446, 50/50) and the shared second kill (trial450, 50/50).

## Product changes

| Change | Bridge | Why |
|---|---|---|
| A client holds the guard word of the stocking block of B0B330h at one for the length of each call, so its original stocks and puts away no fruit | 0.0.249 | Open item 2 of the first increment. Trial409: not enough, the rest of the routine still moved 86 fruits on a client |
| A fruit is a scene entity (wire schema 32: kind `fruit`, a state word carrying the original's state and five flags, in place of the eaten-fruit fields of schemas 30 and 31). The authority publishes every fruit noun with its place, turn, scale, model and state, and again when any of them changes; a fruit it no longer holds is despawned. A client binds each to its own fruit of that number, does not run B0B330h at all, and applies a sample with the writes that routine makes: the spatial object's setters for place and turn, the state word, the four flags, the model in or out of the model world followed by the call at C3FA10h | 0.0.250 | The authority's placements were not shared. The routine's entry, its return, the guard and the two calls it makes around showing a fruit are pinned by their bytes. Rules host-tested; a client's fruit update is recorded as fenced |
| A fruit's model is state: the coordinator admits a model change on a fruit, and a client gives its fruit the authority's model through the spatial object's own setter | 0.0.251 | Trial410: the original gives a fruit another model with the place it puts it; the coordinator refused the update as an identity change and the authority's session ended. Host-tested |
| A client applies a fossil, nest or fruit sample at once instead of blending it over 50 ms | 0.0.252 | Trials 411 to 413: a placed fruit took its new state and stayed where it had been put away. The first blend, a fraction of the way, was applied and the sample then skipped as seen. Host-tested |
| A client applies a fruit's unchanged sample again every half second, the fruits taking turns by number, and records each fruit it had to put back | 0.0.253 | Trial422: one fruit of 157 lay three units lower on one client, moved there by that client's own game. Host-tested |
| The scenario runner seals a checkpoint on request (`--seal`) | first increment | Used for trials 427 and 440 |

A fruit an eater took reaches the clients as the entity's removal or its new place; the per-eater field of 0.0.241
and the rule of 0.0.245 that removed an eaten fruit from the clients by name are gone. The authority still names
the fruit an orderer was fed from (`native_plant_food_eaten`), for the record.

## What the original does, as measured

| Finding | Seen in |
|---|---|
| The whole of B0B330h moves fruit, not only its stocking block | trial409 |
| It gives a fruit another model key with the place it puts it | trial410 |
| The fruit on plants is dealt out anew as the authority's avatar walks: one fruit number at three plants within thirty seconds; with the players at rest the fruit rests | trials 424, 430 |
| A fruit in state 4 is not pickable: sixteen clicks from four sides picked none. A fallen fruit two creatures walked up to was pushed two units and was in state 4 a minute later | trials 424, 439 |
| A client's own game still moves a fruit now and then, by 0.3 to 3 units; the client puts it back (0 to 4 fruits a run) | trials 422, 426, 430, 436, 440 |
| An order to a fruit runs approach then eating for either diet. The herbivore's eating ends with the fruit gone and its hunger up; the carnivore's ends in a third behaviour held seven seconds, with the fruit still there | trials 416, 429, 432, 434 |
| Once the carnivore's eating of a fallen fruit ended with the fruit gone from the world and its hunger unchanged; why is not known | trial419 |
| A fruit on this fixture's plants is eaten by neither diet: the eating behaviour runs four seconds and the fruit stays | trials 416, 429, 432 |
| Of two orders on one fruit half a second apart, the herbivore's, given first, got it | trial430 |
| A corpse has one claimant: of two orders half a second apart the first creature eats, the second's order ends at the claim | trials 433, 435 |

Details in [research log R13](../../docs/research-log.md).

## Limits, and where they go

1. **The authority stocks plants around its own avatar only**, which is player 1's creature: a player far from
   player 1 sees fruit on the plants near it only where the authority's original put some, and that fruit is dealt
   out anew as player 1 walks. What the player sees is what the authority holds, and a fallen fruit it sees can be
   eaten. Stocking around every player is C07.
2. **A fruit still on this fixture's plants feeds no creature.** Ordered through its holder, as the original's
   handler orders it, a creature runs its eating and gets nothing; the scenario records this as what the
   original does here. Whether a lower plant feeds a creature is not established; none was tried.
3. **Of two creatures ordered to one fruit, the nearer takes it**, and a carnivore that takes it wastes it. This
   is the original's rule, the same in every game.
4. What moves a fruit on a client after the authority's place was applied is not known; the client puts the
   fruit back within three quarters of a second.
5. A client that joins after the session began binds the authority's fruit by number; a fruit its own world
   holds and the authority's does not is not put away (C11). Fruit taken by wild creatures reaches the clients
   by the same entity updates and was not observed separately.
6. The stand-by of trial379, a creature ordered to a fruit and not eating it, did not recur in this increment's 18
   meals of fruit and is unexplained.
7. The hunger bar is seen on frames only (first increment); the health number is read back. Death by starvation
   and its agreement belong to C10.

## Trials

| Trial | Scenario | Outcome |
|---|---|---|
| 409 | held-soak, bridge 0.0.249 | PASS 109/109; read for the fruit: 85 of 148 elsewhere on player 2's client at the end |
| 410 | eat-fruit, bridge 0.0.250 | FAIL: the coordinator refused a fruit's model change and the authority's session ended |
| 411 to 413 | eat-fruit, bridge 0.0.251 | The meal passes; about 30 fruits the authority had placed lay at the planet's centre on both clients |
| 414 | eat-fruit, bridge 0.0.252 | PASS 44/44 |
| 415 | eat-fruit, player-1 as then written (a fruit on a plant) | PASS 44/44 |
| 416 | eat-fruit, from-the-plant | FAIL as in trial375: the fallen fruit is eaten, the one on the plant is not (limit 2) |
| 417 | eat-fruit, fed-and-saved, sealed | PASS 49/49 |
| 418, 422 | eat-fruit, restored | The meal passes; the comparison of the three worlds took each game's last census, seconds apart, and in 422 found the fruit one client had moved |
| 419 | eat-fruit, both-order as then written (the carnivore first) | The carnivore's eating ended with the fruit gone, unfed; the herbivore then found none |
| 420, 424, 429, 432 | eat-fruit, player-1 | FAIL on the scenario's way to a fallen fruit: one hidden by the player's own creature, one in state 4, a fruit hanging beyond picked instead |
| 421, 425 | eat-fruit, both-order | FAIL in the scenario: a slip in its code; a view found before the other player's turn |
| 423 | share-kill, sealed | 38/38 checks pass; no seal, the run asked for no Save |
| 426 | eat-fruit, restored, bridge 0.0.253 | PASS 45/45 |
| 427 | share-kill, saved, sealed | **PASS 39/39** ([record](RECORD-trial427.md)) |
| 428 | share-kill, together | FAIL: player 1 walked 87 units to a 3-health creature and was killed there while player 2 walked over |
| 430 | eat-fruit, both-order | PASS 42/42 |
| 431, 437 | share-kill, together | FAIL on the scenario's aim at the corpse under player 1's creature |
| 433, 435 | share-kill, together | Both orders taken half a second apart; the first creature ate, the second stood. The scenario expected both to eat at once (433); player 1, ordered second and hurt, was killed before its own meal (435) |
| 434 | eat-fruit, player-1 | The carnivore went to a fallen fruit, began to eat and turned away; the scenario expected the fruit to go |
| 436 | eat-fruit, player-1 | **PASS 44/44** ([record](RECORD-trial436.md)) |
| 438 | eat-fruit | **PASS 44/44** ([record](RECORD-trial438.md)) |
| 439 | eat-fruit, both-order | FAIL on the scenario's aim: the fruit had been pushed two units |
| 440 | eat-fruit, fed-and-saved, sealed | **PASS 49/49** ([record](RECORD-trial440.md)) |
| 441 | eat-fruit, restored | **PASS 45/45** ([record](RECORD-trial441.md)) |
| 442 | ability-key, player-1-eats | **PASS 52/52** ([record](RECORD-trial442.md)) |
| 443 | ability-key, player-2-eats-fed | **PASS 53/53** ([record](RECORD-trial443.md)) |
| 444 | held-soak | PASS 109/109 |
| 445 | second-kill | FAIL: player 1's walk to the pack took 25 steps and both creatures died before the kill, player 2 of the fixture's starvation |
| 446 | ability-key | PASS 50/50 |
| 447 | share-kill | FAIL on the scenario's aim: six clicks on the corpse in tall grass picked nothing; no card was open in its frames |
| 448 | share-kill, together | **PASS 43/43** ([record](RECORD-trial448.md)) |
| 449 | eat-fruit, both-order | Both orders taken 0.45 seconds apart; the carnivore reached the fruit first and no one was fed. The scenario expected a row naming the fruit |
| 450 | second-kill | PASS 50/50 |
| 451 | share-kill | **PASS 38/38** ([record](RECORD-trial451.md)) |
| 452 | eat-fruit, both-order | **PASS 42/42** ([record](RECORD-trial452.md)): the herbivore first |
| 453 | eat-fruit, both-order | **PASS 42/42** ([record](RECORD-trial453.md)): the carnivore first |
| 454 | eat-fruit, from-the-plant | **PASS 48/48** ([record](RECORD-trial454.md)) |
| 455, 456 | eat-fruit, default and player-1, on the scenario as committed | PASS 44/44 each; in 456 the carnivore's click picked the fallen fruit, and it was fed nothing |

Replay manifest: 80 pairs, all as expected (HOST replay of retained traces). For this increment it holds trials
414, 417, 418, 436, 438, 440 and 441 for `eat-fruit.py`, 448 for `share-kill.py`, 450 for `second-kill.py` and
444 for `held-soak.py`; the recordings of the carnivore variant as first written (391, 397) and trial400 were taken
out, the scenario now choosing other fruit. Trials 442, 443, 446, 451 and 452 do not replay faithfully (a pointer
step times out or the replay ends early) and are not in it.

## Commands and results

| Command | Exit | Observed |
|---|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | 0 | once per version, 0.0.249 to 0.0.253; before each of 0.0.252 and 0.0.253 once at the commit of its test alone, exit 1, the test not compiling |
| `ctest --test-dir build/win32 -C Release` | 0 | 20/20 at 0.0.249, 0.0.250, 0.0.251, 0.0.252 and 0.0.253, and again at the last commit of the scenarios and the replay manifest |
| `native-trial.py run --scenario tests/engine/scenarios/<name>.py [--variant <name>] [--seal] --name trialNNN --port 27NNN` | as in the table | |
| `native-trial.py replay --scenario tests/engine/scenarios/eat-fruit.py [--variant <name>] --trial local/native-trials/trialNNN` | 0 | trials 414, 417 and 418 pass under the comparison by census times (HOST replay) |

Test-first notes: the wire rules of schema 32, a fruit's model as state, the sample blend and the half-second look
were written as failing tests first and seen to fail. A client's applying of a fruit and its fence run only in the
game and were verified by the trials above.

Frames inspected by the assistant: eight of player 1's view around its order to a fallen fruit in trial419 (two
fruits, then one); twelve of each player's view around the two orders in trial430 (the fruit there, then gone in
both). Twelve closer ones of player 1's view in trial430 show player 2's creature walk up to the
fruit and the fruit gone as it arrives. Three of player 1's view at the end of trial447 show its creature in
tall grass and no card open.

Executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, SDK commit `cbf9206b`, as in
each trial's record.
