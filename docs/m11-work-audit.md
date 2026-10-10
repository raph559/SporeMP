# M11 remaining-work audit — 2026-10-07

**M11 is IN_PROGRESS. None of its twelve complete acceptance gates is closed.**
The current work demonstrates individual mechanics in the pinned original
Creature fixture, not a complete normal multiplayer Creature campaign.

## Gate order (2026-10-07)

Gates are closed one at a time. The order follows what each needs and what it unlocks; it is the
assistant's choice under the standing permission in `GOAL.md` and may be revised with the reason recorded here.

| Order | Gate | Why here | First increments |
|---|---|---|---|
| 0 | Capabilities every gate needs | No person is available for input or review | In-process key and mouse input through the original game mode; UI readback of the original HUD and cards |
| 1 | C01 controls | Smallest remaining gap, and it qualifies the input path all later gates drive | Held-key movement soak for A and for foreign B; overlap of movement with an interaction; every ability slot through its own control |
| 2 | C03 combat, **closed 2026-10-08** | Builds directly on C01's ability input | Done: B's second kill, the shared fight, Save and restore after B's combat, recharge on the owner's bar, costs ([record](../evidence/2026-10-08-m11-c03-combat/SESSION.md)) |
| 3 | C02 eating, **closed 2026-10-10** | Shares corpses and targets with C03 | Done: plant food for a herbivore player, nothing for a carnivore, save and restore of a meal; fruit shared by the authority; two players at one fruit and at one corpse at the same moment ([record](../evidence/2026-10-10-m11-c02-eating/SESSION.md), after the [first increment](../evidence/2026-10-08-m11-c02-eating/SESSION.md)) |
| 4 | C06 separate species | Needs C03's B progression | B's own goal lifecycle and completion; species only B has met; evolution timeline |
| 5 | C04 social | Needs objective readback from the capabilities | Distinct-species exchanges, separate objectives, concurrent exchanges |
| 6 | C08 parts | | Distinct-species attribution, concurrent unlocks |
| 7 | C10 death | | Remaining causes and species, game-over |
| 8 | C05 packs and nests | | Recruitment, dismissal, migration, pack behavior |
| 9 | C07 spawning | | Within-location encounter and nest activation |
| 10 | C09 mating and editor | Largest untouched area; needs in-process UI input from step 0 | Mating call, editor commit and cancel, avatar replacement |
| 11 | C12 progression | Needs most of the above | Within-stage progress and the outgoing transition request |
| 12 | C11 extended play | It is the integration of all others | A long mixed session, repeated after restore |

Progress, C01: in-process key input exists (bridge 0.0.190) and both players' held W, A, S, D and jump key pass in
trial165 (0.0.194). Getting there exposed and fixed a client failure on fossils created during play and the original
AI walking B before its owner had sent anything. In-process mouse input exists (0.0.197 to 0.0.203): the original picks
exactly the clicked wild creature in six trials, the scenario is not yet dependable, and the other player's creature is
not picked ([POINTER.md](../evidence/2026-10-07-m11-c01-controls/POINTER.md)). The intro stall at launch has a found
cause and a fix with 24 clean starts. Player 1's normal attack runs from first contact to the kill and payout
(trials 240, 241, [ABILITY.md](../evidence/2026-10-07-m11-c01-controls/ABILITY.md)), after fixes to model drift on
clients, to selection beside a foreign-species player and to a session loss at the kill. Bridge 0.0.219 to 0.0.222 add
interface readback, every attack of a creature in the distinct-species configuration, the Jump button, and scenarios
that drive the attack stance for both players, the social stance for both players in the one-species fixture, the
bar's middle group and a two-minute soak ([INTERFACE.md](../evidence/2026-10-07-m11-c01-controls/INTERFACE.md)).
The camera of the second player's switched avatar turned by itself in the distinct-species fixture and made its held
keys walk a curve; the cause was a native walk left on that avatar, cleared from 0.0.225. **C01 is closed** on trials
298 to 314; C03 followed. See the
[session record](../evidence/2026-10-07-m11-c01-controls/SESSION.md).

