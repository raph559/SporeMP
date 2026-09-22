# Native06: replica spawn blocked by a missing living template

Date: 2026-09-21, Europe/Paris. Bridge **0.0.35**, wire schema **3**, packet size **400 bytes**. Evidence classes: **NATIVE** and **NETWORK-REAL**. **M07 remains IN_PROGRESS.**

Both original clients terminate scene replication at the same dynamic spawn because the qualified native factory route requires an eligible living local template and the selector finds none. The new diagnostics identify this boundary precisely. The shared-encounter gate fails; no new spawn fallback is implemented or accepted. This result does not retroactively diagnose or invalidate the separate bounded [native04 lifecycle/context result](native04-lifecycle.md).

## Exact failure and reproduction boundary

The run starts one original authority and two original clients in the existing prepared disposable Creature fixture. A bounded **35-second** client-origin `engage` exercise targets NPC **79**, global entity `12884901967`, generation **7**. During that exercise the authority publishes a different NPC requiring local representation:

| Failed spawn field | Observed value |
|---|---|
| Global entity / generation | `12884901957` / **34** |
| Source native ID / owner | **69** / **0** |
| Source tick | `39489906` |
| Species key | `[108668588, 731352134, 1080189440]` |
| Resolved animal archetype / source herd | `4130283391` / **1772** |
| Age / health / scale | **1** / **6** / `0.550879359` |
| Result | `missing_template`, code **8** |

Client A records `scene_spawn_template` at sequence **2427**, `network_scene_spawn_failed` at **2428**, then network error **2429**. Client B records the equivalent consecutive chain at **2423 → 2424 → 2425**. The error detail is `Native entity spawn failed.` No `scene_spawn_factory` event exists for this entity. The reviewed control flow independently confirms that `spawn_preflight` returns before entering the factory/projection callback when the selector returns null.

**The diagnostic census inspects only 4,096 nouns and explicitly records `census_truncated=true`.** Within that prefix, each client sees **30 present animals**, **30 living animals**, and **0** matching species-key, archetype or eligible-template entries. Those zero counts must not be described as an exhaustive census or proof that the species asset is unavailable.

Separately, the production `template_for` selector iterates the **full native noun list**, requiring a living, supported animal with the matching complete species key and resolved animal archetype. It returns null. That actual selector result establishes the absence of an eligible template for this attempted spawn; it is not inferred from the capped diagnostic counts. No arbitrary substitute species, unchecked herd or replacement gameplay logic is used.

## What the interrupted encounter establishes

The harness completes its observation procedure but reports `bounded_encounter_observed=false`, `sampled_shared_state_agreement=false`, and `full_m07=NOT_VERIFIED`. Procedure completion is not encounter acceptance.

Its source-native correlations record both players targeting and damaging NPC 79: owner A contributes five one-point decreases including the lethal **1 → 0** call; owner B contributes **4 → 3**. The NPC damages owner A, while damage to owner B is not observed. One source-native owner A DNA call changes **0 → 7** in the observed lethal scope. These are partial authority-side findings. The clients stop replication at the unrelated actor 69 spawn, so those records do not establish all-machine final health/death agreement or delivery/uniqueness of the shared reward. The latest retained client NPC samples still show health **6**.

No contested pickup, encounter-result reconnect, actual latency/loss repeat, or unauthorized-action acceptance follows this failed run. This report performs no new combat visual acceptance. Root inspected initial original-game frames; all three recordings remain private, and their successful closure is not used as proof of later gameplay presentation.

## Closure, checks and private provenance

The frozen actor traces contain **14,245 / 3,156 / 3,155** complete, contiguous events for authority/client A/client B and end with healthy `trace_stop`. Both exact diagnostic chains and all three source hashes were independently checked against the separate lifecycle audit. The private closed-analysis command exits **0**.

Requested game/worker shutdown exits are **0**: authority at `00:29:58.953Z`, client A at `00:29:58.502Z`, and client B game/worker at `00:29:58.164Z` / `00:29:58.178Z`. All three Windows Graphics Capture completions report exit **0**, no wall timeout. Closed checks record **29 personal files unchanged**, zero recognized running workers, and all **six** disposable Games/Creations tree checks unchanged (**16 Games files and 0 Creations files per profile**).

The source/payload manifest records the frozen .35 Release build and full CTest commands, both exit **0**. These BUILD/HOST results remain separate from the native failure above:

```powershell
cmake --build build/win32 --config Release --parallel 4
ctest --test-dir build/win32 -C Release --output-on-failure -V
```

All hashes below identify **private original artifacts**, not public-export files. Raw traces, recordings, account/profile paths and saves remain private under the [evidence policy](../../docs/public-evidence.md).

| Private original | SHA-256 |
|---|---|
| Source/payload manifest | `beb7362514f28998569825450f1ce49a566d89d4973c7b4534d9890fa77107f7` |
| Independent closed spawn review | `890e1891ba351fb7ac07918990e89b0b0b5fdcef0f0079ee162be1acb59203b6` |
| Closed archive report | `fe47ea0e49f5686d95fa6b50c9b9a09cec1c4e36ab98bf7f23c3e498b2a0a769` |
| Shared-combat procedure report | `7bd292439604c713447cdb3f50dedc4404360fc551d4390124572963971851fe` |
| Authority actor trace | `1f65a1a508ce89f65ba6520cccf75d80fc000742f6810e55c8f674d2268bac61` |
| Client A actor trace | `1482da65579ff34d814bf5d91dd705c556653abc8688d28f903c4ace067390ce` |
| Client B actor trace | `0a39f77633e165aff30d446aa34cc0cd5e8ef85ce663c154b36990383a224d8a` |
| Closed personal-file check | `27d386d85ba0f402acbe14c581cc7c14121984f704d05b0f09e5875b4660fd68` |
| Closed fixture-file check | `bbb09bd9d403ade67e6ba93e7ee53a6f4b63943c2e6feb22eb738f05a153ff53` |

## Next smallest experiment

Perform a read-only exact census of local herd **1772** and its profile, paired with the authority herd metadata and validated native lifetimes. A dormant-herd construction route is only a **static lead**, not an implemented or qualified fallback. The packet's resolved animal archetype comes from animal offset `+0xE80`; it must not be equated with the herd field at `+0x88`. A raw profile pointer also requires independently qualified ownership and lifetime before any use.

Keep the current quarantine unchanged while identifying a supported native construction route for a published species/archetype without a living local template. Only after that route is qualified should a bounded spawn/replication reproduction precede another shared-encounter attempt. The complete [M07 acceptance protocol](../../tests/engine/M07.md), including the still-open presentation and reward gates, remains intact.
