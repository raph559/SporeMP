#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace sporemp {
// Local harness authority, not network authentication. No native addresses enter a command.
enum class ActorVerb : uint32_t { move, jump, attack, stop, retire, approach, engage, pickup };
struct ActorCommand {
    uint64_t sequence = 0, actor = 0, target = 0;
    uint32_t owner = 0;
    ActorVerb verb = ActorVerb::stop;
    int direction = 0;
};
struct ActorBinding {
    uint64_t id = 0, epoch = 0;
    uintptr_t local_key = 0; // opaque local identity key; never serialized
    uint32_t owner = 0;
    bool live = false;
};
enum class ActorDecision { accepted, unknown_actor, wrong_owner, stale_scene, invalid_target, full };
class ActorCommands {
public:
    static constexpr size_t capacity = 8, queue_capacity = 32;
    ActorBinding bind(uintptr_t key, uint32_t owner) noexcept;
    ActorBinding find(uint64_t id) const noexcept;
    ActorBinding find_key(uintptr_t key) const noexcept;
    ActorBinding invalidate(uintptr_t key) noexcept;
    void scene_exit() noexcept;
    ActorDecision authorize(const ActorCommand& command) const noexcept;
    ActorDecision enqueue(ActorCommand& command) noexcept;
    bool pop(ActorCommand& command) noexcept;
    uint64_t epoch() const noexcept { return epoch_; }
    size_t pending_count() const noexcept { return size_; }
    size_t live_count() const noexcept {
        size_t count = 0;
        for (const auto& binding : bindings_) if (binding.live) ++count;
        return count;
    }
private:
    std::array<ActorBinding, capacity> bindings_{};
    std::array<ActorCommand, queue_capacity> queue_{};
    size_t head_ = 0, size_ = 0;
    uint64_t epoch_ = 1, next_id_ = 0, next_sequence_ = 0;
};
const char* actor_decision(ActorDecision result) noexcept;
const char* actor_verb(ActorVerb verb) noexcept;
}
