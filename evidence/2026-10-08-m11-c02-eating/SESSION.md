# M11 C02 eating, first increment - 2026-10-08

Follows the closure of C03 ([record](../2026-10-08-m11-c03-combat/SESSION.md)). Bridge 0.0.239 to 0.0.248 (wire schema
29 to 31), trials 364 to 408 (2026-10-08), distinct-species fixture (trial146), the checkpoint sealed by trial370, and for
the two carnivores the one-species fixture (trial118).
No person took part: every key and click is posted in the player's own game (INPUT_IN_PROCESS); the eater's health is
read from its own HUD (UI_READBACK); hunger is each game's own value for the creature (NATIVE state); frames were
inspected by the assistant where stated.
**C02 stays OPEN** at the end of this increment; M11 remains IN_PROGRESS with 2/12 complete gates. Continued and
closed in the [second increment](../2026-10-10-m11-c02-eating/SESSION.md), which also corrects two readings made
here: the carnivore runs that left a fruit in the world had all ordered a fruit on a plant, and what each game moved
on its own is now the authority's alone.

## Result

**A herbivore player eats fruit.** Player 2 walks to a fallen fruit with its movement keys and clicks it where its own
window draws it. The click reaches the authority as that creature's intention, the authority gives the original's own
order, the creature walks over and eats: hunger 52 to 76, health 3.5 to 5.4, and its own HUD goes from 4 to 6. The
fruit leaves all three worlds, and the three games hold the same hunger and health for the creature (`eat-fruit.py`,
trial396, 46/46, [record](RECORD-trial396.md)). Before bridge 0.0.240 the click wrote an order on the client's copy of
the creature, where nothing runs it, and nothing reached the authority (trial364): a herbivore player could only
starve.

**It eats until fed, and the meal is saved and restored.** Two fruits in a row, hunger 53 to 99, health 3.5 to 7.25,
then the original Save, sealed by the runner (trial370, [record](RECORD-trial370.md); again on the final bridge in
trial400, 51/51, [record](RECORD-trial400.md)). Freshly restored from that seal, the creature has the health it was saved with, its hunger less what fell
since, and each player its DNA; once hungry it eats a whole meal in one click, 64 to 87, and the fruit leaves both
clients (trial398, 47/47, [record](RECORD-trial398.md)).

**A carnivore player is let eat none.** Player 1's click on a fruit reaches the authority the same way and the
original's order is given; its creature eats nothing, the fruit stays in all three worlds, its hunger keeps falling (trial397, 46/46,
[record](RECORD-trial397.md)).
Player 1 eats its kill by a click, health 3.5 to 10, hunger 37 to 96 (`ability-key.py` `player-1-eats`, trial402, 52/52,
[record](RECORD-trial402.md)). **A herbivore player is let eat no meat**: from the fed checkpoint player 2 kills, is
paid, and orders its creature to eat the kill; the order reaches the authority and the original lets it eat none of
it (`player-2-eats-fed`, trial403, 52/52, [record](RECORD-trial403.md)).

**The creature is sent only to the fruit the player saw.** The intention carries the fruit's place in the player's own
world, and the authority refuses a fruit of that number that lies elsewhere in its own (rule host-tested; not yet
exercised by a trial, the scenario choosing fruit both worlds hold in one place).

**The other player is fed nothing** by a player's meal: its hunger only falls (checked in each run).

**Two carnivore players and one corpse, one after the other.** In the one-species fixture player 1, starving with
3.7 health, kills a creature of another species and eats it by one click: hunger 0 to 83, health to 10, the corpse's
food from 50 to -41. Player 2 walks 118 units to the corpse with its movement keys; its six clicks on the emptied
corpse bring no order and no food. Each player's hunger is the same in the three games (`share-kill.py`, trial408,
38/38, [record](RECORD-trial408.md)).

## Product changes

