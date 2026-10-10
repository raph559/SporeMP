# Native trial runner — 2026-10-07

## Result and scope

Request: find a way to test the mod for real without opening the game on the
desktop and watching it through desktop control, then build the first step: a
reusable trial runner that closes on its own evidence, with game windows on a
virtual display, then a replay suite and generated records. Result: **the
runner completed one native trial (trial151, PASS); opening the game off the
primary display and without sound is not solved.** No milestone status changes. M11 remains IN_PROGRESS
with 0/12 complete gates.

No `src/` file, native binding, bridge build or evidence class changes.

## What was measured first

From file timestamps of the 131 launched M11 trials (2026-09-27 to 10-02) under
the private trial folder:

| Measure | Value |
|---|---|
| Launch to closure | median 194 s, 90th percentile 291 s (112 trials with a closure record) |
| Start of one trial to the next, same working stretch | median 487 s, mean 569 s (122 gaps) |
| Launch to first accepted scenario input | 41.6 s (trial143), 73.4 s (trial146) |
| Closure and verification | 8-10 s |
| One-off scripts written for these trials | 285 (127 start, 96 review), 1510 KiB |

In the one increment with complete tool-level timing (2026-10-02, trials
148-149, 37.6 minutes): 76% was the assistant reasoning and writing between
tool calls, 13% waiting on the running games, 5% builds; no desktop-control
call was made and 11 still images were read. Tool-level timing exists only for
the sessions that ran trials 110, 148 and 149.

Prior limits that still hold: a never-activated private desktop fails renderer
initialization ([1001], M04) and a minimized game stops updating.

## Implemented

- `tools/native/native-trial.py`: `run` and the internal `watchdog`.
- `tools/native/virtual-display.py`: `version`, `monitors`, `hold`.
- `tests/unit/test_native_trial.py`, `tests/unit/test_virtual_display.py`, `tests/unit/test_replay_suite.py` (110 tests).
- `tests/engine/scenarios/`: three scenarios and the replay manifest.
- `docs/testing.md` section and `docs/sources.md` S12.

## Commands and results

| Command | Exit | Expected | Observed |
|---|---|---|---|
| `python -B -m unittest tests.unit.test_native_trial tests.unit.test_virtual_display tests.unit.test_replay_suite` | 0 | all pass | 110 tests, 1.7 s, OK, 1 skipped (no retained evidence in this worktree) |
| `python -B -m unittest discover -s tests/unit -p "test_*.py"` | 0 | no regression | 430 tests, 13.7 s, OK, 2 skipped |
| `python -B tools/native/virtual-display.py version` | 0 | installed driver answers | `driver_version` 45, supported |
| `python -B tools/native/virtual-display.py monitors` | 0 | two attached displays | 2560x1440 primary and 1920x1080 secondary; no virtual display |
| Replay of the scenario's `drive` and `review` on retained trial149 traces | 0 | pass | 5/5 live expectations, 32/32 review checks, 21/21 standard checks |
| Same replay on retained trial148 traces (the known failed presentation) | 0 | fail | `drive` times out waiting for the populated card; review fails "B's original card text was repopulated" |

Each new test was run and seen failing before its implementation was written.
The replay reads closed traces as if they were live; it is HOST evidence about
the tooling, not a native run.

## Native runs (2026-10-07, with the user's go-ahead for each step)

Virtual display alone: held for 6.05 s as `DISPLAY10`, 1920x1080 at (2560,0),
then gone. Expected and observed agree.

