#pragma once
#include <cmath>
#include <cstdint>
namespace Simulator { class cCreatureAnimal; }
namespace sporemp {
// Pure admission shared with the HOST fixture. Native13 observes a retained
// corpse at health 0 with combatant state 0 after original pool reuse. D85B80
// selects the already-dead pose from mbDead; combatant state is independent.
inline bool native_life_state_admitted(bool dead, bool native_dead, float health,
                                       uint32_t combatant_state) {
    if (!std::isfinite(health) || native_dead != dead ||
        (combatant_state != 0 && combatant_state != 2)) return false;
    return dead ? health == 0 : health > 0 && combatant_state == 0;
}
// Engine thread, after scene/incarnation admission, inside replica apply_state.
// Call once for a fresh dead baseline, a life transition, or replacement of the
// admitted animated model. The scene binding owns that deduplication. No pointer
// is retained here. This projects the original admitted-corpse pose, not the
// cause-specific lethal reaction, gameplay death, or game-over interface.
bool native_life_present(Simulator::cCreatureAnimal* animal, bool dead,
                         uint64_t remote_entity, uint64_t generation);
// Client engine-thread presentation readback; outputs are cleared on refusal.
bool native_life_read_animation(Simulator::cCreatureAnimal* animal,
                                uint32_t& animation, int& index);
}
