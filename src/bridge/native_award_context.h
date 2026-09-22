#pragma once
#include "native_player_context.h"
#include <Spore/Simulator/cCreatureAnimal.h>

namespace sporemp {
bool prepare_native_awards(uintptr_t base,uintptr_t end,DWORD thread,PlayerContextEvent sink,bool(*check)());
NativeHook native_award_owned_binding();
NativeHook native_award_amount_binding();
NativeHook native_award_player_binding();
NativeHook native_award_avatar_binding();
NativeHook native_award_display_binding();
NativeHook native_award_action_binding();
NativeHook native_award_update_binding();
NativeHook native_award_goal_binding();
bool enable_native_awards(Simulator::cCreatureAnimal* actor,uint64_t actor_id,uint64_t epoch);
// Engine-thread scalar view of the qualified B context. Reading never enters
// StageScope, calls a reward routine, or substitutes the manager's player.
bool native_awards_ready(uint64_t actor_id,uint64_t epoch);
bool native_awards_read_dna(uint64_t actor_id,uint64_t epoch,float& dna,bool allow_retired_corpse=false,bool* current_native=nullptr);
void retire_native_awards();
void update_native_awards(uint64_t epoch);
void sample_native_awards();
bool dispatch_native_owned_award(float amount,void(__cdecl* original)(float),uint32_t caller);
// True only within a root-validated original corpse tick, for the current live
// B identity and the same current corpse still claimed by B. It does not enter
// StageScope or mutate the original actor classification/claim.
bool native_pickup_award_scope_ready();
// Per-scope observation, reset/restored by RAII. True only after the exact
// original first-feed DNA call returned under the valid B context with a finite
// nonnegative native amount and a consistent finite DNA balance. Zero is a valid
// configured native amount and must leave DNA unchanged; a positive amount may
// be capped by the original routine. This does not prove positive DNA or food
// benefit: pair the original eaten transition with separately observed native
// food consumption and actor nutrition before claiming a resource benefit.
bool native_pickup_award_scope_granted();
class NativePickupOwnerScope {
public:
    NativePickupOwnerScope(Simulator::cCreatureAnimal* actor,uint32_t owner,uint64_t id,
        Simulator::cCreatureAnimal* corpse);
    ~NativePickupOwnerScope();
    NativePickupOwnerScope(const NativePickupOwnerScope&)=delete;
    NativePickupOwnerScope& operator=(const NativePickupOwnerScope&)=delete;
private:
    Simulator::cCreatureAnimal* previous_actor;
    Simulator::cCreatureAnimal* previous_corpse;
    uint32_t previous_owner,previous_corpse_native_id;
    uint64_t previous_id;
    bool previous_granted;
};
class NativeDamageOwnerScope {
public:
    NativeDamageOwnerScope(Simulator::cCreatureAnimal* actor,uint32_t owner,uint64_t id);
    ~NativeDamageOwnerScope();
    NativeDamageOwnerScope(const NativeDamageOwnerScope&)=delete;
    NativeDamageOwnerScope& operator=(const NativeDamageOwnerScope&)=delete;
private:
    Simulator::cCreatureAnimal* previous_actor;
    uint32_t previous_owner;
    uint64_t previous_id;
};
}
