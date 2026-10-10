#pragma once
#include "../worker/protocol.h"
#include "../network/location_id.h"
#include <Windows.h>
#include <cstdint>

namespace sporemp {
using NativePersistenceEvent = void (*)(const char*, const char*);
enum class NativePersistenceState : uint64_t {
    idle = 0,
    save_pending = 1,
    saving = 2,
    saved = 3,
    load_pending = 4,
    loading = 5,
    loaded = 6,
    failed = 7,
    unavailable = 8
};

struct NativePersistenceSnapshot {
    uint64_t request = 0;
    NativePersistenceState state = NativePersistenceState::unavailable;
    bool native_save_result_valid = false;
    bool native_save_result = false;
};

// Developer controlled Satiria Creature fixture only. Initialize after the
// executable/content/loader guard and actor observation hooks. All entry points
// are restricted to the SDK app-update thread; no I/O thread calls native code.
bool initialize_native_persistence(uintptr_t base, uintptr_t end, DWORD thread, NativePersistenceEvent sink);
worker::Result native_persistence_command(const worker::Message& request, const worker::Message& status);
// Invoke before consuming a new IPC request, so acknowledgement precedes work.
void update_native_persistence(const worker::Message& status);
void annotate_native_persistence(worker::Message& status);
NativePersistenceSnapshot native_persistence_snapshot();
// Bounded Creature home-surface admission: map the selected original save's
// SDK PlanetID and active terrain resource instance to coordinator scalars.
bool native_persistence_matches_home_location(const network::LocationId& expected);
// Developer-only observation of the original serializer's complete primary
// player/avatar graph. Writes a new private run artifact; never imports it or
// selects it as a checkpoint. Must run on the native engine thread after load.
// The optional slice writes empty neighbor/herd references in the artifact
// only. It is incomplete until original destination registration/rebinding;
// never import it directly or qualify it as a transfer from export success.
bool export_native_player_graph(uint64_t request, bool project_scene_references = false, bool include_stage = false);

struct NativePlayerGraphSelection {
    uint64_t epoch = 0, canonical_player = 0, pause_request = 0;
    uint32_t native_slot = 0, expected_unique_game = 0, canonical_home = 0;
};

// Schema 3, with exact selected identity supplied by the transfer controller.
// The canonical home follows the player when the selected actor is a visitor.
bool export_native_selected_player_state(uint64_t request, const NativePlayerGraphSelection& selection);
bool native_persistence_busy();
void dispose_native_persistence();
} // namespace sporemp
