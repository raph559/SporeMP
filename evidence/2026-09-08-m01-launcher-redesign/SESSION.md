# M01 launcher redesign and automatic preparation — 2026-09-08

The user rejected the first checklist launcher and requested a better visual design with as much automatic setup as possible, including finding the game. This increment implements and executes that launcher refinement. **M01 remains IN_PROGRESS. Original-game execution is NOT RUN.**

## Delivered behavior

- Native WPF app: `build/launcher/Release/SporeMP.exe`, 1100 × 720 default, 940 × 640 minimum. The main screen uses original alien-world artwork, compact actual preparation status, Settings, and a disabled multiplayer action with a visible development-build explanation.
- Preparation starts automatically on opening. Bounded registry, Steam library/manifest and common-folder discovery finds installation candidates without a whole-drive recursive scan. The selected installation is remembered; manual selection is a fallback for missing or ambiguous results.
- Executable and content fingerprints are checked before setup continues. Existing Windows shell-folder saves are copied and hash-verified. An unchanged verified backup and existing working folders are reused; changed saves get another backup. Interrupted or damaged copies are retained and replaced. A cross-process preparation lock excludes concurrent writes.
- Settings contains installation override, preparation results, the native blocker explanation, Check again, and local report export. The export contains `result.json` and `README.txt` only. No saves, game assets or uploads are included.
- Preparation uses a cancellable child process with literal arguments, streamed progress and validated final reports. Closing the launcher terminates only its owned preparation child.

The launcher is a development-checkout artifact: its generated runtime configuration points to this source checkout and the locally installed Python runtime. M19 still owns standalone end-user packaging. No game DLL was installed, no Windows user account was created, and no game was started.

## Commands and results

Working directory: `C:\Users\Developer\Documents\ChatGPT\SporeMP`. PowerShell was used. Build tooling prints its exact compiler/dependency commands in the linked logs. Exit 22 below is the expected native prerequisite denial after successful preparation, not a successful native launch.

