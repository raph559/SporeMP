#pragma once
#include "native_player_context.h"
#include <Spore/Simulator/cCreatureAnimal.h>
#include "../network/hatch_wire.h"

namespace sporemp {
bool prepare_native_hatch(uintptr_t, uintptr_t, DWORD, PlayerContextEvent, bool (*)());
NativeHook native_hatch_binding(unsigned index);
// Called inside the existing guarded B restart leaf. These select an original
// constructed drama manager only for that call and restore the primary one.
// Read-only preflight; call before resetting the pending death handler.
bool native_secondary_hatch_ready(Simulator::cCreatureAnimal*, uint64_t current_epoch);
bool begin_native_secondary_hatch(Simulator::cCreatureAnimal*, uint64_t current_epoch);
bool end_native_secondary_hatch();
bool native_secondary_hatch_selected(const void* manager);
bool native_hatch_checkpoint_idle();
bool capture_native_hatch(uint32_t& native_id, uint64_t& current_epoch, network::HatchFrame&);
bool apply_native_hatch_input(uint32_t native_id, uint64_t current_epoch, const network::HatchFrame&);
bool present_native_hatch(const network::HatchFrame&);
void reset_native_hatch();
} // namespace sporemp
