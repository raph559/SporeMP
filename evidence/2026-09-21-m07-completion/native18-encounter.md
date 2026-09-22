# Native18: shared combat, original consumption and result-preserving Join

This **NATIVE / NETWORK-REAL** run used bridge **0.0.44**, wire schema **4**, the pinned Creature fixture, and isolated original SPORE processes. A and B each dealt three original one-point hits to the same NPC. B delivered the final hit and received the original **7 DNA** award; A remained at **0 DNA**. Both clients projected the corpse and its changing food state. A subsequently consumed food through original feeding callbacks, the original engine returned the depleted corpse to its pool, and a fresh B process joined with the **7-DNA** result preserved.

**Full M07 remains NOT_VERIFIED / IN_PROGRESS.** The strict staged target-switch attempt remains **INCONCLUSIVE**: the NPC retaliated against A seven times and did not damage B before dying. Shared damage, consumption and reconnect evidence do not erase that retained result or supply the separate configured-impairment and unauthorized-action checks.

The [reviewed scalar summary](native18-summary.json) contains original source, payload, trace, recording and reviewed-frame hashes, exact event references, independent evaluator checks and cleanup results. Raw logs, recordings, account/process identities and save inventories remain private. Hashes explicitly identify the original retained bytes; the reviewed summary is a separate export.

## Native readiness with replica gameplay suppressed

The .44 build and full **10/10 HOST/FIXTURE CTest** run passed, exit **0**, in **23.73 seconds**. Original-game readiness was then observed separately at **10:52:58.548 UTC**, before combat. All three exact launch identities and live game/supervisor pairs were checked, and their status files were fresh.

| Role | Observed state | App / heartbeat age | Native AI age | Projection observation |
|---|---|---:|---:|---|
| Authority | ready | 203 ms | 203 ms | Native simulation role |
| Client A | ready | 47 ms | 13,469 ms | Connected, baseline 1, age 47 ms |
| Client B | ready | 94 ms | 12,406 ms | Connected, baseline 2, age 94 ms |

The replicas remained ready while their original gameplay AI was intentionally suppressed. Their readiness came from the explicit replica role, fresh app/heartbeat progress, and the authenticated periodic pipe status carrying a connected projection and nonzero applied baseline. Authority readiness still required native AI progress. The immutable observation retains each original status JSON and its hash. This is native evidence for the .44 role-specific readiness correction; HOST tests separately cover stale/malformed projection states and replies that must not establish or refresh projection readiness.

The earlier [Native16 capture failure](native16-capture.md) remains a separate setup result. Native17 subsequently reached readable, inspected first frames but submitted no combat because its private 60-second startup guard expired before the frame-review marker. Native18 used a 90-second startup guard plus a fresh authority-health prerequisite. Those orchestration changes did not modify original gameplay or turn either earlier run into encounter acceptance.

## Actual shared fight and original reward

The target was global entity **12884901967**, generation **7**, original native noun **79**. The authority resolved both authenticated player intentions against that same incarnation.

| Original attacker | Native enter / return sequences | NPC health changes |
|---|---|---|
| A | 13188 / 13193; 13709 / 13714; 14364 / 14369 | 6 → 5 → 4 → 3 |
| B | 16554 / 16561; 17136 / 17143; 17809 / 17820 | 3 → 2 → 1 → 0 |

NPC79 made an original target request against A at sequence **11899**. Its seven paired damage calls against A each removed **0.5 health**, with return sequences **13441, 14121, 14721, 15281, 16371, 17013 and 17586**. There were no paired NPC damage calls against B. Both controlled owners remained alive at the reviewed encounter outcome, and all three native scenes agreed on the same dead target.

During B's final original damage call, sequence **17818** recorded the original owned reward: DNA **0 → 7**, goal progress **0 → 7**, amount **7**, one award. A's native DNA remained **0**. These values are actual returned native reward/readback evidence, not a grant inferred from an accepted command or echoed packet value.

The staged driver ended after **24.312 seconds** with `NPC died before target-switch qualification` and result **INCONCLUSIVE**. Its original report and hash remain retained. The later independent shared-damage observation establishes the actual A/B hits and corpse outcome, while preserving the missing strict target-switch result.

## Contested feeding, depleted-corpse retirement and the original false report

Before pickup submission, both owners were alive and all three native scenes agreed on the untouched corpse: health **0**, dead flag true, fed flag **0**, remaining food **100**, and beneficiary **0**. Both authenticated original pickup orders returned before the winning feeding callback: B at source sequence **26397**, A at **26427**.

A's first-feed callback began at **26638**, performed the original DNA call at **26639 → 26640**, exposed the fed-flag change at **26641**, recorded first-feed ownership at **26642**, and returned at **26643**. Its configured original DNA amount and actual delta were both **0**. The claim callback itself left food **100**, hunger **0**, and health unchanged. Positive nutrition began in the next observed original feeding scope, **26647 → 26653**: food decreased by approximately **0.2399979**, hunger increased by **0.24000001**, and health increased by approximately **0.028**. This distinction preserves original callback behavior; a flag transition alone is not nutrition evidence.

The bounded window contains **481** verified positive A food-to-hunger/health transfers and **zero** losing-owner transfers. A's native DNA stays **0**; B's combat DNA stays **7**. The original once-only first-feed flag/beneficiary and remaining food are separate state: the report does not equate the flag with instant consumption of the entire corpse.

Both clients have exact source-revision native readback pairs, without rounding food or comparing unrelated process clocks:

| Client | Source tick | Source sequence | Client sequence | Native source / client food | Fed / beneficiary |
|---|---:|---:|---:|---:|---|
| A | 77025890 | 30027 | 8515 | -33.1247902 / -33.1247902 | 1 / A |
| B | 77025812 | 30017 | 8359 | -33.1247902 / -33.1247902 | 1 / A |

