# M11 C01 controls, first increment — 2026-10-07

## Result and scope

First work under the 2026-10-07 acceptance amendment (`GOAL.md`): no person takes part, and gates are closed one at a time,
C01 first. **Bridge 0.0.190-0.0.194: a player's key input is delivered inside that player's own game process; both players'
held movement keys and jump key pass in the pinned Creature fixture; and a player's creature no longer moves before its
owner has commanded anything (trial165, PASS 82/82).** C01 stays OPEN; M11 remains IN_PROGRESS with 0/12 complete gates.

## What was added

| Piece | Bridge | What it does |
|---|---|---|
| Worker operation `input` | 0.0.190 | Posts one ordinary `WM_KEYDOWN` or `WM_KEYUP` (W A S D, Space, digits 1-9) to the player client's own game window, where a physical key arrives. The bridge's existing key capture and everything after it are unchanged. Evidence label `INPUT_IN_PROCESS` |
| Runner `trial.key`, `trial.hold` | — | Hold a key for real time in one player's game and always release it; the verdict names the input classes used |
| Late fossil tolerance | 0.0.191 | A fossil the original authority creates after the baseline is recorded as unmirrored on clients and no longer ends the session |
| `qpc_origin` in the trace header | 0.0.192 | Each trace states where its start lies on the performance counter all processes share |
| Aligned replay | — | A replay places the three traces on one timeline, moves recorded time to the row where the game received each replayed key, and steps finer than a live run |
| Unowned-movement stop | 0.0.193-0.0.194 | Authority only. Until its owner's first command in a scene, a player's creature that starts moving is stopped through the same native stop a key release uses |

## Trials

| Trial | Bridge | Outcome | What it showed |
|---|---|---|---|
| 161 | 0.0.190 | FAIL | Held W reached the authority and moved A 6.4 units in 1.3 s; the authority then created a fossil near A and the client quarantined with "Native entity spawn failed" |
| 162 | 0.0.191 | PASS 77/77 | All keys for both players. Its recording cannot be replayed faithfully (no shared time origin) and it predates the checks added below, which it would fail |
| 163 | 0.0.192 | FAIL | "player 2 was not moved by player 1's W": B moved 0.79 units while only A held a key |
| 164 | 0.0.193 | FAIL, not reached | The authority game stayed on its intro movie and never reached the menu; separately, the new stop ran inside both client games |
| 165 | 0.0.194 | **PASS 82/82** | Everything below |
| 166 | 0.0.195 | FAIL, not reached | The authority stayed on its intro movie again, with the background-sleep bypass attached |
| 167, 168, 169 | 0.0.196 | **PASS 82/82** each | The same scenario three more times, with the startup message observer |

## What trial165 establishes

For each of player A and foreign-species player B, from the checkpoint sealed by trial146, 109.3 seconds unattended:

| Check | A | B |
|---|---|---|
| Does not move before its owner's first command | 0.0 units, no stop needed | 0.07 units, one stop |
| Held W for 4 s moves its own creature | 14.2 units | 6.6 units |
| Held A, S, D for 2 s each move it | 6.4, 1.8, 1.7 units | 2.4, 4.6, 2.0 units |
| Each key stayed down for the whole hold | 4.14 s and 2.10-2.11 s | 4.05 s and 2.07-2.09 s |
| It comes to rest after release and stays there | drift below 0.001 units | same |
| The other player's creature is not moved | below 0.001 units every time | same |
| Space reaches the authority as that player's own jump | accepted, jumps and lands | accepted as a request; the original refuses the jump (no jump ability in this fixture) |
| Does not move during 10 s with both players idle | 0.00003 units | 0.0 units |

The client window procedure saw each key go down then up in order, and the authority ran 127 and 115 steering commands
from them. No reward, relationship failure or network error occurred; A ends with 14 DNA and B with 7, both alive.
Frames inspected by the assistant: player 1's filmstrip shows the intro, title and galaxy, then the Creature scene with its
HUD, the view moving across the cells of player 1's own keys and steady afterwards. The other two were not opened.

## Finding: the original AI moved B without its owner

In every restored trial so far, 0.3 seconds after the authority gives B's creature the original avatar behaviour root, the
original starts a walk for it: 7.4 units at 2.5 units per second to the spot where a passing animal of another species
(native id 8519) stood at that moment. B's owner had sent nothing. Trial146's last orders for B pointed the other way, so
it is not a resumed order. Earlier scenarios waited for both creatures to be at rest and so never saw it. In trial163 the
walk paused short and finished 3 seconds later, while A held W, which the scenario caught.

