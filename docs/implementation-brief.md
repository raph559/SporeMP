# SPORE Global Multiplayer — Codex Implementation Brief

Prepared: September 8, 2026  
Status: implementation requirements and proposed architecture; not a claim that the engine integrations below have already been demonstrated.

## 0. Your assignment

You are the lead C++ engineer implementing a complete global multiplayer mod for the original PC game SPORE. Work incrementally toward a usable release, not a standalone feasibility report, a networking mock-up, a ghost-player demo, or a replacement game.

The user wants the original SPORE experience to become multiplayer. Preserve the original engine, assets, editors, AI, combat, economy, and progression wherever those systems can be reused. Do not make a separately reimplemented gameplay server the default solution.

Read this entire brief, inspect the existing repository and available development environment, establish the project documents, and begin implementation. Do not treat the planning files as the deliverable. Do not ask the user to reconfirm decisions already specified here. Resolve ordinary implementation details with documented, reversible choices.

A blocked engine capability is not permission to invent a working API or quietly change the product. Produce a small reproducible investigation, document the exact blocker, and continue independent work that remains valid.

## 1. Product scope and non-negotiable requirements

### 1.1 What GLOBAL means

The required product is one persistent shared universe supporting all five original stages: Cell, Creature, Tribal, Civilization, and Space. Different players can occupy different locations and stages concurrently. Progress belongs to persistent player/species/faction identities, not to whichever client happens to be hosting. Players can meet, interact through supported stage mechanics, separate, reconnect, and continue their campaigns.

Global does not mean that every player must fit inside one scene, that the entire galaxy must be simulated at full spatial detail continuously, or that unlimited player capacity has already been promised. Location partitioning is allowed; unrelated private campaigns presented as a shared galaxy are not.

Target Internet play and independently hosted servers, not LAN-only play. A listen-server configuration may reuse the same architecture, but a human player's client must not be required to remain connected for a dedicated session to exist.

Use Galactic Adventures as a candidate runtime target for ModAPI integration after checking the actual installation and SDK support. Playing Galactic Adventures cooperatively is an optional extension milestone; it must not replace the five-stage campaign requirement.

### 1.2 Preserve the game, add multiplayer

Default architecture: original SPORE processes perform original gameplay simulation; our software adds transport, identities, authority routing, synchronization, supervision, and persistent-state coordination.

Do not independently rewrite native combat formulas, creature AI, pathfinding, stage economies, evolution calculations, or world generation merely because hooking them is difficult. Do not put simplified versions into the coordinator and present them as native behavior.

New rules are permitted where multiplayer creates a genuinely new question: player ownership, simultaneous actions, editor participation, transition consent within a shared faction, disconnected-player behavior, and administrative permissions. Label these as multiplayer policy, separate from preserved native mechanics.

No guarantee of headless operation is assumed. An unattended original-game process with rendering still enabled is an acceptable initial server worker. Rendering suppression is a later optimization whose correctness must be measured.

No deterministic lockstep, arbitrary native snapshot export, unrestricted multi-avatar support, concurrent multi-stage engine context, or multi-process save isolation is assumed to exist.

### 1.3 Initial defaults, not claims of achieved capacity

- Windows is the first implementation and release platform. The injected DLL must match the actual supported executable architecture and ABI; expect to investigate a Win32 bridge rather than assume a 64-bit game.
- Pin one verified executable build, SDK revision, launcher setup, and content configuration initially. Unsupported builds must be rejected explicitly.
- Use C++ for the injected runtime and server/coordinator. Use the toolchain required by the verified SDK for the DLL. Modern C++ in the coordinator must not force incompatible ABI changes into the game bridge. PowerShell and optional test scripts are acceptable tooling.
- Use one coordinator process and a local transactional store initially; do not start with Kubernetes or a distributed microservice deployment.
- The first engine integration test needs two clients and one dedicated original-game worker. Benchmark eight clients later as a provisional engineering target, not a promised supported limit. Publish the supported cap only from measured tests on stated hardware.
- Default to a private, invite-based universe with configurable PvP. Separate species/factions are the default identity model. Shared-faction control can be offered explicitly, with its own permissions and progression policy.
- Standard native content is the initial compatibility profile. Arbitrary third-party gameplay mods are not implicitly supported.

## 2. Evidence boundary: documented capabilities versus project hypotheses

Re-check these primary references against the exact pinned source revision and local executable. Documentation identifies candidates to investigate; it is not runtime verification.

| Reference | Documented starting point | What is NOT established by it |
|---|---|---|
| S1: ModAPI introduction | C++ DLL mods can be loaded into SPORE. | A standalone redistributable engine library or ready-made dedicated server. |
| S2: Detouring | Known functions can be intercepted, and original implementations can be called. | Correct addresses/signatures for every required gameplay hook. |
| S3: GameNounManager | Object creation/enumeration and player/avatar accessors exist; the manager exposes single-player-oriented state. | Multiple native player contexts working concurrently. |
| S4: CreatureAnimal | Creature creation and distinct NPC/avatar AI methods are documented. | A safe, complete remote-player controller or a one-hook AI-disable solution. |
| S5: Combatant | Combat-related state and methods exist; the documentation places substantial combat logic in cCombatSimulator. | Complete attack execution, attribution, rewards, or death handling from setting health. |
| S6: Simulator tutorial | Active-world context and various world/actor interfaces are exposed. | One process simulating arbitrary simultaneous locations and stages. |
| S7: PlanetRecord | Persistent planet records are distinguished from visual planet objects. | A complete, safe world-state import/export or merging API. |

