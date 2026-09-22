# Native04: connected original revival and fresh Join with native context projection

Date: 2026-09-21, Europe/Paris. Bridge **0.0.34**, wire schema **3**. Evidence classes: **NATIVE** and **NETWORK-REAL**. **M07 remains IN_PROGRESS.**

This bounded run passes the previously failing native context/scale projection through owner A's original death and revival, followed by a fresh client B Join and real native movement/jump. All four original process generations close cleanly without projection or network errors. Death UI, corpse pose and complete baby-model presentation remain unqualified. No shared combat, contested pickup, reward uniqueness or full M07 acceptance is claimed for native04.

The earlier .31–.33 failures remain recorded in [native lifecycle experiments](native-lifecycle.md). This report covers native04 only. Raw traces, account inventories, recordings and frame images remain private under the [public evidence policy](../../docs/public-evidence.md).

## Change and tested conditions

Schema 3 carries the authority's native age, alpha flag, combatant state and observed spatial scale with each entity. The replica applies these supported fields and rereads native context while retaining the local avatar flag and other local flags. It continues enforcing the independently queried effective maximum health. The change does not obtain an animation by replaying original GrowUp, Heal, death, revival, reward or progression handlers.

The run uses the same pinned original executable, SDK, injector, Creature fixture, rendered desktop and three disposable profiles documented in the preceding report. The pinned executable SHA-256 remains `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`; SDK commit is `cbf9206b9a823f0911cd9be0217104a49d72380b`. Source/payload hashes are retained in the private native04 manifest identified below.

The expected result was retained owner A identity through original starvation and ordinary death-confirmation input, projected native context on both clients without the NPC 97 rejection, then a fresh post-revival baseline and native actions. Desktop input was limited to the recorded original confirmation; the later move/jump exercise used the existing client IPC/authenticated network path. The independent trace/frame review performs no native input or process actions.

## Death, original revival and NPC growth

Owner A remains global entity `12884902120`, generation `30`. Native IDs are process-local: source/client A use `232`; client B represents remote A with `9601`.

The source retains the dead actor with health **0**, age **1**, alpha **0**, combatant state **2**, and scale **1**. Client A and original client B apply death at sequences `9465` and `9423`, respectively, with source tick `38727718`. Both subsequently apply alive state under the same identity at source tick `38752625`. The original revival restores health **5**, hunger **100**, age **0**, alpha **0**, and combatant state **0**. Source samples observe scale `0.100000001` during the transition and then `0.600000024`; the adapter transports the observed value rather than inferring a size from age.

NPC `97`, global entity `12884901985`, generation `25`, now projects age **1** and effective maximum **10** successfully. Its first adult-age context application occurs at client A sequence `10823` and original client B sequence `10784`, source tick `38752812`; earlier scale-only applications still have age 0. The observed scale changes from `0.733775854` to `0.896912634`. The native effective-health guard remains active; this run records no incoming-state refusal or sampled health above the independently observed native maximum.

Two independent analyses of the frozen complete traces agree on the following results. The first comparison checks each sample/application's received age, alpha, combatant state and scale against the current native reread. The second compares client records with an independent authority record only when entity, generation and source tick match exactly.

| Process generation | Sample/application records with context checked | Exact authority-tick comparisons | Client samples without an exact authority-tick partner | Context/readback or exact-pair differences |
|---|---:|---:|---:|---:|
| Authority | 10,395 | — | — | 0 |
| Client A | 10,506 | 722 | 9,772 | 0 |
| Original client B | 7,127 | 622 | 6,492 | 0 |
| Fresh client B | 1,653 | 121 | 1,529 | 0 |

All observed context bindings validate, none of these records lacks its health context, and no invalid/over-maximum native health is found. Exact-pair checks cover health, hunger, dead state, owner, age, alpha, combatant state, scale, native effective maximum and native base maximum. Floating comparisons use absolute/relative tolerance `1e-6` after decimal JSON serialization. The alpha comparison uses only mask `1`; local avatar and other flags are intentionally not required to equal the authority's full flags word.

The **unmatched samples remain unverified against an independent same-tick authority record**. No nearest-time substitution or complete shared-state digest is claimed. Sample/application counts include immediate lifecycle/context events; exact-pair and unmatched counts cover periodic client samples only.

## Fresh post-revival Join and native actions

The original client B exits through requested shutdown with exit **0** at `00:17:27.316Z`. Its replacement authenticates as the same player and applies **33 distinct entities**, including the restored owner A and exactly one controlled owner B. Fresh client B records:

| Native event | Sequence and observed value |
|---|---|
| NPC 97 context application | `167`: restored supported NPC context before Connected. |
| Owner A context application/sample | `173` / `174`: global `12884902120`, generation `30`, alive, health **5**, hunger **100**, age **0**, alpha **0**, combatant state **0**, native/source scale `0.600000024`, effective maximum **5**. |
| Controlled owner B sample | `178`: global `12884911489`, generation `33`, exactly one controlled native avatar; health/effective maximum **12.5**, age **1**, alpha **1**, scale `0.900904953`. |
| Baseline applied | `179`: **33** entities. |
| Connected | `180`: player **2**, baseline sequence **3**. |

The fresh owner context uses source tick `38824593`. This demonstrates a working post-revival baseline and preserved identities in the recorded fixture. It is not the full M07 reconnect-after-combat/reward test.

