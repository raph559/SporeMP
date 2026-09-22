# M07 original corpse and revival presentation

Evidence class: **STATIC + BUILD + HOST ABI + bounded NATIVE**. [Native10](../evidence/2026-09-21-m07-completion/native10-combat.md) observes the original corpse pose on both clients for its recorded fixture. Native13 exposes a separate admission error after original pool reuse; the narrow .42 correction below is **IMPLEMENTED_NOT_RUN** at this documentation boundary. Neither result completes M07 acceptance. [The native05 report](../evidence/2026-09-21-m07-encounter/native05-combat.md) preserves the prior mismatch: the authority displayed the defeated NPC on its side while client A's scalar-dead NPC remained upright.

The narrow implementation is `src/bridge/native_life_presentation.cpp`, with calling conventions shared through `native_life_abi.h`. It projects the original pose for an already-admitted corpse and the original revival idle. It does not reproduce a cause-specific falling reaction, replay lethal gameplay, manufacture a reward, or drive the game-over interface.

## Pinned evidence

- Original GA executable SHA-256: `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`, preferred image base `00400000`. Addresses below are preferred virtual addresses; the helper uses image-relative addresses after the existing executable/content/loader compatibility gate.
- ModAPI SDK: `cbf9206b9a823f0911cd9be0217104a49d72380b`. Relevant declarations are `Simulator/cCreatureBase.h`, `Simulator/cCreatureAnimal.h`, `Anim/AnimatedCreature.h`, `Anim/anim_cid.h`, `Swarm/IVisualEffect.h`, and `SourceCode/DLL/AddressesAudio.cpp`.
- Ghidra `12.1.3`, JDK `21.0.12.1+1`; exporter `SporeM03Audit.java` SHA-256 `b2809b1726c1e2b5d9facda7681a22c579248ca42602c2b97890de308a87e221`.
- Private, ignored exports and command/exit manifests are under `local/m03-static/`. No native process, game data mutation, desktop input, or game asset publication was used for this audit. Assembly determines calling conventions; inferred decompiler signatures are not accepted on their own.

The decisive export commands, each expected and observed exit **0**, were:

```powershell
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-corpse-presentation-tick -ReuseDatabase -Addresses D8E380,D7E9F0,C0C240
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-life-animation-leaves -ReuseDatabase -Addresses A029B0,A02990,A027C0,A02900,A028E0,A02920,A05100,A008C0,A04840,A02B00
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-life-model-core -ReuseDatabase -Addresses A0C5D0,A029D0,A028C0,A05090,A00130,A00070,A003D0,9FFFC0,9FFDE0
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-life-core-dependencies -ReuseDatabase -Addresses A0C1D0,A01A90,9FFF70,9FF690,9FF630,A048D0
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-life-resource-leaves -ReuseDatabase -Addresses 9A19E0,9A2F90,9AB040,99C880,9A3540,9AE090,A01750
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-life-clear-effects -ReuseDatabase -Addresses 9A1680,9A7FF0,9FF7C0,9A34C0,99CF70,9C4BE0,99EFC0,A005B0
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-life-model-cleanup -ReuseDatabase -Addresses 9C8830,9C4B10,D01980
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-life-effect-stop -ReuseDatabase -Addresses 9FBC50,9FBCB0
```

Earlier decisive exports are `m07-death-presentation-01` (`C08210`, `C02D00`, `C122F0`), `m07-death-presentation-02` (`C12470`, `C148B0`), `m07-death-presentation-03` (`C103B0`), `m07-death-reaction-route` (`C0C710`, `C10390`), `m07-death-behavior-route` (`C0B320`, `C0B380`, `C101B0`), and `m07-death-native-calls/D85B9D`, whose containing function is `D85B80`. Requests at an interior address elsewhere in the exploratory exports are not treated as independent callable functions.

## Why the scalar corpse stayed upright

`C08210` is the original whole-animal death handler. It changes behavior/target/motion state, writes health zero and `mbDead` at `+B5E`, clears effects, and changes original manager/physics membership. It does not start a death animation. The ordinary locomotion selection at `C148B0` requires `mbDead == false`, so setting only the replicated scalar state also prevents it from choosing a new living animation.

The native death behavior descriptor at `01586D68` has behavior ID `02DD78EF`, activation `D85B80`, tick `D8E380`, and deactivation `D7E9F0`. Activation records whether the animal was already dead before invoking whole death. For an already-dead ordinary animal it selects `05807346`; metadata byte `animal->field_E84 + 388` selects alternate `05807345` when nonzero. Fresh lethal reactions use different resources for ordinary, selected damage types, and water conditions. The current wire state does not contain the cause/phase needed to reproduce those reactions.