The negative remaining value is the finite value returned by the original engine; it was neither clamped nor rounded. The authority then recorded this ordered retirement chain:

1. **30106:** original pool return for native noun 79, counter **0 → 1**, disabled and herd absent.
2. **30107:** the recorded first-feed beneficiary A was retired for reason `native_pool_return`.
3. **30111:** the authority published the entity despawn. The coordinator independently received the despawn with generation **7**.
4. Client A **8551** and original client B **8404:** actual native removals with reason `authoritative_despawn`, following their matching corpse projections.

**The original live pickup report remains false and unchanged.** Its parser required the corpse to remain in the final native scene. After legitimate consumption and removal, it also discarded owner balances by extracting them only through the absent corpse's normalized entry. Consequently, it reported missing final projection/balance evidence despite retaining the exact earlier food pairs, real nutrition and native retirement chain.

Two subsequent, separately identified reviews address that diagnostic error:

- The frozen independent terminal evaluator requires the original depletion/pool/beneficiary/despawn sequence, both exact earlier projections and native removals, current owner identities/native DNA, all existing first-feed/nutrition guards, and no intervening failure. It reports the bounded **G04 terminal outcome observed**. Five HOST counterfactual fixtures exercise rejection conditions.
- The corrected production harness extracts owner balances independently of corpse presence and accepts the explicit alternative **`retired_after_projection`**. Its **39 HOST tests** pass, including missing client removal, wrong generation, absent exact projection, stale owner samples, changed balances, malformed retirement and reappearance rejection. Replaying Native18's original complete trace prefixes with its original completion/connection flags passes, with no reported gap. This is a corrected analysis of preserved native evidence, not a new native run.

The corrected report explicitly sets `corpse_currently_projected_on_all_three=false`. It does not manufacture a corpse baseline after removal. Unknown disappearance remains a failure. The current production alternative conservatively refuses ambiguous later ID reuse and a destroy-hook entry without completed original destruction evidence; only the qualified retained-pool route is used here.

## Fresh B Join preserves the outcome

The old B game and supervisor both exited **0**. A fresh isolated B process then joined while the original authority and A remained active. Its baseline advanced **2 → 3**; native baseline application at sequence **211** preceded Connected at **212**. The fresh process retained one unique B-owned entity, **12884911489**, generation **33**, with directly read native DNA **7**. A remained entity **12884902120**, generation **30**, DNA **0**. Fresh B's controlled native avatar represented B; the separate remote A representation was not misreported as a second independently readable local DNA balance.

The fresh baseline excluded consumed generation **7** and removed the stale saved native noun before Connected. No repeated original first-feed grant or original client reward occurred after the baseline. Later original reuse of native noun 79 was sampled at source sequence **34036** as **generation 38**, pool cycle **1**, with fed flag **0** and beneficiary **0**. It is a distinct incarnation and cannot resurrect or inherit the consumed generation's beneficiary.

The original G05 evaluator also remains false. Its two missing checks were the preceding old live pickup verdict and an archive-time requirement that every endpoint still be Connected. The latter snapshot was taken after requested cleanup. The independent review instead uses the recorded interval before cleanup: fresh B's baseline acknowledgment and later traffic from authority, A and fresh B share one coordinator clock. Their observed overlap lasts **14,203 ms**, ending at the earliest subsequent clean connection close. It never subtracts QPC values from different game processes or treats a stale status file as a live connection.

With that actual interval and the independently qualified pickup outcome, all **21** retained G05 checks pass. The original authority/A trace prefixes remain preserved; all owner identities/native balances match; the consumed incarnation stays absent; and there is no intervening native or network failure. This supports the bounded result-preserving fresh Join. It does not claim crash durability, save persistence of this encounter, or a new post-player-respawn action test.

## Inspected views and cleanup

The root operator inspected seven original capture stills. All three initial views show the original Creature world, HUD health **9 / 9 / 10**, and DNA **0**. At each original recording's 105-second frame, the authority shows its original victory dialog; the operator dismissed its native green confirmation. Client A shows blue creatures beside the flat green corpse, health **3**, DNA **0**. Client B shows the flat green corpse, health **10**, DNA **7**. Fresh B's 19-second post-Connected frame shows the blue controlled creature in the foreground, health **10**, full hunger and DNA **7**. The recordings and images remain private; hashes are retained in the reviewed summary. These selected views do not establish every animation frame, audio quality or native frame timing.

The bounded watchdog requested shutdown at **10:58:35.854 UTC**, after its 390-second threshold and within the 420-second overall bound. All four game generations, supervisors and exact-process recorders exited **0**. The four closed actor traces contain **51,963 / 20,876 / 17,487 / 1,091** records for authority, A, old B and fresh B. Independent review verified their original archive hashes, contiguous sequences, monotonic per-process QPC, zero foreign-callback events and healthy trace stops. There are zero recorded scene-capture, scene-projection, pickup-observation, pickup-reward, network-error or trace-limit failures. All **29 protected personal files** and all **six disposable fixture trees** remain unchanged.

Reproduction uses `python -m unittest discover -s tests/unit -p test_m07_harness.py` (39 HOST tests, exit 0; root independently repeated it). The separate evaluator's normalized command is `python evaluate-m07-terminal-rejoin.py --before PICKUP_REPORT --after REJOIN_ARCHIVE_REPORT --old-b-closed OLD_B_ARCHIVE_REPORT --output NEW_REPORT` (exit 0); uppercase arguments identify private archived inputs and a new output, not published paths. Original and corrected helper/evaluator hashes are listed separately. No native gameplay, source C++ mutation, original evidence overwrite or milestone-status promotion was performed by this report review.
