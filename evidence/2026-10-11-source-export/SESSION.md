# M09 to M11 reviewed source export

Date: **2026-10-11**. Classes: **BUILD, HOST/FIXTURE, PUBLICATION**. Native SPORE: **NOT RUN** in this session.

The public source now holds the development source through bridge/NativeHost 0.0.256: M09 persistence, M10
locations and transfer, the M11 Creature work with gates C01, C03 and C02 closed, the native trial runner with its
scenarios, the C++ cleanup of 2026-10-10 and the source of the current presentation website. README, STATUS, the
gate map and the evidence index agree: **M00 to M10 VERIFIED for their recorded fixtures; M11 IN_PROGRESS at 3/12
gates; the campaign is unfinished.** This is a source publication, not a multiplayer release.

## Export and qualification boundary

Changes were reapplied to public base `3c465a822cd98eb77b47914ceb1e5a1d0593c40c` from development revision
`40dacbf`. No private Git history was merged, pushed or rewritten. Kept as they were: the GPL-3.0-or-later license,
the MIT Detours 4 SDK migration with its provenance guard and SDK wrapper test, the curated historical reports and
the evidence policy. Three files needed both sides by hand: `CMakeLists.txt` (imported Detours 4 library, forced
guard header, wrapper test; the bridge no longer sees the SDK's old Detours header), `tools/build/build.ps1` (the
development side's verified SDK source copy with its one pinned patch, built through this checkout's Detours 4
targets) and `docs/testing.md`.

The native results recorded for M07 through M11 were obtained with the development build, whose SDK DLL is the
upstream build. **Original-game execution with the SDK built by this checkout is NOT RUN.** Its build and HOST tests
do not qualify it for native gameplay. Hashes and source revisions in the reports identify the private original
runs; they do not assert byte identity with a public rebuild.

## Validation

Commands run from the reviewed public checkout, Windows 11 Pro 10.0.26200, MSVC 14.44.35207, Windows SDK
10.0.26100.0, SDK commit `cbf9206b9a823f0911cd9be0217104a49d72380b`, Detours from Launcher Kit commit
`26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`:

| Command | Expected | Observed |
|---|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | Build with the Detours 4 SDK | First run exit 1: the bridge's include path still named the SDK's old Detours header and the guard refused it. After removing that path, exit 0; SDK provenance check PASS |
| `ctest --test-dir build/win32 -C Release` | All HOST/FIXTURE targets pass | **21/21 pass**, 68.43 s, `sdk_detours_host` included |
| `python -m unittest discover -s tests/unit -p 'test_*.py'` | Tooling tests pass | 502 tests OK, 1 skipped (the replay suite needs private trial recordings); in a fresh clone of the commit, 502 OK with 2 skipped |
| `node website/build.mjs`, `node website/check.mjs` | Site builds, links resolve | Exit 0: 30 HTML pages, 632 local links, anchors and assets |
| `python -X utf8 tools/check-public-evidence.py` | Allowlist, types and sizes hold | Exit 0 on the staged tree |

Release artifacts of this build, SHA-256: `SporeMP.Bridge.dll`
`daab218b073bab43574b39dedaae3fbe931a7bdde95890a06e10e0c86a677f31`, `SporeModAPI.dll`
`2d94b9e6509c37c8684aa3dc4eeaa8e65a327ddc2ca49e5435af5415b318b8db`, `SporeMP.NativeHost.exe`
`f9ccfd7c60f0e893ed157930cda6cbd74bef40f1d34a023dc3a4b3a86a8ea1a3`. They are not distributed. No build or
automated test launches SPORE. Debug configuration: NOT RUN in this session.

## Privacy and content review

Every exported file passed the publication's redaction rules (user name, computer name, account identifiers,
author email) and was then scanned for email addresses, network addresses, user-profile paths, account
identifiers, host names, hardware addresses, credential assignments and token-like strings. Findings: loopback
addresses, the game version, generic install paths, the disposable worker account names already named in earlier
reports, and SHA-256 values. Local Markdown links were audited; 95 links to privately archived M09/M10/M11
records now point to the archive policy and say so.

Published evidence: 65 reports, 0.4 MiB. The M11 gate sessions and the trial runner's own `RECORD-trialNNN.md`
files they cite, the M09 acceptance report, the M10 native recovery and paired-save reports, and the website
rebuild session. Withheld, kept in the private archive: every frame, capture, trace, save and checkpoint; the
remaining M09/M10 working sessions and the 0.4 MiB M11 social session; account and ACL inventories.

The website images are concept artwork with their provenance in `website/assets/credits.txt`; they are not
gameplay evidence. Publishing the website source here deploys nothing: the site is served from its own repository.

## Known limits carried by this export

- `held-controls.py`, C01's strictest scenario, no longer passes as written: a creature at rest moved 0.006 units
  where the scenario allows 0.001 (trial485). Cause unknown; recorded in STATUS.
- The bridge sources that no gate scenario reaches today were restructured in the cleanup and are verified by
  compilation and HOST tests only.
- The files `native_actors.cpp`, `native_scene.cpp` and `native_network.cpp` are still 3300 to 4900 lines each.

## Next step

M11 gate C06, separate species. For this checkout: one original-game start with the SDK it builds, under the
normal native-test preconditions, to qualify the Detours 4 payload.
