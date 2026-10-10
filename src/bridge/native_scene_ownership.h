#pragma once
#include "../network/protocol.h"
#include <cstddef>

namespace sporemp {
struct NativeOwnedSpecies {
    const network::Entity* owner[3]{};
    bool distinct = false;
};

// A fossil the original authority creates after the baseline has no original counterpart in a
// client's world, and no factory for one is qualified. The client leaves it unmirrored and keeps
// playing instead of quarantining the session (trial161: a held W key walked A into one after
// 1.3 seconds). It stays invisible and uncollectable there, a recorded C07/C08 limit. The same
// holds for a fossil of the baseline that the client's own world does not hold (trials 346 to 348:
// restored from a save made well away from where the session began, both clients' originals had no
// noun for one far fossil the authority's still held). Every other missing template, and a fossil
// the client holds but in another place or shape, remains fatal.
constexpr bool native_scene_fossil_unmirrored(network::EntityKind kind, bool missing_template) noexcept {
    // A fruit is likewise one of the original's own nouns: a client binds the one of that number its world holds.
    return (kind == network::EntityKind::fossil || kind == network::EntityKind::fruit) && missing_template;
}

// How far a client moves an entity from the sample it held towards the one it received, by the time since it
// received it. A creature is blended over 50 ms and projected every frame. A fossil, nest or fruit is applied
// once per sample and then skipped, so it takes the sample's own values at once: blended, a fruit the authority
// placed stayed a fraction of the way from where it had been put away (trials 411 to 413).
constexpr float native_scene_sample_blend(network::EntityKind kind, uint64_t elapsed_ms) noexcept {
    if (kind != network::EntityKind::animal || elapsed_ms >= 50) {
        return 1.0f;
    }
    return static_cast<float>(elapsed_ms) / 50.0f;
}

// A client applies an unchanged fossil, nest or fruit sample once. Its own game can still move a fruit afterwards
// (trial422: one of 157 lay 3 units lower on one client than the authority held it, and was moved there again),
// so a fruit's unchanged sample is applied again every half second, the fruits taking their turns by number.
constexpr bool native_scene_static_recheck_due(network::EntityKind kind, uint32_t native_id, uint64_t applied_ms,
                                               uint64_t now_ms) noexcept {
    return kind == network::EntityKind::fruit && now_ms >= applied_ms &&
           now_ms - applied_ms >= 500u + (native_id & 15u) * 16u;
}

// The original draws an ability's recharge on its owner's ability bar from the creature's recharging bit and
// that ability's progress (static C19900 sets the bit and zeroes the progress when the ability ends; C15470
// advances the progress and clears the bit). Abilities run on the authority only, so a client's copy never had
// the bit (trial303's frames: no wipe on the owner's client). The client applies the bits its own creature
// has on the authority as they change, and lets the original advance what it then draws.
struct RechargeEdges {
    std::array<uint32_t, 3> started{}, finished{};
};

constexpr RechargeEdges native_recharge_edges(const std::array<uint32_t, 3>& before,
                                              const std::array<uint32_t, 3>& now) noexcept {
    RechargeEdges edges;
    for (size_t i = 0; i < 3; ++i) {
        edges.started[i] = now[i] & ~before[i];
        edges.finished[i] = before[i] & ~now[i];
    }
    return edges;
}

constexpr bool native_recharge_tick_admitted(bool client, bool own_creature, bool recharging,
                                             bool other_ability_state) noexcept {
    return client && own_creature && recharging && !other_ability_state;
}

// The original takes an eaten fruit out of the world in one of two ways: it removes the noun (trials 365 to 382) or
// re-places the noun elsewhere (trial383, after a restore). A fruit also moves without being eaten: one on its plant
// shifts between slots (trial391). So a fruit whose noun remains is gone only if its orderer was fed and no fruit of
// that number lies where it was ordered.
constexpr bool native_plant_food_left(bool present, bool at_its_place, bool orderer_fed) noexcept {
    return !present || (orderer_fed && !at_its_place);
}

// Two parts of the reward-idle guard, as native_awards_checkpoint_busy reports them: the second
// player is a foreign species, and that species' relationship data is not in a state that can be
// saved. Both concern checkpoints. A client's selection view only retains the selected creature
// and refreshes the original interface, so they do not refuse it; reward activity in flight, a
// missing context or an undrained display still do. Before this, the primary player could select
// nothing at all beside a foreign-species second player (trials 217 and 220).
constexpr uint32_t awards_busy_foreign = 0x10, awards_busy_relationship_checkpoint = 0x20;

constexpr bool native_selection_allowed_by_awards(uint32_t busy) noexcept {
    return (busy & ~(awards_busy_foreign | awards_busy_relationship_checkpoint)) == 0;
}

// On a client the model of another player's resting creature drifts away from the creature's
// position: the replica plays an original idle animation whose motion moves the model root, and
// setting the position does not bring the root back (trial207: 0.12 units per second, up to 6.5
// units, so the player saw and clicked an empty spot). Beyond this gap a resting replica's model
// is relocated the way a first presentation relocates it. A moving replica is left alone. Some
// creatures are drawn at a constant offset that relocation does not remove (trial210: 1.93 units
// through 81 relocations in three seconds); `residual` is the gap measured right after the last
// relocation, and only drift clearly beyond it counts.
constexpr float native_model_drift_limit = 0.35f;

constexpr bool native_scene_model_drifted(bool at_rest, float gap, float residual) noexcept {
    return at_rest && gap > native_model_drift_limit && gap > residual + 0.2f && gap < 1.0e6f;
}

// Until its owner's first command in a scene a player's creature has been told nothing, so movement
// it starts is the original AI's and not its owner's (trial163: right after restore B walked 7.4
// units to where a passing animal had been). The authority stops it. From the first command on, the
// owner's own orders may keep the creature walking, so the fence ends there. Movement that restarts
// past the limit is reported once instead of being stopped every frame.
constexpr uint32_t native_unowned_stop_limit = 64;
enum class UnownedMovement {
    none,
    stop,
    exhausted
};

constexpr UnownedMovement native_unowned_movement(bool owner_commanded, bool moving, uint32_t stops) noexcept {
    if (owner_commanded || !moving) {
        return UnownedMovement::none;
    }
    return stops < native_unowned_stop_limit ? UnownedMovement::stop : UnownedMovement::exhausted;
}

// A client's own avatar is moved by the authority's poses alone. A native locomotion request left on it makes
// the original camera turn toward that request's destination whenever the creature moves (static export
// m11-creature-camera-inputs, function D22300h reading the avatar's last request; trials 270 to 295: the promoted
// creature of a foreign second player, restored while the original AI was walking it). The client clears such a
// request with the original stop. One that keeps coming back is reported once instead of cleared forever.
constexpr uint32_t native_client_request_stop_limit = 64;
enum class ClientRequest {
    none,
    clear,
    exhausted
};

constexpr ClientRequest native_client_avatar_request(size_t request_words, uint32_t stops) noexcept {
    if (!request_words) {
        return ClientRequest::none;
    }
    return stops < native_client_request_stop_limit ? ClientRequest::clear : ClientRequest::exhausted;
}

inline bool select_native_owned_species(const network::Entity* entities, size_t count,
                                        NativeOwnedSpecies& out) noexcept {
    out = {};
    if (!entities || !count || count > network::max_entities) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const auto& e = entities[i];
        if (!e.owner) {
            continue;
        }
        if (e.owner > 2 || e.kind != network::EntityKind::animal || out.owner[e.owner]) {
            return false;
        }
        out.owner[e.owner] = &e;
    }
    if (!out.owner[1] || !out.owner[2]) {
        return false;
    }
    const auto& a = *out.owner[1];
    const auto& b = *out.owner[2];
    out.distinct = a.species_instance != b.species_instance || a.species_type != b.species_type ||
                   a.species_group != b.species_group;
    return true;
}
} // namespace sporemp