Architecture hypotheses requiring early executable evidence:

H1: original actions can execute for multiple separately owned actors in one process without leaking the native player's state.
H2: authoritative gameplay can be separated from replica presentation without destroying animation, collision presentation, or local controls.
H3: unattended workers can be supervised and isolated safely.
H4: necessary native state can be checkpointed and restored without losing critical references or hidden progression.
H5: multiple original-engine workers can participate in one canonical universe without duplicate global simulation or conflicting saves.
H6: different stages can share consequences in one location without replacing native systems with a parallel game.

Never report a hypothesis as a proven feature because a class name, stub, protocol message, or database table exists.

## 3. Proposed architecture

### 3.1 Components

**Game bridge DLL, player mode.** Captures local intentions, translates authoritative state into native objects, preserves the native camera/UI/editors, and controls which client-side behaviors may execute. It must not independently commit shared gameplay outcomes.

**The same bridge, worker mode.** Runs inside an original SPORE process on the server machine. Receives validated commands, invokes native gameplay, observes resulting state/events, and offers tested checkpoint and world-loading operations. Calls into the engine occur only on verified engine-thread boundaries.

**Server/coordinator executable.** Handles authentication, connections, session state, global IDs, simulation ownership, routing, content manifests, committed-state metadata, and supervision. It does not contain replacement AI, combat, or production formulas.

**Worker supervisor.** Launches, monitors, restarts, and drains original-game workers. It assigns isolated save/configuration/content workspaces, authenticated private IPC, and explicit ownership generations. It can initially live in the coordinator executable.

**Launcher/install tooling.** Validates compatibility, starts the correct client or server configuration, reports actionable failures, and protects personal saves. A separate client networking helper is allowed only if a measured dependency/ABI issue justifies it.

**Transactional store and artifact store.** Persist identities, ownership, checkpoints, transfer records, content hashes, and committed outcomes. Native save artifacts remain native engine data; the database does not become a second implementation of the game.

Conceptual flow:

    Player's original SPORE + bridge
        -> action intention over authenticated transport
        -> coordinator: identity, ownership, routing
        -> authoritative original SPORE worker + bridge
        -> original gameplay functions and simulation
        -> observed authoritative state/events
        -> coordinator persistence/routing
        -> client native presentation

Use explicit serialized IPC/network messages. Never transport C++ pointers, raw object memory, STL/EASTL containers, or engine ABI assumptions between processes.

### 3.2 Authority rules

Every mutable gameplay domain must have one active authority at a time. Describe ownership for local creatures, cities, planet terrain, inventories, species progression, empire relations, and galaxy-level events. The native simulator owning a resource produces its gameplay outcomes; the coordinator enforces ownership and records/routes those outcomes.

A worker can contain native caches or views of data owned elsewhere. It must not autonomously mutate the same world records merely because a full local game instance is running. Audit galaxy-wide AI, economy, disasters, diplomacy, and other background systems instead of only visible entities.

Use an ownership generation/fencing token. A stopped, disconnected, stale, or partitioned worker must not retain permission to commit results after ownership changes.

Do not initially fix the partition to one planet or one player per process. Determine the smallest safely reusable native simulation context through experiments. Multiple players in one scene should share an authoritative simulation rather than run independent authoritative copies.

### 3.3 Important implementation invariants

Keep DllMain minimal and follow the SDK lifecycle plus Windows loader-lock restrictions [S9]. No blocking network startup, joins, extensive disk work, or engine manipulation during arbitrary loader callbacks.

Network and IPC threads validate/decode into bounded queues. Engine-thread code resolves IDs to valid current objects and executes work. Deferred messages must not carry unprotected raw engine pointers.

Interception that calls the original function must be reentrancy-safe. Replica application must not re-emit the same action over the network. Keep authoritative, predicted, and presentation-only operations distinct.

A simple temporary swap through the avatar accessors is not proof of multi-player context support. Delayed callbacks, timers, AI updates, caches, rewards, UI state, and saves may outlive that swap. Investigate asynchronous ownership explicitly.

Native simulation timing and network snapshot frequency are separate. Define mappings between native per-worker clocks, authoritative simulation time, and wall-clock time; preserve timer meaning across restore and transfer. Do not advance the simulation extra times, substitute a new timestep model, or skip render-dependent work without behavior tests.

### 3.4 Protocol requirements

Define a versioned protocol with a compatibility/content handshake, authenticated player identity, session epoch, command sequence, actor/resource ID, generation, location/context ID, and explicit ownership checks.

