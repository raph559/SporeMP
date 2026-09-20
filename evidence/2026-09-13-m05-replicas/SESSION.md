# M05 — first native authority/replica increment

Date: **2026-09-13**. User request: “lets go with M05 now”. Result: **M05 IN_PROGRESS**. Full milestone acceptance is **NOT VERIFIED**; no multiplayer Play flow or network session is implemented. Original M00–M20 scope and acceptance clauses remain required. The initial working tree was already extensively modified by preceding work; [initial-working-tree.txt](initial-working-tree.txt) is retained. This session does not commit, revert or replace that prior work.

The final UTF-8 comparison retains **all 21 milestone sections and all 82 original dependency/work/deliverable/acceptance lines exactly**: [acceptance-clause-check.json](acceptance-clause-check.json). An earlier read-only comparison omitted Git subprocess UTF-8 decoding and reported mojibake mismatches; the corrected comparison passes and no file corruption was present. Git diff whitespace checking also passes; its CRLF normalization notices are not failed checks.

## Concrete outcome

The final bridge **0.0.17** replica, original game PID **3624**, adopted saved A/B without creating nouns and applied one captured original authority sample to A. Health changed **7.62724638 → 10**, energy **1000**, hunger **25.6914024**, DNA **0 → 0**. Native effective maximum health was **10**; the raw maximum-health field was **1**. One native application appears at sequence **2779**, on engine thread **22644**. The duplicate was refused, as were another update and an attempt to publish state after disconnect.

Eight connected projection audits match. There are **344** later A samples with unchanged projected vitals, the last **86.4512188 seconds** after application. The final armed replica counts **147,808** denied AI callbacks and **147,808** denied hunger/healing callbacks. These count attempted callbacks, not unique NPC decisions or frames. The disconnected client remains a replica. Unsupported paths were not silently presented as suppressed: death, pickup, spawn, other timers and full progression remain open.

Actual receiver frames show the original Creature scene and **10 HP HUD**, including the 45-second and 95-second frames. No keyboard/mouse input was used. Audio, camera control, movement/interpolation and native frame-time comparisons were **NOT RUN**. The authority capture has a retained splash image behind a changing HUD and is rejected as 3D gameplay presentation; native source provenance is independently retained. This state sample has no claimed reward/combat cause and zero DNA, so **nonzero reward replay is NOT RUN**.

Primary evidence: [native-summary.json](native-summary.json), [replay-state-03.json](replay-state-03.json), [source-state-02.json](source-state-02.json), [visual-review.json](visual-review.json). Runtime subset and missing bindings: [M05 design](../../docs/m05-replicas.md). Full remaining checks: [native acceptance protocol](../../tests/engine/M05.md).

## Environment, source and payload identity

Windows 11 Pro **10.0.26200**, Ryzen 7 **9700X** (8 cores / 16 logical), RTX **4080 SUPER**, NVIDIA driver **32.0.15.9649**. The operator was elevated. Prepared isolated accounts **SporeMP-M04-01 / 02** were enabled and valid until September 26; runtime access probes and native resource paths are in each host/bridge trace. Accounts have distinct SIDs ending **1008 / 1009**. All runs use the current signed-in rendered desktop, unminimized **1280×720** original game windows. There is no new headless, private-desktop, locked-session or full ETW isolation qualification. [environment.json](environment.json) records the actual preflight, including zero starting games.

- Original installed GOG GA **3.1.0.29 Win32** executable: `C:/Games/SPORE/SporebinEP1/SporeApp.exe`; SHA-256 **dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37**. Native host validates the pinned **108-file** executable/content configuration before creation. Inventory identity does not extend qualification to other configurations.
- SDK: **cbf9206b9a823f0911cd9be0217104a49d72380b**. Injector source: **26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9**. VS2022 v143 **14.44.35207**, Windows SDK **10.0.26100.0**, Win32 C++17 Release `/MD`.
- Final .17 bridge SHA-256: **a11ed03589d2a47cbfc279edd3ece153d7b802136093a662f3dd0b699597be2d**.
- Final .17 NativeHost SHA-256: **08fade1b96b9869e5cdda2b11ed6891b947739d023810fd0d81ab4f2ccf470b2**.
- WorkerControl SHA-256: **69d826aaeedd313d30d8b229bbd49a652b18e075d1233e752352badf416f7c35**.
- SDK DLL SHA-256: **c018b51c56c537e739f24319dcfeb5cfeeefa97ded0ca91bf5729755c190ba11**. Injector DLL SHA-256: **938cf3b106a459174ebfe31803b4d84238b07b004dc5c6e2da5fec61672eab55**.

