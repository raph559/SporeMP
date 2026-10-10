#pragma once
#include "world_identity.h"
#include "location_id.h"
#include "content_wire.h"
#include "transfer_wire.h"
#include "preload_wire.h"
#include "tutorial_wire.h"
#include "social_wire.h"
#include "death_wire.h"
#include "inventory_wire.h"
#include "hatch_wire.h"
#include "history_wire.h"
#include "relationship_wire.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sporemp::network {
constexpr uint32_t protocol_version = 1, schema_version = 32;
// 664 bytes up to schema 27, which the common encoding filled; schema 28 appends an entity's three recharging words
// (680) and schema 30 two more words (688), of which schema 32 uses one for a fruit's state.
constexpr size_t packet_bytes = 688, max_entities = 2048, queue_capacity = 8192;

struct Identity {
    Digest build{}, executable{}, content{}, fixture{};
    WorldIdentity world{};
    bool operator==(const Identity& other) const noexcept;
};
enum class Kind : uint32_t {
    hello = 1,
    welcome,
    reject,
    scene_begin,
    entity,
    scene_end,
    baseline_ack,
    motion,
    despawn,
    action,
    action_result,
    ping,
    content_request,
    content_response,
    transfer_command,
    transfer_receipt,
    preload_request,
    preload_grant,
    tutorial,
    tutorial_done,
    social_view,
    death,
    death_done,
    inventory,
    hatch_view,
    hatch_input,
    history,
    relationship
};
enum class Role : uint32_t {
    player = 1,
    authority = 2
};
enum class Error : uint32_t {
    none,
    malformed,
    incompatible,
    authentication,
    capacity,
    stale,
    ownership,
    not_ready,
    authority_lost,
    timeout,
    queue_full,
    world_mismatch
};
enum class Verb : uint32_t {
    move = 0,
    jump = 1,
    attack = 2,
    stop = 3,
    approach = 5,
    engage = 6,
    pickup = 7,
    social = 8,
    combat = 9,
    steer = 10,
    steer_stop = 11,
    collect = 12,
    forage = 13
};
enum class EntityKind : uint32_t {
    animal = 0,
    fossil = 1,
    nest = 2,
    fruit = 3
};

// What a fruit is doing, as the original holds it: its state word (0 on its holder, 6 put away, others on its way
// down or on the ground), whether its model is in the world, and four of its spatial flags. Each game used to stock
// and move its own fruit (trials 378, 405, 409); the authority's values now reach the clients in one word.
struct FruitState {
    uint32_t state = 0;
    bool in_world = false, pickable = false, tangible = false, enabled = false, supported = false;

    constexpr bool operator==(const FruitState& o) const noexcept {
        return state == o.state && in_world == o.in_world && pickable == o.pickable && tangible == o.tangible &&
               enabled == o.enabled && supported == o.supported;
    }
};

constexpr uint32_t pack_fruit_state(const FruitState& s) noexcept {
    return s.state > 0xffu
               ? UINT32_MAX
               : s.state | (uint32_t(s.in_world) << 8) | (uint32_t(s.pickable) << 9) | (uint32_t(s.tangible) << 10) |
                     (uint32_t(s.enabled) << 11) | (uint32_t(s.supported) << 12);
}

constexpr bool unpack_fruit_state(uint32_t word, FruitState& s) noexcept {
    if (word >> 13) {
        return false;
    }
    s.state = word & 0xffu;
    s.in_world = word >> 8 & 1;
    s.pickable = word >> 9 & 1;
    s.tangible = word >> 10 & 1;
    s.enabled = word >> 11 & 1;
    s.supported = word >> 12 & 1;
    return true;
}