| Change | Bridge | Why |
|---|---|---|
| The authority lists the plants and fruits of its world every ten seconds, read only (diagnostic), with whether a fruit is still on its holder | 0.0.239, 0.0.242 | Nothing was known of plant food |
| A client's click on a fruit is consumed and sent as its creature's intention to eat that fruit, named by the original's own number for it (wire schema 29, verb `forage`). The authority gives the creature the order the original's click handler gives: to the fruit, or to the fruit's holder with the fruit's slot when the fruit is still held; no creature target | 0.0.240 | trial364. The handler's fruit case is pinned by its bytes at D335CCh and D33600h, the holder routine by its bytes at B05800h (static exports `m07-corpse-click`, `c02-fruit-01`). Rules host-tested |
| A player's creature carries the fruit it last ate and a count (wire schema 30, packet 680 to 688 bytes); each client removes that fruit from its own world when the count rises, through the original's own removal of a noun | 0.0.241 | trial365: the eaten fruit stayed in both clients' worlds. Rules host-tested |
| Every game takes the plant food census, for all fruits; the authority says whether the orderer of a fruit that went was fed | 0.0.243 | To compare the three worlds (trial377) |
| The intention carries the place the player saw the fruit at (wire schema 31, an eighth of a unit); the authority gives the order only if its own fruit of that number lies within a unit and a half of it | 0.0.244 | trial378: each game re-places part of its fruit on its own. Rules host-tested |
| A pass-through observer on the original's routine that stocks plants with fruit records how often it runs, when the number of fruits on holders changes, and each fruit a run moved (diagnostic) | 0.0.247, 0.0.248 | Open item 2 |
| An ordered fruit is gone once its noun is, or once its orderer was fed and no fruit of that number lies where it was ordered | 0.0.245, 0.0.246 | trial383: after a restore the original re-placed the eaten fruit's noun 200 units away; the meal was whole and the clients kept the fruit. trial391: with 0.0.245's first form, a fruit that shifted on its plant while a carnivore stood by it was removed from the clients. Rule host-tested |

## What the original does, as measured

| Finding | Seen in |
|---|---|
| The fixture's world holds 5 plants, 2161 fruit groups and 148 fruits as nouns; at the start of a session the three games hold the same fruits in the same places | trials 364, 378 to 382 |
| A click on a fruit picks the fruit noun, classified 23; the handler clears the creature's target and calls the order routine C0C070 with first word 20h, the other words as for a corpse | trial364, client trace; static D32FD0 case 17h |
| A herbivore gains about 23 hunger and 1.875 health from one fruit, two seconds after an order given from three units away | trials 365 to 389 |
| The HUD shows health as a whole number rounded up (4 at 3.5, 6 at 5.375, 8 at 7.25 and 7.5), and its hunger bar grows with the meal | trials 389, 390; frames of trial382 |
| The eater's own client draws the green plus signs of healing over its creature, and the fruit is gone from its view | trial366, four frames of player 2's view |
| A carnivore ordered to a fruit eats nothing and stands by it; standing there it kept a herbivore ordered to the same fruit two units away from it | trials 367, 380, 384 |
| **Each game re-places part of its fruit on its own.** Within 90 to 130 seconds every game moved 36 to 50 of its 148 fruit nouns, most by about 500 units, to places that differ from game to game; the fruits no game moved stayed identical | trials 378 to 380 |
| The original takes an eaten fruit out of the world by removing its noun (fresh fixture, 148 to 147) or by re-placing the noun elsewhere (after a restore, 157 to 157) | trials 368, 383, 385 |
| The original gives a starving creature more than a small corpse holds: food 50 to about -40, hunger 0 to about 82; an emptied corpse takes no further order to eat | trials 407, 408 |
| In the one-species fixture player 2's own game marks its creature as the avatar and gives it the native number the authority gives player 1's; its state rows number the owners accordingly | trials 406, 407 |
| The original grows fruit anew when it loads a world: a save made with 146 fruits and 2161 groups restored with 157 and 2355 | trials 370, 371, 373 |
| A fruit on its plant hangs above the view of a creature standing under it; fruits of one plant lie so close on screen that a click aimed at one may pick its neighbour; a fruit on its plant shifts between the plant's slots | trials 372, 374, 381, 391 |

Details in [research log R13](../../docs/research-log.md).

## Open, in the order found

1. **Two creatures eating one food at the same moment is NOT RUN.** What is run, in the one-species fixture where
   both players are carnivores (`share-kill.py`, trial408): the starving player 1 kills, eats, and empties the
   corpse; player 2, arriving 118 units and a minute later, is taken no order and fed nothing. For the two to eat
   together player 2 must be at the corpse while player 1 eats, and the corpse must hold more than one meal: the
   3-health creatures near player 1 give 50 food and a starving creature takes 90. Next: have player 2 walk over
   first, player 1 kill a larger creature (the fixture has 6-health ones 35 to 47 units away), and both be ordered
   within the meal.
