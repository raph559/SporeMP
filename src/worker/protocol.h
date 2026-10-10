#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sporemp::worker {
// Private local control protocol, deliberately separate from future M06 networking.
// Fixed little-endian scalar fields; never serialize a C++ object or native pointer.
constexpr size_t frame_size = 128;
using Bytes = std::array<uint8_t, frame_size>;
using Generation = std::array<uint8_t, 16>;
enum class Kind : uint32_t {
    status = 1,
    command = 2,
    reply = 3
};
enum class Op : uint32_t {
    status,
    setup,
    players,
    rewards,
    jump,
    move,
    duel,
    retire,
    load,
    save,
    shutdown,
    stop,
    checkpoint,
    restore,
    replica,
    network_action,
    inspect_creation,
    import_creation,
    save_probe,
    engage,
    pickup,
    save_fault,
    bind_primary,
    scene_probe,
    export_player,
    fence_inputs,
    export_player_slice,
    import_player,
    export_player_state,
    quiesce,
    quiescence_status,
    resume_world,
    social_probe,
    social_tree_probe,
    nest_probe,
    fossil_approach,
    input,
    click,
    point,
    ui
};
enum class Result : uint64_t {
    none,
    accepted,
    unavailable,
    stale,
    invalid,
    busy,
    failed
};
enum class Phase : uint64_t {
    starting,
    menu,
    loading,
    scene,
    stopping,
    failed
};
// Kind::status only: values[6] is ProjectionState and values[7] is the
// successfully applied network baseline (nonzero only for connected).
// Kind::reply keeps values[6]/[7] as request sequence/Result. Phase and the
// persistence request/state in values[8]/[9] retain their existing meanings.
enum class ProjectionState : uint64_t {
    none = 0,
    waiting = 1,
    connected = 2,
    checkpoint_lease = 3
};

struct Message {
    Kind kind = Kind::status;
    Op op = Op::status;
    Generation generation{};
    uint64_t sequence = 0, epoch = 0;
    std::array<uint64_t, 10> values{};
};

Bytes encode(const Message& message) noexcept;
bool decode(const uint8_t* data, size_t size, Message& message) noexcept;
bool generation_from_hex(const std::wstring& text, Generation& value) noexcept;
std::wstring generation_hex(const Generation& value);
bool accept_sequence(const Message& message, const Generation& generation, uint64_t& last) noexcept;
// Explicit developer inspection of a creature creation key. It can populate
// original-game caches; it is never an import or permission to publish content.
// Values 0..2 are instance/type/group, with all remaining values zero.
bool valid_creation_inspection(const Message& message) noexcept;
// Local setup defaults to the original same-species fixture (mode 0). Mode 1
// explicitly requests experimental foreign-species initialization; native
// admission still requires a fresh one-player scene. Remaining words are zero.
bool valid_setup(const Message& message) noexcept;
// Shared by the two internal network bootstrap paths. Internal messages need
// the same command kind as external worker control. Existing calls retain
// mode 0; the explicit authority configuration may select experimental mode 1.
Message make_setup_command(uint64_t epoch, uint64_t sequence, bool experimental_foreign = false) noexcept;
// Values 0..7 hold eight little-endian SHA-256 uint32 words; 8..9 must be zero.
// Paths and native pointers are never accepted.
bool valid_creation_import(const Message& message) noexcept;
bool valid_player_import(const Message& message) noexcept;
// Developer-only social input probe: owner, bound actor, target native ID,
// ability index (0..87) or UINT32_MAX for start. All other words must be zero.
bool valid_social_probe(const Message& message) noexcept;
bool valid_social_tree_probe(const Message& message) noexcept;
// Explicit native factory investigation: secondary owner and bound actor.
// No coordinates, primary ownership flag, native pointer or extra word.
bool valid_nest_probe(const Message& message) noexcept;
// Developer-only original walking probe: owner, bound actor, current fossil ID.
// It requests no collection or award and accepts no coordinates or pointers.
bool valid_fossil_approach(const Message& message) noexcept;
// One key transition for a player's own game window (GOAL.md, 2026-10-07 amendment).
// values[0] is 1 for down or 2 for up, values[1] one allowed virtual key: W A S D, Space
// or the digit row 1-9. The bridge posts the ordinary window message; nothing else is admitted.
bool valid_input(const Message& message) noexcept;
// One pointer action on an entity a player's own game shows. values[0] is 1 to point at it, 2 to
// left-click it or 3 to right-click it; values[1] is the entity; values[2] is how high on the
// creature's own box to aim, in percent from 5 to 95. The bridge finds where its own window draws
// that point and posts the ordinary mouse messages there.
bool valid_click(const Message& message) noexcept;
// One pointer action at a place in a player's own game window, for the original's interface (a button
// of a cinematic, the ability bar). values[0] as for a click; values[1], values[2] are client pixels.
bool valid_point(const Message& message) noexcept;
// Read the original's interface in a player's own game, changing nothing. values[0] is 1 for the
// visible windows that carry text or act as buttons, or 2 for the window under the place values[1],
// values[2] in client pixels.
bool valid_ui(const Message& message) noexcept;
const char* op_name(Op op) noexcept;
const char* result_name(Result result) noexcept;
} // namespace sporemp::worker
