#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace sporemp {
// Local harness authority, not network authentication. No native addresses enter a command.
enum class ActorVerb : uint32_t {
    move,
    jump,
    attack,
    stop,
    retire,
    approach,
    engage,
    pickup,
    social,
    combat,
    steer,
    steer_stop,
    collect,
    forage
};

// Shared admission for the two-owner foreign-species experiment. Native
// identity, species and progress checks still run immediately before dispatch.
constexpr bool foreign_creature_input(uint64_t owner, ActorVerb verb, int argument) noexcept {
    if (owner != 1 && owner != 2) {
        return false;
    }
    switch (verb) {
    case ActorVerb::move:
    case ActorVerb::jump:
    case ActorVerb::stop:
    case ActorVerb::steer:
    case ActorVerb::steer_stop:
    case ActorVerb::approach:
        return true;
    // Any ability slot may be asked for as an attack. Whether that slot holds one of the creature's own
    // attacks (Bite, Charge, Spit, Strike) is decided against the native creature before dispatch.
    case ActorVerb::combat:
        return argument >= 0 && argument < 88;
    // Feeding carries no argument. What may be eaten, and by whom, is decided against the native creatures.
    case ActorVerb::pickup:
        return argument == 0;
    // Plant food is named by the original's own number for the fruit. Whether that creature can eat it is the
    // original's decision.
    case ActorVerb::forage:
        return argument > 0;
    default:
        return false;
    }
}
// Where a client sends an ability its own creature's bar or shortcut dispatches. Pinned cCreatureAbility
// values: Dance 34 to Sing 37; Bite 30 to Strike 33 in the attack category (1); Jump 17. Everything else
// stays with the original on that client.
enum class AbilityRoute {
    local,
    social,
    combat,
    jump
};

constexpr AbilityRoute client_ability_route(int type, int category) noexcept {
    if (type >= 34 && type <= 37) {
        return AbilityRoute::social;
    }
    if (category == 1 && type >= 30 && type <= 33) {
        return AbilityRoute::combat;
    }
    if (type == 17) {
        return AbilityRoute::jump;
    }
    return AbilityRoute::local;
}

struct ActorCommand {
    uint64_t sequence = 0, actor = 0, target = 0;
    uint32_t owner = 0;
    ActorVerb verb = ActorVerb::stop;
    int direction = 0;
    // Stamped when admitted. Retained intentions cannot cross a later input
    // fence even if the native actor and scene remain alive.
    uint64_t input_epoch = 0;
    // Collect uses a scene-resource incarnation, never an Animal binding.
    // The native scene adapter revalidates it again immediately before ordering.
    uint64_t target_generation = 0;
    uint32_t target_native_id = UINT32_MAX;
};

struct ActorBinding {
    uint64_t id = 0, epoch = 0;
    uintptr_t local_key = 0; // opaque local identity key; never serialized
    uint32_t owner = 0;
    bool live = false;
};

// Ordering policy only. A release may stop its preceding keyboard walk, never
// a newer interaction. The native adapter reauthorizes it after jump landing.
struct KeyboardSteeringState {
    bool active = false;
    ActorCommand release{};

    void clear() noexcept {
        active = false;
        release = {};
    }

    void observe(const ActorCommand& command) noexcept {
        if (command.verb == ActorVerb::jump) {
            return;
        }
        if (command.verb == ActorVerb::steer) {
            active = true;
            release = {};
            return;
        }
        if (command.verb == ActorVerb::steer_stop) {
            if (active) {
                release = command;
            }
            active = false;
            return;
        }
        clear();
    }
};
enum class ActorDecision {
    accepted,
    unknown_actor,
    wrong_owner,
    stale_scene,
    invalid_target,
    full,
    inputs_fenced
};

class ActorCommands {
public:
    static constexpr size_t capacity = 8, queue_capacity = 32;
    ActorBinding bind(uintptr_t key, uint32_t owner) noexcept;
    ActorBinding find(uint64_t id) const noexcept;
    ActorBinding find_key(uintptr_t key) const noexcept;
    ActorBinding invalidate(uintptr_t key) noexcept;
    void scene_exit() noexcept;
    // Engine-thread policy only. These operations do not stop an already
    // executing native movement/ability or claim a quiescent original world.
    uint64_t fence_inputs() noexcept;
    bool resume_inputs(uint64_t fenced_epoch) noexcept;

    uint64_t input_epoch() const noexcept {
        return input_epoch_;
    }

    bool inputs_ready() const noexcept {
        return inputs_ready_;
    }

    ActorDecision authorize(const ActorCommand& command) const noexcept;
    ActorDecision enqueue(ActorCommand& command) noexcept;
    bool pop(ActorCommand& command) noexcept;

    uint64_t epoch() const noexcept {
        return epoch_;
    }

    size_t pending_count() const noexcept {
        return size_;
    }

    size_t live_count() const noexcept {
        size_t count = 0;
        for (const auto& binding : bindings_) {
            if (binding.live) {
                ++count;
            }
        }
        return count;
    }

private:
    std::array<ActorBinding, capacity> bindings_{};
    std::array<ActorCommand, queue_capacity> queue_{};
    size_t head_ = 0, size_ = 0;
    uint64_t epoch_ = 1, next_id_ = 0, next_sequence_ = 0;
    uint64_t input_epoch_ = 1;
    bool inputs_ready_ = true;
};

const char* actor_decision(ActorDecision result) noexcept;
const char* actor_verb(ActorVerb verb) noexcept;
} // namespace sporemp
