# M11 C03 combat, second increment and closure - 2026-10-08

Follows [the first increment](../2026-10-07-m11-c03-combat/SESSION.md). Bridge 0.0.232 to 0.0.238, trials 331 to
363 (2026-10-08), distinct-species fixture (trial146) and the checkpoint sealed by trial345. No person took
part: every key and click is posted in the player's own game (INPUT_IN_PROCESS), DNA is read from each HUD
(UI_READBACK), and frames were inspected by the assistant where stated.
**C03 is CLOSED**; M11 remains IN_PROGRESS with 2/12 complete gates.

## Result

**The two players fight one creature together and the last blow's owner is paid** (`second-kill.py`). Player 2
selects a pack's alpha, player 1 attacks it by a click, player 2 joins with its attack key, player 1 stops with a
movement key once the creature is weak, player 2 kills it. Player 2's DNA rises on its own HUD and on the
authority; player 1's stays, on both. Three forms, each passing after a fresh restore on bridge 0.0.238:

| Form | What it adds | Trial |
|---|---|---|
| default | Player 2's **second kill of a species it has hunted** (Blatardo, its count 1 to 2, player 1's stays 2; 7 to 15 on its HUD) | trial360, 50/50 ([record](RECORD-trial360.md)) |
| `first-kill-saved` | Player 2's **first kill of a species while player 1 fights the same creature** (count 0 to 1, player 1's stays 0), then the **original Save**, sealed by the runner | trial361, 52/52 ([record](RECORD-trial361.md)); first on 0.0.235 in trial345 ([record](RECORD-trial345.md)) |
| `restored` | **From that save, freshly restored**: each HUD shows the saved DNA; the authority reads player 2's relationships back with the digest they were written with, and its history with both first kills; player 2's attack order on another member of the pack is accepted with its one kill of that species counted, none for player 1 | trial362, 45/45, from trial345's seal ([record](RECORD-trial362.md)) |

**A player's attacks recharge on its own ability bar.** The original draws a yellow wipe over an ability while it
recharges. The owner's client drew none (frames of trial303). From bridge 0.0.238 it draws the authority's, frame
for frame: Charge for 10 seconds and Bite for 1.6 on player 1's client (trial350, frames of the authority and the
client side by side), Bite on player 2's (trial351). `ability-key.py` checks that each attack the authority ran for
a player begins and ends a recharge on that player's client: trial356 for player 1, 50/50 ([record](RECORD-trial356.md)),
trial353 for player 2, 48/48 ([record](RECORD-trial353.md)).

**Cost.** The original charges an ability's energy when the ability ends, and charges nothing for any attack these
creatures have: one call per attack with amount 0, for both players as for wild creatures, and energy 1000 in all
three games. Both scenarios check it.

**Each player's own kill and payout, both in one session, and player 1 eating its kill** pass again on the final
bridge: player 1's kill, trial354, 51/51 ([record](RECORD-trial354.md)); player 2's, trial357, 51/51
([record](RECORD-trial357.md)); both in one session, trial363, 59/59 ([record](RECORD-trial363.md)); player 1 eating
its kill, trial359, 52/52 ([record](RECORD-trial359.md)).

## Product changes

| Change | Bridge | Why |
|---|---|---|
| The authority records why it does not admit a foreign-species command | 0.0.232 | trial331: player 1's attack was refused without a word |
| A pack's alpha is admitted as the second player's target for a later kill | 0.0.233 | trial330 refused one; the only Blatardo in the fixture is an alpha |
| The second player's first kill of a species may be an alpha's, and of a species player 1 has touched | 0.0.234 | The original's results for one were as for any member (trials 339, 340) |
| Player 1 may attack and eat while a fight of the second player is followed. Its own state is still compared: inside the second player's blow or relationship action any change is a failure; between them a change is its own once it has acted or been struck during that fight | 0.0.235 | The authority cancelled player 1's attack the moment player 2 joined (`foreign_combat_preflight`), and trial342's player 1 stood under a pack's bites until its changed relationship ended all combat. Rule host-tested |
| A followed fight whose creature another killed, or that left the world, is finished | 0.0.235 | trial338: wild creatures killed the Blatardo player 2 was fighting. Rule host-tested |
| A client records which entity of a shared scene it could not match, and why it holds no such fossil | 0.0.236 | trial346's restore was refused without a word |
| A fossil of the baseline that a client's own world does not hold is left unmirrored, as one created after the baseline already was; a fossil the client holds in another place or shape stays fatal | 0.0.237 | trials 346 to 348: restored from a save made 50 units from where the session began, neither client held one far fossil the authority's still held. Invisible and uncollectable on those clients: a C07/C08 limit |
| A player's creature carries its recharging abilities to the clients (wire schema 28, packet 664 to 680 bytes); its owner's client sets and clears them as they begin and end, and lets the original advance what it draws for that one creature | 0.0.238 | Recharge was not shown to the owner. Rules host-tested |

Runner: `run --seal` seals the save a scenario requested, once the games have closed and before the worker profiles
are put back, and only when the scenario did not fail (unit-tested).

## What the original does, as measured

