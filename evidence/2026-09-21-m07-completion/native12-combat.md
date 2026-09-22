# Native12: death transition projected; original feeding observed; pickup acceptance incomplete

2026-09-21, bridge **0.0.40**, wire schema **4**, original Creature authority and two authenticated clients. **The bounded combat and native death transition succeed. G04 remains incomplete, G05 is NOT RUN, and full M07 remains IN_PROGRESS / NOT_VERIFIED.** Original feeding gives A actual nutrition with a legitimate zero-DNA call, but .40 leaves pickup beneficiary metadata at zero. A later unrelated detached NPC causes authority scene capture to fail.

This reviewed **NATIVE / NETWORK-REAL** report describes the frozen .40 run. The [sanitized summary](native12-summary.json) records exact trace, recording, frame and artifact hashes. Raw evidence remains under ignored `local/m07-completion-2026-09-21/`. The subsequently revised food-consumption parser is **not used to retroactively pass this run**.

## Combat and original death-state progression

An initial staged attempt ends **INCONCLUSIVE** because `B_in_native_range` is not observed. The subsequent actual shared-combat case submits both `engage` intentions with **no configured B lead**, observes for **25 seconds**, and reports `bounded_encounter_observed=true`.

Both owners damage native NPC **79**, global **12884901967**, generation **7**, authority actor **3**, from **6 HP** to **0**. A applies five one-HP hits, scopes **3753, 3867, 4018, 4169, 4289**; B applies one, scope **3935**. NPC79 targets and damages A four times for **0.5 HP** each in this fight. The earlier native10 encounter targeted B; these separate runs do not establish a target switch between both owners in one fight.

The lethal scope **4289** contains one original DNA entry/return, sequences **21583/21584**, giving **A 0 -> 7 DNA**. B remains **0**. Both replicas read the terminal state and controlled native DNA in the bounded combat snapshot, without executing another original reward call.

The .39 zero-health/late-death capture failure is corrected for this case:

| Native replica readback | Client A | Client B |
|---|---:|---:|
| 0 HP with original `dead=false`, source tick **72497062** | sequence 4546 | sequence 4465 |
| Later original `dead=true`, source tick **72497125** | sequence 4550 | sequence 4468 |
| Original corpse presentation helper | sequence 4551 | sequence 4469 |

Both clients therefore receive the actual intermediate state and then the original death flag, rather than an invented early death. The helper starts once per client and subsequent native readbacks show animation **92304198 / 0x05807346**. The authority's periodic scene sample misses the brief intermediate state; matching client readbacks establish its transmission. Four unrelated NPC69–72 spawns use living templates on each client in this run; it does not repeat native11's inactive-herd fallback.

## Contested requests and actual original nutrition

Two real authenticated pickup intentions target the same dead, previously uneaten NPC. Both original order returns are accepted while the original victory sequence is paused: A sequence **23454**, command **4**, and B sequence **23474**, command **5**. Their authority-clock separation is **0.0151442 seconds**. Original feeding begins after the operator dismisses the native victory confirmation; no artificial food or simulation pause is introduced.

The first qualified feeding tick is A's sequence **26550**, at authority trace time **156.6527763 seconds**, after both order returns. The first-feed transition occurs once in scope **4309**:

| Native evidence | Observation |
|---|---|
| Tick entry 26626 / state return 26629 | A, actor **1**, native **232**, holds the original corpse claim; `fed_on` changes **false -> true**. |
| Original DNA entry 26627 / return 26628 | Caller RVA **0x9713b2**, original amount **0**, A DNA **7 -> 7**. The call is paired and returns normally. |
| First positive nutrition callback, scope 4312, sequences 26634–26639 | Food **100 -> 99.7600021**, hunger **0 -> 0.24000001**, health **2.52796555 -> 2.55596566**. Both before/after claimants are native **232**. |

Zero DNA is the observed original result; no positive DNA bonus is inferred or manufactured. The positive resource effect is original food-to-hunger/health transfer. The closed trace contains **545** paired qualified A feeding callbacks, including **477** with food decrease and hunger or health increase; **53** of these occur before the later capture failure. There is one first-feed flag transition and one corresponding first-feed DNA call. The original remaining-food value eventually reaches **-31.0097942**, recorded without clamping or rounding.

B's original order was admitted, but this trace contains **no qualified B feeding callback**. That absence is not relabeled as an explicit native rejection. The .40 observation logic requires a positive DNA increase before recording the first-feed beneficiary, so it emits no `native_pickup_first_feed_observed` event and publishes **pickup_owner=0**. Both clients observe the eaten flag and reduced food, but the consumed-state beneficiary requirement is unmet.

