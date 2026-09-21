# Testing and evidence contract

## SDK Detours 4 build qualification — 2026-09-21

The supported build replaces the SDK's old Detours 3 dependency with pinned MIT Detours 4.0.1. Run `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release -FetchDependencies` before building bridge/native-host targets; direct CMake builds reject missing or stale SDK provenance. The Release build and all **10/10 HOST/FIXTURE CTest targets pass**, including the actual SDK wrapper hook test and 122 Python tests. The Debug SDK/base build and its focused SDK wrapper test also pass. These are BUILD/HOST results, not original-game acceptance of the changed SDK DLL. Historical native results still refer to their recorded payloads. See [migration details](detours4-migration.md) and [the reviewed session](../evidence/2026-09-21-detours4-license/SESSION.md).

The full acceptance text for M00–M20 is retained in `../MILESTONES.md`. Every milestone records its objective, dependencies, changed modules, commands, expected/observed outcomes, evidence, unresolved issues and next step. Current M01 observations and user acceptance are in ../evidence/2026-09-08-m01-native/SESSION.md. Do not repeat completed tests without a relevant change, failure or unresolved concern.

## M05 verified Creature fixture

M05 is **VERIFIED for the recorded living Creature fixture**, bridge **0.0.29**. [Current native evidence](../evidence/2026-09-14-m05-ability/SESSION.md) and the [acceptance decision](../evidence/2026-09-14-m05-ability/acceptance.md) separate original-game execution, inspected captures, static audit and HOST fixtures. A real 8.75-DNA result is applied without another award; current evidence adds active-bite cleanup, actual foreign-species UI and ordinary saved-world Play denial with working Cancel. Retained Save/movement/Off/disconnect evidence is not rerun or relabeled. Full scene, inventory, charge/projectile, death/respawn and perceptual listening coverage remains unqualified; see [the native protocol](../tests/engine/M05.md).

```powershell
cmake -S . -B build/win32 -G 'Visual Studio 17 2022' -A Win32 -T 'v143,version=14.44.35207' -DCMAKE_SYSTEM_VERSION=10.0.26100.0
cmake --build build/win32 --config Release --target SporeMP.NativeHost SporeMP.WorkerControl sporemp_replica_tests sporemp_worker_tests --parallel 4
ctest --test-dir build/win32 -C Release -R 'replica_host|worker_host|native_actor_abi_host|actor_commands_host' --output-on-failure -V
python -m unittest discover -s tests/unit -p test_replica_probe.py -v
```

The current replica target contains **313** policy/fence/lifetime assertions, **25** actual HOST Detours/Win32 ABI checks and **9** interior-branch ABI checks, including 4,096 alternating branches with deliberate register/FP clobber. Existing actor/award/persistence ABI coverage is **34**. `build-029` has zero warnings and `ctest-029` passes the affected replica target; `ctest-final-028` passes the full **8/8** suite in **6.05 s**. Five focused Python tests pass in `python-027`. Exact commands/exits are in the current session. No game is launched by these commands. Full normal-Play regression and native terminal trace-budget exhaustion are not inferred from these tests.

For native experiments, `start-worker.ps1 -M05Role Authority|Replica` is explicit and isolated. `replica-probe.py` controls already running workers; it does not manufacture results or launch a game. A validated .16–.28 authority source can be reused by the current .29 receiver with the same scalar capture ABI. The current source is the real .18 combat reward. `replay --challenge` invokes actual native entries with live receivers; queued charm/no-attack probes require a later natural-update result, and are explicitly temporary field fixtures rather than actual casts. Keep failed runs, inspect actual captures, observe exits and compare closed save hashes. `evidence/2026-09-14-m05-ability/analyze-closed.py FRESH_NAME.json` analyzes archived traces/media and retains `full_m05_acceptance: NOT_INFERRED_BY_ANALYZER`; the separate acceptance decision evaluates the original milestone clauses.

Optional audio evidence uses the separate `tools/native/audio-capture` CMake project and `SporeMP.AudioCapture.exe GAME_PID SECONDS OUTPUT.wav` (1–60 seconds). It captures only the selected process tree with Windows process loopback, 48 kHz stereo PCM16, no input and no audio processing. Building or running HOST tests never starts SPORE. Packet integrity does not replace listening; exact build/capture commands and Microsoft API references are retained in the presentation session.

