# M03 native context investigation — 2026-09-12

**Result: M03 BLOCKED.** Bridge 0.0.6 supplies five additional guarded native observers and attack-transition correlation. Three original-game runs locate NPC attack cancellation but do not solve reciprocal B combat or independent player progression. The user questioned repeated testing and requested reverse engineering. Work then moved to static analysis of the installed executable; no further game run followed that change of approach.

The native game is closed. Full M00–M20 requirements and the accepted M01/M02 boundaries remain intact. No multiplayer completion, independent B campaign, or replacement gameplay implementation is claimed.

## Changes

- `src/bridge/native_actor_abi.h` supplies exact Win32 signatures shared by the observers and executable ABI fixture.
- `src/bridge/native_actors.cpp` observes native ability selection, Animal effect dispatch, Animal damage, attack-index assignment and attack-animation completion. Native arguments/returns and one original call per hook remain unchanged. It correlates attack state around original NPC AI and preserves the actual caller RVAs. Version advances through 0.0.4/0.0.5 to 0.0.6; the final harness has fourteen hooks, six prefix guards and three virtual-slot guards.
- `tests/unit/native_actor_abi_tests.cpp` runs ten HOST assertions with actual Win32/Detours attach, nesting, arguments, returns and detach. No game loads in this executable.
- `tools/native/analyze-actor-context.py` and seven Python fixtures validate evidence structure and correlate native calls. The analyzer does not grant native acceptance on its own.
- `tools/build/fetch-re-tools.ps1`, `tools/native/export-sdk-symbols.py`, `tools/native/invoke-static-audit.ps1` and two Ghidra scripts create a repeatable static-analysis workflow. Tools and private game-derived outputs stay under ignored `external/` and `local/`.

These are developer diagnostics and research tools. The launcher remains 0.1.3; this increment adds no player-facing launcher flow and therefore no new launcher release-note/version change. Existing unrelated workspace edits are preserved.

## Provenance and environment

HEAD at provenance capture: `d106404da0b7c4531f63f98d0df2377bb897a0f7`; workspace was already dirty and includes earlier uncommitted milestones. This session does not claim a clean commit or source export of all earlier changes.

Native executable: `C:\Games\SPORE\SporebinEP1\SporeApp.exe`, GOG GA 3.1.0.29, PE32, 24,895,536 bytes, preferred base `0x400000`, SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. These runs exercise original Creature campaign gameplay on that executable. Other executable/content builds, base-game executable qualification and adventure gameplay are not implied. The guarded host checks the recorded 108-file content/configuration inventory.

