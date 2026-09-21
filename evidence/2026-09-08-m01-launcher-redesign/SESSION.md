# M01 launcher redesign — 2026-09-08

**Historical result: M01 IN_PROGRESS. Evidence: BUILD, HOST, FIXTURE and inspected actual WPF UI. Original-game execution: NOT RUN.** This automatic-backup flow was later superseded by the [normal-account Play correction](../2026-09-08-m01-native/SESSION.md).

The complete original session, commands, screenshots and manifests remain in the [private historical archive](../../docs/public-evidence.md#historical-artifacts) at `evidence/2026-09-08-m01-launcher-redesign/`. This condensation adds no acceptance.

## Delivered behavior and actual observations

The WPF app gained original artwork, bounded automatic installation discovery, compatibility/preparation checks, remembered selection, reusable verified backups and Settings with refresh/report export. Preparation used a cancellable child and a cross-process lock. Multiplayer stayed disabled with a development-build explanation. Standalone packaging remained M19.

The actual app found the installed game without manual path entry, copied/verified 29 save/creation files (23,886,102 bytes), and reused the backup/workspace on later openings and Check again. Personal sources retained original hashes. Actual default/minimum windows and scrolled Settings were inspected with accessible controls. Export contained its two allowed report entries. No game, account creation or DLL installation occurred.

## Validation and boundaries

| Command / check | Observed |
|---|---|
| Release/Debug `tools/build/build.ps1` | Both exit 0. |
| `python -m unittest discover -s tests/unit -p 'test_*.py' -v` | Exit 0, 35 tests. |
| Release/Debug CTest | Both exit 0, 3/3 targets; 11 C++ and 10 C# host assertions alongside Python. |
| `tools/build/test-clean-build.ps1 -Destination local/clean-room-launcher-final` | Exit 0, independent pinned build and 3/3 tests after final refinements. |
| `python tools/launcher/launcher_service.py prepare --progress` | Expected exit 22: preparation completed, native prerequisites denied. |
| Missing explicit installation selection | Expected exit 23, game_not_found; no silent fallback. |

Fixtures covered malformed discovery data, secondary Steam libraries, spaces, ambiguity, changed/damaged backups, running-game refusal, concurrency, malformed progress and cancellation. They did not establish native store compatibility. An out-of-window UI drag was rejected before input; the app was reopened normally.

Final tested source tree `d88978ae6f04a2a015bcaa61035ed2869e68b145` matched 52 source files. SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`; inspected loader `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. GOG GA 3.1.0.29 executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`; Windows 11 build 26200, Ryzen 7 9700X/RTX 4080 SUPER, MSVC 14.44.35207, Windows SDK 10.0.26100.0 and .NET SDK 8.0.418.

Native callbacks, clean lifecycles, path confinement and restoration were NOT RUN. Prepared directories were not verified isolation. The next step was the guarded native loader/lifecycle experiment, not multiplayer acceptance.
