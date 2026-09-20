# M04 actor checkpoint identity audit

2026-09-13. Evidence class: SOURCE_REVIEW / IMPLEMENTED_NOT_RUN. No game was launched and no desktop input occurred in this subtask. Parent-session integration owns build, executable checks, native acceptance and final source hashes.

## Existing native evidence

The saved worker-02 generation is `3ad40ff385824d87bd5b5e46b7dce27b`, PID 21944, epoch 3. Its trace `evidence/2026-09-13-m04-native/concurrent-02/actors-21944.jsonl` has SHA-256 `a23db105de0e09eebc5cc6cc6a01d9b45b86dbef121ef76a6268ab2a40a2da30`.

| Role | Bridge actor | Native mID | Herd mID | Species instance / type / group | Native archetype |
|---|---:|---:|---:|---|---:|
| A | 1 | 232 | 208 | 108728283 / 731352134 / 1080189440 | -1362063916 |
| B | 2 | 7425 | 7424 | 108728283 / 731352134 / 1080189440 | -1362063916 |

These values appear in trace events 61, 64-66, 78-84. Both political IDs are UINT32_MAX, so political ID does not distinguish these actors. B's herd is reported as non-avatar-owned, initially disabled, personality 6. No B player or reward context was enabled in this M04 saved run. The trace does not contain a loaded-scene noun census, so it does not prove that B or its herd survived reload.

The preserved closed checkpoint manifest at `local/m04-native/worker-02-after-concurrent/backup-manifest.json` hashes to `6d6aa9208d3eecefb0576b30fdae290d81ea3bfead6a5e02fb2350dbe5702583`. The retained original reload establishes a native scene and live AI, with bridge ownership A=0/B=0. It does not establish absence of native B.

## Source-backed identity and lifetime

Pinned SDK commit was refreshed with `git -C external/Spore-ModAPI rev-parse HEAD` (exit 0): `cbf9206b9a823f0911cd9be0217104a49d72380b`. `cGameData.h` defines mID at +0x24, noun type via virtual GetNounID, and lifetime flags. `cCreatureAnimal.h` defines the native herd reference and noun ID 0x018EB45E. `cHerd.h` defines noun ID 0x01BE418E and its retained member vector. Existing qualified M03 bridge reads cover native animal mID, species ResourceKey, archetype and herd mID.

`ActorCommands` stores local addresses only as nonserialized keys; process-local actor IDs and scene epochs are not global persistent identities. Existing scene exit invalidates bindings and clears queued commands. The original `setup()` creates another animal when no owner-2 binding exists. Therefore checkpoint restore must never call setup.

The new checkpoint API resolves candidates from a fresh native noun enumeration. It requires one match for each actor mID, correct animal type/cast, no destruction/deletion flags, a live native archetype/species context, A equal to the manager avatar, and distinct A/B pointers. It verifies each referenced herd exists as a live noun of the expected type, has a unique native mID and contains the animal exactly once. It then compares all six scalar fingerprint fields. These are checkpoint-local matching candidates; the equality predicate does not itself prove persistence.

No new native creation or ownership-transfer function is invoked. Once all checks pass, two free slots in the existing bridge binding table receive A/B ownership before any native diagnostic callback. The existing `bind()` helper populates tracked runtime identity and logs the already-adopted actors. Native manager avatar/player pointers remain unchanged. Wrong or missing candidates produce a rejection with zero created nouns.

Snapshot requires exactly two live bridge bindings, no pending local setup/player/reward/duel/NPC commands, no queued actor command, no outstanding bridge jump or held intention, and no B player/reward context. Restore requires zero live bindings and the same idle context. Both operations census native players and require exactly the current A player. Full B progression persistence is deliberately outside this actors-only M04 slice; it remains required by M09/M11.

## API and trace contract

Files changed by this subtask: `src/bridge/native_actors.h`, `src/bridge/native_actors.cpp`, and two read-only count observers in `src/bridge/actor_commands.h`. No CMake or IPC schema edit was made here.

