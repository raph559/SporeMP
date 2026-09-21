# Detours 4 migration and GPL source licensing

Date: 2026-09-21. Evidence classes: **BUILD, HOST, FIXTURE**. Original-game execution: **NOT RUN**.

## Scope and source

The user requested removal of the non-commercial Detours 3 dependency and adoption of GPL-3.0-or-later. Work was performed in the independent reviewed public checkout based on `7822e2e2ef21b586f119f7372953ec30902382a1`. The private development checkout, concurrent M07 source, live build outputs and frozen native payloads were not modified by this migration.

The SDK remains pinned to `cbf9206b9a823f0911cd9be0217104a49d72380b`; the MIT Detours 4.0.1 source comes from Launcher Kit `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. Both fetched dependency checkouts remained free of tracked modifications. A late MSBuild overlay replaces SDK header/library paths; the bridge and SDK share one freshly compiled, configuration-specific Detours 4 static library. The pinned injector retains its own build of those MIT sources. No dependency binary is published.

The five Detours APIs called by the SDK have matching declarations/calling conventions in the pinned 4.0.1 header. The HOST wrapper test uses the actual SDK header, static/member detour wrappers and commit routine; it covers arguments, return values, original-call count, receiver isolation and detach restoration. This is not a test of SDK DllMain inside SPORE.

## Commands and observations

All commands below ran from the public checkout. Full unredacted command logs remain under ignored `local/detours4-migration/`; only relative paths, hashes and reviewed results are published.

| Command | Expected | Observed exit/result |
|---|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release -FetchDependencies` | Fetch exact pins and compile | Initial exit 1: a forced header included Detours before its internal Windows/architecture setup. Guard removed from Detours' own source build; SDK/bridge guard initializes Windows first. |
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release -FetchDependencies` | Compile SDK and link the new library | Second exit 1: MSBuild property-function escaping combined the library list into one filename. Overlay now explicitly unescapes the returned list. |
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | Build SDK/base, injector, bridge, tools and launcher | Exit 0. Detours 4 input provenance PASS. Nothing installed or launched. |
| `python tools/build/verify-sdk-detours.py --repo . --configuration Release` | Record final source/artifact hashes after verifier refinement | Exit 0, compiler and linker input checks PASS. |
| `cmake --build build/win32 --config Release --parallel 4` | Build with the always-run provenance prerequisite | Exit 0. |
| `ctest --test-dir build/win32 -C Release --output-on-failure -V` | Pass all HOST/FIXTURE targets | Exit 0, **10/10**, 11.93 seconds, including **122 Python tests** and the actual SDK wrapper fixture. |
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Debug -SdkOnly` | Build Debug MIT Detours and both SDK targets | Exit 0, Debug input provenance PASS. |
| `cmake --build build/win32 --config Debug --target sporemp_sdk_detours_tests --parallel 4` | Build actual SDK wrapper fixture against Debug library | Exit 0. |
| `ctest --test-dir build/win32 -C Debug -R '^sdk_detours_host$' --output-on-failure -V` | Verify Debug wrapper behavior | Exit 0, **1/1**, 0.02 seconds. |
| `cmake --build build/win32 --config Release --target SporeMP.Bridge` with an intentionally incorrect SDK hash in the local manifest | Reject stale artifacts even in a targeted incremental build | Expected exit 1, `SDK Detours provenance artifact hashes are stale; rebuild the SDK`. Original manifest bytes restored in `finally`. No SDK binary was changed. |
| `python tools/build/verify-sdk-detours.py --repo . --configuration Release --check` after restoration | Accept intact original artifact set | Exit 0, PASS. |
| `git diff --check` | No whitespace errors | Exit 0. |
| `git -C external/Spore-ModAPI status --porcelain --untracked-files=no` and equivalent Launcher Kit command | Unmodified upstream tracked sources | Both exit 0, empty output. |

Fresh `obj-detours4` trees prevent reuse of historical SDK objects. Actual UTF-16 `CL.read` logs for both SDK targets contain the pinned Detours 4 header. The SDK's `link.read` log contains the expected absolute `sporemp_detours.lib`. The verifier rejects the SDK's old Detours subtree, `E:\Detours`, and ambiguous `detours.lib` inputs. Stored artifact hashes are checked for bridge/native-host builds and native-host identity generation. The manifest is local build provenance, not a signed third-party attestation.

## Environment and identities

- Windows NT build 10.0.26200.0; AMD Ryzen 7 9700X; NVIDIA GeForce RTX 4080 SUPER. No game or GPU acceptance test was run.
- MSVC tool directory 14.44.35207 / compiler 19.44.35226.0, Windows SDK 10.0.26100.0, Win32, Release `/MD` and Debug `/MDd`.
- Game candidate executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, taken from the existing compatibility inventory; the game was not opened or requalified. Content inventory/source/artifact hashes appear in [verification.json](verification.json).
- Canonical GPL v3 text: 35,149 bytes, SHA-256 `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`. The separate project grant explicitly permits version 3 or any later version; dependency notices remain intact and artwork is not assigned the code license.

## Remaining acceptance

Original SPORE startup, SDK initialization/disposal, the existing native fixture and clean shutdown with these new payload hashes are **NOT RUN**. Earlier M01-M06 acceptance refers to the recorded historical SDK, not this rebuilt DLL. The next smallest native step is a bounded lifecycle/fixture regression under the normal authorization, executable/content and isolated-save gates. It must be coordinated with concurrent M07 work; this session does not change its milestones or runtime payloads. Full release packaging and all original M00-M20 requirements remain intact.