Full source/artifact hashes before each native version: [pre-native-provenance.json](pre-native-provenance.json) (.15), [pre-native-provenance-016.json](pre-native-provenance-016.json), [pre-native-provenance-017.json](pre-native-provenance-017.json). Source copies under ignored `local/m05-replicas/source-0.0.15`, `.16`, `.17` retain CMake, source, tests, tooling and configuration. The final analysis rehashes all .17 recorded source/artifact paths against the live tree and compares actual loaded bridge/SDK modules in each run against its own freeze. Subsequent documentation/evidence additions are separate from those native-tested source hashes. Raw binaries, save files and game media remain ignored.

## Implementation and lifecycle

`replica_policy` implements immutable roles, a bounded value/ID registry, source/scene/entity/incarnation/baseline fencing, monotonic samples, tombstones, no nested application, and quarantine on callback failure/invalidation. HOST tests exercise stale sources, destruction/reuse, capacity, malformed values and reentrant callbacks. It contains no game formulas or replacement simulation.

`native_replica` provides explicit isolated authority/replica modes, engine-thread guards, absolute living-A vitals sampling/application and per-domain mutation audits. Existing original AI/target/selector/strike/damage/energy/DNA paths receive role checks. A selector returns its original `-1` failure sentinel; the enclosing native PlayAbility retains its own return/output behavior. New narrowly checked detours cover Save, the hunger/healing virtual branch and the pure DNA setter. Max HP uses the inspected native Combatant query. All engine types stay in `src/bridge`; the existing fixed 128-byte private control protocol carries only IDs and scalar values via appended opcode 14.

Before arming, a disposable replica may load/adopt its existing sealed fixture while saving/publication remain denied. The first admitted baseline latches denial permanently for that process's live policy. Disconnect does not promote the role or reopen bootstrap. After arming the standard worker mutation commands are unavailable; status/shutdown still work. Mapping invalidation occurs before native destruction, and scene exit clears the baseline. The current adapter creates no replica nouns and does not implement death, movement, spawn/despawn or full progression.

Installation and removal run in pinned SDK post-init/dispose callbacks outside DllMain. Correct order is observation → actors → M04 worker/persistence → M05 replica. M04 must inspect original Save bytes before the M05 detour patches them. Native traces show one engine-thread initialization/disposal and healthy detach in each process. The M01 lifecycle envelope's historical `gameplay_hooks:0` field is emitted before opt-in harness installation; the actor trace's ready/stop records identify the actual installed experiment.

Launcher remains **0.1.8** and its player-visible release notes were not changed. Experimental modes are developer-only; normal Play clears the role environment. That code path is not a new native normal-Play regression run. M04's accepted .14 payload remains the evidence for its historical qualification.

## Commands and observed outcomes

[command-index.json](command-index.json) lists **47 exact argv arrays**, start/end UTC times and expected/observed program exits, from the evidence wrapper. Each named action also has its own `*.command.json` and nonempty output log. The wrapper is invoked from PowerShell with explicit `-Arguments @(...)`; an early attempt to pass CMake `-S` directly through the wrapper failed PowerShell parameter binding before CMake ran. The corrected configure succeeds. Several read-only path lookups returned not-found and were resolved with `rg --files`; none launched or changed a game.

