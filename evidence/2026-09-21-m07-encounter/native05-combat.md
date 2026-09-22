# Native05: shared original damage and reward, incomplete client outcome

2026-09-21, bridge **0.0.34**, original Creature campaign, one authority and two clients. **M07 remains IN_PROGRESS; the bounded encounter result is false.** Both authenticated players damaged the same original NPC, and the authority applied one original 7-DNA award. Client B failed a native entity spawn before receiving the combat outcome. Inspected recordings also expose a corpse-presentation difference on client A.

## Observed original gameplay

The selected existing foreign-species NPC was native **79**, global entity **12884901967**, generation **7**, authority actor **3**. It began alive at **6 HP**. Both players were alive and both clients held that same target identity before submission. Two real client IPC requests submitted `engage`; owner 1/request 1 became authority command 1, and owner 2/request 1 became command 2. Correlation uses authenticated owner as well as request, so the equal per-client request numbers are not conflated.

| Native observation | Closed-trace evidence |
| --- | --- |
| A damages the shared NPC | Four one-point hits; Animal damage scopes **2441, 2540, 2675, 2794** |
| B damages the same NPC | Two one-point hits; scopes **2634, 2767** |
| Shared NPC dies on authority | Health **6 -> 0**, retained identity/generation; A's lethal call spans sequences **14258–14265** |
| Original reward applies once | Within lethal scope **2794**, DNA call **2796**, sequences **14262–14263**, amount **7**, A balance **0 -> 7**; B remains **0** |
| NPC retaliates against A | Three native **0.5-HP** hits; scopes **2496, 2639, 2760** |
| NPC interaction with B | B damages the NPC, but no NPC-to-B damage or target switch was observed in this case |
| Held input ends | A/B intentions cancel after target death at sequences **14280/14320** |

Each counted hit includes paired original Animal/base-damage entry and return, the resolved attacker/receiver identities, and an actual health decrease. Queued requests and requested damage are not counted as hits. Original starvation also reduces A's health; those unowned, zero-Animal-scope calls are excluded from combat damage. NPC target callbacks are sampled, so the recorded target requests do not provide an exhaustive decision history.

The harness observed for **35 seconds** after both submissions. Native held input has a **30-second** maximum, with ordinary native AI and delayed actions still responsible for outcomes. IPC, parsing and archival overhead are outside the 35-second observation interval. The complete harness invocation and individual IPC argv/results are retained in the private case report; no request was retried.

## Failed replication and presentation limits

Client B records `Native entity spawn failed.` at sequence **2646**, local trace time **73.1839791 seconds**. Its last projected source tick is **39022453**. At the end of the encounter observation it still reports NPC **6 HP/alive**, A DNA **0**, B DNA **0**. The authority and client A report NPC **0 HP/dead**, A DNA **7**, B DNA **0**. Therefore `npc_dead_on_all_machines`, shared-state agreement and `bounded_encounter_observed` are **false**. Client B's command had already reached the authority; later authoritative B hits do not establish a healthy B replica.

This build reports a generic spawn failure without the exact native rejection result. Client A admitted newly encountered entities near this point, but this evidence does not prove why B failed. The next diagnostic must expose the actual spawn/adoption failure condition before selecting a repair. No local-template absence, factory failure or maximum-health mismatch is asserted as the cause.

Thirteen actual frames from the three closed Windows Graphics Capture recordings were inspected:

- Authority **53 s** shows the original bite effect and target health **5**; **56 s** shows both blue creatures facing the green target at **2 HP**. At **58 s**, the target lies on its side and a floating **+7** appears. At **65 s**, the original victory tutorial is visible.
- Client A **60/65 s** shows **7 DNA** in its HUD, but the green target remains upright beside the players. Native scalar death and reward readback therefore do **not** establish matching corpse/death presentation.
- Client B **49/56 s** remains in the home-herd view with **10 HP/0 DNA**; these frames do not depict the shared encounter outcome.

Timestamps are relative to each recording, not a synchronized frame comparison. All three recordings contain original game content and close with recorder exit **0**. Their approximate durations are **129.433/129.000/128.633 seconds** at **1280x720**. Capture cadence is not native frame timing, audio was not recorded, and the selected frames are not a continuous visual audit of every hit. Original media and extracted frames remain private.

## Closure and reproducibility

All three closed actor traces have contiguous sequences, zero foreign callbacks, `trace_stop.healthy=true`, and detach status **0**. Each game and worker exits **0**, each bridge records disposal, and all workers are stopped. Worker Games/Creations comparisons remain unchanged. Clean shutdown does not erase client B's earlier gameplay failure.

| Closed actor trace | Records | SHA-256 |
| --- | ---: | --- |
| Authority | 21,768 | `df0c0804c01ae591b7e767a674efe9eb170514cf03dd1cdac7845361ad18c392` |
| Client A | 7,303 | `cc74640cec878596f45edef1aa160fec7933dc7b29c90cc49efb528511f31aec` |
| Client B | 4,244 | `6c2a184ac5126b35749c0498c64ea2f1fe1b60b9fa8e810539edadfe2d5689e8` |

Pinned executable SHA-256: `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. SDK commit: `cbf9206b9a823f0911cd9be0217104a49d72380b`. Loaded bridge SHA-256: `bd25e682d87fdcb3627d65ae93450c78d24a7350e5f3570985ab1c38bc53c61c`.

Private artifacts under `local/m07-native-2026-09-21/` retain raw process identities, exact commands, closed logs, video requests/completions and hashes:

- `native05-shared-combat/report.json`: SHA-256 `3e2118077b39cfebed19c2cb09bf9a7b51c108dd2a0e0e2f342ed2360b76d623`.
- `native05-closed/`: closed snapshots and archive report. The analyzer verifies that the earlier bounded after-snapshot bytes are exact prefixes of these closed traces before reproducing both players' hit chains.
- `native05-analysis.json`: SHA-256 `030382d4252d853eadcde5711c3406ef37af8a68f9f3536d6d2afa334019ef06`; produced by `python local/m07-native-2026-09-21/analyze-native05.py`, exit **0**.
- `native05-visual-review/visual-review.json`: SHA-256 `41cecfcff762e1ad3533f589aef3aa5f0311cb160d7b91f74e5322efd2c593f6`; all thirteen recorded FFmpeg extraction commands exit **0**.

This run establishes shared authority-side native damage and one original kill reward, with matching scalar outcome on client A. It does not establish three-machine death agreement, corpse-presentation parity, NPC targeting of B, contested pickup, post-death reconnect, impaired-network acceptance or full M07 completion.
