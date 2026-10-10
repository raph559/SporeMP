# M11 C01 controls, pointer input and unattended starts - 2026-10-07

Continues [SESSION.md](SESSION.md). Bridge 0.0.197 to 0.0.203, trials 170 to 194. C01 stays OPEN; M11 remains IN_PROGRESS
with 0/12 complete gates.

## Result

**A player's click, delivered inside its own game process, reaches the original's own picking, and the original names
exactly the creature that was clicked: both players, six trials (172, 178, 179, 183, 188, 191).** The scenario that
drives it is not yet a dependable regression test: it passed about half of its runs, for reasons that lie in the scene
and are listed below. **Each player clicking the other player's creature passed once (trial196)** and failed in the
run before it; from 18 units of camera distance it never worked.

Two things that blocked unattended work are fixed: the game start that sometimes stayed on the intro movie, and running
with every monitor switched off.

## What was added

| Piece | Bridge | What it does |
|---|---|---|
| Worker operation `click` | 0.0.197 | Finds where the player's own window draws an admitted creature, from the original camera rays, and posts the ordinary mouse move and button messages there. Evidence label `INPUT_IN_PROCESS` |
| Process-local pointer | 0.0.198 | While a scenario points somewhere, `GetCursorPos` in that worker process reports the same point. The real cursor is not moved |
| Free view | 0.0.199-0.0.200 | A point counts only in the part of the view the HUD leaves free (16 to 97 percent of the width, 11 to 86 percent of the height) |
| Camera rays in the trace | 0.0.201 | The screen-point row carries the camera's centre and right rays, so a scenario can steer in camera terms |
| Activation shield | 0.0.202 | A placed worker's game window is never told that it lost activation or focus |
| Box-centre aim | 0.0.203 | The aim point is the middle of the creature's own box instead of half a unit above its feet |
| Runner: `trial.click`, hover before click, replies replayed as recorded | - | A click points first and presses 0.3 s later; a replayed command answers what the recording shows the game answered |
| Runner: blank capture check | - | A trial fails when a captured preview shows no picture |
| Runner: sole virtual display | - | The display holder works when the desktop already lives on the virtual adapter alone |

## Findings, in the order they were met

| Trial | Outcome | What it showed |
|---|---|---|
| 170 | FAIL | Projection is right (own creature at 640,391), but at rest after restore player 1's camera shows no other creature |
| 171 | FAIL | A posted click alone does nothing: the original also polls the cursor (`GetCursorPos`, about a dozen callers, static reading) |
| 172 | PASS | With the process-local pointer both games pick the clicked creature (native ids 92 and 8519) |
| 173 | FAIL | A creature 24 pixels above the bottom edge is under the HUD bar; the click hit the interface |
| 174, 175, 177 | FAIL | Stepping toward the side of the screen a creature is on does not converge: the camera never turns from keys, movement is camera-relative and the camera only follows |
| 176 | FAIL | A creature 66 units away, clear of the HUD, is not picked; picked ones were 5 to 7 units from the player |
| 178 | PASS | Steering by the camera's own forward and right directions; both picks exact (101, 7425) |
| 179 | PASS on traces, blank pictures | The monitors were switched off just before; all three previews were plain white while every trace check passed. The review now fails such a trial |
| 180 | FAIL, not started | With the monitors off Windows keeps the desktop on the virtual adapter, adding the virtual monitor creates no new one, and the holder refused |
| 181 | FAIL | Monitors off, holder fixed, pictures present. A creature that stopped on a slope kept creeping at 0.01 to 0.1 units per second and never read as at rest |
| 182 | FAIL | A second creature stood in front of the clicked one and the original picked it |
| 183, 188, 191 | PASS | Monitors off; picks exact |
| 184 | FAIL, not started | Player 1 stayed on its intro movie (see below) |
| 185, 186 | FAIL | The original picked nothing, then a bush: the frame at the click shows the aim point on a large plant in front of the creature |
| 187, 189, 190 | FAIL | A herd that never parted; steps exhausted |
| 192, 193 | FAIL | Player 1 walked 63 units to 9 units from player 2's creature; six clicks on it picked nothing |
| 194 | FAIL | Wild creatures stayed drawn around player 2's creature; no click was made |
| 195 | FAIL | From closer (camera 14.6 units) player 1's second click picked player 2's creature exactly. Player 2's 18 clicks on player 1's creature from 14.3 units picked nothing |
| 196 | **PASS 41/41** | Player 1 walked 63 units to player 2's creature and its click picked exactly it (camera 13.3 units); player 2's click picked exactly player 1's (13.1 units) |

