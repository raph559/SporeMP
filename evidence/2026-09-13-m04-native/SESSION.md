# M04 current-desktop native qualification — 2026-09-13

Status: **IN_PROGRESS**. This session establishes bounded original-game worker actions, concurrency, crash containment and shutdown. It also establishes a manual native save/reload path and a concrete missing actor-ownership restore. It does not complete M04 or implement multiplayer networking.

The user said **“its available”** in response to the pending desktop-availability request. Computer Use inspected and operated only the explicitly selected original-game windows. The assistant dismissed optional registration/offline notices, selected the preserved Satiria save, tested minimization/restoration and invoked original Save. No gameplay movement/jump input performed the IPC action tests. No personal-account game was started.

## Identity and preserved starting state

- Original executable: `C:/Games/SPORE/SporebinEP1/SporeApp.exe`, GOG GA 3.1.0.29 running the original Creature campaign, SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
- SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`; injector source `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`; bridge 0.0.13. Only project DLLs staged under owned ProgramData directories were loaded; no DLL was installed into the personal game.
- NativeHost SHA-256 `dbda343c49e80d5efb22feb38e76bd80180284ef6a651de16ebebec1abb0255d`; bridge `c4b62d17bb15e3ca7904002647c52a3a6420e623d958e7ea9401eb705acf6515`; WorkerControl `80bfaa560b757b015d32b05b4464a0c04598ad976904dc2d0fa06b4cc4c320af`. All three loaded DLL hashes were independently matched for all four processes.
- The preflight rechecked all 127 source files and recorded artifacts against `../2026-09-12-m04-workers/provenance-02.json` before native execution. The native source archive is `local/m04-final-source-02.zip`, SHA-256 `cb9abc6e34b696615175e34bc2a13b2c83b0b6b53e465713e879eb925d50a9fc`. Later launcher 0.1.5 changes are separately identified in final provenance; native code/binaries were unchanged.
- Windows 11 Pro 10.0.26200; Ryzen 7 9700X, 8 cores/16 logical processors; RTX 4080 SUPER driver 32.0.15.9649; 33,407,430,656 bytes RAM. AMD integrated GPU and Parsec Virtual Display Adapter are also installed. Current details: `environment.json`.
- Workers use actual standard accounts `SporeMP-M04-01/02`, SIDs ending 1008/1009, separate OS profiles, and actual denied personal/game/peer write/delete probes. Initial fixture is `local/m03-fixtures/0912-post-award01`, manifest `978e60923b9192dcc9ceb03a83b82cacfb3aa5e3258296a70dd891bd327f0b16`. Both initial 27-file trees matched before launch; fresh closed backups were taken.

Raw saves, recordings, ETLs, decompiled game exports and credentials remain under ignored local directories. Evidence contains metadata, project sources, hashes and traces, not original game binaries or save payloads.

## Original processes and outcomes

| Evidence directory | Worker / generation | Game / supervisor PID | Observed result |
|---|---|---|---|
| `run-01` | 01 / `70db0dc5fbff4140a832fb074778869e` | 33096 / 26076 | Initial native A/B IPC test; clean game exit 0; 629 contiguous actor records, clean detach 0. |
| `concurrent-01` | 01 / `edb70a0b1ef94a679582fdf1270a3147` | 30140 / 29916 | Native actions while unfocused; deliberate game termination -1 (DWORD 4294967295), supervisor 31, status crashed; 3,265 contiguous recorded actor records. No clean-disposal claim for the killed process. |
| `concurrent-02` | 02 / `3ad40ff385824d87bd5b5e46b7dce27b` | 21944 / 29672 | Native actions, minimization/recovery, survives peer crash and acts again, original UI Save; game/supervisor exit 0; 3,499 contiguous actor records, detach 0. |
| `reload-02` | 02 / `a1b4a0f97c7d408fae1d28a8056ce83a` | 35752 / 37808 | Manual reload of the new closed native checkpoint; ownership absent; stale requests rejected; game exit 0; 541 contiguous actor records, detach 0. |

Every recorded native callback remained on the expected engine thread with zero foreign callbacks. Three clean runs have exactly one initialization/disposal pair on the same thread. Shutdown request-to-game-exit delays are 0.523, 0.530 and 0.536 seconds. The saved run retained live process handles and directly recorded supervisor exit 0. The other two clean detached supervisor exit codes were not captured; only their game exit 0 and final stopped status are established.

The actual worker command line uses `-w -r:1280x720 -multipleInstances`; the latter is the statically verified original switch. Both actual process tokens and native Resource paths identify separate worker profiles. Initial scene selection/load was through the original UI, not an unattended load binding.

Four invocations of `test-worker-actions.py` each sent one wrong-owner and one stale-epoch rejection, then exactly one A jump and one B jump, without mutation retries. All eight original returns have one later correlated native landing, 0.8366693–1.0087859 seconds after return. `accepted` only meant queued; the trace correlation supplies the observed action result. A/B in the first run were also visibly inspected in the recorded action contact sheet. The concurrent and survivor results rely on native traces with live scene inspection, not a continuous video of every jump.

Each operator process exits after its request. Subsequent native actions/AI continue in the same independently supervised game. This demonstrates actual operator-pipe independence, not the still-pending real player-process closure or network-client lifetime gate.

## Desktop and resource observations

Worker 01 completed actions while worker 02 was in front. After both scenes loaded, worker 02 was minimized through its actual window title bar. The UI API reported that window minimized. Across 15.268221 seconds, worker 02's app and AI counters did not change and its status transitioned to unresponsive; worker 01 remained ready with 858 new app updates and 26,598 native AI entries. Worker 02 was restored before the implemented 30-second update timeout and resumed. No long-minimization kill was exercised.

Across the subsequent 5.105734-second stationary sample, both workers remained ready: 263/307 app updates and 8,153/9,210 native AI entries. Whole-process CPU consumption was 0.278486/0.306028 effective cores; working sets ranged 492,347,392–492,793,856 / 503,439,360–503,726,080 bytes. This includes ETW tracing and uncontrolled native NPC timing. It is not a capacity estimate, frame-time result or isolated bridge-overhead measurement. GPU utilization, native frame timing, lock/disconnect/service/RDP/VM behavior are NOT RUN.

The original Options menu and Save success dialog pause AI while app heartbeats continue; the supervisor reports simulation_stalled. The previously recorded private-desktop renderer [1001] failure remains valid. The tested operating arrangement is the current signed-in desktop with unminimized rendered windows.

## File/config isolation and native checkpoint

WPR was confirmed idle before this session's traces and stopped afterward. Concurrent trace `local/m04-native/concurrent-01.etl` spans both native initializations, original scene loads, actions, minimization/recovery and worker-01 termination. Each PID was extracted independently; both extractions report zero events lost and summaries contain no loss markers. The trace ends before worker 02's subsequent Save.

There are no resolved personal/peer-profile opens or completed mutations. Native paths and resolved runtime writes use each worker's own OS profile. Resolved own-profile mutation counts are 444/461. Unresolved file writes remain 61/62, plus 3 SetInfo requests for worker 02; unresolved registry SetInformation correlations remain 3,412/3,355. Both processes also write shared NVIDIA `nvAppTimestamps` and `CaptureCore.log` files, as well as their own project trace files. These are retained limitations, not complete sandbox/config coverage. Simultaneous native checkpoint writes have not been tested.

Original Options > Save in worker 02 displayed **“The game on planet Satiria is saved.”** The separate save trace records 423 resolved own-profile mutations including temporary save writes and checkpoint renames. It also retains 443 unresolved write requests and 2 SetInfo/2 registry SetInformation correlations; the tracer started after the original process had already opened files. Save completion is supported by the original dialog, closed artifacts and subsequent native reload, not inferred from a file-event count.

After clean shutdown, `local/m04-native/worker-02-after-concurrent` preserves the full closed checkpoint (manifest `6d6aa9208d3eecefb0576b30fdae290d81ea3bfead6a5e02fb2350dbe5702583`). The next process shows the updated 09/13/26 Satiria save date and loads the original Creature scene with live native AI. Its bridge ownership status remains A=0/B=0. An old-generation request is unreachable (3), a stale-epoch request is rejected stale (5), and the old unbound actor is rejected invalid (5). No new setup/actor creation was issued after this reload. Native B noun persistence itself is not established by its missing bridge binding.

All 29 personal save/creation files match the exact before backup after every closed phase. Worker 01's Games subtree stayed unchanged, including across the deliberate crash. Worker 02's Save changed its own native checkpoint files; the entire Games subtree remained unchanged after the later reload/clean exit. Creation files stayed unchanged for both workers. Cache/event/login-preference changes and all before/after manifests are preserved, not reset to fabricate a pristine result.

## Visual evidence

- `local/m04-native/capture-check-01`: 5-second PID-filtered WGC test; inspected 2-second frame contains the actual native galaxy intro.
- `local/m04-native/capture-actions-01`: 120 seconds, original game PID 33096; contains manual setup and both IPC jump results. The inspected 5x8 contact sheet samples video time 100 seconds onward at 5 fps and visibly shows distinct A/B jumps and landings.
- `local/m04-native/capture-save-01`: 30 seconds, PID 21944; the inspected 20-second frame contains the original successful Save dialog.
- `local/m04-native/capture-reload-01`: 40 seconds, PID 35752; the inspected 35-second frame contains the newly dated checkpoint selection. **This clip ends before the final loaded-scene inspection.** Its image originally named `reloaded-scene.png` is a menu frame, not evidence of rendered recovery.
- `local/m04-native/reloaded-scene-live.jpg`: preserved bytes of the already-inspected Computer Use screenshot from the loaded native scene, before game shutdown. It is an actual JPEG. The saved file was inspected again; it is not a new capture after shutdown.

WGC filters require exactly one matching actual game PID, executable and title; no monitor/desktop fallback was used. No capture was started while two games made that filter ambiguous. Recorder exits are 0, without wall timeouts. The recorder's `assistant_desktop_input=false` metadata describes the recorder helper; it does not mean the assistant made no scene-selection/UI input during the whole session. Capture cadence is not native presentation timing. Raw media was not uploaded or committed.

## Commands, implementation changes and verification

Each named operation has its exact argument array, timestamps, expected and observed process exit in `NAME.command.json` and a corresponding log where the program emitted output. The runner refuses existing evidence paths. Key records:

| Records | Expected / observed |
|---|---|
| `preflight-01`, all `closed-*`, all `personal-after-*` | 0 / 0; verified before/after backups and identities. |
| `start-01`, `start-concurrent-01/02`, `start-reload-02` | 0 / 0 for detached launcher script; actual game outcomes are separately recorded above. |
| `actions-01`, `actions-concurrent-01/02`, `actions-survivor-02` | 0 / 0, original actions observed; full milestone remains NOT_VERIFIED. Internal expected rejections are exit 5. |
| `crash-injection-01` | 0 / 0 for the verified-PID injection script; game -1 and host 31 expected/observed. |
| `shutdown-01`, `shutdown-saved-02`, `shutdown-reloaded-02` | 0 / 0 for control; three subsequent native exits 0. |
| `check-reloaded-ownership` | 0 / 0; internal expected control exits 0, 3, 5, 5 all observed. |
| WPR start/stop, PID extraction and file summaries | 0 / 0; native ETW loss/unknown fields retained. |
| `analyze-native-01` | Expected 0, observed 1: evidence analyzer syntax error from a raw string ending in a backslash. No native test was repeated. Fixed analyzer `analyze-native-02` exits 0. |
| `static-checkpoint-paths-01`, `static-checkpoint-callers-01` | 0 / 0; Ghidra 12.1.3/JDK 21.0.12.1 static exports against the pinned executable. Not native ABI acceptance. |
| `build-launcher-015-final`, `launcher-host-final`, `worker-python-final` | 0 / 0; zero-warning Release build, 30 launcher HOST assertions, nine worker Python tests. |
| `preview-015-build/run` | 0 / 0; actual WPF against the restricted fixture service, no displayed window/production startup; updated minimum-size guidance visually inspected. |

Several exploratory read commands used nonexistent guessed filenames/globs; they made no changes and provided no evidence. Frame extraction commands are preserved in the conversation and final artifact metadata; they did not invoke native gameplay. Native Debug/full regression, real player closure and full launcher-to-native-worker flow were not run. Existing M03 combat acceptance was not replayed.

Launcher 0.1.5 changes only worker-window guidance plus aligned version/release notes/CHANGELOG. Its Python roster and WPF guidance explain that minimization pauses the simulation and may cause a timeout. The existing actual WPF fixture rendered normal/minimum sizes and exercised selection/start/stop with zero binding errors; displayed fixture counters remain synthetic. The production executable is `build/launcher/Release/SporeMP.exe`.

## Remaining gate and next smallest increment

M04 remains IN_PROGRESS. The original native checkpoint can be saved and manually reloaded, but there is no unattended load/save binding, proven programmatic completion boundary or restored actor ownership. The minimal reproduction and expected/actual behavior are in `docs/m04-workers.md`.

Static helper `0xBADAD0` dereferences the selected star record and consumes star/species/load parameters, making guessed zero-filled `GameLoadParameters` unsafe. Native save dispatcher `0xB29960` defers work; `0xB29000` serializes temporary artifacts and `0xB28890` touches the `.old/complete` path. Callers `0xB29600` and `0xB28B20` have also been exported for audit. Decompiler return types and function names are not binding acceptance.

Next: verify original load construction/lifetimes and save completion, add a checkpoint-bound identity sidecar and restore/fence only matching native nouns without duplicate setup. Then test real player closure, full launcher worker controls and simultaneous checkpoint access. Broader desktop modes remain explicit operating limits. Every original M00–M20 acceptance clause is retained; M05–M19 remain gated. All native processes and owned WPR recordings are stopped at session close.
