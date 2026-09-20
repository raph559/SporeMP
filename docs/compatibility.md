# Compatibility and save protection

## M04 developer worker increment — 2026-09-13

Two real standard accounts, SporeMP-M04-01/02, have distinct OS profiles and hash-checked post-M03 Creature fixtures. Both pass open-only write/delete-denial probes against personal/game and peer profile/save/creation paths. The native host enforces one game per actual SID across sessions and rejects unqueryable process identity before injection. Only workers append the statically identified original `-multipleInstances` switch; no game binary is patched.

The original concurrent processes execute native IPC actions in distinct save/creation paths. Final 0.0.14 acceptance includes overlapping original save calls, successful own-profile checkpoint writes, clean exits and separately sealed backups. Concurrent ETW reports zero lost events and no resolved personal/peer-profile opens or mutations. Some file/registry correlations remain unresolved; shared NVIDIA driver writes occurred. Normal Play changed ten personal files with observed successful player mutations for every changed path; those changes were retained and backed up. The later concurrent worker phase leaves all 29 personal baseline files unchanged. These observations qualify the bounded paths exercised, not every shared Windows/configuration path. See [M04 acceptance](../evidence/2026-09-13-m04-acceptance/SESSION.md).

The separate, never-activated desktop remains a renderer [1001] failure. Current-desktop unminimized windows work; losing focus does not prevent the tested actions. Minimization stops app/AI progress; restoration before the 30-second update deadline resumes it. Native Options/Save dialogs pause AI while app heartbeats continue. Locked/disconnected/service/RDP/VM operation is NOT RUN. Developer accounts are not prerequisites for normal Play. See [worker limits and checkpoint reproduction](m04-workers.md) and [current native evidence](../evidence/2026-09-13-m04-acceptance/SESSION.md). A later approximately 47-second native AI plateau with continuing app updates was correctly reported as `simulation_stalled`; its cause is unproven and indefinite autonomous progression remains unqualified.

## Observed candidate, not a supported release

| Item | Observed/pinned value |
|---|---|
| Installation | GOG SPORE Collection, `C:\Games\SPORE` |
| Candidate executable | `SporebinEP1\SporeApp.exe`, file/product version 3.1.0.29 |
| Architecture | PE32 / IMAGE_FILE_MACHINE_I386 (`0x014c`), executable rather than DLL |
| Size | 24,895,536 bytes |
| SHA-256 | `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37` |
| SDK commit | `cbf9206b9a823f0911cd9be0217104a49d72380b` |
| SDK binding family | `EXECUTABLE_TYPE=2`, March2017; the upstream combined selector lists this file size |
| Local SDK version | 2.5.0 development build (`SDK_BUILD_VER=0`), not a published release number |
| Native compatibility | M01 native lifecycle qualified for this exact local configuration; final player flow confirmed by user |
| Initial content | Exact observed trees for Data, DataEP1, bp1content and SporebinEP1 |
| Clean standard-content equivalence | **NOT VERIFIED**; a local inventory is not an official content certification |

The base-game executable is 1.3.0.29, 20,454,960 bytes, SHA-256 `25d42a7a5c4d438fb155233230f57d29e2849bfdff5c889a5d0847f0469d914e`. It is not the bridge target. GA is the runtime candidate supporting the campaign integration, not a scope change to adventure-only multiplayer.

`../config/compatibility.candidate.json` records exact PE fields and immutable content hashes. Inspection is read-only. Validation rejects different executable bytes (including same-size changes), x64 images, DLLs, malformed/truncated PE headers, missing roots and added/changed/removed immutable content. Reparse points are refused. The pinned SDK's exact regular `SporebinEP1/spore_log.txt` is its documented runtime log; it is permitted only after safe-path validation and separately fingerprinted under `runtime_artifacts`. No other unknown file or directory receives an exemption. This narrowly scoped rule was required after actual normal Play generated that log and the next worker was correctly rejected before launch. No candidate field grants launch permission.

## Guard ordering and launch modes

The upstream SDK binds native addresses during DLL load. The implemented SporeMP.NativeHost validates compiled game/content/payload fingerprints before creating or injecting a game. It rejects reparse paths and unknown content entries, holds file read locks, starts the original process suspended, configures the pinned injector and resumes only after success. A late bridge check is secondary.

The loader source is pinned at 26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9. This configuration loads the current SDK core and project bridge; legacy mods are not included. Three clean native runs and six rejection fixtures are recorded in ../evidence/2026-09-08-m01-native/SESSION.md.

Normal Play uses --play under the player's normal Windows account and existing save paths. There is no separate-account or backup gate and no profile redirection, following the user's explicit correction. Project DLLs stay in local/launcher/native-payload rather than being installed into SPORE.

Launcher 0.1.3 optionally supplies an allowlisted display mode/resolution pair. The host appends only the original readme's documented `-w`/`-f` and `-r:<width>x<height>` options after validating their values. All executable/content/payload guards still precede process creation. The default supplies no override. Source provenance, physical display enumeration and the pending native display acceptance are in launcher.md; no compatibility claim is extended to another executable or new SDK binding.

Optional developer --probe/--launch modes retain the owned SporeMP-M01 account, SID/known-folder checks and protected-path OS denial probes. Earlier environment-variable-only isolation failed; real isolated native runs subsequently reported save/creation/temp paths under C:\Users\SporeMP-M01.

Native ETW traces include actual save/config writes in that test profile and shared NVIDIA driver writes in ProgramData. Some correlations remain unresolved. Complete personal-file manifests and OS denial probes independently establish preservation during those earlier tests. Normal Play intentionally uses normal saves.

The old Python preflight starts no process and does not govern the implemented player action. Other executable/content configurations and official clean-content equivalence remain unqualified.

## M02 observation gate

Bridge 0.0.2's M02 baseline is **VERIFIED for the recorded original Creature paths**, separate from M01's qualification. The 2026-09-09 restored-fixture pair supplies actual reference/observed game video, original jump/landing traces, scene exit/reload, rendered read-only diagnostics, measured presentation/CPU samples and clean exits without trace loss or foreign callbacks. Complete lifetime paths, other stages and gameplay families remain unqualified. The developer `--observe` host mode retains the same compiled file/content/payload guard and disposable-account identity checks as `--launch`, then sets `SPOREMP_M02_TRACE=observe` in the child environment. `--play` and reference `--launch` explicitly clear that variable, even if inherited from the parent. The isolated developer wrapper exposes this as `-ObservationMode Observe`; default is `Off`.

Neither building, CTest, nor the log analyzer launches SPORE or installs a DLL into the game directory. `sporemp_trace` is registered only inside the opted-in original process and only reads state/counters. New source/ABI provenance and pending runtime limits are in `m02-binding-registry.md`; the fixture comparison is in `../tests/engine/M02.md`. The user's normal account/saves remain the normal Play flow.
