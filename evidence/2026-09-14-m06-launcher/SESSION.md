# M06 launcher 0.1.9 — HOST/FIXTURE session

Date: 2026-09-14. Windows 10.0.26200 X64; AMD Ryzen 7 9700X. SDK checkout commit `cbf9206b9a823f0911cd9be0217104a49d72380b`. Candidate manifest SHA-256 `06e58e2c169ff380e1fcea2ec519ef2b65653f87f82999ac644d004fdfdf6b3b`; pinned executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. Original-game execution **NOT RUN by this subtask**. No game binary, save or invitation credential is included in this evidence.

Implemented Home Join/Rejoin, masked session-only invitation, strict validation, stdin secret transfer, restricted temporary native config, real coordinator preflight, guarded native launch integration and native-baseline-driven connection states. Retained existing Play, display and worker lifetimes; added prepared developer profile 03 enumeration for the authorized three-process native matrix. Version, newest release notes and CHANGELOG align at 0.1.9.

## Executed commands and results

All commands ran from the repository root in PowerShell. These checks start only explicit test/fixture programs.

| Exact command | Exit | Expected and observed |
|---|---:|---|
| `dotnet build tests/launcher/SporeMP.Launcher.Tests.csproj -c Release -o build/launcher-tests/m06 --nologo -m:1` | 0 | Build passes; zero warnings/errors. |
| `& build/launcher-tests/m06/SporeMP.Launcher.Tests.exe build/launcher/Release/launcher.runtime.json` | 0 | 54 launcher HOST assertions pass. Includes real child stdin transport, duplicate suppression, connected state, same-invitation Rejoin and existing worker/display lifetime tests. |
| `python -m unittest discover -s tests/unit -p test_multiplayer_launcher.py -v` | 0 | Eight tests pass: strict malformed invite rejection, actual owner-only Windows DACL, cleanup after failure/lifetime, rejected/timed-out handshake prevents game start, absent fixture prevents network/native launch, exact fixture digest in config, and connected requires a native baseline marker. |
| `python -m unittest discover -s tests/unit -p test_worker_manager.py -v` | 0 | 13 tests pass, including prepared profile 03 and retained generation/ownership checks. |
| `python -m unittest discover -s tests/unit -p test_launcher_service.py -v` | 0 | Nine existing service tests pass. |
| `python -m unittest discover -s tests/unit -p test_launcher_automation.py -v` | 0 | 14 existing discovery/preparation tests pass. |
| `dotnet build tools/launcher/preview-m06/Preview.csproj -c Release -o build/launcher-preview/m06 --nologo -m:1` | 0 | Actual WPF preview builds with zero warnings/errors. |
| `& build/launcher-preview/m06/Preview.exe . build/launcher-preview/m06-review-01` | 0 | Actual offscreen WPF Join/Rejoin/close lifecycle passes, zero binding errors. No Show, native launch or desktop input. |
| `& build/launcher-preview/m06/Preview.exe . build/launcher-preview/m06-review-02` | 0 | Same actual controls pass after availability guidance was added. |
| `& build/launcher-preview/m06/Preview.exe . build/launcher-preview/m06-review-03` | 0 | Final actual controls pass after sidebar/readiness strip and primary label were aligned with active multiplayer state. |
| `dotnet build src/launcher/SporeMP.Launcher.csproj -c Release -o build/launcher/Release --nologo -m:1` | 0 | Current Release launcher builds with zero warnings/errors. |
| `git diff --check -- src/launcher tools/launcher tests/launcher tests/unit/test_multiplayer_launcher.py tests/unit/test_worker_manager.py CHANGELOG.md` | 0 | No whitespace errors. |

The first DACL assertion failed because Windows PowerShell could not auto-load its security module from this host environment; the native file was readable and cleanup still occurred. The test now uses installed `pwsh` with a literal `-File` argument array, and verifies the actual returned SDDL contains protected `D:P(A;;FA;;;OW)`. This was a test environment failure, not a native/network acceptance result.

## Visual inspection

Inspected [Home at 1280×820 (archived)](../../docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-14-m06-launcher/home-1280.png") and [Join at 1060×700 (archived)](../../docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-14-m06-launcher/join-1060.png"): original artwork and prominent Play stay intact; Join is a separate green card; the endpoint is legible, invitation text is masked, status/hints fit, and release cards remain accessible by scrolling. The password field clips its masked characters within the input as intended. Subsequent final [Connected (archived)](../../docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-14-m06-launcher/final-connected-1060.png") was inspected after aligning the sidebar/readiness strip and primary button with the active session. [Final Home (archived)](../../docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-14-m06-launcher/final-home-1280.png"), [final Join (archived)](../../docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-14-m06-launcher/final-join-1060.png") and [final Rejoin (archived)](../../docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-14-m06-launcher/final-rejoin-1060.png") are retained from the same passing actual WPF fixture runner. Fixture protocol outputs are explicitly not original-game evidence. The earlier preview/provenance is preserved separately from the final source freeze in `launcher-provenance-final.json`.

## Integration contract and next step

See [launcher design](../../docs/m06-launcher.md). Parent M06 work owns original NativeHost/bridge readiness, consistent fixture loading, actual pinned TLS server/worker/two-client sessions and reconnect acceptance. The launcher requires real probe `authenticated:true` and native applied-baseline status before its respective UI states. It does not claim successful native multiplayer because its own fixture suite passed.