Required message families: connect/authenticate; join/resume; content manifest/fetch; scene begin/readiness; entity create/update/remove; action request/accept/reject/result; authoritative events; checkpoint/delta; editor begin/commit/cancel; transition prepare/commit/abort; resynchronization; shutdown/error.

Use reliable delivery for lifecycle, inventory, progression, ownership, and transactional messages. Use appropriately sequenced replaceable snapshots for transient motion. Separate bulk content transfer from latency-sensitive traffic and bound all buffers, sizes, rates, and decompression.

Select an existing maintained transport after a build/integration check. GameNetworkingSockets is a candidate [S8]; verify its exact build requirements, authentication setup, and deployable connectivity features. Do not assume standalone deployments inherit Steam relay access. Do not implement bespoke cryptography or a reliable-UDP stack as the project foundation.

Prevent stale snapshots resurrecting deleted entities. Use entity generations/tombstones, scene epochs, baseline acknowledgments, and bounded out-of-order handling. Reconnect starts from an authoritative baseline, not the client's memory of its last session.

### 3.5 Persistence and failure semantics

Do not let two processes write a common live SPORE save directory. Do not blindly merge galaxy databases, copy half-written save files, or accept player saves as authoritative.

Checkpoint native state at a verified safe boundary; associate it with the coordinator's IDs, ownership versions, content manifest, and relevant transaction state. Commit a manifest only after its artifacts are complete and validated. Preserve backups and support an explicit rollback/recovery path.

A command being received is different from its result being durable. Define acknowledgments for accepted, applied, and durably committed state where necessary. A database transaction alone cannot make an engine side effect crash-atomic. Test kills between each boundary.

Do not rely on replaying input commands after a crash unless native simulation determinism and all required state restoration have actually been proven. Prefer validated checkpoints and persisted outcomes for non-deterministic behavior. An event journal alone is not a complete native save.

Report the rollback window for transient world simulation. Durable ownership/progression/transfer acknowledgments must obey the chosen committed-state contract rather than silently disappear or duplicate.

## 4. Repository and execution discipline

Adapt to the existing repository instead of deleting or replacing user work. Suggested layout:

    AGENTS.md
    GOAL.md
    MILESTONES.md
    STATUS.md
    docs/
      architecture.md
      protocol.md
      authority-matrix.md
      compatibility.md
      multiplayer-policy.md
      native-behavior-baseline.md
      research-log.md
      testing.md
      recovery.md
      sources.md
      adr/
    src/
      bridge/                 # Only this layer depends directly on ModAPI/engine ABI.
      protocol/
      coordinator/
      supervisor/
      persistence/
      launcher/
      stages/{cell,creature,tribal,civilization,space}/
    tests/{unit,protocol,integration,engine,soak}/
    tools/{build,diagnostics,reverse-engineering}/
    config/

Do not create dozens of empty files to simulate progress. Create real interfaces as they become necessary. Engine-facing methods that cannot yet be implemented must return an explicit unsupported/error result, never fabricated success.

Use milestone statuses TODO, IN_PROGRESS, IMPLEMENTED_NOT_RUN, VERIFIED, and BLOCKED. Engine-dependent completion requires actual original-game execution. Unit tests with mocks do not satisfy native gameplay acceptance tests.

For each milestone, record objective, dependencies, changed modules, exact build/test commands, expected results, observed results, evidence paths, unresolved issues, and next step. Keep evidence tied to repository revision, executable fingerprint, SDK revision, content profile, OS, and hardware.

Record the provenance of every engine binding: source/header or reverse-engineering evidence, build identity, signature/calling convention, expected object layout, thread/context constraints, and test. No invented addresses or guessed signatures presented as facts.

When game execution is unavailable, implement legitimate pure-code components and executable test harnesses. Mark engine tests NOT RUN with the missing prerequisite. Do not fabricate captures, logs, benchmark figures, or compatibility results.

## 5. Detailed milestones

### M00 — Establish the project contract and actionable backlog

**Dependencies:** none.

**Work:** Inspect the repository, toolchain, installed SDK/game, and available testing machines. Record existing code before changing it. Create GOAL.md, AGENTS.md, the full milestone checklist, and a concise STATUS.md. Write initial authority and architecture decisions from this brief. Separate native behavior, new multiplayer policy, and unknown engine capabilities. Map every required feature to an eventual acceptance test.

**Deliverables:** working project documents; prerequisite inventory; executable-build identification plan; dependency graph; initial test commands or a clearly identified missing toolchain.

**Acceptance:** the plan still includes every original stage, dedicated authority, independent locations/progression, reconnects, persistence, and engine reuse. It does not redefine the final product as a movement demonstration. Every unknown has a concrete investigation and an expected observable result. Begin M01 rather than ending the task with documents only.

### M01 — Reproducible build, compatibility guard, and save protection

**Dependencies:** M00.

**Work:** Build a minimal real ModAPI DLL for the locally verified executable. Pin the SDK and dependency revisions. Set the correct architecture, calling conventions, runtime linkage, and build configurations. Log bridge initialization/disposal after the appropriate lifecycle callbacks. Identify the game's actual file/configuration access locations. Create isolated disposable test profiles, back up personal saves, and add a compatibility validator that refuses unknown binaries or incompatible content before unsafe hooks attach.

