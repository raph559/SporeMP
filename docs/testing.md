# Testing and evidence contract

The full acceptance text for M00–M20 is retained in `../MILESTONES.md`. Every milestone records its objective, dependencies, changed modules, commands, expected/observed outcomes, evidence, unresolved issues and next step. Current M01 observations and user acceptance are in ../evidence/2026-09-08-m01-native/SESSION.md. Do not repeat completed tests without a relevant change, failure or unresolved concern.

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
