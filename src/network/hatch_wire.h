#pragma once
#include <cstdint>

namespace sporemp::network {
// Original hatch lesson presentation and one owner-selected ability. No native
// pointers, behavior state, timing, growth values or replacement decisions.
struct HatchFrame {
    uint64_t entity = 0, generation = 0, serial = 0;
    uint32_t active = 0, hint = 0, ability = UINT32_MAX, mode = 0, choice = UINT32_MAX;

    bool operator==(const HatchFrame& b) const noexcept {
        return entity == b.entity && generation == b.generation && serial == b.serial && active == b.active &&
               hint == b.hint && ability == b.ability && mode == b.mode && choice == b.choice;
    }
};

inline bool valid_hatch(const HatchFrame& f, bool input = false) noexcept {
    if (!f.entity || !f.generation || !f.serial || f.active > 1 || f.mode > 1) {
        return false;
    }
    if (input && (!f.active || f.choice >= 88)) {
        return false;
    }
    if (!input && f.choice != UINT32_MAX) {
        return false;
    }
    return f.active ? f.hint && f.ability < 88 : !f.hint && f.ability == UINT32_MAX && !f.mode;
}

inline bool same_hatch_offer(HatchFrame a, HatchFrame b) noexcept {
    a.choice = b.choice = UINT32_MAX;
    return a == b;
}
} // namespace sporemp::network
