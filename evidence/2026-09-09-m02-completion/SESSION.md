# M02 completion investigation — 2026-09-09

Final status: **M02 VERIFIED for the recorded original Creature baseline**, under the unchanged milestone acceptance. The user explicitly authorized assistant computer manipulation and requested completion of M02. Earlier evidence is preserved. This session used the owned disposable developer account; normal Play remains unchanged. Raw game media and fixture saves stay in ignored local storage.

Executed plan: qualify a short Windows Graphics Capture clip before further gameplay; establish actual native presentation timing; compare the same closed Creature fixture with observation Off and Observe; review original action outcomes, native lifetime/scene events, read-only console output, and overhead; retain explicit unknown bindings and the complete M00–M20 plan.

The existing seven-hook Release bridge was retained unchanged. No new game binding or gameplay mutation was needed. Acceptance follows the actual input/video, native trace and source/lifecycle audit; the standalone analyzer continues to report NOT_VERIFIED because it cannot perform external visual review.

## Evidence and acceptance

- [acceptance.json](acceptance.json): reviewed milestone decision and evidence hashes.
- [paired-native-analysis.json](paired-native-analysis.json): 21 passing source/artifact/fixture/PID/lifecycle/action/console/timing checks, exact trace sequences and measured values.
- [visual-review.json](visual-review.json): actual decoded game-image review, six jump sequences, console text and limits. Raw media is local/ignored.
- [media-analysis.json](media-analysis.json): exact capture/probe/extraction commands, final clip hashes, source/recorder metadata, black scans and exit codes.
- [provenance.json](provenance.json): 24 source/tool hashes, six artifact hashes, SDK/loader commits, actual OS/hardware/process configuration. This was recorded during the reference run and rechecked unchanged after both runs. Final added-file hashes are in [integrity.json](integrity.json).
- [input-actions.jsonl](input-actions.jsonl): assistant input timestamps, exact keys/clicks/text, run and window identities, before/completed outcomes. Every action used a current Computer Use game-window observation; no native gameplay was driven by shell input.
- `reference-01/` and `observed-01/`: guarded native logs, lifecycle logs, personal-file guards, timing CSVs and exact wrapper completion records. Observe also contains the raw trace and strict analyzer report.

Each condition performed three normal Space jumps: two before scene exit and one after reload. Both used ordinary click-to-move once, discarded unsaved movement at both save prompts, reloaded the same save and quit normally. The reference installs no M02 hooks. Observe additionally called `sporemp_trace` before and after reload. Timing measurement preceded gameplay inputs in both conditions. Optional online registration was dismissed; no authentication was attempted.

All six three-second jump contact sheets were reviewed at 10 samples per second. Each shows one rise, airborne interval, landing/crouch and return to idle; no duplicated visible effect follows the single input. Native movement and nearby NPC activity continue normally. Health remains 10 and displayed DNA 0 for the tested sequence. This does not exercise or qualify reward, spending or combat behavior. Uncontrolled native NPC/idle animation differences are retained rather than called deterministic equivalence.

The observed trace has 28,614 records / 20,098,694 bytes over 747.272459 seconds, including menu time. It has zero lost records, foreign-thread callbacks or hook failures. Three original avatar DoJump calls return true with argument 0; each has exactly one later same-avatar landing before the next jump or invalidation:

| Input | Avatar token / epoch | Entry / return / landing sequence | Return to landing |
|---|---|---|---|
| First jump | 1104 / 3 | 5483 / 5484 / 5487 | 0.9931473 s |
| Second jump | 1104 / 3 | 5823 / 5824 / 5846 | 0.9948261 s |
| Jump after reload | 10467 / 6 | 26222 / 26223 / 26288 | 0.9910400 s |

Native ID 232 is reused after reload, but the diagnostic token/epoch changes. There are 8,266 real destroy-entry invalidations, 2,179 factory observations, five scene exit/enter pairs, 499,571 NPC AI entries and 17,485 avatar AI entries. These totals do not establish complete factory/destruction path coverage or a universal AI tick rate. The read-only console actually prints `6687 records, scene epoch 3, trace healthy` and later `27461 records, scene epoch 6, trace healthy`. Those outputs match the native trace and were reviewed in the saved video. The command's fixed qualification-pending text avoids auto-accepting an individual probe before this external review.

