# M11 C03 combat, first increment - 2026-10-07

Follows the closure of C01 ([INTERFACE.md](../2026-10-07-m11-c01-controls/INTERFACE.md)). Bridge 0.0.226 to 0.0.231,
trials 315 to 330 (2026-10-07 and 08), distinct-species fixture (trial146). No person took part (INPUT_IN_PROCESS, UI_READBACK).
**C03 stayed OPEN** at the end of this increment; it was closed by
[the second](../2026-10-08-m11-c03-combat/SESSION.md), which also resolves items 1 and 3 to 6 of the list below.

## Result

**Each player kills a creature of a species it has not hunted, is paid for it, and the other player's DNA is
untouched, on its own HUD and on the authority.** Player 1: trial316, 14 to 42 on its HUD (42.75 on the authority).
Player 2: trial317, 7 to 14; its history goes from one entry to two on the authority and on its own client.

**Both players hunt in one session, one after the other** (trial329, PASS 56/56): player 2 kills, 7 to 14; player 1
then meets another species, orders its attack by a click, kills, 14 to 21; each one's DNA is untouched by the other's
kill, on both HUDs and on the authority. Before bridge 0.0.231 player 1 could not attack again in a session once
player 2 had fought.

**Player 1 eats its kill by a click on it** and recovers: health 4.5 to 10, hunger 65 to 95 (trial324).

Player 2's kill failed before bridge 0.0.226 (trial315): the original added a second first-kill event to its
history, correctly, and the bridge admitted a history of one event at most, in its acceptance check, its packet,
its client display and its checkpoint words.

## Product changes

| Change | Bridge | Why |
|---|---|---|
| The second player's history holds up to eight first-kill events: acceptance of the original's result (one event more), packet (schema 27), snapshot, client display, checkpoint words (the eighteen-word form of no or one entry is unchanged) | 0.0.226 | trial315; rules host-tested |
| A later kill of a species the second player has already hunted, short of extinction, is an admitted branch; the kill that reaches extinction is not | 0.0.227 | Its second Blatardo was refused (trial261). Only the branch's damage step is observed so far: values unchanged, as expected (trial318). **Its kill step is NOT RUN** |
| The second player may turn to another creature once nothing of its last fight is in flight or still watched | 0.0.227 | A session followed one creature only. Observed in trials 318 and 320 |
| The second player's death ends the after-fight watch instead of failing the session | 0.0.228 | trial318: its unreadable state was taken for a change to player 1 |
| Either owner may ask to eat a corpse of a third species; the original decides whether that creature can | 0.0.229 | Only player 1 could ask. Rule host-tested |
| A creature's state row carries its species' mouths (diagnostic) | 0.0.230 | To learn what each player can eat |
| The second player's fight is no longer followed once its kill has been watched for three seconds | 0.0.231 | So player 1 may fight after it (trial329). Rule host-tested |

## What the original does, as measured

| Finding | Seen in |
|---|---|
| Both hunted species here make a species feared at one kill and extinct at three | trials 261, 317 |
| The first kill of a species adds one history event (category 8, event 5) whatever the history already holds | trial315 |
| Player 2's creature starts this fixture with 3.5 health; a fight costs it 3; it died at the first return blow of its second fight | trials 318, 320 |
| Player 2's creature does not regain health standing at its own species' nest: 3.5 for 50 seconds | trial319 |
| Player 1's species has a carnivorous mouth and player 2's a herbivorous one. Ordered to eat a corpse, player 2's creature goes through the motions under a question mark and eats nothing; its hunger keeps falling | trials 323, 325; frames of 323 |
| The authority refuses player 2 a pack's alpha as a target (flags bit 1); player 1 may attack one | trial330 |
| Player 2's creature survives its first fight in five runs of eight (trials 317, 318, 320, 323, 329; died in 327, 328; refused in 330) |  |
| The ability bar greys Charge while its target is too close; the client's own copy of the creature never has recharge bits set, while the authority's has them for 10 seconds after Charge and 1.6 after Bite | trials 303, 304 |

## Open, in the order found

1. **Player 2's second kill** needs its creature to survive two fights. It cannot heal at its nest, and as a
   herbivore it cannot eat its kill: it needs fruit, which the shared scene does not carry (C02). Until then the
   later-kill branch's kill step stays NOT RUN. Next: fruit for a herbivore player, then a fixture sealed with a
   healthy, fed second player.
2. **Player 2's death in this fixture ends the session**: the authority repeats its death step 2746 times
   unapplied, then reports "Native scene capture failed: unavailable" (trial320). Death is C10; noted here because a
   fight can end that way.
3. **Recharge is not shown to the owner**: nothing replicates the authority's recharge state to the client whose
   creature it is. Not yet measured as a visible difference: a case where an ability is in range and recharging.
4. Player 1 cannot attack while a fight of the second player is still followed, that is until its kill, or for good
   if it leaves a fight without one; fights at the same time are untested (C11).
5. The second player cannot attack a pack's alpha.
6. Costs, Spit and Strike, and Save and restore after the second player's combat are NOT RUN.

## Trials

| Trial | Scenario | Outcome |
|---|---|---|
| 315 | ability-key, player-2-kill, bridge 0.0.225 | FAIL: history acceptance, as above |
| 316 | ability-key, player-1-kill | PASS 50/50 ([record](RECORD-trial316.md)) |
| 317 | ability-key, player-2-kill, bridge 0.0.226 | PASS 50/50 ([record](RECORD-trial317.md)) |
| 318 | ability-key, player-2-two-kills | First kill passes; in the second fight its creature died and the watch failed the session |
| 319 | nest-heal | readings only: no healing |
| 320 | ability-key, player-2-two-kills, bridge 0.0.228 | First kill passes; its creature died again; the watch held; the session ended on the death path |
| 321, 322, 326 | ability-key, eating and both-kill runs | FAIL on two steps of the scenario itself (a renewed attack after the target had died; a sidestep blocked by the target), since made tolerant |
| 323 | ability-key, player-2-eats, bridge 0.0.229 | Kill passes; the order to eat is accepted and the original eats nothing: a herbivore |
| 324 | ability-key, player-1-eats | PASS 51/51 ([record](RECORD-trial324.md)) |
| 325 | camera-probe, bridge 0.0.230 | PASS; read for the mouths |
| 327, 328 | ability-key, both-kill | Player 2's creature died in its first fight; the session ended on the death path |
| 329 | ability-key, both-kill, bridge 0.0.231 | **PASS 56/56** ([record](RECORD-trial329.md)) |
| 330 | ability-key, both-kill | FAIL: the nearest creature was an alpha, refused to player 2 |

## Commands and results

| Command | Exit | Observed |
|---|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | 0 | once per version, 0.0.226 to 0.0.231 |
| `ctest --test-dir build/win32 -C Release` | 0 | 20/20 at each |
| `native-trial.py run --scenario tests/engine/scenarios/ability-key.py --variant <name> --name trialNNN --port 27NNN` | as in the table | |

Frames inspected by the assistant: the attack slots of player 1's bar, once a second for 32 seconds after its Charge
in trial303; six frames of player 2's view while ordered to eat in trial323 (the question mark above its creature).
