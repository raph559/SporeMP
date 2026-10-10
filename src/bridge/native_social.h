#pragma once
#include "native_player_context.h"
#include "../network/social_wire.h"
#include <cstdint>

namespace Simulator {
class cCreatureAnimal;
}

namespace sporemp {
// The pinned original ability dispatcher serves both social and combat UI.
// Read-only admission of the four normal Creature attacks on a current noun.
bool native_combat_ability(Simulator::cCreatureAnimal* actor, uint32_t index);
// Retain the original avatar behavior root for the admitted secondary human.
// Social attribution is entered separately; idle control owns no social meter.
bool ensure_native_network_player_controller(Simulator::cCreatureAnimal* actor, uint32_t native_id, uint64_t id,
                                             uint64_t epoch, bool experimental_foreign = false);
// Read-only validation for a current, living secondary human controller.
bool native_network_player_controller_matches(Simulator::cCreatureAnimal* actor, uint64_t& id);
// Scene exit retires observations after the original scene owns noun cleanup.
void reset_native_network_player_controller();
// Authenticated native input uses the same bounded original B social scope.
// Admission still requires the qualified empty-pack, same-species context.
bool ensure_native_network_social_owner(Simulator::cCreatureAnimal* actor, uint32_t native_id, uint64_t id,
                                        uint64_t epoch);
bool finish_native_network_social_owner(uint64_t id, uint64_t epoch);
// A new authenticated movement/target action can abandon the native exchange.
// Never discard a pending reward, objective receipt or owner tutorial.
bool cancel_native_network_social_owner(uint64_t id, uint64_t epoch);
// Outside original AI callbacks: release a completed network exchange only
// after its reward counter and any owner tutorial have finished.
void update_native_network_social_owner(uint64_t epoch);
// Original UI snapshot for the currently active social owner. Native IDs are
// process-local lookup candidates, converted to admitted identities by network.
bool capture_native_social_view(uint64_t epoch, uint32_t& owner, uint32_t& actor_id, uint32_t& target,
                                network::SocialFrame& frame);
bool present_native_social_view(const network::SocialFrame& frame);
void clear_native_social_view(bool hide = true);
// Opted-in, local Creature investigation. This does not qualify a network verb,
// native social eligibility, independent rewards, relationships or pack state.
bool prepare_native_social(uintptr_t base, uintptr_t end, DWORD thread, PlayerContextEvent event, bool (*check)());
NativeHook native_social_input_binding();
NativeHook native_social_tick_binding();
NativeHook native_social_result_binding();
NativeHook native_social_pause_binding();
NativeHook native_social_resume_binding();
NativeHook native_social_meter_relation_binding();
// Caller fences scene, owner, current native IDs, authority and persistence.
// UINT32_MAX requests an encounter; 0..87 submits a native social ability index
// only while the actor is already in the original SocialRespond behavior.
// Return means a native order was read back, not that gameplay succeeded.
bool native_social_probe(Simulator::cCreatureAnimal* actor, uint32_t actor_native_id,
                         Simulator::cCreatureAnimal* target, uint32_t target_native_id, uint32_t ability);
// Match original D1E930 social controls: greet the selected target unless already
// in SocialRespond, otherwise submit the selected ability. Authority only.
bool native_social_control(Simulator::cCreatureAnimal* actor, uint32_t actor_native_id,
                           Simulator::cCreatureAnimal* target, uint32_t target_native_id, uint32_t ability);
void sample_native_social_actor(Simulator::cCreatureAnimal* actor, uint64_t actor_id);
// Local-only, reversible native tree selection experiment. Caller supplies a
// live secondary binding. Manager/player roots are never changed. Mode 2
// temporarily classifies B inside the audited original social callbacks only.
// Mode 0 restores; 1 selects the tree only; 2 additionally probes the bounded
// original social-player classification and result-owner path. Local fixture,
// empty pack and no concurrent social encounter only; not network acceptance.
// Mode 3 adds owner-bound first-DNA tutorial deferral until owner retirement.
// It does not deliver the tutorial; pending presentation blocks checkpoints.
bool native_social_tree_probe(Simulator::cCreatureAnimal* actor, uint32_t actor_native_id, uint64_t bridge_id,
                              uint64_t epoch, uint32_t mode);
} // namespace sporemp