The stop is narrow on purpose. It applies only until the owner's first command in a scene, because after that the
owner's own orders (walk to a target, pickup, attack approach) legitimately keep the creature moving. One native stop at
28.59 s ended the walk in trial165 (desired speed 2.5 to 0) and it did not restart. What starts the walk inside the
original is not identified. Whether the original starts such movement again later, long after a command, is not known;
the 10-second idle check is the only evidence and C11's long session must cover it.

## Open: the authority sometimes stays on the intro movie

Trial164's authority showed one frozen intro frame for 90 seconds and never announced its menu; trial152 ended the same
way. Trial158's authority made no progress for its first seconds. In all recorded cases the user was active on the PC at
launch (idle 0.0-1.2 s), while trials launched with the user idle for 5 s to 13 min started normally; trial161 started
normally with the user active. Missing capability: the cause, and a start that does not depend on it.

One explanation is ruled out. Static reading shows that every frame the original checks the foreground window and, when
another process owns it, sleeps for a configured time. Bridge 0.0.195 replaced that routine in workers by a frame
counter. Trial166 stalled all the same: the stored sleep was 15 ms, and the stalled authority ran 5557 frames in 91
seconds. The bypass was reverted.

Bridge 0.0.196 instead records what a placed worker's game window is told from the moment it exists: activation, focus,
size and system commands, and that a key or button arrived, never which key. Three normal starts are on record
(trials 167 to 169, the first with the user active): each window gets its size and show messages, then at most one
deactivation, and reaches the menu 25.8 seconds after its own start. No stalled start has been recorded with the
observer yet; the next one will show what differs. Until then a trial that ends with "all peers ready on one scene"
is a start failure, not a scenario result, and is simply run again.

## Limits

- C01 still needs: every ability through its own key or click, mouse targeting, movement during an interaction, a longer
  soak, and B's jump in a fixture where B can jump.
- A fossil created during play is invisible and uncollectable on clients until a client-side fossil factory is qualified
  (C07/C08). Each client recorded one in trial165.
- Keys are posted to the game window; the Windows input queue, keyboard driver and focus handling in front of it are not
  exercised. The game window does not need focus.
- One fixture; the passing scenario ran four times (trials 165, 167, 168, 169).
- Recordings made before 0.0.192 are replayed as if the three games had started together, and the verdict says so.

## Commands and results

| Command | Exit | Observed |
|---|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | 0 | once per bridge version, 0.0.190 to 0.0.194 |
| `ctest --test-dir build/win32 -C Release` | 0 | 20/20 at 0.0.194 after this record's manifest change; at 0.0.193 `tooling_unit` failed on the manifest entry for trial162, see below |
| `native-trial.py run --scenario tests/engine/scenarios/held-controls.py --name trial161 --port 27161` | 1 | FAIL, fossil |
| same, `trial162` | 0 | PASS 77/77, 100.4 s |
| same, `trial163` | 1 | FAIL, B moved without its owner |
| same, `trial164` | 1 | FAIL, authority never ready |
| same, `trial165` | 0 | PASS 82/82, 109.3 s, input class `INPUT_IN_PROCESS` |
| same, `trial166` | 1 | FAIL, authority never ready (bridge 0.0.195) |
| same, `trial167`, `trial168`, `trial169` | 0 | PASS 82/82 each (bridge 0.0.196) |
| `tools/native/invoke-static-audit.ps1 -RunKey input-focus-sites -ReuseDatabase -Addresses ...` and `-RunKey background-throttle` | 0 | static decompilation of the cursor, foreground and background-sleep routines; outputs under ignored `local/m03-static/` |
| `native-trial.py replay-suite --manifest tests/engine/scenarios/replay-suite.json` | 0 | 24/24 pairs as expected: trials 165 and 167 to 169 PASS 82/82; trials 161, 163, 164, 166 fail for their recorded reasons |

A manifest entry expecting trial162 to replay as PASS was committed by mistake in `8946cd5` and made `tooling_unit` fail
until this record replaced it with trial165. Every trial closed with exit 0 for all three games and the fixture undone.
The generated record of trial165 is [RECORD-trial165.md](RECORD-trial165.md). Commits `109c32c`, `b6fbb4b`, `e6636fb`,
`8946cd5`, `89ffea9`, `864c74e` and the one carrying this record; pins as in the
[relationship snapshot session](../2026-10-07-m11-relationship-snapshot/SESSION.md).

## Next smallest step

Mouse input in process (move, button down and up on the game window), then one ability used on a target through its own
click and key for both players.
