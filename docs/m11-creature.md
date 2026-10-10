# M11 Creature gameplay coverage

## Owner-B goal card after restore, .182–.183 (2026-10-02)

B's client now shows B's own progress on the original Creature Hunt card after
a fresh distinct-species restore. Trial149 displays Hunt 1/3, "Hunt 2 Blatardo"
for B and the unchanged Hunt 2/3, "Hunt 1 Blatardo" for A, with DNA 7/14.

The original species goal caches its counts through `C2EE80`. On the owner-2
client only, a private original relationship root is filled through `BA3B40`
with the exact entries of B's adopted checkpoint root. Each differing goal is
re-evaluated by original `C2FBB0` with that root selected at the single getter
return `82EE8E`; the cached values are seeded first so the original sees no
count transition and its reward branch cannot run on a replica. The original
card item is then repopulated by `E17BF0(item,1)`, with the display root also
selected at `C2E980`'s return `82E98D` inside that call. Trial148 (.182,
without the card call) changes the goal values but leaves the visible text
stale; it is retained as a failed presentation.

| Original binding | Contract | Admission |
|---|---|---|
| `BA3B40` | thiscall(first map, key*), RET 4; inserting accessor | Private display root only |
| `C2FBB0` | thiscall(goal); evaluates `C2EE80` and reloads card props | Owner-2 client, goal vtable `0x106b3c0`, values differ |
| `E17BF0` | thiscall(card item, char), RET 4 | Item vtable `0x107fd64` whose +9C is that goal |
| getter returns `82EE8E`, `82E98D` | relationship-manager selection | Only inside the matching display call |

