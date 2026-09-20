# M04 worker supervision increment — 2026-09-12 / 2026-09-13

Request: **"lets go with m04"**. Result: **M04 IN_PROGRESS**, bridge **0.0.13**, launcher **0.1.4**. Local process supervision, bounded private IPC, two prepared OS account fixtures and Settings controls are implemented with focused BUILD/HOST/FIXTURE evidence. The only original-game worker attempt fails renderer initialization on a private desktop. Native checkpoint load, simultaneous original-game simulation, native isolation and player-client independence are not accepted.

## Source and environment

HEAD remains `d106404da0b7c4531f63f98d0df2377bb897a0f7`. Existing M02/M03 and launcher edits were already uncommitted and are preserved; no reset, commit or milestone scope reduction occurred. The final snapshot contains **127 project source/configuration/document files** at ignored `local/m04-final-source-02.zip`, with its SHA-256 recorded in provenance-02.json. Each file and current Release artifact has a separate hash in [provenance-02.json](provenance-02.json). All 84 original dependency/work/deliverable/acceptance/critical-gate paragraphs in HEAD's milestone plan remain verbatim.

The failed early native probe predates the final source snapshot. Its exact pre-run source snapshot was **not recorded**. A content-addressed payload staged at 21:49:27 UTC is preserved and separately hashed as a **candidate**: the original host log omitted its payload path and failed before module enumeration. Neither the final source archive nor that candidate is presented as observed native bridge loading. The failed run's actual guard/process/timeout/personal metadata is copied unchanged under [private-desktop-01](private-desktop-01/native-host.jsonl).

The executable is GOG GA 3.1.0.29, Win32, original campaign, `C:\Games\SPORE\SporebinEP1\SporeApp.exe`, SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. Both the native guard's 108-file validation and a final read-only candidate validation match the recorded content profile. SDK commit: `cbf9206b9a823f0911cd9be0217104a49d72380b`; injector: `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. Native toolchain: MSVC v143 14.44.35207, Win32 Release /MD, Windows SDK 10.0.26100.0. Launcher: .NET SDK 8.0.418, WindowsDesktop 8.0.24. No new dependency installation occurred.

[host-context.json](host-context.json) records Windows 11 Pro 10.0.26200 x64, Ryzen 7 9700X (8 cores/16 threads), 33,407,430,656 bytes physical RAM, RTX 4080 SUPER driver 32.0.15.9649, AMD integrated graphics and the installed Parsec virtual display adapter. PowerShell 7.6.5, Python 3.11.15, CMake 4.3.2. This is an OS inventory, not a claim about which adapter rendered the failed scene. No SPORE process is running at final inventory.

## Implemented change

- `src/worker`: fixed 128-byte little-endian scalar protocol, exact generation/sequence validation, real local overlapped named pipes, SID/PID checks, bounded queues, actual process/job supervision, readiness/progress/timeout/crash/forced-stop records, and a typed control executable. Closing an operator leaves the worker alive; supervisor loss contains its own child.
- `src/bridge/native_worker.*`, actor wrapper and lifecycle: one app-update-thread request dispatch into the already verified M03 native actor queue; status uses actual app-update and original AI-entry counts. No replacement gameplay, network thread or pointer serialization. Only `src/bridge` imports the SDK. Reserved load/save operations return unavailable.
- Native host/tooling: per-SID mutex and real process-token rejection before launch; separate standard worker identities and mutual path guards; original `-multipleInstances` flag identified in pinned machine code; private or explicitly current rendered desktop; bounded startup and original message-loop shutdown request. The clean shutdown path is statically supported but native execution is NOT RUN.
- Two owned standard accounts, SporeMP-M04-01/02, use separate real profiles/stages seeded from `local/m03-fixtures/0912-post-award01`. Fixture manifest hash `978e60923b9192dcc9ceb03a83b82cacfb3aa5e3258296a70dd891bd327f0b16`; copied files match. Generated credentials stay DPAPI protected and ignored. Exact preparation metadata is preserved without credentials in [worker-01-prepared.json](worker-01-prepared.json) and [worker-02-prepared.json](worker-02-prepared.json).
- Launcher 0.1.4: actual worker roster/selection, Start/Stop/status, preserved selection during refresh, crash and forced-stop text, no success claim for refused shutdown, and no automatic mutation retry. Worker controls stay in Settings. Version, embedded release notes and CHANGELOG align. Normal Play remains the existing-account/save flow; Play now permits only registered worker processes with matching actual PID/parent/SID/path and live supervisor identity. Host checks cover this route; native player/worker coexistence is NOT RUN.
- `test-worker-actions.py`: a bounded unattended A/B jump probe once an original Creature fixture is loaded. It checks wrong owner/stale epoch, original `DoJump` return and later matching landing. The tool is syntax checked but native invocation is NOT RUN; every report retains milestone acceptance NOT_VERIFIED.