## M04 focused worker increment

M04 is **VERIFIED for the recorded bounded original Creature worker fixture**, bridge **0.0.14** / launcher **0.1.8**. The [current native acceptance session](../evidence/2026-09-13-m04-acceptance/SESSION.md) separates original execution from compiler, ABI and fixture checks and records the final evidence/source freeze. The original M00–M20 acceptance clauses remain intact. Future B campaign/reward persistence, arbitrary checkpoints, Internet clients, locked/disconnected desktops and capacity remain unqualified.

The .14 integration Release build and focused worker/actor-command/ABI targets pass 184/15/34 assertions; the ABI count includes eight new persistence checks and stack-balance verification. The latest .1.8 Release build has zero warnings/errors, and the affected CTest run passes 2/2 targets with 39 launcher assertions and 98 Python tests. Checkpoint-sidecar coverage includes malformed/ambiguous pairs, crossed epochs, numeric widths, changed trace/backup/native tree and legitimate status request zero. Poll/lifetime fixtures exercise stable dropdown entries, selection during refresh, serialized/duplicate mutations, cancellation and launcher-test-process closure without killing the fixture backend. Exact pending-mutation closure remains HOST/FIXTURE evidence. The native worker bridge was not rebuilt for the launcher-only poll fix.

The new NativeHost runtime-log exception passes 18 diagnostic tests. Only exact regular `SporebinEP1/spore_log.txt`, authored by pinned SDK `Application.cpp:123–202`, is excluded from immutable-content equality and fingerprinted separately. Changed content/payloads, directories, reparse points and unknown sibling files remain rejected. Concurrent native runs use host SHA-256 `1559d4540bfdcbb02a40c07f50c048461881dc3e57dcfe6585ffca86d0e2a4a2`; .14 bridge, SDK core and injector are unchanged. See [runtime provenance](../evidence/2026-09-13-m04-acceptance/final-runtime-provenance.json).

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

Two concurrent original saves have at least **414.7126 ms** overlap from bounded native/host QPC alignment. Actual `stars.db.tmp` Write intervals overlap **20.1228 ms**. Both later closed checkpoints match the source native request/epoch/fingerprints, trace prefixes and healthy clean-stop traces; backup manifests, all payload hashes and current closed Games trees match their sidecars. Those exact newly sealed concurrent artifacts are not reloaded; the earlier checkpoint recovery remains separate. See [concurrent save](../evidence/2026-09-13-m04-acceptance/concurrent-save-01.json), [checkpoint cross-check](../evidence/2026-09-13-m04-acceptance/concurrent-checkpoint-crosscheck.json) and [full-lifetime concurrent file audit](../evidence/2026-09-13-m04-acceptance/concurrent-isolation.json). Neither native save success nor closed hashes prove power-loss durability.

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

The acceptance pair's `evidence/2026-09-09-m02-completion/measure-frames.ps1` records exact PID-specific arguments, start/end times, exit code, CSV and whole-process CPU delta. It validates the actual executable path and tool hash. The used options are `--process_id PID --output_file FRESH_CSV --session_name UNIQUE_NAME --timed 60 --terminate_after_timed --no_track_input --no_console_stats --v1_metrics`. Confirm output PID, D3D runtime and swapchain, and keep the avatar stationary during the measured window. Native D3D9 `msBetweenPresents` is a presentation interval, not a simulation tick, input delay or compositor cadence. One capped comparison cannot isolate hook cost or certify a universal operating envelope.

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

`evidence/2026-09-09-m03-actors/analyze.py` summarizes the preserved five native probes into a fresh `native-analysis.json`. It reports malformed/sequence errors, native call pairs, command-to-landing correlation, sampled movement goals, resources, global rewards, loaded payload hashes and fixture changes. It deliberately does not grant milestone acceptance. Failed native attempts remain failures. `review-frames.py` extracts actual WGC contact sheets into ignored local storage for manual review; the extraction manifest preserves exact FFmpeg commands/hashes and is separate from `visual-review.json`. Contact-sheet sampling is not native frame timing.
