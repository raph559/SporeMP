# M00/M01 implementation — 2026-09-08

**Historical result: M00 VERIFIED; M01 IN_PROGRESS. Evidence: BUILD, HOST, FIXTURE. Original-game execution: NOT RUN.** This initial implementation predates the later M01 native acceptance.

The complete session, literal commands, logs, inventories and hashes remain in the [private historical archive](../../docs/public-evidence.md#historical-artifacts) at `evidence/2026-09-08-m00-m01/`. This condensed report adds no test or acceptance.

## Implementation and checks

The empty checkout gained the preserved brief, complete M00–M20 plan, 75-row coverage matrix, architecture, authority policy, pinned tooling, actual ModAPI callback registration, compatibility checks and guarded backup/profile tools. Imported symbols did not establish callback execution.

Clean export source tree: `c918d7e09beefb92f275a4eed51c6e9eed56eb17`. SDK: `cbf9206b9a823f0911cd9be0217104a49d72380b`. Launcher Kit `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9` was inspected, not installed/qualified.

| Command / check | Expected and observed |
|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` and Debug equivalent | Final exit 0. |
| Release/Debug `ctest --test-dir build/win32 ... --output-on-failure -V` | Exit 0, 2/2 HOST/FIXTURE targets, 16 Python tests and 11 C++ assertions. |
| `pwsh -NoProfile -File tools/build/test-clean-build.ps1 -Destination local/clean-room-m01-final` | Exit 0, independent pinned fetch/build and same tests. |
| Candidate validation | Exit 0; inventory matched, launch remained denied. |
| Native preflight / deliberate wrong manifest | Expected exits 22 / 20; zero games launched. |
| Shell-folder isolation probe | Expected exit 21 after reporting correction; isolation not established. |

Different working/export bridge hashes mean byte-identical binaries were not established. Backups verified 29 personal files (23,886,102 bytes), and final personal source hashes were unchanged; native restore was NOT RUN.

## Failures, configuration and next step

CMake initially lacked imported SDK libraries for MinSizeRel/RelWithDebInfo; restricting it to built Debug/Release configurations fixed generation. The first isolation probe exited 2 before saving its child error. The corrected probe showed that APPDATA/LOCALAPPDATA overrides left native shell folders on the personal profile; adding USERPROFILE caused shell-folder API errors. This was a failed isolation experiment.

Candidate: GOG GA 3.1.0.29 Win32, executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. Windows 11 build 26200, Ryzen 7 9700X, RTX 4080 SUPER; MSVC 14.44.35207 and Windows SDK 10.0.26100.0.

Missing gate: qualified loader and measured native path isolation, then three clean callback lifecycles and pre-injection mismatch rejection. Gameplay, workers, networking, restoration and timing were NOT RUN. The next experiment was a disposable OS profile or measured native path adapter, retaining every required campaign stage.
