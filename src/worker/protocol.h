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
enum class Kind : uint32_t { status = 1, command = 2, reply = 3 };
enum class Op : uint32_t { status, setup, players, rewards, jump, move, duel, retire,
    load, save, shutdown, stop, checkpoint, restore, replica, network_action, inspect_creation, import_creation };
enum class Result : uint64_t { none, accepted, unavailable, stale, invalid, busy, failed };
enum class Phase : uint64_t { starting, menu, loading, scene, stopping, failed };
// Kind::status only: values[6] is ProjectionState and values[7] is the
// successfully applied network baseline (nonzero only for connected).
// Kind::reply keeps values[6]/[7] as request sequence/Result. Phase and the
// persistence request/state in values[8]/[9] retain their existing meanings.
enum class ProjectionState : uint64_t { none = 0, waiting = 1, connected = 2 };
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
// Values 0..7 hold eight little-endian SHA-256 uint32 words; 8..9 must be zero.
// Paths and native pointers are never accepted.
bool valid_creation_import(const Message& message) noexcept;
const char* op_name(Op op) noexcept;
const char* result_name(Result result) noexcept;
}
