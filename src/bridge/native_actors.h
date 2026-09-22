#pragma once
#include "../worker/protocol.h"
#include "replica_policy.h"
#include "actor_commands.h"
#include <array>
#include <cstdint>
namespace sporemp {
// Checkpoint-local native identity candidates, never global persistent IDs.
// The caller must validate the exact closed native artifact before admission.
struct NativeActorFingerprint {
    uint32_t native_id = UINT32_MAX, herd_native_id = UINT32_MAX;
    uint32_t species_instance = 0, species_type = 0, species_group = 0;
    uint32_t archetype = 0;
};
struct NativeActorCheckpoint {
    std::array<NativeActorFingerprint, 2> actors{}; // A, then B
    uint64_t epoch = 0;
};
void initialize_native_actors(const wchar_t* directory);
void dispose_native_actors();
// Engine-thread-only scalar interface to the already implemented M03 harness.
worker::Message native_actor_worker_status();
worker::Result native_actor_worker_command(const worker::Message& command);
// Engine-thread-only actor ownership slice. B player/reward contexts and
// pending bridge actions are rejected; these APIs neither save nor create nouns.
bool snapshot_native_actor_checkpoint(NativeActorCheckpoint& checkpoint, uint64_t request_sequence);
bool restore_native_actor_checkpoint(const NativeActorCheckpoint& checkpoint, uint64_t request_sequence);
void native_actor_worker_event(const char* event, const char* json_fields);
// M05 bounded living-avatar projection. Resolve the current diagnostic ID
// through the live noun census every call. No damage, reward or spawn routines.
bool native_actor_read_vitals(uint64_t local_id, replica::Vitals& state);
bool native_actor_apply_vitals(uint64_t local_id, const replica::Vitals& state);
// Opt-in developer challenge, only after immutable replica denial is armed.
bool native_actor_probe_replica_denials();
// Engine-thread-only M06 mapping; returns a value, never a native pointer.
bool native_actor_owner_native_id(uint32_t owner, uint32_t& native_id);
// Engine-thread-only M07 authority input. The network caller must first fence
// scene/global ID/generation; this boundary rechecks epoch, local noun lifetime
// and owner before queuing the existing original-game command implementation.
// UINT32_MAX means no target. Targeted actions currently admit NPCs only.
worker::Result native_actor_network_command(uint64_t epoch, uint32_t owner,
    uint32_t actor_native_id, uint32_t target_native_id, ActorVerb verb,
    int direction, uint64_t request);
// Idempotent authority-only bootstrap of the qualified M03 B player/reward
// context. Failure never publishes a fabricated zero B balance.
bool native_actor_network_prepare_rewards();
// current_native distinguishes a present native balance from the retained
// last observed balance of a corpse whose reward context has been retired.
bool native_actor_owner_dna(uint32_t owner, float& dna, bool* current_native=nullptr);
// Observed original first-feed grant beneficiary in the current native scene.
// Zero means unknown; it neither claims nor consumes the remaining corpse food.
uint32_t native_actor_pickup_owner(uint32_t corpse_native_id);
}
