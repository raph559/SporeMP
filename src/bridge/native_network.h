#pragma once
#include "../worker/protocol.h"

namespace sporemp {
namespace network {
struct HatchFrame;
}

void initialize_native_network(const wchar_t* directory);
void dispose_native_network();
void native_network_scene_exit();

struct NativeNetworkProjection {
    worker::ProjectionState state = worker::ProjectionState::none;
    uint64_t baseline = 0;
};

// Engine-thread snapshot for periodic authenticated worker status only.
// Non-network/authority/wrong-thread callers receive none and baseline zero;
// a player receives connected only after its native baseline was applied.
NativeNetworkProjection native_network_projection();
// Developer private-IPC probe enters the exact client network intention path.
worker::Result native_network_command(const worker::Message& request);
// Player client only: post one ordinary key message to this process's own game window, where a
// physical key press arrives. The existing window procedure and everything after it are unchanged.
worker::Result native_network_input(const worker::Message& request);
// Player client only: point at, or click, an entity where this process's own window draws it.
worker::Result native_network_click(const worker::Message& request);
// Player client only: point at, or click, a place in this process's own window.
worker::Result native_network_point(const worker::Message& request);
// Original social ability-bar input only. Resolve both current local nouns to
// admitted network identities; the authority validates and executes the input.
bool native_network_social_input(uint32_t actor_native_id, uintptr_t actor_address, uint32_t target_native_id,
                                 uintptr_t target_address, int ability, uint32_t caller_rva);
bool native_network_combat_input(uint32_t actor_native_id, uintptr_t actor_address, uint32_t target_native_id,
                                 uintptr_t target_address, int ability, uint32_t caller_rva);
// The original ability bar's Jump for this player's own living creature: the same intention its Space key sends.
bool native_network_jump_input(uint32_t actor_native_id, uintptr_t actor_address, uint32_t caller_rva);
// A client's click on a fruit, sent as its creature's intention to eat that fruit.
bool native_network_plant_food_input(uint32_t actor_native_id, uintptr_t actor_address, uint32_t fruit_native_id,
                                     uint64_t seen_place, uint32_t caller_rva);
// Presentation-only selection for an authenticated foreign B replica. The
// caller separately checks native callback/ability idleness and never admits
// gameplay mutations. UINT32_MAX/0 identifies a cleared target.
bool native_network_foreign_selection(uint32_t actor_native_id, uintptr_t actor_address, uint32_t target_native_id,
                                      uintptr_t target_address);
bool native_network_hatch_active();
// Current authenticated cue for this exact admitted local actor. Presentation
// adapters do not retain the returned frame across an engine callback.
bool native_network_hatch_offer(uint32_t actor_native_id, uintptr_t actor_address, network::HatchFrame& frame);
bool native_network_hatch_input(uint32_t actor_native_id, uintptr_t actor_address, int ability, uint32_t caller_rva);
bool native_network_pickup_input(uint32_t actor_native_id, uintptr_t actor_address, uint32_t target_native_id,
                                 uintptr_t target_address, uint32_t caller_rva);
bool native_network_fossil_input(uint32_t actor_native_id, uintptr_t actor_address, uint32_t target_native_id,
                                 uintptr_t target_address, uint32_t caller_rva);
// True preserves legacy non-network/bootstrap diagnostics. Once an authority
// publishes its first baseline, dispatch additionally requires its live lease.
bool native_network_authority_inputs_ready();
// Exact connected primary-native-slot death adapter; no bootstrap/off fallback.
bool native_network_death_authority();
// True only during the exact authenticated authority transfer maintenance
// command. Used by native pause/import adapters, never supplied by private IPC.
bool native_network_transfer_context(uint64_t request, uint64_t epoch);
// Private developer control; a successful receipt proves only bridge input
// fencing/target-clear attempts, never full native world quiescence.
worker::Result native_network_fence_authority_inputs(uint64_t expected_scene, uint64_t request);
} // namespace sporemp