Both games and wrappers exited 0 with exactly one same-thread bridge initialization/disposal and matching loaded module hashes. Reference game PID/thread: **29324 / 13128**; observed **32344 / 14396**. Host PIDs: 17200 / 33292. Guard validation covered the pinned executable and 108-file game/content inventory before injection. The normal original campaign runs inside the GA executable; this fixture is Creature campaign, not an adventure.

## Native timing and overhead sample

| Metric | Observation Off | Observation enabled |
|---|---:|---:|
| Measured process window | 60.0570413 s | 60.0591188 s |
| Actual D3D9 presentation records | 3,620 | 3,625 |
| Mean interval | 16.5637054 ms | 16.5384725 ms |
| Median interval | 16.55235 ms | 16.54640 ms |
| p95 interval | 17.6031 ms | 17.6180 ms |
| p99 interval | 17.9772 ms | 18.0072 ms |
| Maximum interval | 18.6716 ms | 18.8187 ms |
| Intervals > 33.333 ms | 0 | 0 |
| Whole-game process CPU time | 18.578125 s | 19.296875 s |
| Effective CPU cores | 0.3093413 | 0.3212980 |

Percentiles use nearest ordered sample index `round((n-1)*p)`; median uses the standard median. Each CSV contains only the intended game PID, D3D9 runtime and one swapchain. The stationary near-60-FPS pair shows no presentation regression material to this fixture: median delta -0.00595 ms, p99 delta +0.0300 ms. Whole-process CPU differs by **+0.0119567 effective cores / +3.8652% relative**. The CPU comparison includes all game work; it does not isolate hook cost. NPC/background timing, different startup dwell times and the recorder are possible confounders. This is one short capped pair, not a statistical confidence interval, zero-overhead claim or universal operating envelope. No simulation/audio/rendering quality setting was changed between conditions.

