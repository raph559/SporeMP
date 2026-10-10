#pragma once
#include <array>
#include <cstdint>
#include <limits>

namespace sporemp {
// These are branch decisions, not a substitute for native relationship logic.
constexpr bool native_relationship_damage_caller(uint32_t caller) {
    return caller == 0x807bba || caller == 0x807c57;
}

constexpr bool native_relationship_count_below_thresholds(uint32_t count, uint32_t fear, uint32_t extinction) {
    return count != std::numeric_limits<uint32_t>::max() && count + 1 < fear && count + 1 < extinction;
}

constexpr bool native_relationship_first_feared_kill(uint32_t count, uint32_t fear, uint32_t extinction) {
    return count == 0 && fear == 1 && extinction > 1;
}

// A kill of a species its owner has already hunted, which leaves that species short of extinction: the count
// rises and nothing else changes. The kill that reaches the extinction threshold is another branch, not admitted.
constexpr bool native_relationship_later_kill(uint32_t count, uint32_t fear, uint32_t extinction) {
    return fear == 1 && count >= 1 && count < extinction && extinction - count > 1;
}
// The second player's fights are followed one creature at a time. It may turn to another creature once no
// damage or relationship callback of the last fight is running and its after-fight samples are done.
// After a fight the second player's and player 1's states are sampled a few more times. The second player's own
// death is the original's outcome of that fight, not a change to judge: it ends the watch.
enum class RelationshipWatch {
    idle,
    sample,
    end
};

constexpr RelationshipWatch native_relationship_watch(uint32_t later_samples, bool owner_dead) {
    return !later_samples ? RelationshipWatch::idle : owner_dead ? RelationshipWatch::end : RelationshipWatch::sample;
}

// A fight of the second player is over once its kill was seen, or another killed the creature, and the samples
// after it are done. Its creature is then no longer followed. A fight without a death stays followed: its next
// blow may come at any time.
constexpr bool native_relationship_fight_finished(bool kill_observed, uint32_t later_samples, bool target_dead) {
    return (kill_observed || target_dead) && !later_samples;
}
// While the second player's fight is followed, player 1's state is compared with what it was. Inside the second
// player's blow or relationship action any change is a leak. Between them a change is player 1's own once it
// has acted or been struck during that fight (up to 0.0.234 its attacks were refused for as long as the fight
// was followed, and any change ended all combat).
enum class PrimaryChange {
    none,
    own,
    leak
};

constexpr PrimaryChange native_relationship_primary_change(bool changed, bool inside_second_player_call,
                                                           bool first_player_active) {
    return !changed                                              ? PrimaryChange::none
           : (inside_second_player_call || !first_player_active) ? PrimaryChange::leak
                                                                 : PrimaryChange::own;
}

constexpr bool native_relationship_target_replaceable(uint32_t damage_depth, uint32_t action_depth,
                                                      uint32_t later_samples) {
    return !damage_depth && !action_depth && !later_samples;
}

// BA3B40 creates this fifth value word as FFFFFFFF on an absent entry. The
// raw missing snapshot remains absent/zero; only the expected result uses it.
constexpr uint32_t native_relationship_initialized_last_word(bool present, uint32_t value) {
    return present ? value : std::numeric_limits<uint32_t>::max();
}

// C2EE80 caches each species goal's befriended count, kill count and relation
// through BA44C0, BA44E0 and BA40E0(key,null,null). With no animal arguments
// the latter returns the stored relation word, or zero for an absent entry.
struct NativeRelationshipGoalValues {
    uint32_t befriended = 0, killed = 0, relation = 0;
};

constexpr NativeRelationshipGoalValues native_relationship_goal_values(bool present,
                                                                       const std::array<uint32_t, 5>& words) {
    return present ? NativeRelationshipGoalValues{words[2], words[3], words[0]} : NativeRelationshipGoalValues{};
}

// The return address of C2EE80's only relationship-manager getter call.
constexpr bool native_relationship_goal_reader(uint32_t caller) {
    return caller == 0x82ee8e;
}

// C2E980's getter return. E16070 reads the goal's relation through it while
// populating one original goal card; admitted only inside that display call.
constexpr bool native_relationship_card_reader(uint32_t caller) {
    return caller == 0x82e98d;
}

template <class Animal, class Manager>
struct NativeRelationshipAbi {
    using Get = void*(__cdecl*)();
    using Allocate = void*(__cdecl*)(uint32_t, const char*, int, unsigned, const char*, int);
    using Construct = void*(__thiscall*)(void*);
    using Delete = void*(__thiscall*)(void*, uint32_t);
    using Find = uint32_t*(__thiscall*)(void*, uint32_t*, const uint32_t*);
    using Effective = int(__thiscall*)(void*, const uint32_t*, Animal*, Animal*);
    using Action = void(__thiscall*)(void*, uint32_t, const uint32_t*, Animal*);
    // BA3B40 receives the first native map (manager +0x20) and one species
    // key; it inserts an absent entry and returns the five-word value. RET 4.
    using Insert = uint32_t*(__thiscall*)(void*, const uint32_t*);
    // C2FBB0 re-evaluates one original species goal and its two card props.
    using RefreshGoal = void(__thiscall*)(void*);
    // B3D500 returns the original goal-card UI manager; E17BF0 repopulates
    // one card item from its goal. A nonzero flag skips its state animation.
    using CardManager = void*(__cdecl*)();
    using PopulateCard = void(__thiscall*)(void*, char);
    // B212D0 and exactly BA4900's original collection callbacks.
    using Collection = void*(__thiscall*)(Manager*, void*, void*, void*, void*, uint32_t);
    // BA27C0/BA2830 receive the subsystem at root+4 and consume one stack
    // argument. Read/Write stream health, not an inferred AL, reports success.
    template <class Stream>
    using Serialize = void(__thiscall*)(void*, Stream*);
};
} // namespace sporemp
