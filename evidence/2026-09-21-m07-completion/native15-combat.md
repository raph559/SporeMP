# Native15: A-first handoff expires during original death pause

This **NATIVE / NETWORK-REAL** run used bridge **0.0.42**, wire schema 4, three isolated original SPORE processes and the pinned Creature fixture. **The strict G02 shared-fight attempt failed; its staged driver retained INCONCLUSIVE.** A damaged NPC79 and received original retaliation. B's authenticated engage reached the authority, but it never reached attack range before its bounded intention expired during the original player-death pause. The NPC remained alive. Contested pickup, encounter-result reconnect and the unauthorized challenge were **NOT RUN**. Full M07 remains **NOT_VERIFIED**.

The [reviewed scalar summary](native15-summary.json) records exact source/trace/capture hashes, action sequences, original life transitions, readiness diagnostics and cleanup. Raw traces, process/account identities, recordings and save inventories remain private under ignored `local/m07-completion-2026-09-21/`.

## Actual handoff and damage

The staged script dispatched A engage, waited for an actual A hit and NPC retaliation, then dispatched A stop, one backward move and B engage. All four requests traversed their real authenticated clients and coordinator. This serial preparation took long enough that A was nearly dead before B began approaching.

| Operation | Authority command begin / return | Actual result |
|---|---|---|
| A engage | 24493 / 24495 | Original approach followed by three one-point hits on NPC79, leaving it at 3 health. |
| A stop | 28553 / 28556 | Superseded the bounded A engage; it did not prevent the NPC's original retaliation. |
| A backward move | 29316 / 29318 | One original movement destination; the NPC continued pursuing A. |
| B engage, authenticated request 1 | 30123 / 30125 | Created bounded command 4; subsequent original approach calls occurred, but no attack branch was reached. |

B's client intention was **6480**, client result **6481**, authority queue **30114**, and authority receipt **30115**. A had only **0.249906123** health in the sample immediately before B's command. Approximately **0.962 seconds** after B engage started, the NPC's sixth half-point hit killed A: native damage entry **30386**, zero-health state **30390**, returned call **30391**, then a sampled actual dead flag at **30402**. These callbacks distinguish original combat death from starvation alone; A's empty hunger had also been reducing its health before the lethal hit.

NPC79 received exactly **three** observed native damage callbacks, all from A. It hit A **six** times. There were **zero B strikes**, zero NPC strikes against B, and no combat reward. The driver stopped after **24.954 seconds** with `Native owned actor unavailable`. B's already-issued command was not silently cancelled or retried by that parser failure.

## Why the existing B command did not attack

The source recorded **119** B `intention_walk` submissions. Distance fell from **29.326086** to **4.66174126**, while the qualified native ability-plus-footprint threshold was **2.72904944**. The adapter consequently never selected its attack branch: there is no B `intention_attack`, no B target assignment to NPC79 and no B native ability entry in the closed trace. This is not a native attack returning failure; an attack was never submitted.

The unchanged distance has stronger evidence than a geometry guess. Across the reviewed source samples from **152.512 to 196.927 seconds**, all three actor positions remained exactly unchanged. A's native avatar-update count stayed **6171**, B's native NPC-update count stayed **6171**, and NPC79's update count stayed **1335**. App updates, trace output and bridge approach submissions continued. This is an observed pause of the original gameplay updates after A's death. The corpse happens to lie between B and the NPC, but **no collision callback proves corpse obstruction**, and that explanation is not used for acceptance or diagnosis.

The engage deadline uses elapsed wall time. Command 4 expired at sequence **33512**, approximately **175.444 seconds** into the authority trace, with reason `expired`. A's original revival was sampled at **35857**, approximately **197.949 seconds**, after that expiry. Native B/NPC update counters and movement resumed then; the old engage did not restart. The later observation of the existing B action therefore could not supply an attack or a target switch.

For comparison, the successful B fight in native13 crossed its actual range threshold: first attack sequence **12247** recorded distance **2.50767231** against threshold **2.53318882**, followed by original ability/strike/damage. Different native footprint values are preserved rather than replaced by a fixed assumed distance. That earlier success supports the binding, not a claim that native15's accepted B request hit.

After revival, B's original behavior selected A through native caller RVA **0x97C1C3**; this was a follow/target behavior, not renewed engagement with NPC79. B's source health later changed **10 → 12.5**, and NPC79's **3 → 6**, within the same sample interval at sequences **36959/36960**. B already had independently read native maximum **12.5** before this refill; NPC79's resulting maximum was **6**. An original cinematic-related message was also observed in that interval. **The exact healing caller is not instrumented**, so the report does not attribute those changes to a particular revival function, command or invented reward. The final source NPC remained alive at 6 health and both player DNA balances remained zero.

## Original death/revival projection and readiness diagnostic

Owner A retained global entity **12884902120, generation 30** through death and revival. Both clients applied actual death at source tick **74892750**, then alive health **5**, age **0** and combatant state **0** at **74944218**. Each client started the qualified corpse animation once (`92304198`) and the revival idle once (`38280677`), with callback clearing and native start recorded. This run does not contain a later authenticated movement/action after revival, a fresh post-revival baseline, or an NPC terminal outcome.

A separate readiness issue was exposed. Both replica supervisors reported `simulation_stalled` even before the fight and after successful source revival, because their original AI counters stayed at **163/229**, as intended by the replica policy. Their app-update counters and heartbeats continued advancing. The source authority's post-death stall was a real original simulation pause; its later archived status was **ready**, with AI counter **408773** and heartbeat age **63 ms**. These are distinct conditions. The role-independent supervisor check incorrectly required native AI progress from replicas whose gameplay AI is deliberately suppressed. Its correction belongs to the next increment and does not retroactively change .42 evidence.

The next combat experiment should start with a healthy original A and submit B engage before withdrawing A, so original approach can overlap while the authority is still advancing. Actual range, native hits and NPC choices must still be observed. The requirement that the same shared NPC can retaliate against either owner remains in force; neither an accepted queue nor an elapsed wait can replace that evidence.

## Reviewed views, finite bound and closure

The operator inspected **300-second** frames from all three exact-process Windows Graphics Capture recordings. They show ordinary nest gameplay, HUD health **5 / 5 / 13** and DNA **0 / 0 / 0** for authority/A/B. The B HUD rounds its independently read native health **12.5**. These stills establish the observed post-revival views, not a complete animation trajectory or a post-revival command. Request, completion, recording and reviewed-frame hashes are retained; no audio or native frame-timing acceptance is claimed.

The manifest set a **420-second overall bound**, a **390-second capture limit**, and watchdog shutdown at 390 seconds. Root requested generation-bound shutdown at **10:23:01.261 UTC**; all three accepted and all game/worker exits were zero by **10:23:02.486 UTC**. The watchdog recorded `root_completed_before_deadline` and took no shutdown action. All recorders exited zero without wall timeout.

Independent closed-file review verified authority **53,559**, client A **18,972**, and client B **18,955** contiguous actor records, monotonic clocks, zero foreign callbacks, healthy trace stops with detach status zero, disposed bridges and matching archived/staged prefixes. There were no trace-limit, scene-projection, scene-capture, life-presentation or network-error events. The absence of those failures does not convert the failed combat outcome into a pass.

All **29 personal files**, **16 Games files per disposable worker**, and the empty Creations trees remained unchanged; no recognized worker remained. The read-only reproduction command is `python local/m07-completion-2026-09-21/analyze-native15.py` (exit 0). This review changed only new evidence artifacts and did not execute native actions or alter source, milestone status or the acceptance protocol.
