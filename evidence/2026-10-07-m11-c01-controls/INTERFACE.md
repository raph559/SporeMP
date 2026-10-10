# M11 C01 controls, the whole ability interface, held keys and the camera - 2026-10-07

Continues [ABILITY.md](ABILITY.md). Bridge 0.0.219 to 0.0.225, trials 242 to 314. No person took part: every key
and pointer action is a message posted inside that player's own game process (INPUT_IN_PROCESS), and what a game
shows is read from its own interface windows (UI_READBACK). **C01 closes with this record**: every clause of its row has native evidence under the no-person rules, each has a
scenario in `tests/engine/scenarios/`, and each scenario passed after a fresh restore on the final bridge. M11 remains
IN_PROGRESS with 1/12 complete gates. What C01 hands to other gates is listed under Limits.

## Result

| Clause of C01 | Distinct species (trial146 fixture) | One species (trial118 fixture) |
|---|---|---|
| Held movement keys and jump key, one at a time | PASS: trials 165, 167 to 169 ([SESSION.md](SESSION.md)) and trial309 on the final bridge | not run |
| Held keys for two minutes, both players at once, two keys at once, jump while walking | PASS three runs in a row after the fix below (trials 300, 310, 311), 109/109 each; before it four runs in five failed on the camera | PASS twice (trials 312, 313), 109/109 each, with legs of 9 seconds |
| A player's camera keeps its direction while its creature moves | PASS for both players (trials 297 to 299) after the fix below | steady in every soak leg run |
| Targeting by click | Each player on the other's creature: PASS, trials 209 to 214 ([ABILITY.md](ABILITY.md)) and trial314 on the final bridge; on wild creatures in every ability run | PASS in every ability run |
| Attack abilities by key and by button, movement during the fight | PASS for both players, four runs each (trials 265, 268, 284, 303 and 266, 269, 285, 304) | not run |
| Social abilities by key and by button, jump during the exchange | refused by the authority, as recorded (trials 272, 276, 307); gate C04 | PASS for both players, three runs each (trials 275, 286, 305 and 271, 290, 306) |
| The bar's middle group (Jump, Mating Call) | PASS for both players (trials 280, 308) | not run |

## The camera defect, its cause and its fix

In the distinct-species fixture player 2's camera turned by itself whenever its creature moved: 77 degrees during a
six-unit move with no key held (trial279). Movement keys steer relative to the camera, so a held key walked the
creature in a curve or a circle: 131 degrees in five seconds in trial270, more than two full turns in thirteen
seconds in trial273. Player 1's camera did not turn, nor did player 2's in the one-species fixture. It goes back at
least to bridge 0.0.194 (trials 165 and 167) and had passed unnoticed because `held-controls.py` only measures
distance.

Cause, from static exports of the pinned executable (`m11-creature-camera*`, kept under `local/`) and then
measured. In its free mode the original creature camera reads the avatar's last native locomotion request and turns
toward that request's destination; it is the original's help for a creature walking to a clicked place. A client's
own avatar is moved by the authority's poses and normally holds no such request. Player 2's avatar in this fixture
is a wild creature that the checkpoint restored while the original AI was walking it, and which the client then
makes its avatar: the request stays on it. Each time the creature moved, the camera turned toward that fixed place.
Both cameras reported the free mode throughout (trials 295, 296), which ruled out the camera's follow mode.

Fix, bridge 0.0.225: each frame a client clears a native locomotion request found on its own avatar, with the same
pinned original `StopMovement` the authority already uses, at most 64 times per scene (host-tested rule). In
trials 297 and 298 player 2's client cleared one request of 15 words, once, and the camera turned 0 degrees in every
phase: idle, four moves with no key, held W, held D. Player 1's client had none to clear.

Tried before the cause was known and reverted: having the original leave and re-enter its active camera after the
avatar switch (bridge 0.0.223, trial281). It reset the zoom and nothing else.

## What the original does, as measured

| Finding | Seen in | Consequence |
|---|---|---|
| In the attack stance a left click on a creature is itself the order to attack it: the original dispatches Bite on the click, and the creature then stays on the player | trials 263, 264 | The scenario walks away before choosing the stance and opens from a distance |
| The original dispatches Charge only from a distance: never in the tries from 1.5 to 14.8 units, each of nine times from 15.7 to 30.6 | trials 253 to 285 | The opening attack is pressed from 17 units or more |
| The interface shows whole DNA: 42 for the authority's 42.75 | trial256 | The HUD is compared with the authority's DNA rounded down |
| A stance's abilities are the slots that carry a number-key label; the group in the middle carries none | trials 244 to 246 | Abilities are read from the interface, not from fixed places |
| The middle group holds Mating Call and, for a creature that has it, Jump | trials 274, 277 | See the Jump button below |
| The world can stop a creature whose key is still down: a tree behind player 1's starting place did for 5 to 9 seconds of a held S | trials 267, 270, 301 | The soak tells a creature stopped to the end of a leg from one that stands still |
| The camera turns toward the destination of its avatar's native walk | trials 279 to 298 | See above |

