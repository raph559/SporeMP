#pragma once
#include "../network/inventory_wire.h"
#include <Windows.h>

namespace sporemp {
bool prepare_native_inventory(uintptr_t base, uintptr_t end, DWORD thread);
bool capture_native_inventory(uint32_t slot, uint64_t epoch, network::InventoryState& state);
bool present_native_inventory(uint64_t epoch, uint32_t owner, const network::InventoryFrame& frame,
                              const network::InventoryState& state, const network::InventoryState* previous);
} // namespace sporemp