- `NativeActorFingerprint`: six u32 values, native_id, herd_native_id, species_instance, species_type, species_group, archetype. Archetype is represented by its u32 bit pattern.
- `NativeActorCheckpoint`: A/B fingerprints and scene epoch. For restore, the caller supplies the current destination scene epoch; the sealed sidecar separately retains the source generation/epoch.
- `snapshot_native_actor_checkpoint(checkpoint, request_sequence)`: fills the value-only result on success and logs two `checkpoint_actor` rows followed by `checkpoint_snapshot` with request, valid and reason.
- `restore_native_actor_checkpoint(checkpoint, request_sequence)`: logs candidate/census evidence and `checkpoint_restore` with request, valid, reason, new actor IDs and created_nouns=0.
- `native_actor_worker_event(event, json_fields)`: engine-thread-only bounded sink into the existing trace, for the parent persistence adapter.

The bridge API assumes its caller has already validated the exact closed native checkpoint. The parent IPC/checkpoint tool must bind the fingerprint pair to an immutable artifact manifest, executable/content/SDK/bridge identity, source worker generation and source scene epoch; use a fresh destination worker generation; reject any file or content mismatch before restore; and never fall back to creating replacement actors. A successful binding reply is not proof of saved native state or a durable acknowledgement.

## Serialization boundary and remaining experiment

Pinned `Serialization.h` exposes native COM serialization with per-class instance-ID maps. Those serialized object references are distinct from cGameData.mID. The presence of that API does not establish that every noun is serialized or that mID survives.

Existing static export `local/m03-static/m04-checkpoint-paths/B29000.c.txt` shows native save calls noun-manager helper B21170 before serializer traversal; inspecting that selection helper is the next precise static step. SDK-labeled cGameData Write/Read entry candidates are B184D0/B18540. Existing Animal/Base write exports C06650/C0BD80 show native class serialization, without proving B inclusion. B's initially disabled herd is a concrete reason to inspect the native selection rule rather than assuming the manager serializes all nouns.

`native_player_context.cpp` retains B's separate cPlayer noun with a native AddRef and retires it through native DestroyInstance/RemoveOwner before Release. Adoption of a serialized B player would require reestablishing that lifetime/listener ownership without calling its fresh-player initializer. That capability is not added here. `native_award_context.cpp` holds B stage state and the original constructed display queue in bridge statics, with no persistence hookup. Native save alone cannot restore those bridge-owned values. Do not invoke the fresh `enable_native_awards()` path over nonzero restored progression.

Required native acceptance: capture the current fingerprint pair, obtain an actual closed native save, hash-seal it, start a new guarded generation, load those exact native artifacts, census/adopt the matching existing nouns, reject stale generation/epoch and mismatched fingerprints, and execute one original action per adopted actor with trace-correlated outcome. Record complete before/after candidate state and any missing native noun. No replacement B may conceal a failed restore.

## Verification performed in this subtask

Read-only `rg`, bounded `Get-Content`, native-trace extraction, pinned PE string inspection and SHA-256 commands were used. `python tools/native/inspect-pinned-pe.py mPoliticalID`, `mUniqueGameID`, `mHerd`, `cGameData --rtti`, and `cHerd --rtti` all exited 0 and loaded the original executable as data only. A Python `import pefile,capstone` capability probe exited 1 because pefile was not installed; no packages were installed. Some initial PowerShell-glob `rg` reads returned filename syntax errors and supplied no evidence.

`git diff --check -- src/bridge/native_actors.cpp src/bridge/native_actors.h src/bridge/actor_commands.h` exited 0, but these files were already untracked in this checkout, so this was not meaningful diff validation. Build, HOST tests and native execution: NOT RUN by this subtask; parent integration will perform the appropriate checks.

Source hashes at handoff (subject to later parent integration): native_actors.cpp `c3023d59d7b57f2907ac50b08385f8c7d917a6890d71b30d8c3b5ebdca370ddc`; native_actors.h `be9ea3fc71c88a441819ccfec24e2b4060af93fe199a8488ff37a2320dd201c6`; actor_commands.h `d04fe708a2a8c71df76f47107522f765b37954803b472dc349dd2294923deb03`.

SDK source hashes: cGameData.h `3e02e1e8cb4b731ec88e9a157fcf6f5e69dc8ef9b1aed2b3fd040df9ce7aaae1`; Serialization.h `36d3560c20cfe21f9c841274cd073d7f1ab6ada644c4d750da819a8a65963569`; cHerd.h `a47b70ec052273c48fe07b487955da59e7e6ef1b9ba27d10e3ff40d0c2aa2585`; cCreatureAnimal.h `d4bcabbebb5283677e25b02280db54d2b8673ad21c3fa1f7a47d88aec122f7d3`.
