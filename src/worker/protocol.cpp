#include "protocol.h"
#include <algorithm>
#include <limits>

namespace sporemp::worker {
namespace {
void put(Bytes& bytes, size_t at, uint64_t value, size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) bytes[at + i] = static_cast<uint8_t>(value >> (i * 8));
}
uint64_t get(const uint8_t* bytes, size_t at, size_t count) noexcept {
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) value |= uint64_t(bytes[at + i]) << (i * 8);
    return value;
}
}
Bytes encode(const Message& message) noexcept {
    Bytes bytes{};
    put(bytes, 0, 0x34574d53, 4); // SMW4
    put(bytes, 4, 1, 2); put(bytes, 6, frame_size, 2);
    put(bytes, 8, static_cast<uint32_t>(message.kind), 4);
    put(bytes, 12, static_cast<uint32_t>(message.op), 4);
    std::copy(message.generation.begin(), message.generation.end(), bytes.begin() + 16);
    put(bytes, 32, message.sequence, 8); put(bytes, 40, message.epoch, 8);
    for (size_t i = 0; i < message.values.size(); ++i) put(bytes, 48 + i * 8, message.values[i], 8);
    return bytes;
}
bool decode(const uint8_t* bytes, size_t size, Message& message) noexcept {
    if (!bytes || size != frame_size || get(bytes, 0, 4) != 0x34574d53 ||
        get(bytes, 4, 2) != 1 || get(bytes, 6, 2) != frame_size) return false;
    const auto kind = get(bytes, 8, 4), op = get(bytes, 12, 4);
    if (kind < 1 || kind > 3 || op > static_cast<uint32_t>(Op::import_creation)) return false;
    Message decoded;
    decoded.kind = static_cast<Kind>(kind); decoded.op = static_cast<Op>(op);
    std::copy(bytes + 16, bytes + 32, decoded.generation.begin());
    if (std::all_of(decoded.generation.begin(), decoded.generation.end(), [](uint8_t b) { return b == 0; })) return false;
    decoded.sequence = get(bytes, 32, 8); decoded.epoch = get(bytes, 40, 8);
    if (!decoded.sequence) return false;
    for (size_t i = 0; i < decoded.values.size(); ++i) decoded.values[i] = get(bytes, 48 + i * 8, 8);
    message = decoded;
    return true;
}
bool generation_from_hex(const std::wstring& text, Generation& value) noexcept {
    if (text.size() != 32) return false;
    Generation decoded{};
    for (size_t i = 0; i < text.size(); ++i) {
        auto c = text[i];
        int digit = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1;
        if (digit < 0) return false;
        decoded[i / 2] |= static_cast<uint8_t>(digit << (i % 2 ? 0 : 4));
    }
    if (std::all_of(decoded.begin(), decoded.end(), [](uint8_t b) { return b == 0; })) return false;
    value = decoded; return true;
}
std::wstring generation_hex(const Generation& value) {
    const wchar_t* alphabet = L"0123456789abcdef";
    std::wstring text;
    for (auto byte : value) { text += alphabet[byte >> 4]; text += alphabet[byte & 15]; }
    return text;
}
bool accept_sequence(const Message& message, const Generation& generation, uint64_t& last) noexcept {
    if (message.generation != generation || !message.sequence || last == std::numeric_limits<uint64_t>::max() || message.sequence != last + 1) return false;
    last = message.sequence; return true;
}
const char* op_name(Op op) noexcept {
    constexpr const char* names[] = {"status", "setup", "players", "rewards", "jump", "move", "duel", "retire", "load", "save", "shutdown", "stop", "checkpoint", "restore", "replica", "network_action", "inspect_creation", "import_creation"};
    const auto index = static_cast<size_t>(op); return index < std::size(names) ? names[index] : "invalid";
}
bool valid_creation_inspection(const Message& message) noexcept {
    if (message.kind != Kind::command || message.op != Op::inspect_creation ||
        !message.values[0] || message.values[0] >= UINT32_MAX || message.values[2] >= UINT32_MAX ||
        message.values[1] != 0x2b978c46u) return false;
    return std::all_of(message.values.begin() + 3, message.values.end(), [](uint64_t value) { return value == 0; });
}
bool valid_creation_import(const Message& message) noexcept {
    if (message.kind != Kind::command || message.op != Op::import_creation || message.values[8] || message.values[9]) return false;
    bool nonzero = false;
    for (size_t i = 0; i < 8; ++i) {
        if (message.values[i] > UINT32_MAX) return false;
        nonzero = nonzero || message.values[i] != 0;
    }
    return nonzero;
}
const char* result_name(Result result) noexcept {
    constexpr const char* names[] = {"none", "accepted", "unavailable", "stale", "invalid", "busy", "failed"};
    const auto index = static_cast<size_t>(result); return index < std::size(names) ? names[index] : "invalid";
}
}
