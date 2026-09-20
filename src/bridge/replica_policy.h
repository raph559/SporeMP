#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace sporemp::replica {
// Engine-independent policy and identities. These objects are not wire layouts.
enum class Role { single_player, authority, replica };
enum class Mutation : size_t {
    ai, targeting, ability, damage, energy, reward, death, pickup, spawn, timer,
    progression, save, presentation, apply_state, count
};
enum class Link { awaiting_baseline, active, disconnected };
struct Fence {
    std::array<uint64_t, 2> worker{};
    uint64_t scene = 0;
    bool valid() const noexcept;
    bool operator==(const Fence& other) const noexcept;
};
struct Entity {
    uint64_t id = 0, generation = 0;
    bool operator==(const Entity& other) const noexcept;
};
struct Vitals {
    float health = 0, energy = 0, hunger = 0, dna = 0;
};
bool valid(const Vitals& state) noexcept;
struct Update {
    Fence fence{};
    Entity entity{};
    uint64_t baseline = 0, sequence = 0;
    Vitals state{};
};
enum class Decision { accepted, wrong_role, disconnected, invalid, stale, missing, full, busy };
struct Counters {
    std::array<uint64_t, static_cast<size_t>(Mutation::count)> allowed{}, blocked{};
    uint64_t applied = 0, rejected = 0, invalidated = 0, outbound_blocked = 0;
};

class Policy {
public:
    // One role for the entire process lifetime, including a lost connection.
    explicit Policy(Role role) noexcept : role_(role) {}
    Role role() const noexcept { return role_; }
    bool allow(Mutation mutation) noexcept;
    bool may_publish() noexcept;
    const Counters& counters() const noexcept { return counters_; }
    bool applying() const noexcept { return applying_; }
    // M06 scene registry validates remote identity before this callback. This
    // only supplies the same nested-write barrier as scalar Registry::apply.
    using Project = bool(*)(void*);
    bool project(Project callback, void* context) noexcept;
private:
    friend class Registry;
    const Role role_;
    bool applying_ = false;
    Counters counters_{};
};

class Registry {
public:
    static constexpr size_t capacity = 32;
    explicit Registry(Policy& policy) noexcept : policy_(policy) {}
    // Binding means adopting an existing, separately validated native object.
    // A tombstone is retained until a new baseline; local address reuse is not identity.
    Decision begin(const Fence& fence, uint64_t baseline) noexcept;
    Decision bind(Entity entity, uint64_t local_id) noexcept;
    Decision invalidate(uint64_t local_id) noexcept;
    void disconnect() noexcept;
    Link link() const noexcept { return link_; }
    const Fence& fence() const noexcept { return fence_; }
    uint64_t baseline() const noexcept { return baseline_; }
    using Apply = bool(*)(void*, uint64_t, const Vitals&);
    Decision apply(const Update& update, Apply callback, void* context) noexcept;
private:
    struct Slot { Entity entity{}; uint64_t local = 0, sequence = 0; bool live = false; };
    Policy& policy_;
    Fence fence_{};
    uint64_t baseline_ = 0;
    Link link_ = Link::awaiting_baseline;
    std::array<Slot, capacity> slots_{};
    Decision reject(Decision reason) noexcept;
};
const char* name(Mutation mutation) noexcept;
const char* name(Decision decision) noexcept;
}
