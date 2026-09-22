# M07 native Creature pool lifetime

Updated 2026-09-21. Bridge **0.0.41** introduced the native pool change below; its initial BUILD/HOST boundary is retained in this document. [Native18/.44](../evidence/2026-09-21-m07-completion/native18-encounter.md) subsequently records the original pool counter change, beneficiary retirement, authoritative despawn, both client removals and later reuse as a distinct generation without inherited beneficiary. This qualifies the recorded Creature fixture. The complete milestone status is tracked in [M07](../tests/engine/M07.md).

This binding is limited to the pinned GA 3.1.0.29 executable, SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, and SDK commit `cbf9206b9a823f0911cd9be0217104a49d72380b`. Original processes continue to execute pooling, AI, movement, combat and feeding. The bridge observes lifetime and projects authoritative results.

## Reproduction and diagnosis

Native12/.40 reached actual NPC79 death and projected its corpse state on both clients. Both clients also read back the intermediate health0/deadfalse state at source tick 72497062, followed by native deadtrue at 72497125. This verifies the preceding zero-health capture fix for that transition.

During subsequent original feeding, authority sequence 27003 rejected **NPC75**, an unrelated living animal, as `herd_missing`. Its last admitted sample had herd 1776 and health 6/max 6. The rejected noun retained species/profile/archetype/animated-creature references and health 6, but had no herd, disabled spatial state, no model/model-world, and position `[0,0,0]`. Publication stopped at 27005. The existing adapter recognized only completely blank preallocated shells; it treated this initialized inactive noun as unsupported active gameplay.

Pinned code identifies a separate original pool-return route. `C03280` may use `ACD350` → `ACCEC0` and return without `GameNounManager::DestroyInstance`. Animal cleanup virtual+44 (`C083D0`) is not the pool-return observer point. A destruction-only identity fence therefore misses recycling of a retained noun.

Native12 did not log the fixed flag or native pool counter. The new predicates below are derived from the pinned original route and require the next native test. The retained traces end with `trace_stop`, match their archive hashes, and have game/supervisor exits0. Raw traces and static exports remain private under the [public evidence policy](public-evidence.md).

## Original ABI, state and lifetime

`ACCEC0` has ABI **`bool __thiscall(pool_manager*, noun*)`**: manager in ECX, one pointer stack argument, boolean in AL, RET4 on false and true returns. The exact first 11 bytes are `83 EC 78 53 8B 9C 24 80 00 00 00`. Its successful lookup retains the noun pointer in the original pool vector at entry+8 and clears the active byte at entry+4; it does not erase that retained reference.

For the matched Creature/Animal, the original routine fixes and disables the spatial object, calls original SetPosition with the zero vector, resets the render descriptor, increments `cCreatureBase::field_E54`, removes the Animal from its herd and calls `C04700(0)` to release its herd reference. Constructor `C1FA30` initializes E54 to 0. Original Animal Update `C0AE30` returns immediately when mHerd is null. Native12 independently observes the resulting missing render model/model-world while the species and animated-creature references remain.

The hook in [native_actors.cpp](../src/bridge/native_actors.cpp) calls the original exactly once. It captures native ID/counter only after resolving the noun in the current manager census with the pinned Animal vtable and E54 layout. After a true return it resolves the same native ID/address again. Only a changed E54 counter retires the scene identity, actor-command binding, replica binding and observed pickup beneficiary together. Foreign-thread or unqualified calls retain the original behavior. No saved pointer alone authorizes a post-call dereference.

The signature is centralized in [native_pool_abi.h](../src/bridge/native_pool_abi.h); [HOST ABI tests](../tests/unit/native_pool_abi_tests.cpp) check the ECX receiver, stack argument, boolean return, balanced stack and both success/failure paths. They do not execute SPORE.

## Scene guard and reuse fences

[native_scene.cpp](../src/bridge/native_scene.cpp) excludes an initialized retained pool noun only when every checked condition holds:

- Pinned Animal/scalar/spatial bindings and SDK field offsets; non-avatar; nonzero original E54 pool-cycle witness.
- No herd, but retained species key/profile/archetype/animated-creature references.
- Fixed=true, enabled=false, model=null, model-world=null and exactly zero position.

An active, rendered, nonzero-position or otherwise unqualified herdless animal still fails admission. A controlled actor matching the pooled signature is explicitly refused. Ordinary death alone does not meet this signature. The classifier checks pointer presence without dereferencing a dormant profile/model.

The capture registry stores E54 with each native ID/address/incarnation. A changed counter allocates a new generation even when both ID and address are reused between publications. A qualified inactive observation retires the old incarnation. Incoming actions compare the current counter with the published incarnation before command conversion; the hook separately invalidates queued commands and beneficiary records. Existing publisher/session paths send despawn then a new entity for a new generation and reject stale resurrection.

SDK offsets checked per noun include Animal+E54 counter, +1674 herd, +131 fixed, +135 enabled, +15C model and +160 model-world. New scene instruction guards are:

| RVA | Exact bytes | Evidence |
|---|---|---|
| `6CCFDF` | `C6 46 71 01 88 5E 75` | Original fixed/disabled writes |
| `6CD0D5` | `FF 86 54 0E 00 00` | Original E54 increment |
| `81FD5D` | `89 9E 54 0E 00 00` | Constructor E54 initialization |
| `80472F` | `89 BE 74 16 00 00` | Intrusive herd assignment |
| `80AE33` | `83 BE 74 16 00 00 00` | Original inactive Update gate |

All five arrays matched the pinned PE in an independent read-only byte check. Actor hook guards additionally check 16 bytes at RVAs 6CCEC0,6CD0D5 and6CD13E, including the AL-success/RET4 tail. Scene initialization runs after actor hooks but before replica detours; these interior scene checks do not inspect overwritten actor entry bytes.

## Pickup telemetry and remaining native gate

Authority now emits `scene_native_pickup_state_published` after each successful bounded capture for every fed corpse, preserving its actual source tick and native scalar rereads. A matching client revision establishes receipt/application. Clients emit `scene_native_pickup_state_applied` on fed-flag or food change, a fresh fed baseline, or beneficiary metadata change. Food/first-feed values are native rereads; beneficiary remains source metadata. No food rounding, gameplay freeze, consumption replay or fabricated reward is introduced. The 128-entity scene and trace-budget bounds remain enforced.

The `.41` source/header hashes at freeze were `23b000a4c636a6bbbfa67b03e24f91af789c980e7b1144635caeae0f05ae7553` and `2486773607591ecd7d28321bf88204e6c0611bcd507a6b7140a05431c4ebe3fd`, respectively. These identify original development files, not a redacted export. Static exports covering ACCEC0, C04700, C1FA30 and the render-reset descendants all completed with exit0; `git diff --check` passed. The combined Release build and all 10 HOST tests passed in 13.37s, including 2662 network assertions.

**Original .41 acceptance plan:** native execution was NOT RUN at implementation time. The required observations were pool-return counter changes, qualified inactive exclusion, authoritative removal without quarantine, safe generation handling if a noun reappears, and continued exact-tick nutrition agreement. Native18 supplies the bounded evidence linked above. Unknown active herdless nouns remain rejected. Complete encounter, contested nutrition, reconnect and impairment evidence remain separate M07 gates.
