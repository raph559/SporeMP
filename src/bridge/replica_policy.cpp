#include "replica_policy.h"
#include <cmath>

namespace sporemp::replica {
bool Fence::valid() const noexcept { return (worker[0] || worker[1]) && scene; }
bool Fence::operator==(const Fence& other) const noexcept { return worker == other.worker && scene == other.scene; }
bool Entity::operator==(const Entity& other) const noexcept { return id == other.id && generation == other.generation; }
bool valid(const Vitals& value) noexcept {
    // First native experiment accepts living Creature vitals only. Death,
    // respawn, maximum-health changes and full progression need their own bindings.
    return std::isfinite(value.health) && value.health > 0 && value.health <= 1000000 &&
        std::isfinite(value.energy) && value.energy >= 0 && value.energy <= 1000000 &&
        std::isfinite(value.hunger) && value.hunger >= 0 && value.hunger <= 1000000 &&
        std::isfinite(value.dna) && value.dna >= 0 && value.dna <= 1000000000;
}
bool Policy::allow(Mutation mutation) noexcept {
    const auto index = static_cast<size_t>(mutation);
    if (index >= counters_.allowed.size()) return false;
    const bool allowed = mutation == Mutation::presentation ||
        (mutation == Mutation::apply_state ? role_ == Role::replica && applying_ :
            role_ != Role::replica && !applying_);
    ++(allowed ? counters_.allowed[index] : counters_.blocked[index]);
    return allowed;
}
bool Policy::may_publish() noexcept {
    if (role_ == Role::authority && !applying_) return true;
    ++counters_.outbound_blocked;
    return false;
}
bool Policy::project(Project callback, void* context) noexcept {
    if (role_ != Role::replica || applying_ || !callback) return false;
    applying_ = true;
    bool result = false;
    try { result = callback(context); } catch (...) { result = false; }
    applying_ = false;
    ++(result ? counters_.applied : counters_.rejected);
    return result;
}
Decision Registry::reject(Decision reason) noexcept { ++policy_.counters_.rejected; return reason; }
Decision Registry::begin(const Fence& fence, uint64_t baseline) noexcept {
    if (policy_.role() != Role::replica) return reject(Decision::wrong_role);
    if (policy_.applying()) return reject(Decision::busy);
    if (!fence.valid() || !baseline) return reject(Decision::invalid);
    // Monotonic local admission token also fences a different source after
    // reconnect. The trusted controller assigns it; this is not authentication.
    if (baseline <= baseline_) return reject(Decision::stale);
    fence_ = fence; baseline_ = baseline; slots_ = {}; link_ = Link::active;
    return Decision::accepted;
}
Decision Registry::bind(Entity entity, uint64_t local) noexcept {
    if (policy_.applying()) return reject(Decision::busy);
    if (link_ != Link::active) return reject(Decision::disconnected);
    if (!entity.id || !entity.generation || !local) return reject(Decision::invalid);
    Slot* empty = nullptr;
    for (auto& slot : slots_) {
        if (slot.entity.id == entity.id || slot.local == local) return reject(Decision::stale);
        if (!slot.entity.id && !empty) empty = &slot;
    }
    if (!empty) return reject(Decision::full);
    *empty = {entity, local, 0, true};
    return Decision::accepted;
}
Decision Registry::invalidate(uint64_t local) noexcept {
    for (auto& slot : slots_) if (slot.live && local && slot.local == local) {
        slot.live = false; ++policy_.counters_.invalidated; return Decision::accepted;
    }
    return reject(Decision::missing);
}
void Registry::disconnect() noexcept {
    link_ = Link::disconnected;
    for (auto& slot : slots_) if (slot.live) { slot.live = false; ++policy_.counters_.invalidated; }
}
Decision Registry::apply(const Update& update, Apply callback, void* context) noexcept {
    if (policy_.role() != Role::replica) return reject(Decision::wrong_role);
    if (policy_.applying()) return reject(Decision::busy);
    if (link_ != Link::active) return reject(Decision::disconnected);
    if (!(update.fence == fence_) || update.baseline != baseline_) return reject(Decision::stale);
    if (!callback || !update.sequence || !valid(update.state)) return reject(Decision::invalid);
    Slot* selected = nullptr;
    for (auto& slot : slots_) if (slot.live && slot.entity == update.entity) { selected = &slot; break; }
    if (!selected) return reject(Decision::missing);
    if (update.sequence <= selected->sequence) return reject(Decision::stale);
    // Reserve before native callbacks. A callback cannot recursively apply,
    // reconnect, or publish. Failure quarantines the baseline: it is not retried.
    selected->sequence = update.sequence;
    bool result = false;
    policy_.applying_ = true;
    try { result = callback(context, selected->local, update.state); }
    catch (...) { result = false; }
    policy_.applying_ = false;
    if (!result || !selected->live || link_ != Link::active) {
        disconnect(); return reject(Decision::invalid);
    }
    ++policy_.counters_.applied;
    return Decision::accepted;
}
const char* name(Mutation mutation) noexcept {
    constexpr const char* names[] = {"ai", "targeting", "ability", "damage", "energy", "reward", "death",
        "pickup", "spawn", "timer", "progression", "save", "presentation", "apply_state"};
    const auto index = static_cast<size_t>(mutation);
    return index < std::size(names) ? names[index] : "invalid";
}
const char* name(Decision decision) noexcept {
    constexpr const char* names[] = {"accepted", "wrong_role", "disconnected", "invalid", "stale", "missing", "full", "busy"};
    const auto index = static_cast<size_t>(decision);
    return index < std::size(names) ? names[index] : "invalid";
}
}
