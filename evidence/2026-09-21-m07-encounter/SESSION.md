# M07 shared encounter implementation increment

Date: 2026-09-21 (Europe/Paris). Milestone **IN_PROGRESS**. The initial .31 implementation phase below covers implementation, retained-trace analysis, static executable inspection and BUILD/HOST/FIXTURE checks; that phase launched no original-game process or desktop input. The subsequently authorized native phase is recorded at the end and in the linked reports. Initial raw command logs and the environment/source/payload manifest remain private under ignored `local/m07-encounter-2026-09-21/`.

## Implemented change and evidence boundary

Bridge/NativeHost 0.0.31 and wire schema 2 extend the M06 foundation with authenticated native attack/approach/engage intentions, target generations, immediate native-incarnation validation and a developer-only client-origin command interface. The coordinator derives identity from authentication, forwards its own actor fields and rejects dead or foreign actors and stale/player-owned targets. Request-correlated incoming and outgoing decisions are logged without credentials; queued delivery remains distinct from original application.

The authority initializes the qualified M03 B player/reward context. Scene capture exports observed per-owner DNA, and each client's controlled avatar receives its own absolute balance. Evidence distinguishes a current native reread, a source scalar and the retained B corpse balance. B revival/progression after context retirement remains unsupported; no accrued balance is silently reset.

The retained M06 failure was caused by excluding dead nouns from the scene and then attempting to destroy the client's actual native avatar. Native08 proves an original same-noun revival. The new adapter keeps the incarnation and projects explicit health/dead scalars under the existing replica guard. It does not replay native death/reward/revive handlers. Binding checks pin the SDK offset and original set/clear instructions. [Detailed cause, observed transitions and limitations](../../docs/m07-life-state.md), [retained trace hashes](life-state-analysis.json).

At this initial .31 stage, the independent post-respawn Join failure on NPC native ID 97 remained unexplained. New projection diagnostics split the previously compound rejection into precise binding/value/maximum-health/pose checks. Native02/03 subsequently diagnosed the missing context, and native04 verified the bounded correction as recorded below.

The native pickup audit identifies corpse eating as a candidate: original code claims an interacting creature, applies gradual food consumption and guards a one-time DNA bonus. Its native input route and B ownership adaptation are not yet qualified. No invented pickup action, synthetic reward or coordinator gameplay substitute was added. [Native binding and pickup audit](../../docs/m07-native-encounter-audit.md).

Developer delay/jitter tooling relays the real TCP/TLS byte stream without interpreting or dropping bytes. Its HOST tests include real Schannel authentication through the relay. It explicitly does not implement packet loss or assert an Internet RTT. [Inspected local packet-loss prerequisites](impairment-prerequisites.md).

## Commands and results

Expected results for the following successful implementation checks were exit 0. Build/unit-test commands never launch SPORE. A later integrated failure and its correction are retained separately below.

| Command | Observed result |
|---|---|
| `cmake --build build/win32 --config Release --parallel 4` | Initial and reviewed integration builds pass; bridge/NativeHost 0.0.31. Private logs `build-031-01.log` and `build-031-02.log` retain complete output. |
| `ctest --test-dir build/win32 -C Release --output-on-failure -V` | Initial integrated suite: 9/9 targets, 11.18 seconds, including 2,563 network assertions. This predates final review corrections and new harness tests. |
| `ctest --test-dir build/win32 -C Release -R '^network_host$' --output-on-failure -V` | Reviewed coordinator/session integration: 1/1 target, 2,564 assertions, 3.59 seconds. |
| `ctest --test-dir build/win32 -C Release -R native_actor_abi_host --output-on-failure -V` | Existing native-call ABI HOST fixture: 1/1 target, 44 assertions, 0.21 seconds; no native death-presentation claim. |
| `python -m unittest discover -s tests/unit -p test_m07_delay_proxy.py -v` | Final 8 HOST tests pass, including byte ordering, half-close, bounds, capacity, backpressure, private report handling, real Schannel TLS through the relay and cancellation during cleanup. The first seven-test run preceded the recorded Python 3.14 failure. |
| `python -m unittest discover -s tests/unit -p test_m07_harness.py -v` | Final 12 synthetic parser tests pass. They deny native-hit claims from queued requests, missing native returns, mixed owners/generations, superseding actions and scene exits; unauthorized-action cases require the real remote rejection chain and bounded no-forward evidence. |
| `pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-life-fields-01 -ReuseDatabase -Addresses C0BB00,C08210,C02D00` | Pinned-PE static export passes. This is not original-game execution. |
| `pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-pickup-dispatch -ReuseDatabase -Addresses D716F0,D70FD0,D87590,D85A50,D9BBD0,D47860` | Static candidate export passes; no consumption command qualified. |
| `pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-pickup-native -ReuseDatabase -Addresses D7EE10,D869B0,D70A80,D475E0` | Static candidate export passes; no inferred behavior-tick ABI called. |

The second complete CTest attempt (`ctest-031-02.log`) failed **1/9**, exit **8**, after a Python 3.14 shutdown hang in the delay-proxy test. The first standalone proxy test used Python 3.11. The original listener awaited accepted connections before their owners were cancelled, which deadlocked on Python 3.14. Only the verified owned tooling-test process was stopped; the remaining eight CTest targets passed. The helper now closes accepted transports before waiting for listener completion and bounds shutdown waits. A subsequent cancellation review also fixed cleanup awaiting the first writer before closing the second; both writers now close before either wait and bookkeeping is unconditional. Both the failed log and exact-interpreter reproduction are retained.