## Product changes

| Change | Bridge | Why |
|---|---|---|
| A worker reads the original's interface: every visible window with its identity, parent, box and text | 0.0.219, 0.0.220 | So a scenario finds the original's own buttons and reads its HUD (new operation `ui`, label UI_READBACK) |
| The distinct-species configuration admits each of a creature's own attacks, under the conditions Bite had | 0.0.221 | Charge was refused before the original could judge it (trial253); the admission rule is host-tested |
| The bar's Jump button is sent to the authority as that player's jump | 0.0.222 | In trial274 the original jumped the client's own copy of the creature and the authority heard nothing; the routing rule is host-tested |
| A bar ability left to the original on a client is named in its trace | 0.0.222 | Mating Call sent nothing and left no row |
| A camera reading carries the creature's orientation and the original camera's mode words (diagnostic, read only) | 0.0.222, 0.0.224 | To find the camera defect |
| A client clears a native locomotion request left on its own avatar | 0.0.225 | The camera defect; the rule is host-tested |
| A starting worker that meets another worker's game mid-backup uses the set's shared backup report; a refused shutdown is asked again for 40 seconds | - | Trials 248 and 251 failed at launch, and trial251 left a game running |
| A scenario file can be run in a named variant that overrides its constants, including the fixture | - | One restored authority lives five minutes, so each player and stance is its own run |

Not changed: social abilities between distinct species are still refused by the authority. Its relationship routing
for the second player covers the combat path only, so admitting them would credit player 2's social results to
player 1's relationships. That is C04's open work. What the control does there is recorded: the original dispatches
the ability on the client and the authority answers `baseline_not_ready`.

## Scenarios

| Scenario | What it requires |
|---|---|
| `ability-key.py` (player 1) and `--variant player-2-attack` | First contact and its two cards; walk away; attack stance by its button; Charge by key from a distance; Bite by button; health lost; a sidestep during the fight; for player 1 the kill and the HUD showing the DNA paid |
| `--variant player-1-social`, `player-2-social` | One-species fixture: social stance by its button; Sing by key; the authority hands it to the original as that player's input; jump key during the exchange; Sing by button |
| `--variant player-1-social-distinct`, `player-2-social-distinct` | The original dispatches the ability and the authority refuses it |
| `neutral-abilities.py` | The Jump button jumps on the authority and lands; every other middle button stays local and sends nothing |
| `held-soak.py` and `--variant same-species` | Eight legs for both players at once, four of them with two keys; steering without a gap and all accepted; each second of travel along the direction that player's own view asked for; jump while walking; rest after each release |
| `camera-probe.py` and `--variant player-1` | The camera turns no more than 5 degrees while the creature is moved with no key, then by held W and D; the avatar ends without a native locomotion request |
| `ui-probe.py` | Reads the HUD of both players (trial244) |

## Trials

