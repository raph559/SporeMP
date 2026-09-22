# M07 native lifecycle experiments: retained death, original revival and remaining mismatch

Date: 2026-09-21, Europe/Paris. Evidence classes: **NATIVE** and **NETWORK-REAL**, with separately identified static binding evidence. **M07 remains IN_PROGRESS.** Three bounded original-game runs establish retained death state, one fresh Join while the dead actor is retained, and original same-noun revival followed by client scalar recovery. They also expose visible death/respawn disagreement and an NPC age/maximum-health mismatch that terminates replication. This is not complete shared-encounter acceptance.

This reviewed public report follows the earlier BUILD/HOST implementation session. Full logs, account/profile inventories, input records, recordings, frame images and save manifests remain private. Artifact names and hashes below identify private originals, not files published with this report. See [public evidence policy](../../docs/public-evidence.md).

## Scope and pinned conditions

Each run used one original SPORE authority and two original SPORE clients in separate prepared disposable Windows profiles, on the rendered Windows desktop, through the authenticated coordinator connection. The retained Creature fixture is the same bounded scene used for M06. The source actor dies through original starvation; these are lifecycle investigations, not a two-player attack against one shared NPC.

- Original executable: GOG GA 3.1.0.29, Win32; SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
- SDK commit: `cbf9206b9a823f0911cd9be0217104a49d72380b`.
- Injector commit: `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`.
- Host environment: Windows 11 Pro 10.0.26200, Ryzen 7 9700X, RTX 4080 SUPER; this inventory is not a measurement of the active rendering adapter.
- Fixture manifest original SHA-256: `fcc48e03c54bc71f93f259f46b36a2c2708acc344fb81b0417578bd84083c284`.
- Owner A retains global entity `12884902120`, generation `30`. Its native ID is process-local: authority/client A use `232`; client B represents remote A with `9601`. Comparing native IDs across processes is not the identity test.

## Expected versus observed

| Run | Change and expected result | Observed result and boundary |
|---|---|---|
| `native01`, bridge 0.0.31 | Keep the original dead noun instead of publishing a despawn; project death to both clients; allow a fresh dead-state baseline. | Authority and both original clients retain owner A at health 0/dead. The restarted client B applies the retained dead actor before baseline 3 becomes Connected. The authority later exhausts its 32 MiB actor-trace budget and exits 35, before any accepted respawn input. Death presentation differs visibly on client A. |
| `native02`, bridge 0.0.32 | Increase the finite actor-trace limit to 64 MiB without reducing diagnostic detail; continue through the original death confirmation and revival. | Original confirmation input succeeds. The authority shows the egg/nest sequence and returns to health 5/hunger 100. Both clients apply the same entity/generation alive state, then refuse NPC 97 source health 10 against their local effective maximum 5 and enter a network error. Continued connected play and post-respawn Join do not pass. |
| `native03`, bridge 0.0.33 | Add read-only native health-context diagnostics to explain the mismatch, preserving the projection guard. | NPC 97 changes from source age 0/max 5/health 5 to age 1/max 10/health 10. Both replicas remain age 0/max 5 and reject the incoming 10. Owner A again revives under the same identity, with scalar recovery on both clients before the rejection. The diagnostics also reveal missing combatant-state and age changes on replicas. |

## Retained death and fresh Join in native01

The original authority's first dead actor record is sequence `44193`, at actor-trace QPC time `200.8896867` seconds. The final analysis contains 166 dead authority samples, 171 on client A, 79 on the original client B, and 28 on its fresh replacement. The inspected records agree on retained owner A identity, generation and health 0. No owner A despawn is recorded in this run.

The fresh client B applies owner A's dead life state at sequence `177`, records the dead replica sample at `178`, applies its baseline at `182`, then reports Connected with baseline `3` at `183`. This establishes ordering in the native/network trace. It does not establish visible corpse presentation: the fresh client's reviewed 20-second image shows a rendered scene but no uniquely identifiable owner A corpse.

The authority records diagnostic exhaustion at sequence `60467`, followed by `engine_reported_failure` and game/worker exit **35** at `2026-09-20T23:42:17.210Z`. The attempted later input tool call failed without an accepted action. This run therefore does not establish revival. The initial live-monitor predicate missed `trace_limit`; final closed analysis includes the failure. Increasing the finite budget in .32 preserves this .31 failure as evidence.