2. **Fruit placement is each game's own.** A growing share of the fruit a player sees is not where the authority
   holds it (27 of 148 for player 2 at the first census of trial382); from 0.0.244 the authority refuses such an
   order, so the player's click does nothing. The fruit a game moved came from one parked place and went onto plants
   14 to 250 units from that game's own avatar (trial382): the original stocks the plants around the avatar of the
   game it runs in, the authority's being player 1's creature. This is the plant side of C07 (activation around
   the second player). A static reading names the routine: B0B330h, a method of the object at 167EB34h, which
   writes a placed fruit's position and holder words; a pass-through observer saw every game call it each frame
   and put 25 fruits on holders in one run as the scene came up (trial404, bridge 0.0.247), and then move the same 31
   fruits again and again, to places that agreed with the authority's for 10 of them on player 1's client and 3 on
   player 2's (trial405, bridge 0.0.248; research log R13). Next: fence the routine on clients and see what a
   client's world then holds; share the authority's placements; then have the authority stock around both players.
3. **One run in twelve the creature stood by its fruit**: ordered as in the other runs, it walked to 0.93 units of
   the fruit and stayed in the approach behaviour for 25 seconds without eating (trial379). Expected: the meal, as
   in trial378 from the same distance. Unknown cause; the scenario now clicks again as a player would, which has not
   been needed since.
4. **A fruit still on a tall plant is not eaten.** Ordered to one through its holder, as the original's handler
   does, player 2's creature walked 6.6 units to the plant and in 28 seconds ate nothing (trial375; six frames show
   it standing under the tree, the fruit high above). Unknown whether the original lets a creature this small reach
   it. Next: the same order for a fruit on a low plant.
5. A fruit eaten by a wild creature is not reported to the clients; a client that joins after a fruit was eaten is
   not told (C10, C11).
6. The hunger bar is seen on frames only, and the eating animation on the clients was not inspected.

A by-product for later gates: trial370's sealed checkpoint holds a fed second player with 7.25 health, where the
fixture so far gave it 3.5 and three minutes to live.

## Trials

| Trial | Scenario | Outcome |
|---|---|---|
| 364 | fruit probe, bridge 0.0.239 | Readings: census; the click picks the fruit, the client's original orders locally, nothing reaches the authority |
| 365 | fruit probe, bridge 0.0.240 | PASS 35/35: eaten on the authority; the fruit stays on the clients |
| 366 | fruit probe, bridge 0.0.241 | PASS 39/39: the fruit leaves both clients; hunger agrees; frames inspected |
| 367 | fruit probe, player-1 | PASS 39/39 ([record](RECORD-trial367.md)) |
| 368 | eat-fruit, bridge 0.0.242 | PASS 43/43 |
| 369, 372, 374 | eat-fruit, from-the-plant | FAIL on the scenario's way to a hanging fruit: its distance in space, the view from under it, the neighbour picked |
| 370 | eat-fruit, fed-and-saved, sealed | **PASS 47/47** ([record](RECORD-trial370.md)) |
| 371, 373 | eat-fruit, restored | Restore check passes in 373; the scenario expected the saved fruit count (371) and the fruit to leave the world (373) |
| 375 | eat-fruit, from-the-plant | The fallen fruit is eaten; the fruit on the plant is ordered and not eaten (open item 4) |
| 376 | eat-fruit | PASS 43/43 ([record](RECORD-trial376.md)) |
| 377 | eat-fruit, restored | FAIL: the click aimed at a fruit the client did not hold there |
| 378 | eat-fruit, bridge 0.0.243 | The meal passes; the comparison of the three worlds at the end fails: fruit re-placed (open item 2) |
| 379 | eat-fruit, bridge 0.0.244 | FAIL: the creature stood by its fruit (open item 3) |
| 380 | eat-fruit, both-order | FAIL: the carnivore, ordered first, stood by the fruit and kept the herbivore from it |
| 381, 382 | eat-fruit | PASS 45/45 each |
| 383 | eat-fruit, restored | FAIL: the meal was whole; the fruit's noun was re-placed, not removed |
| 384 | eat-fruit, player-1 | PASS 45/45 |
| 385 | eat-fruit, restored, bridge 0.0.245 | PASS 46/46 |
| 386 | eat-fruit | PASS 45/45 |
| 387 | ability-key, player-1-eats | **PASS 52/52** ([record](RECORD-trial387.md)) |
| 388 | ability-key, player-2-eats | Kill and payout pass; the scenario's two clicks on the lying corpse picked nothing (its aim; the client's copy was dead 0.1 seconds after the authority's). It now tries further heights |
| 389 | eat-fruit, with the HUD readback | PASS 46/46 |
| 390 | eat-fruit, restored | PASS 47/47 |
| 391 | eat-fruit, player-1, bridge 0.0.245 | FAIL: the clients removed a fruit that had only shifted on its plant; fixed in 0.0.246 |
| 392 | eat-fruit, fed-and-saved | PASS 51/51 |
| 393, 394 | eat-fruit, player-1 and restored, bridge 0.0.246 | PASS 46/46 and 47/47 |
| 395, 399 | eat-fruit, default and fed-and-saved | FAIL on the scenario's aim: clicks at the fruit's foot, and from 1.8 units behind the creature's own body, picked nothing; it now aims higher and steps back |
| 396 | eat-fruit | **PASS 46/46** ([record](RECORD-trial396.md)) |
| 397 | eat-fruit, player-1 | **PASS 46/46** ([record](RECORD-trial397.md)) |
| 398 | eat-fruit, restored | **PASS 47/47** ([record](RECORD-trial398.md)) |
| 400 | eat-fruit, fed-and-saved | **PASS 51/51** ([record](RECORD-trial400.md)) |
| 401 | ability-key, player-2-eats, bridge 0.0.246 | FAIL: player 2's wounded creature died in its solo fight (C10) |
| 402 | ability-key, player-1-eats | **PASS 52/52** ([record](RECORD-trial402.md)) |
| 403 | ability-key, player-2-eats-fed, from trial370's seal | **PASS 52/52** ([record](RECORD-trial403.md)) |
| 404 | eat-fruit, bridge 0.0.247 with the stocking observer | PASS 46/46; read for the observer's rows |
| 405 | held-soak, bridge 0.0.248 | PASS 109/109; read for each fruit the stocking runs moved. Also the C01 soak on wire schema 31 |
| 406 | share-kill | FAIL: player 1's clicks on the small corpse picked its own creature standing over it; the scenario now steps back |
| 407 | share-kill | Player 1's meal passes; the scenario expected player 2 to be fed from an emptied corpse, and matched creatures across games by native number |
| 408 | share-kill | **PASS 38/38** ([record](RECORD-trial408.md)) |

