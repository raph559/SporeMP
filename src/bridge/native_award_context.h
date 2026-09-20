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
void retire_native_awards();
void update_native_awards(uint64_t epoch);
void sample_native_awards();
bool dispatch_native_owned_award(float amount,void(__cdecl* original)(float),uint32_t caller);
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