struct Entity {
    uint64_t id = 0, generation = 0, owner = 0, tick = 0;
    uint32_t native_id = 0, herd_native_id = 0, species_instance = 0, species_type = 0, species_group = 0,
             archetype = 0;
    float x = 0, y = 0, z = 0, qx = 0, qy = 0, qz = 0, qw = 1, health = 0, energy = 0, hunger = 0, dna = 0, vx = 0,
          vy = 0, vz = 0;
    uint32_t life_state = 0; // 0 alive, 1 native mbDead; never a request to execute death.
    // A reloaded NPC corpse can retain positive native health (.124 trial54).
    // Its dead flag remains authoritative; it cannot act or be attacked.
    uint32_t age = 1, alpha = 0, combatant_state = 0; // Pinned native scalar context; packed bytes on the wire.
    float scale = 1; // Observed native scale; never recomputed with local random growth.
    uint32_t fed_on = 0,
             pickup_owner = 0; // Native first-feed flag and observed original grant beneficiary (0 unknown).
    float food = 0;            // Remaining original corpse nutrition; first-feed bonus is a separate one-time resource.
    EntityKind kind = EntityKind::animal;
    // Original model resource identity for static fossils/nests. Nest affiliation
    // is its observed herd/species identity, never control of a second avatar.
    uint32_t model_instance = 0, model_type = 0, model_group = 0;
    // Schema 28: the original's recharging-ability bits (88) of a player's creature, for its owner's ability
    // bar. Zero for every other entity.
    std::array<uint32_t, 3> recharging{};
    // Schema 32: a fruit's packed state (pack_fruit_state). Zero for every other entity. Schemas 30 and 31 carried
    // here the fruit a player's creature had last eaten, which a shared fruit makes needless.
    uint32_t fruit_state = 0;
};

// Value object only: serialize with encode/decode, never sizeof/memcpy Packet.
struct Packet {
    Kind kind = Kind::ping;
    uint64_t sequence = 0, session = 0, scene = 0, baseline = 0, player = 0;
    Role role = Role::player;
    Error error = Error::none;
    Identity identity{};
    Digest credential{};
    Entity entity{};
    uint64_t target = 0;
    uint64_t target_generation = 0;
    LocationId location{};
    uint64_t worker_generation = 0;
    uint32_t native_load_slot = 0;
    uint64_t maintenance_transfer = 0; // Authority HELLO only: explicit durable recovery admission.
    uint64_t request = 0;              // Action/result correlation; independent from each stream sequence.
    Verb verb = Verb::stop;
    int32_t direction = 0;
    uint32_t count = 0;
    uint32_t world_index = 0;   // Only reject/world_mismatch; fixed allowlist index, never a peer path.
    ContentWire content{};      // Schema 6 content kinds use their own bounded payload after the common 72-byte header.
    TransferFrame transfer{};   // Schema 9: authenticated authority control only.
    PreloadFrame preload{};     // Schema 10: distinct pre-load TLS connection, no scene/baseline.
    TutorialFrame tutorial{};   // Schema 11: owner-only presentation and observed completion.
    SocialFrame social{};       // Schema 13: owner-only meter and scalar drawing segments, no durable state.
    DeathFrame death{};         // Schema 14: original owner presentation and observed restart confirmation.
    InventoryFrame inventory{}; // Schema 22: bounded owner-only native inventory snapshot chunks.
    HatchFrame hatch{};         // Schema 24: native owner lesson cue and selected ability.
    HistoryFrame history{};     // Schema 25: atomic owner-B original Creature history values.
    RelationshipFrame relationship{}; // Schema 26: complete owner-B species relationship values.
};

using Wire = std::array<uint8_t, packet_bytes>;
Wire encode(const Packet& packet);
bool decode(const uint8_t* wire, size_t size, Packet& packet, std::string& error);
bool valid_entity(const Entity& entity) noexcept;
bool unchanged_static_entity(const Entity& a, const Entity& b) noexcept;
bool targeted(Verb verb) noexcept;
bool eligible_target(Verb verb, const Entity& entity) noexcept;
bool valid_action(const Packet& packet) noexcept;
std::string hex(const Digest& digest);
bool parse_hex(const std::string& text, Digest& digest) noexcept;
const char* error_name(Error error) noexcept;
std::string error_detail(const Packet& packet);
} // namespace sporemp::network
