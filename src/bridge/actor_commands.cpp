#include "actor_commands.h"

namespace sporemp {
ActorBinding ActorCommands::bind(uintptr_t key, uint32_t owner) noexcept {
    if (!key) return {};
    if (auto old = find_key(key); old.live) return old.owner == owner ? old : ActorBinding{};
    for (auto& slot : bindings_) if (!slot.live) {
        slot = {++next_id_, epoch_, key, owner, true};
        return slot;
    }
    return {};
}
ActorBinding ActorCommands::find(uint64_t id) const noexcept {
    for (const auto& slot : bindings_) if (id && slot.id == id) return slot;
    return {};
}
ActorBinding ActorCommands::find_key(uintptr_t key) const noexcept {
    for (const auto& slot : bindings_) if (key && slot.local_key == key && slot.live) return slot;
    return {};
}
ActorBinding ActorCommands::invalidate(uintptr_t key) noexcept {
    for (auto& slot : bindings_) if (key && slot.local_key == key && slot.live) {
        auto old = slot; slot.live = false; return old;
    }
    return {};
}
void ActorCommands::scene_exit() noexcept {
    for (auto& slot : bindings_) slot.live = false;
    ++epoch_; head_ = size_ = 0;
}
ActorDecision ActorCommands::authorize(const ActorCommand& command) const noexcept {
    const auto actor = find(command.actor);
    if (!actor.live) return ActorDecision::unknown_actor;
    if (actor.epoch != epoch_) return ActorDecision::stale_scene;
    if (!command.owner || actor.owner != command.owner) return ActorDecision::wrong_owner;
    if (command.verb == ActorVerb::attack || command.verb == ActorVerb::approach || command.verb == ActorVerb::engage || command.verb == ActorVerb::pickup) {
        auto target = find(command.target);
        if (!target.live || target.epoch != epoch_ || target.id == actor.id) return ActorDecision::invalid_target;
    }
    return ActorDecision::accepted;
}
ActorDecision ActorCommands::enqueue(ActorCommand& command) noexcept {
    auto decision = authorize(command);
    if (decision != ActorDecision::accepted) return decision;
    if (size_ == queue_capacity) return ActorDecision::full;
    command.sequence = ++next_sequence_;
    queue_[(head_ + size_) % queue_capacity] = command;
    ++size_;
    return ActorDecision::accepted;
}
bool ActorCommands::pop(ActorCommand& command) noexcept {
    if (!size_) return false;
    command = queue_[head_]; head_ = (head_ + 1) % queue_capacity; --size_;
    return true; // Caller reauthorizes after intervening native callbacks.
}
const char* actor_decision(ActorDecision result) noexcept {
    switch (result) {
    case ActorDecision::accepted: return "accepted";
    case ActorDecision::unknown_actor: return "unknown_actor";
    case ActorDecision::wrong_owner: return "wrong_owner";
    case ActorDecision::stale_scene: return "stale_scene";
    case ActorDecision::invalid_target: return "invalid_target";
    case ActorDecision::full: return "queue_full";
    }
    return "unknown";
}
const char* actor_verb(ActorVerb verb) noexcept {
    switch (verb) {
    case ActorVerb::move: return "move";
    case ActorVerb::jump: return "jump";
    case ActorVerb::attack: return "attack";
    case ActorVerb::stop: return "stop";
    case ActorVerb::retire: return "retire";
    case ActorVerb::approach: return "approach";
    case ActorVerb::engage: return "engage";
    case ActorVerb::pickup: return "pickup";
    }
    return "unknown";
}
}
