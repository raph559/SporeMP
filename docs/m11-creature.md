# M11 Creature gameplay coverage

Status: **IN_PROGRESS**, 2026-09-27. Private development builds .102–.106 now
qualify bounded social rewards, owner-only First DNA guidance and original
save/load recovery. The [reviewed report](../evidence/2026-09-27-m11-social-progress/SESSION.md)
preserves failed attempts and the shared-species, empty-pack, exclusive-action
limits. All full [C01–C12 gates](../tests/engine/M11.md) remain open. Cross-stage
completion remains M17; correct outgoing eligibility is required here.

This public source remains M08 (.50). The sections below describe the initial
audit and exported research tool; the later native implementation and expanded
19-test reducer are not included in this checkout.

## Public M08 implementation and initial gaps

- `native_network.cpp::input_window` translates W/A/S/D and Space. Normal target
  selection, the native ability bar, social actions and mating/editor controls
  are not integrated by that handler.
- Network `Verb` admits move, jump, attack, stop, approach, engage and pickup.
  Dispatch uses original `WalkTo`, `DoJump`, default attack `PlayAbility` and
  bounded corpse feeding. Social, pack, nest and mating commands are absent.
- `native_award_context.cpp::amount_hook` routes B's reward calculation only
  at damage caller RVA `0x807be4` under `owned_damage()`. Feeding has its own
  validated scope. A parameter named `social` does not qualify social ownership.
- `goal_hook` requires A and B to share the same native species profile before
  the existing alternate-world update. Distinct actor/player IDs do not qualify
  distinct-species progression.
- M07 bounds death/corpse presentation; independent B respawn remains open.
  M08 creation transactions do not replace a campaign avatar. M09 restores
  tested inventory/DNA, not newly earned independent unlocks. M10 separation
  across locations does not qualify camera activation within one location.

[feature-coverage.csv](feature-coverage.csv) now cites partial foundations
without upgrading full Creature mechanics to VERIFIED.

## Candidate social reward path

Pinned executable: GOG GA 3.1.0.29 PE32, SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
SDK: `cbf9206b9a823f0911cd9be0217104a49d72380b`.
Preferred image base: `0x400000`; trace callers use RVAs.

The SDK `Spore/Simulator/cCreatureAbility.h` names Dance, Pose, Charm and Sing
as types 34–37, and MatingCall as 16. These are source leads, not a qualified
social command interface. No new binding is installed in this increment.

Executed against the existing partially analyzed Ghidra database:

```powershell
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m11-social-reward-20260927 -ReuseDatabase -Addresses C042A0
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m11-social-caller-20260927 -ReuseDatabase -Addresses D9BBD0
```

Both exited 0 with completed exports: **STATIC evidence**, not native execution.
Decompiled game code remains private under `local/m03-static/`. The database's
reference list is not proof that no other dynamic callers exist.

The x86 listing identifies the known damage caller and `D9BBD0` as callers of
reward calculation `C042A0`. The latter passes true at `D9BCFD`, calls that
calculation at `D9BD05`, then calls original `AddEvolutionPoints` at `D9BD16`,
returning to `D9BD1B` (RVA `0x99bd1b`). It also reaches the species-relationship
manager and reads the campaign avatar. The initiating player, relationship
effects and runtime ownership remain unqualified. Do not wrap the whole routine
in a player/StageScope; it also touches presentation and other native systems.

Existing paired `native_global_dna` events record the caller, call ID, campaign
avatar and immediate DNA values. The new reducer selects this candidate:

```powershell
python tools/native/analyze-creature-social.py local/M11_CLOSED/actors.jsonl --output local/M11_CLOSED/social-review.json --require-social
```

Use the actual complete closed trace; this is a template, not an executed native
test. The tool checks lifecycle, process/thread/clock identity, native pins,
scene/call pairing and finite values. It refuses changed input and existing
output paths. Invalid traces yield no observations. A zero immediate DNA delta
is retained because the original call can enqueue a later award. Reports always
retain `native_acceptance: NOT_VERIFIED` and do not infer a beneficiary.

## Next experiment

After verifying personal backups, measured disposable-profile isolation and
current desktop permission, capture one normal primary-player social interaction
using the existing Actors observer. Inspect the original UI/relationship result
and delayed progression, close cleanly, then reduce the complete trace.
Expected: the candidate call correlates with the observed social action. A
missing call is a failed hypothesis, not permission to force a reward. Trace
the initiating-player/relationship receivers before implementing B's command.
Primary-player evidence alone cannot qualify independent B social ownership.