The frozen pickup harness has a **40-second deadline** and ends with `operation_completed=false` / `bounded_contested_pickup_observed=false` on the native capture error. Its `both_original_orders_before_first_feed=false` diagnostic is conditional on its positive-DNA grant criterion; it is **not evidence of reversed native ordering**. The exact sequences above prove that both original returns precede feeding. The later implementation separates returned original DNA from positive nutrition, but that change is outside this run's acceptance.

## Unrelated detached-noun capture failure

Authority rejection **27003**, at **157.5964644 seconds**, names native **75**, global **12884901963**, generation **3**, with `herd_missing`. Its preceding native scene sample **26577**, tick **72543843**, shows **6 HP**, herd **1776**, position **[-425.439636, 31.5606194, 267.040344]**.

The rejected current noun has **no herd**, spatial state **disabled**, **no model or model-world reference**, position **[0, 0, 0]**, and retains its profile, archetype and animated-creature references at **6 HP**. Network error **27005** stops authority publication. These are direct observations of an initialized detached/inactive noun; qualification of its original pooling/teardown route belongs to the next guarded lifecycle increment.

Both clients then retain the last published corpse state at source tick **72544671**: **0 HP/dead**, `fed_on=1`, food **88.0901108**, beneficiary **0**. Authority native feeding continues after publication fails; this later local progress is not synchronized multiplayer acceptance. The authority's periodic NPC scene samples contain no first-fed sample before the failure, which also prevents a complete three-process sampled consumed-state comparison. Reconnect is not attempted against the failed authority.

## Exact-process visual inspection and closure

The root operator inspected all six actual WGC frames at **95 and 150 seconds into the three recordings**, with requests identifying the corresponding original game process/window. These offsets are recording times, not a subtraction of unrelated game clocks.

- At **95 seconds**, the authority shows the native letterboxed victory scene with two blue players and the green corpse. Both clients show the players and corpse; A displays **7 DNA**, approximately **3 HP**, and empty hunger, while B displays **0 DNA** and **10 HP**.
- At **150 seconds**, the authority's corpse is gone and its visible player's HUD shows **7 DNA**, full health and full hunger. Both clients still show the green corpse. A has approximately **4 HP** and slight hunger recovery; B retains **10 HP**, full hunger and **0 DNA**. This visibly matches continued original feeding alongside stalled scene publication.

The frames confirm original gameplay content and expose the later mismatch. They do not grant complete pickup/reconnect visual acceptance. Request, completion, recording and inspected-frame hashes are included in the summary; original media stay private. All three recorders exit **0** without timeout when the run closes before their **270-second** maximum. The overall bound is **300 seconds**. No native frame-timing or audio claim is made.

`python local/m07-completion-2026-09-21/analyze-native12.py` returns **0** after checking all closed archive hashes, exact combat/pickup prefixes, contiguous sequences, monotonic per-process clocks, zero foreign callbacks, paired feeding scopes, the zero-DNA call and its claim, the exact detached-noun rejection, healthy trace stops, detach status **0**, bridge disposal and game/worker exits **0**.

| Closed actor trace | Records | SHA-256 |
|---|---:|---|
| Authority | 30,794 | `8b33ee11c133a3d726e1f001db19b97e8de61d3d120f9b7b114c277718c97b3e` |
| Client A | 10,850 | `755ae5a0c7e93088eb99294ced63f60763f49f49672eea1ba6c90759f78f2ab7` |
| Client B | 10,812 | `86ee592a0425619676ffaea370b44995089af7b4593be23cbf7f3c1cbebbfed9` |

The authority omits **13** target callback samples intentionally; no trace-limit or foreign callback is recorded. All **29 personal files** and each disposable Games tree's **16** files remain unchanged; Creations trees remain empty and the final check recognizes zero workers. Loaded bridge SHA-256 is `c5a476e76b69c43768b5a7d479818d1989bbe831d876e48c30b7efdb9ce2057e`; original executable SHA-256 is `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, SDK commit `cbf9206b9a823f0911cd9be0217104a49d72380b`. The frozen pickup harness hash is `f8656b8b360839633e346d85969fb842260df030c06b8ca4dd7c5f69804812b0`.

This run qualifies the bounded original combat and observed pending-death projection. It leaves contested consumed-state attribution, subsequent reconnect, the newly identified detached-noun path, configured loss and full M07 acceptance open.
