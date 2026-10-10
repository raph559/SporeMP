#pragma once
#include <Windows.h>
#include <cstdint>
#include "native_global_admission.h"

namespace sporemp {
enum class NativeGlobalObservationPhase : uint32_t {
    before_load,
    scene_ready,
    before_save,
    after_save,
    checkpoint,
    interval,
    end
};
using NativeGlobalObserverEvent = void (*)(const char*, const char*);
// Passive diagnostics for the pinned executable only. The caller must first
// pass the existing executable/content/loader guards, then initialize on the
// engine thread before loading the observed scene. Never call under loader lock.
bool initialize_native_global_observer(uintptr_t image_base, uintptr_t end, DWORD thread,
                                       NativeGlobalObserverEvent callback);
// Only a verified isolated, supervised multiplayer process may arm fatal
// producer rejection. Call before original Load, after inspecting role/context.
// No local config field grants production: install only a current authenticated
// preload response. live() must read the exact preload connection's thread-safe
// liveness snapshot without I/O; it is queried even inside synchronous Load.
bool arm_native_global_authority(const NativeGlobalAdmissionContext& context, bool (*live)());
// Verify the supervised disposable process and reject producers until a grant
// is installed. This precedes the first original startup callback.
bool prepare_native_global_admission(const NativeGlobalAdmissionContext& context);
bool arm_native_global_reader(const NativeGlobalAdmissionContext& context, bool (*live)());
bool install_native_global_grant(const NativeGlobalAdmissionGrant& grant);
void revoke_native_global_grant(const char* reason);
bool native_global_grant_ready();
bool native_global_guard_armed();
// No gameplay methods are invoked. This drains bounded callback samples and
// emits counters/ticker census at ten-second intervals. Epoch is the most recent
// AppUpdate context, and is explicitly labelled as such in callback samples.
void update_native_global_observer(uint64_t app_ticks, uint64_t epoch);
// Full normalized snapshots cover the already-cached planets of starter/saved
// stars plus Sol/temp/scenario. This never loads missing records or claims to
// snapshot every galaxy record. Returns false if any bounded census is partial.
bool mark_native_global_observer(uint64_t request, uint64_t epoch, NativeGlobalObservationPhase phase);
// Detach on the engine thread after the observed window. A failed detach leaves
// the observer attached and returns false; the caller must not unload its DLL.
bool dispose_native_global_observer();
} // namespace sporemp
