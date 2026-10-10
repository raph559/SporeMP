# Testing and evidence contract

## Current public source verification — 2026-10-11

The reviewed export of the M09, M10 and M11 source builds with this checkout's preserved Detours 4 SDK migration and
the pinned SDK model-lifetime patch. Release build exits 0; **21/21 CTest targets pass in 68.43 seconds**, among them
502 Python tests (1 skipped: the replay suite's recordings are private trial output) and the SDK wrapper target that
the development build does not have. `node website/build.mjs` and `node website/check.mjs` exit 0. Neither the build
nor a test starts SPORE. Original-game execution with the SDK built here is **NOT RUN**; the native results in this
document were recorded with the development build. [Export review](../evidence/2026-10-11-source-export/SESSION.md).

Historical raw inputs are privately archived; normal builds and automated HOST tests do not need them. Current reusable analyzers live under `tools/native/`, accept explicit archive inputs and must not treat missing private records as a passing check. Publish only reports listed in `docs/public-evidence-files.txt`; run `python tools/check-public-evidence.py` after staging the intended changes. See the [evidence index](../evidence/README.md) and [archive policy](public-evidence.md#historical-artifacts).

## SDK Detours 4 build qualification — 2026-09-21

The supported build replaces the SDK's old Detours 3 dependency with pinned MIT Detours 4.0.1. Run `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release -FetchDependencies` before building bridge/native-host targets; direct CMake builds reject missing or stale SDK provenance. The Release build and all **10/10 HOST/FIXTURE CTest targets pass**, including the actual SDK wrapper hook test and 122 Python tests. The Debug SDK/base build and its focused SDK wrapper test also pass. These are BUILD/HOST results, not original-game acceptance of the changed SDK DLL. Historical native results still refer to their recorded payloads. See [migration details](detours4-migration.md) and [the reviewed session](../evidence/2026-09-21-detours4-license/SESSION.md).

## M11 owner relationship snapshot, .189 (2026-10-07)

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release --output-on-failure
python -B tools/native/native-trial.py display start
python -B tools/native/native-trial.py run --scenario tests/engine/scenarios/relationship-snapshot.py --name trial160 --port 27160
python -B tools/native/native-trial.py replay-suite --manifest tests/engine/scenarios/replay-suite.json
```

BUILD exit 0; CTest 20/20; NATIVE/NETWORK-REAL trial160 PASS 58/58; HOST replay
16/16. Result, limits and the cadence measurement are stated once in the
[session record](../evidence/2026-10-07-m11-relationship-snapshot/SESSION.md).

## Native trial runner (2026-10-07)

`tools/native/native-trial.py` replaces the per-trial start, watchdog, close and
review scripts with one command. It restores the scenario's sealed fixture,
freezes sources and binaries, starts the coordinator and the three prepared
workers through the existing guarded scripts, drives a scenario from the live
bridge traces, closes as soon as the scenario returns (a detached watchdog
closes at the deadline otherwise), undoes the fixture and writes `verdict.json`
with every check and per-phase timings. It sends no desktop input, never retries
a mutation, and changes no native binding. `visual_review` stays `NOT_INSPECTED`
in the verdict until a person or reviewer records it; a PASS grants no milestone
acceptance.

```powershell
python -B tools/native/native-trial.py run --scenario local/scenarios/<name>.py --name trial150
python -B -m unittest tests.unit.test_native_trial tests.unit.test_virtual_display
```

A scenario is a Python file with `drive(trial)` and an optional
`review(closed)`. `ARTIFACT` names a sealed checkpoint under `local/`,
`AUTHORITY` lists authority session settings, and `DEADLINE_SECONDS` is 30-300
(default 150). `trial.wait_ready`, `trial.until`, `trial.settle`,
`trial.control` and `trial.expect` block on the live traces; `closed.events`,
`closed.one`, `closed.expect` and `closed.expect_none` read the closed ones.
Scenarios with fixture-specific identifiers stay under ignored `local/` and are
frozen into each trial directory.

With `--display virtual` (the default) the runner asks
`tools/native/virtual-display.py` to hold one temporary display on the installed
Parsec Virtual Display Adapter before any game starts and passes each worker a
position on it (`start-worker.ps1 -WindowPosition X,Y`). The launch host hands
that to the bridge, which replaces the game's own centering request, so the
window opens there. The runner records where each window first became visible
and requires each worker's app update counter to advance. Workers are started
with `-NoSound` (the original game's `noSound` switch) unless `--sound` is
given. The display disappears when the holder stops pinging the driver.
`--display none` leaves the windows where they open.

```powershell
python -B tools/native/native-trial.py display start   # one virtual display for the work session (ends by itself after 8 hours)
python -B tools/native/native-trial.py display status
python -B tools/native/native-trial.py display stop
```

While a session display is held, trials reuse it and add or remove nothing,
so Windows plays its new-display sound and animation once per session, not
per trial. A placed worker's window is also kept from being activated: inside
that process only, `ShowWindow`, `SetWindowPos`, `SetActiveWindow`,
`SetForegroundWindow`, `BringWindowToTop` and `SetFocus` are replaced for the
game window. A bridge-placed trial fails if a game was the foreground window
when it first appeared.

Every trial also gets a generated `RECORD.md` (outcome, identities, trace
hashes, phase timings, every check, what was not covered, window placement)
with no machine paths. `record --trial <dir>` prints it again.

```powershell
python -B tools/native/native-trial.py replay-suite --manifest tests/engine/scenarios/replay-suite.json
python -B tools/native/native-trial.py replay --scenario tests/engine/scenarios/<name>.py --trial local/native-trials/<trial>
```

`replay` runs a scenario's `drive` and `review` against a retained closed trial
in recorded time: rows are released by their recorded timestamps, waits and
timeouts use recorded seconds, commands are counted and not sent, and nothing
is launched or written beside the evidence. `replay-suite` does this for every
pair in the manifest and compares each outcome, and optionally its failure
reason, with the expected one. `tests/unit/test_replay_suite.py` runs the suite
inside `tooling_unit` where the retained trials exist and skips elsewhere.
A scenario presses keys with `trial.hold(role, key, seconds)` and clicks a
creature with `trial.click(role, entity)`; both are delivered inside that
player's own game process. `trial.click(..., button='none', expect=None)`
only points and returns whether the creature is in the free part of the view;
the `native_screen_point` row it leaves says where the creature is drawn and
carries the camera rays. `trial.click(..., height=20)` aims low on the
creature, and `trial.click_at(role, x, y)` clicks a place in the window, for
the original's own buttons. `trial.ui(role)` returns the original's visible
interface windows of that game (identity, parent, box, text; label
UI_READBACK), which is how a scenario finds a button or reads the HUD.
A scenario may declare `VARIANTS = {name: {CONSTANT: value}}`; `run` and
`replay` choose one with `--variant name` and a manifest entry with
`"variant": "name"`. A variant may replace the fixture and the authority
settings as well. `run --seal` seals the save a scenario requested
(`trial.control('authority', 'save', trial.scene)`) as
`<trial>/sealed-checkpoint/checkpoint.smp` once the games have closed and
before the worker profiles are put back, and only when the scenario did
not fail; a later scenario names that file as its `ARTIFACT`
(`second-kill.py`: `first-kill-saved`, then `restored`).
The standard review fails a trial whose captured
previews show no picture. The display holder also works with every physical
monitor off, when the desktop lives on the virtual adapter alone.

Each game's trace counts time from its own start. From bridge 0.0.192 the
trace header carries `qpc_origin`, and a replay uses it to put the three
traces on one timeline (`timeline: ALIGNED_BY_RECORDED_ORIGIN` in the
verdict); older recordings are replayed as if all three had started together.
A replayed `input` command moves recorded time to the row where the game
received that key, so a scenario that holds keys sees them when they arrived.
A replay checks scenario expectations against what the original game did in
that run. **It does not exercise mod code changed since the recording**; that
needs recorded data fed to the mod's own logic in host tests, or a new trial.

HOST: 110 runner, display and replay tests pass in 1.7 seconds (1 skipped
without retained evidence); the whole `tests/unit` Python suite passes 430
tests (2 skipped) in 13.7 seconds. They use synthetic traces, real child
processes for command handling, a fake control executable and a fake display
driver.

HOST REPLAY: 9 of 9 pairs give their expected outcome in about 6 seconds:
`goalcard-restore` passes trials 149 and 151 and fails 148 (card never
repopulated) and 152 (authority never ready); `subsequent-combat` passes 146
and fails on 147's different fixture; `restored-progress` passes 147 and 149
and fails 145 (A still at 7 DNA).

NATIVE, eight runner trials of `goalcard-restore` on 2026-10-07:

| Trial | Result | Observed |
|---|---|---|
| 150 | FAIL, no game launched | The runner waited on a captured pipe that the coordinator inherited; stopped by hand after 18 minutes. The runner then undid the fixture itself. Fixed in `25353db`. |
| 151 | PASS, 59/59 checks, 68.2 s | Windows moved to the virtual display 9 s after launch, having opened on the primary display. All three games kept updating there and were captured; no game held the foreground when checked. |
| 152 | FAIL after 90 s | With windows moved while still hidden, the authority stayed on one frame of the opening intro and never loaded the scene; both clients loaded. Every window was still shown on the primary display first. Cause not established. |
| 153 | PASS, 61/61 checks, 70.7 s | Bridge 0.0.184. Each game's first visible position was already on the virtual display (2560,0 / 2877,162 / 3194,325); each bridge reports `attached`, one `SetClientArea` call, one redirect. `-noSound` passed to all three; all exits 0. One game held the foreground when first seen. |
| 154 | FAIL 61/62 (foreground) | Bridge 0.0.185, session display reused. `Canvas::SetVisible(true)` replaced by a non-activating show once per game; every game was still the foreground window when first seen. |
| 155 | FAIL 61/62 (foreground) | Bridge 0.0.186, activation blocked through `ShowWindow`, `SetActiveWindow`, `SetForegroundWindow`, `BringWindowToTop`. None of the last three was called before post-init; every game was still foreground. |
| 156 | PASS 62/62, 69 s | Bridge 0.0.187, adding `SetWindowPos` and `SetFocus`. No game was foreground when first seen or when checked. The start-up counters for the added calls are 0, so the cause of the change is **not established**; it may depend on what the user was doing. |
| 157 | INTERRUPTED, no verdict | Bridge 0.0.188. The user interrupted the runner after the three games had started; its watchdog died with it and the games kept running past the deadline. They were closed with `native-trial.py watchdog --trial`, the fixture was undone by hand and the display stopped; all six exits 0. The first game's bridge reports its window foreground after its first show/hide call with no blocked activation call made. |

All launched processes exited 0, fixtures were undone and the virtual display
was released each time. Only player 2's trial151 filmstrip and the authority's
trial152 filmstrip were looked at; `visual_review` remains `NOT_INSPECTED`.
Opening on the virtual display is solved for this fixture by the bridge hook
(trials 153-156), and the user confirmed that no game window appeared and no
game sound played. Two known annoyances remain by the user's choice:
Windows moves the mouse to the middle of the primary display when the virtual
display is added (once per session with `display start`), and a starting game
can take the foreground when the user has been idle. The activation hooks do
not prevent the latter: the game takes it before the bridge gets control.
The window poll runs every 50 ms, so a shorter appearance elsewhere would not
be seen by it. The watchdog and the display holder are now started as
orphaned helpers and survive the runner's process tree being killed (host
test with real processes). `native_window_host` (43 checks) covers the hooks
on fixtures and the launch-host options. See the
[session record](../evidence/2026-10-07-native-trial-runner/SESSION.md) and
[research log R09](research-log.md).

## M11 subsequent A combat and unequal progression, .181 (2026-10-01)

Release BUILD exits0; `actor_commands_host`, `native_actor_abi_host` and
`network_host` pass3/3 in6.58seconds. No new native binding is introduced.

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release -R '^(actor_commands_host|native_actor_abi_host|network_host)$' --output-on-failure
python -B local/m11-resume-2026-09-27/review-subsequent181.py --visual-inspected-by-root
```

NATIVE/NETWORK-REAL146: six original A Bites kill a different adult, native
DNA7 becomes14, and B keeps7 while diagnostically moving8.1771655 units.
Original Save and fresh147 retain all679 player/inventory rows and unequal
progress. A's normal jump lands and distinct collections are inspected live.
All96 filmstrip cells are read; twelve zero exits, unchanged protected files
and restored profiles pass. All131/132 source/artifact entries are byte-verified.
Closed review exits0, report SHA-256:
`45d938d64699c3789a7af2a24451b108fbf36ce888417ce72229eea2f0674885`.

The report explicitly declines whole-primary history acceptance: the diagnostic
at B extension Write sees7 entries while fresh loads see5. B's history matches
exactly. Its objective UI still shows A's hunt. Both limits remain open; they
are not hidden by the bounded combat/progression result. Full M11 stays0/12.

## M11 primary feeding and saved rewards, .180 (2026-10-01)

Release BUILD exits0. Focused `actor_commands_host`, `native_actor_abi_host`
and `network_host` pass3/3 in6.66seconds before feeding trials and5.83seconds
after the exhausted-corpse restore correction. HOST admission checks preserve
B pickup refusal and reject nonzero A pickup arguments; native execution is
verified separately. No native ABI is added.

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release -R '^(actor_commands_host|native_actor_abi_host|network_host)$' --output-on-failure
python -B local/m11-resume-2026-09-27/review-primary-feed180.py --visual-inspected-by-root
```

NATIVE/NETWORK-REAL142/143 qualify normal A corpse-click feeding to100 hunger
and10 health. Trial143 saves both owners'7 DNA/progression. Fresh145 restores
all679 exact player/inventory rows, completed tutorials and B private history/
relationship without reward replay; both clients join and continue. Trial144's
negative native-food rejection remains a recorded failure. A normally jumps
and lands; B diagnostically steers/releases. All192 filmstrip cells are read.
All four runs have six zero exits, restored profiles and29 unchanged protected
files per role. Trial142 Save and145 A collection view remain NOT RUN/NOT
OBSERVED respectively. Captures cover bounded intervals, not every live action.

Closed review exits0. Its first invocation exits1 because the check expected
the last142 hunger sample to remain exactly100; the measured peak is100 and
the final sample99.9923401 after native hunger decay. The corrected check
requires the exact peak and retains the actual last value. Frozen byte-verified
source/artifact counts are123/126/127/128 for142/143/144/145.
Report SHA-256:
`7764561b221fc4b241e1ecbeefb80791fe86db49ec9c71d7a48e331a758645d6`.
Full M11 remains IN_PROGRESS,0/12 complete gates.

## M11 primary foreign-fixture combat and tutorial, .179 (2026-10-01)

Release BUILD exits 0. Focused `actor_commands_host`, `native_actor_abi_host`
and `network_host` pass 3/3 in 6.60 seconds. Tests cover the common A/B verb
admission rule, unsupported abilities/owners and independent tutorial queues.
These are HOST results; native acceptance is separate.

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release -R '^(actor_commands_host|native_actor_abi_host|network_host)$' --output-on-failure
python -B local/m11-resume-2026-09-27/review-primary179.py --visual-inspected-by-root
```

NATIVE/NETWORK-REAL trial141 records six original A Bites, one kill, 7 DNA and
normal owner First DNA completion. B moves 7.5253123 native units during A's
presentation and retains its 7 DNA/history. All 32 accepted inputs correlate
with native execution and return; B's release settles to zero velocity/drift.
Six process exits are zero, all profiles restore and protected files match.
All 192 filmstrip cells from138–141 are inspected. Failed138 history-format
admission and139–140 B-only network guard rejections remain retained.

Closed review exits 0. The initial review exits 1 because trial138's temporary
coordinator binary had been replaced before archival; its launch hash remains,
but the report explicitly excludes byte verification for that one artifact.
All 122 source/artifact entries for the qualified141 run match retained bytes.
Review report SHA-256:
`298ce01f2f6f5798ed9bb82d811680dcf0ad4968fcffb725176583bce2ac0044`.
A's new client history, new-result persistence, physical held input and later
progression remain unqualified. No full M11 gate closes.

## M11 repeated diagnostic steering, .178 (2026-10-01)

Release BUILD exits0. `network_host` passes1/1 in4.09seconds, including existing
steering codec, reserved/nonunit direction and target-smuggling checks.
`actor_commands_host` passes1/1 in0.02seconds for release/landing sequencing and
interaction fences. No new native ABI is introduced.

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release -R '^network_host$' --output-on-failure
ctest --test-dir build/win32 -C Release -R '^actor_commands_host$' --output-on-failure
python -B local/m11-resume-2026-09-27/review-steering178.py --visual-inspected-by-root
```

NATIVE/NETWORK-REAL trial137 correlates94 accepted client requests with native
queue, execution and return:90 WalkTo requests, three original releases and
one normal A jump. B moves8.2438935/8.6073925 units; A moves14.7117400. After
settling, velocity is zero and drift is below0.001. Both rendered owned models
follow; post-startup sampled model offsets are at most0.106441(A)/0.041014(B).
The jump is accepted; landing is NOT OBSERVED before closure. Physical held-key
input is NOT RUN, because the documented desktop input API only taps keys.

Trial135 rejects both unsupported diagnostic verbs before movement. Trial136
retains successful partial native steering but fails whole-run acceptance:
purple captures, a diagnostic time-budget assertion, A projection failure and
B forced exit35 following a display switch. Trial137 uses the same build,
renders normally without a display switch and closes all six processes with0.
All three trials restore their disposable profiles and preserve29 personal
files per role. All144 filmstrip cells were inspected, including failures.

Closed review exits0; its first attempt exits1 because it initially assumed
no136 network error. The correction retains the observed A projection failure.
Private report: `local/m11-resume-2026-09-27/trial137/closed-steering-review.json`,
SHA-256 `52273c4e028bb316ccb91a48f837a2b10d8e9e93f6d8d6fcdfc621fc2227e5cd`.
Full C01/C06 and all twelve M11 gates remain OPEN. See the
[native report (archived)](public-evidence.md#historical-artifacts).

## M11 distinct-species native checkpoint, .177 (2026-10-01)

Release build exits 0. Focused `native_actor_abi_host|network_host` passes
2/2 in 6.56 seconds; the checkpoint tool suite passes 7/7 in 0.124 seconds.
Typed x86 serialization fixtures check the +4 relationship/+8 history receivers,
original stream pointer, callee stack cleanup and history failure propagation.
Sealer tests cover schema3 compatibility and schema4 missing/duplicated/misowned,
wrong-epoch, failed, reordered or replayed private-root receipts. These are HOST
results, separate from the original-game execution below.

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release -R 'native_actor_abi_host|network_host' --output-on-failure
python -B -m unittest discover -s tests/unit -p test_durable_checkpoint.py
```

Native130 completes original Save with B's distinct-species first-kill result,
7 DNA and private relationship/history. Native134 freshly restores the sealed
artifact into all three processes: all 679 player/inventory rows match exactly,
existing actors are adopted, both clients connect with the correct creature
and original Sporepedia collection, normal A jump lands and diagnostic B
movement travels 5.5978906 native units. No native reward/history event is
replayed. Failed client baselines131–133 remain failures; their successful
native reads are not treated as successful multiplayer recovery.

Closed review command (exit 0):

```powershell
python -B local/m11-resume-2026-09-27/review-foreign-save177.py --visual-inspected-by-root
```

Private report: `local/m11-resume-2026-09-27/trial134/closed-foreign-save-review.json`,
SHA-256 `f9203cf4f7ea26f49b6b3c0141a86d5bcd126cdcf8e8036e0fd29feb84a17fce`.
All five trials close cleanly, all 240 filmstrip cells are inspected, 29 protected
personal files per role are unchanged and disposable profiles are restored.
Final trial134 retains 109 frozen source/artifact entries. Full C06/C11 remain
OPEN: extended normal play, foreign lifecycle/editor/nest changes, normal held
B movement and schema4 coordinator publication/crash qualification are NOT RUN.
Exact commands, hashes and observed failures are in the
[native report (archived)](public-evidence.md#historical-artifacts).

M11 .163: repaired Release build exits0; native-actor ABI HOST test passes1/1
in0.68s. The first build's invalid void template argument was corrected to
the pinned actor/combatant/vector types. A test run after that failed build
used the previous binary and is not counted as current-build verification.
Trial103 was closed cleanly without confirmations or captures after its
interaction window was missed. It supplies no hatch acceptance. Startup now
starts the three exact-window recorders automatically for the repeated run.

M11 .161/.162: Release builds exit0 after correcting an erroneous SDK enum
name in the first .161 build (the pinned native age is an integer). The .161
native-actor ABI/network HOST checks pass2/2 in4.71s. Existing fixtures cover
the reused eight-word native AI leaf convention and thiscall word/float/no-arg
contracts. Trial101 separately checks pending-corpse lifetime and retains the
primary world-reset failure; it does not qualify B hatch. See the session
report for original-game outcomes and limitations.


M11 .130/.131: repaired .130 Release build and .131 Release build exit0.
Complete CTest passes19/19 in62.99s and61.61s respectively. HOST tests cover
camera bases, unit diagonal directions, quantized spherical directions,
malformed/reused payload rejection and release ordering across jumps, newer
interactions and fences. Trials61-62 separately qualify original two-player
normal movement/release/jump slices. Held-key soak remains NOT RUN. See the
[M11 report (archived)](public-evidence.md#historical-artifacts).


M11 .123 and .125: Release builds exit0; complete CTest passes **19/19**
in **61.81s** and **61.21s**, respectively. Schema16 regression checks preserve
a reloaded positive-health NPC corpse while refusing combat/social targets,
negative health, old schema15 and unqualified positive-health owned deaths.
.126 adjusts corpse-pose admission after native55's presentation refusal;
Release build exits0 and native-life ABI/network tests pass **2/2**, **4.07s**.
The paired fresh original-game trial56 and exact closed-file review are separate
NATIVE evidence. .127's new observation-only picked-object thiscall has guarded
instructions/caller/RET4 and HOST hook/stack checks; Release build exits0 and
actor-command/native-actor ABI/network tests pass **3/3**, **4.79s**.

M11 .122: Release build passes with no warnings/errors. The first focused
run fails an outdated assertion that verb9 must be unsupported; the new combat
verb is9 and the unknown-verb case now checks10. After rebuilding, replica,
actor-command, native-actor ABI and network HOST/FIXTURE tests pass **4/4**
in **4.95s**. This includes selected-index roundtrips, old-schema rejection,
wrong-owner/stale-target rejection and dead actor/target fences. Original
gameplay remains a separate native gate.

M11 .118 (2026-09-27): `pwsh -NoProfile -File tools/build/build.ps1
-Configuration Release` exits0 with no warnings/errors; `ctest --test-dir
build/win32 -C Release --output-on-failure` exits0, **19/19**, **61.89 s**.
BUILD/HOST/FIXTURE evidence remains separate from trial44's original-game
cancellation/re-entry/jump evidence in the M11 native social session report.

## Website rebuild (2026-09-27)

Run `node website/build.mjs` followed by `node website/check.mjs` for the
independent presentation site. The rebuilt English/French site has 30 generated
HTML documents, including 22 indexable pages, and the final static pass checks
632 local links, fragments and assets. Browser checks cover 320–1440 px layouts,
200% text, keyboard stage selection, mobile menu/Escape, language continuity,
article/archive routes, disclosures, historical links, reduced motion, and
content without JavaScript. See [website maintenance](website.md) and the
[session record](../evidence/2026-09-27-website-rebuild/SESSION.md). These checks
do not start SPORE or qualify native gameplay.

## M11 Creature trace tooling (2026-09-27)

Bridge/NativeHost **0.0.106** builds successfully. The full Release suite passes
**19/19 in 59.18 seconds**. The subsequent focused evidence suite passes
**19/19 tests** after adding delayed-objective validation. HOST checks
cover payload/meter boundaries, original input/tick/result ABIs, exact-once
forwarding, stack balance, and refusal of malformed or leaking result evidence.
These tests load no original game. The earlier .95 transfer test failure and
successful unchanged targeted rerun remain recorded; its cause is unresolved.
Native trials17–18 record B-only First DNA presentation, A continuing to play,
native tutorial completion and original save/load. Trials19–20 expose a
separate delayed species-objective award to A. Trial21 fixes that path, with
B receiving 10+25 and A unchanged; trial22 restores B at 45 and A at 35,
without replay, with exact 682-row inventory/player agreement. Bridge .110
passes the full 19/19 suite in 59.86 seconds and trial29 adds a bounded original
mouse/keyboard greeting/Sing result with the original colored meter. The
objective panel remains stale. The .111 full suite initially passes 18/19:
one assertion still expects schema 13. After replacing that literal with the
declared schema version, the unchanged first-feed offsets and the network
suite pass (4.05 seconds). The .112 and .113 focused replica, native ABI and
network suites each pass 3/3 (4.75 and 4.88 seconds). The .114 focused suite
passes 3/3 in 4.86 seconds; .115 passes the full Release suite, 19/19 in
62.96 seconds. Both Release builds have zero warnings/errors. The .116 worker,
replica, native ABI and network selection passes 4/4 in 17.90 seconds; .117
replica, native ABI and network passes 3/3 in 4.91 seconds. Those Release builds
also have zero warnings/errors. These are HOST checks; the death, nest and
controller/save-load native follow-ups are recorded separately. Separate
species/objectives and all full C01–C12 requirements remain open.
All 29 protected personal files remain unchanged.
See the [native report (archived)](public-evidence.md#historical-artifacts) and
the earlier [observer session (archived)](public-evidence.md#historical-artifacts).

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release --output-on-failure
python -m unittest discover -s tests/unit -p test_creature_social.py -v
python tools/native/analyze-creature-social.py local/M11_CLOSED/actors.jsonl --output local/M11_CLOSED/objective-review.json --require-owned --require-action --require-objective
```

The [Creature gate map](../tests/engine/M11.md) and
[source/binding audit](m11-creature.md) retain every normal gameplay requirement.
The earlier reducer passed 5/5 HOST/FIXTURE tests before action observations were
added. Reducing the retained M03 combat trace found zero calls
from the candidate social reward caller; requiring such a call correctly exits
1. This is analysis of old evidence, not a new native gameplay test. The tool
never upgrades parser success or a global DNA return into social ownership.

## M10 native recovery increment (2026-09-26)

Bridge 0.0.86 passed the full Release build and 19/19 CTest cases. Subsequent
focused native-global-checkpoint and publication validator suites passed 6/6
and 7/7. These are BUILD/HOST/FIXTURE checks; they do not launch SPORE.
The [native recovery report](../evidence/2026-09-26-m10-native-state/NATIVE-RECOVERY-086.md)
records original-worker cuts, retained actual exit codes, current native
artifacts, jump/landing outcomes, profile restoration and the M10 acceptance decision.
Fast command intent phases require an observed durable phase at actual death;
an earlier admin receipt cannot establish that the intended phase was cut.

The final destination-cleanup interruption exposed a coordinator ordering race.
After the correction, rebuild and run the affected HOST regression with:

```powershell
cmake --build build/win32 --config Release --target SporeMP.Coordinator sporemp_transfer_control_tests --parallel 4
ctest --test-dir build/win32 -C Release -R '^transfer_control_host$' --output-on-failure
```

Both commands exited 0; the affected test passed in 21.11 seconds. It verifies
that destination recovery preserves the pending source cleanup, then accepts
its receipt and both recovered baselines. The earlier 19/19 result retains its
original build identity. Three subsequent original-game cleanup interruption
cases and the final fresh-process player reconnect pass with the corrected
coordinator; Bridge and NativeHost hashes are unchanged.

The full acceptance text for M00–M20 is retained in `../MILESTONES.md`. Every milestone records its objective, dependencies, changed modules, commands, expected/observed outcomes, evidence, unresolved issues and next step. Current M01 observations and user acceptance are in ../evidence/2026-09-08-m01-native/SESSION.md. Do not repeat completed tests without a relevant change, failure or unresolved concern.

## Reviewed SDK model lifetime patch — 2026-09-26

`tools/build/build.ps1` builds the SDK DLL and SDK-base library from a verified
private archive copy of pinned commit
`cbf9206b9a823f0911cd9be0217104a49d72380b`, with
`tools/build/patches/modapi-model-load-lifetime.patch` applied. The external
checkout remains unchanged. The patch retains the original model with the
SDK's `ModelPtr` across background-load completion and texture override.

The patch SHA-256 is
`7ca2bce9880f1f6a8423f4bb28c73e5c4cf8ea10a4700cf9cf1e3edbb4479b44`.
`config/dependencies.lock.json` also pins the exact target file before and
after hashes. `prepare-sdk-source.py` rejects a modified external checkout,
changed patch, incomplete or unknown cache receipt, and any altered, missing
or extra file in the copied source. Text bytes are independent of Git's local
line-ending configuration. Existing SDK artifact output paths are retained;
the guarded host must be rebuilt to embed the changed SDK payload identity.

```powershell
python tools/build/test-sdk-source.py
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release -SdkOnly
python tools/build/prepare-sdk-source.py .
```

The first verification passed **10 HOST tests in 3.570 seconds**, exit 0.
The Release SDK-only build and post-build source revalidation both exited 0.
No game was launched by these checks. Native repeated-load validation of the
lifetime change is required before claiming that the observed loader crash
is resolved. See the exact [SDK compatibility identity](compatibility.md#reviewed-sdk-source-patch--2026-09-26).

## M10 location and transfer policy — current verification

The earlier Release build and all 17 then-registered CTest cases passed as BUILD/HOST/FIXTURE evidence.
`location_ledger_host` exercises canonical leases,
durable transfer recovery, one live coordinator routing two location sessions,
isolated scene baselines, wrong-location rejection, and a content event reaching
a player across sessions through the shared universe registry. A real CLI smoke
copied the closed M09 state into ignored local storage, exported its checkpoint,
bootstrapped two location leases with slots `[0,1]`, restarted the online
coordinator, and authenticated both location-specific player probes. The receipt
labels this `native_validation: NOT_RUN`; the test did not start SPORE or any
worker.

The 2026-09-26 live preparation increment adds `transfer_control_host` as the
18th registered CTest case. Its focused Release verification passed
`checkpoint_host`, `location_ledger_host` and `transfer_control_host` **3/3 in
2.34 seconds**, exit 0, with 25, 281 and 238 assertions respectively. This
increment did not run the entire newly expanded suite. The transfer test
uses three real Schannel authority connections and two player connections,
checks continuing third-location motion and actions, retries after durable
reopen, private request/receipt failure recovery, and malformed schema-5
records. Schema-2/3/4 coordinator recovery remains covered. These are HOST
fixtures; no original-game avatar is transferred by these tests.

```powershell
cmake --build build/win32 --config Release --target SporeMP.Coordinator sporemp_transfer_control_tests sporemp_location_ledger_tests sporemp_checkpoint_tests --parallel 4
ctest --test-dir build/win32 -C Release -R '^(transfer_control_host|location_ledger_host|checkpoint_host)$' --output-on-failure
```

The implemented private administrator control accepts durable `prepare` and
unfenced `cancel` only. Native fence, restore, ownership commit and cleanup
remain refused. See the [control contract](m10-locations.md#live-preparation-and-cancellation-control)
and [reviewed coordinator evidence (archived)](public-evidence.md#historical-artifacts).

An earlier bridge build restored one matched original-game checkpoint in
two live Satiria/Haagos authority workers under one coordinator. One native
player process joined Haagos, then a fresh process joined Satiria on the
respective location route; both applied baseline 1, and inspected original
game windows named the corresponding planet. The authority traces show both
owner avatars in both locations, so native occupancy failed despite successful
routing. A subsequent HOST guard rejects that cross-location publication. An
earlier Haagos player join failed closed on a missing original NPC herd
template. Native transfer, meeting,
separation, per-location checkpoint publication, worker kills during transfer,
and shared native consequences remain **NOT RUN**. See the
[M10 gate map](../tests/engine/M10.md), [0.0.70 coordinator session (archived)](public-evidence.md#historical-artifacts),
[M10 session (archived)](public-evidence.md#historical-artifacts),
[native continuation (archived)](public-evidence.md#historical-artifacts),
the [selected-load run (archived)](public-evidence.md#historical-artifacts),
and the [live two-location run (archived)](public-evidence.md#historical-artifacts).
The subsequent one-primary original-game probe captured one leased owner and
zero foreign owned avatars in each simultaneously running Satiria/Haagos
scene; see the [native primary capture report (archived)](public-evidence.md#historical-artifacts).
A selected 30-file two-save head then admitted both scenes under one live
coordinator. Separate original player processes applied the corresponding
scene baselines. After an authority disconnect, a fresh original Satiria
authority republished its scene while Haagos stayed connected; player 1
applied a new baseline. A later run durably advanced Satiria's worker
generation from 5 to 6 and recovered the same player while Haagos stayed
live. This is [bounded NATIVE location and higher-generation
reconnect evidence](../evidence/2026-09-24-m10-one-primary-online/SESSION.md).
It does not qualify live transfer,
shared-world Save publication or once-only global native consequences.

```powershell
cmake --build build/win32 --config Release --parallel 4
ctest --test-dir build/win32 -C Release --output-on-failure
```

The latest home-location guard build passed all 17/17 CTest targets in 35.09
seconds. Native acceptance uses the guarded disposable-profile, checkpoint
restore and same-coordinator procedure in the linked live run; the commands
above do not launch SPORE. Trial 20 of the linked online session admitted
matching Satiria and Haagos original home scenes and the Haagos player. It
rejected a one-value-wrong area before connection. This is bounded NATIVE
location admission;
away-area mapping and live transfer remain NOT RUN.

## M09 checkpoint/recovery completion — recorded verification

Bridge/NativeHost **0.0.64**, launcher **0.1.11**. Full Release build exits 0;
full CTest passes **16/16 in 34.67 seconds**. This includes 251 Python tests,
302 worker assertions, 71 durable-store checks, 25 checkpoint checks, 246 registry
checks, 970 content/TLS checks and 2,963 network assertions. The subsequent focused
launcher build/test passes **55 HOST assertions in 4.85 seconds**, including the
recovered Rejoin progress state. These are BUILD/HOST/FIXTURE results.

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release --output-on-failure -V
dotnet build tests/launcher/SporeMP.Launcher.Tests.csproj -c Release -o build/launcher-tests/Release --nologo
ctest --test-dir build/win32 -C Release -R launcher_host --output-on-failure -V
```

Separately authorized original-game runs establish native full-save recovery,
two-client rejoin, exact original player/inventory/progression preservation,
critical resubmission, crashes before and after original Save, and five actual
coordinator publication terminations followed by fresh native loads. The fixed
five-minute recovered-authority deadline is exercised with original gameplay
paused. All profiles and personal files are restored/verified after cleanup.
The final supervisor was built against the frozen qualified bridge; a fresh link
changes the build fingerprint and requires compatible checkpoint provisioning.

[Reviewed acceptance, command records, hashes, failures and limitations](../evidence/2026-09-22-m09-persistence/acceptance.md),
[D01–D08 matrix](../tests/engine/M09.md),
[protected recovery commands and persistence contract](m09-persistence.md).

## M08 completion — retained verification

Bridge/NativeHost **0.0.50**, launcher **0.1.10**, network schema **7**, config
schema **2**. Final Release build exits0. Full CTest passes **14/14 in 32.77s**,
including 244 Python tooling tests, 190 registry checks, 937 content transport/TLS
checks and 2,963 existing network checks. Six executable/private-pipe/TLS smoke
checks also pass. These checks launch no game:

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release --output-on-failure -V
```

Separately authorized native01–11 establish the bounded M08 foundation. The final
.49/.50 increment seals ten healthy original-process traces with clean exits,
matching original content and continued gameplay, precise dependency refusal,
commit/cancel and canonical terrain evidence. Personal/world hashes pass. The
actual-profile terrain mismatch is a HOST prelaunch refusal, not a new game run.
[Acceptance, exact commands, failures and limitations](../evidence/2026-09-22-m08-completion/SESSION.md).
The sections below are historical increments, not the current milestone status.

## M08 native creation and inspection follow-up

The authorized .46 native creation/import sequence now runs in two separate
disposable profiles. Original loader calls copy matching **23 rigblocks/47
capability entries**; original editor/test-drive views, changed-draft cancellation
and a home terrain-key observation are recorded. Four clean process exits, healthy
closed traces and unchanged personal/world files pass. This does not complete M08's
dependency, terrain synchronization or multiplayer editor transaction gates.
[Native session and commands](../evidence/2026-09-22-m08-native-creation/SESSION.md).

The Python driver now admits a logical CRT backed by BEM only with a matching
healthy closed native save witness. **19 focused tests** and **223 tooling tests**
pass, exit 0; the affected CTest target takes **6.72 seconds**. Commands:

```powershell
python -m unittest discover -s tests/unit -p test_m08_content_probe.py -v
ctest --test-dir build/win32 -C Release -R tooling_unit --output-on-failure -V
```

Native artifacts are unchanged from the prior complete .46 build and 12/12 suite.
The fixture's BEM save and CRT import have different bytes/local keys; matching
copied native scalars does not assert full byte or gameplay equivalence.

## M08 admission and inspection increment — pre-native boundary

Bridge/NativeHost **0.0.46** and launcher **0.1.10** use network schema **5** and
session config schema **2**. The final integrated Release build passes; all **12/12
HOST/FIXTURE targets** pass in **26.93 seconds**, including **219 tooling tests**.
Network coverage is **2,963 assertions**; worker coverage is **280 assertions**;
the content ABI fixture has **18 checks** and the passive editor decoder has
**26 checks**. The earlier full suite passed 11/11 before adding that decoder target;
the affected loading-guard/driver rerun passed 3/3. These
commands never launch SPORE:

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release --output-on-failure -V
ctest --test-dir build/win32 -C Release -R 'worker_host|native_content_abi_host|tooling_unit' --output-on-failure -V
```

The separate coordinator CLI test verifies exact changed-world-file and legacy
config refusals. C++ and Python readers agree on the six files in each closed
prepared profile. The native inspection driver has **15 focused HOST tests**;
its actual original-game loader call is **NOT RUN**. Original editor creation,
clean second-profile loading, native behavior, exact dependency readiness,
generated-terrain correlation and editor transactions remain required.
[Session and identities](../evidence/2026-09-22-m08-admission-inspection/SESSION.md),
[world contract](m08-world-identity.md), [native probe](m08-native-content-probe.md).

## M08 offline content foundation

Current second-increment results: **32 focused content tests and 202 tooling tests**,
both exit 0 using the same commands below. `compare-trees` adds exact closed-copy file
and archive-resource differences, with bounded input and source/output protection.
Whole disposable-profile baselines pass inspection; new editor/import gameplay is
NOT RUN. The four-entry static audit is not native ABI acceptance.
[Current session and reproduction](../evidence/2026-09-21-m08-content-foundation/editor-preparation.md).

The first M08 increment changes Python tooling and documentation only. Run `python -m unittest discover -s tests/unit -p test_spore_content.py -v`, then `ctest --test-dir build/win32 -C Release -R tooling_unit --output-on-failure -V`. Final results are **23/23 focused tests** on Python 3.11.15 and **193/193 tooling tests** on the CTest-configured Python 3.14.3, both exit 0. The initial full-suite failure exposed different Windows `ctime` semantics in handle/path queries; compare `ctime` only across handle queries while preserving path identity/size/mtime checks. The regression is covered and the failed log retained.

The offline inspector also read six closed original-game archives, decoded 108 compressed records, and verified private quarantine/reuse with unchanged source hashes. Those are HOST operations on historical native-produced bytes. No new C++ build, game launch, native import, editor transaction or M08 native acceptance is claimed. [Content contract and limits](m08-content.md), [M08 gate map](../tests/engine/M08.md), [exact session/results](../evidence/2026-09-21-m08-content-foundation/SESSION.md). The complete M00–M20 plan remains unchanged.

## M07 build and native evidence

The M07-qualified bridge/NativeHost version is **0.0.45**, with wire schema4. The Release build and **10/10 HOST/FIXTURE CTest targets** pass, exit0, in **23.82 seconds**, including 2,670 network assertions. The subsequent Python-only pickup observation correction passes41 focused HOST tests. These commands do not launch SPORE. Native results and the current acceptance decision are tracked separately in [the M07 protocol](../tests/engine/M07.md) and [completion session](../evidence/2026-09-21-m07-completion/SESSION.md). Native18 supplies the normal encounter/consumption/reconnect evidence; later runs retain their own exact artifact and helper hashes.

The following .34/.35 results record the initial integration boundary, before the later native pickup, pool, presentation, supervisor-readiness and capture-cadence changes.

Bridge/NativeHost 0.0.34 uses network wire schema 3 (400 bytes). The .31 foundation adds native attack/approach/engage routing, target-generation validation, authority B reward bootstrap and explicit native life-state scalars. After .31–.33 native lifecycle investigations, .34 adds observed age/alpha/Combatant context and native scale while retaining the effective-health limit. The current Release build and 9/9 CTest targets pass in 12.40 seconds, including 2,595 network assertions and 132 Python tests; these are BUILD/HOST/FIXTURE evidence only. Current implementation boundaries and every original native acceptance requirement are retained in [M07.md](../tests/engine/M07.md), [encounter design](m07-encounter.md), [native action audit](m07-native-encounter-audit.md) and [death-state analysis](m07-life-state.md).

The historical **0.0.35** build adds diagnostic-only dynamic-spawn result/template/factory records, retaining schema 3 and all selection/rejection behavior. Its final Release build exits0 and all **9/9 CTest targets pass in 12.44 seconds**, with the same 2,595 network assertions and 132 Python tests. Native04/.34 verifies the bounded context/scale lifecycle and fresh Join; native05/.34 records shared damage and one original reward but fails complete client agreement; native06/.35 classifies the newly active species failure as `missing_template`. Full M07 was IN_PROGRESS at that boundary.

```powershell
cmake --build build/win32 --config Release --parallel 4
ctest --test-dir build/win32 -C Release --output-on-failure -V
```

Neither command launches SPORE. Exact commands/exits, including the failed Python 3.14 proxy shutdown attempt and final correction, are recorded under ignored `local/m07-encounter-2026-09-21/`; the reviewed [initial session report](../evidence/2026-09-21-m07-encounter/SESSION.md) and [initial verification manifest](../evidence/2026-09-21-m07-encounter/verification.json) retain .31 results and identities. Historical .32–.34 build/test logs and frozen payloads are under ignored `local/m07-native-2026-09-21/`; subsequent completion artifacts remain under ignored `local/m07-completion-2026-09-21/`. Desktop authorization and refreshed save/profile checks permitted the original-game runs; [the reviewed lifecycle report](../evidence/2026-09-21-m07-encounter/native-lifecycle.md) retains the initial successes and failures. Every new native attempt still needs matching frozen payloads and inspected process-specific captures. Do not infer native gameplay acceptance from a successful build. The pickup binding's later native qualification is described in [the pickup document](m07-native-pickup.md). A delay-only TCP proxy cannot certify packet-loss testing; the separate [packet filter](m07-packet-impairment.md) records actual scoped capture, loss and reinjection decisions.

## M05 verified Creature fixture

M05 is **VERIFIED for the recorded living Creature fixture**, bridge **0.0.29**. [Current native evidence](../evidence/2026-09-14-m05-ability/SESSION.md) and the [acceptance decision](../evidence/2026-09-14-m05-ability/acceptance.md) separate original-game execution, inspected captures, static audit and HOST fixtures. A real 8.75-DNA result is applied without another award; current evidence adds active-bite cleanup, actual foreign-species UI and ordinary saved-world Play denial with working Cancel. Retained Save/movement/Off/disconnect evidence is not rerun or relabeled. Full scene, inventory, charge/projectile, death/respawn and perceptual listening coverage remains unqualified; see [the native protocol](../tests/engine/M05.md).

```powershell
cmake -S . -B build/win32 -G 'Visual Studio 17 2022' -A Win32 -T 'v143,version=14.44.35207' -DCMAKE_SYSTEM_VERSION=10.0.26100.0
cmake --build build/win32 --config Release --target SporeMP.NativeHost SporeMP.WorkerControl sporemp_replica_tests sporemp_worker_tests --parallel 4
ctest --test-dir build/win32 -C Release -R 'replica_host|worker_host|native_actor_abi_host|actor_commands_host' --output-on-failure -V
python -m unittest discover -s tests/unit -p test_replica_probe.py -v
```

The current replica target contains **313** policy/fence/lifetime assertions, **25** actual HOST Detours/Win32 ABI checks and **9** interior-branch ABI checks, including 4,096 alternating branches with deliberate register/FP clobber. Existing actor/award/persistence ABI coverage is **34**. `build-029` has zero warnings and `ctest-029` passes the affected replica target; `ctest-final-028` passes the full **8/8** suite in **6.05 s**. Five focused Python tests pass in `python-027`. Exact commands/exits are in the current session. No game is launched by these commands. Full normal-Play regression and native terminal trace-budget exhaustion are not inferred from these tests.

For native experiments, `start-worker.ps1 -M05Role Authority|Replica` is explicit and isolated. `replica-probe.py` controls already running workers; it does not manufacture results or launch a game. A validated .16–.28 authority source can be reused by the current .29 receiver with the same scalar capture ABI. The current source is the real .18 combat reward. `replay --challenge` invokes actual native entries with live receivers; queued charm/no-attack probes require a later natural-update result, and are explicitly temporary field fixtures rather than actual casts. Keep failed runs, inspect actual captures, observe exits and compare closed save hashes. `python tools/native/analyze-m05-ability.py --input-dir local/M05_ARCHIVE --output local/FRESH_REPORT.json` analyzes privately archived traces; add `--media-dir local/CAPTURES` for optional retained media and retains `full_m05_acceptance: NOT_INFERRED_BY_ANALYZER`; the separate acceptance decision evaluates the original milestone clauses.

Optional audio evidence uses the separate `tools/native/audio-capture` CMake project and `SporeMP.AudioCapture.exe GAME_PID SECONDS OUTPUT.wav` (1–60 seconds). It captures only the selected process tree with Windows process loopback, 48 kHz stereo PCM16, no input and no audio processing. Building or running HOST tests never starts SPORE. Packet integrity does not replace listening; exact build/capture commands and Microsoft API references are retained in the presentation session.

## M04 focused worker increment

M04 is **VERIFIED for the recorded bounded original Creature worker fixture**, bridge **0.0.14** / launcher **0.1.8**. The [current native acceptance session](../evidence/2026-09-13-m04-acceptance/SESSION.md) separates original execution from compiler, ABI and fixture checks and records the final evidence/source freeze. The original M00–M20 acceptance clauses remain intact. Future B campaign/reward persistence, arbitrary checkpoints, Internet clients, locked/disconnected desktops and capacity remain unqualified.

The .14 integration Release build and focused worker/actor-command/ABI targets pass 184/15/34 assertions; the ABI count includes eight new persistence checks and stack-balance verification. The latest .1.8 Release build has zero warnings/errors, and the affected CTest run passes 2/2 targets with 39 launcher assertions and 98 Python tests. Checkpoint-sidecar coverage includes malformed/ambiguous pairs, crossed epochs, numeric widths, changed trace/backup/native tree and legitimate status request zero. Poll/lifetime fixtures exercise stable dropdown entries, selection during refresh, serialized/duplicate mutations, cancellation and launcher-test-process closure without killing the fixture backend. Exact pending-mutation closure remains HOST/FIXTURE evidence. The native worker bridge was not rebuilt for the launcher-only poll fix.

The new NativeHost runtime-log exception passes 18 diagnostic tests. Only exact regular `SporebinEP1/spore_log.txt`, authored by pinned SDK `Application.cpp:123–202`, is excluded from immutable-content equality and fingerprinted separately. Changed content/payloads, directories, reparse points and unknown sibling files remain rejected. Concurrent native runs use host SHA-256 `1559d4540bfdcbb02a40c07f50c048461881dc3e57dcfe6585ffca86d0e2a4a2`; .14 bridge, SDK core and injector are unchanged. See [runtime provenance (archived)](public-evidence.md#historical-artifacts "Original path: evidence/2026-09-13-m04-acceptance/final-runtime-provenance.json").

Affected executable checks, after rebuilding changed targets:

```powershell
ctest --test-dir build/win32 -C Release -R 'worker_host|native_actor_abi_host|actor_commands_host|launcher_host' --output-on-failure -V
python -m unittest discover -s tests/unit -p test_worker_checkpoint.py -v
python -m unittest discover -s tests/unit -p test_worker_manager.py -v
python -m unittest discover -s tests/unit -p test_diagnostics.py -v
```

These commands never start SPORE. While an existing launcher is open, use an isolated output directory for launcher checks rather than overwriting its Release binaries:

```powershell
dotnet build tests/launcher/SporeMP.Launcher.Tests.csproj -c Release -o build/launcher-tests/m04-poll-fix --nologo -m:1
& build/launcher-tests/m04-poll-fix/SporeMP.Launcher.Tests.exe build/launcher/Release/launcher.runtime.json
dotnet build src/launcher/SporeMP.Launcher.csproj -c Release -o build/launcher/m04-poll-fix --nologo -m:1
```

`worker_host` covers bounded wire parsing, actual overlapped pipes and PID authentication, two HOST fixture children, controller disconnect, crash containment, same-profile rejection and owned-job teardown. Real ACL denial probes and personal-file comparisons are separate HOST evidence. Counts and exact command outcomes stay tied to their source versions in the [implementation session](../evidence/2026-09-13-m04-completion/SESSION.md), [launcher poll/lifetime checks](../evidence/2026-09-13-m04-completion/launcher-poll-lifetime-fix.md), and `affected-tests-018.command.json` / `affected-tests-018.log` in the current native evidence directory. M04 Debug native execution remains NOT RUN.

### Executed native recovery and concurrent-save procedure

In an authorized disposable worker, load the controlled `Satiria.spo` fixture, run the unattended action probe and wait for original landings, then request `save`. Matching persistence state 3 means the original helper returned true only. After clean shutdown and all games closed, run `worker-checkpoint.py seal 01 --output FRESH_SIDECAR`. From a fresh worker's original galaxy menu, run `worker-checkpoint.py load-restore 01 --sidecar SEALED_SIDECAR --output FRESH_REPORT`, then `test-worker-actions.py 01 --require-existing-actors --output FRESH_ACTION_REPORT`. These scripts are in `tools/native`; resolve fresh output paths before use. Recovery never creates a replacement actor for a missing saved noun, and unknown mutation outcomes are never retried.

This .14 sequence has now run: the initial closed checkpoint was loaded twice into fresh generations, both adopted matching existing A/B noun/herd/species/archetype fingerprints with zero nouns created, and native return/landing actions followed. Wrong ownership and stale epoch were refused. Mismatched-sidecar negative cases have HOST/FIXTURE coverage; native execution of those negative cases is NOT RUN. The action probe intentionally retains full milestone acceptance `NOT_VERIFIED`; a helper result is not an acceptance decision. It defaults to .14, with explicit `--bridge-version 0.0.13` for preserved older evidence.

Two concurrent original saves have at least **414.7126 ms** overlap from bounded native/host QPC alignment. Actual `stars.db.tmp` Write intervals overlap **20.1228 ms**. Both later closed checkpoints match the source native request/epoch/fingerprints, trace prefixes and healthy clean-stop traces; backup manifests, all payload hashes and current closed Games trees match their sidecars. Those exact newly sealed concurrent artifacts are not reloaded; the earlier checkpoint recovery remains separate. See [concurrent save (archived)](public-evidence.md#historical-artifacts "Original path: evidence/2026-09-13-m04-acceptance/concurrent-save-01.json"), [checkpoint cross-check (archived)](public-evidence.md#historical-artifacts "Original path: evidence/2026-09-13-m04-acceptance/concurrent-checkpoint-crosscheck.json") and [full-lifetime concurrent file audit (archived)](public-evidence.md#historical-artifacts "Original path: evidence/2026-09-13-m04-acceptance/concurrent-isolation.json"). Neither native save success nor closed hashes prove power-loss durability.

Actual .1.7 WPF Start and subsequent launcher closure preserve authority; an original normal-account player then exits and the recovered worker completes fresh native actions afterward. Actual .1.8 selection remains open 15.661 seconds across status polls, worker selection 02/01 succeeds and both Stop operations cleanly exit the games/supervisors. Native .1.8 Start and exact closure during a pending worker mutation are NOT RUN. Original personal baseline failure after normal Play is retained: ten files changed, with bounded player write/rename/delete attribution; the later 29-file personal tree stays unchanged across the concurrent worker interval.

ETW attributes 488 successful own-Games writes to each engine thread, with zero resolved peer/personal access or mutations and zero reported loss. It retains 103/102 unresolved file writes, 3,263/3,132 registry SetInformation events and 1,109/1,093 undecoded events for workers 01/02. Shared NVIDIA driver writes remain. Exact kernel process lifetimes filter reused PID events; zero resolved cross-writes is not exhaustive Windows/config isolation. Worker 02's approximately 47-second AI plateau after save with continuing app heartbeats has no established cause; it was reported as stalled and stopped cleanly. Minimization, desktop/session and performance limits are in [M04.md](../tests/engine/M04.md) and [worker instructions](m04-workers.md). No unchanged M03 combat/reward replay was performed.

## Build and executable host tests

From the repository root, using PowerShell:

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release -FetchDependencies
ctest --test-dir build/win32 -C Release --output-on-failure -V
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Debug
ctest --test-dir build/win32 -C Debug --output-on-failure -V
python -m unittest discover -s tests/unit -p 'test_*.py' -v
pwsh -NoProfile -File tools/build/test-clean-build.ps1 -Destination local/clean-build-unique-name
```

`-FetchDependencies` downloads the locked SDK and injector repositories if absent; it does not reset a dirty/wrong dependency checkout. The source-provided MSBuild project builds the actual core DLL and import library. The bridge links those symbols, with MSVC Win32 layouts. /MD and /MDd follow the upstream default CRT configurations. `build.ps1` prints exact commands and fails on nonzero exits. SDK full logs/binlogs are under `build/sdk/<configuration>`.

`diagnostics_host` exercises Windows CNG hashing, known SHA-256 vectors, path checks and rejection of a foreign executable. It never loads the SDK core. `tooling_unit` uses synthetic PE/content/save fixtures to test wrong architecture, same-size tampering, extra/missing content, reparse points, backup preservation/corruption/interruption, path traversal, exclusive evidence creation and preflight denial. Mocked process-list observations are explicitly fixture tests. Passing these cannot prove original-game load, safe shutdown or native gameplay.

## Live file inventory and safe prerequisite probes

The build also produces `build/launcher/<configuration>/SporeMP.exe` using .NET SDK 8.0.418 and the .NET/WindowsDesktop 8.0.24 packs. No external NuGet sources are enabled. `launcher_host` tests literal process arguments, report validation, actual streamed child progress, malformed-response child cleanup and cancellation. The Python suite also tests installation discovery, Steam secondary libraries and malformed metadata, automatic cold/warm setup, changed/damaged backups, duplicate preparation exclusion, and missing/ambiguous/running/mismatched game paths. All are HOST/FIXTURE, not original-game acceptance.

The player flow is open launcher → Play SPORE, using the normal Windows account and existing saves. It requires no backup/profile setup. Automatic discovery, prior native runs and final user confirmation already cover this M01 increment; no repeat run is pending. Details and report export remain in Settings. See launcher.md and the native session.

For launcher-only edits while a player is using the existing build, compile to a separate directory without launching either application:

```powershell
dotnet build src/launcher/SporeMP.Launcher.csproj -c Release -o build/launcher/0.1.2 --nologo -m:1
$launcherPython = & python -c 'import sys; print(sys.executable)'
@{ repo_root = (Get-Location).Path; python_executable = $launcherPython } | ConvertTo-Json | Set-Content -LiteralPath build/launcher/0.1.2/launcher.runtime.json -Encoding utf8NoBOM
```

The 0.1.2 redesign was compiled this way. Its new navigation, search and layouts have not been exercised in a visible window; native tests were not repeated. The next focused UI review is Home / release selection and search / Settings at normal and minimum sizes, when desktop interaction is welcome. This is a launcher presentation check and does not reopen M01 native acceptance.

```powershell
python tools/diagnostics/sporemp_diag.py inventory --game-root 'C:\Games\SPORE' --output local/new-candidate-review.json
python tools/diagnostics/sporemp_diag.py validate --game-root 'C:\Games\SPORE'
python tools/diagnostics/sporemp_diag.py new-profile --root local/profiles --name another-worker
python tools/diagnostics/sporemp_diag.py probe-isolation --profile local/profiles/another-worker/profile.json
python tools/diagnostics/sporemp_diag.py preflight --game-root 'C:\Games\SPORE' --profile local/profiles/another-worker/profile.json
```

Use new names/output paths; tooling refuses overwriting existing profiles/evidence. Candidate inventories require source review and are never automatically promoted to supported builds. Exit 0 for `validate` means candidate fingerprint/content equality only. Exit 20 means incompatibility; 21 means isolation unproven; 22 means native-launch prerequisites missing; 2 means malformed input or I/O/probe error. The current preflight starts zero game processes. Its refusal is an executed host test; native acceptance was subsequently completed through the separate host.

Native M01 evidence is closed in ../tests/engine/M01.md: three captured clean lifecycles plus final user acceptance of the revised player launcher. Optional developer reproduction uses tools/native/invoke-isolated.ps1; it is never part of player setup. Build/tests never launch SPORE automatically.

## Global regression mapping (all native scenarios TODO/NOT RUN)

| Test | Required behavior | Milestones |
|---|---|---|
| T01 | Two real clients move/interact through a dedicated original-game worker | M03–M07 |
| T02 | Shared native enemy targets either player; damage/death/reward agree | M03/M07/M11–M15 |
| T03 | Contested pickup, spending and ownership have one valid native result | M07/M09/M11–M15 |
| T04 | Replica application never repeats an authoritative action | M05/M06/M07 |
| T05 | Native editor begin/commit/cancel does not pause/overwrite other players | M08/M11–M17 |
| T06 | Disconnect/reconnect during combat/after reward preserves identities without duplication | M06/M07/M09 |
| T07 | Separate locations, meeting and separation in one canonical universe | M10/M15 |
| T08 | Stale workers/packets cannot mutate a newer authority generation | M06/M10 |
| T09 | Worker/coordinator kills across checkpoint/transfer boundaries recover valid committed state | M09/M10/M17 |
| T10 | Native world changes persist after departure and server restart; global systems execute once | M09/M10/M15 |
| T11 | All five normal stage loops and all four native campaign transitions | M11–M17 |
| T12 | Independent stages/progression and applicable cross-stage effects | M10/M16/M17 |
| T13 | Invalid builds/content, malicious input, spoofed ownership and unauthorized admin fail safely | M01/M06/M08/M18 |
| T14 | Clean install can host over Internet, join, restart and resume using shipped instructions | M18/M19 |
| T15 | Real-game multi-hour sessions meet a measured operating envelope | M18/M19 |

`feature-coverage.csv` breaks the five stages and cross-cutting requirements into mechanic families, keeping native entry point, context, owner, replica hook, persistence and evidence columns visible. Stage tests cover all applicable rows, not just motion. All normal progression fixtures retain native prerequisites; debug shortcuts cannot be the only campaign evidence.

## Measurement and failure rules

Every native evidence bundle pins source revision/content hashes, executable/SDK/loader profile, OS and hardware, role, worker/client counts and fixture. Record native frame/tick interval, p50/p95 delays, RTT/jitter/loss, snapshot age/correction, queue pressure, bandwidth, save/restore/startup timing and CPU/RAM/GPU. Record real clients separately from synthetic protocol clients. Do not claim a fixed universal native tick rate or eight-client support before measuring it.

M18 runs 100 ms RTT / 20 ms jitter / 1% loss and 150 ms RTT / 30 ms jitter / 2% loss profiles. M19 includes all stage loops, full campaign, independent locations/stages, edits, compatibility/content failures, reconnects, native/coordinator crashes, partitions, backup/restore and sustained soak. Untested mandatory scope keeps the release partial/experimental.

Evidence classes: BUILD (compiler/linker), HOST (real OS/file operation), FIXTURE/MOCK (synthetic inputs), NATIVE (original SPORE), NETWORK-REAL (actual deployed Internet path). No class silently substitutes for another. Do not commit saves/assets; keep only their hashes/manifests and relevant diagnostics in versioned evidence.

## M02 observation increment

Launcher 0.1.3 display checks are recorded separately in `../evidence/2026-09-08-launcher-display/SESSION.md`. `test_display_settings.py` covers saved/reopened choices while a game is running, installation-field preservation, physical DEVMODEW layout, Desktop re-resolution after monitor changes, invalid/unavailable values and the service-to-host argument boundary. The launcher host suite exercises the actual child-process argument transport and view-model load/save/reopen behavior. Diagnostics host checks cover exact `-w`/`-f`/`-r:` construction and malformed argument rejection. None starts SPORE.

The focused commands for this increment are `python -m unittest discover -s tests/unit -p 'test_*.py' -v` and `ctest --test-dir build/win32 -C Release -R 'diagnostics_host|launcher_host' --output-on-failure -V`, after rebuilding the changed targets. The evidence includes a separate offscreen WPF renderer that exercises the real controls against a fixture service and saves normal/minimum-size images. It deliberately bypasses production application startup and sends no desktop input. Next native display acceptance: choose a mode/resolution in the launcher, save, start the original game when the current session is closed, and compare the requested mode and client pixel size with the observed result. Repeat for the other mode and reopen the launcher to check persistence. This display acceptance is pending, not implied by host results.

Targeted builds can reuse the already pinned dependencies without rebuilding/opening the launcher:

```powershell
cmake -S . -B build/win32 -G 'Visual Studio 17 2022' -A Win32 -T 'v143,version=14.44.35207' '-DCMAKE_SYSTEM_VERSION=10.0.26100.0'
cmake --build build/win32 --config Release --parallel 4
ctest --test-dir build/win32 -C Release --output-on-failure -V
cmake --build build/win32 --config Debug --parallel 4
ctest --test-dir build/win32 -C Debug --output-on-failure -V
```

`observation_host` executes the real pinned Detours library against compiled HOST fixture methods, including Win32 thiscall/fastcall adaptation, unchanged arguments/results, nested calls, foreign-thread pass-through and detach. It also tests bounded entity IDs, address reuse, reentrant destruction fencing, scene invalidation, JSON non-finite values, exclusive evidence creation and loss counters. It never imports the SDK core or loads SPORE. `test_gameplay_trace.py` exercises malformed/stale/mixed/incomplete synthetic logs and evidence gates. Native qualification remains [M02.md](../tests/engine/M02.md).

Use `python tools/native/analyze-gameplay-trace.py TRACE_JSONL --output FRESH_REPORT_JSON` for a structural report. `--require-native` refuses HOST_FIXTURE; `--require-m02-coverage` additionally requires the implemented Creature-campaign action/lifecycle coverage with no recorded failures/losses. No analyzer output automatically grants M02 native acceptance. Reports refuse to overwrite evidence. Session commands and results: `../evidence/2026-09-08-m02-observation/SESSION.md`.

The optional developer wrapper also accepts `-DisplayMode Fullscreen|Windowed -Resolution WIDTHxHEIGHT`; its default `Game` supplies no display override. Resolution validation runs before profile/probe side effects. The same allowlisted native host options used by the launcher receive these values. Native pair 01 used `-DisplayMode Fullscreen -Resolution 2560x1440`: both process command lines confirm `-f -r:2560x1440`, and the reviewed reference game-window video is 2560x1440. This establishes command transport and captured image dimensions; independent presentation-mode measurement, the full launcher-to-native flow and windowed-mode acceptance remain pending. See `../evidence/2026-09-08-m02-pair-01/SESSION.md`.

## Developer gameplay capture

Pair 01 exposed a GDI capture failure: the observed recording contained non-game pixels followed by 163.5 seconds of black frames despite normal recorder exit and reported frame counts. Reject that media for visual acceptance. Do not reuse the pair's GDI retry drivers as the current capture procedure or infer native frame times from encoded media cadence.

`tools/native/capture-game.ps1` is a developer-only replacement using the installed FFmpeg `gfxcapture` filter and Windows Graphics Capture. It requires exactly one original game process with the specified PID/path, exact executable/title window filters, a fresh output directory inside ignored `local`, and no reparse points. It has no desktop/monitor fallback, launches no game and sends no input. The recorder is bounded to 5–600 seconds, with an additional 15-second wall timeout for a non-rendering source. Missing-game and missing-window refusal checks passed as HOST evidence; successful original-game WGC capture was subsequently **VERIFIED** in the 2026-09-09 M02 pair: 5/240/120/420-second clips with actual game-frame review. See `../evidence/2026-09-09-m02-completion/SESSION.md`.

After an authorized native game launch, use the actual PID from the guarded native host and a fresh path:

```powershell
pwsh -NoProfile -File tools/native/capture-game.ps1 -GamePid ACTUAL_GAME_PID -OutputDirectory local/m02-captures/FRESH_NAME -Seconds 180
```

Resolve the placeholders before execution. Inspect decoded initial frames for actual game content before requesting a gameplay sequence, and inspect representative frames throughout the finalized clip, including any scene transition or focus change. A valid file, exit 0, resolution or frame count alone never passes visual acceptance. Keep raw media and derived images in ignored local storage; commit only relevant diagnostic metadata and hashes. Native frame durations require a separately verified timing source. Filter syntax and capture limitations: [official FFmpeg gfxcapture documentation](https://ffmpeg.org/ffmpeg-filters.html#gfxcapture).

## Native presentation timing and accepted M02 pair

`tools/build/fetch-presentmon.ps1` downloads official standalone PresentMon **2.5.1** from its versioned release and checks SHA-256 `9bec3083069f58f911e6a512f4806db51a27bd096103087bc1d05ef54c80a191`. It stores the executable under ignored `external/PresentMon-2.5.1` and installs no service. Source/provenance: [official release](https://github.com/GameTechDev/PresentMon/releases/tag/v2.5.1). The previously bundled NVIDIA executable's failed invocation remains separate evidence.

The reusable `tools/native/measure-frames.ps1` (extracted from the acceptance pair) records exact PID-specific arguments, start/end times, exit code, CSV and whole-process CPU delta. It validates the actual executable path and tool hash, requires an already authorized running game, and defaults output to ignored `local/native-timing/<RunKey>`. The used options are `--process_id PID --output_file FRESH_CSV --session_name UNIQUE_NAME --timed 60 --terminate_after_timed --no_track_input --no_console_stats --v1_metrics`. Confirm output PID, D3D runtime and swapchain, and keep the avatar stationary during the measured window. Native D3D9 `msBetweenPresents` is a presentation interval, not a simulation tick, input delay or compositor cadence. One capped comparison cannot isolate hook cost or certify a universal operating envelope.

The accepted pair's native/visual decisions are separate: `paired-native-analysis.json` checks the actual trace, lifecycle, loaded payloads, fixture restoration, personal hashes and timing provenance; `visual-review.json` records inspection of real game images; `acceptance.json` links those to the unchanged M02 criteria. Raw videos and fixture contents remain ignored. Reuse `tests/engine/M02.md` with fresh run/output names and closed fixture snapshots; never overwrite prior evidence or substitute a synthetic trace for original gameplay.

## M03 actor harness

M03 is now VERIFIED for the recorded original Creature fixture: [bridge 0.0.12 completion](../evidence/2026-09-12-m03-awards/SESSION.md). Build-05 and focused abi-02 pass 2/2 in 0.18s (26 ABI and 22 observation assertions). The actual award-01 run supplies B/A native reward ownership; full Creature progression remains M11. Earlier test results below stay tied to their recorded source versions. The disposable GGEUserData change is retained as a failed unchanged-fixture expectation, not hidden by the successful personal-save check.

The current investigation prioritizes [static reverse engineering](m03-reverse-engineering.md) of the native attack-decision and player-context paths. The portable tools operate on the pinned executable as data and do not start SPORE. Reuse the saved Ghidra database for selected functions. Do not repeat a full native fight/reload sequence to answer a single unresolved branch; establish the needed observation and use one bounded capture after the static audit.

The Win32 Release build includes `actor_commands_host`: 15 HOST/FIXTURE assertions for separate owners, takeover rejection, queued destruction/reuse, target invalidation, held-intention authorization, bounded command ordering and scene fencing. The earlier 2026-09-12 build-06 and ctest-04 pass6/6; these commands launch no game. Bridge 0.0.9's later build-03 passes, followed by the affected `native_actor_abi_host` target only: **15 assertions/eight real Detours signatures, CTest 1/1, 0.07s**. This includes the native decider's double clock/eight stack words, x87 float return and dirty output, and the original pointer/null return of the memory lookup. The previous tooling run retains 67 Python tests. These are HOST fixtures; native M03 Debug execution remains NOT RUN. Exact commands/results are in `../evidence/2026-09-12-m03-decisions/SESSION.md`.

Bridge 0.0.11 player-build-03 passes; the affected player callback/removal ABI target passed 18 assertions/ten signatures, CTest 1/1 in 0.07s. The ASLR-only guard correction changes no signature and needs no repeated ABI run. The new native lifecycle fixture is recorded in [the player session](../evidence/2026-09-12-m03-rewards/SESSION.md). It verifies two native player generations and cleanup, not rewards.

```powershell
cmake --build build/win32 --config Release --parallel 4
ctest --test-dir build/win32 -C Release -R native_actor_abi_host --output-on-failure
python tools/native/analyze-actor-context.py TRACE_JSONL --require-context --output FRESH_REPORT_JSON
```

The actor-context analyzer checks strict JSON, sequence/process/thread/clock identity, pinned executable/SDK for native traces, clean stop/detach, paired native call identities and nested ability/strike/damage scopes. It counts health decreases only with matching native damage state. `--require-context` rejects an older trace without the new observer header, but every report retains `native_acceptance=NOT_VERIFIED`. The analyzer does not validate all possible gameplay semantics, every array field, visual behavior or saved-file isolation. The current `../evidence/2026-09-12-m03-decisions/SESSION.md` records two diagnostic runs and one successful affected-behavior run: the nest exemption restores six native NPC hits against B while B kills that NPC. Native per-owner rewards remain absent; full M03 stays BLOCKED. Earlier failed combat behavior and sealed evidence remain in `../evidence/2026-09-12-m03-context/`.

The isolated wrapper accepts `-ObservationMode Actors`, mapped to the native host's isolated `--actors` mode. It enables only `SPOREMP_M03_ACTORS=harness`; normal Play, reference launch and observation clear this switch. Actual native procedure, commands and prerequisites: [M03.md](../tests/engine/M03.md). The current full M03 gate is BLOCKED despite partial native successes; do not infer completed co-op from command enqueue acceptance, native void returns, visible AI animation, or passing host tests.

`python tools/native/analyze-m03-probes.py --input-dir local/M03_ARCHIVE --output local/FRESH_REPORT.json` summarizes the privately preserved native probes. Use `--runs` to select the retained probe count; the default is five. It reports malformed/sequence errors, native call pairs, command-to-landing correlation, sampled movement goals, resources, global rewards, loaded payload hashes and fixture changes. It deliberately does not grant milestone acceptance. Failed native attempts remain failures. The archived one-off `review-frames.py` extracted actual WGC contact sheets into ignored local storage for manual review; the extraction manifest preserves exact FFmpeg commands/hashes and is separate from `visual-review.json`. Contact-sheet sampling is not native frame timing.


## M11 fossil and model-placement continuation (2026-09-28)

Bridge .144 trial78 qualifies normal A fossil collection, both replica removals
and original Save.79/81 restore exactly683 canonical player/inventory records
without replay, but expose a rendered B model left at the saved A position.
Scalar coordinates and native landing alone do not establish visual acceptance.
.146 trial82 fixes initial placement only; moving model remains stale.
.147 trial84 qualifies bounded visible model following in both clients, both
normal jump landings and exact683-row restore. Each client has111 settled
readbacks; maximum model/source distances are0.00898 for A and0.36527 for B.
Exact equality is not established. Six exits, closed traces, protected29 files
and50 frozen hashes pass. The latest full CTest run (.146) remains19/19 in61.69s;
unrelated host tests were not repeated for this native rendering check.

Native checks must compare rendered behavior with admitted source state and
verify closed evidence. Restore the exact
checkpoint.smp file and require a successful restore exit before launching;
trial80 records the mistakenly supplied directory and safe authority refusal.
See [the work audit](m11-work-audit.md) and the [native session (archived)](public-evidence.md#historical-artifacts).


### B fossil ownership and first-part tutorial, .148-.149

Trial85/.148 proves native B part attribution but fails continued play because
the original first-part tutorial pauses the authority. Trial86/.149 defers that
specific original request to B, validates native pause/confirmation completion,
records B's original completion marker and leaves A's progress unchanged. A
jumps while B's tutorial is pending; B jumps afterward. Original Save2 completes.
Trial87 loads its sealed result into three fresh original processes: all684
player/inventory rows match, A57/B1 parts and DNA35/17 remain, both consumed
fossils are absent and both normal jumps land. No new Save or replay occurs.

The .149 build and affected native_actor_abi_host/network_host tests pass2/2
in4.90s. Existing .146 full-suite19/19 evidence is retained separately, not
reported as a .149 full-suite run. Both new native runs close with all six
exits0, three healthy single-thread traces,55 frozen hashes and29 protected
files unchanged. Actual loaded views and all full-duration filmstrips were
inspected. Fixture undo passes after each run. Private review scripts and
reports are under local/m11-resume-2026-09-27/review-fossils86.py and
review-fossils87.py. That increment left live inventory presentation open; the
.150 check below adds it. Distinct-species progression still remains open.


### Owner inventory and original part card, .150

Build150 initially fails on an incorrectly qualified game-mode constant; the
existing SDK global constant correction builds successfully. The affected check
command is `ctest --test-dir build/win32 -C Release -R '^(native_actor_abi_host|network_host)$' --output-on-failure`:2/2 pass in4.50s.
They exercise bounded inventory decoding/assembly, mid-publication admission,
stale revision and wrong-owner rejection, owner-only routing, and4096 native
notification ABI stack/receiver/key checks. No unchanged full suite is repeated.

Trial88 restores sealed78 and proves normal B collection, exact312-row owner
inventory application, the original Manglerfish card, no A card/update, owner
tutorial completion, both normal jumps and original Save. Trial89 restores its
sealed save and compares all684 saved records plus both312-row client inventories
exactly. No reward/card/tutorial replays. Its extra control check is incomplete
before automatic closure; the160-second capture does not cover that late attempt.
All retained filmstrips are inspected, all six exits per run are0,59 frozen
source/build hashes and29 protected files match, and both restore layers are
undone. Private review scripts are review-inventory88.py and review-inventory89.py
under local/m11-resume-2026-09-27. Full C08/M11 remains open. See the
[native session (archived)](public-evidence.md#historical-artifacts) for pins and commands.

### Native B home creation and restore, .155/.156

Build .156 and `ctest --test-dir build/win32 -C Release -R '^network_host$'
--output-on-failure` pass (1/1 HOST,4.03 seconds). The test covers nest wire
identity, non-interactable admission, late baselines, incarnation retirement,
and suppression of unchanged static state without hiding changed/invalid state.

Native94 creates B's original nest, projects its original model through the
audited native setter, observes both normal jumps and completes original Save1.
The initial run also has one transient reconnect per client. .156 suppresses
unchanged nest packets using the existing static-resource publication policy.
Native95 restores the sealed94 artifact into three fresh game processes:
the same B nest/herd/model exists without another factory call, both clients
admit123 nests,684 progression rows match, both normal jumps land, and each
client stays connected for103 seconds. Inspect the native report for original
terrain-position movement, retained failures, exact hashes and cleanup. This
is bounded C05 foundation evidence; B respawn and complete packs are NOT RUN.

### Owner hatch ability eligibility, .166

Release BUILD exits0. `ctest --test-dir build/win32 -C Release -R
'^(native_actor_abi_host|network_host)$' --output-on-failure` passes2/2 in5.07s.
The new eligibility ABI fixture verifies detour attachment, signed ability
argument, full EAX result,4096 caller-stack/exact-call checks, and detached
original dispatch. Native tests are separate: trial111 reaches owner lesson
input and original adult growth, but its authority hits the checkpoint budget
before clean closure. Save success alone does not reset that publication budget.
The strict checkpoint sealing gate is retained. Trial112 installs a native-save
completion watcher with a310-second fallback deadline; the deadline is the
actual recorded trigger. Original Save completes before orderly trace stop,
all six exits are0 and strict sealing succeeds. The watcher generates no
gameplay input. Its captures end about two seconds before Save; they establish
the inspected lesson/growth/controls, not a visible Save UI result.
See the native session report for closed trace and restore outcomes.

### Populated-herd melee respawn, .167

The Release build before trial115 exits 0. The full command
`ctest --test-dir build/win32 -C Release --output-on-failure` passes 19/19 in
62.96 seconds (`local/m11-resume-2026-09-27/ctest-115-full.log`). These checks
start no native game. Earlier affected ABI/network checks and the eight pure
herd-admission cases remain recorded in the session report.

NATIVE trials115-116 qualify one B melee respawn after loading its populated
two-member herd, preserving both members and A's scoped state, original
hatch/growth, normal confirmation and jump controls, Save3 and fresh restore.
All 684 recorded player/inventory rows and 123 nests per view survive; the
fresh clients both jump without lifecycle/reward replay. Combat setup uses a
separately classified diagnostic intention and stop after the first original
hit. It is not normal combat-control acceptance or a fabricated damage call.
The second setup times out approaching a different pooled baby under the old
native ID; it does not exercise another death or same-process context reuse.

The closed review commands are `python -X utf8
local/m11-resume-2026-09-27/review-hatch115.py` and `python -X utf8
local/m11-resume-2026-09-27/review-hatch116.py`, both exit 0. Each verifies 67
frozen entries, healthy trace closure, six zero process exits and 29 unchanged
protected files per role. Original footage and all 48 filmstrip cells per
trial were inspected. Full C10 and all twelve complete M11 gates remain open.


Trial118 extends this result to two original B melee deaths and normal owner
confirmations in the same authority process. Both original two-member herds
are preserved, one private hatch sequence is constructed and reused, original
growth completes twice, and A's scoped state remains unchanged. Normal A
jumps before the first death and during the second pending death land; B
and A both jump after second growth. Save3, strict sealing and all 684 loaded
and before/after-save census comparisons pass. The closed review command
`python -X utf8 local/m11-resume-2026-09-27/review-hatch118.py --visual-inspected`
exits 0, verifying 68 frozen entries including the diagnostic helper, six
zero process exits, three healthy traces and 29 unchanged protected files per
role. All 48 filmstrip cells and the live confirmation/growth/control views
were inspected. Trial119 freshly restores this new seal into three new
generations with all 684 census rows exact, both original adult identities,
123 nests per view and both normal jump landings. No lifecycle or reward
replay occurs. Its review command `python -X utf8
local/m11-resume-2026-09-27/review-hatch119.py` exits 0, verifying 67 frozen
entries, six zero exits, three healthy traces and 29 unchanged protected files
per role. All 48 filmstrip cells and live adult/control views were inspected.
A's saved starvation continues without death; B stays at health 12.5. Full
C10 and all twelve complete M11 gates remain open.

The first trial118 startup used Windows PowerShell5 and failed on the
unsupported `utf8NoBOM` encoding after starting the coordinator, before any
game launch. That exact coordinator was verified and stopped, the two partial
directories were preserved, and startup was repeated with PowerShell7 against
the same restored fixture. The retained launch-tool error is not a native
game failure or an extra gameplay cycle.


### Foreign-species initialization prerequisite, .168

The full Release build exits 0 (`build168.log`). Focused HOST tests
`worker_host`, `native_actor_abi_host` and `network_host` pass 3/3 in 18.46
seconds (`ctest168.log`). The new worker mode validation and native getter
ABI fixtures are separate from gameplay acceptance.

Trial120 uses one isolated local worker with a verified fresh one-player
source. Original foreign B creation, independent player initialization and
one scoped original species normalization pass with A unchanged. B's
diagnostic DoJump call returns false and has no landing; A's diagnostic jump
lands. Foreign controls remain NOT VERIFIED, and no NETWORK-REAL, normal
owner UI, foreign progression, Save or Save-refusal exercise is claimed.

`python -X utf8 local/m11-resume-2026-09-27/review-foreign120.py --visual-inspected`
exits 0 with `BOUNDED_FOREIGN_INITIALIZATION_PASS_FOREIGN_CONTROLS_INCOMPLETE`.
It verifies all 69 frozen entries, healthy closed trace, two zero process
exits, 29 unchanged protected personal files, retained source hashes and
exact restoration of both disposable profile folders. All 16 filmstrip
cells and the live original species views were inspected. Full C06 remains
open; this review outcome does not imply complete control acceptance.

### Foreign-species network admission and retained failures, .169-.171

The .169 Release build succeeds. Its initial conversion warning is corrected
and the final build has no warning. Initial HOST checks pass native_actor_abi_host
in 1.29 seconds and network_host in 4.12 seconds; worker_host fails because
the new wire fixture omitted its required generation. After that fixture is
corrected and rebuilt, worker_host passes in 13.24 seconds. The retained
initial failure is not a native gameplay result. Logs are under
`local/m11-resume-2026-09-27/`: `build169.log`, `build169-test-fix.log`,
`build169-final.log`, `ctest169.log` and `ctest169-worker-final.log`.

The .170 diagnostic build exits 0 and native_actor_abi_host passes in 1.80
seconds (`build170.log`, `ctest170.log`). The .171 archetype correction builds
successfully and its native_actor_abi_host/network_host pair passes 2/2 in
5.84 seconds (`build171.log`, `ctest171.log`). These are BUILD/HOST results.

The closed reviewers `review-foreign121.py --visual-inspected-by-root` and
`review-foreign122.py --visual-inspected-by-root`, run with Python from
`local/m11-resume-2026-09-27/`, exit 0 while retaining incomplete native
outcomes. Both authority bootstraps succeed; both client baselines reject
before promotion. Trial122 identifies the original default animal versus
home-herd archetype mismatch without changing admission. No gameplay control,
Save or checkpoint acceptance is claimed. Each review verifies 76 frozen
entries, six zero exits, three healthy traces, 29 protected personal files
per role and all three profile restorations. All 48 filmstrip cells from
each run were inspected. Exact commands, hashes and the .171 native-provenance
correction are recorded in the session report. Full C06 remains open.

The closed .171 trial123 reviewer also exits 0 with an explicitly incomplete
outcome: `FOREIGN_BOOTSTRAP_PROMOTION_PASS_B_MOTION_NOT_DEMONSTRATED_RECONNECT_GUARD_REJECTED`.
Both baselines and owned promotions pass. Normal B input reaches the original
functions, but its zero-level jump is refused and a 41.779 ms movement pulse
produces zero observed displacement. A's jumps land, including after B exits.
Fresh B reconnect fails world-hash admission before any new game launches.
The review verifies 76 frozen entries, three healthy initial trace stops,
six initial zero exits, 29 protected files per role and all three original
profile restorations. All 48 filmstrip cells were inspected; visible model
and camera promotion do not establish complete UI parity. No Save or foreign
durability acceptance is claimed. Run details and report hash are in SESSION.

Trial124 retains the same .171 bridge. Its final reviewer command is
`python local/m11-resume-2026-09-27/review-foreign124.py --visual-inspected-by-root --output closed-foreign-network-review-final.json`,
exit 0. It verifies diagnostic native B displacement of 5.5019504, a fresh
baseline 3 with exact entity/species and initial moved position, and A's normal
jump landing between the two B sessions' exit and owned-baseline boundaries.
Later A starvation death is retained. The explicit stopped-worker fixture
reset preserves all original journals and six configured world hashes.

The review verifies 78 frozen entries, four healthy traces, eight zero exits
(old B pair plus the final three pairs), 29 protected files per run and all
three original profile restorations. All 64 filmstrip cells were inspected.
Post-reconnect B controls are NOT RUN: automatic closure precedes the optional
UI attempt and WorkerControl returns exit 3 with win32=2 before dispatch.
No normal B keyboard movement, automatic recovery, complete UI or foreign
persistence acceptance is inferred. Full C06 and all full M11 gates stay open.

### Foreign B portrait binding and baseline refresh, .172

The initial Release build exits 1 because the SDK display type has ambiguous
AddRef/Release bases (`build172.log`). The correction retains its declared
IWinProc interface with the original +38 adjustment. The repaired Release
build exits 0 without warnings (`build172-final.log`). The focused command
`ctest --test-dir build/win32 -C Release -R '^native_actor_abi_host$'
--output-on-failure` passes 1/1: 1.29 seconds for the test, 1.30 seconds total
(`ctest172.log`). Its portrait fixtures verify the thiscall lookup, cdecl image
setter, exact key reference, signed image index, original true/false and null
returns, and 4096-call Win32 stack balance. These are HOST fixtures, not native
image or gameplay acceptance.

The static audit pins the original portrait control 03D99151, its current
avatar profile key and the Image::SetBackgroundByKey call at 807CA0. The new
adapter performs one synchronous attempt only after a committed foreign B
baseline, preserves A/default identities and reports the original result.
It never invokes the enclosing D2C000 routine, which also changes ability
mode. The private exact binding and resource report is retained under
`local/m11-resume-2026-09-27/hud-static-resources/`. No extracted game assets
are published. History contents and full UI parity remain unverified.

Trial125's closed reviewer runs with
`python local/m11-resume-2026-09-27/review-foreign125.py
--visual-inspected-by-root --output closed-foreign-network-review-final.json`
and exits 0. It checks 81 frozen entries, four healthy trace stops, eight
zero game/host exits, 29 protected files per run and three restored original
profiles. The exact source manifest, reviewer, report and payload hashes are
recorded in the trial125 SESSION entry. All 64 filmstrip cells and live
views were inspected; capture durations are 166.733, 166.433, 47.366 and
96.000 seconds for authority, A, initial B and reconnected B respectively.

Both B baselines make one successful original portrait assignment with
unchanged identities, corroborated by visible green B portraits and retained
blue A portrait. Diagnostic B movement measures 5.50665676 units initially
and 5.59924252 final units after reconnect (maximum 5.59957035). The fresh
baseline preserves entity, generation, species and exact moved position;
23 controlled samples have zero position delta before the second move.
A's normal jump lands between old B exit and fresh B owned baseline.
Normal B Space after reconnect reaches original DoJump and preserves its
zero-capability refusal. These are bounded native results. Ordinary B
keyboard movement, unopened history contents, automatic recovery, foreign
persistence and full C06 are not verified. No Save/checkpoint or Save-refusal
test runs; all twelve complete M11 gates remain open.

### Private native relationship lifecycle and preflight refusals, .173

Release builds run through
`pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` and exit
0. The initial focused HOST command
`ctest --test-dir build/win32 -C Release -R '^(actor_commands_host|native_actor_abi_host|network_host)$' --output-on-failure`
passes 3/3 in 5.43 seconds (`build173-initial.log`, `ctest173-initial.log`).
After the first-entry sentinel comparison and nested foreign damage-owner
latch corrections, the same checks pass 3/3 in 5.95 seconds
(`build173-final.log`, `ctest173-final.log`). The final tutorial integration
build also exits 0; the focused command
`ctest --test-dir build/win32 -C Release -R '^(native_actor_abi_host|network_host)$' --output-on-failure`
passes 2/2 in 5.92 seconds (`build173-tutorial.log`, `ctest173-tutorial.log`).
These logs are retained under `local/m11-resume-2026-09-27/`.

The added ABI fixtures cover the original six-argument allocator, constructor,
deleting destructor, relationship getter/action/effective-read conventions,
and native collection callback arguments. They check exact-once forwarding,
full pointer returns and Win32 stack balance across 4096 calls. Pure checks
cover unsigned threshold boundaries, the original fifth-word initialization
for a previously absent record, and the two permitted outgoing damage caller
sites. These are HOST results and do not qualify native relationship changes.

The final trial126 bridge SHA-256 is
`fabe8d64f44aa9d93eba2a06e7b1e4bb9fd58f562f0fe06d3c3b8070603c074d`.
The closed reviewer command is
`python local/m11-resume-2026-09-27/reviewer126.py --visual-inspected-by-root`,
exit 0. Reviewer SHA-256:
`f48606fbc9e6ed5ce6dfb5d65fb0eb922c1993ef4d75e5426707bb85ffe5099a`.
Report `local/m11-resume-2026-09-27/trial126/closed-relationship-preflight-review.json`
SHA-256: `8800aa55c670e06ec341b88fdbd5f8559ddd03af8762f6fb503d1795f925d5c1`.
Its outcome is
`PRIVATE_NATIVE_RELATIONSHIP_LIFECYCLE_AND_TWO_PREFLIGHT_REFUSALS_PASS_COMBAT_NOT_RUN`.

The review verifies all 89 frozen source/artifact entries, three healthy trace
stops, six zero game/host exits, 29 unchanged protected personal files per run,
and three restored profile journals with original backups and closed roots.
One native 96-byte constructor-only relationship manager is enabled for B,
without listener registration, then retired through its original deleting
destructor. Original foreign Stage/profile initialization and both client
promotions pass; this does not establish an independently owned combat result.

Exactly two diagnostic candidates are refused: approach to native target 90
and combat ability index 5 against target 95. Both native reads are valid,
but first kill count 0+1 reaches fear threshold 1, below extinction threshold
3, and both herds have ShouldEvolve set. No combat command, original animal
damage callback, relationship action, kill or B reward follows. Tutorial
deferral is enabled, but its receipt stays unrequested and incomplete; foreign
First DNA presentation/completion and normal Bite remain NOT RUN.

After the refusals, normal B Space reaches original DoJump and returns false
with native jump level and selected power zero. Normal A Space returns true
and lands. Both native DNA samples are zero; B's reward, goal and combat
counters stay zero. Final authority state has A alive at health 1.11352503
and B alive at health 6. A's 6896 original starvation damage calls are retained
separately from the absent B combat damage.

The retained visual receipt verifies all 48 filmstrip cells against their exact
hashes, plus live owned windows. B's body/portrait remain green and A's portrait
remains blue. Durations are 173.533 seconds for authority, 173.266 for A and
173.433 for B. These are sampled visual observations; command outcomes and
health values come from closed native traces. No Save, checkpoint-refusal,
reconnect or persistence exercise occurs. No full C06 or M11 gate is closed.


### Foreign first-kill/history result and corpse retirement, .174–.175

Release builds use
`pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release`.
The .174 build exits 0; focused HOST command
`ctest --test-dir build/win32 -C Release -R '^(actor_commands_host|native_actor_abi_host|network_host)$' --output-on-failure`
passes 3/3 in 5.79 seconds (`build174-initial.log`, `ctest174-initial.log`).
Both .175 builds exit 0; the final focused command
`ctest --test-dir build/win32 -C Release -R '^(native_actor_abi_host|network_host)$' --output-on-failure`
passes 2/2 in 6.16 seconds (`build175-initial.log`, `build175-final.log`,
`ctest175-final.log`). No build warnings or errors remain. Logs are under
`local/m11-resume-2026-09-27/`. The new HOST fixtures cover native history
allocation/initialization/deletion, cdecl event forwarding and full pointer
returns, repeated x86 stack balance, exact caller containment and first-kill
threshold boundaries. Completed-target retirement is covered by the separate
native trial below. These HOST tests are not
native gameplay evidence.

The trial 127/.174 bridge SHA-256 is
`0932368b952cca2e5cb5fcae06bf5c3196339c1cf7bfb7b0c16242a108178e7f`.
Its closed report, `trial127/closed-firstkill-review.json` under the same local
root, has SHA-256
`53aed0caa3aea659960a2dbe610f4a158877b243ebdac95f4756cb0f852ad429`.
The outcome is `FIRSTKILL_PASS_POSTKILL_CORPSE_RETIREMENT_FENCES_CONTROLS`;
whole-trial acceptance remains INCOMPLETE. It verifies 95 frozen entries,
three healthy trace stops, six zero game/host exits, 29 unchanged protected
files per run and all three original profile restorations.

The original first-kill chain is observed in both 127 and 128: six Bites of
one damage kill target 90, and five half-damage NPC counterhits leave B at 3.5
health. B's native DNA, goal and combat progress rise from 0 to 7, while A's
progress remains 0. The first kill changes only B's effective relationship
from 3 to 1 at the original fear threshold 1, below extinction 3. The original
shared target-herd effects execute, with A's relationship/profile/owned herd
unchanged in the recorded checks. One original 64-byte private history root
holds B's new category 8/event 5/relation 1 entry; its membership and payload are
verified independently of the getter selection. A's complete observed history
is unchanged. Original owner-only First DNA pauses/resumes on B and completes
one authenticated native marker without crediting A. This is diagnostic
network combat, not normal Bite UI acceptance.

Trial 127 then exposes the erroneous corpse-retirement fence roughly 60 seconds
after death. A prior normal W pulse reaches WalkTo/release without meaningful
displacement; the later A/B Space requests are refused. Trial 128's .175 bridge,
SHA-256 `f6bb4d0a8de5d34d4b1434d98d24bde8713fe748bcdd3752a8f26a652de0822b`,
retains the completed result when the original corpse is returned to its pool.
Authority event `native_relationship_completed_target_retired`, sequence 37404,
native time 170.1422647 seconds, records the borrowed identity released and
owned results preserved. No relationship acceptance failure or combat input
fence follows that retirement; the normal shutdown fence is a separate event.
The final B reward state still contains 7 DNA, 7 goal and 7 combat progress.

Automatic closure at 17:06:28–29 UTC precedes the planned final control inputs;
post-retirement controls are NOT RUN. No Save, checkpoint, reconnect or
foreign persistence exercise occurs. Native history storage is verified;
its UI contents, independent objective receivers, normal Bite controls and
further transitions are not. Full C06 and all twelve full M11 gates remain open.

The exact closed-review commands are
`python local/m11-resume-2026-09-27/closedreview127.py --visual-inspected-by-root`
and
`python local/m11-resume-2026-09-27/closedreview128.py --visual-inspected-by-root`,
both exit 0. The final trial 128 report is
`local/m11-resume-2026-09-27/trial128/closed-firstkill-review.json`, SHA-256
`00ca889ae881fec67204a7497b398c829c14869b775c4ec399d7c244f86259c8`.
The reviewer SHA-256 is
`9e18dd803702e1ae2e51c8c7a2ed258a830f31a96153d408837ed33da5de3bc8`.
Its outcome is
`FIRSTKILL_HISTORY_TUTORIAL_AND_COMPLETED_CORPSE_RETIREMENT_PASS_POST_RETIREMENT_CONTROLS_NOT_RUN`;
whole-trial acceptance is INCOMPLETE. It verifies 96 frozen entries, three
healthy trace stops, six zero exits, 29 unchanged protected files per run and
three restored original profile journals. All 48 sampled filmstrip cells were
inspected; the three captures are 190 seconds each.

The 62 scoped A/history checks extend through native time 170.142202 seconds,
not continuously through closure. First DNA completes at 133.660436 seconds.
A later dies from its continuing saved starvation at 201.117213 seconds;
final health is A 0/B 3.5. The only input fence, at 202.5104675 seconds, is
immediately followed by network shutdown reporting that the game session has
closed. It is not the earlier relationship-retirement defect. These bounded
checks establish result preservation; no missed control input is counted as
executed or successful.


### Owner Sporepedia Creature history, .176/trial129 (2026-10-01)

Trial129 passes the bounded owner-history display: B's original first kill
is replicated as an atomic schema25 snapshot and appears only in B's original
Sporepedia Creature Game collection. A keeps its own collection; B's entry
remains correct after closing/reopening. Nine original B queries return its
one owned entry. No history event or reward is replayed on either client.
The separate evolution timeline remains unqualified.

After completed corpse retirement, A's normal jump lands and B's diagnostic
move travels5.2670313native units. Both remain alive through early closure.
The final build exits0; focused native_actor_abi_host/network_host pass2/2
in5.82seconds. Closed review verifies99 frozen entries, three healthy traces,
six zero exits,29 protected files per run and all three profile restorations.
All48 filmstrip cells and the live original views were inspected.

Published original history roots retain their borrowed UI entries until
process exit, with a16-root bound and compatible-state reuse. Unpublished
roots use original deletion. This is bounded experimental history presentation;
foreign Save/reload, native history reconnect, sustained normal B movement,
full independent progression and all twelve complete gates remain open.
Exact commands, hashes, original reader bindings, UI lifetime limits and
observations are in the [native session report (archived)](public-evidence.md#historical-artifacts).

### Owner-B Creature goal card after restore, .182–.183/trials148–149 (2026-10-02)

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release -R '^(actor_commands_host|native_actor_abi_host|network_host)$' --output-on-failure
```

BUILD exits0 and the three HOST suites pass3/3 for both builds. The HOST
additions cover the `BA3B40` insert, `C2FBB0` goal refresh and `E17BF0` card
populate ABIs, the goal value selection and the two display-only getter return
sites. These are not native acceptance.

Trial149 passes the bounded display check: on a fresh restore of sealed146,
B's client shows Hunt 1/3 "Hunt 2 Blatardo" and A's client Hunt 2/3 "Hunt 1
Blatardo", with one goal refreshed, one card populated, exact cached values and
no reward on any process. Trial148 (.182) is the retained failed presentation
whose goal values changed while the card text stayed stale. Closed review
verifies132 frozen entries per trial, six healthy traces, twelve zero exits,
29 protected files per role and both profile restorations; all96 filmstrip
cells and both enlarged card regions were inspected. Neither run sends input.

This presents saved state only. Live relationship replication to the owner
client, cards for species only B has met, goal completion and B's own goal
lifecycle remain open, as do all twelve complete gates. Exact commands, hashes,
bindings and the retained HOST layout observation are in the
[native session report (archived)](public-evidence.md#historical-artifacts).
