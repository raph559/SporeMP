# M07 retained native death state and diagnostics

Updated 2026-09-21. **Native04/.34 verifies the bounded scalar/scale lifecycle correction:** retained death, original same-noun revival, age/effective-health agreement, fresh post-respawn Join and subsequent native move/jump. Full death/game-over and baby/adult model presentation remain unqualified. This does not establish M07 encounter acceptance. The historical diagnosis below is retained; new original-game results and failures are in [the lifecycle investigation](../evidence/2026-09-21-m07-encounter/native-lifecycle.md).

## Retained evidence and cause

The [retained trace analysis](../evidence/2026-09-21-m07-encounter/life-state-analysis.json) records source hashes and exact events from native05, native07 and native08. Original owner A died at health zero while the native noun remained present. The old scene census omitted every `mbDead` animal; therefore it published a despawn for that controlled actor. A client's replica despawn deliberately refuses to destroy `cGameNounManager::GetAvatar()`, which caused terminal `Native entity removal failed`. This was a mismatch between the scene's living-only census and original noun lifetime, not proof that SPORE had destroyed the avatar.

The native08 authority trace, PID 32020, records actor 1 / native noun 232 / epoch 3 as dead at sequence **35453**, health **0**, then alive at sequence **36929**, health **5**, hunger **100**. Its identity is unchanged. Native05 and native07 retained the dead actor until closure and provide no later revival evidence.

The separate historical native08 launcher Join failure rejected NPC native ID **97**, remote entity **12884901985**, at sequence **152**, with the old compound reason `native_binding_or_pose`. That old trace alone cannot disclose which condition failed. New native02/.32 and native03/.33 traces identify the independent age/effective-maximum mismatch; the avatar-retention fix alone did not repair it. Native04/.34 subsequently qualifies the explicit context/scale correction and fresh post-respawn Join in [the closed lifecycle report](../evidence/2026-09-21-m07-encounter/native04-lifecycle.md). The original failure remains historical evidence, not a successful test.

## Implemented representation

The wire entity's scalar `life_state` is 0 for alive and 1 for the observed native `mbDead` value. A dead entity must have exactly zero health. Scene capture retains present dead nouns with the same ID and generation; original destruction still invalidates the binding and publishes a tombstone. Missing/deleting controlled nouns remain a failure requiring a separately qualified reset/adoption path. No replacement avatar is fabricated.

Client projection applies absolute health and `mbDead` inside the existing authenticated, fenced, engine-thread `native_replica_project` operation. It does not execute native death/revive, reward, progression or game-over handlers. Fresh baselines can include a retained corpse; the original avatar is kept and can receive a later same-noun alive state. Every admitted entity has a bounded once-per-second native sample, and life-state transition records expose terminal state even between samples. Original movement/pose bindings, effective maximum-health checks and vitals bounds remain in force.

This initial representation was a scalar state projection. Later native14 qualifies the recorded corpse-animation/life-context correction, and native18/native27 qualify original corpse interaction and inspected client presentation for the encounter fixture. See [life presentation](m07-life-presentation.md) and [M07 acceptance](../evidence/2026-09-21-m07-completion/acceptance.md). Game-over UI parity, complete native revival/model presentation, arbitrary owner destruction and independent B respawn remain unqualified; setting a field is not proof of visual/gameplay parity.

The authority also reads each owner's qualified native DNA balance through `native_actor_owner_dna`. A controlled client's ordinary native stage DNA receives its own owner's absolute balance. Evidence separates incoming `source_dna` from `native_dna`, which is independently read back from the authority owner's native context or the controlled replica's native stage. Other replica owners, NPCs and a retired B corpse balance expose `native_dna:null` with `dna_native_observed:false`; an echoed wire value or cached balance is never labeled a current native observation. B corpse snapshots use the independently guarded award-context lifecycle; they do not invent a new balance or provide persistence. Native reward capture and progression acceptance are documented with the M07 encounter work.

Before a published source or target becomes a native command, `validate_native_scene_action_entity` rechecks the current scene epoch and the captured native ID, incarnation, address and fixture signature against a fresh noun census. The original destruction hook clears the captured incarnation before destruction. Reuse of the same native ID, even at the same address between two 50 ms publications, therefore cannot redirect an old network action to a new noun. The actor command queue separately revalidates its process-local actor/target lifetime at actual execution.

## Pinned binding and checks

