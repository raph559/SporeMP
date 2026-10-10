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

struct NativePickupCheckpoint {
    std::array<std::array<uint32_t, 2>, 2048> grants{};
};

bool snapshot_native_pickup_checkpoint(NativePickupCheckpoint& checkpoint);
bool restore_native_pickup_flags(const NativePickupCheckpoint& checkpoint);
bool restore_native_pickup_checkpoint(const NativePickupCheckpoint& checkpoint);
void initialize_native_actors(const wchar_t* directory);
void dispose_native_actors();
// Engine-thread-only scalar interface to the already implemented M03 harness.
worker::Message native_actor_worker_status();
worker::Result native_actor_worker_command(const worker::Message& request);
// Engine-thread-only actor ownership slice. B player/reward contexts and
// pending bridge actions are rejected; these APIs neither save nor create nouns.
bool snapshot_native_actor_checkpoint(NativeActorCheckpoint& checkpoint, uint64_t request,
                                      bool diagnostic_second_player = false);
bool restore_native_actor_checkpoint(const NativeActorCheckpoint& checkpoint, uint64_t request,
                                     bool restored_second_player = false);
struct NativeAwardCheckpoint;
bool restore_native_actor_awards(const NativeAwardCheckpoint& checkpoint, uint64_t epoch,
                                 bool foreign_checkpoint = false);
void native_actor_worker_event(const char* event, const char* json_fields);
// Disconnected developer import experiment only. The imported native graph
// must already exist and pass its strict source/destination census. These
// bindings authorize movement diagnostics; they do not enable award routing.
bool native_actor_import_probe_ready();
bool native_actor_adopt_import_probe(uint64_t epoch, uint64_t request, uint32_t player_native_id,
                                     uint32_t avatar_native_id, uint32_t herd_native_id);
// Native slots are primary=1 and tracked secondary=2, independent of the
// coordinator's canonical player IDs. Resolves a live, idle pair by census.
bool native_actor_transfer_selection(uint64_t epoch, uint32_t native_slot, uint32_t expected_unique_game,
                                     uint32_t& player_native_id, uint32_t& avatar_native_id);
bool native_actor_transfer_import_ready(uint64_t epoch, uint32_t primary_player, uint32_t primary_avatar,
                                        uint32_t primary_unique_game);
bool native_actor_adopt_transfer_import(uint64_t epoch, uint64_t request, uint32_t primary_player,
                                        uint32_t primary_avatar, uint32_t primary_unique_game, uint32_t imported_player,
                                        uint32_t imported_avatar, uint32_t imported_herd);
// After committed departure, remove only the selected secondary's original
// avatar, tracked player and empty no-nest herd. Primary departure uses actual
// supervised process shutdown, never this helper.
bool native_actor_retire_transfer_secondary(uint64_t request, uint64_t epoch, uint64_t pause_request,
                                            uint32_t expected_unique_game, uint32_t player_native_id,
                                            uint32_t avatar_native_id, uint32_t herd_native_id);
// M05 bounded living-avatar projection. Resolve the current diagnostic ID
// through the live noun census every call. No damage, reward or spawn routines.
bool native_actor_read_vitals(uint64_t local_id, replica::Vitals& value);
bool native_actor_apply_vitals(uint64_t local_id, const replica::Vitals& value);
// Opt-in developer challenge, only after immutable replica denial is armed.
bool native_actor_probe_replica_denials();
// Engine-thread-only M06 mapping; returns a value, never a native pointer.
bool native_actor_owner_native_id(uint32_t owner, uint32_t& native_id);
// Engine-thread-only M07 authority input. The network caller must first fence
// scene/global ID/generation; this boundary rechecks epoch, local noun lifetime
// and owner before queuing the existing original-game command implementation.
// UINT32_MAX means no target. Targeted actions currently admit NPCs only.
worker::Result native_actor_network_command(uint64_t epoch, uint32_t owner, uint32_t actor_native_id,
                                            uint32_t target_native_id, ActorVerb verb, int direction, uint64_t request,
                                            uint64_t resource_id = 0, uint64_t resource_generation = 0);
// Engine-thread input fence. Cancels queued/retained bridge work and uses the
// verified native target-clear path. It does not claim that native movement,
// abilities or the background world are idle.
uint64_t native_actor_fence_network_inputs(const char* reason, bool stop_native = true);
bool native_actor_resume_network_inputs(uint64_t input_epoch);
// Idempotent authority-only bootstrap of the qualified M03 B player/reward
// context. Failure never publishes a fabricated zero B balance.
bool native_actor_network_prepare_rewards();
bool native_actor_experimental_foreign_ready(uint64_t epoch);
// Create a missing secondary home through the original nest factory before
// initial scene publication. Existing saved nests are only validated.
bool native_actor_network_prepare_home();
// current_native distinguishes a present native balance from the retained
// last observed balance of a corpse whose reward context has been retired.
bool native_actor_owner_dna(uint32_t owner, float& dna, bool* current_native = nullptr);
// Observed original first-feed grant beneficiary in the current native scene.
// Zero means unknown; it neither claims nor consumes the remaining corpse food.
uint32_t native_actor_pickup_owner(uint32_t corpse_native_id);
} // namespace sporemp
