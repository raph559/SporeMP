# M03 focused reverse engineering — 2026-09-12

**M03 is VERIFIED for the recorded native Creature fixture with bridge 0.0.12.** Reverse engineering located the nest-decision ownership assumption, native player lifecycle, reward calculation, scoped player/avatar reads and the deferred display counter. The resulting native run credits B and A separately through original functions. Full quest/species/brain progression remains M11. See [completion](../evidence/2026-09-12-m03-awards/SESSION.md). The dated investigation below preserves the earlier results and explains how the bindings were obtained.

The local Ghidra project and selected decompilations are available. This is static analysis of the actual pinned game, with existing native traces used to locate the failure. It is not a reimplementation of SPORE or a claim to have recovered its original source.

## Reproducible analysis

Executable: the installed GOG GA 3.1.0.29 PE32 `SporeApp.exe`, SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. Preferred image base: `0x400000`. All addresses below are preferred virtual addresses for this exact executable. The runtime evidence is the original Creature campaign, not an adventure or qualification of another game build.

SDK headers/address tables: clean pinned commit `cbf9206b9a823f0911cd9be0217104a49d72380b` under `external/Spore-ModAPI/Spore ModAPI/`. `tools/native/export-sdk-symbols.py` extracts 1,054 labels and source provenance into ignored `local/m03-static/`. Labels identify source leads; folded functions may have several unrelated SDK names.

`tools/build/fetch-re-tools.ps1` fetches hash-pinned portable Ghidra 12.1.3 and Temurin 21.0.12.1+1 into ignored `external/re-tools/`. It does not change the global PATH or start the game. The tool URLs, hashes and actual fetch result are in the script and [session evidence](../evidence/2026-09-12-m03-context/SESSION.md).

The reusable project is `local/m03-static/projects/SporePinned.gpr`. Initial automatic analysis reached its explicit 300-second limit and saved a **partially analyzed database**. The first export script failed to compile even though the headless process returned 0; that failure is preserved. The corrected wrapper checks export results separately. Subsequent selected function exports succeeded. Neither successful export nor a decompiler's inferred C prototype establishes a runtime ABI.

Example for a fresh named export from the existing database, without native execution:

```powershell
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey next-decision-audit -ReuseDatabase -Addresses D65980,D657F0,C0BA00,BC9160
```

Use a new RunKey; the wrapper refuses to overwrite existing evidence. `-Addresses` is one comma-separated string. Private decompiled game code and the database remain excluded from Git. The source deliverables are the research tools, concise findings and fingerprints.

## Located cause and verified correction

The original attack behavior is displaced by native nest-avoidance decider **0x04A1A0F9**, index 25 in this live tree. Its static record at `0x1587C40` points to `Decide 0xD7D900`; native `decision-02` selects it during all seven logged active-attack deactivations. Native attack-stop results and the exact tree-cleanup frame separate this selection from a target setter or a guessed perception failure.

`D7D900` checks proximity to the manager's avatar herd and reads its remembered target through `D99930`. Its player-target exemption tests the target's native avatar bit `0x200`. B has explicit bridge owner 2 but lacks that bit. This is a specific ownership assumption in a native decision, not a general inability of the game to damage non-avatar actors.

The new adapter captures the returned remembered target at exact caller RVA `0x97D9AA`. It calls both original routines once and preserves their arguments/pointer/scratch outputs. It changes a positive score to zero only when that remembered target is a live bridge-owned actor lacking the native avatar bit. Existing native behavior selection then continues; the adapter never calls a hit/damage routine itself or suppresses an AI tick.

The original decider uses seven C++ arguments occupying **eight stack words**, including a native double clock, with a caller-cleaned `0x20` stack and x87 float result. The SDK's provisional two-float clock arguments cannot be copied as a semantic clock signature. The cleanup slot at decider+0xC is null; native scratch initialization is preserved. The guarded bytes and callsites are in [nest-binding-audit.json](../evidence/2026-09-12-m03-decisions/nest-binding-audit.json). The affected real Detours ABI fixture passes 15 assertions across eight signatures.

Native `ownership-01` uses `setup`, `duel 2`, then original AI. It records **six B→NPC and six NPC→B native hits**, NPC health 6→0/dead, B health 10→4 and A health 10 unchanged. All native effect/Animal/base-damage calls pair, the 3,134-record trace closes cleanly, game/wrapper exit 0 and 29 personal hashes are unchanged. This verifies the bounded combat correction. B still earns no DNA. The capture ends near the encounter's start, so it does not provide continuous visual coverage of the complete fight. [Native evidence](../evidence/2026-09-12-m03-decisions/native-summary.json), [session](../evidence/2026-09-12-m03-decisions/SESSION.md).

