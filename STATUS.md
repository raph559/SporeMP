# Project status

Updated: 2026-09-08. **Partial/experimental: no multiplayer gameplay implemented.**

| Milestone | Status | Evidence boundary |
|---|---|---|
| M00 | VERIFIED | Project contract, full M00–M20 acceptance plan and initial architecture/coverage inventory. |
| M01 | VERIFIED | Pinned build and guarded loader; three clean original-game bridge lifecycles; mismatch rejection and native path evidence; simplified player launcher confirmed working by the user. |
| M02–M19 | TODO | Required scope retained in MILESTONES.md. |
| M20 | TODO | Optional native Galactic Adventures multiplayer. |

M01 is complete under the user's recorded launcher correction. On 2026-09-08 the user confirmed “it works” and requested no further repeated tests. Completion uses existing evidence and that acceptance. No further native game launch or repeated M01 test followed the confirmation.

## Current player flow

Launcher 0.1.2 redesigns Home around Play and the artwork, adds permanent navigation, searchable release history and simpler Settings. A compiled build is staged at build/launcher/0.1.2/SporeMP.exe while the previous launcher is left running. The redesign is IMPLEMENTED_NOT_RUN: the Release build passes, but the new window has not been opened or visually checked because the user is playing and explicitly requested no desktop control. The accepted M01 game tests were not repeated. Evidence: evidence/2026-09-08-launcher-redesign/SESSION.md. Git records the project and launcher updates locally.

Open build/launcher/Release/SporeMP.exe and select **Play SPORE**. The launcher detects the installed game, checks compatibility automatically and uses the player's normal Windows account and existing saves. Separate accounts, profile setup and backup checks are not player prerequisites. Details stay in Settings. Optional historical isolation/backup tools remain developer-only.

## Completed evidence

- Three native cycles with the current Release bridge: lifecycle-01 (PID 34720), lifecycle-02 (33932) and gui-aa6e8891815042a6 (35772). Each recorded exactly one initialization and disposal callback on the same engine thread, matching fingerprints and normal game exit 0. These runs used the earlier isolated launch mode.
- Six actual native-host rejection fixtures exited 20 before creating a game: changed executable, incompatible content set, changed injector, changed SDK core, changed bridge, and wrong account for the isolated-only mode. The account restriction does not apply to normal Play.
- Native Resource paths and ETW accesses were captured. All 29 personal save/creation files matched their backups after the earlier isolated probes. Some ETW correlations were unresolved, and shared NVIDIA driver writes occurred. Normal Play uses and updates normal game saves.
- Earlier Release/Debug suites and a clean source rebuild passed. After the player-flow correction, Release and Debug builds succeeded, 38 Python tests passed, and Debug CTest passed 3/3 targets with 11 C++ and 14 launcher assertions in addition to the Python suite.
- The final normal-account launcher was confirmed working by the user. The assistant's attempted UI check was stopped with Escape. User acceptance completes this flow; it is not represented as another assistant-captured trace. The earlier clean export predates the final player-flow change.

Evidence: [session](evidence/2026-09-08-m01-native/SESSION.md), [native lifecycles](evidence/2026-09-08-m01-native/native-lifecycles.json), [guard results](evidence/2026-09-08-m01-native/guard-results.json), [completion](evidence/2026-09-08-m01-native/completion.json).

## Scope and next milestone

Qualification applies to the recorded local GOG GA 3.1.0.29 Win32 executable/content configuration. Executable SHA-256: dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37. SDK: cbf9206b9a823f0911cd9be0217104a49d72380b. Injector source: 26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9. Other builds and official clean-content equivalence remain unqualified.

**Next: M02 — native-behavior baseline and engine observability.** Native Debug execution, native save restoration, simultaneous workers, networking, persistence, all five campaign stages and distribution packaging retain their original milestone requirements. The adopted brief and full M00–M20 acceptance text remain intact.