| Command / executed operation | Expected | Observed and evidence |
|---|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | Build pinned SDK, bridge and launcher | Exit 0; [full-build-release.log](full-build-release.log) |
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Debug` | Build Debug artifacts | Exit 0; [full-build-debug.log](full-build-debug.log) |
| `python -m unittest discover -s tests/unit -p 'test_*.py' -v` | Pass discovery/preparation/diagnostic tests | Exit 0, 35 tests; [tests-python.log](tests-python.log) |
| `ctest --test-dir build/win32 -C Release --output-on-failure -V` | Pass three executable host/fixture targets | Exit 0, 3/3; [tests-release-final.log](tests-release-final.log) |
| `ctest --test-dir build/win32 -C Debug --output-on-failure -V` | Same coverage in Debug | Exit 0, 3/3; [tests-debug.log](tests-debug.log) |
| `pwsh -NoProfile -File tools/build/build-launcher.ps1 -Configuration Release` | Build final text/scrollbar refinements | Exit 0, no warnings/errors; [build-release-ui-final.log](build-release-ui-final.log) |
| `pwsh -NoProfile -File tools/build/test-clean-build.ps1 -Destination local/clean-room-launcher-redesign` | Fetch pinned dependencies and build/test in empty export | Build/test exits 0; [clean-build-result.json](clean-build-result.json). Superseded by the final source export below. |
| `pwsh -NoProfile -File tools/build/test-clean-build.ps1 -Destination local/clean-room-launcher-final` | Independently build/test all final sources | Build/test exits 0, 3/3 targets; [clean-build-final-driver.log](clean-build-final-driver.log), [clean-build-final-result.json](clean-build-final-result.json) |
| `python tools/launcher/launcher_service.py prepare --progress` | Discover without a supplied path, verify, back up, prepare folders; keep native launch denied | Exit 22, four progress phases, `development_build`, zero game processes; [automatic-first-run.jsonl](automatic-first-run.jsonl) |
| Missing explicit selected-installation host probe | Reject the missing selection instead of silently selecting another installation | Exit 23, `game_not_found`; [missing-selected-installation.json](missing-selected-installation.json) |
| `python evidence/2026-09-08-m01-launcher-redesign/collect-final-evidence.py` | Compare final source, preserve plan/brief, verify backups and original files | Exit 0; 52 source files match tested tree, all 21 milestone bodies preserved, brief preserved, original saves unchanged; [final-verification.json](final-verification.json) |

The three CTest targets contain **35 Python tests, 11 C++ host assertions and 10 C# launcher host assertions**. Launcher tests include an actual synthetic Python child for streamed progress, malformed-output cleanup and cancellation. Release and Debug passed before final cosmetic refinements; the final Release app and independent clean build passed after those refinements. There were no subsequent source changes.

Discovery fixtures cover secondary Steam libraries, paths with spaces, malformed metadata, duplicate candidates, missing/ambiguous selections and safe path handling. Preparation fixtures cover cold/warm runs, changed or damaged backups, game-running denial and concurrent preparation. **These fixtures do not establish native Steam/EA/GOG compatibility.** The actual installed GOG file inventory was tested separately.

## Actual machine and desktop observations

Opening the actual EXE without entering a path found `C:\Games\SPORE`. Provenance included 32-bit EA `InstallLoc`/`DataDir`, GOG game `1948823323`, its uninstall entry and the bounded common-folder check. Results were deduplicated to one installation. The Galactic Adventures runtime executable and content matched the recorded development candidate.

Automatic setup copied and verified **29 save/creation files, 23,886,102 bytes**, into `local/backups/automatic-af7a3be8bcf46be3341e5adb25dbefea`. Subsequent app openings and Settings → Check again reverified and reused that same backup and `local/profiles/launcher-client`. The verification snapshot at 12:24 UTC records five successful preparation runs plus one missing-selection probe, exactly one automatic backup, and zero launched games. A final reopen at 12:28 UTC also returned exit 22 with that same backup/workspace reused; its report is [final-ui-preparation.json](final-ui-preparation.json).

The two personal source folders matched the original `local/backups/m01-before-native/backup-manifest.json`: 26 files in `%APPDATA%\Spore` and three files in `Documents\My Spore Creations`, unchanged. Backup file integrity is verified; a native restoration is **NOT RUN**.

Actual Windows UI actions performed with the Computer Use skill:

1. Opened the built EXE and observed automatic preparation and final installation/backup status.
2. Opened Settings and checked detected path and real preparation details.
3. Used Export report; observed the saved local ZIP and verified its two-entry allowlist.
4. Used Check again and observed preparation finish with the existing backup/workspace reused.
5. Checked the default and minimum window sizes, opened Settings at minimum size and scrolled to the bottom actions. No action control was inaccessible; the Settings panel intentionally scrolls.
6. Verified Play multiplayer remained disabled. Closing and reopening returned to the main screen with automatic setup complete.

During final presentation, a Computer Use drag from the minimum-size window toward a point outside that window's bounds was rejected before input. The launcher was closed normally with Alt+F4 and reopened at its default size instead. The final app was left open on its main screen, 1100 × 720, window title `SporeMP`.

- [Final default-size app screenshot](launcher-final-default.png)
- [Final minimum-size app screenshot](launcher-minimum-final.png)
- [Final Settings screenshot, minimum size and scrolled](launcher-settings-final.png)

The screenshots show the running app. The decorative background is original generated artwork, not a gameplay screenshot. Asset, generation prompt and provenance: `src/launcher/Assets/universe.png` and `src/launcher/Assets/README.md`. Its SHA-256 is `5397f78275521e8bb8f7f4344c79e58bf6d16cdb36b0a1f66a3c5de6e625b184`.

## Source, environment and artifact identity

[environment.json](environment.json) records Windows 11 Pro 10.0.26200 x64, AMD Ryzen 7 9700X (8 cores/16 logical processors), 33,407,430,656 bytes RAM, RTX 4080 SUPER driver 32.0.15.9649 plus the other enumerated adapters. Toolchain: VS 2022 Build Tools 17.14.37216.2, Win32/v143, Windows SDK 10.0.26100.0, CMake 4.3.2, Python 3.11.15, PowerShell 7.6.5. Launcher: pinned .NET SDK 8.0.418 and .NET/WindowsDesktop 8.0.24; external NuGet sources are disabled.

- ModAPI SDK commit: `cbf9206b9a823f0911cd9be0217104a49d72380b`.
- Inspected upstream loader source commit: `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`; not installed or runtime-qualified.
- Final immutable source tree: `d88978ae6f04a2a015bcaa61035ed2869e68b145`.
- Clean source archive SHA-256: `4acaa0b829121da95e4b46369d206b45588c909a61e4d528e85d0bdb182c82f1`.
- Compatibility inventory SHA-256: `06e58e2c169ff380e1fcea2ec519ef2b65653f87f82999ac644d004fdfdf6b3b`.
- Selected original executable: `C:\Games\SPORE\SporebinEP1\SporeApp.exe`, 3.1.0.29, x86 PE32, 24,895,536 bytes; SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.

Working build SHA-256 values:

| Artifact | SHA-256 |
|---|---|
| `build/launcher/Release/SporeMP.exe` | `7f6fb99ecd4fd8e317b8223d33091acaa8dedb5cbec17141f02f019e38a8efc3` |
| `build/launcher/Release/SporeMP.dll` | `491ca9769e45fa3332f6a757d9a76e27447e3696ea337bff99e9080ff5e9844d` |
| `build/win32/Release/SporeMP.Bridge.dll` | `ae36c9f59e2960938b944028e0790b7c5ab8d94994229044ba210d2ce2317af5` |
| `build/sdk/Release/SporeModAPI.dll` | `c018b51c56c537e739f24319dcfeb5cfeeefa97ded0ca91bf5729755c190ba11` |

The clean export's binary hashes are recorded separately in [clean-build-final-result.json](clean-build-final-result.json). Rebuild reproducibility is demonstrated; byte-identical binary reproducibility is not claimed. [final-verification.json](final-verification.json) lists every source hash and confirms all 52 current source files match the independently tested tree. The original user-adopted brief and all M00–M20 acceptance-section bodies are preserved against the initial source snapshot `c918d7e09beefb92f275a4eed51c6e9eed56eb17`.

## Remaining native gate and next smallest step

**NOT RUN:** DLL loading in original SPORE, native initialization/disposal callbacks, three clean native starts/exits, native file-path confinement, native restoration, dedicated workers, network play or multiplayer gameplay. Prepared working directories do not constitute verified Windows-profile or native-path isolation.

The host reproduction remains: matching executable/content inventory produces `MODAPI_LAUNCHER_NOT_CONFIGURED`, `DISPOSABLE_OS_OR_NATIVE_PATH_ISOLATION_NOT_CONFIGURED` and `NATIVE_LIFECYCLE_QUALIFICATION_NOT_RUN`; expected and actual behavior is denial with zero game launches. Earlier shell-folder isolation experiments and the missing capability are preserved in `docs/research-log.md` and the initial M00/M01 evidence.

Next experiment: qualify a pinned loader in a verified disposable OS profile/VM or a measured early native path adapter, then capture original-game file accesses and three normal initialization/disposal cycles with pre-loader mismatch rejection. `App::cAppSystem::SetUserDirNames` and `Resource::Paths` remain source-level investigation candidates, not proven isolation bindings. Follow `tests/engine/M01.md`; keep M01 IN_PROGRESS until native acceptance passes.