**C03 is closed** (2026-10-08, bridge 0.0.238) on trials 353 to 363. See the
[closure record](../evidence/2026-10-08-m11-c03-combat/SESSION.md).

**C02 is closed** (2026-10-10, bridge 0.0.253) on trials 436 to 454; next is C06. It leaves to C07 the stocking of
plants around the second player, to C11 a late joiner's own fruit, to C10 starvation. See the
[closure record](../evidence/2026-10-10-m11-c02-eating/SESSION.md).

Latest .189/trial160 result: B's private species relationships now reach B's
own client during play as a complete, revisioned snapshot, and only B's client
receives it. In the restored fixture the values equal what the client had
adopted from the checkpoint, so nothing changes, nothing is earned and B's
Hunt 1/3 card stays as restored. This is the channel only: a relationship that
changes during play cannot be produced yet. Next: admit B's second kill on the
authority (the relationship branch at kill count 1, and a history snapshot of
more than one entry). See the
[session record](../evidence/2026-10-07-m11-relationship-snapshot/SESSION.md).

Retained .182–.183/trials148–149 result: after a fresh restore of sealed146,
B's client shows its own Blatardo Hunt card (1/3, "Hunt 2 Blatardo") while A's
client keeps 2/3. Only the owner-2 client evaluates the original goal against
a private copy of B's adopted relationship values; no reward runs. Trial148
changed the goal values but not the cached card text and is retained as a
failed presentation; .183 adds the original card populate call. This is saved
state only: live relationship changes are not replicated to the client, a
species only B has met has no card, and B has no goal lifecycle of its own.
Next: replicate B's relationship entries live and qualify a B kill changing
its own card while A's stays unchanged.

Latest .181/trials146/147 result: A completes a subsequent original kill,
reaching14 DNA while B keeps7 and moves during the encounter. Original Save and
fresh shared-scene restoration preserve679 exact player/inventory rows. A's
normal jump lands and both original collections show distinct owned results.
Both runs close cleanly and restore their profiles. B's objective panel still
shows A's hunt; the primary history census differs between the B extension-write
diagnostic and reload. Whole-primary history needs observation at its exact
original serialization point. No complete gate closes.

Retained .180/trials143/145 result: A normally clicks its killed corpse and
original feeding restores health to 10 and hunger to 100 without extra DNA.
Original Save and fresh three-process restoration preserve both owners' 7 DNA,
goal/combat progress and all 679 player/inventory rows. Both clients join;
A's normal jump lands and B diagnostically steers/releases. Trial144's native
exhausted-food reload rejection is retained and corrected. All four142–145
runs close cleanly and restore their profiles. A live new-history delivery,
foreign B feeding, full presentation and physical held input remain open.
No full gate closes.

Retained .179/trial141 result: A's first diagnostic combat reaches six original
Bites, a kill and 7 DNA. A alone receives and normally completes First DNA;
B moves 7.5253123 native units during the presentation, stops and retains its
7 DNA, private history and tutorial completion. A's authority history grows
from two entries to three. Its new client history delivery and Save/reload
remain unfinished. Both owners remain alive; six clean exits, restored profiles
and 29 unchanged protected files per role pass. Failed138–140 attempts remain
documented; the shared admission correction is qualified in141. No full gate
closes.

Retained .178/trial137 result: repeated diagnostic steering reaches original
WalkTo and StopMovement for both owners. B moves8.2438935/8.6073925 native units
in two directions and A moves14.7117400; all settle to zero sampled velocity
and less than0.001 units of drift. Correct owned views, bounded model following,
retained DNA, six clean exits and restored profiles pass. Physical held-key
input remains NOT RUN. Trial136 retains its display interruption, A projection
failure and forced B exit; it does not qualify normal play. The change adds
developer command coverage, not a native gameplay correction.

Retained .177/trials130/134 result: original Save and fresh three-process reload
preserve 679 exact player/inventory records, B's 7 DNA/goal/combat progress,
private relationship/history and existing owned actors. Both clients reconnect
to their saved creature and original Sporepedia collection; A's normal jump
lands and B moves 5.5978906 native units diagnostically. Both remain alive at
closure. Native state reads also succeeded in131–133, but their rejected client
baselines remain failures. Every trial closes cleanly and restores its profiles.
This closes the bounded foreign Save/reload gap; full C06/C11 remain OPEN.