## Executed commands and results

All recorded invocations have adjacent `*.command.json` with literal argv, UTC start/end, expected exit and observed exit, plus `*.log`. The runner preserves failures and refuses evidence overwrite. Native commands were separate from build/unit tests.

| Evidence prefix | Actual operation | Expected / observed |
|---|---|---|
| `build-03` through `build-06` | `cmake --build build/win32 --config Release --parallel 4` | 0 / 0. Final artifacts are build-06. |
| `launcher-build-03` | `pwsh -NoProfile -File tools/build/build-launcher.ps1 -Configuration Release` | 0 / 0; final embedded release notes, both Release builds, zero warnings/errors. |
| `host-03` | `ctest --test-dir build/win32 -C Release -R worker_host\|launcher_host --output-on-failure -V` (regex is one literal argument; no shell pipe) | Expected both pass; worker times out after 15.02s, launcher passes 30 assertions in 1.11s; CTest exits 1. |
| `worker-host-04` | `ctest --test-dir build/win32 -C Release -R worker_host --output-on-failure -V` | 0 / 0 after pipe cleanup fix; **173 assertions, 0.69s**. |
| `worker-python-03` | `python -m unittest discover -s tests/unit -p test_worker_manager.py -v` | 0 / 0; **9 tests**. |
| `launcher-python-02` | `python -m unittest discover -s tests/unit -p test_launcher*.py -v` | 0 / 0; **23 launcher tests**. |
| `prepare-worker01`, `prepare-worker02` | `pwsh -NoProfile -File tools/native/prepare-worker.ps1 -WorkerId 01` / `02` | 0 / 0; actual owned OS accounts and hash-checked fixtures, no game launch. |
| `peer-access-02` | `pwsh -NoProfile -File evidence/2026-09-12-m04-workers/probe-peers.ps1` | 0 / 0; both real standard-account tokens denied write/delete to personal/game/peer paths. |
| `tool-syntax-01` | `pwsh -NoProfile -File evidence/2026-09-12-m04-workers/check-tools.ps1` | 0 / 0; 5 PowerShell and 3 Python tools parse/compile as source. |
| `preview-build-02`, `preview-run-02` | Build actual WPF preview and run it against its restricted fixture service | 0 / 0; actual selection and Start/Stop bindings, zero binding errors, no Show()/production startup/desktop input. |
| `real-worker-list-02` | `python tools/launcher/launcher_service.py worker_list` | 0 / 0; final backend lists real prepared accounts 01/02 stopped, Start available. |
| `personal-before-01`, `personal-after-01` | Worker personal-file tool with exact preserved backup and fresh output paths | 0 / 0; **29 personal files unchanged**; no unknown/active SPORE process. |
| `candidate-validate-01` | `python tools/diagnostics/sporemp_diag.py validate --game-root C:\Games\SPORE` | 0 / 0; pinned executable/content equality, no gameplay qualification. |
| `provenance-03` | `python evidence/2026-09-12-m04-workers/record-evidence.py --generation 02` | 0 / 0; final source/artifact hashes and 84 original clauses preserved. |

Initial configuration and two early builds were visible tool output before the evidence runner was used: CMake configure succeeded; the first build hit C4244 wchar-to-char conversion under /WX, fixed by explicit ASCII generation conversion; the next build succeeded. They have no standalone command records and are not reported as final acceptance builds. The earlier `worker-host-01/02`, launcher build/test and six-test worker Python results remain tied to their earlier source, not the final 173/30/9 totals.

Other retained failures: `peer-access-01` is a PowerShell parser failure from `$workerId:` interpolation before any child was launched; corrected `${workerId}` executes in peer-access-02. An initial unittest runner invocation used ambiguous PowerShell `-p` passthrough and did not execute tests; explicit `-Arguments @(...)` resolved it. `provenance-01` decoded UTF-8 Git text using Windows locale defaults and falsely reported a changed Unicode clause after creating partial copies. Explicit UTF-8 decoding finds all 84 exact clauses; provenance-02 reuses only byte-identical partial copies/archive without overwriting them. No acceptance paragraph was changed to make the check pass.

