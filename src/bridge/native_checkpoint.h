#pragma once
#include "native_persistence.h"
#include "native_actors.h"

namespace sporemp {
bool observe_native_checkpoint_destroy(const void* noun, uint32_t caller_rva);
// Original serializer extension, registered only after compatibility and SDK
// initialization. All methods run on the engine thread.
bool initialize_native_checkpoint(uintptr_t base, uintptr_t end, DWORD thread, NativePersistenceEvent sink);
bool arm_native_checkpoint(const NativeActorCheckpoint& actors, uint64_t request);
bool finish_native_checkpoint_write();
uint32_t native_checkpoint_schema();
bool native_checkpoint_present();
uint64_t native_checkpoint_deadline();
// Authenticated, adopted schema4 IDs from this exact native scene. This is
// distinct from a fresh one-player scene and authorizes no new factory call.
bool native_checkpoint_foreign_pair(uint64_t epoch, NativeActorCheckpoint& pair, uint32_t& player_id);
bool restore_native_checkpoint(uint64_t epoch, uint64_t request);
void dispose_native_checkpoint();
} // namespace sporemp
