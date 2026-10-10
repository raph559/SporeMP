# M10 multi-location authority — current increment

The latest [native recovery report](../evidence/2026-09-26-m10-native-state/NATIVE-RECOVERY-086.md)
supersedes the historical open-test notes below for the cases it names. It
records .86 global admission, closed native worker-fault results, paired-Save
recovery, all applicable interruption boundaries and final .86 player reconnect. The complete campaign
and unsupported stages are not qualified by the pinned Creature fixture.

Status: **VERIFIED for the recorded original Creature location foundation**. One coordinator routes independently fenced TLS
sessions by location, persists avatar interest and global-owner policy, and
shares one durable content registry. The first original-game session used an
M09 two-avatar checkpoint and failed location occupancy: both authority
processes published both owners. A subsequent 30-file, two-save original
Creature head passed guarded one-primary scene capture and was selected as
an initial immutable M10 checkpoint. Satiria and Haagos simultaneously
published only their leased original primary owner through the same
coordinator. Separate original player processes joined and applied the
appropriate 32- or 18-entity baseline. A Satiria authority replacement
durably advanced its worker generation from 5 to 6 and recovered its player
on a fresh baseline while Haagos stayed live. The later 0.0.82 session completed
outward travel, a visiting player's original move/stop/jump, and return travel
with native secondary cleanup. Its return-player reconnect was not executed
before the two idle original creatures starved. The later .86 qualification completes the recorded native fault and shared-consequence
foundation; the linked report states its exact stage limits.
The selected head now uses original-game observed home `PlanetID` and active
terrain instance values for both location keys. A native engine-thread guard
admitted both authority locations and the matching Haagos player, then
rejected a deliberately wrong area value before connection. This qualifies
fixed home-scene admission, not away-area or travel mapping.
[Gate map](../tests/engine/M10.md), [0.0.70 coordinator evidence (archived)](public-evidence.md#historical-artifacts),
[bounded native joins (archived)](public-evidence.md#historical-artifacts).
[one-primary native probe (archived)](public-evidence.md#historical-artifacts),
[one-primary online session (archived)](public-evidence.md#historical-artifacts),
[bounded original outward/return (archived)](public-evidence.md#historical-artifacts).

## Implemented coordinator policy

`LocationId` consists of nonzero canonical galaxy, star, planet and area IDs.
For the two qualified home Creature scenes, the star field is the low 24 bits
of the original game's `PlanetID`, the planet field is that full `PlanetID`,
and the area field is the active terrain resource instance matching the home
planet's generated terrain. Galaxy `1` is a coordinator namespace, not a
native galaxy value. The mapping is guarded on the engine thread before the
network joins; bounded travel between these two homes has run, while away-area
mapping remains unqualified.
Each location has one worker/generation lease, a selected checkpoint revision
and a content digest. Avatar leases are per player. The economy, diplomacy,
galaxy AI and disaster systems have explicit single-owner slots; a second owner
cannot be installed through this policy. An outcome check requires the matching
player, location, avatar generation, worker and worker generation. A lost worker
deactivates its location and avatar outcomes. Global outcomes also fail while
their owner worker is inactive. Rebinding requires a higher generation. The
coordinator rejects a location whose content digest differs from its active
universe content identity.

The online coordinator restores one `Session` per active location under the
same listener and durable universe. A player's authenticated HELLO is routed
only after its canonical location agrees with the persisted avatar and interest
lease. The two location sessions keep their own worker credential, generation,
native-load slot, scene baseline and connection epoch. Content registry state
is shared in memory and persisted once for the universe; the location assigned
the single economy lease supplies the one content-attestation authority. Content
events sent to another location are stamped with the recipient's session epoch.
The HOST TLS test covers that cross-location event path. This does not make a
content edit native in another worker; the native fan-out and applied return
remain unqualified.

The transfer record advances through `prepared`, `source_fenced`,
`destination_restored` and `committed`, then cleanup. Prepare leaves the source
active. Source fence removes avatar authority from both locations. Destination
restore requires the current destination worker generation. Commit places the
avatar at the destination with a newer avatar generation. A precommit abort
after fencing requires an explicitly newer source recovery generation; a
committed transfer cannot be silently rolled back. A prepared transfer is
discarded when its crashed source is replaced from a checkpoint. A crashed
destination must be restored and recorded again before commit. The record and
all lease changes are one atomic coordinator-store revision per step.

The coordinator reads existing schema-2 through schema-9 state and writes
schema 9 on the next publication. Schema 3 added a bounded, validated location ledger to the
same checksummed record as credentials, content and the original checkpoint.
Schema 4 also retains the complete last worker-replacement request at its
publication revision, so exact retry checks include both the old and new
worker generations after recovery. A later publication retires that receipt.
Schema 5 added complete live preparation identities. Schema 6 added bounded
native command/receipt records, canonical original-player and home identities,
maintenance admission, and immutable player DB/M10F byte pairs. Schema 7 adds
durably pinned worker process identities, observed process-death records and
the source rollback-commit marker needed for interrupted recovery. Existing
schema-6 receipts migrate with their process identities; a pre-export record
without a durable process identity cannot invent that association on recovery.
Schema 8 separately retains completed source-cleanup evidence and the completion
marker so later destination recovery cannot lose or repeat historical cleanup.
It permits at most four native records: two active operations and two prior
player artifact heads. A prior head is retired only when its player's next
validated immutable DB/M10F pair has been durably bound. Schema 9 adds the
closed native-owner checkpoint approval/proof and an explicit checkpoint
supersession marker for retained player-transfer history. It permits a
terminal transfer to refer to its historical selected checkpoint; an active
transfer must still refer to the current head. A saved player's previous
artifact bytes are cleared only when the validated new whole-owner Save
proves their inclusion. Source retirement and cleanup evidence remain intact.
Unknown schema or invalid ledger fails closed. Every location and replacement
worker must reference the selected immutable checkpoint revision; offline
publication and recovery reject an older or divergent location revision. The
generic checkpoint commit remains refused with location leases. The narrow
closed-owner publication below advances all location references together after
validating the complete original owner world and unchanged nonowner save.
An authenticated location session is bound to its durable worker, avatar
generations, interest and global content-owner role. A changed binding fences
the session, clears connections and cached/staged scene state, and stops its
transport before further packet polling. This cannot recall already sent
packets or undo applied native actions; native quiescence remains required.
`--location-bootstrap` requires
an exclusive offline writer, a selected M09 checkpoint and the expected current
revision. It cannot publish a location referencing a future checkpoint. The
bootstrap plan is a local ASCII file with schema 1 and exactly these fields:

```text
schema=1
location_count=2
location.1=galaxy,star,planet,area,worker,generation,native_slot
location.2=galaxy,star,planet,area,worker,generation,native_slot
player.1=1
player.2=2
global.economy=1
global.diplomacy=1
global.galaxy_ai=1
global.disasters=1
```

Location and global-owner row references are one-based indexes. Native load
slots are the two fixed reviewed entries 0 and 1, not arbitrary paths. The
command writes a HOST receipt with `native_validation: NOT_RUN`. Online
transfer-phase publication now requires authenticated, request-correlated native
receipts as described below. The generic ledger mutation API remains offline.
The generic checkpoint command cannot rotate a head with location leases;
the separate closed-owner publication has additional native and process gates.
The guarded transfer
control surface is a developer interface; full native M10 acceptance requires
the separate original-game transfer, meeting, return and recovery runs.

## Closed original-owner checkpoint publication

The 0.0.83 coordinator adds a qualified paired Haagos Save publication path.
It accepts one complete closed original owner snapshot. The retained
`Satiria.spo` must remain byte-identical to the selected checkpoint and every
previous member must still exist. There is no resource-record merge. A strict
native validator proves the successful original Save, saved paired player
graph, transfer/import/activation lineage and selected native spice outcome;
its fixed 464-byte `M10G` approval binds those inputs and the exact proof bytes.
The coordinator independently checks the current selected head, all four
global-owner leases, exact latest player artifact heads and actual retained
local process handles. Every active authority must have exited; the owner
must have exited with code 0. No ordinary transport or active transfer may
remain. Remote worker death proof is not qualified by this local interface.

One durable publication selects the candidate, advances every location's
checkpoint reference, increments active worker/avatar generations and updates
global-owner generations. Inactive empty homes retain the generation needed
to verify their historical retirement. Superseded player DB/M10F bytes are
cleared while retirement/cleanup history remains available for return-home
maintenance. The publication event ID is its resulting durable revision
(`expected_revision + 1`); the native Save request is a separate field because
request numbers may repeat in a fresh original worker.

The private administrator writes bounded regular files
`global-checkpoint.smp`, `global-checkpoint.expected` and
`global-checkpoint-proof.json`, followed atomically by:

```text
schema=1
expected_revision=N
artifact_sha256=<complete candidate SHA-256>
approval_sha256=<M10G SHA-256>
proof_sha256=<exact proof JSON SHA-256>
```

The request filename is `global-checkpoint.request`. The coordinator claims it
and writes `global-checkpoint-receipt.json`, plus current
`global-authority-gG-sS-pP-aA.conf` and `global-player-N.conf` files. An exact
retry uses the original expected revision and identical three inputs; it does
not advance ownership or the selected head again. After coordinator reopen,
authority credentials/epochs are fresh, so the exact retry regenerates the
current configurations. A fresh original reader must stage the complete
selected checkpoint before admission. The receipt reports
`native_reader_qualified:false`; durable publication alone does not establish
successful native reload or full L07 producer exclusivity.

## Native ownership and evidence boundary

| Domain | Required native owner | Current evidence |
|---|---|---|
| Avatar and local NPC/resource consequences | One original worker for one active location | The corrected selected two-save world publishes separate owner scenes. In 0.0.82 the visiting replica's move, stop and jump reached the imported original slot; outward and return cleanup completed. Native fault recovery and old-generation outcome injection remain unqualified. |
| Canonical planet terrain/records | One native planet-resource owner with versioned projection | Two fixed original Creature saves have distinct home planet and active terrain candidates; developer-selected native load and simultaneous original workers passed. Versioned artifact publication, projection and shared consequences are NOT RUN. |
| Galaxy AI, economy, disasters and diplomacy | Exactly one qualified native producer per global resource | Authenticated grants precede original startup producers and remain separate from ordinary scene publication. Native evidence covers owner spice callbacks, denial before nonowner mutation, initialized original-reader no-ops and owner-generation replacement. Other native stage services remain unqualified. |
| Reconnect and interest | Current location's scene, fresh baseline and ownership epoch | NATIVE: separate owner-specific player joins and a durable Satiria generation-6 authority replacement with player baseline 2 while Haagos remained live. Reconnect after a committed move and generalized interest changes are NOT RUN. |

The earlier two-avatar 2026-09-24 original-game run advanced the local scene and interest
row: two live original authority workers restored checkpoint revision 2 under
one coordinator, and a third original process applied the Haagos player-2
baseline followed by a fresh Satiria player-1 baseline. It does not qualify
separated native occupancy: both authorities published owner 1 and owner 2.
The post-run coordinator owner-mask check rejects an authority baseline or
live spawn containing an owner leased to another location, and its HOST test
passes. The subsequent one-primary native run did exercise this guard with
only the leased owner in each separately published scene. Reconnect after
transfer, generalized interest activation, and a post-Save shared world
revision remain open.
The location IDs in that earlier run were placeholder coordinator
keys; the later home-scene admission trial uses observed native values. An earlier Haagos player baseline
failed closed because the authority NPC herd ID had no matching local template;
the successful joins do not remove this robustness gap.

The earlier M09 checkpoint contains one complete `Game0` native world and one
original selected native load path, now exercised for both fixed Satiria and
Haagos saves in a developer-only worker command. The earlier M06/M09 network
`Session` admitted one authority and published one scene; 0.0.70 added HOST
location routes and 0.0.71 connected two original workers to them in the
bounded native run. The new Haagos save was created in a copy
of that same original `Game0`: both `.spo` files coexist, while the original
`stars.db`, planet records/scripts and `Planets.package` changed. A closed
manifest of all 30 files is retained privately. The original six-file M08
admission tuple differs in four entries before versus after creating Haagos,
despite the unchanged `Satiria.spo` hash. Thus the old exact-bundle handshake
correctly refuses mixing these revisions; it cannot yet identify independent
location files versus shared galaxy files. Starting copies of either revision
would risk double-owning native global state. No live save trees are combined,
and no guessed native travel or serialization call is used.
Offline DBPF comparison narrows the difference to one changed Haagos terrain
script record, one new Haagos terrain package record, 36 changed planet-record
entries and seven changed star-database entries. Both observed planet IDs occur
among the changed planet records, so byte changes cannot be assigned to Haagos
alone from the filenames. These HOST results do not qualify a native merge.
In a controlled repeat, the original game loaded and played Haagos, then exited
without an explicit Save. Eight closed profile files changed; decoded records
were added to planet and terrain archives and six star-database keys changed.
The `.spo` saves did not change. Native load/play/quit therefore also writes
shared candidates. The specific writer and safe quiesce point remain unverified.
The exact six-file tuple remains a valid **admission identity for one sealed
checkpoint**. It cannot be repurposed as an equality test between two workers'
live mutable directories or as a per-location version: native load itself can
change those bytes. M10 requires a separately selected immutable source
revision, explicit local artifact ownership and a single versioned global head
before either worker can publish a new shared revision.
The 2026-09-24 original UI Save controls start each location from the same
sealed 30-file input. Without an explicit Save, original load/AI/clean shutdown
leaves `Game0`'s `.spo`, planet records, scripts and stars byte-identical in
three trials; runtime caches change. With an original Options → Save action,
each location changes its own `.spo` and the shared world archives. Decoding
the closed archives finds 41 planet-record keys and six star-database keys
changed in both Save branches, with **different values in all 47 shared keys**.
This is a measured native artifact collision. It is not safe to choose one
worker's whole tree or apply a last-writer key merge. The original Save also
creates a `Game0.old` copy of the pre-Save world archives. [Reviewed probe (archived)](public-evidence.md#historical-artifacts).
The subsequent opt-in writer observation ran in two original-game processes
after their selected Creature loads. It captured all 41 conflicting planet keys
through a pinned `cStarManager` bulk copy from the temporary planet database;
the Satiria run captured all six conflicting star keys through the generic
serializer writing `stars.db.tmp`. A further post-load probe traced all six
star keys to original `COMSerializer::SaveClassObjects`. This identifies the
native class-object serialization path, not the native mutations that produced
the divergent objects or the owner/meaning of their fields. A pre-load hook
attempt crashed during Load and is retained as a failed boundary; the
subsequent post-load runs exited cleanly and restored their disposable profiles.
An additional original Save traced all 41 conflicting planet records into
`planetRecords.pkt`, then the same 59 temporary keys into `planetRecords.pkp`.
The pinned SDK and executable identify the temporary writes as
`cStarManager` calls to `cResourceManager::WriteResource` using the temporary
database at offset `0x224`. This locates the native Save writer but does not
identify which gameplay producer last changed each live record. No merged
native artifact has been qualified. Two separate Satiria Saves from the same
sealed input changed the same 41 planet and six star keys to different
decoded bytes as well. This same-location control shows that branch byte
differences alone do not identify location-specific state; a single selected
head needs original-game adoption/reload and explicit native writers.
[Reviewed writer observation (archived)](public-evidence.md#historical-artifacts).
A subsequent serial original-game control loaded Haagos from Satiria's
complete Save output, saved Haagos, and then loaded Satiria in a fresh process
from that new complete world. Both native scenes and the two UI observations
passed. Haagos's Save preserved `Satiria.spo` byte-for-byte; the later Satiria
load left `Game0`'s guarded world files unchanged without another Save. This
qualifies only a candidate complete head under exclusive sequential writers,
not concurrent branch publication or a live transfer.
[Reviewed sequential run (archived)](public-evidence.md#historical-artifacts).
The earlier original UI load/play/quit run followed different preparation and
did change shared archives without an explicit Save. The controlled no-Save
result applies only to the exact 30-file input and direct native load path in
the new report; it does not establish a general no-write guarantee.
The simultaneous Satiria/Haagos run began from one identical 30-file input
and produced disjoint newly indexed planet/terrain records, yet both workers
changed the same six top-level `stars.db` records. Those outputs remain
immutable private candidates. A union of their DBPF entries would still have
conflicting shared star records and has no native validation.

The pinned SDK's `cPlanetRecord::mKey` and `PlanetID`/`StarID` declarations
provide candidate native planet/star identity. Bridge 0.0.65 first observed
Satiria's home planet candidate `9519123` (`0x914013`) and generated terrain
instance `684339943` (`0x28ca32e7`). Bridge 0.0.67 read the active
`cPlanetModel::mPlanetKey` at the verified original-load boundary; its complete
resource key matched Satiria's generated terrain key. A separate original UI
save/reload of Haagos on bridge 0.0.68 observed home planet candidate
`3788854` (`0x39d036`) and generated terrain instance `684339955`
(`0x28ca32f3`); its active model key matched that terrain, and the UI entry
event identified Haagos by its original home name. Both terrain keys share type
`18450871` and group `1082433792`. These are two distinct native planet
candidates in separately observed Creature scenes, not a live transfer or proof
of an area partition. All events retain `canonical_location_qualified:false`.
The SDK `SpacePlayerData` active-planet pointer was unavailable in this Creature
scene and is not used for location authority. [Initial run (archived)](public-evidence.md#historical-artifacts),
[continuation (archived)](public-evidence.md#historical-artifacts).

The M09 final native matrix used frozen bridge SHA-256
`f03f9a27e3535cf75221adb71cbdb610ac39de7304eb337a8f73afa82226a6c0`.
The 0.0.65–0.0.70 development probes have different binary identities. None
inherits the frozen M09 result. Every new native transfer run must pin and
validate its own bridge/host/config/checkpoint bundle before startup. The
0.0.69 selected-load/two-worker observations remain separate from M09
transfer or durability acceptance. The 0.0.70 HOST bootstrap smoke reused a
private copy of the closed M09 state and intentionally made no native load.
[Selected-load evidence (archived)](public-evidence.md#historical-artifacts).

## Next native and implementation gates

1. Extend the qualified two-home-scene `PlanetID`/terrain mapping to observed
   departure, arrival and away-area boundaries in original SPORE. The current
   fixed Creature saves do not identify transfer serialization or travel events.
2. Establish source object/field ownership for the 41 conflicting
   planet-record and six star-database keys observed on original Save. The
   `cStarManager` temporary writer, outer planet bulk-copy and star
   class-object serializer are now identified; their mutable native producers
   remain unqualified. Then
   produce immutable per-location artifacts with one versioned global head
   for the same current `Game0` identity. Sequential whole-world adoption is
   observed, but two closed concurrent Save branches cannot be combined by
   filename or last-writer resource-key policy.
3. Qualify the implemented online orchestration against original-game
   quiesce/export/restore/activation results. The coordinator binds each phase
   to a canonical native receipt and immutable artifact; HOST tests exercise
   the full protocol and interrupted-worker recovery with synthetic receipts.
   Connected worker/player Sessions are fenced
   against replaced avatar and worker generations. Already-dispatched native
   inputs require the separate original-game input-fence validation.
4. Qualify original NPC herd/template admission across divergent load timing.
   One Haagos player baseline refused a missing herd, while later joins into
   both locations passed. Then exercise native area activation, meeting,
   separation and reconnect under one coordinator. Terminate either worker
   at every transfer boundary and recover without duplication or loss.
5. Audit original native global producers and prove shared economy/diplomacy
   consequences occur once. HOST global-owner/event-ID policy does not suppress
   unqualified SPORE background calls.

The desktop was authorized for M10 checks; recording and protected-save checks
from M04–M09 still apply. Original Save ownership was probed in 2026-09-24
native runs. Bounded outward/return travel is recorded separately; native phase
faults, reconnect after return, and the complete shared-world acceptance remain
open. M10 is not VERIFIED
and M11–M19 remain blocked by their stated dependencies.

## Guarded live transfer and recovery control

The online coordinator pumps a local administrator request from its private
output directory. This is a developer control surface; it is not exposed in
the player transport or normal Play flow. `transfer.request` is atomically
claimed as `transfer.claimed`. Input is limited to 2048 bytes, must be a regular
file with one link, and cannot be a reparse point. Fields are strict, unique,
and numeric values are checked for overflow. Requests contain no file paths.

The schema-1 preparation/cancellation request and schema-2 native control
request share these exact base keys:

```text
schema, op, id, player, expected_revision, prepare_expected_revision,
checkpoint_revision, avatar_generation, source_worker,
source_worker_generation, destination_worker, destination_worker_generation,
source, destination, universe, checkpoint_sha256
```

Each line is `key=value`; each location is four comma-separated unsigned
integers. Universe and checkpoint identities are complete SHA-256 hex values.
Schema 1 accepts `prepare` or `cancel`. For preparation, `expected_revision` equals
`prepare_expected_revision` and the current durable revision. Cancellation
retains the complete original preparation identity and supplies its own current
`expected_revision`. The original revision is never rewritten for a retry.

A new preparation requires both current authority Sessions to have completed
their baselines, plus exact active source/destination worker leases, the source
avatar incarnation, universe and selected checkpoint hash/revision. A complete
baseline is only a coordinator readiness check; it is not evidence of native
quiescence or restoration. Preparation leaves ownership, interest, Sessions,
native input permissions and their baselines unchanged.

The current coordinator record retains at most two complete preparation identities.
Exact preparation retries survive unrelated durable publications and recovery
while the corresponding reservation and leases remain current. Cancellation
is permitted only before source fencing; its bounded complete receipt supports
an exact retry at the resulting revision, including after recovery. Any later
publication retires that cancellation receipt. Replacement of either involved
worker atomically cancels the affected unfenced reservation. Schema 2, 3 and 4
records remain readable; schema 4 retains its exact worker replacement receipt.

The atomic `transfer-receipt.json` reports the exact request file SHA-256,
operation, transfer/player identity, expected and resulting durable revisions,
and a fixed error identifier. `ok:true` follows successful durable publication,
a verified exact retry, or a successful read-only status query. The receipt
separately reports `ownership_committed`, numeric `native_phase` and terminal
`native_completion`; acceptance of a queued command never establishes a native
result. A receipt-write failure retains the claimed request
for retry. A stale `transfer-receipt.tmp` deliberately blocks receipt replacement
until the administrator inspects and removes that private obstruction; the
durable identity prevents a duplicate operation when retry resumes.

Schema 2 additionally accepts `prepare_maintenance`, `register_source`,
`register_destination`, `fence`, `status`, `bind`, `restore`, `discard`,
`rollback`, `activate`, `source_activate`, `source_cleanup`, `commit`, `cleanup`,
`recover_source`, `recover_destination` and `artifact`. Registration adds exact
`process_id`/`process_created` fields; fencing adds `source_slot` and
`destination_slot`; recovery adds `replacement_worker` and
`replacement_generation`. Requests contain no native paths. Artifact binding
reads only `transfer-<id>.db` and `transfer-<id>.expected` in the private
administrator directory, bounded to 1 MiB and 20,680 bytes respectively.

Normal progression is preparation, source fence/export, validated artifact
binding, provisional destination restoration, durable ownership commit,
destination activation and fresh baseline, then source cleanup. The native
source receipt must bind its process identity and at least three seconds of
advancing application callbacks with zero native simulation deltas and unchanged
AI count. The host validator supplies the separate digest-bound M10F approval.
The destination receipt proves restoration under its own held pause. Activation
requires pause release and resumed original AI. The selected primary and
secondary slots are distinct from canonical player identities. A secondary
departure requires actual native removal; an empty primary source requires
actual process death observed through a retained registered handle. A source
that owns a global producer cannot be closed through this cleanup operation.

An explicit maintenance startup admits a fresh original worker after its scene
loads, with an authenticated transfer-only channel and no ordinary baseline.
This supports a return to a retired home using a higher worker generation and
restoration of the fresh selected-secondary export over the original primary.
Ordinary publication is released only after restoration, ownership and native
activation. During maintenance on an already active Session, valid ordinary
output from its exact previous baseline is drained without cache or subscriber
updates; a future baseline is rejected. This addresses the actual first native
trial's in-flight output race.

The interrupted recovery controls require a durably pinned old process identity,
its retained handle signaled with a real exit code, the exact current lease and
a strictly higher generation. They rotate credentials and publish the new
maintenance admission atomically with the lease change. Post-export source
recovery restores the bound artifact before rollback/activation. A dead
provisional destination is restored again; a dead committed destination retains
ownership and restores under a higher generation. Death of the departing
secondary process proves its removal, but the replacement resident still needs
a fresh baseline before cleanup can finish. A pre-export source loss can return
to the selected checkpoint and records `checkpoint_recovered`, which is not a
completed transfer. After terminal completion, destination recovery restores
the retained latest artifact while preserving destination ownership and prior
source retirement. After terminal abort, source recovery restores that artifact
without republishing the rollback. Both require a higher generation, a new
authenticated native restoration/activation result and a fresh baseline before
ordinary publication. No original-game recovery pass is inferred from the HOST
tests.

Status identifies the terminal recovery head in `recovery_artifact_transfer`.
The read-only `artifact` operation exports its exact durable DB/M10F bytes to
the same fixed administrator filenames used by `bind`; existing unequal output
is refused. A new preparation or unapproved export does not evict that head.
Recovery of the old head can atomically abandon a later attempt only while the
later attempt remains unbound. A new validated artifact prevents that older
recovery. Generic worker replacement and restart admission are fenced when
they would expose an older selected-save avatar in place of retained durable
player state.

Before any native fence command exists, the supported recovery route is exact
preparation cancellation followed by ordinary higher-generation worker
replacement after the supervisor observes actual process death. Cancellation
and replacement each have their own durable exact retry receipt; interruption
between them leaves a cancelled reservation and the old worker generation.
The controller resumes at replacement using the new durable revision. If a
previous terminal artifact head exists, generic replacement is refused and
that head must be restored. Source death after a completed fence/export receipt
but before artifact binding requires validation and binding of that exact closed
export, followed by source maintenance restoration. It does not fall back to
the original checkpoint.

Private peer configuration schema 6 includes `resident_owners` (canonical owner
bits 2, 4 or 6) and `maintenance_transfer` (zero for ordinary admission). Authority
primary selection follows its native load slot; a player's `primary_owner` is
the canonical player being controlled, even when visiting another native load
slot. During maintenance, intended residents include the selected player while
its avatar remains fenced. This configuration is admission intent; actual
baseline owners remain validated against the current durable leases. Older
one-primary configurations default to that primary's single owner bit.

Read-only status exposes source and destination canonical witness objects and
their authenticated SHA-256 values, current effective worker/avatar generations,
the pending command and last receipt. The immutable preparation remains
inspectable after terminal cleanup. Requests advancing a phase retain that
original preparation identity and use the current durable revision. The generic
`commit_locations` API remains offline-only and refuses live reservations.
See [coordinator implementation and HOST evidence (archived)](public-evidence.md#historical-artifacts).