**Deliverables:** build scripts, DLL artifact, a diagnostic launcher/command, compatibility manifest, isolated test workspace, and uninstall/rollback instructions.

**Acceptance:** a clean checkout builds with documented prerequisites. The DLL loads in the real game, records its verified build fingerprint, and exits cleanly. A deliberately mismatched build/configuration is rejected without attempting unsafe bindings. Test runs do not modify the user's personal save profile. Test multiple clean launches and exits, not just compilation.

### M02 — Native-behavior baseline and engine observability

**Dependencies:** M01.

**Work:** Add structured tracing for game mode, scene lifecycle, entity creation/destruction, player identity, native action entry/result, AI updates, damage/death, inventory/progression, and save/load as relevant bindings become verified. Establish reference runs with multiplayer mutations disabled. Inventory entry points for all five stages now, so later stage risk is visible early. Build a read-only diagnostic overlay or console and guarded investigation commands.

**Deliverables:** native-behavior baseline, binding registry, stage capability matrix, reusable fixtures, and research log containing reproductions rather than speculation.

**Acceptance:** at least one original gameplay action is traced from invocation to outcome with the original implementation still executing. Logs distinguish engine events from mod events. Lifecycle tests identify entity invalidation. Observational instrumentation does not double-apply actions or change the baseline behavior. Unidentified internals remain labeled unknown.

### M03 — Multiple actors using original gameplay functions

**Dependencies:** M02.

**Work:** In a controlled original-game scene, create/manage two independently owned actor representations. Drive both through native movement and action mechanisms, not transform-only teleports or custom attack formulas. Investigate local-avatar, player, species, faction, and inventory assumptions. Test actions during intervening AI ticks, delayed callbacks, death, and object recreation. Prefer verified per-actor entry points; carefully scoped context adaptation is an investigation, not an assumed solution.

**Deliverables:** engine-side command harness, actor ownership mapping, a native context audit, and recorded two-actor tests.

**Acceptance:** actor A and actor B can receive distinct commands; a native NPC can interact with either; damage, action attribution, and any tested rewards are assigned correctly. A command for A never spends B's resources. Native action processing—not merely a health setter—causes the outcomes. At least one asynchronous action completes under the correct identity. If the worker requires a native server-only avatar, prove it does not steal targets/rewards or silently determine which remote-player areas remain active. Record the exact behavior still unsupported.

**Critical gate:** if multi-actor gameplay is not verified, mark dependent multiplayer gameplay BLOCKED. Continue binding research and independent infrastructure, but do not substitute an invented simulator or mark movement as complete co-op.

### M04 — Unattended original-game worker and isolation

**Dependencies:** M01–M03 for an interactive worker acceptance test; basic supervision can start after M01.

**Work:** Implement coordinator-to-worker private IPC, readiness/health states, startup arguments/configuration, scene loading, controlled shutdown, and crash detection. Start with normal rendering. Investigate focus loss, minimization, dialogs, desktop-session requirements, process-instance restrictions, file locking, and configuration/global path use. Verify isolation using observed file access; changing one environment variable is not proof. Investigate native save/load of the controlled fixture and basic restoration of mod identities.

**Deliverables:** supervisor, worker configuration, isolated workspace manager, IPC protocol, and unattended operation instructions.

**Acceptance:** a worker runs the verified native action test without a human playing it; a player client can close without shutting down authority. Two worker processes operate on separate test workspaces without save/configuration cross-writes. Kill one and observe detection while the other remains healthy. Reload a controlled native checkpoint. State any required desktop or separate-user/VM setup honestly.

**Critical gate:** do not plan unlimited workers around unverified launch or persistence isolation.

### M05 — Separate authoritative simulation from replica presentation

**Dependencies:** M02–M04.

**Work:** Identify and control the client's duplicate AI decisions, damage/death, pickups, rewards, spawning, timers, and progression. Preserve local camera, UI, animation, audio, interpolation, and appropriate movement presentation. Add explicit execution roles and a replica-application guard. Design local movement prediction only around proven reversible state; never replay irreversible native rewards as part of prediction.

**Deliverables:** authority matrix, native hook policies, replica object lifecycle, and mutation-audit counters.

**Acceptance:** applying a worker result updates the client without triggering a second authoritative action or reward. Deliberately divergent local NPC decisions cannot permanently override the worker. Disabling the multiplayer connection does not allow a client to upload locally invented progress later. Single-player runs with multiplayer disabled retain the observational baseline.

### M06 — Real network session and authoritative scene replication

**Dependencies:** M04–M05.

**Work:** Integrate the selected transport, authenticate sessions, enforce protocol/build/content handshakes, and route to the dedicated worker. Implement global IDs, entity generations, scene epochs, reliable spawn/despawn, motion snapshots, interpolation, baseline acknowledgments, bounded queues, and basic reconnect. Separate engine calls from networking threads. Use explicit protocol schemas, not native memory dumps.