Final integrated verification repeats the full CTest command after all source and test edits: **9/9 targets pass**, exit **0**, **12.16 seconds** (`ctest-031-03.log`). This includes **2,564 network assertions**, **132 Python tests** (2.002 seconds, Python 3.14), 322 replica assertions, 189 worker assertions and 54 launcher assertions. No resource warning appears in the final log. The reviewed Release build is `build-031-02.log`, exit **0**; no C++ source changed afterward. `git diff --check` passes. The [reviewed verification manifest](verification.json) records exact source/payload hashes, commands, outcomes and the private-manifest hash. Earlier successful runs are not represented as tests of later edits.

## Environment and protected scope

Live read-only inventory: Windows 11 Pro 10.0.26200, Ryzen 7 9700X (8 cores/16 logical processors), RTX 4080 SUPER. The inventory also lists the AMD integrated and Parsec display adapters; this is not proof of which adapter a future native run uses. No SPORE/NativeHost/coordinator process was running at the recorded inventory.

Pinned SDK commit: `cbf9206b9a823f0911cd9be0217104a49d72380b`. Injector commit: `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. The current original executable hash still matches `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. Build tooling retains compiled executable/content/payload checks. New native admission still requires fresh personal backups and disposable-profile isolation checks; this session's read-only executable match does not replace them.

Concurrent repository-publication/website work is outside this increment. Its publication-only commit is preserved on the shared branch; this task has not committed or pushed the M07 changes. The original-history remote remains the private archive, as coordinated by that task.

## Open gates and next smallest step

The full [M07 protocol](../../tests/engine/M07.md) keeps every original requirement: two-player same-NPC combat and response to either owner, shared health/death and one reward, a simultaneous contested native pickup, post-death reconnect, original respawn/reset, configured latency/loss, an unauthorized real client action and inspected native evidence/cleanup.

At the end of the initial .31 implementation phase, desktop permission was pending. The planned next step was a bounded original death/baseline run with refreshed isolation/protection and a frozen payload, followed by diagnosis of NPC97. The user subsequently authorized that native work. Original corpse interaction/pickup ownership and a suitable actual packet-loss mechanism remain prerequisites for the full encounter. No incomplete gate has been marked verified or removed from scope.

## Authorized native follow-up and final .35 state

The user authorized desktop/native work with “yes lets go.” Six bounded cases ran in the three existing verified disposable profiles, with refreshed personal-save protection, separately frozen .31–.35 payloads, actual original-game traces and process-specific WGC recordings. No game binary, save, invitation, account inventory or raw capture is published here. Private full evidence is under ignored `local/m07-native-2026-09-21/`.

- [Native01–03](native-lifecycle.md) retain the dead owner and demonstrate a fresh dead-state Join. They also preserve the .31 trace-budget failure, .32 original revival followed by client rejection, and .33 diagnosis of missing native age/effective-health context. The .32 trace ceiling increases from32 to64 MiB without reducing diagnostic sampling; affected bounded readers accept66 MiB and trace exhaustion remains an explicit failure.
- [Native04/.34](native04-lifecycle.md) verifies the changed context/scale path through original death and revival, then a fresh 33-entity baseline before Connected and subsequent native move/jump. The 400-byte schema3 carries native age, alpha status, Combatant state and observed scale. Existing effective-maximum-health validation and local-avatar ownership remain intact. No full death UI or baby-model parity is claimed.
- [Native05/.34](native05-combat.md) records both authenticated players damaging the same6-HP NPC and one original7-DNA reward. It fails complete encounter agreement: client B stops on a new entity spawn, and client A's scalar-dead target remains visibly upright. NPC retaliation against B, contested pickup, reward-preserving post-combat reconnect, unauthorized native action and latency/loss remain unverified.
- [Native06/.35](native06-spawn-blocker.md) adds diagnostic-only spawn logging and reproduces the exact `missing_template` result on both clients, before the native factory. The new species needs a qualified local native creation path; the existing living-template selector has no eligible result. The next experiment is a read-only local herd/profile and corresponding authority-metadata comparison, not an unchecked factory fallback.

Final commands are `cmake --build build/win32 --config Release --parallel 4` and `ctest --test-dir build/win32 -C Release --output-on-failure -V`, both exit 0. The final .35 suite passes **9/9 in 12.44 seconds**, including **2,595 network assertions and 132 Python tests**. This remains BUILD/HOST/FIXTURE evidence. Private `build035-final` and `ctest035-final` command JSON/log pairs record exact commands, completion UTC and outputs. Per-run source/payload manifests retain the actually tested versions; later documentation edits do not rewrite their hashes. The reviewed [final verification manifest](native-verification.json) exports source/payload hashes and bounded outcomes without raw private logs or account data.

All games, supervisors, coordinators and recorders are closed at final inspection. Final native06 game/worker exits are0, all29 personal files remain unchanged and all six disposable Games/Creations tree checks match their retained manifests. Coordinator logs finish with `stopped:true` and no log exhaustion. Native01's earlier authority exit35 on trace exhaustion remains recorded; it is not relabeled a clean pass. No peer/launcher credential ACL grants were introduced in these runs. The changes remain on the private development branch without a task-created commit or push; the unrelated publication commit is preserved. **M07 remains IN_PROGRESS.**