## The intro stall: cause found, fix in place

Bridge 0.0.196 began recording what each worker window is told. Over 57 observed game starts, every game that reached
its menu was either not deactivated before the menu (20) or deactivated within 3.1 seconds of opening (36). The one game
that stalled (trial184, player 1) was deactivated 3.7 seconds after opening. The next game's window normally appears 1.4
to 2.2 seconds later and takes the activation early; in trial184 it came 3.7 seconds later. For the last game launched
only the user's own activity deactivates it, which is why the stall followed the user being at the PC.

Bridge 0.0.202 keeps the loss of activation and focus from reaching a placed worker's window. Since then: 24 game
starts in trials 187 to 194, no stall, 48 deactivation messages withheld. That is consistent with the cause and not yet
proof of the fix: four stalls were seen in about 33 earlier trials.

Ruled out on the way: the original's per-frame background sleep (15 ms; see SESSION.md).

## Working with the monitors off

The user switched the physical monitors off between trials 178 and 179. Verified since: games run, keys and clicks are
delivered and picked, traces close cleanly and previews show pictures (trials 181 to 194 all ran in that state). Two
changes were needed: the display holder takes the virtual adapter's own display when it is the only one, and the
review detects blank previews. A topology change during a trial still spoils that trial's pictures, as in trial179.

## Limits and open points

- The wild-creature scenario (`pointer-target.py`) depends on where creatures and plants happen to be; it is evidence,
  not a regression test. It is no longer in the replay manifest because the scenario changed after its recordings.
- Clicking the other player's creature: picked from 13 to 15 units of camera distance (trials 195, 196), never from 18
  (trial193, six clicks). In trial195 player 2's 18 clicks on player 1's creature from 14.3 units picked nothing,
  which is not explained; one pass does not make `pointer-player.py` dependable either.
- No ability has been used on a clicked target yet; right click and clicks on nests, fossils and food are NOT RUN.
- The free-view margins are fixed fractions read off frames, not the original's own hit test.
- The pointer and shield apply to placed worker processes only. Normal Play is unchanged.
- The unattended start fix has 24 starts behind it.

## Commands and results

| Command | Exit | Observed |
|---|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | 0 | once per bridge version, 0.0.197 to 0.0.203 |
| `ctest --test-dir build/win32 -C Release` | 0 | 20/20 at 0.0.203 |
| `native-trial.py run --scenario tests/engine/scenarios/pointer-target.py --name trialNNN` | 0 for 172, 178, 179, 183, 188, 191; 1 otherwise | as in the table; trial179 now fails its review on the blank previews |
| `native-trial.py run --scenario tests/engine/scenarios/pointer-player.py --name trial192` to `trial196` | 1, and 0 for trial196 | as in the table |
| `tools/native/invoke-static-audit.ps1 -RunKey mouse-position-flow -ReuseDatabase -Addresses ...` | 4 | partial static export (two addresses are inside functions already exported); cursor accessors and their callers listed |
| `native-trial.py replay-suite --manifest tests/engine/scenarios/replay-suite.json` | 0 | 26/26 pairs as expected, including trial196 PASS and trial195 FAIL |

Frames inspected by the assistant: the frame at the failing click of trial186 (aim point on a bush in front of the
creature), the frame at the last click of trial193 (aim point in a nest area with several creatures, nothing clearly
under it), the filmstrip of trial165 player 1 and the blank filmstrip of trial179 player 2.

## Next smallest step

Look at the frames of trial195's missed clicks on player 1's creature, and repeat `pointer-player.py` until its misses
are explained. Then one ability used on a clicked target through its own key, for both players.