**Deliverables:** coordinator networking, game bridge transport/IPC integration, protocol tests, and a repeatable multi-process harness.

**Acceptance:** two real client game processes join one original-game worker, load a consistent fixture, and observe distinct controlled actors. Out-of-order motion cannot resurrect an entity or change scene ownership. Unsupported versions fail clearly. Malformed/oversized messages are rejected. A reconnect obtains a fresh authoritative baseline without duplicating a player.

### M07 — First complete native shared encounter

**Dependencies:** M03, M05, M06.

**Work:** Integrate original movement, target selection, AI response, attack execution, damage, death, at least one contested pickup/reward, and respawn or scene reset. Ensure client-origin requests reach the correct actor in the dedicated worker. Capture native action traces alongside authoritative network events and normalized shared-state digests.

**Deliverables:** an end-to-end shared encounter regression test with recordings, logs, and assertions.

**Acceptance:** both players fight the same NPC, which can target either player; all machines agree on its final health/death and the single reward outcome. Simultaneous pickup requests award the resource once. Reconnect after death does not restore the NPC or duplicate the reward. Repeat with configured latency/loss and with one client attempting an unauthorized action.

This is the first meaningful multiplayer integration slice, not the final product and not a reason to stop at one scene.

### M08 — Native content, terrain, and editor-commit foundation

**Dependencies:** M06–M07.

**Work:** Create a content-addressed manifest for custom creations and their dependencies; map global content identities to native resource keys. Verify the actual creation representation needed by the engine instead of assuming an image alone contains everything. Transfer only approved data, with size/dependency validation and a quarantined import path. Keep native assets installed locally. Synchronize canonical generated terrain and relevant world records, not just a random seed. Establish native editor begin/commit/cancel transactions, with immutable creation versions and authoritative native validation of the resulting gameplay properties.

**Deliverables:** content registry/cache, dependency checks, readiness barriers, terrain identity checks, and a basic editor transaction path.

**Acceptance:** a newly created native creature is loaded consistently by the worker and a clean second client. Its gameplay properties are checked through verified native behavior, not trusted client numbers. Missing parts or terrain mismatches block readiness with a precise error. Editing does not globally pause the session or publish a half-finished creature. Oversized, corrupt, or path-traversal content fails safely.

### M09 — Durable checkpoints, reconnects, and crash recovery

**Dependencies:** M04, M07, M08.

**Work:** Implement safe native checkpoints plus coordinator manifests and ID mappings. Define accepted/applied/durable acknowledgment semantics and rollback windows. Add backups, schema migration boundaries, consistent checkpoint barriers, and restore validation. Protect critical progression/inventory changes from duplicate application. Test abrupt termination at every boundary between a native action, event capture, database write, save artifact, and durable acknowledgment.

**Deliverables:** transactional store, artifact/checkpoint manager, recovery commands, crash-injection tests, and documented persistence contract.

**Acceptance:** restart the coordinator and worker, reconnect both clients, and restore the tested world, identities, creations, inventory, and progression. Duplicated/resubmitted requests do not grant duplicate rewards. A durable acknowledgment remains true after recovery. An interrupted checkpoint cannot replace the last good one. Any loss of non-durable transient motion is within the documented window. Passing mocks alone is insufficient.

### M10 — One universe across multiple native locations

**Dependencies:** M04, M08, M09.

**Work:** Add canonical universe/star/planet/location identifiers; worker scheduling; ownership leases and fencing; area activation/deactivation; and interest subscriptions. Establish who owns global native systems as well as local ones. Build a transfer state machine: prepare destination, validate content/checkpoint, quiesce and fence source authority, restore destination, commit ownership, and clean up. Persist each transfer step and support abort/recovery. Native outcomes from an old worker generation are rejected.

Initially use explicit test travel commands if necessary; these are test harness operations, not a replacement for the eventual native Space travel implementation.

**Deliverables:** universe coordinator, ownership matrix covering native background systems, transfer protocol, and split-brain tests.

**Acceptance:** players in two separately simulated locations share one canonical universe, can meet in the same scene, separate again, and reconnect without diverging histories. Killing either worker during every transfer step creates neither two authoritative avatars nor a lost persistent avatar. Two workers must not double-run the same global economy/diplomacy event. Prove shared consequences, not just shared planet names.

### M11 — Creature stage gameplay coverage

**Dependencies:** M07–M10.

**Work:** Implement the Creature stage adapter across movement, eating/hunger, combat, social actions, relationships, packs, nests, spawning/despawning, native unlocks/DNA, mating/editor return, death/respawn, and native progression inside the stage. Map per-player species state separately from local avatar state. Test distant players and camera-dependent activation. Connect outgoing stage-change requests to the transition contract; full cross-stage completion is M17.

**Deliverables:** Creature gameplay checklist, verified bindings, fixtures, and native-versus-multiplayer behavior comparisons.