## Earlier cancellation investigation, before the cause was located

Probe-03 records seven NPC3 Bite selections accepted against B2. Each active attack index is cleared on return from its next native `NPCTickAI`, 15.4362–16.6991 ms later. The native target-clear callsite is `0xD672F4` (return address `0xD672F6`). The target setter itself leaves the accepted attack index intact. The subsequent instruction at `0xD67307` resets that index inside the routine beginning `0xD672A0`. No NPC effect dispatch or B damage follows these seven attempts.

A raw pointer-table read identifies the native behavior record at `0x1583F30`: behavior ID `0x02D852E6`, activation `0xD67980`, tick `0xD67CD0`, deactivation `0xD672A0`. This agrees with the pinned SDK's 16-byte `AI::cBehavior` layout. The generic tree executor at `0xBC9160` invokes a deactivation callback both when behavior processing fails and when the selected branch changes. Therefore the observed reset identifies the cleanup path, **not the decision that caused it**.

Evidence: [native correlation](../evidence/2026-09-12-m03-context/native-summary.json), [reset callsite audit](../evidence/2026-09-12-m03-context/transition-binding-audit.json), [behavior pointer reference](../evidence/2026-09-12-m03-context/ai-reset-references.json), [table values](../evidence/2026-09-12-m03-context/factory-tables.json).

## Attack-stop predicate

The tick routine calls `0xD65980` before continuing an attack when its behavior animation state permits reevaluation. Disassembly supports a five-stack-argument, caller-cleaned bool result: behavior state, acting creature, decision flags, combatant target, animal target. These descriptive parameter names are research annotations. Bridge 0.0.7 subsequently installed this observer; its native results retain the original return and separate the stop predicate from tree preemption.

The following branches are checked against actual instructions, not only inferred decompiler arguments:

| Preferred VA | Condition / native dependency | Evidence limit |
|---|---|---|
| `0xD65985–0xD659B4` | Missing target, disabled target spatial object (`+0x75`), or destroyed target game data stops the behavior. | The trace logs herd enabled state, which is a different field. |
| `0xD659BB–0xD659CB` | Getter `0x8E8230` reads **target combatant `+0x34`**; value 2 takes an early stop branch. SDK `cCombatant.h` associates this value with death. | This is not a game-mode getter. Its ECX receiver is the target combatant. |
| `0xD65A03–0xD65A26` | Equal political IDs stop the behavior only when the acting ID is not `UINT32_MAX`. | A/B/NPC creation records use `UINT32_MAX`; equality alone does not establish this rejection. |
| `0xD65A34–0xD65A57` | Without actor flag `0x100`, `0xC0BA00(self,target)` checks native relationship value 1 (`Feared`). It stops unless `0xD657F0(state)` accepts a particular native order/context. | Relationship result and the live order at these seven transitions were not recorded. Do not force a relationship or flag as a fix. |
| `0xD65A59–0xD65A7F` | Getter `0xC0C290` checks **target** `mbStealthed` (`+0xBB1`); the branch adds native AI memory and stops. | Its receiver is the target, not the attacker. |
| `0xD65A8A–0xD65ACB` | Behavior animation completion, a decision-flag-dependent context/leash helper, and an actor-flag-dependent helper can stop processing. | Helpers and order semantics need further interpretation. |
| `0xD65ACD–0xD65B34` | Native visual detection (animal primary slot `0xC0`) and hearing (`0xC0D6C0`) both fail, with no matching target in native memory flag `0x400`. | Selection or an animation marker is not proof of continuing perception. |
| `0xD65B3A–0xD65BD0` | Additional memory, default-species and relationship conditions may clear an entry and stop. | This is another candidate branch, not a demonstrated reason for B's failure. |

Static supporting windows and hashes: [decision audit](../evidence/2026-09-12-m03-context/decision-binding-audit.json). Detailed local exports are under `local/m03-static/decision`, `eligibility`, `factory`, `initialization` and `combat-stop`. `0xD657F0` follows state `+0x28` to its creature's tree and tests context `+0x8 == 0x060995FC`; the context type/ownership still needs interpretation. The SDK's provisional name for general flag `0x100` is not sufficient evidence of its complete meaning.

`NPCTickAI` also performs campaign-avatar-centered sensing before tree execution. That is an actual dependency to understand, but it does not establish an unconditional avatar-only combat restriction.

## Factory checks that prevent a speculative fix

The harness supplies fixed personality **Guard (6)** to both newly created herds. Probe-03 records those herds disabled, whereas A's existing herd is enabled and has personality None (0). `CreateHerd` at `0xB23920` stores the supplied personality directly. These are harness construction choices; native tree selection must be understood before treating them as required multiplayer settings.

