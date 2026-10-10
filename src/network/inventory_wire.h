#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <vector>

namespace sporemp::network {
constexpr uint32_t inventory_capacity = 1024, inventory_chunk_items = 24;

struct InventoryItem {
    uint32_t instance = 0, group = 0, status = 0, index = UINT32_MAX;

    uint64_t key() const noexcept {
        return (uint64_t(group) << 32) | instance;
    }

    bool operator==(const InventoryItem& b) const noexcept {
        return instance == b.instance && group == b.group && status == b.status && index == b.index;
    }
};

// Complete observed native statuses plus the original unlocked-list order.
// Item definitions remain the pinned local content, never peer-supplied code.
struct InventoryState {
    uint32_t points = 0, group = 0;
    std::vector<InventoryItem> items;

    bool operator==(const InventoryState& b) const noexcept {
        return points == b.points && group == b.group && items == b.items;
    }
};

struct InventoryFrame {
    uint64_t entity = 0, generation = 0, revision = 0;
    uint32_t total = 0, offset = 0, count = 0, points = 0, group = 0;
    std::array<InventoryItem, inventory_chunk_items> items{};
};

inline bool valid_inventory_item(const InventoryItem& item, uint32_t total) noexcept {
    return item.instance && item.group && item.status <= 7 &&
           ((item.status & 1) ? item.index < total : item.index == UINT32_MAX);
}

inline bool valid_inventory_frame(const InventoryFrame& f) noexcept {
    if (!f.entity || !f.generation || !f.revision || !f.total || f.total > inventory_capacity || f.offset >= f.total ||
        f.offset % inventory_chunk_items || f.points > INT32_MAX ||
        f.count != std::min(inventory_chunk_items, f.total - f.offset)) {
        return false;
    }
    for (uint32_t i = 0; i < f.count; ++i) {
        if (!valid_inventory_item(f.items[i], f.total)) {
            return false;
        }
    }
    for (uint32_t i = f.count; i < inventory_chunk_items; ++i) {
        if (!(f.items[i] == InventoryItem{})) {
            return false;
        }
    }
    return true;
}

inline bool valid_inventory(const InventoryState& state) {
    if (state.items.empty() || state.items.size() > inventory_capacity || state.points > INT32_MAX) {
        return false;
    }
    std::set<uint32_t> order;
    uint64_t previous = 0;
    for (const auto& item : state.items) {
        if (!valid_inventory_item(item, static_cast<uint32_t>(state.items.size())) || item.key() <= previous) {
            return false;
        }
        previous = item.key();
        if (item.status & 1) {
            if (!order.insert(item.index).second) {
                return false;
            }
        }
    }
    return order.empty() || (*order.begin() == 0 && *order.rbegin() == order.size() - 1);
}
enum class InventoryReceipt {
    ignored,
    partial,
    complete,
    invalid
};

class InventoryAssembly {
    uint64_t entity_ = 0, generation_ = 0, revision_ = 0, committed_ = 0;
    uint32_t total_ = 0;
    InventoryState pending_;

public:
    void reset() {
        *this = {};
    }

    InventoryReceipt receive(const InventoryFrame& f, InventoryState& completed) {
        if (!valid_inventory_frame(f)) {
            return InventoryReceipt::invalid;
        }
        if (entity_ && (entity_ != f.entity || generation_ != f.generation)) {
            return InventoryReceipt::invalid;
        }
        if (f.revision <= committed_ || f.revision < revision_) {
            return InventoryReceipt::ignored;
        }
        if (f.revision != revision_) {
            // A newly ready subscriber can join midway through a publication.
            // Wait for its next complete snapshot instead of admitting a tail.
            if (f.offset) {
                return InventoryReceipt::ignored;
            }
            entity_ = f.entity;
            generation_ = f.generation;
            revision_ = f.revision;
            total_ = f.total;
            pending_ = {f.points, f.group, {}};
            pending_.items.reserve(f.total);
        }
        if (f.total != total_ || f.points != pending_.points || f.group != pending_.group ||
            f.offset != pending_.items.size()) {
            return InventoryReceipt::invalid;
        }
        pending_.items.insert(pending_.items.end(), f.items.begin(), f.items.begin() + f.count);
        if (pending_.items.size() != total_) {
            return InventoryReceipt::partial;
        }
        if (!valid_inventory(pending_)) {
            return InventoryReceipt::invalid;
        }
        committed_ = revision_;
        completed = std::move(pending_);
        return InventoryReceipt::complete;
    }
};
} // namespace sporemp::network
