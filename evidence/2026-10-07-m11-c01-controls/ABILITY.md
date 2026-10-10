# M11 C01 controls, a normal attack from first contact to the kill - 2026-10-07

Continues [POINTER.md](POINTER.md). Bridge 0.0.204 to 0.0.218, trials 197 to 241. C01 stays OPEN; M11 remains IN_PROGRESS
with 0/12 complete gates.

## Result

**With no person involved, player 1 meets another species, goes through the original's own first-contact steps, selects
the creature, presses its first attack key, and the authority plays the original fight to the kill and pays it out, with
the session intact (trials 240 and 241, PASS 40/40 each).** Player 1's own game then shows 42.75 DNA, as the authority
does, and the original's "You've discovered a new part!" cinematic.

**Each player clicking the other player's creature now picks it at the first click, six trials in a row** (209 to 214),
after a presentation defect was fixed.

Getting there exposed four product defects, all fixed and each now covered by a check:

| Defect | Seen in | Fix | Bridge |
|---|---|---|---|
| On each client the other player's resting creature was drawn away from where it stood: its model drifted about 0.12 units per second, up to 6.5 units | trial207 (frame: the pointer on an empty spot 90 pixels from the creature) | A client relocates the model of a resting replica once it is more than 0.35 units from its position, as it already did at first presentation; a gap that relocation itself leaves is not relocated again | 0.0.208-0.0.209 |
| Player 1 could select no creature at all while the second player is a foreign species: the guard reused from checkpoints is always false there | trials 215 to 220 (denial rows: only the foreign flag and its relationship-checkpoint consistency refused) | The selection view, which only retains the selected creature and refreshes the original interface, passes when those two parts alone refuse | 0.0.212 |
| A player's kill ended the session: the original swaps the model of the hunted species' nest, and the coordinator refused that update as an identity change | trials 233, 236 to 239 | The coordinator accepts a model change for a nest, which clients already apply, and for nothing else | 0.0.218 |
| A coordinator rejection did not say what it refused | trial233 | A rejection carries the kind and player of the refused message | 0.0.216 |

## What a first contact takes

Read off frames of trials 224 to 229, then scripted in `ability-key.py`:

1. Walk up to a creature of a species neither player belongs to and left-click it. The original picks exactly it.
   (The nearest creature at the restored spot is player 1's own species' alpha, an ally: abilities do nothing on it.)
2. The original plays "Who's this!? You've met a new species!" and waits for its confirm button.
3. It then shows its Stance tutorial with another confirm button, and ticks the goal "Meet another Species".
4. Choose the attack stance with its button. The number keys follow the chosen stance.
5. Select the creature again and press 1. The original dispatches its attack (ability index 5), the client sends one
   intention, the authority accepts a `combat` command for that target.
6. The authority's creature walks up and strikes; in trial241 the target went from 7.5 to 0 health, the attacker from 10
   to 5. The original pays 8.75 DNA for the kill and 20 for the hunt goal: 14 to 42.75.

Steps 2 to 4 are clicks on the original's own buttons at fixed places of the 1280x720 window, through the new worker
operation `point`.

## Other things added

| Piece | Bridge | What it does |
|---|---|---|
| Aim height | 0.0.204 | A click states how high on the creature's own box it aims. Trial195's 18 misses: player 1's idle creature had lain down in tall grass under a sleep bubble; the scenario now tries the middle, low and high |
| Keys seen as held | 0.0.205 | For digit keys, `GetKeyState`, `GetAsyncKeyState` and `GetKeyboardState` in the worker process report the key down between press and release. Added before the real cause of trial204 was known; whether the original needs it is not established |
| Species in creature samples | 0.0.213 | So a scenario can tell allies from other species |
| Worker operation `point` | 0.0.214 | Pointer messages at a place in the player's own window, for the original's buttons |
| Display kept awake and woken | - | The runner holds a display-required request during a trial and, when Windows reports the display off, sends one pointer input that moves nothing. After six idle hours the display had turned off and each recorder received a single frame (trials 197 to 200) |
| Drawn-model check | - | `pointer-player.py` fails when a client draws the other player's resting creature more than 0.6 units from where it stands over two samples; the recordings made before the fix fail it |

## Trials

| Trials | Scenario | Outcome |
|---|---|---|
| 197, 198 | pointer-player | Picks exact for both players; captures blank (display off), so FAIL |
| 199, 200, 207 | pointer-player | FAIL: 12 clicks on player 2's creature picked nothing (the drift, identified in 207) |
| 201 to 203 | pointer-player | PASS at the time; they fail the drawn-model check added later, as they should |
| 209 to 214 | pointer-player | PASS, first click for both players, six in a row |
| 204, 205 | ability-key on the other player | FAIL: the client's ability path accepts only wild creatures as targets |
| 215 to 224 | ability-key on a wild creature | FAIL: selection refused, then an ally selected |
| 225 to 229 | ability-key | FAIL: key pressed during the cinematic, then during the Stance tutorial |
| 231, 234, 235 | ability-key | PASS in its version up to "the target loses health" |
| 233, 236 to 239 | ability-key | FAIL: session rejected at the kill |
| 240, 241 | ability-key | **PASS 40/40**, through the kill and payout |

## Limits and open points

- Only player 1 is driven through the ability. Player 2's selection has its own, stricter rule (a creature of a third
  species) and is NOT RUN here. The social stance, the other ability keys, right click, nests, fossils and food are
  NOT RUN.
- The goal "Meet another Species" is ticked in player 1's own game. Whether the authority holds the same state, and what
  a restore does with it, is not examined (C05/C06).
- The first-contact buttons are clicked at fixed places read off frames, for one window size.
- The approach to a wandering wild creature still fails some runs before the click (trials 226 and 208); the two passing
  kill trials do not make `ability-key.py` a dependable regression test yet.
- What makes a replica's model drift (which original animation) is not identified; the relocation corrects it, and one
  wild creature per client keeps a constant gap that relocation does not change.
- The pointer, key-state, shield and display handling apply to placed worker processes only. Normal Play is unchanged.

## Commands and results

| Command | Exit | Observed |
|---|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | 0 | once per version, 0.0.204 to 0.0.218 |
| `ctest --test-dir build/win32 -C Release` | 0 | 20/20 at 0.0.218 |
| `native-trial.py run --scenario tests/engine/scenarios/pointer-player.py --name trialNNN` | as in the table | |
| `native-trial.py run --scenario tests/engine/scenarios/ability-key.py --name trial240`, `trial241` | 0 | PASS 40/40 each |
| `native-trial.py replay-suite --manifest tests/engine/scenarios/replay-suite.json` | 0 | every pair as expected, including trials 240 and 241 PASS and trials 233, 238, 239 failing on the session rejection |

Frames inspected by the assistant: the click frames of trials 195, 196, 207, 224, 225, 227 and 229, and player 1's last
frame of trial241 (the new-part cinematic). The generated record of trial241 is [RECORD-trial241.md](RECORD-trial241.md).

## Next smallest step

Player 2's ability on a creature of a third species, then the social stance for both players. Read the original's
interface instead of clicking fixed places.