The death tick passes its selected animation, block index `-1`, and a blend flag to original `cCreatureBase::PlayAnimation` at `C12470`. The rest of that tick includes opacity/removal timers and nearby-creature queries, so the helper does not call it. The helper uses the already-dead ordinary selection and refuses `alternate_corpse_style_unsupported` when the native metadata selects the alternate corpse. It also respects the tick's native `+166D` animation-suppression byte by refusing `native_animation_suppressed` when set.

Original revival `C02D00` calls `C0BB00` to clear `mbDead`, performs manager/behavior work, and calls `C122F0(02481DE5, true, -1)`. The helper reproduces only that model selection and queue behavior. It never calls either whole revival function.

## Model bindings and callbacks

The current pinned model vtable is **`01448AF0`**. The SDK header's historical comment `0144CEC0` is not used. All model calls receive the primary `AnimatedCreature*` in ECX; there is no Creature secondary-base adjustment in these calls.

| Operation | Vtable slot / preferred address | Proven ABI and result |
|---|---|---|
| Map original animation ID | `C0C710` | cdecl `(Animal*, uint32, uint32*)`, caller cleans 12 bytes, bool AL means remapped; false may leave the original ID valid |
| Load animation | `+08 / A0C5D0` | thiscall `(uint32, int*)`, `ret 8`, index EAX; zero means unavailable |
| Set mode | `+0C / A029B0` | thiscall `(index, int)`, `ret 8`, meaningful bool AL via `A00130` |
| Start animation | `+18 / A029D0` | thiscall `(index)`, `ret 4`, meaningful bool AL via `A01A90` |
| Set query value | `+3C / A028C0` | thiscall `(index, 0)`, `ret 8`, meaningful bool AL via `9FFF70`, writes query `+BC` |
| Set original ID | `+40 / A028E0` | thiscall `(index, uint32)`, `ret 8`, meaningful bool AL via `9FFFC0`, writes query `+D8` |
| Clear gameplay callback | `+48 / A02920` | thiscall `(index, 0)`, `ret 8`, meaningful bool AL via `A00070`, writes query `+DC` |
| Read current animation | `+58 / A05090` | thiscall with four optional output pointers, `ret 10h`; no defined success return, so the SDK's inferred int return is ignored |
| Read prior native mode | `A048D0` | thiscall `(index)`, `ret 4`, bool AL; index zero means current query |
| Clear queued animation | `A02B00` | thiscall with no stack arguments, tail-call to `A008C0` |

The helper checks the exact vtable and used slots, plus instruction prefixes (complete byte arrays are in the implementation). Representative prefixes are `A0C5D0: 8B 44 24 08 8B 54 24 04`, `A029D0: 8B 44 24 04 8B 89 84 01 00 00`, `A05090: 56 57 8B F9 8B 87 84 01 00 00`, `A02B00: 8B 89 84 01 00 00 85 C9 74 05`, and `C0C710: 56 E8 FA F1 F4 FF 3D 10 4C 65 01`. These local checks supplement the outer whole-executable identity gate.

`C12470` and `C122F0` normally call `C103B0`, which registers `C10390` and metadata arguments as the animation query's callback. That callback reaches `C101B0` and can create original behavior/effect work through `BCA980`. A block-index argument of `-1` does not make the wrapper presentation-only because metadata can override it. The helper therefore calls the qualified model operations directly and clears query `+DC` through the native setter **before** starting the animation. It leaves the separate native animation-choice callback at `+E8` intact.

The bool setter implementations return one when either resolved native query exists and zero when both are absent. Every required configuration result is checked before `StartAnimation`. The index must have a valid native slot byte `1..16`. `A01A90` has meaningful AL success but assumes a valid query for nonzero indices; preceding successful query setters establish that prerequisite on the same engine thread.

Native queue clearing is not side-effect-free: `A008C0` visits sixteen queue slots and `A003D0` resets queries and releases animation resources. The audited descendants perform native model effect cleanup as well. `9A19E0 -> 9A7FF0 -> 9C8830` reaches visual effect stop (`9FBC50`, matching `IVisualEffect::Stop` slot `0C`) and the original audio system (`9FBCB0 -> A20670`, SDK `AudioSystem::Get`). No creature gameplay callback is registered or dispatched by this path. `A01750` preserves native scheduling/blending and uses those same query cleanup functions. The model core retains normal visual/audio cleanup rather than replacing it with invented scalar resets.

## Lifetime, state and integration