The native creature constructor calls the locomotion/spatial constructor at `0xC1FA52–0xC1FA5A`, which reaches spatial construction through `0xC439C8 -> 0xC897C0`. The instruction at `0xC898F1` initializes **spatial `+0x75` to 1**. Thus an empty disabled herd does not mean its animal was constructed with a disabled spatial object. This proves the constructor's default only; it does not recover the field's later value during a failed attack.

Animal primary slot `0x54` resolves to `0xC06660`, the native locomotion-strategy creation routine. It is not a generic "enable herd" method. Static table values are preserved in `factory-tables.json`. No herd enable/personality/player flags have been changed based on these hypotheses.

The decompiled animal factory has incorrect inferred argument placement after indirect calls. Its apparent use of a species-relative pointer as the herd is a decompiler artifact, contradicted by the machine instructions and the pinned SDK's six-argument cdecl signature. Future factory changes must use those sources, not paste the inferred C.

## Native reward context

The native Animal damage path at `0xC075E0` receives the combatant subobject (full creature `+0x5A8`), delegates actual damage, and evaluates player-owned/relationship/assist/death eligibility before calculating a campaign award. The observed B actors fail to produce any AddEvolutionPoints call after their two complete kills in probes-01/02. This is not proof of reward theft by A.

`AddEvolutionPoints` at `0xD2E8A0` has several native dependencies:

| Native destination | Effect found by static analysis |
|---|---|
| Global `0x169E398` | Campaign DNA balance and clamp. |
| Manager player `+0x10F0` | Current native goal progress is updated. The folded getter at `0xF67D40` reads manager `+0x74`; an unrelated SDK alias on the same machine-code body is not its meaning at this callsite. |
| Creature-mode display | `0xD2E2E0` adjusts its receiver pointer by `+0x128` to select an embedded counter, then calls `0xD2D560`. The latter rounds a positive amount to a signed integer and enqueues it as an eight-byte entry in the native deque at counter+4. This is queued work, not a second DNA balance. |
| `ExecuteAction(0x045AB96E, payload)` at `0xD39360` | Synchronous action listeners run; the native case compares the payload with the manager avatar and sets strategy `field_48`, which can be consumed later. |

The amount itself also depends on owner context: `C042A0` calculates the victim award and calls `C035A0`. The latter reads current campaign brain level at preferred VA `0x169E370` (the start of the native 0x50-byte cCreatureGameData object), combines it with the victim's brain level and reads native property `0x3819A4D3`. After the award, `C75840` updates native player social/combat trait progress at `+0x1278/+0x127C`. Redirecting only the final DNA addition would still calculate or assign other progress under the wrong campaign context.

`EnsurePlayer B20F40` creates native noun **0x02C21781** only when manager+0x74 is empty. Its factory thunk `B1E960` allocates 0x12D8 bytes and reaches constructor `C7B930`. The generic `CreateInstance B20BF0` links a native noun ID and manager lifetime before `EnsurePlayer` calls virtual `SetGameDataOwner(nullptr)` at slot 0x28. The native override `C7C870` initializes a unique ID, selection groups, native collectable items and properties, then registers ten global message listeners through `C75380`. `RemoveOwner C7C3C0` removes those listeners and native references. Allocating a second object without controlling those callbacks would not establish independent ownership.

**Pinned SDK mismatch:** cPlayer.h declares TYPE `0x03C609F8`, while this executable's cPlayer `Cast C755D0` and `GetCastID C79F90` use **0x02C216ED**. The fallback `B183A0` only accepts base IDs. Thus the SDK type name alone cannot authorize a cast. The header also does not supply a cPlayer-specific NOUN_ID for a generic factory template; use the verified native noun ID when implementing its lifecycle. Bridge 0.0.11 now uses the verified native noun/cast identity and qualifies two player creation/removal generations; see the [native player lifecycle](../evidence/2026-09-12-m03-rewards/native-player-summary.json). This does not qualify native stage initialization or reward routing.

The native Creature strategy constructor uses primary vtable `0x147ABC0`. Its `Initialize D47910` creates and appends the following five action-handler definitions; `ExecuteAction D39360` invokes slot 0x18 on each. The old minidumps retain the global strategy pointer but lack its handler-vector heap pages. This table comes from static construction and vtable bytes, **not a recovered live handler inventory**.

| Initialization order | Vtable | Native ExecuteAction |
|---|---|---|
| 1 | `0x147AB38` | `D40090` — avatar/campaign progression events; can reach native brain progression `D3FCA0`. |
| 2 | `0x147AB18` | `D47FC0` — native collection/unlock and campaign event paths. |
| 3 | `0x147AD48` | `D47860` — native interaction dispatch for the inspected action IDs. |
| 4 | `0x147AA90` | `D38EC0` — deferred handler state for IDs0x06CA7A75 and0xD3353639. |
| 5 | `0x147AAF8` | `D3A5A0` — avatar-scoped mating/other campaign event paths. |