| Trial | Command | Exit | Expected | Observed |
|---|---|---|---|---|
| 150 | `native-trial.py run --scenario local/scenarios/goalcard-restore.py --name trial150 --port 27150` | 1 | PASS | No game launched. The runner blocked on a captured pipe inherited by the coordinator. After a manual `stop.request` it undid the fixture and released the display. Fix `25353db`. |
| 151 | same, `--name trial151 --port 27151` | 0 | PASS | PASS, 59/59 checks, 68.2 s from start to verdict, `drive` 40.7 s. Windows opened centered on the primary display and were moved after 9 s. App update counters advanced on the virtual display; three captures of 40 s each completed. |
| 152 | same, `--name trial152 --port 27152`, with windows moved as soon as they existed | 1 | PASS, windows never shown on the primary display | FAIL: authority frozen on an intro frame, never ready; `drive` timed out at 90 s. Each window was first seen hidden, received 8-10 move requests over about 2 s, and was shown on the primary display before any was applied. One game held the foreground when checked. |
| 153 | `native-trial.py run --scenario tests/engine/scenarios/goalcard-restore.py --name trial153 --port 27153` (bridge 0.0.184, commit `1fc7c20`) | 0 | PASS, every window first visible on the virtual display | PASS, 61/61 checks, 70.7 s. First visible rectangles 2560,0 / 2877,162 / 3194,325, all on the virtual display. Each bridge: `attached`, 1 call, 1 redirect, last request 1280x720. `-noSound` passed three times. One game held the foreground when first seen. |
| 154 | same scenario, `--name trial154 --port 27154`, session display held by `display start` (bridge 0.0.185, `7a0bc45`) | 1 | PASS with no game foreground | FAIL 61/62: all three games foreground when first seen although each bridge reports one non-activating show. The session display was reused; nothing was added or removed around the trial. |
| 155 | same, `--name trial155 --port 27155` (bridge 0.0.186, `7d1b561`) | 1 | PASS with no game foreground | FAIL 61/62: same; `activations_suppressed` 0 in every process. |
| 156 | same, `--name trial156 --port 27156` (bridge 0.0.187, `e0c0f68`) | 0 | PASS with no game foreground | PASS 62/62, 69 s; no game foreground when first seen or when checked; `repositioned_without_activation` 0, `activations_suppressed` 0, `foreground_after` none. Why this run differs is not established. |
| 157 | same, `--name trial157 --port 27157`, started after the user's go (bridge 0.0.188, `12a803a`) | interrupted | a labelled foreground result with the user idle | The user interrupted the tool call after launch. The runner and its watchdog were killed; three games, three hosts and the coordinator kept running past the deadline. Closed with `native-trial.py watchdog --trial local/native-trials/trial157` (exit 0, closure clean), `durable-checkpoint.py undo` (journal `originals_restored`), `display stop`. No verdict, placement record or captures. Bridge reports: player-1 `foreground_after` = show_window, others none; all counters of blocked activation calls 0 at start-up and at session end. |

Every launched game and host exited 0 and every fixture journal ended
`originals_restored`. The user reported seeing the windows on the main display
in trials 151 and 152 and asked for no sound. Trial153 addresses the first for this
fixture, and the user confirmed afterwards that no game window appeared and no game
sound played. They did notice the Windows new-display sound and animation and the
mouse jumping to the middle of the main display.

Replay suite (`replay-suite --manifest tests/engine/scenarios/replay-suite.json`,
exit 0, about 6 s): 9 of 9 pairs as expected, including trials 151 (PASS) and
152 (FAIL). HOST evidence about the scenarios, not a new native run.

## Still open

| Item | Status | Next experiment |
|---|---|---|
| Game window opening on the virtual display | DONE for the recorded fixture (trial153) | Repeat across a few runs; the 50 ms poll cannot rule out a shorter appearance elsewhere |
| Game sound | DONE for the recorded fixture: user heard nothing in trial153 | — |
| New-display sound and animation at every trial | DONE: `display start` holds one display for the session (trials 154-156 reused it) | — |
| Mouse jumps to the middle of the primary display | EXPLAINED, left as is by the user's choice | Caused by Windows when the virtual display is added; once per session with `display start` |
| A starting game takes the foreground when the user is idle | NOT PREVENTED, left as is by the user's choice | The game takes it before the bridge's init callback; the activation hooks have no shown effect |
| Watchdog died with an interrupted runner (trial157) | FIXED in tooling, HOST-tested with real processes | Not yet exercised by a real interrupted trial |
| Authority stuck on the intro in trial152 | UNEXPLAINED, one occurrence | Repeat with the trial151 placement before blaming the early move |
| Cadence over a real increment | NOT MEASURED | Use the runner for the next M11 increment |
| Recorded data as fixtures for the mod's own C++ logic | NOT STARTED | Start with the history wire format and A's real saved entries |
| CTest `tooling_unit` through the build tree | NOT RUN here | Run in the checkout that has a build tree |

## Pins

- Base commit `10d5b539bb465c2503e3c967e8165a1e626e252a`; SDK commit
  `cbf9206b9a823f0911cd9be0217104a49d72380b`; no executable or content fingerprint
  was exercised.
- Virtual display protocol reference: `nomi-san/parsec-vdd` at
  `a827c7137659b618d0a65f261ad8b2da1c74f772` (MIT); installed adapter driver 0.45.
- Windows 11 Pro 10.0.26200, AMD Ryzen 7 9700X, NVIDIA RTX 4080 SUPER, Python 3.11.15.
- Tool and test hashes are recorded per trial in its `source-hashes.json`; commits `f4a2e03`, `25353db`, `d5c05cb`.

## Next smallest step

Static pass done (research log R09) and applied in bridge 0.0.184. Build exit 0;
CTest 20/20 in 68.25 s including `native_window_host` and the replay suite.
Bridge 0.0.188 builds and passes CTest 20/20. Next: measure cadence on the next
M11 increment with the runner, a session display and the replay suite; then
start feeding recorded native data to the mod's own C++ logic in host tests.