| Trials | Scenario | Outcome |
|---|---|---|
| 242 | ability-key, earlier version | PASS 40/40 |
| 244 | ui-probe | PASS |
| 245 to 250, 253 to 264 | ability-key variants while the interface-driven version was built | FAIL: each on the step then being worked out (reselecting, the second attack, Charge's distance, rest on a slope) |
| 248, 251, 252 | ability-key | FAIL at launch: the backup race and the game it left running |
| 265, 268, 284, 303 | ability-key, player 1 attack | PASS, 49/49 |
| 266, 269, 285, 304 | ability-key, player 2 attack | PASS, 46/46 |
| 271, 290, 306 | ability-key, player 2 social, one species | PASS |
| 287 | the same | FAIL: the scenario pointed at a wild creature player 2's game did not have at that moment and waited for a row that never comes; it now waits and tries again |
| 275, 286, 305 | ability-key, player 1 social, one species | PASS |
| 272, 276, 307 | ability-key, social between distinct species | PASS: dispatched and refused |
| 274 | neutral-abilities, bridge 0.0.221 | recorded the local-only jump |
| 277 | neutral-abilities | FAIL: the scenario expected an ability on a button player 2's creature does not have |
| 280, 308 | neutral-abilities | PASS |
| 309 | held-controls, final bridge | PASS 85/85 |
| 267, 301 | held-soak, distinct species | FAIL on the tree, by the stall rule of the time |
| 270, 273, 278, 289, 294 | held-soak, distinct species, before the fix | FAIL on player 2's direction in its first leg: the camera defect |
| 292 | the same, before the fix | PASS 109/109 with the camera turning 84 degrees |
| 300 | held-soak, distinct species, after the fix | **PASS 109/109** |
| 310, 311 | held-soak, distinct species, final scenario | **PASS 109/109** each |
| 312, 313 | held-soak, one species, final scenario | **PASS 109/109** each |
| 282, 288, 291 | held-soak, one species | Legs passed for both players until the scenario's rest check failed: it read a slope's reported speed, then one sample twice, then a creature settling against a tree. Fixed in turn |
| 293, 302 | held-soak, one species | Five and seven legs passed for both players; then player 1's creature died. It was sealed starving in this fixture and loses its 7.5 health in about 150 seconds whatever it does; in trial293 the authority then exited with code 35, not examined |
| 279, 281, 295, 296 | camera-probe before the fix | readings: player 2's camera turning, player 1's steady, both in the free mode |
| 297, 298, 299 | camera-probe after the fix | PASS: 0 degrees in every phase for both players |
| 314 | pointer-player, final bridge | PASS 43/43 |

## Limits and open points

- Attack abilities, the middle group and single-key movement were not run in the one-species fixture; social
  abilities between distinct species are refused by the authority (C04).
- Player 2's kill and payout in the distinct-species fixture are left to C03, which already lists its second kill.
- Mating Call is only shown to stay on the client; what it does belongs to C09. Right click, nests, fossils and
  food belong to C02, C05 and C08.
- The one-species fixture starves player 1's creature within about 150 seconds, so its soak uses legs of 9 seconds.
  Why the authority exited after that death in trial293 is not examined (death is C10).
- The trial118 fixture was sealed by bridge 0.0.167 and restores under 0.0.221 to 0.0.225; nothing else about it
  was requalified.
- A client clears its avatar's native request each frame it finds one. Where such a request could legitimately
  belong to a client's avatar (none is known) it would be cleared too.
- Of the passing recordings made before 0.0.225 only trials 275 and 280 replay to PASS; twelve others end their
  replay before the scenario does and are not in the manifest: replaying a long pointer-driven scenario in recorded
  time is not faithful yet. Of the fifteen final-bridge recordings twelve replay to PASS and are listed; the soak
  runs 310 and 311 and the refusal run 307 do not and are not.
- `worker_host` failed once in a full host run right after a rebuild and passed alone and in every later full run.
- Whether the virtual key-state hook of 0.0.205 is needed is still not established.
- All of this applies to placed worker processes. Normal Play is unchanged.

## Commands and results

| Command | Exit | Observed |
|---|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | 0 | once per version, 0.0.219 to 0.0.225 |
| `ctest --test-dir build/win32 -C Release` | 0, once 8 | 20/20 at 0.0.222 and 0.0.225; once 19/20 (`worker_host`) |
| `pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m11-creature-camera<-part> -ReuseDatabase -Addresses <list>` | 0 | seven static exports of the creature camera and what it reads |
| `native-trial.py run --scenario tests/engine/scenarios/<scenario> [--variant <name>] --name trialNNN --port 27NNN` | as in the table | |
| `native-trial.py replay-suite --manifest tests/engine/scenarios/replay-suite.json` | 0 | 51 pairs, all as expected: twelve final-bridge recordings PASS, and trials 274, 289, 294 and 295 FAIL for their recorded reasons |

Frames inspected by the assistant: player 1's view near the end of trial267 (the tree), and nine frames of player
2's view around its first key in trial270 (the scene turning); eight frames of player 2's view across trial310 (its
creature walking through changing ground, the HUD whole) and eight across trial306 (the walk to another species'
nest, the original's social meter, DNA 17 then 27). Generated records:
[attack, player 1](RECORD-trial303.md), [attack, player 2](RECORD-trial304.md), [social, player 1](RECORD-trial305.md),
[social, player 2](RECORD-trial306.md), [middle group](RECORD-trial308.md), [single keys](RECORD-trial309.md),
[camera, player 2](RECORD-trial298.md), [camera before the fix](RECORD-trial279.md), [soak](RECORD-trial300.md),
[soak failing on the camera before the fix](RECORD-trial289.md).

## Next smallest step

C03 combat, the next gate in the order of [m11-work-audit.md](../../docs/m11-work-audit.md): the second player's
second kill, other abilities' costs and cooldowns.