Retained .176/trial129 result: B's first-kill history reaches only B's original
Sporepedia Creature Game collection; A's collection stays unchanged. Nine
original B queries return its own one-entry list, including after reopening.
After completed corpse retirement, A's normal jump lands and B moves 5.2670313
native units through a diagnostic intention. Both remain alive through early
manual closure, all six process exits are zero and all three profiles restore.
The separate evolution timeline, foreign Save, sustained normal B movement and
broader independent progression remain unqualified. No complete gate is closed.

Retained .174–.175/trials127–128 result: one diagnostic foreign B combat
intention produces an original kill, 7 DNA/goal/combat progress, private
authority history (category 8, event 5, relationship 1) and owner First DNA
completion, with A's progression/relationship/history unchanged. Trial127
then fences commands when the corpse naturally returns to its pool. The .175
correction preserves the completed owned results through that retirement;
trial128 records the accepted cleanup at 170.1422647 seconds without a failure
fence. A later dies through original saved starvation while B remains alive.
Each run closes with six zero process exits and all three profiles restored.
Post-retirement controls are NOT RUN because automatic closure preceded the
attempted input. Client history UI, foreign Save, sustained
ordinary B movement and broader independent progression remain unqualified.

**Native test environment restored 2026-09-30:** the earlier requested removal of the disposable accounts, staged payloads and saved worker credentials is retained as history. Workers 01/02/03 were subsequently re-created in separate OS profiles. Their current account identities match the prepared records and expire on 2026-10-14. Trial110 records 28 denied personal/game/peer write and delete probes per worker, original save/creation/temp paths under each worker's own profile, six clean game/host exits and 29 unchanged protected personal files. Its fixture restore was undone. These checks support reuse for bounded rendered trials; they do not establish exhaustive Windows isolation or additional gameplay acceptance. Trial110 stopped before any respawn confirmation and requested no Save. Trials107-109 never launched because the prior accounts had expired on 2026-09-28. Details and commands are in the native session report.

## What the evidence establishes

| Area | Recorded native result | Material limit |
|---|---|---|
| Controls | Earlier shared-species normal controls and foreign B's short127 W pulse are retained. Trial137 adds two repeated foreign B steering segments and one A segment, actual native movement/release, settled zero velocity and visible owned model following. | The short127 pulse does not establish a movement bug. Diagnostic137 does not exercise physical held keys. Sustained ordinary foreign B input and full ability UI remain unqualified; A's137 jump is accepted but closure precedes observed landing. |
| Separate species | Distinct avatars, correct portrait, diagnostic movement/reconnect, one B first kill and owner First DNA. Trial129 qualifies owned Sporepedia history; trials130/134 add original Save and fresh three-process restoration of 679 exact player/inventory rows, B's 7 DNA and private relationship/history, with correct views and continued controls. | Separate evolution timeline, broader progression, full UI, foreign lifecycle/editor/nest changes and sustained ordinary B movement remain unqualified. |
| A progression with foreign B | Trial141 adds A's first native kill and owner tutorial. Trials143/145 preserve both earned results and A nutrition;146/147 add a subsequent A kill and fresh restoration of A14/B7 DNA. A normally jumps/lands afterward and original collections remain distinct. | Combat and sustained B movement are diagnostic. B's objective panel mirrors A's hunt. A live history delivery, exact whole-primary history retention and concurrent independent encounters remain unfinished. |
| Combat and food | Normal Bite rewards and bounded B corpse feeding execute natively. Trials142/143 add normal A corpse-click feeding with foreign B: hunger reaches 100 and health 10; trial145 restores the earned result without replay. | Full abilities, costs, cooldowns, foreign B feeding, contention and other diets remain incomplete. Exhausted-corpse restoration is corrected after retained failure144; full presentation parity is unqualified. |
| Social | One bounded B greeting/Sing exchange, correctly owned immediate and delayed rewards, cancellation and saved progress. | Same species, empty pack, exclusive exchange; objective UI, distinct relationships and concurrent encounters remain incomplete. |
| Parts | .150 trials88-89 add B's original Manglerfish card and exact live owner inventory, no A notification, owner tutorial with peer continuation, Save and fresh684-row state/both312-row inventory restoration without replay. | Distinct-species attribution, simultaneous independent unlocks and editor integration remain incomplete. The extra two-client controls check after89's reload was not completed. |
| Death | .167 trial118 qualifies two original melee respawns in one authority process, preserved two-member herd and peer state, private hatch-context reuse, both normal controls, Save and strict sealing. Earlier first-lesson and populated-herd restore results remain qualified. | Trial119 freshly restores that seal with exact census/nests, both adult identities and normal controls, without replay. Combat setup is diagnostic. Remaining causes/species, game-over and full model/UI parity remain unqualified. The exact 684-row census excludes learned-ability bits and complete Stage state. |
| Persistence | Several specific earned results survive fresh original loads with exact player/inventory comparisons. Trials130/134 extend explicit restoration to the distinct-species first-kill result and correct owner views. | Extended integrated play with editing/disconnects, later progression and coordinator publication/crash qualification of schema4 remain open. |