**Acceptance:** two players can play the normal supported Creature loop with different ownership identities; social and combat outcomes agree; progression and creations persist. Player separation does not delete another player's active encounter. A player editing or disconnecting cannot freeze the other player's world or overwrite their species. Record any intentional shared-faction behavior separately. Run an extended session and repeat after restore.

### M12 — Cell stage gameplay coverage

**Dependencies:** M08–M10; reuse infrastructure, not assumptions about Creature classes.

**Work:** Investigate the native Cell-stage subsystem independently. Integrate control, collision, food, attacks/defenses, growth/scale changes, native part unlocks, editor return, death/restart, and progression within the stage. Define how cell-level sessions reference their canonical planet without inventing incompatible terrain or spatial correspondence. Support multiple cell players in the same relevant simulation, not only parallel solo cells.

**Deliverables:** Cell adapter, scale-aware state schema, native action traces, and stage tests.

**Acceptance:** two players interact with shared cell-stage entities; contested food is consumed once; collisions and native outcomes agree through growth changes. Native editor and death/restart behavior preserve correct ownership. Save/reconnect restores stage progress. Transition eligibility is recorded correctly for M17. Do not claim the stage works from a Creature-stage actor rendered smaller.

### M13 — Tribal stage gameplay coverage

**Dependencies:** M08–M11 infrastructure; M12 is not a code dependency.

**Work:** Integrate native unit selection/commands, gathering, food storage, tools, buildings, native population mechanics, combat, social/instrument interactions, gifts, relationships, and tribe progression. Distinguish human-owned tribes from AI tribes. Ensure native AI is retained for NPC tribes but cannot issue contradictory decisions for a player-owned tribe. Establish access controls for optional shared-tribe play.

**Deliverables:** Tribal adapter, faction permissions, multi-unit command protocol, and native economy/action tests.

**Acceptance:** two independently owned tribes can interact with each other and native NPC tribes. Resource spending, ownership, conquest/social outcomes, and progression agree. A client cannot command the other tribe's units or spend its food. Death, rebuilding, editor/configuration operations where applicable, reconnect, and checkpoint restore work. Preserve native calculations instead of moving a simplified resource loop into the coordinator.

### M14 — Civilization stage gameplay coverage

**Dependencies:** M08–M10 and faction/command infrastructure from M13.

**Work:** Integrate city ownership and management, buildings, native production, resource nodes, vehicle creation and controls, land/sea/air movement, military/economic/religious actions, diplomacy, native special abilities, and stage victory/progression. Audit implicit player-civilization state in UI and native action paths.

**Deliverables:** Civilization adapter, city/vehicle replication, ownership transfer handling, and three-strategy test fixtures.

**Acceptance:** two civilizations issue independent commands; the same city cannot be conquered or purchased inconsistently; production and resources are not double-counted. Exercise military, economic, and religious interactions, including concurrent requests. Reconnect and worker recovery preserve cities, vehicles, diplomacy, and progression. Completing a native stage goal produces one valid transition request for M17.

### M15 — Space stage and persistent galaxy gameplay

**Dependencies:** M08–M10 and mature faction/persistence infrastructure.

**Work:** Integrate native ship control and planet/system/galaxy travel; tools and combat; inventories, energy and currency; native trade and spice production; colonies and buildings; terraforming/ecosystems; relationships, missions, empire events, and native progression/unlocks. Route native travel through M10 transfers. Establish single ownership of galaxy-wide background simulation and correct treatment of unloaded areas using observed native behavior, not guessed offline-growth formulas.

**Deliverables:** Space adapter, native galaxy-state mapping, travel integration, world-event ownership tests, and inactive-area policy.

**Acceptance:** two independent empires explore different systems, meet, trade or fight under configured policy, alter a planet, leave, and observe the same result after returning and restarting the server. Inventory/currency cannot duplicate across travel or trade. Native background outcomes occur once, not per worker. Leaving a planet must not restore an older client's version. Do not certify this milestone from ship movement alone.

### M16 — Mixed stages and cross-location consequences

**Dependencies:** M10–M15 for the relevant stage combinations.

**Work:** Produce an explicit stage-pair interaction matrix. First prove independent stages coexist on separate planets; then prove relevant stages can share consequences on one planet. Investigate native tech-level assumptions, physical scale, stage-local representations, ownership, and how Space tools affect an occupied lower-stage world. Use one owner per affected world resource and verified native functions for effects. Do not run two independently authoritative versions of the same terrain, city, or population.

Some stage pairs have no direct native interaction. Mark those not applicable with an explanation, not a fictional new mechanic. Native cross-stage interactions relevant to the product may not be silently removed merely because they are difficult.

**Deliverables:** stage-pair matrix with status/evidence, canonical world projection rules, and cross-worker transaction tests.

**Acceptance:** at least one player remains in a lower stage while another is in Space; visiting or affecting that same world produces consistent supported native consequences in both views, survives reload, and does not forcibly advance the lower-stage player. The other applicable stage-pair scenarios have documented tests. No faction's advancement overwrites unrelated player progress. Untested/blocked interactions remain visibly incomplete.

### M17 — Native editors and the full Cell-to-Space campaign

**Dependencies:** M08–M16.