Limits: saved state only. Live relationship changes are not replicated to the
client, a species only B has met has no goal or card, and B has no goal
lifecycle, completion presentation or current-goal selection of its own. No
full M11 gate closes. Pins, commands and failures are in the
[native session report (archived)](public-evidence.md#historical-artifacts).

## Subsequent primary combat and unequal saved progress, .181 (2026-10-01)

A's admitted brain-0 combat no longer requires zero earned progress. Identity,
species, living-context and exclusive B-observation checks remain; original
SPORE still executes combat and rewards. Trial146 starts from the fed143 save.
One diagnostic combat intention selects a different native adult89. Six
original Bites kill it; native AddEvolutionPoints raises A7 to14. B retains7
DNA/goal/combat progress and diagnostically moves8.1771655 native units during
the encounter, then stops. First DNA is not repeated.

Original Save and fresh147 preserve all679 player/inventory rows. A14/B7 DNA
remain separate in their own client views. A shows hunt2/3 and its normal jump
lands. Original Sporepedia shows A's blue and encountered orange species cards;
B keeps its own single orange card. Both players remain alive. All twelve
process exits are zero and disposable profiles restore.

Two limits remain explicit. B's objective panel also displays A's hunt2/3;
independent objective presentation is unfinished. Primary history is observed
as eight categories/seven entries during B extension serialization, but six
categories/five entries on all fresh loads. B's one-entry serialization matches
exactly. The diagnostic has not been correlated with the exact original primary
serialization point, so this comparison neither proves lost saved entries nor
qualifies whole-primary history preservation. Observe original primary Write before
claiming that boundary. A live history delivery, subsequent foreign B combat/
food, full normal controls and later progression remain open. No full M11 gate
closes.

## Primary feeding and both owners' saved rewards, .180 (2026-10-01)

Trials142/143 qualify A's normal corpse click after its diagnostic first kill
and normally completed owner First DNA tutorial. The authenticated client
forwards a zero-argument pickup intention; only the authority executes the
existing original feeding order and nutrition callbacks. A reaches 100 hunger
and 10 health, with 7 DNA unchanged by the native zero-amount feeding callbacks.
B retains its own 7 DNA/progression. Foreign B feeding remains outside this
increment's admitted inputs.

Trial143 completes original Save and produces a sealed checkpoint. Trial144
rejects the otherwise valid saved corpse because native feeding exhausted its
food to -34.025074. Restore now accepts a finite exhausted-food value for the
authenticated dead corpse, preserving its original value and restoring only
the saved first-feed bit. Identity, lifetime, duplicate-grant and dead-state
checks remain; a new feeding order still requires positive food.

Fresh145 restores all three original processes and both clients join. All 679
player/inventory rows match before Save, after Save and at each fresh load;
both retain 7 DNA/goal/combat progress and original tutorial completion. B's
private relationship/history deserialize exactly. No reward is replayed. A's
normal Space jump lands, B diagnostically steers 7.8252966 native units and
releases to zero sampled velocity/drift, and both remain alive at closure.

All four runs close with six zero exits, restored disposable profiles and
unchanged protected files. Trial142's Save is NOT RUN: automatic closure had
already claimed the run. A's collection click in145 occurs near automatic
closure and its resulting view is NOT OBSERVED. The primary history readout
observes four entries after reload; an exact pre-Save whole-primary digest
was not captured. A live new-history delivery, full animation/UI parity,
foreign B feeding, other diets, normal sustained held input and coordinator
publication/crash acceptance remain unqualified. Full M11 remains 0/12.

## Primary combat and owner tutorial with foreign B, .179 (2026-10-01)

Trial141 qualifies A's first original kill in the restored distinct-species
fixture. A diagnostic intention passes the shared network/native admission
rule, six original Bites kill the selected adult and original AddEvolutionPoints
changes A from 0 to 7 DNA. A's original campaign history grows from two entries
to three, while B's private entry remains unchanged. B retains 7 DNA.

First DNA is deferred before the authority's global cinematic request and sent
to A's authenticated client. A normally confirms the original presentation;
only A's original tutorial completion is newly recorded. Independent pending
slots prevent one owner's completion from consuming the other's request. Save
remains fenced until both pending requests are complete. No new native binding
or replacement combat simulation is introduced.

B's 30 diagnostic steer inputs and release execute during A's presentation:
7.5253123 native units of displacement, then zero sampled velocity and drift.
All 32 commands correlate through client acceptance, authority queue, native
execution and return. Both owners remain alive at early clean closure. All six
process exits are zero and the disposable profiles restore.

This does not qualify A's new client history display, saving this new result,
physical held input or later progression. Trial138's incompatible A history
expansion is removed; trials139–140 expose a second B-only admission guard,
corrected through a shared tested policy in141. Full M11 remains 0/12 complete
gates. Exact commands, pins and failures are in the
[native session report (archived)](public-evidence.md#historical-artifacts).

## Distinct-species native Save and recovery, .177 (2026-10-01)

Trials130/134 qualify explicit original Save and fresh authority/two-client
restoration of the living brain-0 first-kill fixture. All 679 player/inventory
rows match before Save, after Save and at reload. B retains 7 DNA/goal/combat
progress, its private relationship and one original Creature history entry.
Both clients reuse their saved actors and show their own creature, portrait
and Sporepedia collection. A's normal jump lands and B moves 5.5978906 native
units through a diagnostic intention. Both remain alive at closure.

Checkpoint extension schema4 retains the existing native player pointer,
actor fingerprints, 20-word secondary Stage subset and pickup records. It
adds two original private-container serializations. Schema3 remains accepted
for the shared-species format. No native reward or history event is replayed.
World-update pending state is preserved without qualifying later evolution.
Ordinary transfer/stage export continues to reject foreign contexts.

| Original binding | ABI and receiver | Evidence and admission |
|---|---|---|
| Relationship Write `0xBA27C0`, Read `0xBA2830` | `void __thiscall(void*, ISerializerWriteStream*)` / `void __thiscall(void*, ISerializerReadStream*)`; private root +4; callee removes 4 bytes | Pinned ClassSerializer descriptor `0x0156C370`; stream health is authoritative. Native130 write and134 read preserve first-map count/hash. Second map must be empty. |
| History Write `0x7EB210`, Read `0x7EC2F0` | `bool __thiscall(void*, ISerializerWriteStream*)` / `bool __thiscall(void*, ISerializerReadStream*)`; private root +8; callee removes 4 bytes | Serializable vtable `0x01414330`, slots +0x10/+0x14. Read requires serialization version at least `0x230002`. Native130/134 preserve all recorded date/header/payload words for the one-entry snapshot. |

Addresses are VAs in the pinned original executable (image base `0x400000`),
not arbitrary executable offsets. Exact code-prefix guards, including the
relocated operand mask, precede use. Calls execute on the original engine
thread inside its Save/Load serialization callback with idle owner contexts.
Roots use original constructors/deleting destructors and are never substituted
for global managers or registered as global listeners. Static source, ABI HOST
tests, executable/content/SDK pins and native receipts are in the
[session report (archived)](public-evidence.md#historical-artifacts).

The saved-pair client path admits only the authenticated schema4 identities,
matching species/archetypes, distinct saved herds and exactly two live native
players. It adopts the existing B actor instead of making another one. Trials
131–133 retain client baseline failures: the initial fresh-only census rejected
the saved player; a subsequent predicate mistakenly required player nouns to
be animals. Trial133's named rejection isolated that mistake. Trial134 fixes
the player lifetime check without removing identity or species validation.

The sealer requires both successful private-root writes, in order, inside the
same native save barrier. Missing, duplicated, wrong-owner/epoch, replayed or
failed receipts are rejected. Original Save130 is sealed only after clean
closure. Fresh generation134 restores that exact artifact; no coordinator
publication or native crash test is claimed. All five trials have six zero
process exits each, 29 unchanged protected personal files per role and restored
profiles. The closed review preserves every failed attempt separately.

Foreign death, editor/nest changes, additional relationship transitions,
extended play, normal sustained B keyboard movement and all twelve full M11
gates remain OPEN. History remains bounded to one Creature entry and the
existing 16-root presentation lifetime limit. Next: ordinary sustained B
movement and release in the restored fixture with A continuing normally.

Latest nest increment: **0.0.156**, trials94-95. The authority creates B's
original home through CreateNest, preserves A and both players' progression,
and publishes its original herd/species/model identity to both clients.
Both players jump, original Save completes, and a fresh three-process load
restores nest16485 for B's herd12558 without creating another nest. Both clients
admit all123 nests and both post-reload jumps land. All684 saved player/inventory
records match. Static nest updates are suppressed when unchanged; both clients
remain connected for103 seconds through clean closure. The first creation and
first reload samples differ slightly in nest position; the timing and cause of
that movement are not established. Clients match the restored authority; exact
creation-pose preservation is not claimed. Full C05 packs,
recruitment/dismissal, C10 B respawn and all twelve full M11 gates remain OPEN.
See the [native report (archived)](public-evidence.md#historical-artifacts).

Latest part increment: **0.0.150**, trials88-89. B's normal fossil click grants
one original Manglerfish part and now updates B's actual local inventory and
original new-part card. A's complete player/inventory state is unchanged and
A receives no B part notification. The original owner tutorial completes while
A continues playing; B then resumes and both normal jumps land. Original Save
completes. A fresh three-process load restores all684 saved player/inventory
records exactly and projects each owner's312 inventory rows: A57/B1 unlocked
parts and DNA35/17, with no replayed reward, part card or first-part tutorial.
The extra two-client control check after this reload is NOT COMPLETED before
automatic closure; post-collection controls are qualified separately in88.
BUILD and the two affected HOST tests pass. Distinct-species attribution,
concurrent independent unlocks, editor integration and every full C01-C12 gate
remain open. Trials85-87 retain the earlier tutorial failure and correction.

Earlier trials79/81 exposed an invisible restored B avatar. Trial82/.146 fixes
initial placement only; .147 trial84 qualifies bounded visible model following
and both jumps. Its sampled B model offset stays below0.366 units; exact
model/source equality is not claimed. The preceding full CTest run (.146)
passed19/19 and was not repeated for these focused changes. See the [remaining-work audit](m11-work-audit.md).

Earlier combat/death increment: **0.0.139**, trials71-72. A's normal Bite lands six
original hits and grants7 DNA (35 to42) while B stays at17; original Save
completes in71, whose sealed artifact is retained but not yet reloaded. Trial72
qualifies A's melee-death prompt on its owner client, original confirmation and
revival of the same actor, restored view and a normal jump/landing. B keeps
simulating and lands two jumps while A's prompt is open. The authority remains
connected. Trial72 Save is NOT COMPLETED: a pending-jump refusal is retained,
and the retry follows the bounded shutdown. Initial combat-death stall69 and
ASLR binding refusal70 remain in the evidence. Other death causes, B respawn
and all full C01-C12 gates remain open.

Earlier control increment: **0.0.130-0.0.131**, trials61-62. Both players have
qualified normal camera-relative cardinal/diagonal movement and key-release
stopping through original WalkTo/StopMovement. Six releases stop their own
keyboard walks; moving-jump releases wait for the original landing. All four
trial62 jumps land, with zero sampled drift after settling. Full held-key,
interaction-overlap and ability coverage remain open. Builds and19/19 HOST
checks are separate from the closed original-game evidence.


Latest input increment: **0.0.129**, trial59. A normal B corpse click now reaches
the authority and executes original feeding, after normal Bite and automatic
feeding. No first-feed reward is repeated; both players then jump and land,
and original Save completes after a refused pending-action attempt. A eating
and contention remain NOT RUN. Trial58 preserves the failed reverse-identity
lookup attempt. All full gates remain open.

Earlier bounded native increment: **0.0.123-0.0.126**, trials52-56. One normal
B Bite shortcut approaches, lands six original hits and receives7 DNA (B10 to17,
A35). Save/load restores exactly682 player/inventory rows without a second reward.
The original load gives a retained NPC corpse positive health while keeping it
dead. Schema16 and corpse presentation preserve that observed combination;
trial56 connects both clients with exact corpse nutrition/beneficiary and both
normal jumps. Failed attempts53-55 remain in the evidence. Complete abilities,
diets and long-term normal play are still open; no full C01-C12 gate is closed.

Earlier bounded native increment: **0.0.120–0.0.121**, trials46–49. Original
hunger recognizes B within two audited ownership calls, preserving native
rates, temporary behavior pauses and starvation damage. Save/load restores
hunger48.2126274 and the exact682-row progression census. A later load at
hunger0/health9.02655983 reaches native death with B45/A35 retained and A
jumping afterward. B respawn remains unimplemented; full C02/C10 remain open.

Earlier increment: **0.0.118** qualifies one normal movement cancellation
of B's unanswered social exchange. Trial44 retains the original controller,
clears the meter, grants no abandoned reward, then completes a fresh native
greeting/Sing with B10+25/A35 and both normal jumps. Pending rewards/tutorials
still block cancellation; other interruption cases remain unqualified.

**0.0.117** retains the original avatar behavior root
for network player B while entering/releasing social attribution separately.
Trial41 completes normal greeting/Sing, B's 10+25/A35, original owner release,
both normal jumps and Save. Trial43 reloads the same sealed native snapshot
with exact 682-row player/inventory agreement, no replay and both normal jumps.
The .116 original nest factory and .117 normal jumps after it are developer
probes: nest replication and persistence remain unimplemented/unqualified.
The actor still receives native NPC updates outside the scoped social callbacks;
this root selection does not qualify normal B hunger, respawn or distinct species.
Late social responses in trials39–40 retain an unfinished exchange. Broader
social concurrency and all full C01–C12 gates remain open.

Status: **IN_PROGRESS**, 2026-09-27. Bridge/NativeHost **0.0.106** records bounded
original B social and delayed species-objective rewards with unchanged A
progress. First DNA presentation is isolated to B's client, with A still
playing. Original save/load preserves the earned social progress and tutorial
marker. Trial22 restores B at 45 DNA/goal/social, A at 35, with no reward replay
and exact 682-row player/inventory agreement. The same-species, empty-pack,
exclusive encounter limits remain. Trial29/.110 subsequently qualifies one
original mouse/keyboard greeting/Sing exchange and the colored native social
meter while A jumps. The objective panel remains stale. Trials32/34/.113
qualify bounded A starvation, original confirmation/revival/fade and pending-death
reconnect with clean exits while B plays. The earlier black-view and deadline
failures remain recorded. Trial36/.114 preserves B's native reward context after
NPC combat death while A jumps; B's respawn flow remains unimplemented. Independent
species/objectives and the full M11 matrix remain unqualified. The current
[native report (archived)](public-evidence.md#historical-artifacts) retains failed attempts and subsequent successes.
The [gate map](../tests/engine/M11.md) retains
every mechanic. Cross-stage completion remains M17; correct outgoing transition
eligibility and requests are still required here.

## Existing implementation and concrete gaps

- `native_network.cpp::input_window` translates W/A/S/D and Space. The .110
  social input adapter forwards B's original target selection and Sing controls
  in the recorded exchange. Full ability coverage, camera-relative movement
  and mating/editor controls remain unqualified.
- Network `Verb` admits move, jump, attack, stop, approach, engage, pickup and social.
  Dispatch uses original `WalkTo`, `DoJump`, default attack `PlayAbility` and
  bounded corpse feeding. Authenticated social start/response now reaches the
  original native orders; trial29 also exercises original mouse/keyboard social
  controls. Pack, nest and mating commands remain absent.
- `native_award_context.cpp` now routes the original social amount, DNA,
  social-history getter and action through a validated result-owner scope.
  Trial21 also matches a delayed objective to B's exact native relationship
  transition. Trials17–18 qualify owner-only tutorial completion and persistence.
  Default distinct-species relationships and separate objective instances remain open.
- `goal_hook` requires A and B to share the same native species profile before
  the existing alternate-world update. Distinct actor/player IDs do not qualify
  distinct-species progression.
- M07 bounds death/corpse presentation. The .113 probes add A's original
  starvation screen, confirmation, revival and view recovery, including a fresh
  client reconnect with the same pending death. In .114, B's still-present dead
  noun retains its original Stage/counter/tutorial context without ticking it;
  actual destruction, identity invalidation and disposal still retire it.
  Independent B respawn and broader death/model presentation remain open.
  M08 creation transactions do not replace a campaign avatar. M09 restores
  tested inventory/DNA, not newly earned independent unlocks. M10 separation
  across locations does not qualify camera activation within one location.

[feature-coverage.csv](feature-coverage.csv) now cites partial foundations
without upgrading full Creature mechanics to VERIFIED.

## Current qualified increment: 0.0.102–0.0.106

The authenticated developer social intention reaches the original greeting and
response orders. First DNA uses its original cinematic and caption on the
owning replica. Its original pause/resume cycle precedes the native completion
record on B; A's marker and progression are unchanged. Pending presentation
blocks a checkpoint. Trial17 saves the completed marker and social reward;
trial18 restores them without replay, with exact player/inventory agreement.
Trial19 checks repeat-result owner cleanup without another First DNA popup.

Individual D9BBD0 isolation is insufficient for species objectives. Trials19
and20 reproduce a later C2EE80 reward to A. In .106, the non-inserting BA3190
lookup observes the exact relationship count and status before/after B's
original result. A one-use receipt requires the same actor, native identity,
epoch, species key, cached objective state, one-step count change, native
alliance transition and required count. An expired or mismatched receipt
cannot select B. No score, threshold, relationship or reward amount is replaced.

The existing C2EE80 updater remains in its original world context. Only the
qualified social DNA leaf selects B's Stage/player; exact getter call sites
select B for the native history update and toast position. Other callbacks
retain their existing behavior. Trial21 records B 10→20→45 and unchanged A35
through the complete updater. Trial22 restores B45/A35, zero replay, both
native tutorial markers and an exact 682-row census; both actors jump and land.

| Native binding | Pinned x86 contract | Evidence |
|---|---|---|
| B3D5C0 | cdecl singleton getter, EAX pointer; ASLR relocation checked | Static instructions; trial21 live lookup |
| BA3190 | thiscall map at manager +0x20, two-word iterator output, three-word key, RET 8; no insert | Static instructions, 4096-call HOST ABI stack/argument check, native before/after transition |
| C2EE80 | Existing thiscall detour; two output pointers, RET 8; vtable RVA 0x106b3c0 and caller RVA 0x82fbd4 | .105 read-only reproduction and .106 complete callback observations |
| C2EFCE / C2EFE7 / C2EF76 | Exact return sites for original social DNA, history-player and toast-avatar leaves | Original code audit and trial21 A/B progression snapshots |

The executable, SDK and content pins are unchanged and recorded in the native
session. These tests qualify the bounded shared-species experiment. Separate
goal instances, default distinct-species relationships, simultaneous social
exchanges, normal ability-bar input and all other C01–C12 requirements remain
open. Use `--require-objective` in addition to `--require-owned --require-action`
for an objective-completing trace; the retained leaking trial20 correctly fails.

## Candidate social reward path

Pinned executable: GOG GA 3.1.0.29 PE32, SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
SDK: `cbf9206b9a823f0911cd9be0217104a49d72380b`.
Preferred image base: `0x400000`; trace callers use RVAs.

The SDK `Spore/Simulator/cCreatureAbility.h` names Dance, Pose, Charm and Sing
as types 34–37, and MatingCall as 16. These are source leads, not a qualified
social command interface. No new binding was installed in that initial increment.

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

## Social action observation in 0.0.87

The existing `ExecuteAction` hook now observes action `0x060b4123` only at
return RVA `0x99bd64`. The pinned instructions `D9BD4F..D9BD57` initialize
exactly three 32-bit words before the call at `D9BD5F`. The provisional SDK
structure lists more fields; the bridge deliberately copies only the 12
initialized bytes. A Windows guard-page HOST test checks that boundary.

The observer resolves payload addresses against the live original noun roots,
checks the pinned Creature vtable and lifecycle flags, then records native
actor/target/player IDs and both players' DNA, goal, social and combat values
before and after the original action. It preserves exactly one original call,
its receiver and payload, and adds no StageScope, player substitution, social
command or reward. An active alternate StageScope cannot supply a resolved
primary-player observation. The existing opted-in Actors mode owns the observer;
normal Play does not enable it.

The reducer now separately reports `action_observations`; `--require-action`
requires a complete root-resolved dispatch. Missing returns, reused call IDs,
changed roots/epochs/scopes, absent schema, unsupported caller/action, invalid
values and unresolved observations discard all usable social observations.
Neither an action return nor a progress delta identifies the initiating human.
The relationship update precedes this action, so these records do not claim a
before/after relationship comparison or a completed deferred reward.

The initial Release build and 19/19 CTest cases passed. After the last reducer hardening,
its 10 focused tests passed again. Native social gameplay was NOT RUN at that point.
[Session, commands, hashes and readiness checks (archived)](public-evidence.md#historical-artifacts).

## Raw meter observation in 0.0.88

The existing engine-thread sample follows the current Creature strategy's
display root (`+0x68`), then its social UI root (`+0x60`). The pinned constructor
`E33080` installs vtable RVA `0x1081900`; `D2BA50` independently confirms the UI
getter. Only that readable native object can supply a resolved sample.
The sampler reads three flag bytes, one integer and eight finite float fields,
ending at `+0x1e0`; it never copies the native object's containers or pointers.
Fields retain raw offset labels because they are observations, not a replacement
score calculation or a general success verdict. It emits changes and scene
resets, adds no detour and never enters an alternate player/StageScope.
HOST tests cover the final read against an unreadable page, wrong object type,
truncation, nonfinite values, invalid flags and unchanged source bytes.

Trial01 (.87) exercised original controls but produced no successful social
callback. Trial02 (.88) observed two original Sing successes, the pinned
success action, DNA/goal/social increases and an alpha part unlock. Earlier
responses failed; responding earlier succeeded. This is a timing lead from
the recorded attempts, not a claim that all failures have one cause.

## Next ownership experiment

Trace the initiating-player and relationship receivers before implementing B's
command. Primary-player evidence alone cannot qualify independent B social
ownership. Preserve every failed run and require both `--require-social` and
`--require-action` when reducing a complete closed successful trace. Recheck
protected profiles before each launch; current desktop permission was supplied
by the user for this session.

## Original social inputs: 0.0.89 / 0.0.90

The .89 native run records the original C032F0 -> C0C070 start order, and a
third primary-player success after reloading the saved 22.5 DNA. It reaches
35 DNA and another alpha part unlock. That C0C070 observer sees no response
with extra mask `0x10`; the hypothesis is not used as a command binding.

Static D1E930 instead handles types 34..37 under behavior `0x941bd4bf` by
issuing BC9960 order `0x10000000`, priority 10, target null, with data `+0x0c=0`
and `+0x10=ability index`. D48AB0 pushes the actor and index and caller-cleans
eight bytes; D1E930 returns with plain RET. The .90 observer forwards this
original input exactly once and reads its resulting order. It has a 512-call
limit with an explicit exhaustion event. It does not replace input or scoring.

The local-only `social_probe` worker operation takes owner, bound actor ID,
current native NPC ID and an ability index (UINT32_MAX requests an encounter).
Scene, owner, input fence, original noun membership, pinned instructions,
replica role, native species and life state are checked. Online sessions cannot
use it. A response additionally requires a native social ability, the original
SocialRespond behavior and matching native target/intention. It uses C0C070's
actor-local original order writer. Start arguments match the .89 observation;
response uses zero clear/prerequisite masks to reach the same BC9960 and data
words as the original UI. No scoring, strength, avatar flags, relationship,
reward or global player context is assigned by this adapter.

This is an investigation interface, not normal multiplayer controls. Read-back
of an order establishes queuing only. Independent social eligibility and reward
ownership require original-game observations; no M11 gate is closed by the
probe's compilation or HOST ABI checks.

## B's original social result: 0.0.91-0.0.96

Read-only tree samples and the pinned x86 listings establish that B's ordinary
native root lacks SocialRespond. A reversible local probe deactivates the
original tree through C0D500, then selects the existing avatar root through
BCB7D0. Mode 1 only selects the tree. Mode 2 also temporarily classifies B as
an avatar inside the original D9C100 social tick and D9BBD0 result; RAII restores
the classification bit. Global avatar/player roots and the ordinary NPC tick
are unchanged. Original scoring, costs, greeting, target orders and Sing execute.

The tick ABI is cdecl with eight argument words (including a double time and
float delta); both original BC9160 dispatch returns 0x7c9479 and 0x7c96a0 are
pinned. The result is thiscall with context ECX, NPC and beneficiary stack
arguments, eight-byte callee cleanup and a full 32-bit animation return.
HOST Detours fixtures exercise 4,096 calls with stack, argument and return checks.
Native trials retain the initial missing dispatch and failed exchanges.

Mode 2 requires the current secondary owner, an empty original pack and no
active/pending social encounter. Competing primary social commands and original
UI social inputs are refused. It is a local exclusive experiment, not concurrent
or network social control. Within the validated original result, exact getter,
reward and action leaves select B: avatar 0x99bc07, amount 0x99bd0a, DNA
0x99bd1b, social-history player 0x99bd34 and action 0x99bd64. Original native
routines perform the changes. A whole-AI StageScope is never used.

Trial09 exposed A's separate social-history update between the DNA and action
callbacks; trial10 corrected that getter. Trial11 wraps the complete result in
observations: B DNA/goal/social 0 to 10, A unchanged at 35, combat unchanged,
one original reward and action. Both original return and visual +10 are retained.
The stricter reducer checks the full interval and rejects duplicate, missing,
malformed or leaking evidence. For this path use:

```powershell
python tools/native/analyze-creature-social.py local/M11_CLOSED/actors.jsonl --output local/M11_CLOSED/social-review.json --require-owned --require-action
```

`--require-social` instead requires the historical unscoped primary DNA path.
Do not apply it as a prerequisite for the scoped B path. Reducer success does
not certify relationships, shared objectives/tutorials, independent presentation,
normal controls, persistence or any whole C01-C12 gate. The original shared
tutorial interrupts A and the same-species objective changes for the global UI.
See the closed native report for all commands, pins, failed trials and cleanup.

## Secondary original death and restart bindings

The secondary adapter retains the exact reason when the original base damage
call takes B from positive health to zero, then waits for SPORE's own dead flag.
Starvation does not necessarily enter the animal-to-animal damage wrapper.
The pinned original handler constructor at D47B50-D47B67 initializes a 24-byte
object containing its vtable, reference count, state, timer, effect and reason.
The bridge gives B separate storage and never inserts it in A's handler list.
Original D38EC0 receives the action, D401E0 applies the penalty and D405F0 resets
the instance. D38F00 with flag0 disposes it without freeing bridge storage.
Only the state1 tick is dispatched; the authority does not advance it through
A's unrelated cinematic state or run the state3 world-herd reset.

The SDK `GetAvatar` and `GetPlayer` helpers directly read the noun manager's
primary members. They do not call the executable getters B1FD00 and F67D40.
The native death guard must inspect the latter's scoped result for B. The
separate native Stage/player/avatar scope covers audited death and restart
leaves, never an original whole-world update.

Restart retains original C02D00 revival and D40FA0 RestartAtNest: native health,
age, hunger and nest placement are not recreated by the coordinator. The initial
qualification requires brain level0, an empty primary pack, B's one-member
herd with a native nest, and no pending migration property. A's shared action
handlers and primary target are preserved. Exact D40FA0 callers of D3C360,
D4D670 and D4D740 are shared drama-manager boundaries; other callers retain
their original behavior. Suppressing the latter also omits the original
type4 baby/mother drama activation. Trial98 therefore qualifies native revival
but leaves B at hatchling age/scale; complete growth is not claimed.
The three primary restart-position words are restored
after B's call. Original animation preloading through IAnimManager is retained.

The B restart fade is presented on B's client. The scoped authority suppresses
the original fade request and both AE0930 object parameters together: request0
would otherwise select A's current or last queued cinematic. Runtime evidence
must show all six presentation routes, unchanged primary life/position and
cinematic state, the original B return, and resumed normal client control.
The network retains separate owner1/owner2 occurrences and accepts completion
only for the matching owner, entity generation and native death identity.

All addresses above are STATIC bindings for executable dc04aee5 and SDK cbf9206.
HOST x86 fixtures check handler float/word arguments, return values, receiver,
destructor flag, stack cleanup and UI forwarding/suppression. Neither class of
evidence establishes native respawn acceptance; the session report records the
actual runs, including the failed damage-handoff and SDK-getter attempts.

### Independent native hatch context (.161 implementation)

The .161 adapter gives B a separately original-constructed cDramaManager:
D4EA40 constructs112 bytes, D4E020 loads the original event properties, and
D4EAC0 with flag0 disposes bridge-owned storage. The constructor/destructor
write the native singleton, so the engine-thread adapter restores A's pointer
immediately and compares A's complete112-byte manager state. This is not a
second world updater: only the type4 activation/deactivation requested by
original RestartAtNest and growth run for B. No alternate whole-manager update
is dispatched. Native references/containers remain native and process-local.

The eight hooks below are pinned STATIC x86 bindings for the same dc04aee5
executable and cbf9206 SDK. Actor and mother identity, current epoch, authority,
thread and secondary Stage are validated before entering a scoped callback.
The original mother remains a native NPC in B's herd. Primary pack support is
still excluded: D52DF0 reads the manager's mPosseMembers vector at B1F900.

| Entry VA | Native contract and scope |
|---|---|
| C04F30 | thiscall(actor, int age), RET4. Scoped player classification prevents the original age0 path from replacing B's avatar root with NPC baby AI. |
| C04FB0 | thiscall(actor, float delta), RET4. The original growth state machine changes scale/age/health and deactivates B's type4 drama. |
| D62F10 / D63F70 | Original AvatarBaby activate/tick, eight caller-cleaned words with actor, double time, flags/data, state/context and float delta. |
| D63070 / D61BD0 | Original mother activate/tick, same native leaf ABI. |
| D64A30 | Original mother sequence leaf, same ABI. Its original D65024 SetAge(1) call initiates growth; bridge code does not synthesize this call or its timing. |
| D8E380 | Original corpse leaf, same ABI. Only the authenticated pending B death receives scoped native player classification, avoiding NPC corpse expiry without changing its timer. |

Classification changes only the player bit for the audited call and restores
that bit afterward, retaining other original flag changes. Stage and singleton
selection also end before whole-world AI/physics resumes. Pending hatch state
blocks checkpoints. Prefixes, destructor tail and singleton availability are
checked before hooks attach. Existing x86 HOST ABI fixtures pass; original-game
acceptance remains separate and is recorded in the session report.

Trial101 reaches the pending-corpse check but exposes A's original D405F0
state3 reset healing B's dead actor. In .162, C6A1C0's original thiscall/RET
herd-active predicate is fenced only at return address D4069E and only for
the current authenticated B herd. Its other callers and NPC herds retain the
original result. This excludes the owned peer from A's NPC heal/relocation
loop without rewriting health, death, placement or the original corpse timer.

Trial102 confirms that fence and an82.327-second delayed B revival, but does
not enter the hatch leaves. The .163 correction scopes original decision
callbacks D61B60 (baby) and D61B90 (herd mother) as well as their selected leaves.
Both return a native float through x87 and receive the existing eight-word
Decide ABI from BC8B27-BC8B44. The first reads type4/mother state; the second
also compares the candidate's herd to the selected avatar's herd. B's original
herd companions therefore share B's scoped drama selection. Other herds and
A keep their original context. The original predicate decides eligibility;
the bridge does not supply a replacement score or force a behavior.

Trial104 enters the baby and companion callbacks but never the designated
mother sequence. Its separate tree record15874D0 selects D61B00 and activates
D62D40 before D64A30. The .164 adapter scopes both additional callbacks using
the same audited Decide/Tick contracts. D61B00 tests type4 and compares the
candidate with the original manager's selected mother; no score is fabricated.
D62D40 initializes its own state and native approach path to the selected avatar.

Three presentation calls from that path otherwise affect A's process-wide UI.
Only their exact audited callers are fenced inside B's selected hatch context:

| Entry VA | ABI and exact caller return |
|---|---|
| 67C310 | thiscall(self, bool, bool), RET8; D62F04. Stops/clears the current global presentation record. |
| 67C7F0 | thiscall(self, uint32), RET4; D64ED1. Selects a global presentation record. |
| B6F5E0 | thiscall(self, uint32), RET4; D2E549 and exact primary display-manager receiver. Marks that manager dirty at+70. |

The original ability-mode change and ability teaching remain native and use
B's scoped Stage. The corresponding owner-client hatch hints are not yet
projected; this is an explicit remaining presentation limit. Prefixes and
caller instructions are checked before attaching these hooks. A failed owner
validation now stops its pending native growth instead of entering the original
growth function with A's context. BUILD and HOST results do not establish
native hatch, control, Save or restored-state acceptance.

Trial105 qualifies original egg opening but stops in the native ability lesson.
The .165 implementation transports that original hint and the player's actual
ability choice through schema24 HatchFrame. The authority keeps the native
state machine: D1E930's original player/mbUseForBabyGame/type4 branch writes the
lesson order. Duplicate choices under one lesson serial are consumed once;
stale actors, epochs, owners or cues cannot enter it. The client calls the
original67C7F0 presentation method with the authority's hint and selects the
original ability mode. It consumes normal D48AB0/D1E930 input before any local
order. Native lesson completion is not inferred from cue delivery or from
the HOST transport tests.

The .166 STATIC follow-up establishes a preceding UI gate: D49560 calls
D49220 and dispatches D48AB0 only for result0. D49220 delegates to D1F650;
its original type4/player branch asks D1E6C0 whether the selected ability
matches the designated mother's current lesson. Without that branch the
replica enters ordinary combat/social targeting and returns3/4 when there is
no target. Showing the original hint alone therefore does not make the
normal lesson input eligible.

The client adapter hooks pinned D49220 as `uint32 __cdecl(int)`, one
caller-cleaned stack word, EAX result and plain RET; its prefix and the
D49568 call/cleanup bytes are checked. For the current authenticated owner
cue and the exact admitted living baby, only the authority-observed expected
ability can project missing-target result3/4 to eligible0. Original busy,
modal and other refusal codes remain refused; wrong lesson abilities return1.
Other actors, stale cues, adult avatars and non-client paths retain the
original result. This projects native lesson eligibility into the owner UI;
the existing intention path still executes original D1E930 on the authority.
No lesson success, learned bit, growth or local gameplay order is supplied.
The binding is exercised by a HOST detour/stack fixture; native completion
requires a separate observed run.

Trials112–113 subsequently qualify the bounded original Charge lesson, native
adult growth, both controls, Save and fresh adult restoration without replay.
Full C10 remains open. The .167 follow-up replaces the initial one-member herd
restriction with current original-member admission: one B, unique member IDs,
the same herd and no A. Original C6B6E0 continues to own population; it fills
only its native deficit and may count resident corpses. No herd member is
discarded. Before the pending death handler is reset, a repeated cycle must
have the same owner/epoch, no active private drama and completed adult growth.
Only bridge mother/lesson metadata is cleared; cue serials increase and the
original manager retains mother reference ownership. A first respawn after
restoring the populated herd does not exercise same-process context reuse.
BUILD and HOST checks pass. Trials115-116 subsequently qualify one original
B melee respawn after loading its populated herd: both members are retained,
normal owner confirmation reaches original hatch/growth, and A's scoped
state remains unchanged. The native sequence skips the already learned lesson
and grows B to age 1, scale 1.1 and health 12.5. Peer/B normal jumps, Save3,
strict sealing and fresh restoration of all 684 player/inventory rows and
123 nests per view pass. A's saved starvation continues on reload; B stays
adult. This combat setup uses an explicitly diagnostic network intention and
stop after one original hit; it does not qualify normal combat initiation.

The attempted second cycle in trial115 selects a different pooled baby under reused
native ID89, over 232 units away. Original approach runs until the diagnostic
timeout; no second death occurs. The stop clears the combat intention and
target, not the outstanding native walk. Trial118 subsequently qualifies
same-process private hatch-context reuse: two original melee deaths and
normal confirmations, two preserved two-member herds, one private construction
and two complete growth sequences, with A unchanged in every scoped check.
Cue serials increase from 1 through 4 without replaying the learned lesson.
Normal A/B controls, Save3 and strict sealing pass; all 684 census rows match
before and after Save. Trial119 freshly restores that new seal into three
new generations, preserving the exact 684-row census, both adult identities
and all 123 nests per view. Both normal jumps land without any hatch, death,
lesson or reward replay. B stays at health 12.5 and scale 1.1; A remains alive
while its saved starvation continues. Full C10 and all twelve complete M11
gates stay OPEN.


### Experimental foreign-species initialization, .168

The explicit local worker command `setup <epoch> 1` admits a fresh Creature
scene with one original player and avatar, then creates B through the original
herd/animal factories using a living foreign-species template. Omitted mode
or mode 0 retains the existing same-species setup. The selected B must retain
its template profile/key and initial brain level in a separate native herd.
The experimental epoch becomes ineligible for checkpoints before the factory.

The subsequent original player/reward initialization scopes one species-profile
selection to the original D2E720 normalization call returning from 4DF490 at
D2E740. The original getter still runs exactly once; only the guarded initial
B normalization receives B's resident profile. The species manager's default
key and map are not changed. Postconditions check positive finite scale and
unchanged primary Stage, identity/profile/key and progression. Later world
normalization, foreign checkpointing and secondary death routing remain
unqualified and guarded. This is not general foreign-species progression.

Trial120 verifies this initial normalization in one isolated local worker.
The native preflight finds one player and one avatar flag. Original A species
108728283 remains intact; B uses species 108728367, a separate original herd
and independent native player. Exactly one scoped profile selection produces
a valid secondary Stage at brain level 0 with A unchanged. The operator sees
the blue A creature and the visibly different green B creature.

Foreign control coverage is incomplete: B's diagnostic jump command reaches
original DoJump, which returns false with no landing; A's diagnostic jump
returns true and lands. The reason for B's refusal is unknown. Missing jump
capability is not inferred. These are worker-issued diagnostic controls, not
normal owner UI or NETWORK-REAL acceptance. No Save is requested, so the
recorded checkpoint guard is not an exercised Save-refusal result. The run
closes cleanly, all 69 frozen entries and 29 protected personal files match,
and both original disposable profile folders are restored exactly. Full C06
and all twelve complete M11 gates remain open.

### Experimental foreign-species network admission, .169-.171

An explicit authority configuration now selects the foreign-species setup.
The authority initializes B's original player context and native controller;
the experimental session admits movement, jump and stop intentions only.
Foreign progression, nests, death and checkpoints remain guarded.

The client baseline reserves original A and creates a separate foreign B
through the original herd/animal factories. It promotes the locally owned
creature with the pinned original SetAvatar and an explicit matching avatar
herd. Original native references protect both actors and herds during the
exchange. Rejected promotion restores avatar, herd and Stage; incarnation
retirement and pool counters prevent stale ownership reuse. A committed
foreign view can retain its validated normalization while disconnected,
without restoring network command admission. These are implementation
conditions, not completed native reconnect acceptance.

Trial121 (.169) initializes the foreign authority successfully, but both
client baselines reject before promotion with binding_mismatch. Trial122
(.170) adds observation-only reasons and identifies the exact rejected
condition: original A's profile ownership passes, but its animal archetype
2932903380 differs from its home herd's archetype 3636883490. Both runs stop
early, with no gameplay command or Save. Each retains 76 verified frozen
entries, six zero exits, three healthy trace stops, 29 unchanged protected
files per role and exact restoration of all three disposable profiles.

The .171 correction follows the original factory's separate default-species
path. It validates A against the unique original AED08DD4 archetype-map entry,
including the entry ID and exact native pointer. Foreign actors retain the
herd-derived archetype check. Profile ownership, keys, lifetime, references,
brain level and membership remain checked; NPC fallback qualification is
unchanged. Release builds and focused HOST checks pass.

Trial123 (.171) admits both client baselines and promotes their correct
owned actors. The green foreign B model and centered camera are visible;
portrait/history imagery still shows A, so complete UI parity is not claimed.
B's normal jump reaches the original function and correctly returns false
with jump level and selected power both zero. A 41.779 ms W pulse reaches
original WalkTo but produces no measured displacement; movement remains
NOT DEMONSTRATED. A's normal jumps land before and after B disconnects,
including a command at least 73.213 seconds after B's game exits. The attempted
fresh B reconnect is refused before game launch because planetRecords.pkp
no longer matches the admitted world hash. Six initial processes exit 0;
the extra rejected host launches no game. All original profile restorations
and protected-file checks pass. Foreign movement, reconnect, full C06 and
all twelve complete M11 gates remain open.

Trial124 uses the unchanged .171 bridge and demonstrates a bounded diagnostic
network move: original B WalkTo produces 5.5019504 units of measured native
displacement. After cleanly closing B, a reviewed helper archives only that
stopped disposable worker's active profiles and restores the pinned fixture,
preserving all three original restoration journals and the unchanged world
hashes. A fresh B process adopts baseline 3 with the same entity, generation
and species; its first position exactly matches the previously moved position.
Across 49 controlled samples, later position drift is at most 0.00007005.

A's normal jump lands after old B exits and before new B's owned baseline.
A later dies from its saved starvation, so continued survival is not claimed.
No post-reconnect B command is dispatched: the final optional attempts arrive
after automatic closure and are NOT RUN. The green B model, camera and
retained moved position are visible, while portrait/history imagery remains A.
All eight process exits, four trace stops, 78 frozen entries, protected files
and three original profile restorations pass. This qualifies diagnostic
movement and a fresh reconnect baseline with explicit fixture reset; ordinary
B keyboard movement, post-reconnect controls, automatic recovery, complete UI,
foreign persistence and full C06 remain open. No Save or checkpoint is claimed,
and all twelve complete M11 gates remain open.

### Foreign B portrait after committed baseline, .172

The portrait adapter now reproduces only the original current-avatar image
assignment. The pinned original D2C000 reads the avatar's species creation key,
uses its PNG variant, finds Creature HUD control 03D99151 and calls original
Image::SetBackgroundByKey. The adapter runs once after a foreign B baseline
has committed all entity projections and cleanup. It requires the current
authenticated epoch and live B incarnation, matching avatar/herd/player,
unchanged A/default identities and the qualified original UI bindings.
Existing display, layout and window references remain held for the synchronous
call; no UI pointer is retained across frames. The global species key is
unchanged. The broader D2C000 refresh is never called because it also changes
posse, diet and ability UI. History contents remain unverified.

The original image setter's boolean and the before/after identity checks are
recorded separately from gameplay baseline acceptance. A false result is not
treated as an unchanged image or a successful portrait update. The first
Release build exposes ambiguous SDK display AddRef/Release bases; retaining
the specifically declared IWinProc interface at +38 resolves that compile
failure. The repaired build has no warning and the focused native ABI HOST
target passes. BUILD/HOST results do not replace visible native inspection.

Closed native trial125 verifies the original image setter returns true once
for initial B and once for fresh reconnected B, with A/default identities
unchanged. No portrait refresh runs for A or authority. Live inspection and
all 64 retained filmstrip cells show the green B portrait and model in both
B sessions while A retains its blue portrait. History contents were not
opened and remain NOT_VERIFIED; this is portrait acceptance only.

Diagnostic B movement reaches 5.50665676 units before disconnect. Fresh B
adopts the same entity, generation 33, species and exact moved position;
23 controlled samples remain stationary before the next input. A second
diagnostic move after reconnect reaches 5.59924252 units final displacement
(maximum 5.59957035). A normal jump lands after old B exits and before fresh
B's owned baseline. A remains alive at closure despite its saved starvation.
Normal B Space reaches original DoJump and returns false with jump level and
selected power both zero; no jump ability is granted. Ordinary B keyboard
movement remains unqualified.

The closed review verifies 81 frozen entries, four healthy traces, eight
zero process exits, 29 protected files per run and all three original profile
restorations. Reconnect still uses an explicit stopped-worker fixture reset,
not automatic recovery. No Save, checkpoint or Save-refusal exercise occurs.
Foreign progression/persistence, full C06 and all twelve complete M11 gates
remain open.

### Private native relationship context and refused first combat, .173

The experimental foreign B context now owns a separate original relationship
manager. It uses the pinned 96-byte native allocation, original constructor
and deleting destructor. It does not register another global listener, copy
A's relationship records or replace the global manager. Exact outgoing damage
callers can select B's manager only inside an authenticated B callback. Every
nested damage call establishes its own owned or neutral scope. Original
in-flight actions retain their owner; a failed boundary fences later input
and cannot be resumed in the same scene epoch. These routing and action
conditions are implemented but are not exercised by trial126's refusals.

Experimental admission checks the original target, ability, relationship and
shared herd conditions before queueing and again during actual dispatch.
The initial bound requires an ordinary adult third-species target, effective
relation 3, a first kill strictly below both native relationship thresholds,
clear species reaction flags and an already-clear herd evolution flag.
Foreign First DNA uses the existing owner-directed deferred presentation
path. Its matching idle combat controller can finish the authenticated
receipt without a social-controller reset. Foreign tutorial delivery and
completion remain NOT RUN because no reward occurs in this trial.

Closed trial126 verifies one private native relationship manager construction
and one original deleting-destructor retirement. Both foreign client baselines
are admitted, and original B initialization reports valid Stage/profile
normalization with A unchanged. Two diagnostic intentions, approach to native
target 90 and Bite-index combat against target 95, are refused before any
combat command is queued. Both native reads are valid: effective relation 3,
fear threshold 1, extinction threshold 3, no existing A/B relationship entry,
and herd ShouldEvolve already true. A first kill would reach the fear
threshold, so these candidates do not satisfy the unchanged-relation bound.
No normal target selection or Bite input is attempted after those refusals.

Normal B Space subsequently reaches original DoJump and receives its native
level-zero refusal. A's normal Space command is accepted and lands. A remains
alive at health 1.11352503 despite continued saved starvation; B remains alive
at health 6. Both native DNA observations are zero, and B's reward, goal and
combat counters remain zero. There is no B combat damage, relationship action,
kill, reward or tutorial delivery. Original starvation damage to A continues;
this is not a claim that the scene has no damage.

The closed review verifies 89 frozen entries, three healthy traces, six zero
process exits, 29 unchanged protected files per run and all three original
profile restorations. All 48 retained filmstrip cells and live owned windows
are inspected: B keeps its green body and portrait, while A keeps its blue
portrait. Capture durations are 173.533, 173.266 and 173.433 seconds. No Save,
reconnect, foreign persistence or complete UI acceptance is added. Full C06
and all twelve complete M11 gates remain open.

The exact closed-review command is
`python local/m11-resume-2026-09-27/reviewer126.py --visual-inspected-by-root`
(exit 0). Its private report is
`local/m11-resume-2026-09-27/trial126/closed-relationship-preflight-review.json`,
SHA-256 `8800aa55c670e06ec341b88fdbd5f8559ddd03af8762f6fb503d1795f925d5c1`.
The reviewer SHA-256 is
`f48606fbc9e6ed5ce6dfb5d65fb0eb922c1993ef4d75e5426707bb85ffe5099a`.
The next combat experiment requires a reviewed original first-kill transition,
including its shared herd, history and objective effects; no native threshold
or target property is changed to force the rejected experiment through.


### Foreign first kill, private history and corpse retirement, .174–.175

Trials 127–128 exercise one diagnostic network approach and Bite intention for
foreign B against an ordinary third-species creature. Original gameplay lands
six one-damage Bites and kills native target 90; five original half-damage
counterattacks leave B alive at health 3.5. The original reward grants B 7 DNA,
7 goal progress and 7 combat progress, while A's DNA and progression remain at 0.
This is a native combat result from diagnostic intentions; normal Bite UI input is not qualified.

The first kill reaches the original feared threshold 1, below extinction 3,
and changes B's effective relation from 3 to 1. Original target-herd evolution
is disabled; the admitted shared-herd and reaction-flag effects remain native.
A's relationship, profile, owned herd and progression remain unchanged in the
recorded scoped observations. B owns the original 96-byte relationship manager
and a separate original 64-byte history container, each constructed and freed
through its pinned native lifecycle without replacing either global manager.
The history result is a new entry in B's actual Creature category 8 list with
event 5, the target species and relation 1. A's complete observed history remains
unchanged. The original First DNA presentation runs on B's client, and its
normal confirmation produces one authenticated native completion with A's
marker and progression unchanged. History UI contents remain uninspected.

Trial 127/.174 retains a later failure: about 60 seconds after the kill, original
corpse pool retirement incorrectly invalidates the completed experiment and
fences both players' subsequent commands. Its first-kill, reward, history and
tutorial results stand, but continued controls do not pass. An earlier short
normal B W pulse reaches original WalkTo/release without meaningful measured
movement; later A/B Space requests are refused after the fence.

The .175 correction distinguishes a completed target from an owned player.
After a validated kill/history result, finished callback observations and
unchanged owned state, the original target's retirement releases only its
borrowed identity. It neither dereferences the retired corpse nor discards
B's progression or private native containers. Incomplete results, callbacks
still in progress and owned-identity retirement retain their failure fences.
Trial 128 repeats the first kill and owner tutorial, then records completed
target retirement at trace sequence 37404 (native time 170.1422647 seconds)
with owned results preserved and no resulting acceptance failure or command
fence. B retains 7 DNA/goal/combat through closure. The 62 scoped A/history
checks extend through native time 170.142202 seconds,
not continuously through closure. A later dies from its continuing saved
starvation at native time 201.117213 seconds; B remains alive at health 3.5.
The sole input fence accompanies ordinary session shutdown, separately from
this successful corpse-retirement handling.

The final post-retirement controls check is NOT RUN: automatic closure at
17:06:28–29 UTC precedes the planned inputs. No normal post-combat movement,
post-retirement jump or continuous-control acceptance is inferred. Foreign
Save/reload, reconnect after this result, independent objective receivers,
history UI, further relationship transitions and all twelve full M11 gates
remain open. The [testing record](testing.md) and
[native report (archived)](public-evidence.md#historical-artifacts) retain the
separate successful results and unfinished checks.

The final closed review verifies 96 frozen entries, three healthy traces, six
zero process exits, 29 unchanged protected files per run and all three original
profile restorations. All 48 filmstrip cells were inspected; the three captures
are 190 seconds each. Whole-trial acceptance remains INCOMPLETE because the
optional post-retirement controls were not run.


### Owner Sporepedia Creature history, .176/trial129 (2026-10-01)

Trial129 passes the bounded owner-history display: B's original first kill
is replicated as an atomic schema25 snapshot and appears only in B's original
Sporepedia Creature Game collection. A keeps its own collection; B's entry
remains correct after closing/reopening. Nine original B queries return its
one owned entry. No history event or reward is replayed on either client.
The separate evolution timeline remains unqualified.

After completed corpse retirement, A's normal jump lands and B's diagnostic
move travels5.2670313native units. Both remain alive through early closure.
The final build exits0; focused native_actor_abi_host/network_host pass2/2
in5.82seconds. Closed review verifies99 frozen entries, three healthy traces,
six zero exits,29 protected files per run and all three profile restorations.
All48 filmstrip cells and the live original views were inspected.

Published original history roots retain their borrowed UI entries until
process exit, with a16-root bound and compatible-state reuse. Unpublished
roots use original deletion. This is bounded experimental history presentation;
foreign Save/reload, native history reconnect, sustained normal B movement,
full independent progression and all twelve complete gates remain open.
Exact commands, hashes, original reader bindings, UI lifetime limits and
observations are in the [native session report (archived)](public-evidence.md#historical-artifacts).