| Evidence class | Named commands | Expected versus observed |
|---|---|---|
| STATIC, not native execution | `static-updates-01`, `static-projection-01`, `static-guards-01`, `static-timers-01`, `static-vital-timers-01`, `static-health-cap-01` | All expected/observed exit 0. Ghidra 12.1.3 reuses the pinned database. Actual executable/tool/script identities, output locations and export hashes are in `static/` and the command index. |
| BUILD | `configure-01`, `build-01` through `build-05` | All exit 0. Build05 is the .17 native host/bridge integration with zero warnings/errors. Build commands do not launch SPORE. |
| HOST/FIXTURE | `focused-tests-01`, `replica-tests-02`, `focused-tests-03`, `focused-tests-final` | All exit 0. Final four targets pass in 1.41s: replica 313+9 ABI, worker 185, actor 15, existing ABI 34. The ABI fixtures perform real Win32 Detours in test executables, not the game. |
| HOST Python tooling | `python-tests-01`, `python-probe-tests-final` | Full 103 tests pass; affected 5 pass again for .17. They validate bounded values, exact native trace prefix/sample linkage, tamper/identity/thread/clock rejection and existing tooling. |
| Personal protection | `personal-before-01`, every start's precheck, `personal-after-01` | All checks pass. All **29** personal files equal the verified closed backup before and after native work. |
| NATIVE explicit starts | `start-authority-01/02`, `start-replica-01/02/03` | All launch helpers exit 0; readiness is established separately by actual native trace events, not the helper's exit. |
| NATIVE initial failed load | `load-authority-01` | Expected admission, observed `unavailable`, program exit **5**. Original Save binding prefix was already detoured; preserved failure. |
| NATIVE corrected loads/adoption | `load-authority-02`, `load-replica-02/03`, `adopt-authority-02`, `adopt-replica-02/03` | All commands exit 0. Native load completion and exact A/B adoption each follow; `created_nouns:0`. Sidecars validated against current original Games trees before adoption. |
| NATIVE source sample | `capture-state-02` | Exit 0; one original .16 authority sample is retained with its native prefix/hash. |
| NATIVE failed replay | `replay-state-02` | Expected application, observed rejected raw-health precondition; helper exit **1**. Begin/bind accepted, apply invalid, no native vitals write, baseline quarantined. |
| NATIVE successful replay | `replay-state-03` | Exit 0. Request 5 applies once; request 6 duplicate `stale`; request 8 disconnects; request 9 update `invalid`; request 10 publication `unavailable`. Expected rejection commands inside the helper exit 5 and are preserved in its JSON. |
| Capture / extraction | `capture-replica-check-01`, `capture-authority-02`, `capture-replica-02/03`, `preview-replica-03`, `review-capture-frames` | Owned exact-HWND recorders exit 0 without wall timeout. Actual pixels are reviewed separately; capture success is not visual or native timing acceptance. |
| NATIVE shutdown | `stop-authority-01/02`, `stop-replica-01/02/03` | All shutdown commands accepted, then actual game and supervisor exit **0**. No forced game termination in this session. |
| ANALYSIS of closed native records | `analyze-native-01` | Exit 0: five healthy closed runs, source/native-prefix/payload matches, personal hashes and sealed Games trees unchanged. Full M05 acceptance explicitly remains NOT_VERIFIED. |

The two nonzero program outcomes are retained, not edited to look successful. The outer PowerShell tool may report exit 1 when its child control program returns 5; the exact child exit/reply is in the command JSON and log.

## Native process ledger and failure reproductions

| Run | Generation / game PID / engine thread | Original trace | Observed result |
|---|---|---|---|
| .15 authority / account 01 | `c04dff947ef848309517375821cbaa0e` / 21604 / 8684 | `native-initial-01`, 24 records | Role hooks installed, persistence binding rejected; menu only; clean exit. |
| .15 replica / account 02 | `d8ef94e3451a4ad5b449d9a27797a404` / 26088 / 6548 | `native-initial-02`, 24 records | Same binding-order issue; menu only; clean exit. |
| .16 authority / account 01 | `800a365ab6a44962b8330cc064aadcfb` / 19144 / 30188 | `native-pair-01`, 15,613 records | Original load and saved A/B adoption; sample captured; original AI/hunger calls continue; clean exit. |
| .16 replica / account 02 | `fbf7f804945c415aa170177bc692dedb` / 28384 / 15320 | `native-pair-02`, 15,520 records | Original load/adoption; wrong raw max-HP assumption rejects apply before writes; guarded quarantine; clean exit. |
| .17 replica / account 02 | `b685c0e3604b4add805f969988529cac` / 3624 / 22644 | `native-replica-03`, 9,090 records | One successful application, duplicate/disconnect/publication denials, stable observed vitals and inspected scene/HUD; clean exit. |

