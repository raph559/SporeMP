# M03 context investigation — 2026-09-12

**Historical result: M03 BLOCKED. Evidence: three original-game investigations, STATIC analysis and HOST/FIXTURE checks.** Bridge 0.0.6 located NPC attack cancellation but did not solve reciprocal B combat or independent rewards. After the user's request to reduce repeated testing, work moved to reverse engineering with no further game run in this increment.

Five guarded observers and attack correlation preserved original calls/returns. Shared ABI types, executable Detours fixtures and analysis tooling were added. Final Release build and six HOST/FIXTURE CTest targets passed, including ten ABI assertions and 67 Python tests.

The complete session, literal commands, original traces/media and source/artifact hashes remain in the [private historical archive](../../docs/public-evidence.md#historical-artifacts) at `evidence/2026-09-12-m03-context/`. This condensation adds no test or acceptance.

## Native findings

| Run | Observed |
|---|---|
| probe-01 / 0.0.4 | A/NPC reciprocal damage and an allied-assisted global 8.75 award. B killed another NPC with six hits; five accepted NPC selections produced no effect dispatch against B. |
| probe-02 / 0.0.5 | B killed another NPC, no DNA. Ten accepted/seven rejected NPC selections produced no B damage. |
| probe-03 / 0.0.6 | Seven accepted NPC attacks were cleared at the next original AI return after 15.4362–16.6991 ms. Later B hits on A did not establish a kill; A's later death was starvation. Held intention expired after 30.0866515 seconds. |

All games/wrappers exited 0, with healthy same-thread lifecycle and zero foreign callbacks. All 29 personal hashes and disposable Games/creation contents were unchanged. Stop attempts were late: active Stop and active-intention scene exit remained NOT RUN.

The 300/300/180-second recordings were inspected, but encounters were sometimes offscreen and later probe03 events occurred after its recording. No capture cadence was treated as native timing. Version 0.0.4 retained loaded-artifact hashes but lacked a complete pre-run source snapshot.

## Static conclusions, failures and next step

Static analysis mapped the observed reset behavior and native attack-stop predicate, corrected a misleading decompiler receiver interpretation, and separated target stealth/death checks from global mode. Native constructor spatial enablement differed from harness herd state. DNA processing also touched native player, goal, display and deferred strategy state; a separate scalar balance would not supply independent progression.

An undeclared _countof build error was fixed using std::size. The first Ghidra export failed after a bounded initial analysis; a corrected script produced selected exports. Guessed paths, a fixture report-name collision and partial static analysis were retained, not presented as runtime acceptance.

Pins remained SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, loader `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`, and GOG GA 3.1.0.29 executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37` on Windows 11 build 26200/Ryzen 7 9700X/RTX 4080 SUPER. Final bridge SHA-256: `ca86979931c155beff3bb38a7ce0214391bad87392947bb74651e78e83a7f361`.

The next step was the original decision/eligibility branch and owner-scoped native reward context. Exact ABI and durable research findings remain in [M03 reverse engineering](../../docs/m03-reverse-engineering.md); raw decompilation remains private.