The earlier bundled NVIDIA PresentMon invocation produced exit 1 without frame data. This session used the official standalone **PresentMon 2.5.1**, pinned to its release asset digest by `tools/build/fetch-presentmon.ps1`. The five-second PID-specific probe and both sixty-second samples produced actual D3D9 records and exit 0. Source: [official release](https://github.com/GameTechDev/PresentMon/releases/tag/v2.5.1), [official release asset metadata](https://api.github.com/repos/GameTechDev/PresentMon/releases/tags/v2.5.1). No service or installation was added. `msBetweenPresents` is presentation timing, not simulation ticks, input delay or compositor cadence.

## Fixture, isolation and personal files

Owned developer profile: `C:\Users\SporeMP-M01`, SID `S-1-5-21-1000000000-2000000000-3000000000-1007`. The guarded wrapper enforces this identity and one-game quiescence, backs up/hashes the two personal roots, validates the executable/content/payload and verifies personal hashes after normal exit. All **29 personal files are unchanged** after both runs; see their `native/personal-after.json` records. This account is a developer probe constraint, not a normal player prerequisite.

The start is the closed, existing Satiria Creature save with the blue avatar at its nest, health 10 / DNA 0, from the previous disposable campaign. No progression shortcut or new save was created for this comparison. `fixture-start.json` records all 27 source files and the snapshot `local/m02-fixtures/0909-start`. The reference end and observed end are separately backed up and verified as `0909-reference-end` / `0909-observed-end`. The `Games/` save files and all creation files retain their starting hashes in both ends. Only `Temp/SP1788888311.evt`, `MVJCache/cacheDB.ini`, `GraphicsCache.package` and `Pollination.package` differ. See `fixture-end-comparison.json`.

Between runs, `restore.ps1` verified the closed reference end and snapshot, resolved the two targets under the owned profile, moved the original directories to sibling `.m02-0909-reference-end` archives, restored their ACLs on the replacement roots, copied the start snapshot and verified exact hashes. No recursive delete was used. Both original directories remain intact at:

- `C:\Users\SporeMP-M01\AppData\Roaming\Spore.m02-0909-reference-end`
- `C:\Users\SporeMP-M01\Documents\My Spore Creations.m02-0909-reference-end`

The game actually loaded the restored save and later reloaded it normally. This is observed native loading after file restoration; an exact save-file-open trace and atomic native save/load API remain unqualified.

## Commands and results

Commands below ran from `C:\Users\Developer\Documents\ChatGPT\SporeMP` in PowerShell. Every listed executable command expected exit 0 and observed exit 0. Fresh evidence/output paths were required; previous evidence was not overwritten. Actual underlying launch arguments and timestamps are also stored as structured arrays in each completion/request JSON.

```powershell
pwsh -NoProfile -File tools/build/fetch-presentmon.ps1
python evidence/2026-09-09-m02-completion/fixture.py snapshot start
pwsh -NoProfile -File evidence/2026-09-09-m02-completion/launch.ps1 -RunKey reference-01 -Mode Off
pwsh -NoProfile -File tools/native/capture-game.ps1 -GamePid 29324 -OutputDirectory local/m02-captures/0909-recorder-probe -Seconds 5
pwsh -NoProfile -File tools/native/capture-game.ps1 -GamePid 29324 -OutputDirectory local/m02-captures/0909-reference-01 -Seconds 240
pwsh -NoProfile -File evidence/2026-09-09-m02-completion/measure-frames.ps1 -GamePid 29324 -RunKey reference-01 -Seconds 60
python evidence/2026-09-09-m02-completion/provenance.py
pwsh -NoProfile -File tools/native/capture-game.ps1 -GamePid 29324 -OutputDirectory local/m02-captures/0909-reference-reload -Seconds 120
python evidence/2026-09-09-m02-completion/fixture.py snapshot reference-end
pwsh -NoProfile -File evidence/2026-09-09-m02-completion/restore.ps1 -EndKey reference-end
pwsh -NoProfile -File evidence/2026-09-09-m02-completion/launch.ps1 -RunKey observed-01 -Mode Observe
pwsh -NoProfile -File tools/native/capture-game.ps1 -GamePid 32344 -OutputDirectory local/m02-captures/0909-observed-01 -Seconds 420
pwsh -NoProfile -File evidence/2026-09-09-m02-completion/measure-frames.ps1 -GamePid 32344 -RunKey observed-01 -Seconds 60
python evidence/2026-09-09-m02-completion/fixture.py snapshot observed-end
python tools/native/analyze-gameplay-trace.py evidence/2026-09-09-m02-completion/observed-01/native/gameplay-32344.jsonl --require-native --require-m02-coverage --output evidence/2026-09-09-m02-completion/observed-01/gameplay-analysis.json
python evidence/2026-09-09-m02-completion/review-jumps.py reference-01
python evidence/2026-09-09-m02-completion/review-jumps.py observed-01
python evidence/2026-09-09-m02-completion/analyze-pair.py
python evidence/2026-09-09-m02-completion/review-media.py
ctest --test-dir build/win32 -C Release --output-on-failure
python evidence/2026-09-09-m02-completion/close-docs.py
```

`launch.ps1` blocks until game exit while Computer Use operates the actual window. Its underlying native wrapper calls use `-Action Launch`, names `m02-0909-reference-01` / `m02-0909-observed-01`, `-Configuration Release`, `-ObservationMode Off` / `Observe`, `-DisplayMode Fullscreen -Resolution 2560x1440`. Actual game arguments are `-f -r:2560x1440`. Reference ran 12:57:41–13:07:11 UTC; Observe 13:07:31–13:20:12 UTC. Run roots are under `C:\ProgramData\SporeMP\M01\runs\` with those names.

The PresentMon five-second probe used `external/PresentMon-2.5.1/PresentMon-2.5.1-x64.exe --process_id 29324 --output_file evidence/2026-09-09-m02-completion/reference-01/frames-probe.csv --session_name SporeMP-M02-0909-probe --timed 5 --terminate_after_timed --no_track_input --no_console_stats --v1_metrics`; the CSV/output are retained. The exact invocation was checked against the current task transcript. The sixty-second exact command arrays are in `frames-request.json`. FFmpeg recorder/filter/encoder arguments, PID/path identity, image extraction arguments and their exit codes are in `media-analysis.json` and the jump-extraction JSON files. `presentmon-help.txt` records the tool's actual available options. A failed attempt to retrieve a nonexistent upstream README URL returned HTTP 404; release metadata and the executable's own help supplied provenance/options instead.

All four WGC clips exit 0 without timeout. Their full black scans (`blackdetect=d=0.5:pix_th=0.1:pic_th=0.98`) find no qualifying black interval. This supports the actual image review rather than replacing it. The 30-FPS recorder cadence never substitutes for native timing. The recorder's `assistant_desktop_input=false` field describes the recorder process itself; this session's assistant gameplay actions are separately recorded as true in the launch completions/input log.

Release CTest passes 4/4: diagnostics_host, observation_host, tooling_unit and launcher_host. These are HOST/FIXTURE evidence, not additional game tests. The game bridge/native adapters were not rebuilt or changed this session; the exact existing source and loaded artifacts match their manifest. Later documentation and evidence scripts are recorded by final integrity checks. The preexisting launcher 0.1.3 and M02 working-tree changes remain intact; no commit or publication was made by this completion session.

The first `python evidence/2026-09-09-m02-completion/finalize.py` expected exit 0 but returned nonzero (PowerShell status 1, Python's explicit failure status 2): the original-milestone comparison decoded Git's UTF-8 output using Windows cp1252. The retained `acceptance-initial-scope-check.json` shows every other check passed. Inspecting Unicode code points identified mojibake only in the verifier's subprocess string; the actual milestone file preserved U+2014 correctly. The verifier now decodes Git bytes explicitly as UTF-8, with no change to milestone clauses. The failed record was renamed with a literal PowerShell Move-Item, preserved in this directory, before creating the final acceptance. Final command: `python evidence/2026-09-09-m02-completion/finalize.py` followed by `exit $LASTEXITCODE`; final results and checks are recorded in acceptance.json. No additional native run was needed for this reporting-code correction.

## Pinned configuration

| Item | Identity |
|---|---|
| Base Git commit | d106404da0b7c4531f63f98d0df2377bb897a0f7; existing uncommitted work preserved |
| SDK | cbf9206b9a823f0911cd9be0217104a49d72380b; native runtime 2.5.0 |
| Injector source | 26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9 |
| Original executable | GOG GA 3.1.0.29 Win32; C:\Games\SPORE\SporebinEP1\SporeApp.exe |
| Executable SHA-256 | dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37 |
| Release bridge 0.0.2 SHA-256 | 7251f5830a092cb04fd6233d9ff25c8de4c3617284fbf5a8df4468dbb03a4a9c |
| Release NativeHost SHA-256 | 72eb8e50632d24b91cfc66bbd25cbf4b201dd47662284d1f008748df9c5932f9 |
| SDK core SHA-256 | c018b51c56c537e739f24319dcfeb5cfeeefa97ded0ca91bf5729755c190ba11 |
| Injector DLL SHA-256 | 938cf3b106a459174ebfe31803b4d84238b07b004dc5c6e2da5fec61672eab55 |
| PresentMon 2.5.1 x64 SHA-256 | 9bec3083069f58f911e6a512f4806db51a27bd096103087bc1d05ef54c80a191 |
| Observed gameplay trace SHA-256 | eebc2e86240e56feff09a69b1a4d388afea9e2db6b2e5017e66af5c7ea9db55c |
| OS | Windows 11 Pro, 10.0.26200, 64-bit |
| CPU / RAM | Ryzen 7 9700X, 8 cores / 16 logical processors; 33,407,430,656 bytes RAM |
| Graphics | RTX 4080 SUPER, driver 32.0.15.9649; Radeon and Parsec adapters also enumerated in provenance |

Source and ABI declarations, preferred-image addresses, typed receiver adjustment, engine-thread requirements and the exact original-forwarding/lifetime audit remain in `docs/m02-binding-registry.md`. Qualification applies only to these executed paths and this content/build configuration.

## Remaining coverage and next smallest step

M02's original acceptance requires at least one complete native action, engine/mod event distinction, real entity invalidation, unchanged observational baseline and explicit unknowns. This evidence satisfies those conditions without changing their text. The full M00–M20 plan, all five stage requirements and feature coverage inventory are retained.

Damage/death events, inventory/progression, native save/load interception, full factory/destructor paths and other stage-native fixtures remain **NOT RUN / unknown** as detailed in the conditional tracing backlog. A health/dead sample is not a combat event; native ID reuse is not persistent multiplayer identity; a file restore is not an atomic native checkpoint API. Native Debug, multiple workers, networking, asynchronous multi-actor attribution, sustained/hardware-diverse timing and all-stage gameplay are unqualified. No multiplayer gameplay is implemented by M02.

Next is M03: audit source/call-site receiver context for actor creation and the shared TakeDamage address, then build one disposable two-actor original-game fixture that proves independent commands and correct delayed action/resource/damage attribution. Keep native functions authoritative and reject unverified bindings. No M03 implementation or acceptance is included in this session.