- Executable: GOG GA 3.1.0.29 Win32, SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
- SDK: `cbf9206b9a823f0911cd9be0217104a49d72380b`, `Spore/Simulator/cCreatureBase.h:280`, `mbDead` at animal offset `0xB5E`.
- Original death routine `0xC08210` writes 1 at `0xC0838F`: `c6 86 5e 0b 00 00 01`.
- Original revive routine `0xC02D00` calls `0xC0BB00`, whose first instruction clears the same field: `c6 81 5e 0b 00 00 00`. It separately restores `mHunger` at `0xBBC`, which the scalar projection already carries. Original revival also executes behavior/effect/model paths; the projection does not claim to replay those effects.
- Initialization checks both exact store instruction sequences against the pinned executable, in addition to existing spatial instruction checks. Every application verifies the compiled SDK field offset against `0xB5E` before writing it.

Static export command, exit **0**:

```powershell
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-life-fields-01 -ReuseDatabase -Addresses C0BB00,C08210,C02D00
```

The export report is `local/m03-static/m07-life-fields-01.json`; hashes of its assembly/decompiler outputs are retained in the analysis artifact. This is static inspection of the pinned PE, not native execution.

Release compilation of `SporeMP.Bridge` and `sporemp_native_actor_abi_tests` exited **0**, with no warnings. `ctest --test-dir build/win32 -C Release -R native_actor_abi_host --output-on-failure -V` exited **0**, **1/1** target with **44** HOST ABI assertions, **0.21 seconds**. Those existing checks cover calling conventions and spatial secondary-base receivers. The new lifecycle protocol regressions are separate HOST coverage; neither set runs SPORE or validates native death presentation.

## Initial bounded experiment and current next step

The initial plan was one worker and two clients through original owner-A death and same-noun respawn, followed by fresh corpse and revived-state baselines under the established disposable-profile and backup/hash gates. Native01 supplies the corpse baseline; native02/03 expose and diagnose the context mismatch; [native04](../evidence/2026-09-21-m07-encounter/native04-lifecycle.md) verifies the corrected life/context projection and fresh post-revival Join with inspected footage and closed save checks. This does not qualify native death UI or full model-resource presentation.

The subsequent shared encounter exposes a separate dynamic creation failure. The next bounded experiment is the read-only herd/profile comparison specified in [native06](../evidence/2026-09-21-m07-encounter/native06-spawn-blocker.md). Preserve the successful lifecycle evidence and current spawn refusal while qualifying that new route.

## Additional age and scale binding audit, 2026-09-21

**STATIC inspection, not native acceptance.** The historical evidence and status above remain intact. This audit explains the native context needed to preserve the original effective maximum-health calculation and identifies a bounded scale setter. It does not establish successful replicated growth, revival presentation or M07 completion. All addresses below are preferred virtual addresses in the same pinned executable above, with image base `0x400000`; runtime checks must add the corresponding RVA to the actual module base. SDK references use the same pinned commit above.

`cCreatureBase.h:270,275,277` identifies `mAge` at Animal `+0xB34`, the animated-creature pointer at `+0xB54`, and `mGeneralFlags` at `+0xB58`. The alpha mask is exactly `0x1` (`cCreatureBase.h:78`). Only that flag bit belongs in this projection: the local avatar bit `0x200` and all unrelated flags retain their local values. `cCombatant.h:134` identifies `field_34`, which is at Animal `+0x5DC` because the combatant receiver is Animal `+0x5A8`. Original damage `0xBFCF10` writes state 2 on lethal damage and original restoration `0xBFD210` writes state 0; those handlers also emit messages and must not be replayed by a replica. The bounded representation admits ages 0/1 and combatant states 0/2; this is the qualified fixture domain, not a claim to enumerate every game state.

Original adult predicate `0xC0B8D0` compares `[ECX+0xB34]` with 1 (`33 c0 83 b9 34 0b 00 00 01 0f 94 c0 c3`). Original effective maximum-health virtual `0xC05D50`, reached through combatant slot `+0x58`, calls the already checked base-health virtual `0xC04170`. In Creature mode it applies the native age multiplier when age is not 1 and the native alpha multiplier on the adult branch. Neither function uses combatant state as a maximum-health input. Therefore applying authoritative age and the alpha bit before calling the original maximum-health getter preserves its formula; assigning an arbitrary raw maximum does not. If maximum-health validation fails, restore the prior scalar context before rejecting the projection. Health, age, alpha and combatant-state readback remain necessary native evidence.

The scale ABI is independently pinned by the original spatial secondary vtable `0x1469EC0` and assembly:

| Operation | Slot and target | Receiver / arguments | Exact entry bytes |
| --- | --- | --- | --- |
| `GetScale()` | `+0x34` -> `0xC37230` | ECX = Animal `+0xC0`, x87 float result | `d9 41 64 c3` |
| `SetScale(float)` | `+0x40` -> `0xC0E750` | ECX = Animal `+0xC0`, one 4-byte float stack argument, `ret 4` | `f3 0f 10 44 24 04 56 8b f1 8b 86 94 0a 00 00` |
| `SetLocalExtents(bounds, scale)` used internally | `+0x64` -> `0xC892F0` | Same spatial receiver; two stack arguments, `ret 8` | `83 ec 2c 8b 44 24 30 f3 0f 10 44 24 34` |

`cSpatialObject.h:69,72,133` provides the SDK declarations and `mScale` at spatial `+0x64` (Animal `+0x124`). The setter writes the existing animated creature's scale at `+0x3C`. If the scalar changes, it writes spatial scale and `field_7C`, then calls `0xC0DB10` with the whole Animal receiver. That helper derives native extents from species bounding-box values at `+0x554/+0x558/+0x55C`, updates bounds/radii through `SetLocalExtents`, marks the spatial bounds flag, and preserves the requested scale. `cSpeciesProfile.h:84` identifies the bounding-box field. The setter's other call, `0xB5B910`, obtains the current game-mode ID through `0xA42700` (`8b 41 20 c3`). The audited setter chain contains no growth, combat, DNA, reward or random-scale calculation.

The setter's initial null check is insufficient by itself: on a scale change, `0xC0DB10` unconditionally dereferences both the species pointer at Animal `+0xB20` and animated-creature pointer at `+0xB54`. The application must retain the current-noun, engine-thread and apply-state guards, verify the spatial receiver/vtable/slots and SDK field offsets, and require live readable species and animated objects. Bounding-box components and their scaled products must be finite. The packet scale must be finite, positive, and within the implementation's explicit admission bound; the schema-3 ceiling of 1000 is a defensive protocol bound, not a discovered native gameplay maximum. Capture the authority's actual `GetScale()` result and apply it through this original setter, then read it back. A direct `mScale` write omits animated-scale and bounds maintenance.

Do not recompute scale on clients using `CalculateScale` (`0xC04B50`): its non-avatar path calls `0x572A10` and uses local native context, so independent recalculation does not preserve the authority's observed result. Do not invoke `GrowUp` (`0xC223A0`) to fix these fields: it changes age, rebuilds the model, changes health in proportion to the old maximum, refreshes physics and clears behavior state. These exceed scalar projection. Full model refresh `0xC21D50` chooses separate baby/adult species resource keys (`+0x510` versus `+0x504`), replaces the animated object, transfers animation attachments through `0xC21600`, sets animation state and invokes additional native paths. **Applying age and scale does not prove baby/adult model-resource regeneration or complete visual parity.** That remains an explicit native presentation gate; no new model-refresh callback is qualified by this audit.

Static commands below each exited **0**; none launched SPORE. Corresponding reports and outputs are private under `local/m03-static/<RunKey>.json` and `<RunKey>/`. Reports record the executable hash, exact command, timestamps, Ghidra 12.1.3, JDK 21.0.12.1+1 and exporter SHA-256 `b2809b1726c1e2b5d9facda7681a22c579248ca42602c2b97890de308a87e221`.

```powershell
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-age-projection-audit -ReuseDatabase -Addresses C223A0,C04B50,C05510,C02C70,C02DF0,C07560,C03280
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-age-model-sideeffects -ReuseDatabase -Addresses C21D50,C0C320,B48870,BCA150,C05D50,C04170
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-age-scale-route -ReuseDatabase -Addresses C0E750,C37230,C21600
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-age-scale-descendants -ReuseDatabase -Addresses C0DB10,B5B910
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m07-age-scale-extents -ReuseDatabase -Addresses C892F0,A42700
```

Assembly output SHA-256: `m07-age-scale-route/C0E750.asm.txt` = `20bf9771856580a25cce3cba91d448235b011d2a74b2f897b5b6c0488a8b0b2b`; `C37230.asm.txt` = `0300e4b1d7bb1444ee48ac9d19a551bc05e867e1f44869359ab26226290b86e7`; `m07-age-scale-descendants/C0DB10.asm.txt` = `c5cc87e334f36b2aa8db1eb2b708bf6bb3a3e2aab76c2db6e9c828113531dc17`; `m07-age-scale-extents/C892F0.asm.txt` = `37569be3aea93b18bad669e6c0b376b81fe4108a2959215ca2fd9c349359f022`.
