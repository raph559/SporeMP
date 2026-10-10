#pragma once
#include <array>
#include <cstdint>

namespace sporemp::network {
// One complete observed snapshot of owner B's private species relationships: the
// original first map only. Each value is the original five-word entry (relation,
// native count, befriended, killed, native value) under its species key as the
// original stores it. Native nodes, links and allocations stay local, and the
// snapshot carries state, never a relationship action to replay.
constexpr uint32_t relationship_capacity = 8;

struct RelationshipEntry {
    std::array<uint32_t, 3> key{};
    std::array<uint32_t, 5> words{};

    bool operator==(const RelationshipEntry& other) const noexcept {
        return key == other.key && words == other.words;
    }
};

struct RelationshipFrame {
    uint64_t entity = 0, generation = 0, revision = 0;
    uint32_t count = 0;
    std::array<RelationshipEntry, relationship_capacity> entries{};
};

// Entries are strictly ascending by key, so a snapshot has one canonical form and
// no duplicate species; everything past `count` is zero.
inline bool valid_relationships(uint32_t count,
                                const std::array<RelationshipEntry, relationship_capacity>& entries) noexcept {
    if (count > relationship_capacity) {
        return false;
    }
    for (uint32_t i = 0; i < relationship_capacity; ++i) {
        const auto& entry = entries[i];
        if (i >= count) {
            if (!(entry == RelationshipEntry{})) {
                return false;
            }
            continue;
        }
        if (entry.key == std::array<uint32_t, 3>{}) {
            return false;
        }
        if (i && !(entries[i - 1].key < entry.key)) {
            return false;
        }
    }
    return true;
}

inline bool valid_relationship_frame(const RelationshipFrame& frame) noexcept {
    return frame.entity && frame.generation && frame.revision && valid_relationships(frame.count, frame.entries);
}

inline bool same_relationships(const RelationshipFrame& a, const RelationshipFrame& b) noexcept {
    return a.count == b.count && a.entries == b.entries;
}

// A client can add or rewrite an entry but has no original call to remove one, so
// a snapshot is applicable only when it still names every species the client holds.
inline bool relationships_cover(const RelationshipFrame& snapshot, const RelationshipFrame& local) noexcept {
    for (uint32_t i = 0; i < local.count; ++i) {
        bool found = false;
        for (uint32_t j = 0; j < snapshot.count && !found; ++j) {
            found = snapshot.entries[j].key == local.entries[i].key;
        }
        if (!found) {
            return false;
        }
    }
    return true;
}
} // namespace sporemp::network