## Features still requiring substantial implementation

- C05: full nest/migration and pack behavior, recruitment and dismissal. Initial B home replication and native save/reload now pass the bounded94-95 check.
- C06: broader independent distinct-species gameplay and progression. Trials127–134 add one original first kill, owner First DNA, private relationship/history, B-only original Sporepedia and explicit native Save/reload. The separate evolution timeline, sustained ordinary B movement, complete UI and later progression remain open.
- C07: within-location spawning and distant/camera activation that preserve both encounters.
- C08: distinct-species attribution and concurrent independent unlocks; the bounded live owner inventory/card path now works in88/89.
- C09: normal mating, editor commit/cancel, campaign avatar replacement and peer continuation.
- C10: remaining death causes/species, game-over and full lifecycle recovery. Trials112-113 qualify the first starvation lesson/growth; trials115-116 add one populated-herd melee respawn and fresh restoration. Trials118-119 add two same-process cycles, Save and exact fresh restoration with both normal controls and no replay.
- C12: normal within-stage progression and correct outgoing transition eligibility/request.

C01–C04 and C11 also retain the limits in the table. These are unfinished
features and integration requirements, not merely remaining test paperwork.
The exact acceptance contract remains [M11.md](../tests/engine/M11.md).

## Model-position failure and bounded correction

Trial79 exposed an invisible B avatar after restoring A's fossil checkpoint.
Trial81 added read-only observations: simulation and animation positions matched
B, but its rendered model stayed more than81 units away near A's saved position.
Trial82/.146 uses the original Teleport path for initial replica placement. B
becomes visible, but subsequent model motion remains stale by several units.

Build **0.0.147** extends that original relocation path to changed admitted poses
of the controlled replica avatar. **BUILD and the bounded trial84 native check
pass.** Both clients visibly follow admitted movement; both original jumps land.
The full closed trace has111 settled model readbacks per owner. Maximum model
position offsets are0.00898 for A and0.36527 for B; exact model/source equality
is not established. The683 saved player/inventory rows match without replay.
All six game/host exits are0, protected29 files and50 frozen hashes match,
and the disposable restore is undone. No games or coordinator remain running.
The preceding .146 full CTest run passed19/19 in61.69seconds and was not repeated.
Trial83 remains a prepared-and-undone fixture with no game launch.

## Execution problem and next bounded result

Trials130/134 now qualify that explicit foreign Save/reload path. Three failed
client baseline attempts are retained: the initial fresh-only player census
rejected a valid saved second player; the added saved-pair route then used an
animal-only presence predicate for player nouns. Named rejection evidence in133
identified that type mismatch. Trial134 uses authenticated saved identities and
the player noun's lifetime flags, adopts both saved actors, and passes the full
bounded continuation check. No repeated first-kill setup was needed after130:
all four reload attempts reused its sealed artifact.

