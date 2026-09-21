# M03 native actor commands — 2026-09-09

**Historical result: full M03 BLOCKED, with bounded native paths verified.** Eight original-game attempts introduced owner-checked scalar command queues, native actor creation/actions, lifetime fencing and original damage/resource observations. Builds and tests launched no game.

The complete session, literal commands, original traces/media and source/artifact hashes remain in the [private historical archive](../../docs/public-evidence.md#historical-artifacts) at `evidence/2026-09-09-m03-actors/`. This condensation adds no test or acceptance.

## Native attempts and corrections

| Attempt | Observed result |
|---|---|
| 01 | Null herd from initial factory setup caused a native AI access violation; wrapper exit 31. Corrected by native herd creation, without disabling AI. |
| 02 | Native herd was empty, setup reported create_failed; clean exit 0, no two-actor acceptance. |
| 03 | A/B jumps and B/NPC reciprocal damage executed, then an incomplete opponent archetype caused a native UI crash, exit 31. Corrected by complete native campaign factory context. |
| 04 | Distinct A/B movement, jumps, wrong-owner refusal and destroy/recreate fencing worked. A received a native assisted 8.75 campaign award. Distant B combat did not complete. Clean exit 0. |
| 05 | Two B hits, native scene reload and stale identity refusal worked. B's requested jump returned false while already airborne; the visible jump was not credited. A's later jump was accepted. Clean exit 0. |
| 06 | Caller tracing located target resets, but verbose diagnostics exhausted 32 MiB and omitted the healthy trace footer. Normal game exit 0 did not make the trace complete. NPC target sampling fixed diagnostic volume. |
| 07 | Held native approach expired after 30.116 seconds because the input threshold ignored creature footprints. No successful held attack. Clean exit 0; native range plus footprints corrected input selection. |
| 08 | Two original B six-hit kills completed; NPC ability attempts caused no B damage and DNA stayed 0. After reload, stale B was rejected; fresh B's accepted jump landed after 1.0027685 seconds and 70 AI ticks. Clean exit 0. |

Probe08 had 40,075 contiguous records, healthy detach and zero foreign callbacks. Held intentions canceled on target death. The intended Stop arrived after cancellation, so active Stop and active scene-exit cancellation remained NOT RUN. Zero-energy calls did not demonstrate nonzero resource spending. Native starvation/respawn of idle A was not credited to combat.

All eight personal postchecks retained 29 hashes. Disposable Games/creation content was unchanged; normal cache/event changes were retained. No fixture restore occurred between attempts.

## Checks and boundary

Final `cmake --build build/win32 --config Release --parallel 4` and focused/full recorded CTest runs exited 0; five HOST/FIXTURE targets included 15 actor assertions, 60 Python tests and 22 launcher assertions at build09. An initial missing generated-header include and wrapper argument binding were corrected before successful builds.

Final bridge SHA-256: `01cf9279cde15a4a9e920eed19290fd01d5da1ab669a366329f5eb4eff83c523`. SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`; loader `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`; GOG GA 3.1.0.29 executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. Windows 11 build 26200, Ryzen 7 9700X, Win32 MSVC 14.44.35207.

Actual contact sheets were inspected with offscreen/ambiguous coverage retained. Probe08's second kill appeared on video, but its post-reload B jump occurred after the recording limit and relied on native events/live observation.

Remaining reproduction: setup → B duel/engage produced a B kill, NPC attempts without B hits and no DNA. Next was native hit-eligibility and reward-context analysis, not another unchanged fight. Independent species/progression, active cancellation, nonzero costs, persistence and server-avatar/area behavior were unqualified. [Later M03 acceptance](../2026-09-12-m03-awards/SESSION.md) closes only its recorded fixture.