**Work:** Complete all required native editor transactions and all four campaign stage transitions. Bind native eligibility/unlock checks to persistent species ownership. Checkpoint before transitions, prepare destination context/content, execute the native transition where possible, commit the new state, and restore client presentation. Handle failures, cancellation, disconnection, concurrent edits, and simultaneous stage transitions. Define explicit consent/ownership behavior for shared-species progression.

The world continues for unaffected players. The editing player's native pause behavior must be mapped to a documented multiplayer policy rather than propagated as a global pause.

**Deliverables:** transition orchestration, per-editor coverage, progression lineage, resumable operations, and full campaign test scripts.

**Acceptance:** complete a genuine Cell -> Creature -> Tribal -> Civilization -> Space progression with multiplayer enabled, while another player progresses independently. Validate native prerequisites and inherited progress at every transition. Debug shortcuts can prepare fixtures but cannot be the only evidence that normal progression works. Interrupt each transition and editor commit, then recover without duplication, lost species, globally forced advancement, or cross-player state leakage.

### M18 — Usable hosting, Internet operation, security, and capacity

**Dependencies:** hardening starts with M01; complete integration depends on M06–M17.

**Work:** Provide client join/rejoin UI, standalone server start/stop/status, validated configuration, invitation/password or token authentication, player management, backups, diagnostics export, and actionable compatibility/content errors. Implement a verified direct-connect Internet deployment; add relay support only with an explicit real implementation and deployment requirements. Harden public endpoints, parser boundaries, content import, worker IPC, logging, and admin access. Avoid open unauthenticated admin/debug interfaces.

Measure bandwidth, snapshot behavior, native tick progress, CPU/RAM/GPU per worker, render/focus effects, input correction, save pauses, and capacity. Test two clients first, then the provisional eight-client workload on recorded hardware. Distinguish simulated protocol clients from real running game clients.

Investigate low-render/headless modes only behind optional capability flags. Keep them only if native behavior and lifecycle tests still pass; otherwise document the rendered-worker requirement and its costs.

**Deliverables:** usable launcher/server package, deployment/runbook, threat model, fuzz tests, measured operating envelope, and profiling report.

**Acceptance:** a second machine can join over a real Internet path with correct authentication; reconnect and denial paths work. Spoofed ownership, invalid action rates, stale commands, malformed content, replayed commands, and unauthorized administration do not mutate the world or expose arbitrary execution. Test configured 100 ms RTT/20 ms jitter/1% loss and a harsher 150 ms RTT/30 ms jitter/2% loss profile. Correctness survives; publish measured responsiveness rather than inventing a performance claim. State any port, desktop, GPU, and supported-capacity requirements.

### M19 — Release qualification and complete deliverables

**Dependencies:** M00–M18 verified for required scope.

**Work:** Run the release test matrix on clean installs. Exercise each stage, full campaign, independent progression, mixed-stage interactions, editor transactions, multi-location travel, dedicated persistence, backup/restore, compatibility failures, disconnects, content mismatch, native worker crashes, coordinator crashes, and network partitions. Include a multi-hour soak and repeated lifecycle/transfer tests. Audit the feature matrix for hidden stubs and behaviors implemented only in mocks.

Package source/build scripts, the bridge DLL, coordinator/supervisor, launcher/install tooling, example configuration, end-user instructions, operator guide, supported-version/content matrix, recovery guide, troubleshooting, known limitations, and test evidence. Distribute only project-owned/permitted code and artifacts; require the operator's and players' own original game installations. Do not bundle EA executables/assets or introduce licensing/authentication bypasses.

**Acceptance:** a new user can follow the documentation, host the supported dedicated configuration, join with another user, play the supported all-stage campaign, disconnect, restart, and resume without developer intervention. The release matrix contains no unimplemented mandatory feature disguised as a limitation. Any unresolved requirement keeps the build labeled partial/experimental rather than global-complete. Give a measured supported player/worker configuration, not an unlimited scaling claim.

### M20 — Optional extension: Galactic Adventures multiplayer

**Dependencies:** relevant creature, content, authority, and persistence foundations. This can be investigated earlier, but it must not displace M11–M19.

**Work:** Integrate native adventures, captains, abilities, props, pickups, objectives, mission state, completion, and rewards. Handle adventure-specific assumptions about one captain and objective ownership. Reuse the original adventure system rather than authoring an unrelated mission engine.

**Acceptance:** two players finish shared native objectives and receive the intended non-duplicated rewards; late join, reconnect, and worker recovery preserve mission state. Maintain an adventure compatibility matrix. Do not claim every user-authored adventure works from a single purpose-built test.

## 6. Cross-cutting acceptance and evidence

### 6.1 Native fidelity

Maintain a feature coverage matrix with columns: mechanic, stage, original native entry point, player-context requirements, authoritative owner, client suppression/application hook, persistence representation, baseline evidence, multiplayer evidence, intentional policy differences, and status.

Compare with original behavior under controlled conditions. Use identical initial state/content and RNG state where genuinely controllable. When native behavior is non-deterministic, compare relevant invariants and distributions rather than pretending identical input must produce a bit-identical replay.

