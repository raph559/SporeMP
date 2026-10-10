#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>

namespace Simulator {
class ISerializerWriteStream;
}

namespace sporemp {
// Developer experiment only. The host must obtain these values from the strict
// closed schema-2 slice validator, including its exact three-object closure.
// This approval is bound to the complete artifact digest, never to a filename.
// It is not a network protocol and does not authorize a location transfer.
struct NativePlayerTransferFixture {
    std::array<char, 65> sha256{};
    uint32_t schema = 2;
    uint32_t bytes = 0, serialization_version = 0;
    uint32_t source_player = 0, source_avatar = 0, source_unique_game = 0, source_home = 0;
    uint32_t species_instance = 0, species_type = 0, species_group = 0;
    uint32_t age = 0, animal_archetype = 0, herd_archetype = 0, herd_archetype_group = 0;
    int32_t herd_generation = 0;
    uint32_t herd_personality = 0;
    std::array<uint32_t, 4> progression_bits{}; // goal, total, social, combat
    uint32_t status_count = 0, unlocked_count = 0, unlock_points = 0;
    // Schema 3 only: brain, scale bits, flaps, mode, DNA bits, deprecated
    // ability low/high, and three learned-ability words. Read from the native
    // stage record and its exact source export witness by the closed validator.
    std::array<uint32_t, 10> stage_persistent{};
    // item instance, item group, status; sorted by group then instance.
    std::array<std::array<uint32_t, 3>, 1024> statuses{};
    // Native unlock-list order, item instance then group.
    std::array<std::array<uint32_t, 2>, 1024> unlocked{};
};

struct NativePlayerTransferProbeResult {
    bool imported = false;
    uint32_t player = 0, avatar = 0, herd = 0;
    bool has_stage = false;
    bool restored_primary = false;
    bool restored_secondary = false;
    // NativeAwardCheckpoint encoding, obtained from the actual original stage
    // reader including its transient defaults. No native pointers leave here.
    std::array<uint32_t, 20> stage_words{};
};

using NativePlayerTransferProbeEvent = void (*)(const char*, const char*);
bool initialize_native_player_transfer_probe(uintptr_t image_base, uintptr_t image_end, DWORD engine_thread,
                                             NativePlayerTransferProbeEvent callback);
// Optional source-stage experiment. The caller owns an already opened native
// stream/record and its close/release. Writes only original persistent Creature
// stage fields; it adds no key or COM object to the current slice contract.
bool write_native_primary_transfer_stage(uint64_t request, Simulator::ISerializerWriteStream* stream);
bool write_native_selected_transfer_stage(uint64_t request, uint64_t epoch, uint64_t pause_request,
                                          uint32_t native_slot, uint32_t player_id, uint32_t avatar_id,
                                          Simulator::ISerializerWriteStream* stream);
// Reads diagnostics/player-import.expected. Its eight digest words follow the
// existing worker SHA convention: each word represents four little-endian
// digest bytes. Words 8 and 9 must be zero. The sidecar is generated only by
// the strict closed-slice validator; it binds every expected value to the DB.
bool load_native_player_transfer_fixture(const std::array<uint64_t, 10>& sha_words,
                                         NativePlayerTransferFixture& approved);
// Reads only diagnostics/player-import.db, with a non-share-write handle held
// throughout. A partial native mutation terminates this disposable worker;
// no partial graph is allowed to reach another native tick.
NativePlayerTransferProbeResult import_native_player_transfer_probe(uint64_t request, uint64_t epoch,
                                                                    const NativePlayerTransferFixture& approved);

// Supplied only by the authenticated transfer controller after its durable
// admission checks. This bridge additionally checks the live held pause token,
// primary native identity, queue fence and approved artifact digest.
struct NativePlayerTransferAdmission {
    uint64_t epoch = 0, pause_request = 0, canonical_player = 0;
    uint32_t primary_player = 0, primary_avatar = 0, primary_unique_game = 0;
};

NativePlayerTransferProbeResult import_native_player_transfer(uint64_t request,
                                                              const NativePlayerTransferAdmission& admission,
                                                              const NativePlayerTransferFixture& approved);
// Return to the player's original location: updates the existing primary and
// applies the native-read stage. No player/animal/collection/herd is created.
NativePlayerTransferProbeResult restore_native_primary_transfer(uint64_t request,
                                                                const NativePlayerTransferAdmission& admission,
                                                                const NativePlayerTransferFixture& approved);
// Overlays a selected already restored secondary. No noun is created and the
// current primary, secondary IDs, native model/herd and bridge bindings remain.
// The exact canonical home comes from the authenticated artifact admission.
// Applies the original-read stage to the retained secondary award context.
NativePlayerTransferProbeResult restore_native_secondary_transfer(uint64_t request,
                                                                  const NativePlayerTransferAdmission& admission,
                                                                  const NativePlayerTransferFixture& approved,
                                                                  uint32_t player_id, uint32_t avatar_id,
                                                                  uint32_t herd_id, uint32_t canonical_home);
// Only after the exact successfully imported secondary's verified native
// retirement. Requires all three nouns gone and the unchanged primary ready.
bool reset_native_player_transfer_after_retirement(uint64_t epoch, uint32_t player_id, uint32_t avatar_id,
                                                   uint32_t herd_id);
} // namespace sporemp