The five inspected action switches do not contain a direct 0x045AB96E award branch; the outer dispatcher itself still sets strategy+0x48 when the award payload actor is the manager avatar. This narrows synchronous award handling but does not prove a second campaign context works. Native brain progression `D3FCA0` changes the campaign avatar and same-species creatures and emits native messages. Future ownership must survive those later paths. Two provisional strategy-table exports (`D39800`, `D46350`) proved to be serialization routines, not simulator ticks; no binding was made from that incorrect lead.

**Deferred consumer located:** Creature mode's `IGameMode::Update D45740` tests strategy+0x48 at `D45B19`. It collects native herds marked for evolution whose species differs from the current player species and whose positive evolution threshold is below the current native goal progress. It then advances their native generation/species/member state. The flag is cleared at `D45C2B` only when the collected list is nonempty. This is deferred world herd evolution, not merely display refresh. Its goal input `D2E360` reads manager+0x74 then cPlayer+0x10F0; it does not read spendable campaign DNA. The specific Update calls return at RVAs `0x945AA0` (player species) and `0x945AA9` (goal progress).

**Player callback layout:** cPlayer's IMessageListener subobject is at +0x34, with vtable `0x1472CA4`. Slot0 is `C79FB0`, a destructor thunk subtracting0x34; slot4 is the actual `HandleMessage C77ED0`. The latter includes global Space actions and a Creature-item reload callback. A second registered player therefore requires explicit callback ownership and lifetime handling. `C7A0A0` writes a native flag map; it does not initialize a goal. Player+0x10FC is the Cell consequence trait, while +0x10F0/+0x10F4 hold current/total goal progress.

**Evidence correction:** the sealed decisions session described `D2E2E0` as adding to a value at display+0x128. Its instructions actually use `add ecx,0x128`, selecting the counter. The sealed record remains unchanged; this correction supersedes that interpretation. Native counter growth `D2CA90` allocates0x100-byte blocks of32 eight-byte entries. The SDK's provisional display fields and default deque specialization must not be treated as a verified constructor/layout binding. See the [continued reward investigation](../evidence/2026-09-12-m03-rewards/SESSION.md).

Static metadata: [reward-binding-audit.json](../evidence/2026-09-12-m03-decisions/reward-binding-audit.json), [offline dump limit](../evidence/2026-09-12-m03-decisions/offline-handler-context.json). Private exports remain under `local/m03-static/reward-*`; exact invocations and hashes are retained by the session. Independent progression must preserve native calculation, goal/action/species ownership and queued work. A temporary avatar swap or a second DNA float is not yet a supported adapter.

**Stage initialization located:** `D43DE0` case0 sums native configuration floats at `1582E44`, `1582E40`, `1582E3C`, `1582E38` and writes cPlayer+10F4. The audited instructions are D43E76–D43EAE; D43E76 is inside that function, not a callable entry. This explains A's goal total1,000 versus newly constructed B's1. The same mode initializer configures input, UI and global world state, so calling it again for B would duplicate unrelated campaign work. `D42520` is a display/input/configuration refresh, not a player goal setter. `D2E980` resets the singleton 0x50-byte CreatureGameData and installs default learned-ability/stance values; it has no player argument and cannot initialize B while A is live without an audited owner context. No such call is installed.

## Next decision and validation budget

The decision-transition question is answered and the narrow combat adapter is implemented and natively verified. The next implementation concerns a correctly owned native cPlayer/campaign context for B, including listener lifetime, brain-level input, goal/combat progress, and deferred progression. The strategy+0x48 consumer is now located. Native player lifetime and Creature configuration-callback routing are now qualified in player-02. Implement stage/reward and deferred context next; player constructor defaults alone are insufficient. Reuse static constructor evidence rather than launching the game merely to rediscover addresses.

The next reward validation must demonstrate a real B-caused native award changing B's native progress while A's balance, goal/trait state and species progression stay unchanged, followed by enough native updates to catch deferred effects. A reverse-order A award must preserve B. Use one prepared encounter after the owner adapter exists; another unchanged fight supplies no necessary reward evidence. Do not create a fake award, mark B as the campaign avatar, or suppress native callbacks to satisfy the test.

Routine builds and focused host checks remain appropriate after code changes. Repeating already accepted M01/M02 sessions or the whole M03 matrix during each analysis step is not required. Final acceptance still needs actual two-owner combat/progression evidence; static analysis cannot supply that result. Active Stop/scene cancellation, nonzero resource costs and any server-only-avatar activation effects remain recorded as unqualified.