Trial137 now answers whether repeated steering itself moves and stops the
restored foreign actor: it does. A real held-key check remains separate and
requires a human hold/release, since the documented desktop input API only
taps keys. Do not infer a native movement defect from the earlier95ms pulse
or launch further diagnostic repetitions to substitute for that missing input.
When physical input is unavailable, continue an unfinished foreign progression
capability rather than replaying completed save/history or steering trials.

Trials146/147 now complete the subsequent A encounter and unequal-progression
Save/reload increment. The next useful work is owner-specific objective
presentation or live A history delivery. B currently displays A's hunt counter.
Before claiming whole-primary history retention, observe its exact original
serialization point: the extension diagnostic and reload counts differ.
Do not repeat successful combat/save just to reconfirm14/7 DNA. Preserve A's
existing campaign entries instead of applying B's one-entry snapshot format.

Earlier execution plan, completed by143/145:

Trial141 qualifies A's first combat and owner tutorial in the restored
foreign fixture. Its saved hunger is zero and its final health is 0.233043432;
no health or hunger was fabricated. The next useful progression increment is
native feeding for A, followed by a Save/reload of both owners' earned results.
Prepare that sequence before launch. A's client history needs a format that
preserves its two preexisting campaign entries; B's bounded one-entry format
must not be applied to A. Trial138 demonstrates that incompatibility.

Earlier execution plan, completed by130/134:

Trial129 qualifies the next substantive feature, owner Sporepedia history,
and combines the previously missed continuation check with that new work.
Diagnostic approach/combat are prepared before launch; live original state
controls their one-time submission. The normal A jump lands and diagnostic B
movement completes after corpse retirement. Close/reopen preserves B's history,
both peers remain alive through early closure, and no repeat solely to complete
the old optional line is needed. Next, address independent foreign persistence:
the existing Save guard must remain until native profile, Stage, relationship
and history state can be restored without overwriting A or replaying rewards.

Trials127–128 add the first foreign kill/history ownership and correct the
later corpse-retirement fence. They also retain another missed final control
check: in128 automatic closure under the 210-second run limit occurred before
the attempted A Space input. No post-retirement control result is claimed, and the next run
must not repeat completed work solely to fill that optional check. Prepare
inputs before launch and use the remaining live deadline when deciding what
can still be tested. The next substantive increment must address a remaining
foreign-species capability; client history UI and foreign persistence have
not been qualified by the private authority history result.

Trial125's portrait correction and post-reconnect diagnostic commands remain
qualified. Its correct B portrait, original zero-capability jump refusal and
A jump during the reconnect gap do not need another standalone replay.

The retained .171 run124 demonstrates foreign B movement and its fresh-process
reconnect. It also retains another missed final control check: the automatic
deadline closed the games before the optional post-reconnect inputs were
dispatched. This is an orchestration failure, not a native gameplay result.
Do not repeat the same run just to fill that optional line. The next native
run must exercise a substantive new foreign-species implementation, prepare
all commands before launch, and check the actual remaining deadline before
switching from evidence inspection back to input. A's saved starvation kills
it near trial124's end; do not use this fixture for an extended living-peer
claim without addressing that original gameplay condition.

Work spent too long on successive native experiments without regularly
reassessing whole-milestone completion. Short-lived probes established useful
facts, but also produced repeated partial fixes and left the high-level status
behind the latest evidence. No completion percentage or near-term M11 ETA is
justified by those probe counts. Specific avoidable costs include repeated
fixture/capture preparation, visual integration checks delayed until after
scalar successes, and trial80 continuing launch preparation after its restore
argument failed. The launch sequence now requires a successful restore first.
A failed review assumption is corrected against retained evidence rather than
rerunning gameplay merely to obtain a cleaner report.

Trials88-89 complete the next bounded owner inventory/card feature and its fresh
restore; no extra native run was launched just to repeat an unfinished optional
control check. Both restore layers are undone, six game/host pairs exited0 and
all protected files match. The full C08 gate remains open.

