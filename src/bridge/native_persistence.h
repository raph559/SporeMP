#pragma once
#include "../worker/protocol.h"
#include <Windows.h>
#include <cstdint>

namespace sporemp {
using NativePersistenceEvent = void(*)(const char*, const char*);
enum class NativePersistenceState : uint64_t {
    idle=0, save_pending=1, saving=2, saved=3, load_pending=4,
    loading=5, loaded=6, failed=7, unavailable=8
};
struct NativePersistenceSnapshot {
    uint64_t request=0;
    NativePersistenceState state=NativePersistenceState::unavailable;
    bool native_save_result_valid=false;
    bool native_save_result=false;
};
// Developer controlled Satiria Creature fixture only. Initialize after the
// executable/content/loader guard and actor observation hooks. All entry points
// are restricted to the SDK app-update thread; no I/O thread calls native code.
bool initialize_native_persistence(uintptr_t base,uintptr_t end,DWORD thread,NativePersistenceEvent sink);
worker::Result native_persistence_command(const worker::Message& request,const worker::Message& status);
// Invoke before consuming a new IPC request, so acknowledgement precedes work.
void update_native_persistence(const worker::Message& status);
void annotate_native_persistence(worker::Message& status);
NativePersistenceSnapshot native_persistence_snapshot();
bool native_persistence_busy();
void dispose_native_persistence();
}
