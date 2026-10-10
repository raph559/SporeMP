#include "protocol.h"
#include <algorithm>
#include <limits>

namespace sporemp::worker {
namespace {
constexpr uint32_t frame_magic = 0x34574d53; // SMW4
// The resource type of a creation: crt_type in src/bridge/native_content.cpp.
constexpr uint64_t creation_type = 0x2b978c46u;
// A social probe names one of the abilities 0..87 (protocol.h).
constexpr uint64_t social_ability_count = 88;

void put(Bytes& bytes, size_t at, uint64_t value, size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) {
        bytes[at + i] = static_cast<uint8_t>(value >> (i * 8));
    }
}

uint64_t get(const uint8_t* bytes, size_t at, size_t count) noexcept {
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) {
        value |= uint64_t(bytes[at + i]) << (i * 8);
    }
    return value;
}

bool all_zero(const Generation& generation) noexcept {
    return std::all_of(generation.begin(), generation.end(), [](uint8_t byte) { return byte == 0; });
}

// Whether every value of a message from `first` on is zero, as a command that does not use them requires.
bool values_zero_from(const Message& message, size_t first) noexcept {
    return std::all_of(message.values.begin() + first, message.values.end(), [](uint64_t value) { return value == 0; });
}
} // namespace

Bytes encode(const Message& message) noexcept {
    Bytes bytes{};
    put(bytes, 0, frame_magic, 4);
    put(bytes, 4, 1, 2);
    put(bytes, 6, frame_size, 2);
    put(bytes, 8, static_cast<uint32_t>(message.kind), 4);
    put(bytes, 12, static_cast<uint32_t>(message.op), 4);
    std::copy(message.generation.begin(), message.generation.end(), bytes.begin() + 16);
    put(bytes, 32, message.sequence, 8);
    put(bytes, 40, message.epoch, 8);
    for (size_t i = 0; i < message.values.size(); ++i) {
        put(bytes, 48 + i * 8, message.values[i], 8);
    }
    return bytes;
}

bool decode(const uint8_t* data, size_t size, Message& message) noexcept {
    if (!data || size != frame_size || get(data, 0, 4) != frame_magic || get(data, 4, 2) != 1 ||
        get(data, 6, 2) != frame_size) {
        return false;
    }
    const auto kind = get(data, 8, 4), op = get(data, 12, 4);
    if (kind < 1 || kind > 3 || op > static_cast<uint32_t>(Op::ui)) {
        return false;
    }
    Message decoded;
    decoded.kind = static_cast<Kind>(kind);
    decoded.op = static_cast<Op>(op);
    std::copy(data + 16, data + 32, decoded.generation.begin());
    if (all_zero(decoded.generation)) {
        return false;
    }
    decoded.sequence = get(data, 32, 8);
    decoded.epoch = get(data, 40, 8);
    if (!decoded.sequence) {
        return false;
    }
    for (size_t i = 0; i < decoded.values.size(); ++i) {
        decoded.values[i] = get(data, 48 + i * 8, 8);
    }
    message = decoded;
    return true;
}

bool generation_from_hex(const std::wstring& text, Generation& value) noexcept {
    if (text.size() != 32) {
        return false;
    }
    Generation decoded{};
    for (size_t i = 0; i < text.size(); ++i) {
        const auto character = text[i];
        const int digit = character >= L'0' && character <= L'9'   ? character - L'0'
                          : character >= L'a' && character <= L'f' ? character - L'a' + 10
                                                                   : -1;
        if (digit < 0) {
            return false;
        }
        decoded[i / 2] |= static_cast<uint8_t>(digit << (i % 2 ? 0 : 4));
    }
    if (all_zero(decoded)) {
        return false;
    }
    value = decoded;
    return true;
}

std::wstring generation_hex(const Generation& value) {
    const wchar_t* alphabet = L"0123456789abcdef";
    std::wstring text;
    for (auto byte : value) {
        text += alphabet[byte >> 4];
        text += alphabet[byte & 15];
    }
    return text;
}

bool accept_sequence(const Message& message, const Generation& generation, uint64_t& last) noexcept {
    if (message.generation != generation || !message.sequence || last == std::numeric_limits<uint64_t>::max() ||
        message.sequence != last + 1) {
        return false;
    }
    last = message.sequence;
    return true;
}

const char* op_name(Op op) noexcept {
    constexpr const char* names[] = {"status",
                                     "setup",
                                     "players",
                                     "rewards",
                                     "jump",
                                     "move",
                                     "duel",
                                     "retire",
                                     "load",
                                     "save",
                                     "shutdown",
                                     "stop",
                                     "checkpoint",
                                     "restore",
                                     "replica",
                                     "network_action",
                                     "inspect_creation",
                                     "import_creation",
                                     "save_probe",
                                     "engage",
                                     "pickup",
                                     "save_fault",
                                     "bind_primary",
                                     "scene_probe",
                                     "export_player",
                                     "fence_inputs",
                                     "export_player_slice",
                                     "import_player",
                                     "export_player_state",
                                     "quiesce",
                                     "quiescence_status",
                                     "resume_world",
                                     "social_probe",
                                     "social_tree_probe",
                                     "nest_probe",
                                     "fossil_approach",
                                     "input",
                                     "click",
                                     "point",
                                     "ui"};
    const auto index = static_cast<size_t>(op);
    return index < std::size(names) ? names[index] : "invalid";
}