The pipe timeout was a real host defect: cleanup called `GetOverlappedResult(TRUE)` on every OVERLAPPED slot, including operations that had never returned ERROR_IO_PENDING. Waiting only for genuinely outstanding operations preserves cancellation lifetime without waiting on an unused unsignaled event. The expanded fixture now verifies bounded teardown, two independent children, controller closure, one crash with the peer still controllable, actual same-profile process rejection, job-close kill and explicit forced-stop status.

The final backend integration allows ordinary Play only when all existing game processes are OS-verified registered workers. Tests reject changed PID, parent, SID, executable path, exited supervisor and unqueryable process inventory, and verify that normal player preparation still requires no personal backup/profile setup. The shared worker-personal verifier uses the same identity checks. `worker-python-03` passes 9 tests; `launcher-python-02` passes 23; `personal-after-02` runs the final verifier against the actual closed system and retains all 29 hashes. Native coexistence is still NOT RUN. The earlier source archive/provenance generation01 is retained; generation02 captures this final code and rebuilt embedded notes.

Offscreen images live under ignored `local/m04-launcher-preview-02`; hashes and [ui-checks.json](ui-checks.json) are retained. Actual states at 1280x820 and 1060x700 were inspected: the worker card and controls are readable, contained and correctly enabled. The ready-state counters are synthetic fixtures. Preview-01 captured part of the navigation fade; preview-02 disables that animation only for stable still inspection. This does not establish visible desktop or native-game acceptance.

## Native failure reproduction

Exact command (recorded in [private-desktop-01.command.json](private-desktop-01.command.json)):

```powershell
pwsh -NoProfile -File tools/native/invoke-isolated.ps1 -Action Launch -RunName m04-private-01 -ObservationMode Worker -DisplayMode Windowed -Resolution 1280x720
```

The original game starts once under the previously qualified developer SporeMP-M01 account, SID ending1007. Supervisor PID21628, game PID31940, generation `9eb713167b1840f789955e303b7d4a65`. A separate `WinSta0\SporeMP-M04-...` desktop is created and **never activated**. The guard validates 108 game/content entries and payload before injection; the process is resumed at21:49:33.611 UTC.

Expected: rendered original game initializes the bridge, connects IPC and produces app/native progress. Observed: a native Alert dialog says **"Could not start the renderer. Please ensure your display is set to 32 bit color. [1001]"**. Its text is retained in [renderer-observation.json](renderer-observation.json), transcribed from the read-only inspection output. No bridge/actor trace is created and progress remains zero. At21:51:33.598 UTC the supervisor reports initialization_timeout and terminates only its job. Game exit35; wrapper exit31 (recorded expected0/observed31). Personal after-check retains all29 hashes.

Missing capability: a usable rendered desktop for this isolated worker. The next experiment is one prepared worker on the current signed-in desktop when it is available. No visible launch has run in this increment. The generic dialog is not evidence that changing color depth will fix it. Headless/render suppression, minimized, focus loss, modal dismissal, locked/disconnected session, RDP, VM and service operation remain unqualified.

## Static binding findings and remaining acceptance

Copied [static audit metadata](static/m04-app-loop.json) retains exact Ghidra argv and source executable hash. Local decompiled exports are hashed but stay ignored. The original EAMain checks `multipleInstances` before its global instance mutex, and the parser accepts `-/` prefixes; the implemented worker option is source-derived. The Canvas tick message loop returns on WM_QUIT and EAMain proceeds into original shutdown, establishing the intended dispatch path only. Runtime shutdown/dialog behavior is NOT RUN.

The persistence manager getter/constructor/vtable and load slot `0xB271D0` are identified. The SDK expressly questions the parameter struct size, requires player initialization and declares a bool result not established by the decompilation. Original saved-game parameter construction, native completion, save confirmation behavior and identity restoration remain missing. Load/save commands therefore return unavailable. Full ABI/context notes and paths are in [docs/m04-workers.md](../../docs/m04-workers.md).

The native gate table remains [tests/engine/M04.md](../../tests/engine/M04.md), with machine-readable [acceptance.json](acceptance.json). Two original workers with observed save/config isolation, unattended native action outcomes, real player-client closure while native authority lives, crash survival of the other native worker, controlled checkpoint reload and native desktop/shutdown behavior are all **NOT RUN**. Two actual OS account access probes and two HOST fixture processes do not satisfy those native criteria.

Next smallest work: current-desktop native initialization and the prepared bounded IPC action probe, then verified original saved-game selection/load construction. The registered-worker coexistence route is implemented and host tested; its real player-close behavior is still part of that native session. Preserve the accepted M03 fixture and avoid another unchanged combat replay. M05–M19 remain gated; all five campaign stages, independent progression, persistent shared universe, native authority, reconnects and optional M20 scope are intact.