The module requires the client engine thread and the existing `apply_state` scope. The scene has already admitted the exact entity incarnation and validated scalar state before the presentation call. The helper independently requires a live census member, current Creature mode, the pinned Animal vtable, a readable current intrusive `mpAnimatedCreature`, its pinned vtable, its bounded `anim_qb` (`+184`, size `FD0`), and readable `anim_cid` (`+17C`, size `1930`) whose `pCreature` refers back to the same model. Original resource loading depends on `mpAnimWorld` (`+190`), its `+1C` context, and that context's `+14` resource manager. The current model owns those dependencies; no pointer is cached by the helper or serialized.

The scene binding deduplicates by admitted incarnation, applied life state, and current model identity. A fresh dead baseline, a life transition, or model replacement invokes presentation once. An initial living baseline leaves the existing native animation untouched. Revival of a locally dead noun still invokes the qualified living path. Failed helper calls emit `native_life_presentation_rejected` with an explicit reason and fail the scene application; they are not converted into an accepted upright corpse.

The shared pure `native_life_state_admitted` guard requires a finite health value and agreement between requested and native `mbDead`. A corpse requires health exactly zero and either qualified combatant state `0` or `2`; a living presentation requires positive health and state `0`. The function validates those independent source values and does not change them. Other combatant values, a mismatched dead flag, invalid health, or an unsupported model/archetype remain rejected. The existing scene and wire validators continue to admit the separate native zero-health/not-yet-dead interval; that interval alone does not authorize a corpse or revival animation.

`native_life_presentation` records requested/original and mapped IDs, native index, mode, setter/start results and immediate current-animation readback. A started query is not evidence that a rendered frame has displayed it. The event explicitly records `visual_acceptance: NOT_INFERRED`; later animation readback and actual captured frames are required.

## Validation and next native observation

The private command `pwsh -NoProfile -File local/m07-life-helper-check/build-check.ps1` compiled the helper in isolation with the pinned MSVC `14.44.35207` Win32 toolchain and Windows SDK `10.0.26100.0`; it did not replace the active native bridge or launch SPORE. Helper compile, fixture build and fixture run all returned **0**. Nine HOST ABI checks passed, including all four readback output positions, null middle outputs, true/false AL, exact ECX receivers, and 4096 mixed cdecl/thiscall sequences with unchanged stack and sentinels. Private `manifest.json` records exact compiler arguments and source hashes. `native_life_abi_host` registers the same fixture with CTest, labeled `host;fixture;not-native`.

Native10 subsequently records one corpse animation application on each client and retains it in all 72/220 counted subsequent corpse samples, with inspected original-process frames. This remains bounded to that recorded corpse; it does not validate every life-state or model combination. Cause-specific falling reactions, death fade/opacity phase, game-over UI, and baby/adult model regeneration remain separate unverified capabilities.

## Native13 independent corpse state and the .42 correction

Native13/.41 returns the former NPC79 to the original retained pool at source sequence `24571` (pool counter `0 -> 1`). The next published incarnation uses the same native ID and global entity `12884901967`, but generation **38**, herd **1745**, and species `[109087434,731352134,1080189440]`. Its live herd/profile and original factory admissions succeed. In **59** authority samples from source sequence `29113` through `35067` (58.6602632 seconds between first and last), the original noun remains `mbDead=true`, health `0`, and combatant state `0`. It then returns to the original pool at sequence `35136` (counter `1 -> 2`). This is an observed active incarnation, not a vanished entity or a brief zero-health/death-flag race.

Client A's original factory creates local noun `9640` and reads back the correct independent source values at sequences `9349/9350`, source tick `73653546`. Its helper rejects `life_state_mismatch` at sequence `9351` because .41 incorrectly requires every corpse's combatant state to equal `2`. Fresh B likewise creates local noun `9606`, applies the correct scalar/context values at sequences `195/196`, source tick `73682093`, and rejects at `197`; its baseline then fails. The source continues without this error. The scalar projection and original creation succeeded before the additional presentation guard failed. No authority corpse-animation observation is inferred from its scalar samples.

The pinned original selection supports removing only that invented coupling:

- `D85B8E` reads `Animal+B5E` (`mbDead`), and `D85B94` stores the prior value in behavior state `+16`. The already-dead branch calls `C0C240` at `D85D98` and selects ordinary corpse idle `05807346` at `D85DA4`, or the qualified metadata's alternate resource. It does not select from `cCombatant+34` (`Animal+5DC`).
- The retained death tick checks `mbDead` at `D8E487`, checks the existing native animation-suppression byte `Animal+166D` at `D8E4DE`, and dispatches the selected animation through `C12470` at `D8E4F4`. The animation-selection path has no combatant-state-two prerequisite. The helper continues to use only its previously qualified model calls, without invoking this whole behavior tick.