| Finding | Seen in |
|---|---|
| In the attack stance, the first click on a creature selects it; the next click, or the attack's key with the pointer on it, is the order | trials 332, 339 |
| A wounded creature left by its attacker walks off faster than player 2's creature follows; at 1 health or less it runs from both players | trials 334 to 337 |
| Player 1's blows take 3 health from the Blatardo and 1 from the pack's alpha; player 2's take 1 | trials 337, 345 |
| The creature killed by player 2 after blows of both pays player 2 alone: 7 to 15.75 | trial345 |
| A click on the other player's creature opens "You've met a new species" on the clicking client and takes every later click until confirmed | trial344, frame |
| Recharge: the bit and progress the ability bar draws from, who sets and advances them | static `c03-recharge-01`, `m05-outcomes-01`; trials 303, 350 |
| Player 2's creature starves 175 seconds into a session from this fixture and dies 65 seconds later | trials 331 to 340 |

Details in [research log R12](../../docs/research-log.md).

## Handed on, with the reason

| Item | To | Reason |
|---|---|---|
| The kill that makes a species extinct for player 2, and its hunt goal's reward | C06 | The second player's own goals are that gate's subject |
| Player 2's next kill from the restored save | C02 | Its creature starves within a minute of that restore; a herbivore needs fruit, which the shared scene does not carry |
| Player 2's death | C10 | It still ends a distinct-species session ("Native scene capture failed: unavailable") |
| Spit, Strike and any ability with a cost | C08 | No creature of the fixtures has them; they come with parts |
| The two players fighting two different creatures at once | C11 | Not run; only the shared fight is |
| A fossil of the baseline that a client's world does not hold is invisible there | C07/C08 | As above |

One unexplained occurrence: in trial343 all three games stopped answering 47 seconds in, before any fight, and were
closed by the runner. It did not recur in the 20 trials since.

## Trials

| Trial | Scenario | Outcome |
|---|---|---|
| 331 to 336 | hunt-together (removed) | FAIL: three hunts in a row do not fit in player 2's three minutes |
| 337, 338 | second-kill, bridge 0.0.233 | FAIL: the wounded creature walked off; wild creatures killed it |
| 339, 341 | second-kill | PASS 47/47; player 1 struck once only, its attack cancelled by the authority, found in trial342 |
| 340 | second-kill, first-kill-shared, bridge 0.0.234 | PASS 47/47, with the same hidden cancellation |
| 342 | second-kill, first-kill-saved | FAIL: `primary_changed_during_damage`, as above |
| 343 | second-kill, first-kill-saved, bridge 0.0.235 | FAIL: the three games stopped answering |
| 344 | second-kill, first-kill-saved | FAIL: the cinematic of the met species took player 1's clicks; the scenario now confirms it |
| 345 | second-kill, first-kill-saved | **PASS 50/50**, sealed ([record](RECORD-trial345.md)) |
| 346 to 348 | second-kill, restored, bridge 0.0.235 to 0.0.236 | FAIL: both clients refused the shared scene over one fossil |
| 349 | second-kill, restored, bridge 0.0.237 | PASS 44/44 |
| 350, 351 | ability-key, default and player-2-attack, bridge 0.0.238 | PASS 49/49 and 47/47; recharge frames inspected |
| 352 | ability-key, bridge 0.0.238 with the cost check | FAIL: the creature chosen walked away for 100 units and player 1 never reached clicking range |
| 353, 354, 356, 357, 359 | ability-key: player-2-attack, player-1-kill, default, player-2-kill, player-1-eats | **PASS** 48/48, 51/51, 50/50, 51/51, 52/52 |
| 355, 358 | ability-key: player-2-kill, both-kill | FAIL: player 2's creature died in its solo fight and the session ended on the death path (C10), as in three runs of eight before |
| 360 | second-kill | **PASS 50/50** |
| 361 | second-kill, first-kill-saved, sealed | **PASS 52/52** |
| 362 | second-kill, restored | **PASS 45/45** |
| 363 | ability-key, both-kill | **PASS 59/59** |

Replay manifest: 64 pairs, all as expected (HOST replay of retained traces). It gains trials 353, 354, 356, 357,
360 to 363 and 345 as passes and 342, 346, 352, 355 as failures. Trials 303 and 304, recorded on bridge 0.0.225, are
now expected failures of the recharge check, which that bridge could not meet; 356 and 353 take their place.
Trial359's recording does not replay faithfully (a pointer step times out) and is not in the manifest.

## Commands and results

| Command | Exit | Observed |
|---|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | 0 | once per version, 0.0.232 to 0.0.238 |
| `ctest --test-dir build/win32 -C Release` | 0 | 20/20 at 0.0.238. At the first build of 0.0.238 `network_host` and `transfer_control_host` failed: the new words overran the 664-byte packet; fixed by growing it before any native run |
| `python -B -m unittest tests.unit.test_native_trial` | 0 | 137 tests |
| `native-trial.py run --scenario tests/engine/scenarios/<file> [--variant <name>] [--seal] --name trialNNN --port 27NNN` | as in the table | |
| `tools/native/invoke-static-audit.ps1 -RunKey c03-recharge-01 -ReuseDatabase -Addresses C19900` | 0 | static export |

Test-first notes: the rules of 0.0.235 and 0.0.238 and the runner's seal step were written as failing tests first
and seen to fail. The baseline fossil change of 0.0.237 reuses a host-tested rule; its engine-side use and the two
diagnostics of 0.0.236 have no host test and were verified by trials 348 and 349.

Frames inspected by the assistant: the attack slots of the authority's and player 1's bars every half second for 25
seconds after Charge in trial350, and of player 2's bar over its fight in trial351; one frame of player 1's view in
trial344 (the cinematic).

Executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, SDK commit `cbf9206b`, as in
each trial's record.