## Original revival and why replication still stops

In native02, the authority's ordinary death confirmation produces the original nest/egg sequence. Client A records a dead application at sequence `9510`, then an alive application at `10780` with the same global entity/generation, health **5** and hunger **100**. Its subsequent sequence `10799` reports `Native motion projection failed`. The applied life-state transition is a real native scalar observation; it does not turn the subsequent replication failure into a successful session.

Native03 narrows the reason. NPC `97` is global entity `12884901985`, generation `25`. Both clients reject incoming health **10** at source tick `37892125` (client A sequence `15062`; client B `14978`). Their independently queried local effective maximum remains **5**, age **0**, base maximum **10**. Authority sequence `52477`, source tick `37892500`, observes age **1**, effective maximum **10**, health **10**. This source sample is **375 ms after** the rejected source tick; exact source context at the rejection tick was not logged. Later samples retain the new source state. No sampled authority health exceeds its own native maximum.

The raw maximum field remains 1, while the original effective getter applies native context. Base maximum 10, species-health contribution 0, herd override 0 and stage brain 0 remain consistent in the compared observations. The evidence supports an unprojected native age/effective-maximum change. Raising the replica cap or clamping source health would conceal that disagreement.

The same diagnostic run exposes two further differences:

- At owner A's death, the authority records `mbDead=true` and combatant state **2**. Replicas set the dead flag but retain combatant state **0**.
- Original revival gives owner A age **0** and effective maximum **5**. Client A retains age **1**/maximum **10**. Client B's remote A also retains its alpha flag and effective maximum **12.5**. Scalar health/dead agreement does not imply matching native age, model, behavior or health context.

The closed .33 traces contain **9,570 / 9,078 / 9,045** health-context observations for authority/client A/client B. All reported getter bindings validate; no sampled native health exceeds that process's own effective maximum. All three traces end in `trace_stop`. The .33 instrumentation observes the original getters and fields; it does not change wire state, bypass health limits or invoke death/revival gameplay handlers.

## Independent visual review

Windows Graphics Capture targeted each recorded process/window separately. All recorder completions report exit **0** without wall timeout. Selected full-resolution frames were extracted and independently inspected; neither video containers nor trace scalars were accepted as visual proof.

| Run and inspected video timestamps | Visible result |
|---|---|
| native01 authority 150.033 s and 190 s; client A 150.033 s and 190 s | Authority shows the inverted death pose and original starvation confirmation. Client A remains upright in normal gameplay view with HUD health 0. **Death presentation parity fails.** |
| native01 fresh client B 0 s and 20 s | First frame is a white transition view; 20 s shows the rendered scene. Retained dead actor identity/corpse presentation is not visually established. |
| native02 authority 175 s, 187 s, 195 s and 220 s; client A 175 s and 220 s | Authority progresses from death confirmation to an egg, then a small living creature in the nest with HUD health 5/full hunger. Client A goes from health 0 to 5/full hunger with green healing effects but remains at the earlier camera location. |
| native03 authority 10.033 s, 28 s, 37 s, 43 s and 50 s; client A 10 s and 43 s | Authority shows death confirmation, then an egg, then a small upright creature in the nest and finally the normal HUD at health 5/full hunger. Client A remains upright at death, later showing health 5/full hunger and healing effects in the earlier view. |

The independent reviews inspect **12 / 6 / 7** frames respectively, not every animation frame. The exact shell-breaking instant is not among the selected frames. Native03 video begins late, with the death dialog already present. Client B footage is visually reviewed only for native01 in this bounded review; native02/03 client B metadata and video hashes are retained without a visual-parity claim.

Capture request UTC precedes recorder startup, and input metadata is written after the input tool returns. Video PTS, actor QPC and input timestamps are therefore not precisely synchronized. The report uses exact video timestamps for image observations and approximate UTC differences only to select frames. Compositor cadence is not native frame timing or network latency. No audio was recorded.

The private reviews retain the exact extraction arguments and frame/log hashes. The analysis command form was:

```powershell
ffmpeg -hide_banner -loglevel info -ss $Seconds -copyts -i $PrivateVideo -vf showinfo -frames:v 1 -n $PrivateFrame
```