The .42 change uses the same pure predicate in production and the existing HOST life fixture. Sixteen table cases cover the exact native13 state, the earlier state-two corpse, valid living revival, both dead-flag mismatches, incorrect positive/negative/zero health, unknown combatant values, NaN, and positive/negative infinity. The nine existing calling-convention checks remain. **The new tests and .42 native case are NOT RUN by this author at this boundary; the root build/test run records their eventual result.** No ABI, native method, model callback, protocol field, scene-selection rule, or authoritative gameplay value is changed. The next bounded native observation must pass dynamic creation and a fresh baseline for this qualified state while preserving corpse animation readback and the prior reward/lifetime guards.

Original private trace hashes for this diagnosis (SHA-256; raw paths and account details remain private):

| Original artifact | SHA-256 |
|---|---|
| Native13 final authority trace | `8b89da54a1b93b3daf7645eff465e5a1b29530fc25f1cf3e00f78a748c7a75e3` |
| Native13 final client A trace | `7d55e54ae3681cebd9150b998f018c05af4d22287d874c93dc67c01e2a37e6bd` |
| Native13 fresh client B trace | `ef4f6c2788fc6576c1080fb907de333622e9bba5be4a16ef614ef29bdec25855` |
| Native13 original client B closed trace | `cd3c8909a4fdcc70e6bff9f8ab979338fd5d337ae7c0f82672265d1e79538032` |
| Pinned D85B80 assembly export (`D85B9D` request) | `80ef63695a8545b02f50cd2001a84e2eead1d487afbb9d9ded4113c16913708c` |
| Pinned D8E380 assembly export | `3df743f6394ccc4c111d750c30a73e58fc854c160a7223c06bc5fe065c8d88d9` |

## Native14 verification of the .42 correction

The subsequent root Release build and all 10 HOST/FIXTURE CTest targets pass (13.25 seconds). Native14 then actually exercises a dynamically admitted corpse with health zero, dead=true and combatant state zero on both clients. Each applies the existing corpse helper once and retains the animation in all 60 subsequent samples. This closes the diagnosed state-admission regression for dynamic spawning; native14 does not attempt a fresh Join containing that corpse.

The same run projects original A death and same-noun revival on both clients, with one corpse/idle transition each, matching independently coincident source ticks and no scene rejection. Inspected recordings show original authority death, egg and hatching, and client A's corpse and restored health. They do not clearly establish remote A's baby model on both views. No post-revival movement request was accepted before shutdown. See the [reviewed native14 report](../evidence/2026-09-21-m07-completion/native14-lifecycle.md) for exact counts, closed hashes, capture limits and clean-save evidence; this is a bounded presentation result, not full M07 acceptance.

## Source-only limitation: pending death during adoption or model replacement

The bounded source review during the frozen .45/Native20 run identifies an unsupported combination of otherwise independent guards. **This is source-only reasoning, not an observed Native20 failure. No production code was changed and no new native reproduction was run for this finding.** It remains visible for M11 arbitrary lifecycle coverage, outside the stable post-death incarnation selected for the bounded M07 G05 Join check.

The scene and wire validators deliberately accept the original intermediate state `life_state=0, health=0`: Native11 observed lethal damage reaching zero before the native `mbDead` flag changed. In [native_scene.cpp](../src/bridge/native_scene.cpp), `apply_bound_pose` requests living presentation when it adopts a locally dead noun or observes a model replacement. If the incoming authoritative state is that zero-health/not-yet-dead interval, the scalar application succeeds but [native_life_state_admitted](../src/bridge/native_life_presentation.h) rejects the requested living animation because it requires positive health. The result is `native_life_presentation_rejected` with `life_state_mismatch`, followed by scene projection refusal. The ordinary unchanged-model pending-death update does not take this presentation branch, so this finding does not contradict the already observed normal lethal transitions.

The smallest future experiment is a focused HOST fixture for the presentation decision and retained binding state: combine incoming zero-health/not-yet-dead with (a) a locally dead adopted noun and (b) a changed model, then advance the same authoritative incarnation to explicit death or positive health. Record the current refusal first. A candidate correction would defer animation during the intermediate state while retaining the pending presentation requirement until the subsequent qualified state; simply marking the animation as presented could incorrectly suppress that later transition. Such a change must preserve the original health/dead values, model/lifetime guards and callback suppression, and must not infer a corpse or invoke whole death/revival gameplay.

**Native reproduction and correction: NOT RUN.** Their prerequisite is a bounded original lethal transition coinciding with qualified baseline adoption or an original model replacement, with before/after scalar and model identity readbacks and inspected presentation. A synthetic HOST fixture alone cannot establish that native combination. This limitation does not broaden current respawn, arbitrary model replacement, player death-interface or general progression support.