All five traces have contiguous sequences, monotonic QPC at 10 MHz, one recorded engine thread, zero foreign callbacks and healthy detach. Bridge initialization/disposal records match that thread. Each actual loaded bridge/SDK hash matches its own pre-native version freeze; every game hash matches the pinned original executable.

**Binding order reproduction:** install M05 Save detour before `initialize_native_worker`, then request the original load. M04 sees patched bytes at RVA `0x729600`, records `persistence_binding_rejected`, and returns unavailable. Expected: M04 verifies the original function before M05 attaches the known guard. Correction .16 changes only that initialization order for this failure.

**Max HP reproduction:** load/adopt the sealed Creature scene in the .16 replica, begin/bind, and apply the original authority's 10 HP. The raw max field equals 1, so the precondition rejects. Expected: validate against native effective max HP, not an uncertain raw SDK field. Static audit and .17 runtime confirm Combatant virtual `+0x58` at `C05D50` returns 10. The final run reuses the original source sample; no authority combat/reward was repeated or fabricated.

The source prefix has **2,095,729 bytes**, SHA-256 **14008ef0a08c5443ee97a77dea87006278676c7d6bd048fca0128547d97cd100**. Its source generation, scene 3, entity 1/incarnation 1/sample 1 and scalar bit words are verified against the actual original trace. Original source storage remains under its closed ProgramData run, with a diagnostic copy archived here. The source and receiver were different original processes/profiles. The final replay was an artifact transfer over local private IPC, not a live network session.

## Saves, capture and shutdown closure

The three loaded runs adopted exact checkpoint nouns A **232** / herd **208**, B **7425** / herd **7424** for account 01 and B **8519** / herd **8518** for account 02, preserving the checkpoint species fingerprints. Each native restore reports `created_nouns:0`. Both closed Games trees still match the sealed M04 concurrent checkpoints: worker 01 **dd23c69a1a35ad1c9a06f4d103014511f49f547fb17ce303dc405ffcd85d2940**, worker 02 **f361377b1bc4fa4a41b3ab621e0fdea85ce787c48cea375afdfa563b2925a332**. These are Games-tree digests, not claims that every disposable cache file is unchanged. Personal before/after reports match the 29-file backup `local/backups/automatic-5bc48ae4a6b147fe63b32945423523e7`.

Raw video is ignored under `local/m05-replicas/capture-*`, with recorder request/completion metadata, exact selected game PID, HWND, executable, duration and recorder hash. The quick 5-second check was visually inspected as the real galaxy menu. The later recordings are 180s authority, 180s .16 replica and 120s .17 replica. [Capture extraction](capture-extraction.json) preserves exact FFmpeg argv and frame hashes; [visual review](visual-review.json) lists ten inspected frames and rejects the unsuitable authority 3D view. No audio was captured. No compositor cadence is claimed as native frame timing.

All owned recorders completed and all games/supervisors are stopped. The final replica shutdown request **11** was accepted at **12:57:26.240 UTC**; actual game and supervisor exit **0** was observed at **12:57:26.747 UTC**. The M04 authority AI progress heuristic may label an armed replica `simulation_stalled` even while its app updates and rendering continue; that status is not a qualified replica-health model.

## Remaining gate and next smallest step

Full M05 is not accepted from a scalar projection. Known native paths for death, pickups, spawning, inventories, other timers and full progression need a proven separation from presentation. `C0AE30` still runs mixed native update work outside the guarded AI/hunger functions. A deliberate conflicting NPC action and a real nonzero worker reward must be traced and applied once without a local repeated outcome. Native save/autosave denial, replica invalidation/reuse, camera/movement/animation/audio fidelity and the disabled-mode observational baseline remain NOT RUN.

Next: identify and observe the terminal native pickup grant/consume path with exact ABI, caller, object lifetime and actual inventory/DNA deltas, then use a real worker outcome for the smallest client conflict/replay test. Do not manufacture a grant, fake a source trace, disable the entire mixed Update function, or count zeros from unhooked audit categories as native suppression. M06–M19 remain blocked on unfinished dependencies; the complete five-stage/global-universe scope remains intact.
