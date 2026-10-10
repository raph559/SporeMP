#pragma once
#include <array>
#include <cstdint>

namespace sporemp::network {
// One complete observed Creature category-8 snapshot: the first-kill events of the foreign second player, in the
// order the original keeps them. Up to bridge 0.0.225 it held one event at most (trial315: the second species
// that player hunted was refused). Other categories and other events are not admitted. Native allocations,
// links and resource pointers stay local.
constexpr uint32_t history_category = 8;
// 72 header bytes, 80 fixed frame bytes and 60 per entry fill 632 of a packet's 688 bytes.
constexpr uint32_t history_capacity = 8;

struct HistoryEntry {
    std::array<uint32_t, 3> header{}; // Entry date[2], then original header kind.
    std::array<uint32_t, 12> data{};  // Complete BA57A0/BA5F10 scalar payload.

    bool operator==(const HistoryEntry& other) const noexcept {
        return header == other.header && data == other.data;
    }
};

struct HistoryState {
    std::array<uint32_t, 2> date{}; // Original root +30/+34 packed creation date.
    uint32_t count = 0;
    std::array<HistoryEntry, history_capacity> entries{};

    bool operator==(const HistoryState& other) const noexcept {
        return date == other.date && count == other.count && entries == other.entries;
    }
};

struct HistoryFrame {
    uint64_t entity = 0, generation = 0, revision = 0;
    HistoryState state{};
};

inline bool valid_history_entry(const HistoryEntry& entry) noexcept {
    // BA57A0 creates kind 0 through 7EC230 and writes key type/instance/group,
    // marker 1, event and relation. This admitted branch requires event 5 and
    // relation 1; BA5F10 supplies classification 1..3 and a float.
    // Preserve all remaining observed values, including the original dates.
    const auto& data = entry.data;
    return !entry.header[2] && data[1] && data[2] && data[3] && data[4] == 5 && data[6] == 1 && data[7] >= 1 &&
           data[7] <= 3 && data[8] == 1 && data[10] <= 1 && (data[11] & 0x7f800000u) != 0x7f800000u;
}

inline bool valid_history(const HistoryState& state) noexcept {
    if (state.count > history_capacity) {
        return false;
    }
    for (uint32_t i = 0; i < history_capacity; ++i) {
        if (i < state.count ? !valid_history_entry(state.entries[i]) : !(state.entries[i] == HistoryEntry{})) {
            return false;
        }
    }
    return true;
}

// The only way a history changes: whole entries added after the ones it had.
inline bool history_extends(const HistoryState& current, const HistoryState& next) noexcept {
    if (current.date != next.date || current.count > next.count || next.count > history_capacity) {
        return false;
    }
    for (uint32_t i = 0; i < current.count; ++i) {
        if (!(current.entries[i] == next.entries[i])) {
            return false;
        }
    }
    return true;
}

inline bool valid_history_frame(const HistoryFrame& frame) noexcept {
    return frame.entity && frame.generation && frame.revision && valid_history(frame.state);
}
} // namespace sporemp::network
