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
    load, save, shutdown, stop, checkpoint, restore, replica };
enum class Result : uint64_t { none, accepted, unavailable, stale, invalid, busy, failed };
enum class Phase : uint64_t { starting, menu, loading, scene, stopping, failed };
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
const char* op_name(Op op) noexcept;
const char* result_name(Result result) noexcept;
}