The bounded action exercise then completes two fresh-client requests through the real connection:

- **Move:** client request `1` reaches authority owner/actor **2**, command `1`, then original `native_walk_request` sequence `59343`. Observed native displacement is **0.6284385155**, above the harness's **0.5** exercise threshold, with unchanged scene lifecycle.
- **Jump:** request `2` reaches authority command `2`; original `native_jump_return` sequence `59692` reports accepted, followed by `native_landing` sequence `59796` with `jump_command=2`. The native call and landing establish more than a queued request.

The action report finishes successfully at `00:18:49.730Z`. It explicitly leaves native visual acceptance separate and does not grant milestone completion.

## Inspected presentation and its limits

Seven full-resolution frames from the closed recordings were independently inspected. Exact selected video PTS values are:

| Recording | Inspected observations |
|---|---|
| Authority, **185 s / 201 s / 215 s** | Original death/corpse camera; intact egg in the nest cinematic; then a small upright creature in the nest with normal gameplay HUD, health **5** and full hunger. |
| Client A, **185 s / 215 s** | During death, normal gameplay view with an upright creature and HUD **0**. After revival, HUD **5**/full hunger and a camera overlooking the nest. The nest center appears empty in the sampled client view; the original small authority creature model is not clearly visible there. |
| Fresh client B, **20 s / 45 s** | Rendered original-game scene before and after the bounded action exercise, with normal HUD. Its displayed health is **13**; native records independently show **12.5**, so the rounded HUD is not used as exact numeric evidence. |

**Death presentation still differs visibly.** Native field/scale equality does not establish the original corpse pose, game-over UI, egg sequence, baby model or model assets on the client. The sampled after-frame does not by itself prove the cause of the missing visible creature. The two fresh-client stills establish rendered scene arrival and continued rendering, not a complete visual walk/jump trajectory or landing audit.

Capture durations are **322.433 s** authority, **322.100 s** client A, **220.033 s** original client B and **58.033 s** fresh client B. All four recorders report exit **0**, no wall timeout. The independent review did not inspect original client B frames in this pass. Capture request UTC, actor QPC and input metadata are not precisely synchronized; timestamps are only a frame-selection aid. No audio or native frame-time measurement was taken.

## Closed traces and preserved files

All four actor traces have contiguous sequences and end in a healthy `trace_stop`. There are **zero** projection/capture/binding rejections, network error statuses, trace-limit failures or native context readback mismatches in these frozen inputs. Normal load/exit events and replica-policy shutdown are retained separately rather than treated as unexplained failures.

The final fresh client B, client A and authority game/worker exits are all requested and **0**, at `00:19:09.074Z`, `00:19:09.351Z` and `00:19:09.781Z`. Closed verification records **29 personal files unchanged**, zero recognized running workers, and all six disposable Games/Creations tree checks unchanged (**16 Games files, 0 Creations files per profile**).

The private context analyzer command accepts the frozen authority trace and individually named original/fresh client traces. It hashes complete bounded inputs, records actual native readbacks, baseline ordering and host termination events, and explicitly refuses to infer visual or full M07 acceptance. The analysis completed with exit **0**. Seven frame extractions using the previously documented FFmpeg command also exited **0** and were followed by actual image inspection.

## Private original provenance

These are SHA-256 values of **private originals**, not public-export hashes. Full traces, captures, absolute private paths and save data are not included in this public document.

| Private original | SHA-256 |
|---|---|
| Source/payload manifest | `dc4d083a439f62126116f28a4696c2f42b0bd8cea9bd37aca203c25ae39c555d` |
| Independent closed context review | `50638d0383483b119da17cc6d1cbb6235842d6fbef3aab5143410c3bfc436449` |
| Separate lifecycle/context analysis | `0481466685af255dfb20bf748dd91b8339c285fd8dc886b896c6255b03551349` |
| Independent visual review | `ca6a374ec9a739677165fb29ed252e60949ad6a804d7eb9ecc9ab1f272c76812` |
| Fresh-Join action report | `58cc1baeb9a781559bbe9472ed3ba6a006714df76d9c36de8e8ed7027e7672e0` |
| Authority actor trace | `36308758c3cbd315262298943c514258accefcb96336c16f2748605884563ad1` |
| Client A actor trace | `de1cf44bd5c56c10b89c90429bf6778b63fc425b7970dda8bb1d7dabbef9f7b1` |
| Original client B actor trace | `738d366b4bb3888f68b8eb816403fce3fe34515027e1bcf01fa6e9aa0c05bc6b` |
| Fresh client B actor trace | `bce0f0844adfaa74d3b2450909f4218335757c681bbb97ed8aabc1c83c228d5b` |
| Closed personal-file check | `1bcccae87a0db0438c81f5fc16a97f0fa7df7507c6189124fb9d7e01d20cf2ff` |
| Closed fixture-file check | `018df10ce77d96dbf5d5b5fc691f058f376aa4c3ca77f7d9da53c8be92ffd37e` |

Native04 closes the bounded scalar/context/scale and post-revival Join regression exposed by native01–03. The complete [M07 encounter protocol](../../tests/engine/M07.md) remains open, including shared native combat and response to both owners, a unique reward, simultaneous contested pickup, encounter-result reconnect, actual configured latency/loss, unauthorized-action challenge and full inspected native presentation. Any subsequent combat run must have its own evidence; this lifecycle result does not imply it passed.
