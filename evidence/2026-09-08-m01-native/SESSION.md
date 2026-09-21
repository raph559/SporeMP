# M01 completion — 2026-09-08

**M01 VERIFIED for the recorded local configuration and amended player flow. Evidence: NATIVE, BUILD, HOST/FIXTURE and user acceptance.** The user confirmed the simplified launcher worked and requested no further repeated tests. No new game run followed.

The complete session, commands, traces, inventories and original hashes remain in the [private historical archive](../../docs/public-evidence.md#historical-artifacts) at `evidence/2026-09-08-m01-native/`. This condensation does not expand acceptance.

## Product and loader boundary

Normal Play uses the existing account and saves; compatibility checks are automatic and diagnostics stay in Settings. The normal-account route was confirmed by the user, not a fresh assistant native trace. An assistant UI check stopped on physical Escape. The overall confirmation is not evidence for every window action.

The host checks content/payload hashes before process creation, uses the pinned suspended-process injector bootstrap and verifies actual mappings before resume. SporeMP DllMain only registers callbacks; hashing/diagnostics/native reads occur later. The pinned SDK/injector perform audited hook work under loader lock, a retained dependency limitation.

## Native results

| Run | Observed |
|---|---|
| native-attempt-01 | Remote KERNEL32.DLL bootstrap lookup failed; owned suspended process terminated, exit 32. |
| native-attempt-02 | Earlier bridge initialized/disposed, exit 0; excluded from final three-run set. |
| lifecycle-01, lifecycle-02, gui-aa6e8891815042a6 | Current bridge initialized/disposed once on the same engine thread; actual galaxy menu and normal UI quit; game/wrapper exit 0. |

The three captured runs used the earlier isolated developer flow. SDK resource paths placed saves/creations/temp data under its real OS profile. ETW found no resolved personal-profile opens/completed writes, but unresolved correlations and shared NVIDIA ProgramData writes remain. One early capture had unrelated loss markers; later captures reported zero loss. This is bounded path evidence, not a universal sandbox.

All 29 protected personal files (23,886,102 bytes) retained hashes. This describes the probes, not later normal player saves. Six deliberate guard failures returned exit 20 with zero game creations.

## Checks, failures and limits

Pre-correction Release/Debug builds and 3/3 HOST/FIXTURE suites passed. An independent clean export fetched both dependencies, built and passed 3/3. After the player-flow correction, Release/Debug builds passed, Python passed 38 tests and Debug CTest passed 3/3. Archived records retain literal commands; current build commands are in the [testing guide](../../docs/testing.md).

Earlier missing SDK-base linkage, extractor include errors, a locked launcher and account runtime/policy failures were corrected. The evidence collector failed its byte comparison on line endings; a later normalized comparison found no pre-correction content differences. Neither establishes identical binaries.

GOG GA 3.1.0.29 Win32 executable SHA-256: `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`; 108 content files. SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, injector `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. Windows 11 build 26200, Ryzen 7 9700X/RTX 4080 SUPER; MSVC 14.44.35207, Windows SDK 10.0.26100.0, .NET SDK 8.0.418.

Gameplay observation, unattended workers, multi-process isolation, native restoration, networking and release packaging remained later milestones. Next was M02, without reopening accepted M01 runs.
