# M02 paired Creature baseline — 2026-09-08

**Historical result: IN_PROGRESS. Evidence: NATIVE trace checks and incomplete presentation evidence. Paired visual acceptance: NOT VERIFIED. Overhead: NOT MEASURED.** Both original games and recorders closed; the observed video was rejected after actual inspection.

The complete session, literal commands, original traces/media and source/artifact hashes remain in the [private historical archive](../../docs/public-evidence.md#historical-artifacts) at `evidence/2026-09-08-m02-pair-01/`. This condensation adds no test or acceptance.

## Results and preserved failures

The user supplied gameplay input at requested fullscreen 2560×1440. Reference and Observe used the same starting closed fixture, restored under the guarded disposable profile. The reference user accepted Save instead of declining; that deviation and a separate end backup were retained. Native load after closed-file restoration was observed, but exact save-choice/timing/RNG equivalence was not assumed.

Both game/wrapper exits were 0; matching loaded payloads and all 29 personal-file hashes were retained. Observe had 20,275 records, zero loss/foreign callbacks and all 14 pair correlation checks passing. Five pre-reload jumps and one post-reload jump had accepted native entry/return and one later same-avatar landing each, 0.988–0.998 seconds later. Reload changed diagnostic avatar ID 1513/epoch 3 to 8424/epoch 6. Two read-only console invocations and actual scene invalidation were traced; console-rendered text was not visually qualified.

The reference video showed gameplay and reload. The observed GDI recording had a valid container and recorder exit 0 but initially captured non-game pixels and then 163.5 seconds of black frames. It was unusable and remained private. Earlier minimized-window attempts produced zero frames or invalid dimensions. A working reference video could not replace missing Observe footage.

Installed PresentMon attempts exited 1 without CSV, so no timing result was claimed. A bounded exact-game Windows Graphics Capture helper was prepared and host checked. Its missing-game guard rejected correctly; real successful game recording with it remained IMPLEMENTED_NOT_RUN in this session.

## Checks and remaining step

The isolated launch used `tools/native/invoke-isolated.ps1 -Action Launch -ObservationMode Off|Observe -DisplayMode Fullscreen -Resolution 2560x1440` with distinct run names. The strict gameplay analyzer and pair analyzer exited 0 while retaining native acceptance NOT_VERIFIED. PowerShell parsing and wrapper argument checks passed. The backup restoration verified owned paths, closed processes, hashes and ACLs before moving only disposable roots; no personal save was restored.

Configuration remained the pinned original Creature fixture, SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, GOG GA 3.1.0.29 executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, Windows 11 build 26200, Ryzen 7 9700X/RTX 4080 SUPER. Concurrent application load was uncontrolled.

Next was a short inspected WGC clip before further actions, followed by missing paired presentation coverage and separately verified native timing. M02 completion is recorded in the [later accepted session](../2026-09-09-m02-completion/SESSION.md).
