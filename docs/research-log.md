# Native capability investigations

No gameplay hypothesis is verified. Each investigation must produce a trace, executable probe, failed reproduction or code change; headers alone do not satisfy acceptance.

M01 resolution (2026-09-08): R01–R03 below describe the initial investigations. They were subsequently resolved through the pinned built injector, a guarded native host, three clean bridge lifecycles and recorded native paths. The user later rejected profile/backup gates for players and confirmed the normal-account launcher works. See ../evidence/2026-09-08-m01-native/SESSION.md. These initial NOT RUN statements are historical, not current blockers.

## R01 — SDK lifecycle and build identity (M01)

Pinned SDK: `cbf9206b9a823f0911cd9be0217104a49d72380b`. Read `Spore/ModAPI.h`, `SourceCode/DLL/DllModAPI.cpp`, `Application.cpp`, `dllmain.cpp` and the actual vcxproj. The local executable size 24,895,536 appears in the combined selector as March2017. Current projects use v143; the older installation tutorial says v142. Implemented a Win32 v143 build using the actual project and generated import library. No guessed exports or import stubs are used.

The SDK core installs detours at load. Post-init callbacks run after `AppInit_detour` calls its original function; disposal callbacks run inside `AppShutdown_detour`. The bridge only registers these two callbacks and installs no gameplay hooks. Callback execution is NOT RUN. Next: pin/qualify loader/core artifacts inside an isolated native test environment and collect initialize/dispose traces over three clean starts/exits.

Initial CMake generation failed because MinSizeRel/RelWithDebInfo were generated without corresponding imported libraries. Resolved by limiting configurations to Debug/Release. Initial isolation probe stopped on the first child HRESULT and lost detail; resolved by recording per-folder errors and both environment variants. See session evidence for commands and results.

## R02 — Environment variables do not establish profile isolation (M01/M04)

Minimal reproduction:

```powershell
python tools/diagnostics/sporemp_diag.py new-profile --root local/profiles --name isolation-repro
python tools/diagnostics/sporemp_diag.py probe-isolation --profile local/profiles/isolation-repro/profile.json
```

Expected gate result: exit 21, `isolation_verified=false`, original-game test NOT RUN. Observed on this machine: APPDATA/LOCALAPPDATA overrides alone retain personal shell folders. Adding USERPROFILE produces `SHGetFolderPathW` errors `HRESULT -2147024894 (0x80070002)` for AppData and LocalAppData, while Documents changes. A directory workspace is not a Windows profile.

Missing capability: a test process launch arrangement with demonstrated game/launcher save, creation, cache, config and registry isolation. This is an OS/path-isolation prerequisite, not evidence that original gameplay cannot be reused.

Next smallest experiment: establish a disposable Windows user/VM with its own shell folders and game access, then capture an unmodified native startup's file/registry paths. Alternatively investigate the source-documented early `cAppSystem::SetUserDirNames` path with a read-only binary call-site audit before a confined runtime probe. Do not call `IAppSystem::Get()` before its documented initialization boundary. Enumerate the resulting Resource path IDs and compare to access traces; test two processes later in M04.

## R03 — Loader setup (M01)

No installation found in the targeted registry, standard directories or `%APPDATA%\Spore ModAPI Launcher` lookup. Source inspected at `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. The launcher starts a suspended original process, injects `ModAPI.DLLInjector.dll`, and requests the current core, legacy core and mod DLLs. Its source also copies files to mLibs and writes user metadata. A source clone is not an installed/qualified loader. No installer was executed.

Next: provision a pinned release or build in the isolated test environment, inventory all injected files and turn the read-only preflight into the guarded launch adapter. It must reject an executable/content/loader mismatch before injector/core loading. The current bridge/core build is legitimate compilation evidence, not a load test.

## Architecture hypotheses and executable gates

| Hypothesis | Next bounded investigation | Required observable result | Gate |
|---|---|---|---|
| H1 multiple owned actors preserve native player context | Trace one native action in M02, then issue distinct actions to two native actors across AI ticks and a delayed completion | Correct actor/faction resource, target, damage/reward and death attribution; no temporary-avatar-swap leakage | M03; block dependent gameplay if it fails |
| H2 authority can separate from presentation | Apply one native result to a replica while tracing local decisions/rewards and render/UI activity | One authoritative outcome; no re-emission, duplicate AI or reward, retained presentation | M05 |
| H3 unattended workers can be isolated and supervised | R02/R03, then clean lifecycle, focus-loss, two-workspace and single-worker-kill probes | Paths confined, native tick continues where required, one failure does not kill the other | M01/M04 |
| H4 sufficient native state can be checkpointed/restored | Controlled encounter save/reload plus ID map; kill after action/event/metadata/artifact boundaries | Valid committed identities and native state, explicit rollback window, no duplicate durable reward | M04/M09 |
| H5 workers share one canonical universe | Trace off-screen galaxy mutations on two workers, then fenced location transfer with crash injection | Exactly one global-system outcome and one actor authority after restart/transfer | M10/M15 |
| H6 mixed stages share native consequences | Lower-stage player and Space visitor, canonical terrain/population owner, observed native tool effect | Matching supported consequence after reload without forced advancement | M16 |

Stage-specific bindings remain unknown until separately audited. In particular Cell does not inherit Creature interfaces. `native-behavior-baseline.md` and its coverage CSV carry every initial mechanic family forward.
