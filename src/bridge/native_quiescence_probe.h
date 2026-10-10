#pragma once
#include <Windows.h>
#include "../worker/protocol.h"
#include <cstdint>

namespace sporemp {
enum class NativeQuiescencePhase : uint32_t {
    idle,
    settling,
    observing,
    measured,
    rejected,
    released
};

struct NativeQuiescenceStatus {
    NativeQuiescencePhase phase = NativeQuiescencePhase::idle;
    bool owns_pause = false, ready = false;
    uint64_t request = 0, epoch = 0, input_epoch = 0, elapsed_ms = 0;
    uint64_t app_begin = 0, app_end = 0, ai_begin = 0, ai_end = 0;
    uint64_t zero_native_deltas = 0, nonzero_native_deltas = 0;
    uint32_t player_native_id = 0, avatar_native_id = 0, nouns = 0;
    int32_t gameplay_count = 0, pause_flags = 0;
};

using NativeQuiescenceEvent = void (*)(const char*, const char*);
// Supervised worker only. Existing full executable/content guards must have
// passed before engine-thread initialization. Developer requests require a
// disconnected worker; authenticated transfers use the separate entry below.
bool initialize_native_quiescence_probe(uintptr_t image_base, uintptr_t end, DWORD thread,
                                        NativeQuiescenceEvent callback);
// Accepted acknowledges one original Pause call, not a stable world or a lease.
worker::Result request_native_quiescence_pause(uint64_t request, uint64_t epoch, uint64_t app_ticks);
// The active authenticated authority maintenance command must match request
// and epoch. This path may measure both native player slots.
worker::Result request_native_transfer_quiescence(uint64_t request, uint64_t epoch, uint64_t app_ticks);
// After an authorized import/discard changes native state under our same pause,
// discard the old observation and require a new complete stability window.
bool restart_native_transfer_observation(uint64_t request, uint64_t epoch, uint64_t app_ticks);
// request is the original held pause request token, not the resume IPC command ID.
worker::Result request_native_quiescence_resume(uint64_t request, uint64_t epoch);
// Call on each existing worker AppUpdate, using its actual app_ticks counter.
// This never blocks/sleeps and uses native_actor_worker_status().values[2]
// (the original NPC/avatar hook counter), never a replacement AI counter.
void update_native_quiescence_probe(uint64_t app_ticks);
NativeQuiescenceStatus native_quiescence_probe_status(uint64_t epoch);
bool native_quiescence_probe_ready(uint64_t epoch);
// Caller should explicitly resume before scene exit. This cleanup releases only
// a proven owned increment in the same epoch; it never drains native counters.
void dispose_native_quiescence_probe();
} // namespace sporemp
