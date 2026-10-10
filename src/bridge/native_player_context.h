#pragma once
#include "detour_transaction.h"
#include <cstdint>

namespace Simulator {
class cPlayer;
}

namespace sporemp {
// Developer Creature fixture only. All native pointers remain inside src/bridge.
using PlayerContextEvent = void (*)(const char* event, const char* json_fields);
bool prepare_native_player_context(uintptr_t base, uintptr_t end, DWORD thread, PlayerContextEvent sink,
                                   bool (*thread_check)());
NativeHook native_player_message_binding();
NativeHook native_player_remove_binding();
bool create_native_player_context(uint64_t epoch);
void sample_native_player_context(uint64_t epoch);
// Bounded read-only census for original-save qualification. No player is
// created, adopted, modified or serialized by this observation.
bool observe_native_player_checkpoint(uint64_t request, const char* phase);
// Read-only observation around the already-bound original UnlockPart call.
// The items receiver is compared to the current player census before reading.
void observe_native_part_unlock(void* receiver, uint32_t instance, uint32_t group, int cost, uint32_t caller_rva,
                                bool returned, bool result);
bool validate_native_player_checkpoint(Simulator::cPlayer* player, uint32_t id);
bool adopt_native_player_checkpoint(Simulator::cPlayer* player, uint64_t epoch);
void retire_native_player_context(const char* reason);
Simulator::cPlayer* native_second_player(uint64_t epoch);
} // namespace sporemp
