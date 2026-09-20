# M02 first user-operated native probe — 2026-09-08

The user explicitly requested: launch the game and give instructions; the user will operate gameplay. This authorizes launching the M02 probe, not assistant mouse/keyboard control. No assistant desktop input was sent.

Pre-launch: the previous increment's 122 source/SDK/artifact hashes verified with exit 0. No SporeApp was running. The existing closed disposable profile's 17 files were copied into ignored `local/m02-fixtures/user-probe-01-before` and their copied hashes matched (`fixture-before.json`). This snapshot precedes initial gameplay fixture preparation; it is not an already qualified paired campaign baseline. Personal file backup/hash checks and disposable OS isolation were performed by the existing native launch wrapper; see `personal-backup.json`, `os-probe.json` and launch output. No player profile setup is imposed on normal Play.

Exact native command:

`pwsh -NoProfile -File tools/native/invoke-isolated.ps1 -Action Launch -RunName m02-user-observe-01 -Configuration Release -ObservationMode Observe`

Started through the hidden `launch.ps1` helper (driver PID 36148). Native host PID 30692; original game PID **35020**. Expected final exit: 0. Observed at handoff: game is running; final exit **PENDING**. `completion.json` is written by the helper after game exit and personal file verification. The helper copies only diagnostic JSON/JSONL/TXT, never saves/assets/credentials.

Actual startup evidence: bridge initialized and gameplay trace contains `trace_start`, **hooks_ready count=7**, stage observation and periodic AI counter summaries. Engine callback thread 34752. Target executable/SDK/bridge hashes match the recorded 0.0.2 build. See `launch-request.json`, `startup-observation.json`; live logs are under `C:\ProgramData\SporeMP\M01\runs\m02-user-observe-01` until the helper also copies them into `native/` after shutdown. The display name is not used as compatibility proof.

This establishes one original-game observation installation/startup, not gameplay action acceptance, full hook coverage, baseline equivalence or shutdown success. M02 remains **IN_PROGRESS**. All bindings still require the specific native event/behavior qualification in `tests/engine/M02.md`.

User sequence: enter Creature campaign, perform five separate jumps with complete landings, walk near NPCs for roughly 30 seconds, invoke `sporemp_trace` in the native console, then return to the main menu and quit normally. If Creature is unavailable in the disposable profile, resolve the fixture selection before asking for gameplay. After the user reports completion, inspect the actual entry/return/landing, lifetime, mode and failure counters plus normal exit; decide the next smallest missing test from that evidence. Additional paired-reference and damage/progression/save/load work remains.

## Closed trace analysis after the user's jump report

The user reported "just did some jumps btw". The closed native logs now establish nine accepted `DoJump` calls on avatar diagnostic ID 232, scene epoch 2, original Creature campaign mode 0x1654C01. All nine native bool returns are true, with argument 0. Each return is followed by one landing callback on that same avatar/epoch before the next jump (0.991841–1.068419 seconds later). There are ten total avatar landing callbacks: one precedes the first recorded jump. The 636 total landing callbacks include other creatures and must not be reported as 636 player jumps. This is native callback evidence plus the user's report; no gameplay video or paired behavior comparison was captured.

The trace has 7,812 records, 3,210 distinct diagnostic entities, 513 native destruction invalidations, 61,325 NPC AI entries, 2,453 avatar AI entries and 68 avatar state samples. These AI entry counts are not a tick-rate/capacity claim. Seven hooks installed. No hooks_failed, overflow, lost records or foreign-thread callbacks were recorded. A clean trace_stop was written. The scene message pair is menu exit -> Creature enter, not evidence of exiting/reloading Creature. No diagnostic_snapshot event was recorded, so the read-only console invocation remains NOT RUN.

Original game PID 35020 exited normally at 2026-09-08T16:22:19.122Z; the wrapper completed with exit 0. The bridge initialized/disposed once on thread 34752. Game/payload guards passed; loaded core/injector/bridge hashes and the original staged host match this run's launch request. Personal-after.json reports all 29 personal files unchanged. The current build's host was subsequently changed for launcher display options; analysis verifies the original ProgramData payload, not the later build output.

Executed from the repository root in PowerShell:

```powershell
python tools/native/analyze-gameplay-trace.py evidence/2026-09-08-m02-user-probe-01/native/gameplay-35020.jsonl --require-native --require-m02-coverage --output evidence/2026-09-08-m02-user-probe-01/gameplay-analysis.json
python evidence/2026-09-08-m02-user-probe-01/summarize-jumps.py
git diff --check
```

Expected: structurally valid actual native evidence with paired action returns, later same-avatar landing, native invalidation/scene messages, clean lifecycle and matched provenance. Observed: both analysis commands exit 0; the coverage gate has no gaps and all 12 cross-log/provenance checks pass. The summarizer records exact entry/return/landing sequence numbers and preserves exclusive output creation. No code was changed, rebuilt or native gameplay launched for this analysis. Commands for the earlier native launch and environment/artifact provenance remain above and in launch-request.json / ../2026-09-08-m02-observation/source-manifest.json and environment.json.

Trace SHA-256: 65e1ce3b2cdb89c3124bee94123ffc57d69cce87c915ee5b1bb8f33701fc755a. Detailed result: gameplay-analysis.json and jump-evidence.json, including raw-input and summarizer hashes. The analyzer reports m02_trace_coverage_ready=true but deliberately retains native_acceptance=NOT_VERIFIED. M02 stays IN_PROGRESS: a reference/observed pair from the same closed Creature fixture, visible behavior comparison, overhead measurement, explicit console invocation and Creature exit/reload remain outstanding. Native damage/death, inventory/progression and save/load bindings also remain unqualified.

No additional jumps are needed from this capture. Next smallest native experiment: record the closed Creature fixture, then run it without the observation hooks using the same user-operated jump sequence and compare native invariants; collect the pending console and Creature exit/reload in the observed side of that pair. Creating the fixture via `levels -unlock` was suggested for this fresh account; its actual console use was not recorded and cannot stand in for natural campaign progression acceptance.
