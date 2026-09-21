# SDK Detours 4 migration

The supported SporeMP build uses MIT-licensed Detours 4.0.1 for the SDK DLL, SDK base headers, bridge and injector. The upstream SDK's old Detours 3 files are no longer inputs to these builds. The SDK commit remains `cbf9206b9a823f0911cd9be0217104a49d72380b`; the Detours source is the already pinned Launcher Kit commit `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. No upstream checkout is edited.

## Build integration

1. `tools/build/build.ps1` verifies clean pinned dependency checkouts and builds the MIT library using the pinned MSVC Win32 toolchain.
2. `sdk-detours4.targets` is passed through MSBuild's `ForceImportAfterCppTargets` hook. It replaces both SDK configurations' old include paths, including the upstream `E:\Detours` fallback, and the SDK DLL's old library dependency. Standard MSVC/Windows libraries remain available.
3. SDK objects use fresh `obj-detours4` directories. Every SDK translation unit force-includes a header requiring `DETOURS_VERSION == 0x4c0c1`; bridge compilation has the same check. Detours' own sources retain their original internal-header initialization order.
4. The SDK DLL links the absolute, configuration-specific `sporemp_detours.lib`. The bridge and HOST tests import that same library. The injector builds the same pinned Detours sources through its existing project.
5. `verify-sdk-detours.py` checks MSVC compiler and linker read logs for the expected 4.0.1 header/library and rejects old or ambiguous Detours paths. It records hashes and source pins in `build/sdk/<Configuration>/detours-provenance.json`. An always-run prerequisite validates the manifest for bridge and native-host builds, including targeted/incremental builds. Native-host identity generation also checks it and rejects missing/stale SDK output.

The old Detours directory remains in the downloaded, ignored upstream Git checkout for source integrity. It is not compiled, linked or copied into a SporeMP distribution by this pipeline. Do not substitute prebuilt SDK binaries, invoke the upstream SDK project directly, or distribute its entire checkout as a project package.

## Verification

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release -FetchDependencies
ctest --test-dir build/win32 -C Release --output-on-failure -V
python tools/build/verify-sdk-detours.py --repo . --configuration Release --check
```

The `sdk_detours_host` test uses the actual pinned SDK `CppRevEng.h` wrappers with compiled HOST functions. It verifies static and member attachment, original trampolines, signed/float arguments, return values, receiver isolation and restoration after detach. It does not load SPORE or SporeModAPI.dll. Other existing HOST Detours/ABI tests retain their own scope.

The provenance verifier's synthetic tests check missing inputs, old paths, incorrect libraries and stale artifacts. They do not establish native compatibility. Reviewed commands, outcomes, hashes and remaining acceptance are recorded in [the migration session](../evidence/2026-09-21-detours4-license/SESSION.md).

## Native qualification boundary

This changes the SDK DLL's hook implementation. Earlier M01-M06 native evidence refers to the historical SDK payload; it does not qualify this rebuilt SDK. Original-game startup, SDK initialization/disposal, the recorded gameplay fixture and clean shutdown with the exact new payload are **NOT RUN** in this migration session. They require the normal native-test authorization, compatible game/content, protected saves and verified isolated profile gates. A build or HOST test does not satisfy that gate.

Concurrent M07 work and its frozen payloads remain independent. The public checkout contains the reviewed M06 source snapshot plus this migration; it does not incorporate private M07 work. The original M00-M20 acceptance plan is unchanged.