bool valid_setup(const Message& message) noexcept {
    if (message.kind != Kind::command || message.op != Op::setup || !message.epoch || message.values[0] > 1) {
        return false;
    }
    return values_zero_from(message, 1);
}

Message make_setup_command(uint64_t epoch, uint64_t sequence, bool experimental_foreign) noexcept {
    Message message;
    message.kind = Kind::command;
    message.op = Op::setup;
    message.epoch = epoch;
    message.sequence = sequence;
    message.values[0] = experimental_foreign ? 1 : 0;
    return message;
}

bool valid_creation_inspection(const Message& message) noexcept {
    if (message.kind != Kind::command || message.op != Op::inspect_creation || !message.values[0] ||
        message.values[0] >= UINT32_MAX || message.values[2] >= UINT32_MAX || message.values[1] != creation_type) {
        return false;
    }
    return values_zero_from(message, 3);
}

bool valid_creation_import(const Message& message) noexcept {
    if (message.kind != Kind::command || message.op != Op::import_creation || message.values[8] || message.values[9]) {
        return false;
    }
    bool nonzero = false;
    for (size_t i = 0; i < 8; ++i) {
        if (message.values[i] > UINT32_MAX) {
            return false;
        }
        nonzero = nonzero || message.values[i] != 0;
    }
    return nonzero;
}

bool valid_player_import(const Message& message) noexcept {
    if (message.op != Op::import_player) {
        return false;
    }
    auto digest_request = message;
    digest_request.op = Op::import_creation;
    return valid_creation_import(digest_request);
}

bool valid_social_probe(const Message& message) noexcept {
    if (message.kind != Kind::command || message.op != Op::social_probe || !message.epoch ||
        (message.values[0] != 1 && message.values[0] != 2) || !message.values[1] || message.values[2] >= UINT32_MAX ||
        (message.values[3] >= social_ability_count && message.values[3] != UINT32_MAX)) {
        return false;
    }
    return values_zero_from(message, 4);
}

bool valid_social_tree_probe(const Message& message) noexcept {
    if (message.kind != Kind::command || message.op != Op::social_tree_probe || !message.epoch ||
        message.values[0] != 2 || !message.values[1] || message.values[2] > 3) {
        return false;
    }
    return values_zero_from(message, 3);
}

bool valid_nest_probe(const Message& message) noexcept {
    if (message.kind != Kind::command || message.op != Op::nest_probe || !message.epoch || message.values[0] != 2 ||
        !message.values[1]) {
        return false;
    }
    return values_zero_from(message, 2);
}

bool valid_fossil_approach(const Message& message) noexcept {
    if (message.kind != Kind::command || message.op != Op::fossil_approach || !message.epoch ||
        (message.values[0] != 1 && message.values[0] != 2) || !message.values[1] || message.values[2] >= UINT32_MAX) {
        return false;
    }
    return values_zero_from(message, 3);
}

bool valid_input(const Message& message) noexcept {
    const auto key = message.values[1];
    if (message.kind != Kind::command || message.op != Op::input || !message.epoch ||
        (message.values[0] != 1 && message.values[0] != 2) ||
        !(key == 'W' || key == 'A' || key == 'S' || key == 'D' || key == 0x20 || (key >= '1' && key <= '9'))) {
        return false;
    }
    return values_zero_from(message, 2);
}

bool valid_click(const Message& message) noexcept {
    if (message.kind != Kind::command || message.op != Op::click || !message.epoch || message.values[0] < 1 ||
        message.values[0] > 3 || !message.values[1] || message.values[2] < 5 || message.values[2] > 95) {
        return false;
    }
    return values_zero_from(message, 3);
}

bool valid_point(const Message& message) noexcept {
    if (message.kind != Kind::command || message.op != Op::point || !message.epoch || message.values[0] < 1 ||
        message.values[0] > 3 || message.values[1] >= 8192 || message.values[2] >= 8192) {
        return false;
    }
    return values_zero_from(message, 3);
}

bool valid_ui(const Message& message) noexcept {
    if (message.kind != Kind::command || message.op != Op::ui || !message.epoch ||
        (message.values[0] != 1 && message.values[0] != 2) || message.values[1] >= 8192 || message.values[2] >= 8192 ||
        (message.values[0] == 1 && (message.values[1] || message.values[2]))) {
        return false;
    }
    return values_zero_from(message, 3);
}

const char* result_name(Result result) noexcept {
    constexpr const char* names[] = {"none", "accepted", "unavailable", "stale", "invalid", "busy", "failed"};
    const auto index = static_cast<size_t>(result);
    return index < std::size(names) ? names[index] : "invalid";
}
} // namespace sporemp::worker