Trials 364 to 367 ran the scenario under its first name, `fruit-probe.py`. Replay manifest: 73 pairs, all as
expected (HOST replay of retained traces); for `eat-fruit.py` it holds trials 389, 390, 392, 396, 397, 398 and 400 as
passes and 391 as a failure, and for the corpse runs trial403. Trials 387, 402 and 408 do not replay faithfully (a
pointer step times out or the replay ends early) and are not in it.

## Commands and results

| Command | Exit | Observed |
|---|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | 0 | once per version, 0.0.239 to 0.0.248 |
| `ctest --test-dir build/win32 -C Release` | 0 | 20/20 at 0.0.240, 0.0.241, 0.0.243, 0.0.244, 0.0.245, 0.0.246 (once the replay manifest held recordings of the current scenario), 0.0.247 and 0.0.248 |
| `native-trial.py run --scenario tests/engine/scenarios/eat-fruit.py [--variant <name>] [--seal] --name trialNNN --port 27NNN` | as in the table | |
| `tools/native/invoke-static-audit.ps1 -RunKey c02-fruit-01 -ReuseDatabase -Addresses B05800,D2F940,AC8010` | 0 | static export |
| the same with `-RunKey c02-fruit-02 -Addresses B04B00,B3D540` and `-RunKey c02-fruit-03 -Addresses B09790,B078E0,B07F00,B074C0,B0B330,B05850,B058E0,B04B80` | 0 | static exports |

Test-first notes: the wire rules of 0.0.240, 0.0.241 and 0.0.244, the client's step on an eaten fruit and the rule of
0.0.245 and its correction in 0.0.246 were written as failing tests first and seen to fail. The authority's order, its comparison of places and the
client's removal run only in the game and were verified by trials 365, 366 and 385. The handler's last call, D2F940h,
draws the local player's pointer feedback and is not made by the authority; a client that consumes the click does not
draw it either.

Frames inspected by the assistant: four of player 2's view around its meal in trial366; its health and hunger bars
at six moments of trial382; six of its view under the tall plant in trial375.

Executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, SDK commit `cbf9206b`, as in
each trial's record.