All **25** frame extractions exited **0**. Each saved image was then visually inspected. No new native process or input was performed by the independent review.

## Closure and preserved data

Native01 authority fails as described; its original/restarted client processes close through requested shutdown with exit 0. All native02 and native03 game/worker processes record requested shutdown and exit **0**. The native03 authority/client A/client B game exits occur at `00:03:08.961Z`, `00:03:08.516Z`, and `00:03:08.129Z` on 2026-09-21. These are observed process exits, not merely queued shutdown acknowledgments.

Each run's closed verification records all **29** personal source files unchanged, zero recognized running workers, and each of the three disposable profile trees matching the fixture: **16 Games files and 0 Creations files per profile**. This protects the recorded fixture and personal data; it does not qualify persistence or checkpoint recovery beyond the existing milestones.

## Provenance: private original SHA-256 values

These hashes identify unredacted private originals. They are not public-export hashes and do not imply that captures, personal paths or full logs are published here.

| Private original artifact | SHA-256 |
|---|---|
| native01 final life-state analysis | `7962308aa56311570fe4de2b67741e5c385956f16fb54ab7434d0688429ae91e` |
| native01 independent visual review | `dce56196a281d9179568cf6f1974b6f29d4c7c4ebeda8861e8009e97ef0966f9` |
| native02 source/payload manifest | `f441c4f8e5c99735db14a14769b7b82ff72f7b4891207b431987589eeebd1d7b` |
| native02 independent visual review, including closed client transition/error records | `bfbe39794cddeb0792051cdf0a7c7f7da62199398dc1daac1eb2bbefbb8c80ff` |
| native03 source/payload manifest | `fd9584b508416a304771ebd8b206afb693f6d1277f38fe5e0a075f18673be8a2` |
| native03 closed health-context analysis | `f6d299a2f1083ed78355da61619808db54c88d41bdd8ce76c72caa629863283f` |
| native03 independent visual review | `3c05edc901b0b122f31f4145f5b1b22c94de706c1551702b1253d22db4248d92` |
| native03 authority actor trace | `cb61548d7aba893f9c25db4932e4e3e65b907f9ec8ec4932520556b514f218a6` |
| native03 client A actor trace | `a875057f2bb601b8acbe52bb84d393853f6a3dfc919f65e58d0e34baad3eb44c` |
| native03 client B actor trace | `2ce57050ef2665a26609a0f28cf849acc946f1d0c01c3feca1850081b0b08ded` |
| native01 closed fixture save check | `ea912afabf157d102680791a04342d24cddb8c426955cba8fa19bb23b0c6c5be` |
| native02 closed fixture save check | `c09e1f8d280011e98a83a8a45053b1ae2c7147750bb9730630bc90a46e7073af` |
| native03 closed fixture save check | `8e539119d856845d1cc3974e5936770ad546b93f8ed1401469e8a43be96e5375` |

## Remaining gate and next bounded experiment

This report freezes native01–03 evidence. A subsequent bridge .34/schema 3 implementation is being prepared to carry age, alpha/combatant state and observed scale; its new native acceptance is **pending** at this report's cutoff. It does not retroactively change the .31–.33 failures or visual findings recorded here.

The smallest next implementation must qualify the original age/health-context projection and lifecycle presentation while preserving native gameplay ownership and the existing maximum-health refusal. It must not replay original death/revival reward or progression callbacks merely to obtain an animation. Then repeat a bounded death/original-revival sequence, requiring continued connected replicas, matching supported native context and inspected presentation, followed by a fresh post-respawn Join.

The full [M07 acceptance protocol](../../tests/engine/M07.md) remains unchanged. **NOT RUN in these three lifecycle experiments:** both players attacking the same NPC with response to either owner; one final native death/reward outcome; simultaneous contested pickup awarding once; post-encounter reconnect without restored victim or duplicated reward; configured actual latency/loss repeat; and the unauthorized real-client encounter challenge. Healthy connected play after original revival and fresh post-respawn Join remain **not accepted**. The fresh retained-dead baseline in native01 is narrower than those gates. No complete encounter, pickup/reward uniqueness, B respawn, full campaign or milestone completion is claimed.