SDK: `cbf9206b9a823f0911cd9be0217104a49d72380b`. Injector source: `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. [Provenance 0.0.5](provenance-0.0.5.json) records SDK/injector HEAD, sources, artifacts and hardware. [Provenance 0.0.6](provenance-0.0.6.json) snapshots the exact final runtime sources before probe-03 and hashes the release artifacts. Version 0.0.4 has exact loaded-artifact hashes but no complete pre-run source snapshot; do not reconstruct that claim from current files.

Host: Windows 11 Pro 10.0.26200; Ryzen 7 9700X, 8 cores / 16 threads; RTX 4080 SUPER driver 32.0.15.9649. The captured inventory also lists Parsec Virtual Display Adapter 0.45.0.0 and AMD Radeon Graphics 32.0.21042.62. Native desktop mode requested: fullscreen 2560×1440.

Each game uses the previously measured disposable developer account `SporeMP-M01`, SID `S-1-5-21-1000000000-2000000000-3000000000-1007`, with one native process at a time. Closed 27-file fixture snapshots and 29-file personal backups/postchecks are preserved. All three personal postchecks match; all disposable Games and creation contents match the accepted September 9 start. Cache/event changes are not presented as save changes. The launcher’s normal Play path continues to use the user's normal account and saves.

## Commands and executable checks

[commands.json](commands.json) indexes the exact argv arrays, start/end timestamps, expected/observed process exit codes and original JSON record paths. Paired `.log` files retain command output. Native run argv appears separately in each `probe-0N/completion.json`; concrete console inputs and UI actions are in `input-actions.jsonl` and the native actor traces. A process exit code does not override semantic failures listed below.

Final build:

```powershell
cmake --build build/win32 --config Release --parallel 4
ctest --test-dir build/win32 -C Release --output-on-failure
```

Expected both exit 0. Observed `build-06.json` exit 0 and `ctest-04.json` exit 0, all six HOST/FIXTURE targets passed: actor authority, native ABI fixture, diagnostics, observation, Python tooling and launcher host. Counts include ten ABI assertions and 67 Python tests. These checks finish in seconds and do not launch SPORE. No full host-suite rerun followed the documentation/static-analysis-only work.

Guarded native launch, repeated with distinct run names `m03-0912-probe-01`, `-02`, `-03`:

```powershell
pwsh -NoProfile -File tools/native/invoke-isolated.ps1 -Action Launch -RunName m03-0912-probe-03 -Configuration Release -ObservationMode Actors -DisplayMode Fullscreen -Resolution 2560x1440
```

The session wrapper `launch.ps1` records the actual invocation and copies native reports after completion. These are explicit developer probes, never side effects of the build/unit commands. Every run ends through the game UI without saving, with game/wrapper exit 0, matching loaded payloads, one initialization/disposal on the recorded engine thread and zero foreign callbacks.

## Native observations

| Run | PID / bridge / records | Actual result |
|---|---|---|
| probe-01 | 7216 / 0.0.4 / 18,984 | A/NPC reciprocal damage and an allied-assisted campaign award; B kills a second NPC with six native one-point hits, no DNA call. The NPC's five accepted selections against B produce no effect dispatch. |
| probe-02 | 23736 / 0.0.5 / 9,351 | B completes another six-hit NPC kill, no DNA call. Seventeen NPC ability attempts include ten accepted selections and seven rejected selections, with no NPC effect dispatch or B health decrease. |
| probe-03 | 31704 / 0.0.6 / 45,313 | B lands one hit on NPC3. Thirteen NPC attempts include seven accepted selections; each accepted attack is cleared at the next original AI return after 15.4362–16.6991 ms, with no hit on B. Later B lands twelve native hits on A; A's health returns between hits. The held intention expires after 30.0866515 s. |

The probe-01 DNA event is an assisted global reward of 8.75. Its final damaging attacker is an untracked ally (actor 0), following A involvement. It is not an A final-hit award or independent B progression. Probe-03's later A death is starvation, supported by native hunger/health and the displayed starvation message; it is not a B combat kill. No accepted Stop command appears for the attempted late Stop in that run. The Stop attempts in the earlier runs also follow cancellation/death. **Active Stop and active-intention scene-exit acceptance remain NOT RUN.**

The seven next-tick clears are correlated by exact sequences/callers in [native-summary.json](native-summary.json). This file also carries the per-run loaded payload hashes and healthy lifecycle checks. The final trace SHA is `0c10752b85a86ae53df56f3132cebf70088ab890c11c3a91b95d07d3fdb27068`; final bridge SHA is `ca86979931c155beff3bb38a7ce0214391bad87392947bb74651e78e83a7f361`. Probes-01/02 trace SHAs are `0ba37196871e708d6e68a9daca43af5fbeb837b97904becd9444de219eea22d5` and `cca8fc397b8bfae3f51c17a187b79d4a56f55c3b9d9203eadd364c9ec24f994f`.

## Capture review

The 300/300/180-second WGC clips are under `local/m03-captures/0912-probe-0N/capture.mkv`, excluded from Git. All finalized normally. Request/completion/stderr records are copied under each probe's `capture-02/`. `capture-extraction.json` records exact FFmpeg/ffprobe commands and source/sheet hashes. All three nine-frame contact sheets were actually viewed. [visual-review.json](visual-review.json) records the limited conclusions from those views; the extraction report's original PENDING marker is preserved as an extraction-stage state.

Probe-01 contains actual game and console footage but the B encounter is often at the right edge/offscreen, so it does not independently identify every hit. Probe-02 shows B/NPC interaction after the native camera zoom-out and includes the Don't Save confirmation near 294 seconds. Probe-03 shows the loaded fixture and animated B/NPC encounter; its 180-second recording ends before intention expiration, starvation, the late Stop attempt and final quit. Those later events rely on native logs and live UI observation, not that recording. Capture frame rate is not a measurement of native game timing.

## Static analysis after the user's steering

The portable Ghidra archive SHA is `93a5d11a9ad510622acaaf908c556a7b9b764d338e78a7567f3689bf5081fd54`; Temurin archive SHA is `f9d6e191ab098c0d416e7d588a24420a8621cd2f4720dab2459b8b7b2d2d8b4e`. Their versioned official release URLs are pinned in `tools/build/fetch-re-tools.ps1`. The wrapper temporarily sets Java/PATH/heap for its own process and restores them; it does not install services, open a Ghidra window, inject code or execute the game.

The headless import saved a partially analyzed database after its explicit 300-second analysis limit. Its first Java export failed; the follow-up script fixed that and produced the selected decompilations. Exports `named`, `decision`, `eligibility`, `combat-stop`, `factory`, `initialization` and `constructors` complete successfully. Private outputs are under `local/m03-static/`, with exact command/version/script hashes in the corresponding run JSONs. They are analysis artifacts, not accepted SDK bindings.

[Focused RE findings](../../docs/m03-reverse-engineering.md) contain the durable conclusions:

- The native behavior table connects the observed reset routine `0xD672A0` to behavior `0x02D852E6`, activated at `0xD67980` and ticked at `0xD67CD0`.
- Attack-stop predicate `0xD65980` checks target existence/enabled/death, political affiliation, relationship/fear and native order, stealth, animation/context and perception/memory. A deactivation callback alone does not reveal which predicate or decider changed.
- Disassembly corrects misleading decompiler receiver inference: `0x8E8230` reads target combatant `+0x34` (SDK death-state lead), not global game mode; `0xC0C290` tests target stealth at the stop callsite.
- The animal constructor reaches the native spatial constructor, which sets spatial enabled true at `0xC898F1`. This is distinct from the disabled herd observed in the harness. No speculative flag/personality change was made.
- Native DNA updates shared balance, cPlayer goal progress, display and action state, including a deferred strategy flag. Independent B progression requires native context ownership beyond a second float.

## Preserved failures and coverage limits

- `build-02`: compilation failed on undeclared `_countof`; corrected to `std::size`, later builds pass.
- Initial fixture wrapper reused an output name, causing report collision. Its command report is preserved as `fixture-start-command.json`; a fresh, verified pre-probe-01 snapshot was used. `run-command.ps1` now refuses collisions.
- Initial pre-probe-02 snapshot invocation gave Python an invalid combined CLI argument (exit 2); the separate-argument retry succeeds. The incomplete provenance-0.0.5 attempt is also preserved alongside the corrected record.
- `native-summary-call`: analysis failed on an incorrect fixture-report key (exit 1); corrected retry succeeds.
- `capture-extraction-call`: FFmpeg font lookup failed (exit 1); explicit Windows Arial font path fixes extraction, retry succeeds. Extraction success alone is not visual acceptance.
- `static-import-call`: headless process returned 0, automatic analysis timed out as configured, and the Java export script failed to compile because its assumed API method was absent. Original failed script/wrapper are preserved locally. The database was saved; the initial function export **failed**. Corrected follow-up exports succeed, and the wrapper now checks semantic export results.
- A documentation patch failed to match a full STATUS paragraph and applied no changes; the exact paragraph was read and the patch reapplied successfully.

No repeated native run is justified solely by these tooling failures. Already captured gameplay remains valid within its limits. `acceptance.json` retains explicit unqualified gates: B reciprocal hits, independent reward/progression, nonzero resources, active Stop/scene cancellation, server-only avatar/area effects, other campaign stages and multi-process/network behavior.

## Next smallest step

Continue static interpretation of behavior decider/order and native player/goal/action ownership. Before another native run, establish the smallest safe observation ABI that distinguishes attack-stop predicate success, another tick failure, and decider replacement at the first accepted NPC attack. A bounded first-transition capture can answer that question; another repeated kill/reload session cannot. Then implement and validate the resulting cause-specific adapter. Do not mark M03 complete from these diagnostics or force an outcome by replacing native AI/damage/rewards.