Trials84 and87 answer model following and the new saved-part reload without
new builds or unrelated CTest repeats. Trial85 found a real first-part tutorial
stall; .149 trial86 fixes owner presentation and proves peer continuation before
trial87 checks the saved result. Trial84-87 restore layers are undone and all
three game/host pairs are closed. Continue one complete player-facing feature at a time and report
against its full gate, preserving the remaining scope. The user sets no wall-clock limit; the requirement is efficient progress. Each
native run must name the unresolved question, the evidence that will answer it
and the next action for either outcome. Reuse existing startup, capture and
review tooling; do not repeat unrelated host tests or successful native cases
without a changed implementation or regression concern. Stop dependent launch
steps when fixture preparation fails. Validate visible integrated behavior
early rather than accumulating scalar-only successes.

Raw logs, captures and saves remain private under ignored local storage.
The [session report (archived)](public-evidence.md#historical-artifacts) records
the retained failed attempts, pins, commands, closed traces and save checks.

## B's native nest, respawn and remaining lifecycle coverage

**Latest bounded result: .167 trials118-119.** Two original B melee deaths and
normal owner confirmations complete in the same authority process. Both
restarts preserve herd membership (2 to 2) and A's scoped state. One private
hatch sequence is constructed, then reused for the second complete original
growth; cue serials advance 1 to 2 to 3 to 4. Normal A jumps before the first
death and during the second pending death land, followed by B and A jumps
after second growth. Save3 is strictly sealed, with all 684 loaded and
before/after-save census rows exact. All 68 frozen entries, 29 protected
files per role, six zero process exits and inspected footage pass. Fresh
restoration of the new seal in trial119 preserves all 684 census rows, both
adult identities and 123 nests per view, with both normal jumps and no
lifecycle/reward replay. B stays at health 12.5; A remains alive as its saved
starvation continues. The restore run retains 67 frozen entries, 29 protected
files per role and six zero exits. The combat setup remains diagnostic; full
C10 and all twelve complete gates remain open.

**Retained populated-herd restore result: .167 trials115-116.** One diagnostic network
intention and a stop after the first original hit let native NPC retaliation
cause B's melee death. Its normal owner confirmation follows 40.788 seconds
later. The original restart preserves herd 12558 with two members, all scoped
A state checks pass, and original hatch progression skips the learned lesson
before growing B to age 1, scale 1.1 and health 12.5. A's normal jumps land
before, during and after B's pending death; B jumps after growth. Save3 is
strictly sealed and freshly restored in116. All 684 player/inventory rows,
both owner identities and 123 nests per view survive without replay; both
normal jumps land. A's saved starvation continues naturally; B stays adult.
Both runs retain 67 matching frozen entries, 29 protected files per role and
six clean game/host exits.

The second setup in115 did not cause another death: the old target ID had
been pooled into a different baby over 232 units away. Original navigation
ran until the diagnostic timeout. Stop cancelled the combat intention, not
the remaining native walk. This failure does not test private hatch-context
reuse. Trial118 above subsequently qualifies that bounded requirement. Full
C10 and all twelve complete M11 gates remain OPEN (**0/12**).

Trial117 repeats the first populated-herd melee respawn, but the planned
second setup is never issued while the game is running. First growth finishes
at 116.571 seconds. A context compaction delay consumes the remaining time;
the automatic deadline closes the workers before the second preview starts.
Nearby adult targets remain in the retained authority records, so this is an
avoidable orchestration delay rather than evidence of missing candidates or
failed combat admission. No new Save or normal jump is recorded. The next
attempt must prepare its second-cycle command before launch and continue
immediately after first growth, within the existing safety deadline.

**Retained first-lesson result: .166 trials112-113.** B's normal Charge input
(ability 7) is delivered once to the original lesson, after its original
starvation death and owner confirmation. The native mother sequence and
growth finish at age 1, scale 1.1 and health 12.5, with A unchanged during
the scoped callbacks. Trial112 records one normal jump per player, original
Save1, a strict seal and all six game/host exits 0.

Trial113 freshly loads that seal, adopts the existing actors and players,
and supplies 77 authority samples per owner with both adults intact: A at
health 10, scale 1 and DNA 35; B at health 12.5, scale 1.1 and DNA 17. Each
player lands one normal jump. All 684 player/inventory rows match the
trial112 census before and after Save, with no hatch, death or reward replay.
That comparison does not include learned-ability bits or complete Stage state.
The reviewed closure retains all 66 frozen hashes and 29 protected personal
files; the trial111-113 fixture restore journals are undone. Trial111's earlier successful
lesson/growth/Save observations remain bounded by authority exit 35 at the
checkpoint deadline and do not supply a strict seal.

The .167 trials115-116 above qualify the first revival after loading a grown
B with its original companions. That run constructs a new private hatch
context. Trial118 supplies two complete cycles in one authority process and
qualifies that reuse. Original gameplay remains authoritative; the empty primary
pack and brain-level 0 limits remain. Full C10 and all twelve complete M11
gates are still OPEN.

The earlier investigation below records how the initial blockers were resolved.

The closed trial89 trace supplies a concrete prerequisite: A's original herd208
has nest209; B's herd12558 has one member and no nest. Original RestartAtNest
(D40FA0) resolves the avatar's herd through C046F0, then uses that herd's
nest and position. At that point the authority death packet and pending handler selected native
slot1 only. Sending the existing restart message for B is not an implementation:
it would still select A, and the unmodified path requires the missing B nest.

That initial nest prerequisite is now completed in .155/.156 trials94-95:
original B home creation, model/identity replication, native Save and fresh
restoration without duplication. Both post-reload jumps land, and all684
progression rows remain exact. The separate .116 developer probe still blocks
Save. The .159 trial98 increment subsequently routes B's original handler,
penalty and restart independently, with peer continuation and Save. It does
not complete the hatch sequence: B remained at age 0 and scale 0.1. The
subsequent trial112-113 result above supplies bounded adult growth, normal
control and fresh owner restoration.

The nest integration retained failed90-93 instead of rerunning unrelated cases:
an internal request-kind mistake, an incorrect replica factory input, and then
the diagnosed missing model key. Trial94 passed creation/save but exposed
unnecessary static motion traffic and a transient reconnect. The affected HOST
test passes, and95 restores the nest with103 seconds of uninterrupted client
admission. This bounded result does not close any full C01-C12 gate.

The follow-up static audit identifies the next ownership problem more exactly.
The original death action handler is a24-byte object: D38EC0 receives the
observed reason, D401E0 advances its state and applies the original penalty,
and D405F0 releases its effect and resets it. A completed cinematic can make
that reset reposition world herds. These routines cannot simply share A's
handler state with B.

RestartAtNest D40FA0 uses B's herd through GetAvatar/GetHerd, but also reads the
manager's primary mPosseMembers vector, clears global interaction state in
D3C360, updates primary UI handlers and queues a fade cinematic. AE0930 treats
cinematic request0 as the current request; merely suppressing PlayCinematic and
returning0 would redirect B's subsequent parameters into A's cinematic. No
unqualified global call is used for B in the .159 increment. The observed home
save and reload evidence is reusable; it does not need another standalone probe.

Trials99-100 distinguish SDK GrowUp from the actual native hatch producer.
The original mother sequence calls SetAge(1); the native creature update then
performs growth. B's original age0 call instead resets its AI to NPC baby and
the earlier adapter omitted type4 mother drama. The .161 implementation adds
an independently constructed native drama context and routes only its audited
baby/mother/growth leaves. Trial101 establishes73.8 seconds of valid pending
B corpse state, then exposes A's native NPC herd reset healing that dead peer.
The .162 ownership fence excludes B's herd from that exact reset caller.
Trial102 establishes that fence and successful original B revival after82.3
seconds pending, with A already revived and continuing. B's separately
constructed original drama activates, but no baby/mother leaf runs: the
decision callbacks still select against A's manager. The .163 adapter scopes
those two original decisions to B and its current herd. Their x87 return and
argument ABI passed the affected HOST test; native hatch was still unqualified.
Trial103 missed its interaction and recording window, so it is retained as
incomplete and repeated on the same binary with automatic recorder startup.
Trial105 then reached original egg opening but stopped at the lesson. The
.165 cue/input transport and .166 native eligibility correction led to the
trial112-113 result above. None of the twelve full M11 gates is closed.
