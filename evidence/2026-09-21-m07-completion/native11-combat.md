# Native11: inactive-herd creation observed; terminal projection fails

2026-09-21, bridge **0.0.39**, wire schema **4**, one original Creature authority and two authenticated clients. **G03 fails in this run; contested pickup and the dependent reconnect are NOT RUN. Full M07 remains IN_PROGRESS / NOT_VERIFIED.** The original encounter reaches a lethal hit and one 7-DNA reward, but capture rejects the native interval between zero health and the later death flag. Separately, client A successfully exercises the guarded existing-inactive-herd creation path.

This is reviewed **NATIVE / NETWORK-REAL** evidence for the pinned fixture. The [sanitized summary](native11-summary.json) contains hashes and bounded results. Raw traces, exact private commands, process metadata and recordings remain under ignored `local/m07-completion-2026-09-21/`. Successful trace analysis and recorder closure do not grant visual acceptance.

## Original combat and the missing terminal projection

The shared target is native **79**, global entity **12884901967**, generation **7**, authority actor **3**, initially **6 HP**. The actual client `engage` submissions use a five-second B lead and a 25-second observation. Closed original damage chains show B applying four one-HP hits (scopes **2514, 2671, 2829, 2976**) and A applying two (scopes **2915, 3009**). Both owners therefore damage the same original NPC. NPC79 damages B twice for **0.5 HP**; this run does not establish a target switch between both owners.

| Authority event | Exact observation |
|---|---|
| Damage scope 3009, sequences 14947–14954 | A's original hit takes NPC79 **1 -> 0 HP**; `damage_state` 14950 and `animal_damage_state` 14953 still report **dead=false**, food **100**, not previously eaten. |
| Original DNA call, sequences 14951/14952 | One matched entry/return gives **A 0 -> 7 DNA**, amount **7**, inside the lethal damage scope. This is a combat reward, not a pickup. |
| Capture rejection, sequence 14960 | `unsupported_native_values` for the same global entity/native ID, at authority trace time **84.6174019 seconds**. |
| Network failure, sequence 14961 | `Native scene capture failed: unsupported_entity`; subsequent terminal state is not published. |
| Later original actor readback, sequence 14969 | **0 HP/dead=true**, at **84.7368041 seconds**, **0.1194022 seconds after the rejection**. This is the first sampled dead actor state, not a measurement of the exact native flag-write time. |

The observed intermediate state conflicts with the .39 validator's requirement for positive health whenever the native death flag is false. The next bounded repair must preserve the original zero-HP/not-yet-dead state and fence new actions during it; it must not manufacture an early death flag. This report does not accept the later implementation or its native test.

The exact sampled positions make the stale terminal projection visible:

| Observation | Native health/life | Position | Source tick |
|---|---|---|---:|
| Last authority periodic NPC scene sample, sequence 14689 | 2 HP, alive | `[-406.245972, 179.25206, 249.526627]` | 72088281 |
| Last client A NPC scene sample, sequence 4005 | 1 HP, alive | `[-406.253479, 179.179504, 249.53656]` | 72089109 |
| Last client B NPC scene sample, sequence 3963 | 1 HP, alive | `[-406.253479, 179.179504, 249.53656]` | 72089109 |
| First later authority actor corpse readback, sequence 14969 | 0 HP, dead | `[-406.659058, 179.356552, 249.789017]` | Not a published scene sample |

No terminal native scene sample exists on any of the three processes, and neither client records the NPC's dead projection. The periodic authority sampling cadence explains why its last sampled scene health is 2 while the clients receive a later 1-HP update; these are different source ticks. There are **zero native pickup order, pickup tick, first-feed or pickup reward events**. The harness correctly reports `bounded_encounter_observed=false`; its `operation_completed=true` means only that the observation procedure finished.

## One actual inactive-herd fallback

Client A's first new source NPC, native **69**, global **12884901957**, generation **34**, has no eligible living local template at sequence **1854**. The fallback verifies the current manager's ownership of the existing profile, the exact species key **[108668588, 731352134, 1080189440]**, and base archetype **4130283391**. Its complete census inspects **8,048** nouns within the **16,384** bound and identifies one exact herd **1772**.

The selected herd is inactive (`enabled_byte=0`); `herd_enable_called=false`. Preflight **1861** passes, the original factory returns qualified local native **9637** at **1862**, native context readback **1863** shows **6 HP / native maximum 6**, age **1**, scale **0.518023372**, and herd **1772**, and spawn **1864** records the authoritative entity. Position **[-372.070801, 235.877411, 249.032349]** matches the source state at tick **72086000**.

This is **one fallback**, followed by three additional spawns using the newly created living template. Client B creates all four source NPCs using its existing living template. The fallback result is distinct from the later NPC79 capture failure. It qualifies this existing inactive herd/profile/archetype fixture only; it does not establish arbitrary missing-profile loading, generated-archetype resolution or all-species compatibility.

## Closed verification and limits

`python local/m07-completion-2026-09-21/analyze-native11.py` returns **0** after independently checking every archived file hash, both combat snapshot prefixes, contiguous actor sequences, monotonic per-process clocks, zero foreign callbacks, healthy trace stops, detach status **0**, bridge disposal, stopped worker state and actual game/worker exits **0**. The exact source death/reward events and inactive-herd factory/readback events are retained in the private analysis JSON.

| Closed actor trace | Records | SHA-256 |
|---|---:|---|
| Authority | 16,264 | `eb796558513fe250651f3d021051f63b6c977239af4b0e8d5461d2ffe69a86f4` |
| Client A | 4,086 | `cb3b55a0295816dd47740d04392d401fb480bf7d16164040b36823bd1f28f6b6` |
| Client B | 4,040 | `a17ca8ff6b2cb9369362e1e0eaebde2cca9e15a870cbb9722c1b94bf3ce58eeb` |

The authority contains exactly one scene-capture rejection. There are no trace-limit, scene-projection or life-presentation rejections. Three target callback samples were intentionally omitted on the authority; healthy writer closure is not an exhaustive AI-history or network-loss measurement.

The overall run bound is **300 seconds** and the three exact-process WGC recording requests have **270-second maximums**. The run stops early on the native failure; all three recorders close with exit **0**, without timeout. The requested full recording duration is not claimed. Final trace durations are **119.1391669 / 96.7467779 / 95.5053311 seconds**, each relative to its own trace origin. The recordings remain private and are not used by this trace report to claim visual corpse or pickup acceptance.

All **29 personal files** are unchanged. Each disposable Games tree retains **16** unchanged files and each Creations tree remains empty; the final protected-file check recognizes zero running workers. The loaded bridge hash is `d9a1be4b25f2d6c27e8eff1642c414388e0e923ef858b22a8d5a3fe033bb95e0`; original executable SHA-256 is `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, SDK commit `cbf9206b9a823f0911cd9be0217104a49d72380b`. Other pinned source, payload and archive hashes are recorded in the sanitized summary.

G03 remains failed for native11 despite the valid original damage/reward. G04/G05 are not attempted, and this run adds no configured-loss, respawn, ownership-challenge or full M07 acceptance.