For player actions, capture the path into original native execution. Setting an output value to mimic the expected result is not proof of preserved native logic.

### 6.2 Core regression scenarios

T01: two clients move and interact through an original-game worker.
T02: shared enemy selects either player; native damage/death/reward agrees.
T03: simultaneous pickup/spend/ownership attempts produce one valid result.
T04: replica updates cannot cause second authoritative actions.
T05: editor open/commit/cancel does not pause or overwrite other players.
T06: disconnect/reconnect during combat and after rewards does not duplicate entities/progress.
T07: clients occupy separate locations, meet, and separate again in one universe.
T08: stale workers and delayed packets cannot mutate a new ownership generation.
T09: worker/coordinator crash at each checkpoint/transfer boundary restores a valid committed state.
T10: native world changes persist after all players leave and return.
T11: all five normal stage loops and four native campaign transitions work.
T12: different stages progress independently; applicable cross-stage consequences agree.
T13: invalid builds, missing content, malicious inputs, and unauthenticated admin requests fail safely.
T14: a clean install can host/join/resume using shipped instructions.
T15: sustained real-game sessions meet the documented operating envelope.

Each test must identify which assertions use real native game execution and which use pure-code fixtures. Normalize shared-state comparisons by stable IDs and authoritative values; never compare pointers or require client animation poses to match byte-for-byte.

### 6.3 Performance reporting

Record native frame/tick intervals, p50/p95 update delays, network RTT/jitter/loss, snapshot age, correction distance in stage-appropriate units, queue pressure, packet sizes, bandwidth, save/load duration, worker startup/restore time, and CPU/RAM/GPU measurements.

Do not fabricate a universal tick rate. Establish stage-specific operating targets after baseline profiling and keep the game simulation semantics intact. Test process working-set limits and failure under memory pressure on the actual supported build.

A provisional capacity experiment is not a certification. Report the number of real clients, worker count, distribution across scenes, populated-entity workload, and hardware behind every result.

## 7. Handling blockers without changing the product

For a failed capability: record the smallest reproduction; the verified bindings involved; actual/expected behavior; whether failure comes from an unknown binding, engine semantics, process isolation, native save limitations, or an architectural mistake; and the next experiment.

Prefer, in order: documented native interfaces; verified existing SDK bindings; narrowly scoped new reverse-engineered bindings/detours; scoped native context adaptation; isolated original-game processes with explicit state ownership. None of these is automatically guaranteed to work.

Do not add a replacement gameplay subsystem without explicitly identifying the native behavior it replaces and obtaining a separate scope decision. Until then, keep that requirement BLOCKED and work on valid independent milestones. Do not declare impossibility merely because no previous public mod completed it, and do not declare feasibility merely because a function can be hooked.

No endless investigation: each research step should produce an executable probe, a trace, a verified binding, a failed reproduction, or a concrete updated implementation. Keep research integrated with engineering.

## 8. Required first Codex response and work sequence

First summarize the existing repository/environment findings and the specific initial changes you will make. Then create or update the core project contract and begin M01. Do not launch into implementing all stages in one unreviewable change.

If the environment has the game/toolchain, produce the first real loadable diagnostic DLL and run its safe launch test. If it does not, produce the reproducible build/configuration/diagnostic tooling and executable tests that can honestly run, recording the exact missing prerequisites.

At the end of each work session, report:

    Current milestone and status
    Concrete implemented changes
    Exact build/test commands executed
    Observed outcomes and evidence paths
    Native-game tests not run, with reason
    Blocking unknowns and next smallest experiments
    Next implementation step

Keep the full global multiplayer goal intact throughout. A successful early slice is a milestone toward the release, not permission to shrink the final scope.

## 9. Primary source references

These references were inspected for this brief on September 8, 2026. Re-check them and pin actual source revisions during M00/M01. They support the documented entry points, not the unproven architecture hypotheses.

S1 — Spore ModAPI introduction and SDK repository:
`https://modapi-docs.sporecommunity.com/`
`https://github.com/Spore-Community/Spore-ModAPI`

S2 — Detouring and calling original functions:
`https://modapi-docs.sporecommunity.com/_detouring.html`

S3 — GameNounManager:
`https://modapi-docs.sporecommunity.com/class_simulator_1_1c_game_noun_manager.html`

S4 — CreatureAnimal:
`https://modapi-docs.sporecommunity.com/class_simulator_1_1c_creature_animal.html`

S5 — Combatant and its relationship to CombatSimulator:
`https://modapi-docs.sporecommunity.com/class_simulator_1_1c_combatant.html`

S6 — Simulator interaction and active context:
`https://modapi-docs.sporecommunity.com/_simulator_basic.html`

S7 — Persistent planet records:
`https://modapi-docs.sporecommunity.com/class_simulator_1_1c_planet_record.html`

S8 — Candidate maintained networking transport:
`https://github.com/ValveSoftware/GameNetworkingSockets`

S9 — Windows DLL lifecycle restrictions:
`https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices`
