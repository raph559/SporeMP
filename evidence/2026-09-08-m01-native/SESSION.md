# M01 completion — 2026-09-08

M01 is **VERIFIED** for the exact local configuration, under the user's amended player requirements. The user confirmed the simplified launcher works and asked to stop repeating tests. Closure uses the completed native/guard work and that confirmation; no additional game launch or test followed it.

## Final product decision

The user rejected mandatory separate Windows profiles and personal-save protection gates in the player flow. Normal Play now uses the existing Windows account and SPORE saves. Discovery and compatibility checks happen automatically; paths and diagnostics stay in Settings. Optional developer isolation/backup scripts remain separate. GOAL.md and MILESTONES.md record the amendment without changing the preserved implementation brief or removing later milestones.

The final normal-account route was confirmed by the user, not captured as a fresh assistant native trace. The assistant's attempted UI check was interrupted by physical Escape and stopped. Closing the launcher during play is implemented to hide its window until game exit; the user's overall confirmation is not expanded into unobserved claims about every window action.

## Implementation and provenance

Changed modules include the compiled Windows native host, build/dependency identity generation, real SDK-base linkage, read-only native path diagnostics, optional account/probe tooling, ETW extraction/summaries, launcher backend/UI and relevant fixture tests.

- Game: C:\Games\SPORE\SporebinEP1\SporeApp.exe, GOG GA 3.1.0.29, PE32 x86, 24,895,536 bytes; SHA-256 dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37.
- Content: 108 files, 6,199,616,926 bytes, exact Data/DataEP1/bp1content/SporebinEP1 inventory in config/compatibility.candidate.json. This is locally qualified observed content, not official clean-content certification.
- SDK: cbf9206b9a823f0911cd9be0217104a49d72380b, local runtime 2.5.0, March2017 game type 1.
- Injector: pinned Launcher Kit 26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9, built ModAPI.DLLInjector component. The payload loads current SDK core and SporeMP bridge, with no legacy mods.
- Windows 11 Pro 26200 x64; Ryzen 7 9700X, approximately 32 GB RAM, RTX 4080 SUPER. Full hardware/driver/tool versions: environment.json.
- Win32/v143, MSVC tool directory 14.44.35207/compiler 19.44.35226; Windows SDK 10.0.26100.0; /MD and /MDd. Launcher .NET SDK 8.0.418/runtime 8.0.24.
- artifacts.json identifies the earlier captured native configuration. final-files.json records final source/build artifact hashes after the player-flow correction. The earlier clean export's immutable tree and artifact hashes are in clean-build-result.json.

## Loader and lifecycle audit

The host has no SDK-core import. It rejects unknown content and compiled payload hashes before creating a game, locks files against write/delete, creates the original process suspended and configures the pinned injector before resuming. --play preserves the current user/profile. Developer --probe/--launch retain explicit disposable-SID/known-folder/access-denial checks.

Injector ABI comes from pinned ModAPI.Common/Injector.cs and ModAPI.DLLInjector/dllmain.cpp: exported SetInjectionData is APIENTRY, taking a byte buffer with non-disc selector, uint32 count, and length-prefixed UTF-16 paths. Its allocator initialization detour loads the DLL list after native allocator initialization; disposal unloads in reverse before native allocator disposal. The host uses the pinned same-bitness LoadLibrary bootstrap, then verifies actual module mappings before configuration/resume. It uses dynamic Windows exports, not invented game offsets or launch flags.

Bridge DllMain only registers SDK post-init/disposal callbacks. SDK DllModAPI.cpp appends these to fixed vectors (capacity 2048); SporeMP adds one callback to each. The SDK core itself attaches native/texture/icon detours under loader lock, and the injector attaches its GetStartupInfoA hook there. These are audited pinned dependency behaviors, not work moved into SporeMP DllMain. Bridge hashing, diagnostics and native reads occur in the SDK callbacks. No networking, gameplay action, thread join or project gameplay detour runs under loader lock.

AppInit_detour calls the original initializer before post-init callbacks. AppShutdown_detour invokes disposal callbacks before shutdown. Resource::Paths::GetDirFromID is const char16_t*(PathID), x86 cdecl, from Spore/Resource/Paths.h, SourceCode/IO/IODefinitions.cpp and AddressesResource.cpp:264 (SelectAddress(0x6888A0, 0x688650)). The real SDK base static library supplies that wrapper. Calls were read-only, after post-init, on the engine thread. BaseData/BaseDataLocale returned null and are retained as such.

## Native results

| Run | Original PID | Expected and observed |
|---|---:|---|
| native-attempt-01 | 7520 | Initial suspended-process bootstrap failed with Remote module unavailable: KERNEL32.DLL; owned suspended game terminated, exit 32. Not a clean native run. |
| native-attempt-02 | 15160 | Earlier bridge initialized/disposed; normal exit 0. Preliminary run, not counted in the current-bridge three-run set. |
| lifecycle-01 | 34720 | Current bridge initialized/disposed on the same engine thread; actual galaxy menu and normal UI quit; exit 0. |
| lifecycle-02 | 33932 | Same current bridge and clean result; exit 0. |
| gui-aa6e8891815042a6 | 35772 | Launched through the earlier desktop button; actual menu, normal UI quit, native/launcher exit 0 and exported lifecycle report. |

The last three constitute the captured current-bridge set. They used the earlier isolated launcher mode; the bridge remains in the final player build. Native-lifecycles.json, runs/, gui-native-result.json and gui-export-verification.json retain exact timestamps, engine threads, executable identity and loaded-module hashes.

