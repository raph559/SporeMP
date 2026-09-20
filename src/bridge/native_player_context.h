#pragma once
#include "detour_transaction.h"
#include <cstdint>
namespace Simulator {class cPlayer;}

namespace sporemp {
// Developer Creature fixture only. All native pointers remain inside src/bridge.
using PlayerContextEvent = void(*)(const char* event, const char* json_fields);
bool prepare_native_player_context(uintptr_t base, uintptr_t end, DWORD thread,
    PlayerContextEvent event, bool(*thread_check)());
NativeHook native_player_message_binding();
NativeHook native_player_remove_binding();
bool create_native_player_context(uint64_t epoch);
void sample_native_player_context(uint64_t epoch);
void retire_native_player_context(const char* reason);
Simulator::cPlayer* native_second_player(uint64_t epoch);
}
