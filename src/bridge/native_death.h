#pragma once
#include "native_player_context.h"

namespace sporemp {
struct NativeDeathRequest {
    uint64_t epoch = 0, occurrence = 0;
    uint32_t native_id = UINT32_MAX, reason = 0, native_slot = 1;
};

bool prepare_native_death(uintptr_t, uintptr_t, DWORD, PlayerContextEvent, bool (*)());
NativeHook native_death_begin_binding();
NativeHook native_death_cinematic_binding();
NativeHook native_death_message_binding();
NativeHook native_death_interaction_binding();
NativeHook native_death_ui_deactivate_binding();
NativeHook native_death_ui_activate_binding();
NativeHook native_death_parameter_binding();
bool pending_native_death(uint32_t native_slot, NativeDeathRequest&);
// Called after original base damage changes an owned victim from positive
// health to zero. Retain its exact reason; dispatch only after original mbDead.
void observe_native_secondary_death(uint32_t native_id, uint32_t reason);
void update_native_secondary_death();
bool complete_native_death(const NativeDeathRequest&);
bool present_native_death(uint64_t occurrence, uint32_t reason);
bool native_death_confirmed(uint64_t occurrence);
bool restore_native_death_view(uint64_t occurrence);
void observe_native_death_pause(bool pausing, uint32_t type, uint32_t caller, int counter, int flags);
bool native_death_checkpoint_idle();
void reset_native_death();
} // namespace sporemp
