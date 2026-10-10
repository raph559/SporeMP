#pragma once
#include <cstdint>

namespace sporemp::network {
// A native death occurrence, not a client request to kill/revive an actor.
// Pinned native DamageType: melee (2) and starvation (5). The original death
// handler selects the corresponding cinematic; other causes remain unqualified.
inline bool valid_death_reason(uint32_t reason) noexcept {
    return reason == 2 || reason == 5;
}

struct DeathFrame {
    uint64_t occurrence = 0, entity = 0, generation = 0;
    uint32_t reason = 0;

    bool operator==(const DeathFrame& other) const noexcept {
        return occurrence == other.occurrence && entity == other.entity && generation == other.generation &&
               reason == other.reason;
    }
};

inline bool valid_death(const DeathFrame& value) noexcept {
    return value.occurrence && value.entity && value.generation && valid_death_reason(value.reason);
}
} // namespace sporemp::network