The user authorized closing the optional registration panel. No login or account registration was performed. All counted exits used the game UI, not forced termination.

Actual Resource paths placed test saves, creations and temporary data beneath C:\Users\SporeMP-M01; installation data/config remained beneath C:\Games\SPORE. ETW summaries record real file/registry requests and successful writes. No resolved personal-profile opens/completed writes were seen. Some file/registry correlations remain unresolved. lifecycle-01 includes unrelated DefenderApiLogger loss markers; later custom-profile captures have no such markers and report events_lost=0. Shared NVIDIA driver logs/cache in ProgramData were written. These are not proof of a universal filesystem sandbox.

OS probes denied write/delete access to protected personal/game paths. Personal-file-verification.json compares complete current trees against each recorded pre-run backup: 29 files, 23,886,102 bytes unchanged at collection. lifecycle-01's subsequent backup and lifecycle-02's GUI preparation provide their after-state comparisons; GUI has an immediate personal-after.json. This statement describes those earlier probes, not subsequent normal player save updates.

## Executed commands and outcomes

All commands were run from the repository with PowerShell. Build/test tooling itself never launched a game.

| Command or interaction | Expected / observed exit and evidence |
|---|---|
| pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release -FetchDependencies | Build pinned SDK/core/base/injector/bridge/host/launcher; successful build recorded in build logs. |
| pwsh -NoProfile -File tools/build/build.ps1 -Configuration Debug | Exit 0; build-debug-final.log. |
| ctest --test-dir build/win32 -C Release --output-on-failure -V | Exit 0, 3/3; ctest-release-final.log, before final player correction. |
| ctest --test-dir build/win32 -C Debug --output-on-failure -V | Exit 0, 3/3; ctest-debug-final.log, before final player correction. |
| pwsh -NoProfile -File tools/native/invoke-isolated.ps1 -Action Launch -RunName lifecycle-01 | Exit 0; lifecycle-01.log and runs/lifecycle-01. |
| pwsh -NoProfile -File tools/native/invoke-isolated.ps1 -Action Launch -RunName lifecycle-02 | Exit 0; lifecycle-02.log and runs/lifecycle-02. |
| pwsh -NoProfile -File tools/native/test-guard.ps1 -RunName guard-final-02 | Runner exit 0; six expected guard exits 20 and zero game creations; guard-results.json. |
| wpr -start tools/native/sporemp.wprp!SporeMP -filemode | Exit 0 for custom captures. |
| wpr -stop local/native-traces/gui-lifecycle-03.etl | Exit 0; wpr-gui-stop.log. |
| build/win32/Release/SporeMP.TraceExtract.exe local/native-traces/gui-lifecycle-03.etl local/native-traces/gui-lifecycle-03-game.jsonl 35772 | Exit 0; trace-extract-gui-lifecycle-03.json. Same extractor used with recorded lifecycle-01/02 PIDs. |
| python tools/native/summarize-trace.py local/native-traces/gui-lifecycle-03-game.jsonl evidence/2026-09-08-m01-native/gui-lifecycle-03-file-access.json --personal-profile C:\Users\Developer | Exit 0; GUI file-access summary. Other named summaries retain their corresponding traces. |
| pwsh -NoProfile -File tools/build/test-clean-build.ps1 -Destination local/clean-room-m01-native-final | Exit 0; both dependencies fetched into empty export, build and 3/3 tests passed. Source tree 82f22ce53565f6212062f036a3b3590960fd592e. Predates final player-flow correction. |
| pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release | Final player correction exit 0; build-player-release.log. |
| pwsh -NoProfile -File tools/build/build.ps1 -Configuration Debug | Final player correction exit 0; build-player-debug.log. |
| python -m unittest discover -s tests/unit -p 'test_*.py' -v | Exit 0, 38 tests; python-player.log. |
| ctest --test-dir build/win32 -C Debug --output-on-failure -V | Final player correction exit 0, 3/3 targets; ctest-player-debug.log. |
| Final simplified player UI | User confirmed “it works”; no additional assistant run after acceptance. |

Additional raw build/OS-probe logs preserve intermediate failures: inaccessible personal PowerShell runtime; child execution-policy refusal; missing SDK-base linkage; initial trace-extractor include error; launcher EXE locked by its previous window. They were resolved before the successful results above. The dedicated OS probe used installed Windows PowerShell and process-scoped RemoteSigned, without changing machine execution policy.

The evidence collector exited 1 at its final byte-identical source comparison because of source-export line endings. Its completed native/guard/personal-file records remain raw evidence; it did not produce a blanket qualification-result.json. A subsequent read-only comparison found no pre-player source-content differences after CRLF/LF normalization. Neither that comparison nor the clean build establishes byte-identical binary reproducibility. Final milestone closure is the explicit completion.json record with user acceptance.

## Limits, rollback and next step

M01 establishes startup/guard/lifecycle tooling for the local configuration. It does not implement multiplayer, native checkpoint restoration, unattended workers, multiple game instances, gameplay observability, Internet hosting or packaged distribution. Those remain M02–M19; optional adventures remain M20.

Player DLLs are staged under local/launcher, with no installation into the personal game. The earlier developer account, profile, ProgramData staging and test-SID-only game ACL entry remain optional local artifacts and are unused by Play. docs/recovery.md records their owned paths and selective rollback instructions; no cleanup or personal-save restoration was performed during closure.

Next smallest implementation step is M02's bounded native gameplay action trace. Do not reopen M01's completed tests merely to reconfirm acceptance.
