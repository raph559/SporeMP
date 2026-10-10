# Project status

Updated: 2026-10-10. **Partial/experimental: the recorded shared Creature encounter is verified; the full multiplayer campaign is unfinished.**

**Public source export, 2026-10-11.** This checkout holds the development source through bridge/NativeHost 0.0.256, with the M09, M10 and M11 implementation. It keeps its own licence and its own SDK build with MIT Detours 4.0.1. The native results recorded below were obtained with the development build, whose SDK DLL is the upstream build; original-game execution with the SDK built by this checkout is **NOT RUN**. An `evidence/` path that is not in `docs/public-evidence-files.txt` names a privately archived record ([policy](docs/public-evidence.md#historical-artifacts)). [Export review](evidence/2026-10-11-source-export/SESSION.md).

| Milestone | Status | Evidence boundary |
|---|---|---|
| M00 | VERIFIED | Project contract, full M00–M20 acceptance plan and initial architecture/coverage inventory. |
| M01 | VERIFIED | Pinned build and guarded loader; three clean original-game bridge lifecycles; mismatch rejection and native path evidence; simplified player launcher confirmed working by the user. |
| M02 | VERIFIED | Matched native Off/Observe fixture, visible original actions, real invalidation/reload, read-only console, clean exits and measured presentation/CPU samples. Qualification is limited to the recorded Creature baseline. |
| M03 | VERIFIED | Native A/B commands, NPC combat, damage/reward ownership, delayed actions and reload fencing in the recorded original Creature fixture. Bridge 0.0.12 closes B reward routing. Full Creature campaign coverage remains M11. |
| M04 | VERIFIED | Original unattended actions, actual player/launcher closure independence, two OS-profile workers with overlapping original saves and observed isolation, retained crash containment, clean shutdown, and sealed checkpoint recovery with existing A/B identity adoption. Qualified for the recorded rendered Creature fixture and stated desktop/stall limits. |
| M05 | VERIFIED | Bridge 0.0.29 closes the four original acceptance clauses for the recorded living Creature fixture: native result without a second reward, challenged local mutation denial, disconnect/publication fences and retained Off behavior. Actual active-bite cleanup, foreign-species UI and usable saved-world load denial are recorded. Full scene/ability/campaign coverage is not claimed. |
| M06 | VERIFIED | Authenticated original worker/two-client scene, distinct controlled actors, native movement/jumps, fresh reconnect baselines, protocol rejection tests and inspected footage. Actual WPF Join/Rejoin preserves one player identity; closed saves and temporary ACL restoration pass. Qualification is the recorded living Creature fixture. |
| M07 | VERIFIED | Native18 qualifies the normal shared encounter; native27/.45 completes combat, contested original feeding, result-preserving reconnect, late-resource and wrong-owner rejection with legitimate continuation under actual packet delay/loss. Native28 completes the declared original fresh-session reset, native movement and jump/landing under the same filter. Inspected views, closed traces, process/filter cleanup and protected-file checks pass. Qualification is bounded to the pinned Creature fixture. |
| M08 | VERIFIED | Native01–11 qualify the pinned Creature content/editor foundation: original save/import and exact 23-rigblock/47-capability agreement; nine native part roots; pre-import missing-part refusal; canonical terrain mapping and actual-profile mismatch denial; concurrent editing/cancel and two immutable native-validated publications. Developer/local-approval boundary; no campaign avatar replacement or restart durability. |
| M09 | VERIFIED | Pinned Creature checkpoint restores both original players/actors, exact tested inventory, 8.75 native DNA and immutable creation ownership. Actual native-save and five coordinator-publication crashes preserve the selected revision; resubmission grants no duplicate reward; the recovered authority enforces a five-minute checkpoint budget. Explicit protected recovery workflow, not automatic hosting or arbitrary campaign compatibility. |
| M10 | VERIFIED | Pinned original Creature locations: separate simulation, meeting, current-state return, paired Save/publication/restart and reconnect; authenticated native global admission; 36 recorded native worker-fault cases across applicable transfer/abort/cleanup boundaries. Full campaign remains unfinished. See the native recovery report. |
| M11 | IN_PROGRESS | **3/12 gates closed** with no person in the loop (in-process scripted input, UI readback and recorded frames inspected by the assistant, labeled as such): C01 controls (bridge 0.0.225), C03 combat (0.0.238) and C02 eating (0.0.253), each with a scenario that passes after a fresh restore of the pinned Creature fixtures. C04 to C12 remain OPEN. See the increment below, the [gate map](tests/engine/M11.md) and docs/m11-work-audit.md. |
| M12 | TODO | Cell coverage can reuse the verified foundations; its native subsystem needs independent investigation. |
| M13–M19 | BLOCKED | Required stage/faction/campaign dependencies remain unfinished; full scope retained. |
| M20 | TODO | Optional native Galactic Adventures multiplayer. |

The [0.0.86 native recovery report](evidence/2026-09-26-m10-native-state/NATIVE-RECOVERY-086.md)
records the exact passed cuts, retained failed attempts, original producer
admission observations, build hashes and the final acceptance decision. The
[closed trial06 report](evidence/2026-09-26-m10-native-state/LIVE-TRANSFER-06.md)
records the paired Save, publication, restart, return and reconnect.

**Native test environment restored 2026-09-30:** the earlier requested removal of the disposable accounts, staged payloads and saved worker credentials is retained as history. Workers 01/02/03 were subsequently re-created in separate OS profiles. Their current account identities match the prepared records and expire on 2026-10-14. Trial110 records 28 denied personal/game/peer write and delete probes per worker, original save/creation/temp paths under each worker's own profile, six clean game/host exits and 29 unchanged protected personal files. Its fixture restore was undone. These checks support reuse for bounded rendered trials; they do not establish exhaustive Windows isolation or additional gameplay acceptance. Trial110 stopped before any respawn confirmation and requested no Save. Trials107-109 never launched because the prior accounts had expired on 2026-09-28. Details and commands are in the native session report.

## C++ cleanup — 2026-10-10

Bridge 0.0.254 to **0.0.256**, no behaviour change intended; wire schema 32 unchanged. Conventions:
[docs/cpp-style.md](docs/cpp-style.md).

| What | State |
|---|---|
| One enforced format (`.clang-format`, `tools/build/format.ps1`) over all 189 C++ files of `src/` and `tests/` | Done; `format.ps1 -Check` passes |
| Warnings as errors in every target, the bridge included (`/W4 /WX`) | Done; BUILD clean |
| Lint (`.clang-tidy`, `tools/build/lint.ps1`) over the sources clang can parse: `src/network`, `src/worker`, the bridge sources without SDK headers | Done; 0 findings in 24 files. Clang does not parse the game SDK's EASTL, so the SDK-facing bridge sources rely on `/W4 /WX` |
| Long functions split into named steps, repeated blocks into one helper, one-letter names into words | Done in every source file: in `src/`, functions over 60 lines went from 128 to 7 and those over 100 from 65 to none. The longest left are a 95-line table of pinned bytes and the worker's dispatcher, 83 lines, which is the order of its gates |
| Files over 1000 lines split into several | NOT DONE: `native_actors.cpp`, `native_scene.cpp` and `native_network.cpp` are still 3300 to 4900 lines each |
| Test sources restructured | NOT DONE beyond formatting: 56 test functions are over 60 lines |

Verification. HOST: `ctest --test-dir build/win32 -C Release` 20/20 at 0.0.254, 0.0.255 and 0.0.256, and the replay
suite within it. For the session and protocol sources a differential run of the restructured code against the code
before it, 1.6 million packets and 20,000 scripted sessions, was identical bit for bit (the agent that did the work
ran it; the harness is not in the repository). NATIVE, the gate scenarios after a fresh restore: on 0.0.254 trials
462 to 470, 8 of 9 pass; on 0.0.255 trials 471 to 485, 12 of 15; on 0.0.256 trials 486 to 496, 9 of 11. The two that failed on 0.0.256 pass when run again (trials 497 and 498, 52/52 each), the first once the scenario took a second view of the corpse.
Each failing run was traced to the scenario and not to the bridge: clicks that picked nothing or another object at a
corpse (trials 469, 479, 491), a cinematic taken for quiet while its button had not appeared (trials 461, 480;
the scenarios now wait for it), a creature that walked away from player 1 for thirty steps (trial492).

Found on the way, not caused by the cleanup: `held-controls.py` requires a creature at rest to stay within 0.001
units and trial485 measured 0.006 for player 1. The soak scenario, which tolerates more, measured 0.012 at bridge
0.0.248 (trial405) and 0.002 at 0.0.253 (trial444), before any of this work; `held-controls.py` last passed at
0.0.225. C01's strict scenario therefore no longer passes as written, and why player 1's creature creeps is open.

Not verified natively: the bridge sources no gate scenario reaches today (social exchanges, history, death, hatch, the content
network, the quiescence and transfer probes, the global observer). They were restructured under the same rule,
compile under `/W4 /WX` and were compared with their previous text for strings, constants and calls, one commit per
file so that any of them can be reverted alone.

## M11 current increment — 2026-10-10

**Gate C02 eating is CLOSED** (bridge 0.0.253, wire schema 32); M11 is at **3/12** complete gates and stays
IN_PROGRESS. With no person involved: fruit is one shared thing, the authority's place and state for every fruit
reaching the clients, which no longer place their own; a herbivore player eats fruit by walking to it and clicking
it, eats until fed, is saved and restored with its meal, and eats again; a carnivore player is fed nothing from
fruit and eats its kill, a herbivore none of its kill; of two players ordering one fruit at the same moment the
first creature to reach it takes it; of two ordering one corpse at the same moment the first eats and the other
eats what is left after it; hunger, health, the fruit and a corpse's food agree in the three games, and each
eater's HUD shows its health. Result, limits and what is handed to C07, C10 and C11:
[C02 closure](evidence/2026-10-10-m11-c02-eating/SESSION.md), after the
[first increment](evidence/2026-10-08-m11-c02-eating/SESSION.md).

**Gate C03 combat is CLOSED** (bridge 0.0.238, wire schema 28). With no person involved: each player kills and is paid; the two fight one creature together and the
last blow's owner alone is paid, as player 2's first kill of a species and as its second; the original Save after
such a kill restores with player 2's DNA, relationships and history as saved; a player's attacks recharge on its
own ability bar; the original charges these attacks nothing, the same in every game. Result, limits and what is
handed to C02, C06, C07/C08, C10 and C11:
[C03 closure](evidence/2026-10-08-m11-c03-combat/SESSION.md), after the
[first increment](evidence/2026-10-07-m11-c03-combat/SESSION.md).

**Gate C01 controls is CLOSED** (bridge 0.0.225).
With no person involved, both players move with held keys alone and together for two minutes, target by click, and
use the whole ability interface through the original's own buttons and keys: attacks in the distinct-species fixture,
social abilities in the one-species fixture, the Jump button. Closing it took one more product fix: on the second
player's client in the distinct-species fixture the original camera turned by itself while its creature moved, because
that avatar still held a native walk it was restored with; the client now clears it. Social abilities between distinct
species are still refused by the authority and belong to C04. Result, product changes and limits:
[INTERFACE.md](evidence/2026-10-07-m11-c01-controls/INTERFACE.md).

Earlier in this increment: both players' held movement keys and jump key pass with key input
delivered inside each player's own game process (trials 165, 167 to 169), and neither creature moves without its
owner. A click delivered the same way reaches the original's own picking, which names exactly the clicked creature
and, since a presentation fix, each player's click picks the other player's creature at the first try (six trials
in a row). Player 1 now carries a normal attack from first contact to the kill and its payout with no person
involved (trials 240, 241); see [ABILITY.md](evidence/2026-10-07-m11-c01-controls/ABILITY.md) and
[POINTER.md](evidence/2026-10-07-m11-c01-controls/POINTER.md). Game starts no longer stall on the intro movie in 24
starts since the fix, and trials run with the monitors off. Keys: see the
[C01 session record](evidence/2026-10-07-m11-c01-controls/SESSION.md). Gate order and the no-person acceptance rules are in
[m11-work-audit.md](docs/m11-work-audit.md) and [M11.md](tests/engine/M11.md).

Previous increment: **0.0.189**, trial160. Owner B's species relationships reach
its own client as a live snapshot; result, limits, commands and the cadence
measurement of the new unattended test loop are in the
[session record](evidence/2026-10-07-m11-relationship-snapshot/SESSION.md).
Native trials now run through `tools/native/native-trial.py` (see
[testing](docs/testing.md)). Full M11 remains **0/12** complete gates.

Earlier text of this section, 2026-10-01:

Work paused at the user's request after the .181 closed review. No native test
process remains running and all disposable fixture restores are undone.
Resume with B's objective ownership or the primary history serialization
observation described in docs/m11-work-audit.md; do not repeat the completed
feeding and unequal-progression Save/reload checks without a new change.

Latest progression increment: **0.0.181**, trials146/147. A continues from its
earned state, kills a different native creature with six original Bites and
reaches 14 DNA while B retains 7 and moves independently. Original Save and
fresh shared-scene load preserve all 679 player/inventory rows. A's hunt shows
2/3, its normal jump lands, and the original collections show two cards for A
and one for B. Both remain alive; twelve zero exits, restored profiles and
unchanged protected files pass. BUILD, three HOST tests and closed review pass.
The review also retains two limits: B's objective panel displays A's hunt, and
the primary history census changes from seven entries at the B extension
write diagnostic to five at reload. The exact primary serialization point is
not yet observed, so complete history preservation is not claimed. Full M11
remains **0/12** complete gates.

Retained progression increment: **0.0.180**, trials142–145. A's normal corpse
click reaches original feeding after its first kill and owner tutorial. Native
hunger reaches 100 and health recovers to 10; both owners retain 7 DNA. Trial143
completes original Save. Trial144 rejects its exhausted corpse's negative food
value; the correction preserves that original value and restores only the
saved first-feed bit. Fresh145 joins both clients, matches all 679 saved
player/inventory rows and replays no reward. A's normal jump lands; B moves
7.8252966 native units through diagnostic steering and stops. Both remain alive.
All four trials have six zero exits, restored profiles and 29 unchanged
protected personal files per role. BUILD, three focused HOST tests and the
closed native review pass. Coordinator publication/crash acceptance, foreign
B feeding, A live new-history delivery and physical held input remain open.
Full M11 remains **0/12** complete gates.

Retained progression increment: **0.0.179**, trial141. A's diagnostic combat
intention reaches six original Bites, a kill and 7 DNA. Its original first-DNA
presentation runs only on A and completes through the normal confirmation.
B moves 7.5253123 native units during that presentation, releases to zero
velocity and retains its 7 DNA, history and tutorial completion. A's authority
history grows from two entries to three; delivery of that new history to A's
client is not implemented. Both owners remain alive at early clean closure.
All six process exits are zero, profiles restore and 29 protected personal
files per role remain unchanged. BUILD and three focused HOST tests pass.

Trials138–140 retain the failed history-format expansion and the missed B-only
authority guard. Both command boundaries now share the tested admission rule.
The result is one bounded first combat, not full independent progression.
Saving/reloading A's new result and physical held-key input are NOT RUN.
Full M11 remains **0/12** complete gates.

Retained controls increment: **0.0.178**, trial137. The developer command channel
now accepts the existing validated steering/release packets. Three diagnostic
segments reach original WalkTo/StopMovement: B moves 8.2438935 and 8.6073925
native units in two directions; A moves 14.7117400. All settle to zero sampled
velocity, with less than0.001 units of later drift. Both original client views
render their own creature and retained DNA. All six process exits are zero,
29 protected files per role are unchanged and all disposable profiles restore.

This is diagnostic repeated steering, not physical held-key acceptance or a
change to native movement behavior. Trial135 first rejected the unsupported
developer verbs. Trial136 reaches movement but retains a purple display,
interrupted diagnostic, A projection failure and forced B exit35 after a
display-mode switch; it is not a clean pass. Trial137 needs no display switch.
Normal A Space reaches original jump acceptance, but closure precedes observed
landing. The prior134 landing remains the qualified result. BUILD and focused
network/release-state HOST tests pass. Full M11 remains **0/12** complete gates.

Retained distinct-species increment: **0.0.177**, trials130/134. Original Save
includes B's private relationship and history containers through their original
serializers. Fresh authority and two client processes restore the existing
players, actors, herds and 679 exact player/inventory records. B retains 7 DNA,
7 goal/combat progress and its first-kill history; A retains its own state.
Both clients reconnect to the correct creature and original Sporepedia collection.
A's normal jump lands, B moves 5.5978906 native units through a diagnostic
intention, and both remain alive through clean closure. All five trials close
with six zero process exits each, 29 unchanged protected personal files per
role, and restored disposable profiles. Trials131–133 remain failed client
baseline attempts; their native state reads succeeded but their views did not.

This qualifies explicit save/restoration of the pinned living brain-0 result.
Coordinator publication/crash recovery for this new format, foreign death,
editor/nest changes, further relationship transitions and world evolution are
NOT RUN. Normal sustained B keyboard input and all twelve full gates remain
OPEN. The repeated native steering path now passes137; a physical hold/release
check needs real keyboard input because the documented desktop API only taps
keys. No movement bug is established by the earlier short pulse. Build and focused HOST checks pass
(2/2, 6.56 seconds); checkpoint tool tests pass 7/7.

Retained distinct-species increment: **0.0.176**, trial129. B's original first-kill
history is replicated as a complete owner-only scalar snapshot. The original
Sporepedia Creature Game query displays B's orange target-species card; A keeps
its blue original-species card. All nine observed original B queries read one
entry from B's private native container. Closing/reopening the view preserves
that result without replaying a relationship action or reward on either client.

Original corpse retirement completes at 120.9861657 seconds. A's normal jump
is accepted at 134.4956214 and lands at 135.4908856 seconds; B's subsequent
diagnostic move travels 5.2670313 native units. A/B remain alive at health
0.856883049/3.5 through early manual closure. All six exits are zero, 99 frozen
entries match, 29 protected files per run are unchanged and all three profiles
are restored. The final build and focused HOST tests pass (2/2, 5.82 seconds).

This qualifies the bounded Sporepedia collection, not the separate evolution
timeline, history after native Save/reload or full independent progression.
Published history roots retain their native borrowed entries until process
exit, with a 16-root bound, because all original UI borrower lifetimes are not
qualified. Ordinary sustained B movement and all twelve full gates remain OPEN.

Retained distinct-species increment: **0.0.174–0.0.175**, trials127–128.
Trial127's diagnostic combat intention produces an original foreign B kill,
7 DNA, 7 goal progress and 7 combat progress. B's private authority history
receives its original category-8/event-5 entry with relationship 1; A's
progress, relationship and history remain unchanged. The owner First DNA
presentation completes. This qualifies the bounded first feared transition,
not client history UI, foreign Save or a complete independent campaign.

Trial127 later fences commands when the original corpse returns to its pool.
The .175 correction recognizes completed corpse retirement only after its
post-kill observations and final owned-state checks. Trial128 records that
retirement at 170.1422647 seconds with A/B results preserved and no failure
fence. A later dies through original saved starvation at 201.117213 seconds;
B remains alive at health 3.5. Post-retirement controls are **NOT RUN**:
automatic closure under the 210-second run limit occurred before the attempted
A Space input, and the tool reported
that the window was gone. This is a missed test window, not a control result.
Each trial closes with six zero process exits and all three disposable profiles
restored. Trial127's normal B W pulse reaches original WalkTo and StopMovement
but produces no material displacement; sustained ordinary B movement remains
unverified. All twelve full gates remain OPEN.

Retained .173/trial126 evidence establishes private native relationship-manager
construction/cleanup and two preflight refusals before attack execution. Its
subthreshold prerequisite did not cover the actual first feared transition;
the later trials above address that bounded branch without changing native
thresholds.

Retained distinct-species increment: **0.0.172**, trial125. The original image
setter refreshes B's current-avatar portrait at initial connection and fresh
reconnect, using B's own creation key. Both original calls return true and
preserve A/player/default-species identities; native views show the green B
portrait and unchanged A portrait. The history view is not tested. B moves
through diagnostic network commands both before and after reconnect. Its
normal Space input after reconnect reaches original DoJump, which refuses
the species' zero jump capability. A's normal jump lands during the reconnect
gap, and A remains alive through early clean closure while its saved
starvation continues. All three disposable profiles are restored. This
closes the previous optional post-reconnect command gap without qualifying
normal B keyboard movement, full UI, foreign combat/social progression or
persistence. All twelve full M11 gates remain OPEN.

Retained distinct-species foundation: **0.0.171**, trials123-124. Both clients
adopt their correct original creatures; B has a visibly different green
model and its own camera. A diagnostic network move makes B travel 5.50195
native units. After B closes, a fresh B process rejoins the still-running
authority with the same entity, species and exact initial moved position.
Its next 49 observed samples preserve that identity. A's normal jump lands
during B's absence; A later dies as its saved starvation continues. The
portrait/history icon still showed A. B's original jump refusal is explained
by its zero jump capability; this is not a successful jump or a forced ability.

The closed trial124 review verifies 78 frozen entries, four healthy traces,
eight zero process exits, 29 protected files per run and restoration of all
three disposable profiles. Controls after reconnect were NOT RUN: the
planned commands reached the already closed windows/processes. No Save was
requested. This is a developer-only foreign-species movement/reconnect
increment; normal B keyboard movement, complete UI, foreign gameplay,
independent progression and persistence remain open. Formal completion is
still **0/12**. See the [native session report (archived)](docs/public-evidence.md#historical-artifacts).

Latest death increment: **0.0.167**, trials118-119. Two original B melee deaths
and normal owner confirmations complete in the same authority process. Both
restarts preserve the original two-member herd and A's scoped state. One
private hatch sequence is constructed and reused; original growth completes
twice at age 1, scale 1.1 and health 12.5, with increasing cue serials and no
repeated learned lesson. A jumps normally before the first death and during
the second pending death; B and A both jump after the second growth.
Original Save3 completes and strict sealing succeeds. All 684 loaded and
before/after-save player/inventory rows exactly match sealed112. The closed
run retains 68 matching frozen entries, 29 unchanged protected files per role
and six zero process exits. Trial119 freshly restores this seal into three
new generations: all 684 saved census rows, both adult identities and all
123 nests per view match without death, hatch, lesson or reward replay.
Both normal jumps land. Across 65 authority samples each, B stays at health
12.5 and scale 1.1; A remains alive while its saved starvation continues
(health about 7.497 to 4.241, hunger 0). The restore run retains 67 matching
frozen entries, 29 unchanged protected files per role and six zero exits.
Combat setup remains diagnostic; remaining death causes/species, full model/UI
parity and full C10 remain open. Formal M11 completion remains **0/12**.

Retained populated-herd restore increment: **0.0.167**, trials115-116. A diagnostic network
intention provokes one original hit, then stops B's combat intention; original
NPC retaliation causes B's melee death. B's normal owner confirmation revives
it from its restored two-member herd, preserving both members and A's scoped
state. Original hatch progression skips the already learned lesson and returns
B to age 1, scale 1.1 and health 12.5. A lands normal jumps before, during and
after B's pending death; B lands a normal jump after growth. Original Save3
completes and strict sealing succeeds.

Trial116 freshly restores that seal with the same actor/player identities,
all 684 player/inventory rows, and all 123 nests on each view, including B's
nest 16485 and herd 12558. Both normal jumps land without death, hatch, lesson
or reward replay. A's saved starvation continues naturally (health about
8.763 to 4.206, hunger 0); B remains an adult at health 12.5. Both runs retain
67 matching frozen entries, 29 unchanged protected files per role and six
clean game/host exits. The attempted second cycle in trial115 selected a
distant pooled baby under a reused native ID and never caused another death.
Trial118 above subsequently qualifies two cycles in one authority process.
Other remaining causes/species, full model/UI parity and C10 remain open.
Formal M11 completion is **0/12**.

Retained first-lesson increment: **0.0.166**, trials112-113. After B's original
starvation death and owner confirmation, its normal Charge lesson input
(ability 7) reaches the authority once. The original mother sequence and
growth produce an adult B at age 1, scale 1.1 and health 12.5. A's state is
unchanged during the scoped hatch callbacks. Both players land one normal
jump; original Save1 completes and a strict checkpoint seal succeeds.
All six game/host processes exit 0.

Trial113 freshly restores that sealed checkpoint. Across 77 authority samples
per owner, A remains an adult at health 10, scale 1 and DNA 35; B remains an
adult at health 12.5, scale 1.1 and DNA 17. Both players land one normal jump.
All 684 player/inventory census records match trial112 before and after
Save, with no hatch, death or reward replay. This census does not serialize
learned-ability bits or complete native Stage state. Closed evidence preserves
all 66 frozen hashes and 29 protected personal files. The trial111-113 fixture
restore journals are undone. The later trials115-116 extend this result to
one fresh-process melee respawn from a populated herd. Trial118 above then
qualifies same-process context reuse. None of the twelve full M11 gates is closed.

Trial111 first established the bounded lesson, adult growth and Save result,
but its checkpoint deadline ended the authority with exit 35, so it supplied
no strict seal. Earlier .159 trial98 revived and saved B while it still had
age 0, scale 0.1 and health 5; fresh restoration of that older artifact was
not run. Trial102/.162 fixed A's NPC-herd reset affecting B's pending corpse.
Trial105/.164 reached original egg opening but stopped at the lesson. The
.165 owner cue/input transport passed BUILD/HOST checks before .166 supplied
the native lesson eligibility correction. These retained intermediate results
are superseded by the bounded trial112-113 result above, not full lifecycle
acceptance.

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
recruitment/dismissal, complete C10 lifecycle coverage and all twelve full M11 gates remain OPEN.
See the [native report (archived)](docs/public-evidence.md#historical-artifacts).

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
passed19/19 and was not repeated for these focused changes. See the [remaining-work audit](docs/m11-work-audit.md).

Earlier combat/death increment: **0.0.139**, trials71-72. A's normal Bite lands six
original hits and grants7 DNA (35 to42) while B stays at17; original Save
completes in71, whose sealed artifact is retained but not yet reloaded. Trial72
qualifies A's melee-death prompt on its owner client, original confirmation and
revival of the same actor, restored view and a normal jump/landing. B keeps
simulating and lands two jumps while A's prompt is open. The authority remains
connected. Trial72 Save is NOT COMPLETED: a pending-jump refusal is retained,
and the retry follows the bounded shutdown. Initial combat-death stall69 and
ASLR binding refusal70 remain in the evidence. Other death causes, repeatable B respawn
and all full C01-C12 gates remain open.

Earlier control increment: **0.0.130-0.0.131**, trials61-62. Both players have
qualified normal camera-relative cardinal/diagonal movement and key-release
stopping through original WalkTo/StopMovement. Six releases stop their own
keyboard walks; moving-jump releases wait for the original landing. All four
trial62 jumps land, with zero sampled drift after settling. Full held-key,
interaction-overlap and ability coverage remain open. Builds and19/19 HOST
checks are separate from the closed original-game evidence.

Build **0.0.129** trial59 also records normal B corpse-click feeding, original
Save and both jumps. Trial60 reloads exact682-row player/inventory state with
B17/A35 and no reward replay. Trials64-66 correct the initial visual part-bone
identification: object9771 is an original type3 Stick, with classification28,
not a collectible type2 fossil. A collection and restore are recorded in78/79; B collection, owner tutorial and exact durable restoration follow in86/87. Owner inventory and original part-card projection follow in88/89; distinct-species and editor integration remain open.
Details and retained failed attempts
are in the [M11 native report (archived)](docs/public-evidence.md#historical-artifacts).


Bridge/NativeHost **0.0.120–0.0.121** enables original hunger updates for B
inside two exact, engine-thread-scoped ownership predicates. Trials46–49
record original hunger loss, exact saved-hunger restoration, starvation damage
and death. B retains45 DNA and A35; A jumps after B dies. The suspected rate
freeze proved to be a temporary original behavior pause, ended by its native
callback without a bridge rate write. All three games/hosts close cleanly and
all29 protected personal files remain unchanged. Those trials did not yet provide B's respawn prompt.
Normal diets/eating and full C02/C10 remain unqualified.

**0.0.123** trial52 completes one normal B Bite encounter: mouse selection,
shortcut1, original approach, six native hits and one7-DNA reward (B10 to17,
A35). Both normal jumps land and original Save completes. Fresh-load trials53-55
retain the failed corpse-admission/presentation checks. **0.0.125-0.0.126**
corrects the false assumption that an original reloaded NPC corpse must have
zero health. Schema16 preserves its dead flag, positive health and nutrition;
dead-target combat/social actions remain refused. Trial56 restores exactly682
player/inventory records, B17/A35 with no reward replay, both connected clients,
matching native corpse state and both normal jumps. All closes and29 protected
personal files verify. Full C01/C02/C03/C11 remain open.

Bridge/NativeHost **0.0.118** adds bounded cancellation of an unfinished B
social exchange through a normal movement input. Trial44 records original
deactivation, retained control root, visible movement and a cleared social
meter, followed by one fresh greeting/Sing result: B10+25, A35 unchanged, then
both normal jumps. The abandoned exchange grants no reward. Release build and
19/19 CTest cases pass; all three original games/hosts close cleanly and all29
protected personal files remain unchanged. Full C01–C12 acceptance remains open.

Bridge/NativeHost **0.0.117** retains SPORE's original avatar behavior root for
B's normal control. Trial39 records B jumps after the .116 native nest factory
probe and while A is dead. Trials39–40 retain missed social responses without
a completed result. Trial41 completes a normal-control B greeting/Sing result,
keeps B45/A35, releases social ownership while retaining the avatar root, and
records both players' normal jumps before original Save. Trial43 reloads that
sealed save with zero new nouns or replayed rewards, exact 682-row inventory/
player agreement, and both normal jumps. Trial42 correctly refuses mismatched
client/authority world files before launching its authority; matching disposable
snapshots resolve the test setup. These are bounded native results. Broader social
interruption/concurrency, B's complete hunger/death/respawn lifecycle, full pack/nest mechanics,
distinct species and every full C01–C12 gate remain open.

The [Creature contract](docs/m11-creature.md) and [gate map](tests/engine/M11.md)
separate retained native foundations from normal gameplay gaps. Bridge/NativeHost
0.0.106 retains original greeting, Sing, scoring and rewards. B's First DNA
tutorial is presented on B's client while A continues playing; its completion
and earned progress survive an original save/load. Trials19–20 expose a
separate delayed species-objective reward incorrectly assigned to A. Trial21
assigns that native 25-DNA award to the player whose original relationship
transition completed it: B reaches 45 and A stays at 35. Trial22 restores those
balances without replay and matches all 682 player/inventory census rows.
Release build, 19 focused evidence tests and 19/19 CTest cases pass.
Bridge 0.0.110 subsequently qualifies a bounded exchange using B's original
mouse/keyboard controls and colored social meter. Trial29 gives B 10+25 while
A stays at 35 and completes a normal-control jump during the exchange. B's
objective panel remains stale at 2/3. The shared species, empty pack and
exclusive encounter limits remain. Trial30 isolates A's original death screen
while B plays, but its confirmation guard rejects the original message.
Trial31/.112 corrects that guard and revives A through the original restart
handler; A's world view stays black, and the extended diagnostic run ends at
the existing five-minute checkpoint deadline. Trial32/.113 fixes that view
using the original fade effect. Trial34 repeats A's pending-death reconnect,
confirmation, native revival and normal jump with healthy closed traces.
Trial33's earlier deadline stop remains a failed cleanup. Trial35 reproduces
B's combat death without a respawn prompt and premature reward-context
retirement. Trial36/.114 preserves that native context and B's 18.75 DNA while
A jumps; B's respawn was not implemented in that increment. Distinct species, independent
objectives, broader normal abilities and the full 12-gate Creature matrix remain
required. The current native
investigation is recorded in the [social baseline report (archived)](docs/public-evidence.md#historical-artifacts).
The [observer session (archived)](docs/public-evidence.md#historical-artifacts) records
commands, hashes, protection checks and the next experiment. The
[earlier session](evidence/2026-09-27-m11-start/SESSION.md) retains the website and
supporting-material refresh and initial static investigation.

## Earlier M10 implementation sessions

The following dated observations retain their historical boundaries; the current
M10 verdict and evidence are above.

The earlier M10 coordinator increment's exact commands, hashes, HOST outcomes,
and native prerequisites are recorded in
[the 2026-09-23 session report (archived)](docs/public-evidence.md#historical-artifacts).
[The 2026-09-24 native save report (archived)](docs/public-evidence.md#historical-artifacts)
records original-game UI Save confirmation in each location, closed-file and
decoded-record conflicts, and the remaining artifact ownership gate.
[The original-game writer report (archived)](docs/public-evidence.md#historical-artifacts)
correlates the conflicting record keys with native Save writer call sites,
including the failed pre-load/relocation probes and the healthy post-load
trials. It also records the one-checkpoint HOST guard. The subsequent full
Release CTest run passed 17/17 as BUILD/HOST/FIXTURE evidence. The latest
original Save reached the `cStarManager` call site that writes temporary
planet records; it did not qualify an artifact merge or a live transfer.
[The sequential native run (archived)](docs/public-evidence.md#historical-artifacts)
proves Haagos can Save after loading Satiria's full world, and Satiria can
then reload from the new full world. It does not exercise concurrent writers.
[The live two-location native run (archived)](docs/public-evidence.md#historical-artifacts)
records the matched original-game checkpoint, two live authority scenes, both
sequential player joins, both avatars in both authority scenes, a fail-closed
missing NPC template and the remaining M10 acceptance gates. No live transfer
or split-world Save was exercised.
[The one-primary native run (archived)](docs/public-evidence.md#historical-artifacts)
records separate original player censuses, native jumps, and successful
one-owner scene capture in both simultaneously running locations.
[The online one-primary run (archived)](docs/public-evidence.md#historical-artifacts)
records the selected two-save head, two online original authority locations,
separate native player joins, a durable generation-6 authority replacement,
native home-location admission and wrong-area rejection, protected-file
restoration and the remaining M10 gates.

The [2026-09-26 save analysis (archived)](docs/public-evidence.md#historical-artifacts)
corrects the interpretation of the earlier 47 differing archive records:
nested compression and serializer object identities account for much of their
binary variation. Concrete native global field differences remain.
The [projected-world reloads (archived)](docs/public-evidence.md#historical-artifacts)
successfully load each original saved location from one historical Satiria
world plus the complete independently saved Haagos `.spo`. Both original
player censuses, home and terrain match their source observations. This is
a bounded closed-artifact experiment; live publication is not qualified.
The [native player export (archived)](docs/public-evidence.md#historical-artifacts)
first exposed nearby world references, then produced a validated three-object
slice through the original serializer without changing the source census.
The [native destination import (archived)](docs/public-evidence.md#historical-artifacts)
restores that slice into Haagos with fresh registered native IDs, exact source
progression and collection state, an unchanged primary player, and an imported
creature that performs original movement, jump and landing. The subsequent
schema-3 run restores its separate DNA/ability stage through the original reader,
then completes an original Save. A fresh original process reloads both existing
native players, preserves all 732 player-census rows and the imported stage,
creates no replacement nouns and replays no rewards. The imported creature moves
and completes another native jump/landing after reload. These disconnected
experiments qualify the recorded import/save/reload path, not committed travel.
The [native input fences (archived)](docs/public-evidence.md#historical-artifacts) reject
later inputs and clear original movement requests while another location keeps
advancing. Native AI, damage and residual velocity continue after this input
fence. A separate native pause experiment then holds the actual AI counter,
8,014 noun ID/class memberships, primary actor, inventory and stage stable for
40.125 seconds while AppUpdate and 2,463 original zero-delta calls continue.
State export succeeds under that pause; the matching single-token release
resumes original AI and movement. This observation does not establish that every
global subsystem is idle.
The [coordinator changes (archived)](docs/public-evidence.md#historical-artifacts)
add durable exact replacement retries, generation-overflow refusal, session
fencing and live prepare/cancel operations that preserve active sessions.
Travel, transfer interruption and global outcome gates remain open. Builds
and HOST tests do not qualify native transfer.

The [combined .80 native transfer (archived)](docs/public-evidence.md#historical-artifacts)
passes authenticated source pause/export, strict coordinator-pinned artifact
approval and original destination import with exact source collections and
unchanged destination primary state. Ownership remains uncommitted: destination
receipt revalidation detects a changed actor flag after its stability window.
The .81 fix requires native camera transparency to settle before starting that
window and retains full actor-flag comparison. The subsequent
[trial04 (archived)](docs/public-evidence.md#historical-artifacts) completes
committed outward travel and clean source retirement.
The .80 in-flight output fix passes the native source fence; valid queued scene
updates no longer invalidate the newly acquired pause. Both games and supervisors
closed with exit 0 and all disposable and protected personal profiles were checked.

The [reviewed trial05 (archived)](docs/public-evidence.md#historical-artifacts)
completes outward travel, visiting-player native input and current-state return,
including original secondary retirement and resumed separate authorities. Its
later idle starvation is recorded separately; the planned reconnect was not run.
The [fresh original reader (archived)](docs/public-evidence.md#historical-artifacts)
observes the owner's saved spice value without another mutation. Native global
producer exclusivity still requires an admission fence; one canonical publication
alone does not establish that original producers ran exclusively.

M01 is complete under the user's recorded launcher correction. On 2026-09-08 the user confirmed “it works” and requested no further repeated tests. Completion uses existing evidence and that acceptance. No further native game launch or repeated M01 test followed the confirmation.

## M09 completion — 2026-09-22

M09 is **VERIFIED for the pinned two-player original Creature fixture**. Original
SPORE saves and reloads both actual native players, existing A/B actors, all 53
unlocked items and 624 item-status entries, B's positive 8.75 DNA/progression, and
the tested creation's exact owner/version. No replacement avatar or replayed
reward is used. Twenty fresh original loads verify the saved state across the
recovery and interruption matrix; two original clients reconnect to the same
universe with unchanged credentials and fresh applied baselines.

Actual termination before original Save and immediately after its successful
return cannot produce a seal or replace the durable checkpoint. Five real
coordinator publication terminations select the old complete revision before
CURRENT publication, or the new complete revision afterward. The uncertain-ack
retry is idempotent. Native reward/feeding resubmission grants no second reward.
The final supervisor enforces the fixed five-minute budget even while the original
game is paused. Recovery currently uses explicit stop/save/seal/commit/restart;
unattended periodic rotation, arbitrary campaign compatibility and portable
Windows-account key backups are not qualified.

Bridge/NativeHost **0.0.64**, launcher **0.1.11**. Full Release build and **16/16
CTest targets** pass; the focused updated launcher suite passes 55 HOST assertions.
Recovery views are inspected. Every owned test process is closed, all protected
disposable trees are restored and the 29 personal files remain unchanged.
[Reviewed acceptance and exact boundaries](evidence/2026-09-22-m09-persistence/acceptance.md),
[D01–D08 gate map](tests/engine/M09.md),
[persistence/recovery contract](docs/m09-persistence.md).

## M08 completion — 2026-09-22

M08 is **VERIFIED for the recorded native Creature content/editor foundation**.
The reviewed public source is published as
[`b3536de`](https://github.com/raph559/SporeMP/commit/b3536de4e2bd1233aeba99bfb94ba89d4bb370cf),
with successful GitHub host/website checks. The separate website is deployed from
[`25cc334`](https://github.com/raph559/sporemp-site/commit/25cc33456282ea398b8e8cc0853b12bd80680cc2);
its live English/French M08 pages were verified. The public export preserves its
independent license and Detours 4 SDK migration: its Release build and 15/15 HOST
targets pass, while native execution with that rebuilt public SDK remains NOT RUN.
The native results below refer to this development checkout's frozen payloads.
[Public export verification](evidence/2026-09-22-m08-publication/SESSION.md).
Original authority and receiver import two fresh native-produced creations over
TLS, derive matching 23 rigblocks/47 capability entries and map distinct local
resource keys to immutable global versions. Nine part roots resolve through the
original resource managers. An absent part is refused before receiver PNG import;
a changed persisted terrain record is rejected before game creation with its
exact filename. The actual canonical terrain property maps into PlanetScripts.

A changed original editor draft is cancelled while the other player moves and
jumps; the saved source archive stays byte-identical. Owner-authorized commit
waits for native authority validation and every live participant's readiness,
publishes once, rejects a duplicate, and preserves the version on later cancel.
Original received model/test-drive views and continuing shared gameplay were
inspected. Ten .49/.50 processes close cleanly; 29 personal files and six world
files stay unchanged per run. The final .50 Release build and **14/14 CTest targets**
pass, including 244 tooling tests, 190 registry checks and 937 content/TLS checks.

[Acceptance and commands](evidence/2026-09-22-m08-completion/SESSION.md),
[verification](evidence/2026-09-22-m08-completion/verification.json),
[transaction contract](docs/m08-content-transactions.md),
[all gates](tests/engine/M08.md).

The negative missing-part authority is an explicitly labeled HOST fixture; the
receiver lookup/refusal is native. Successful publications use the real original
authority. Earlier starvation, notification-order and closed-pipe attempts are
retained as failures. The final terrain mismatch check changes a backed-up
isolated profile, refuses startup, then restores and verifies every profile file.

Qualification uses a pinned installed-content profile and fixed canonical world,
locally approved creations and developer editor controls. It does not claim
arbitrary mod/planet support, a player-facing sharing UI, campaign species
replacement or durable registry recovery. The full global five-stage plan stays
unchanged. **Next smallest step: M09 durable native checkpoint/content-version
commit and recovery, starting with interrupted commit and coordinator restart.**

## Earlier M08 creation increment — historical boundary

Four authorized original-game runs in isolated profiles now establish the saved
creation representation and a real clean-profile PNG import. The original save
uses BEM in EditorSaves; the imported creation uses CRT in Pollination and receives
a different local key. Both native loader inspections return exactly matching
**23 rigblocks and 47 capability entries**, with correct owned release. Original
editor/test-drive views were inspected. Cancelling an orange draft preserves the
saved cyan creation; the source EditorSaves and PNG remain byte-identical.

The existing native Satiria load also emits its home generated-terrain key, but its
complete relationship to persisted terrain resources remains unqualified. This is
not a completed dependency/readiness barrier or multiplayer editor transaction.
**At this earlier boundary M08 remained IN_PROGRESS.** All four games/supervisors close cleanly, all traces
have healthy footers, and the 29 personal files plus six guarded world files stay
unchanged. The updated Python driver passes **19 focused/223 tooling tests**;
native artifacts retain the prior .46 build and 12/12 HOST test identity.
[Native session](evidence/2026-09-22-m08-native-creation/SESSION.md),
[acceptance map](tests/engine/M08.md).

## M08 content and world admission — before native runs, 2026-09-22

Bridge/NativeHost **0.0.46** and launcher **0.1.10** add actual-profile prelaunch
world-file hashing and six declared identities to the coordinator handshake.
Different companion world files now receive a precise refusal before Welcome or
a scene baseline. Wire schema is **5 / 600 bytes** and configuration schema is
**2**; regenerate older private session configs. C++ and Python readers agree on
all six identities in the two closed prepared baselines, with all source hashes
unchanged; current workers 01/02/03 also have matching bundles.

The isolated developer `inspect_creation` command calls the pinned original
creature loader, copies bounded rigblock/capability scalars and releases its owned
result. It rejects loading, replicas, stale requests and malformed keys. Its
single-request driver checks exact process/trace provenance and preserves unknown
outcomes without retrying. A post-load observation additionally records the home
planet's generated-terrain key. A passive observer records original editor
request/accept/cancel messages without treating them as committed transactions.
At this pre-native build boundary these additions were **IMPLEMENTED_NOT_RUN**;
neither a copied native value nor matching prelaunch files establish complete
terrain, dependency or gameplay qualification.

The final .46 Release build passes. All **12 HOST/FIXTURE CTest targets** pass in
**26.93 seconds**, including **2,963 network assertions**, **280 worker assertions**,
**18 native-shaped content ABI checks**, **26 editor decoder checks** and
**219 Python tooling tests**. No original
game was started. Fresh verified backups protect the two prepared worker profiles
and 29 personal files. Native editor creation/import and all M08 native acceptance
gates were **NOT RUN** at that boundary. The subsequent native results are recorded above.
[World guard](docs/m08-world-identity.md), [native inspection](docs/m08-native-content-probe.md),
[complete gate map](tests/engine/M08.md), [current session](evidence/2026-09-22-m08-admission-inspection/SESSION.md).

## M08 offline content foundation — 2026-09-21

The second increment adds bounded whole-copy comparison, including opaque creation
files and per-archive resource differences, and a static audit of four native editor/import
entries. Current checks pass **32 focused tests and 202 tooling tests**. Two disposable
profiles have verified closed backups; each has 27 data files and 17 archives containing
4,014 active records. Their creation folders are empty. Native editor creation and peer
import/load are **NOT RUN**, pending current desktop availability and refreshed personal-save
gates. No game was started. [Current session](evidence/2026-09-21-m08-content-foundation/editor-preparation.md),
[native investigation](docs/m08-editor-audit.md).

**At this earlier boundary M08 was IN_PROGRESS.** The first reviewable increment adds bounded offline DBPF/DBBF and RefPack inspection, decoded native resource identities, exact archive comparison, and a private immutable candidate quarantine. All **23 focused HOST/FIXTURE tests** pass with Python 3.11; the existing `tooling_unit` CTest target passes **193 tests** with Python 3.14. The full suite exposed a Windows handle/path `ctime` discrepancy; the corrected check preserves file identity, size, modification-time and same-handle change detection. The initial failure remains recorded.

Six closed historical native-produced archives were inspected without starting SPORE: **2,950 active records, including 108 compressed records**. One generated-model record was privately quarantined and reused with identical identity; all six source archive hashes stayed unchanged. The sampled EditorSaves is empty and Pollination contains catalog summaries, not a new `.crt` fixture. This is HOST file evidence, not native creation/import acceptance. Complete dependency enumeration, authoritative native gameplay validation, canonical terrain synchronization, multiplayer readiness and editor begin/commit/cancel remain unimplemented. The next native experiment is one original editor save and clean second-profile load with exact before/after resource inventories. [Implementation](docs/m08-content.md), [acceptance map](tests/engine/M08.md), [session](evidence/2026-09-21-m08-content-foundation/SESSION.md).

## Source repository publication — 2026-09-21

The 2026-10-11 reviewed export brings the M09, M10 and M11 source, the native trial runner and the M11 gate reports
into this public checkout, again preserving its license and Detours 4 SDK work. Build and test results of this
checkout: [export review](evidence/2026-10-11-source-export/SESSION.md).

The 2026-09-22 reviewed export brings the M07/M08 source and acceptance reports
into this public checkout while preserving its independent license and Detours 4
SDK work. The current public Release build and **15/15 HOST/FIXTURE CTest targets**
pass. [Export review and qualification boundary](evidence/2026-09-22-m08-publication/SESSION.md).

Public evidence is now curated: concise acceptance/report exports stay in Git, complete historical traces and operational records are archived privately, and reusable analyzers/generators live under `tools/native/`. An allowlist and CI size/type guard prevent accidental raw-evidence publication. Public history is filtered without changing the private M07 checkout or milestone acceptance. See the [evidence index](evidence/README.md) and [cleanup report](evidence/2026-09-21-evidence-cleanup/SESSION.md).

Original project code now uses **GPL-3.0-or-later**, with third-party terms and artwork scope retained separately. The supported SDK/bridge build replaces Detours 3 with pinned MIT Detours 4.0.1. Release compilation and **10/10 HOST/FIXTURE CTest targets** pass; Debug SDK/base compilation and its SDK-wrapper test pass. Compiler/link input checks exclude the old dependency, and targeted/incremental builds reject stale SDK artifacts. Native gameplay with this rebuilt SDK is **NOT RUN**; earlier milestone evidence remains tied to its historical payload. That historical dependency migration preceded the separately reviewed M07/M08 source export. See [licensing](docs/licensing.md), [migration details](docs/detours4-migration.md) and [validation](evidence/2026-09-21-detours4-license/SESSION.md).

The mod source and reviewed development evidence are hosted at https://github.com/raph559/SporeMP. The initial private publication retained the original local history; public preparation redacts personal identifiers across that history and omits historical desktop captures. Private originals remain archived separately. `origin` points to the public repository and local `main` tracks `origin/main`. A fresh incremental Release build and all 9 HOST/FIXTURE CTest targets passed before the initial publication; no new native gameplay acceptance is claimed. Public contributor guidance, issue/PR templates and Windows Python plus website CI checks are included. This initial publication predates the M07/M08 completion now recorded above. See `docs/repository.md`, `docs/public-evidence.md` and `evidence/2026-09-21-repository-publication/SESSION.md`.

## Public presentation website — 2026-09-21

The independent presentation website is published at https://raph559.github.io/sporemp-site/ with English by default and a complete French translation under `/fr/`. It contains a project overview, five dated development articles, selected launcher changes and a future-work roadmap. The EN / FR selector preserves article identity. Publication is automatic from the separate public website repository; only reviewed website sources and the original launcher illustration are included. The user-supplied Spore MP Leetchi fundraiser is configured for both languages. This website does not change native milestone status or announce a public multiplayer release. See `docs/website.md` and `evidence/2026-09-21-website/SESSION.md`.

The public website repository now uses English for documentation, default content, workflow labels, metadata and new URL paths. French translations live under `locales/fr/`; compatibility mappings preserve previously published French article paths and section fragments. Follow-up evidence: `evidence/2026-09-21-website/ENGLISH-REPOSITORY.md`.

## M07 shared encounter verified — 2026-09-21

**M07 is VERIFIED for the recorded original Creature fixture.** Native18 supplies the normal shared encounter and original retaliation against A. Native27/.45 supplies the complementary shared encounter with retaliation against B and completes contested feeding, fresh B reconnect with its original7-DNA result preserved, refusal of a late consumed-resource request, a real wrong-owner rejection and subsequent legitimate B jump/landing under actual25 ±5ms packet delay and1% sampled loss. Native28 then executes the declared original fresh-session scene reset under the same live filter, adopts new native baselines and completes B movement of at least1native unit plus jump/landing. The [acceptance decision](evidence/2026-09-21-m07-completion/acceptance.md) evaluates every G01–G09 gate and retains all failed attempts.

Original-process initial, corpse and post-reconnect views were inspected. All owned games, supervisors, coordinators and recorders close; the packet filter is removed. The29 protected personal files and six disposable save trees remain unchanged. The .45 Release build and10/10 HOST/FIXTURE CTest targets pass; the subsequent Python observation correction passes41 focused tests. Full stage/campaign coverage, arbitrary life/model transitions, restart durability, Internet usability and headless operation remain outside this fixture qualification. **M08 followed this historical milestone; it is now complete within its recorded scope, with M09 next.** Changes remain in the private development checkout; this completion does not publish or release multiplayer.

### Earlier M07 stopping points and investigations

**Earlier stopping point, before the explicit resume:** Native20/.45 passes shared combat under actual packet impairment: A hits the common NPC once, B five times, the NPC hits B four times, and B receives the original 7 DNA. Its preserved pickup archive passes the unchanged strict evaluator, including exact food/beneficiary revisions on both clients and original consumed-corpse retirement. The live pickup check initially reads a shorter authority prefix than B's trace; reviewing that diagnostic mismatch consumes the reconnect margin. Fresh B is closed in the menu by the unchanged deadline before its baseline. The run observes 66,079 captured packets and 665 actual sampled drops, with 50 matching dropped-data retransmissions, balanced reinjections, no helper errors and verified filter cleanup. This is substantive impaired combat/pickup evidence, not a completed impaired reconnect or full M07.

The harness now reads client traces before the authority trace and continues any failed final convergence check only to its original deadline; it never retries gameplay intentions or weakens exact revision matching. All 41 focused HOST tests pass. A single final automated attempt, native21, stops before gameplay because the actual visual-review marker arrives at authority age87.385s, after the frozen80s reset-jump admission guard. This orchestration failure is preserved; no further native retry or final reset is launched following the user's usage/time concern. The remaining native gates are the impaired result-preserving reconnect, consumed-resource late request and wrong-owner challenge with legitimate continuation, and the selected final fresh-session reset. Production remains .45; its build and all10 CTest targets passed before the final Python-only correction.

Native18/.44 records three real hits from each owner against the same NPC, original retaliation against A, and one 7-DNA kill award to B. Both original feeding orders precede the first-feed call; A receives 481 positive nutrition transfers while the original feeding DNA amount is zero. Exact client source revisions, original pool return, both removals and unchanged owner balances qualify consumed-corpse retirement. Fresh B applies baseline 3, retains 7 DNA and one controlled actor, and overlaps the connected authority/A for 14.203 seconds before clean shutdown. The original pickup parser's false result and late-archive reconnect failure remain preserved alongside the separate corrected evaluations. The stricter same-fight target-switch driver remains inconclusive; the explicitly declared paired shared trials still require both owners to damage the same NPC in each trial and original retaliation against B in the next trial.

The .44 supervisor distinguishes original simulation readiness from authenticated replica projection readiness; native18 records healthy app/projection progress while replica AI is deliberately suppressed. The full Release build and 10/10 HOST/FIXTURE targets pass. Native19 starts under the declared 25 ± 5 ms packet delay and 1% sampled-loss profile but closes before gameplay: a slow initial capture is immediately followed by three equal-tick updates, which the coordinator correctly rejects. All processes close cleanly, the packet filter is removed, and protected files remain unchanged. The next change fixes the publication schedule without relaxing receiver fences. This failed short run has no actual dropped packets and does not qualify impairment acceptance.

The `.42` corpse-presentation correction passes the Release build and **10/10 HOST/FIXTURE targets** (13.25 seconds). Native14 actually projects the previously rejected dead=true/health0/combatant0 combination on both clients, retaining corpse animation in 60/60 later samples each. Original A death and revival also reach both clients with matching life/context samples and clean exits. Fresh Join and an accepted post-revival action are not run in that attempt; exact baby-model presentation remains limited by the recorded views. [Native14 lifecycle evidence](evidence/2026-09-21-m07-completion/native14-lifecycle.md).

Native15 starts its fight after A has already lost health to hunger. A damages the NPC three times, then dies before B reaches attack range. Original simulation pauses during the death prompt; B's bounded approach expires during that pause and does not restart after revival. No B hit, pickup or reconnect is claimed. The separate replica `simulation_stalled` status is a supervisor diagnostic defect: client AI is deliberately suppressed while app updates and scene projection continue. A role-specific readiness correction and an earlier, bounded combat kickoff are the next increment. All native15 original processes close cleanly, with all 29 protected personal files and six disposable trees unchanged.

Native11/.39 genuinely exercises the qualified inactive-herd factory fallback, but publication stops during the native interval with health zero and dead flag still false. Native12/.40 projects that interval and the later corpse correctly on both clients. Two original feeding orders overlap and A consumes food, but the loaded original first-feed DNA amount is zero; .40 wrongly requires a positive DNA increase to retain beneficiary metadata. Later, an unrelated initialized NPC returned to SPORE's retained pool stops publication. [Native11](evidence/2026-09-21-m07-completion/native11-combat.md), [native12](evidence/2026-09-21-m07-completion/native12-combat.md).

Bridge .41 distinguishes an observed original reward call from an actual positive balance increase, preserving zero DNA while requiring separate positive native food/nutrition evidence. It also tracks the pinned original pool-cycle counter, retires scene/queued-command/beneficiary identities on the original pool return, and excludes only the qualified inactive pool signature. The Release build and all **10/10 HOST/FIXTURE targets pass** (13.37 seconds; 2,662 network assertions, 163 Python tests). [Pickup qualification](docs/m07-native-pickup.md), [pool lifetime qualification](docs/m07-native-pool.md).

Native13's contested-food check passes: one first-feed call with zero DNA, 481 positive original nutrition callbacks, no losing-owner nutrition transfer, and exact source-tick food/beneficiary agreement on both clients. The original pool returns and consumed corpse removal continue without an authority error. The staged fight itself fails its target-switch gate: B's original AI resumes attacking after Stop and kills the NPC before A arrives. Later, native slot79 is reused with a new generation/species and native dead=true, health0, combatant state0. The client's stricter corpse-animation guard rejects that real combination; client A stops projecting it and fresh B fails its baseline. This preserves **G04's bounded pass and G05's failure**, not full M07 acceptance. The 270-second watchdog closes the correct original/fresh generations; all game, worker and recorder exits are clean and protected files remain unchanged.

Completion work continues in [the current session](evidence/2026-09-21-m07-completion/SESSION.md). Native07/.36 exits the scene before combat; native08/.36 loses a client to a status-publication failure before combat. Native09/.37 repeats actual shared damage and isolates the missing-template condition to a present inactive herd with a matching profile candidate. Its full herd census completes; profile ownership and native creation still require qualification. The .37 supervisor publication correction passes actual Windows contention fixtures and the affected 3/3 HOST suite. All three attempts preserve protected save trees. No complete M07 encounter or packet-impaired acceptance is claimed.

Native10/.38 records B's five hits and A's one hit on NPC79, three original NPC hits against B, and one original 7-DNA reward to B. Both clients apply the corpse animation once and retain it in all 72/220 subsequent counted corpse samples. Inspected exact-process recordings show the corpse and the corresponding player balances. A real wrong-owner request is rejected; B subsequently completes an original jump and landing. All processes close cleanly with protected saves unchanged. The source manifest's inherited 150-second bound was exceeded; the 180-second recording and approximately 305-second native trace discrepancy remains explicit. The existing-herd fallback was not exercised: an ASLR-sensitive guard disabled it while living templates supplied the new creatures. [Reviewed native10 report](evidence/2026-09-21-m07-completion/native10-combat.md).

Bridge .39 corrects those loaded-address guards and adds the original corpse-eating order/callback, scoped first-feeding reward observation, and schema 4 flag/food/beneficiary projection. Native claim and consumption remain original SPORE behavior. Optional EatMeat property globals remain outside general progression qualification. [Pickup binding and evidence boundary](docs/m07-native-pickup.md). New native execution is still required for these changed bindings.

The initial Bridge/NativeHost **0.0.31**, wire schema **2**, increment integrates the existing qualified M03 movement/attack/approach/engage paths with authenticated client intentions. The coordinator replaces untrusted client actor fields with authoritative state, checks the target's generation, rejects dead actors/targets and retains the NPC-only target policy. The authority initializes the existing B player/reward context and exports its observed DNA rather than a fabricated zero. This phase had HOST coverage only; the subsequent native attempts and current .35 state are recorded below.

The retained M06 trace proves that owner A dies and revives on the same native noun. The scene adapter now retains that noun's identity and projects explicit health/dead scalars rather than requesting destruction of the client's own avatar. Pinned executable set/clear instructions and the SDK field layout are checked. This does not execute native death/revive handlers or qualify death animation/UI, real owned-actor destruction or B respawn progression. Post-respawn Join required the further native context correction verified in native04 below. See [life-state analysis](docs/m07-life-state.md).

The .31 reviewed Release build and all **9 HOST/FIXTURE CTest targets pass** (12.16 seconds; 2,564 network assertions and 132 Python tests). These include dead-actor refusal, target-generation fencing, corpse late-join state, same-identity revival without balance addition, request-correlated refusal, native-evidence parser checks and bounded TLS proxy cleanup. The failed Python 3.14 proxy shutdown attempt and its correction remain in [the implementation report](evidence/2026-09-21-m07-encounter/SESSION.md). Full M07 acceptance is retained in [the native protocol](tests/engine/M07.md) and [encounter design](docs/m07-encounter.md).

After explicit desktop authorization, three bounded original-game attempts ran in the previously verified distinct profiles. Native01 preserves owner A's dead noun and a fresh client adopts the dead baseline; that authority later exhausts the old 32 MiB trace budget. Native02/.32 raises the trace budget to 64 MiB without reducing sampling and observes original same-noun revival, then both clients reject NPC97's health10 against local maximum5. Native03/.33 diagnostics show the authority's NPC97 becoming adult (age1/max10) while clients retain age0/max5. Both clients also retain the wrong age for the revived player and the wrong Combatant state at death. These are retained failures, not complete respawn or presentation acceptance. All .32/.33 original games exit cleanly; all 29 personal files and six disposable Games/Creations trees remain unchanged.

Bridge/NativeHost **0.0.34**, wire schema **3** (400 bytes), carries observed age, alpha status, Combatant state and native scale. The existing maximum-health check runs with that replicated context, preserving the local avatar flag. Original scale calculation contains randomness, so replicas use the authority's observed scale through the pinned original setter. Native04 closes the diagnosed scalar/scale lifecycle failure: original death and same-noun revival reach both clients; a fresh B applies the 33-entity post-respawn baseline before Connected and completes native move/jump. Exact matching source-tick comparisons total 722/622/121 across the two original clients and fresh B, with no differences in the reviewed life/context fields. All four original games close cleanly and protected saves remain unchanged. Full baby/adult model-resource regeneration and death/game-over presentation remain unqualified.

Native05 then records both authenticated players damaging the same original NPC79: four A hits and two B hits reduce native health6 to0; one original lethal award changes A's DNA0 to7. The NPC retaliates against A; targeting B is not observed. Client A follows the health/death scalars and reward, but its recorded dead target remains visibly upright; B stops applying updates on `Native entity spawn failed`, before the terminal outcome. Full three-machine encounter agreement therefore fails. [Closed combat and visual evidence](evidence/2026-09-21-m07-encounter/native05-combat.md).

Bridge **0.0.35** adds diagnostic-only spawn-result details. Native06 reproduces `missing_template` on both clients for newly active entity `12884901957`, generation34, species108668588, native69/herd1772. The full existing selector returns no eligible living native template and the factory is never called. The supplementary 4096-noun census is explicitly truncated; it is not exhaustive absence evidence. The next bounded investigation is an exact local herd/profile census and a qualified native creation path independent of an already living same-species animal. Existing model/death presentation, original NPC response to either owner, contested pickup, reward-preserving reconnect after combat, a real unauthorized client challenge and configured latency/loss remain open. A delay-only relay cannot qualify packet-loss testing. All final .35 BUILD/HOST tests pass (9/9, 12.44 seconds, 2,595 network assertions, 132 Python tests); all native06 games/workers close with exit0 and protected trees remain unchanged. [Verified .34 lifecycle boundary](evidence/2026-09-21-m07-encounter/native04-lifecycle.md).

[Exact native06 spawn failure and next experiment](evidence/2026-09-21-m07-encounter/native06-spawn-blocker.md), [final source/payload and result manifest](evidence/2026-09-21-m07-encounter/native-verification.json), [complete M07 session record](evidence/2026-09-21-m07-encounter/SESSION.md). All test games, workers, coordinators and recorders are closed. The changes remain uncommitted on the private development branch; no publication was performed by this increment.

## M06 native networking acceptance

Bridge/NativeHost **0.0.30** and launcher **0.1.9** implement certificate-pinned TLS, authenticated authority/two-player sessions, scalar entity baselines, reliable lifecycle updates, interpolated native poses and client intentions. Native06 has two original client processes applying the worker's 33-creature scene, **400/400** matching sampled native poses, all four original movement/jump action checks, unobstructed original-game recordings and clean exits. Native05 supplies a client process reconnect with a fresh baseline and the same controlled player identity. [M06 acceptance decision](evidence/2026-09-14-m06-network/acceptance.md).

Actual WPF **Join → close → Rejoin** in native09 launches two successive original games. Player 2 keeps entity `12884911489`, generation **33**, and one native avatar while receiving fresh baselines **3 → 5**. Both original games and backend runs close cleanly, with temporary session credentials removed. This lifecycle check uses an original-menu-paused living authority; unpaused gameplay is qualified separately by native06. [Actual launcher archive (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-14-m06-network/launcher-native09-closed/report.json"), [inspected UI (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-14-m06-network/launcher-native09-ui-review.json").

The longer native05/native07 runs reached a real starving-player death and client-avatar removal failure; native08 attempted Join after that death/respawn and failed. These failures remain an explicit **M07 death/respawn gap**. This is living-Creature networking qualification, not complete co-op or campaign progression. Final checks retain all **29** protected personal files, all three disposable Games trees with their **16** original files each, and empty Creations trees. All test games, supervisors, coordinator, launcher and recorders are stopped; all **20** temporary ACL targets match their original permissions after six ordered restores. Final CTest passes **9/9** in **10.08 seconds**, separately labeled HOST. [Final save checks (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-14-m06-network/final-save-check.json"), [ACL restoration (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-14-m06-network/launcher-acl-restored.json"), [process closure (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-14-m06-network/final-process-check.json").

## M05 completion in the recorded Creature fixture

Bridge **0.0.29** completes the four original M05 acceptance clauses under the recorded living-Creature qualification. An actual native **0 → 8.75 DNA** combat result is applied absolutely without another native award. Immutable replica roles, application guards, entity/baseline fences and mutation counters remain active after disconnect and scene exit. [Acceptance decision and remaining capability boundaries](evidence/2026-09-14-m05-ability/acceptance.md).

The new original-game tests cover an already-active native B bite, its later animation cleanup and a natural foreign-species social/UI callback with unchanged award state. Native-02 retains **787** matching avatar/DNA samples through **197.056 seconds** after application. The final .29 run also denies the actual saved-world Play action before it hides the panel; Cancel returns to the complete galaxy menu. Filename-load challenges leave native mode and bindings unchanged. All six new native runs exit **0**, with healthy traces, zero foreign callbacks, unchanged closed worker Games hashes and all **29 personal-file hashes** unchanged. Four runs retain failed presentation expectations; only the final ordinary-menu fix passes that UI check. [Session](evidence/2026-09-14-m05-ability/SESSION.md), [closed analysis (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-14-m05-ability/native-summary-02.json"), [inspected UI](evidence/2026-09-14-m05-ability/ui-review.md).

Retained .24 evidence supplies **3,826** matching avatar/DNA samples through **958.609 seconds**, actual menu Save denial, movement, selection/clear and scene exit. Retained .21 supplies a challenge after **580.224 seconds disconnected** and a fresh baseline without another reward. Earlier population, trace-limit and ordinary Save failures remain recorded with their fixes; today's failed load-UI boundaries are preserved in the same way. No unchanged native soak was repeated for this closeout. [Earlier presentation session](evidence/2026-09-13-m05-presentation/SESSION.md), [reward/disconnect session](evidence/2026-09-13-m05-completion/SESSION.md).

Actual WGC footage shows original camera changes, ground movement, jumps/landings and selection UI. A separate .23 Off run has zero replica events and shows native starvation, death and normal nest respawn. Descriptive 60-second D3D9 samples have median/p99 **16.30250/17.53424 ms Off** and **16.26585/17.44923 ms Replica**, with no interval above 33.333 ms. Different actions/cameras and uncontrolled NPC timing prevent a general overhead claim. These timing samples predate .24 selection. Target-process audio capture is intact; listening feedback is pending.

**M05 is VERIFIED for this fixture. M06–M08 completion is recorded above; the following limits describe this historical M05 boundary.** This qualification does not admit charge/spit/projectile effects, full inventories/consumption, death/respawn snapshots or the complete campaign. The charge-impact cut is implemented but has no actual charge callback in the current fixture. Charm/no-attack timer probes are explicitly temporary field fixtures. Perceptual listening remains NOT RUN. Those capabilities require native evidence before their corresponding M07/M11–M15 feature acceptance; the full original plan is unchanged. The earlier working protocol's broader expansion checks are retained explicitly in the acceptance decision, not marked passed.

The .29 Release build passes with zero warnings, followed by the affected replica HOST target. The full .28 CTest suite passes **8/8** in **6.05 seconds**. Replica fixtures contain **313 policy, 25 Win32/Detours ABI and 9 interior-branch checks**; retained actor/award/persistence ABI coverage is **34** checks. Five focused Python tests also pass. No HOST fixture executes SPORE. [Binding audit](docs/m05-mutation-audit.md), [design/coverage](docs/m05-replicas.md), [acceptance protocol](tests/engine/M05.md).

## Current player flow

Launcher **0.1.10**, with bridge/NativeHost **0.0.50**, checks the canonical saved world and its companion files before Join and names a mismatch. Private invitations, authenticated Join/Rejoin and connection status remain available. Normal Play uses the existing account/saves and clears experimental networking/replica roles. Experimental Join requires the matching prepared Creature fixture. M08 creation sharing currently uses developer controls and locally approved native creations; it is not a normal-player campaign/editor flow. Disposable accounts, staging and ACL helpers remain development acceptance infrastructure. The following M04 launcher evidence is historical and keeps its original payload qualification.

Launcher **0.1.8** is built at `build/launcher/Release/SporeMP.exe`, with matching release notes/CHANGELOG and zero build warnings/errors. Worker Start returns while the detached authority remains alive; worker selection survives status polling, and Stop stays attached to the selected generation. Submitted Start/Stop operations survive frontend closure under a bounded deadline. Real 0.1.7 Start, idle-launcher closure and normal Play were exercised; final 0.1.8 selection and both original-worker Stops were exercised. Closing during an active Start is separately HOST-tested. Prepared workers currently require an elevated developer operator, distinct OS profiles and unminimized windows on the signed-in rendered desktop. Normal Play uses the existing account/saves. Bridge **0.0.14** original load/save and checkpoint-bound A/B adoption now have native acceptance. All test games, supervisors, launchers and trace recorders are stopped. Details: [M04 acceptance session](evidence/2026-09-13-m04-acceptance/SESSION.md).

Retained 0.1.5 player-flow evidence:

Launcher **0.1.5** retains prepared worker selection, Start/Stop and live status in Settings and now explains that minimizing a worker pauses its simulation and can cause a timeout. All developer game processes have exited; the deliberate crash remains recorded separately from clean stops. Normal Play remains the existing account/save path. The Release executable is `build/launcher/Release/SporeMP.exe`; build has zero warnings/errors, 30 launcher assertions and nine backend tests pass. Actual WPF controls were exercised with a fixture service and the updated guidance was inspected at the minimum size. That historical 0.1.5 increment did not qualify the full launcher-to-original-worker flow; the current acceptance above supersedes that gap. Details: [native M04 session](evidence/2026-09-13-m04-native/SESSION.md).

Retained display increment:

Launcher 0.1.3 adds Settings > Display with Fullscreen/Windowed, remembered resolutions, Desktop resolution resolved at each launch, and a Home shortcut. The new Release build is available at build/launcher/Release/SporeMP.exe and build/launcher/0.1.3/SporeMP.exe. It uses original documented game flags through the guarded host and does not edit game preferences or saves. Release builds, 60 Python tests, 22 C++ host assertions and 22 launcher host assertions pass. Actual WPF selection/save bindings were exercised with a fixture service, and offscreen Home/Settings renders were inspected at normal/minimum sizes. Both developer runs launched the original game with `-f -r:2560x1440`, and the reviewed reference game-window video is 2560x1440. Independent presentation-mode measurement, windowed behavior and the full launcher-to-game flow remain unverified. Evidence: evidence/2026-09-08-launcher-display/SESSION.md and evidence/2026-09-08-m02-pair-01/SESSION.md. The 0.1.2 redesign history remains in evidence/2026-09-08-launcher-redesign/SESSION.md.

Open build/launcher/Release/SporeMP.exe and select **Play SPORE**. The launcher detects the installed game, checks compatibility automatically and uses the player's normal Windows account and existing saves. Separate accounts, profile setup and backup checks are not player prerequisites. Details stay in Settings. Optional historical isolation/backup tools remain developer-only.

## Completed evidence

- Three native cycles with the current Release bridge: lifecycle-01 (PID 34720), lifecycle-02 (33932) and gui-aa6e8891815042a6 (35772). Each recorded exactly one initialization and disposal callback on the same engine thread, matching fingerprints and normal game exit 0. These runs used the earlier isolated launch mode.
- Six actual native-host rejection fixtures exited 20 before creating a game: changed executable, incompatible content set, changed injector, changed SDK core, changed bridge, and wrong account for the isolated-only mode. The account restriction does not apply to normal Play.
- Native Resource paths and ETW accesses were captured. All 29 personal save/creation files matched their backups after the earlier isolated probes. Some ETW correlations were unresolved, and shared NVIDIA driver writes occurred. Normal Play uses and updates normal game saves.
- Earlier Release/Debug suites and a clean source rebuild passed. After the player-flow correction, Release and Debug builds succeeded, 38 Python tests passed, and Debug CTest passed 3/3 targets with 11 C++ and 14 launcher assertions in addition to the Python suite.
- The final normal-account launcher was confirmed working by the user. The assistant's attempted UI check was stopped with Escape. User acceptance completes this flow; it is not represented as another assistant-captured trace. The earlier clean export predates the final player-flow change.

Evidence: [session](evidence/2026-09-08-m01-native/SESSION.md), [native lifecycles (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-08-m01-native/native-lifecycles.json"), [guard results (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-08-m01-native/guard-results.json"), [completion (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-08-m01-native/completion.json").

## Scope and next milestone

M08 is complete within the pinned Creature content/editor foundation. **M09 is next:** persist native checkpoints and immutable content versions, then verify restart, reconnect and interrupted-transaction recovery. The following M02–M04 details retain their original qualification boundaries.

Qualification applies to the recorded local GOG GA 3.1.0.29 Win32 executable/content configuration. Executable SHA-256: dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37. SDK: cbf9206b9a823f0911cd9be0217104a49d72380b. Injector source: 26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9. Other builds and official clean-content equivalence remain unqualified.

**M02 completed — native-behavior baseline and engine observability.** Bridge 0.0.2 supplies seven opt-in original-function hooks, scene messages, bounded JSONL diagnostics, local entity IDs and the read-only `sporemp_trace` command. Normal Play clears the experimental switch. No gameplay mutation or networking was added.

The 2026-09-09 native pair used the same hash-verified, closed 27-file Creature fixture. Reference PID 29324 had observation disabled; observed PID 32344 enabled it. Both ran the original campaign at 2560×1440 through the guarded disposable developer account. The assistant operated the game under the user's explicit authorization. Each run performed two normal jumps, short native movement, exit without saving, reload, and a third jump. Actual WGC recordings and all six action contact sheets were inspected: one jump/landing per input, normal movement/NPC behavior, no duplicated visible effects, and the same starting saved scene after reload. The two observed console outputs were verified onscreen.

All 21 provenance/correlation checks pass. The observed trace contains 28,614 records, three accepted original avatar calls and one subsequent same-avatar landing each in 0.9910–0.9948 seconds. It records 8,266 entity invalidations, 499,571 NPC AI entries and 17,485 avatar AI entries, with zero lost records or foreign callbacks. Reload changes the sampled avatar from diagnostic ID 1104 / epoch 3 to 10467 / epoch 6, even though native ID 232 is reused. These are local diagnostic identities, not multiplayer ownership. Each run has one same-thread bridge initialization/disposal, game/wrapper exit 0, matching loaded payload hashes and 29 unchanged personal files. Both developer save trees under Games and all creation files also retain their starting hashes; four cache/event files changed normally.

The native presentation sample covers 60 seconds per condition, using pinned official PresentMon 2.5.1 and actual D3D9 game records. Reference/observed median frame intervals: **16.55235 / 16.54640 ms**; p99 **17.9772 / 18.0072 ms**; no interval exceeded 33.333 ms. Whole-process CPU use was 18.578 / 19.297 CPU-seconds across approximately 60.06 seconds, a measured difference of 0.01196 effective CPU cores (+3.87% relative to the reference game process). This is one capped, stationary pair with uncontrolled NPC/background timing, not a universal overhead limit or isolated hook-cost measurement. The recorded behavior shows no regression in this fixture.

Completion: [session and exact commands](evidence/2026-09-09-m02-completion/SESSION.md), [native pair analysis (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-09-m02-completion/paired-native-analysis.json"), [visual review (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-09-m02-completion/visual-review.json"), [acceptance protocol](tests/engine/M02.md). Release CTest passes 4/4 HOST/FIXTURE targets. The original M00–M20 acceptance clauses are unchanged.

**M03 completed — multiple actors using original gameplay functions.** Bridge **0.0.12** now gives B a native reward context and originally constructed deferred award queue. In fresh `award-01`, B's six native hits kill its NPC, which hits B seven times. B receives **8.75 DNA, goal progress and combat progress while A stays at zero**. A then receives **8.75** from an encounter completed with native allies; B's balances remain unchanged. A's unlocked items increase from 53 to 54; B remains zero. This closes the missing reward gate alongside the retained movement, delayed jump, ownership rejection and reload evidence.

The actual original Creature run has **18,733 contiguous records**, zero foreign callbacks, clean native counter/player retirement, hook detach 0 and game/wrapper exit 0. Release build succeeds; the two affected HOST targets pass in **0.18s** (26 ABI and 22 observation assertions). All 40 source/artifact identities match the run. All 29 personal save/creation hashes are unchanged. The disposable profile's `Games/Game0/GGEUserData.dat` changes despite native Don't Save; before/after snapshots are preserved, and this is not reported as an unchanged fixture.

Qualification is the recorded same-species, brain-level-zero Creature actor fixture. Shared tutorials/quest state, independent species and full progression remain unfinished. B's native display queue completes; pending herd evolution and brain upgrades are not qualified. Nonzero costs, active Stop/scene cancellation, B part unlocks, server-only-avatar/area activation and B persistence remain explicit limits. M03's original acceptance text is unchanged; the full campaign requirements remain in M11–M17. The 300-second video was inspected but ends before A's reward/exit; those have complete trace and live observation evidence.

Completion: [session](evidence/2026-09-12-m03-awards/SESSION.md), [native reward analysis (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-12-m03-awards/native-summary.json"), [M03 acceptance](evidence/2026-09-12-m03-awards/acceptance.json), [context audit](docs/m03-native-context-audit.md). **M04 is completed below; current M05 progress is recorded above.**

**Previous player-lifetime increment, bridge 0.0.11 (its reward blocker is resolved by 0.0.12 above):** Bridge **0.0.11** creates B's native cPlayer through the original noun factory and owner initialization, while A's manager player/avatar pointers remain unchanged. Fresh `player-02` creates player 7426, retires it, creates 7427, and cleans up at bridge disposal. Both have distinct native game IDs, independent item storage and ten selection groups. Native RemoveOwner runs once each, clears items/groups and leaves zero listeners requiring extra cleanup. The 2,141-record trace is contiguous with zero foreign callbacks, clean detach and game/wrapper exit 0. All 29 personal hashes and the disposable Games/creation contents remain unchanged.

A's goal total stays 1,000 with 53 unlocked items; both new B players have the native constructor defaults of goal total 1 and zero unlocked items. Player lifetime is verified; complete Creature stage initialization and reward routing remain unimplemented. Three configuration notifications execute B's original callback; two more are rejected during retirement. Campaign-global cheat/UI/Space callbacks remain assigned to A. Scene-exit retirement and multi-stage callback ownership are NOT RUN. The 60-second capture covers setup and first creation; later retirement/recreation/exit are established by the complete trace and live observations.

Release build passes, and the changed ABI signatures passed 18 assertions across ten signatures (1/1 focused CTest, 0.07s). No full suite or combat replay was needed. See [player session](evidence/2026-09-12-m03-rewards/SESSION.md), [native lifecycle analysis (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-12-m03-rewards/native-player-summary.json"), and [context audit](docs/m03-native-context-audit.md).

**Retained combat result, bridge 0.0.9:** Bridge **0.0.9** extends the original nest-avoidance rule's player-target exemption to live actors with explicit bridge ownership. Reverse engineering and two bounded diagnostic runs identified decider `0x04A1A0F9`, which repeatedly displaced native attack processing against B near the avatar's home nest. The adapter preserves the original decision and memory lookup once and changes only this classification; native AI, attack timing, hit checks and damage still execute.

The fresh `ownership-01` original Creature run verifies **six B→NPC native hits and six NPC→B native hits**: B ends at health 4, the NPC dies at 0, and A stays at 10. One initial B attack was queued; subsequent attacks came from original native AI, with no held `engage` command. Its 3,134 contiguous records have paired effect/Animal/base-damage calls, zero foreign callbacks, clean detach and game/wrapper exit 0. All 29 personal hashes and both disposable Games/creation trees are unchanged. The recording's nine inspected samples cover load/setup and the start of the encounter; it ends before the complete fight. The complete exchange is established by the native trace, not continuous video coverage.

The Release build passes, and the affected Win32/Detours ABI target passes **15 assertions across eight signatures** (1/1 focused CTest target, 0.07s). No full-suite rerun was needed for the nest fix. Native rewards remain unresolved: B's kill emits no DNA award. Static research now locates campaign brain-level input to the award amount, native player creation/destruction and ten message listeners, five action handlers, goal/combat-trait updates and queued display/progression work. The SDK cPlayer cast ID differs from this pinned executable and must not be used blindly. The later 0.0.11 increment qualifies native player lifetime; independent stage/reward redirection remains unimplemented. Active Stop/scene cancellation, nonzero costs and any server-only-avatar activation remain unqualified. See [current session](evidence/2026-09-12-m03-decisions/SESSION.md), [native analysis (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-12-m03-decisions/native-summary.json"), [acceptance (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-12-m03-decisions/acceptance.json"), and [context audit](docs/m03-native-context-audit.md). Earlier 0.0.4–0.0.6 evidence remains preserved in its [sealed session](evidence/2026-09-12-m03-context/SESSION.md).

**Earlier M03 evidence (2026-09-09):** Bridge 0.0.3 adds the guarded developer-only `sporemp_actors` console, bounded value-only command queue, distinct owner/actor IDs, native creation/movement/Jump/Bite, held attack intentions, native damage/resource/target tracing and destruction/scene fencing. Normal Play clears the harness switch. Eight native attempts used the measured disposable account. Two factory-context crashes and a trace-cap failure are preserved and corrected; probes07/08 exit normally with healthy traces and unchanged personal hashes.

Healthy probe 04 demonstrates distinct six-unit native movement goals (A within 0.4892 units, B within 0.9579 units, both within five seconds), three accepted owner-matched delayed jumps, A/NPC reciprocal damage, a native assisted campaign reward, wrong-owner rejection, and B destruction/recreation with a new ID. Healthy probe 05 demonstrates two B→NPC hits (health 6→4), a single native approach request, same-process exit/reload and stale-ID rejection; native IDs are reused but actor IDs change from A1/B2 in epoch 3 to A4/B5 in epoch 6. A's post-reload jump completes correctly. **B's post-reload request returns false because it is already airborne; its visible native animation is not counted as that command's success.**

Healthy probe08 now completes **two B kills, each through six native Bite hits (NPC health 6→0)**. The held input adapter uses native ability/footprint values to choose approach versus attack and preserves native cooldown, hit and damage processing. Intentions cancel after target death. The same-process reload rejects old B2, assigns fresh A5/B6 in epoch6 despite native-ID reuse, and completes B6's accepted jump after1.0027685s and70 native AI ticks. The earlier probe05 rejected jump remains rejected. Expiration is exercised in probe07; active Stop and active-intention scene cancellation remain NOT RUN.

**Issue recorded on September 9:** NPC3 attempts six native attacks targeting B without a healthy-run hit; the September 12 bridge 0.0.9 increment above resolves that bounded combat case. Neither September 9 B kill emits DNA; the game still has one native campaign avatar and no independent B progression account. Tested cooldowns are per acting creature, but all observed energy calls are zero, so nonzero costs remain unqualified. Server-only avatar/area activation is NOT RUN. This is not playable co-op.

Release build09 and ctest04 pass (5/5 HOST/FIXTURE targets, including15 actor-authority assertions). Probe08 has40,075 contiguous native records, zero foreign callbacks, paired recorded ability/damage/target calls, clean hook detach and game/wrapper exit 0. NPC target observations are explicitly sampled; the sampler preserves original calls. All eight personal postchecks retain29 hashes, and disposable Games/creation contents retain their starting hashes. Native video contact sheets were inspected; the reload video ended before the accepted B jump, which is covered by native events and live screenshots. See [session and commands](evidence/2026-09-09-m03-actors/SESSION.md), [native analysis (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-09-m03-actors/native-analysis-02.json"), [acceptance (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-09-m03-actors/acceptance.json"), [context audit](docs/m03-native-context-audit.md), and [M03 protocol](tests/engine/M03.md). The September 12 increments supersede those earlier next steps.

**Historical M04 increment, bridge 0.0.13 (2026-09-13; completion follows below):** bridge 0.0.13 now has original-game evidence for private IPC dispatch, concurrent rendered workers, native progress after operator-pipe closure, deliberate crash containment and clean engine-loop shutdown. Four native processes ran under SporeMP-M04-01/02; three exited normally and one was deliberately terminated. Eight original A/B jump returns match later landings, including actions in an unfocused worker and in the surviving worker after its peer crashed. No native M03 combat replay was needed. Earlier HOST evidence remains 173 worker assertions in 0.69s; the updated launcher 0.1.5 passes 30 assertions and nine backend tests.

The earlier private-desktop attempt remains a renderer [1001] failure. The newly tested current signed-in desktop works with unminimized 1280x720 worker windows. During a 15.27-second minimization sample, worker 02's app/AI counters stopped while worker 01 advanced 858 updates and 26,598 original AI entries. Restoring worker 02 resumed both counters. Its original Options/Save dialog also paused simulation while app heartbeats continued. Lock/disconnect/service/RDP/VM behavior is NOT RUN; no headless support or unlimited capacity is claimed.

The concurrent ETW trace has zero reported lost events and no resolved personal/peer-profile opens or mutations. Both games used their own native save/creation paths. Unresolved file/registry correlations and shared NVIDIA driver writes are retained as limits. The worker-02 original Save flow produced its success dialog and checkpoint writes; after clean shutdown, a fresh process manually reloaded that preserved checkpoint. Native scene/AI returned, but bridge actor IDs were both zero: ownership restoration is unimplemented. Old-generation, stale-epoch and unbound-actor requests were refused without creating replacement actors. All 29 personal files and all creation files remain unchanged; worker save/cache changes are preserved in closed snapshots.

**M04 completed (2026-09-13):** bridge 0.0.14 executed four clean original worker lifecycles and **12 native jump/landing pairs**, including actions after closing the actual launcher and normal-account player. The original fixed-filename save was sealed after clean exit, then loaded in two fresh generations. A/B ownership binds only the exact existing saved native nouns, with `created_nouns: 0`; new valid actions work, while wrong-owner/stale-epoch commands are rejected. Full player, reward and campaign persistence remains later work.

The final two workers saved concurrently in their own profiles. Instrumented original-save spans overlap by at least **414.7126 ms**; this includes logging/scheduling boundaries and is not a claim of simultaneous CPU execution. Both saves subsequently have clean exits and independent sealed backups. Full-lifetime ETW records their own Games writes, zero lost events, and no resolved personal/peer-profile opens or mutations. Unresolved file writes, unknown event schemas and shared driver output remain in the reports. The retained 0.0.13 deliberate-crash test closes crash containment without another destructive replay.

Normal-account Play changed ten personal files, with successful player mutations observed for every changed path. Those files were preserved and backed up; the final concurrent worker phase leaves all 29 personal baseline files unchanged. This is not an unchanged-personal-tree claim across normal Play. Worker 02 later paused native AI for approximately 47 seconds while app updates continued; the launcher displayed `simulation_stalled`, and Stop still exited cleanly. Its cause is unproven. The qualified arrangement requires the signed-in rendered desktop and unminimized worker windows; service/headless/locked/disconnected operation and indefinite autonomous progression remain unqualified.

Release integration checks pass: 184 worker, 34 native ABI and 15 actor assertions; final affected launcher/tooling checks pass 39 launcher assertions and 98 Python tests. Actual recordings and launcher/game screenshots were inspected separately. Completion: [session](evidence/2026-09-13-m04-acceptance/SESSION.md), [acceptance matrix (archived)](docs/public-evidence.md#historical-artifacts "Original path: evidence/2026-09-13-m04-acceptance/acceptance.json"), [worker instructions](docs/m04-workers.md), [native protocol](tests/engine/M04.md). M05 now has the bounded vitals projection recorded above; full native authority/presentation separation, networking, global universe behavior and the mandatory campaign remain unfinished.

The [binding registry](docs/m02-binding-registry.md) and [five-stage inventory](docs/m02-stage-capabilities.md) retain the conditional M02 tracing backlog: complete damage/death and inventory/progression coverage, save/load interception, object-lifetime paths and non-Creature native fixtures remain unqualified. Those capabilities must be verified before the corresponding actor/stage acceptance; no mandatory mechanic or campaign stage has been removed. Native Debug execution, broader worker isolation, networking, full transactional recovery and release packaging retain their original gates.

Earlier evidence remains preserved: [initial observation implementation](evidence/2026-09-08-m02-observation/SESSION.md), [first native probe](evidence/2026-09-08-m02-user-probe-01/SESSION.md), and [pair 01 with rejected observed GDI video](evidence/2026-09-08-m02-pair-01/SESSION.md). The 2026-09-09 WGC pair resolves the missing visual/timing evidence; it does not retroactively validate the rejected recording or the failed older PresentMon invocation.
