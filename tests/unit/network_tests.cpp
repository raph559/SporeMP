#include "coordinator.h"
#include "tls.h"
#include "channel.h"
#include "../../src/bridge/native_scene_cadence.h"
#include "../../src/bridge/native_scene_ownership.h"
#include "steering.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace sporemp::network;
int encounter_network_tests();
int world_identity_tests();

namespace {
int assertions = 0;

void check(bool pass, const char* message) {
    ++assertions;
    if (!pass) {
        throw std::runtime_error(message);
    }
}

Digest digest(uint8_t v) {
    Digest d{};
    d.fill(v);
    return d;
}

SessionConfig settings() {
    SessionConfig c;
    c.identity = {digest(1), digest(2), digest(3), digest(4)};
    for (auto& value : c.identity.world) {
        value = digest(4);
    }
    c.authority = digest(10);
    c.player1 = digest(11);
    c.player2 = digest(12);
    c.epoch = 42;
    return c;
}

Entity entity(uint64_t id, uint64_t owner) {
    Entity e;
    e.id = id;
    e.generation = 1;
    e.owner = owner;
    e.tick = 1;
    e.native_id = uint32_t(id);
    e.herd_native_id = 8;
    e.species_instance = 9;
    e.species_type = 10;
    e.species_group = 11;
    e.archetype = 12;
    e.x = 1;
    e.y = 2;
    e.z = 3;
    e.health = 100;
    e.energy = 50;
    e.hunger = 30;
    e.dna = 8.75f;
    return e;
}

void codec_tests() {
    std::array<Entity, 3> owned{entity(1, 1), entity(3, 0), entity(2, 2)};
    sporemp::NativeOwnedSpecies selection;
    check(sporemp::select_native_owned_species(owned.data(), owned.size(), selection) && !selection.distinct &&
              selection.owner[1] == &owned[0] && selection.owner[2] == &owned[2],
          "owned species selection ignores NPC ordering");
    for (unsigned key = 0; key < 3; ++key) {
        auto foreign = owned;
        if (key == 0) {
            ++foreign[2].species_instance;
        }
        if (key == 1) {
            ++foreign[2].species_type;
        }
        if (key == 2) {
            ++foreign[2].species_group;
        }
        check(sporemp::select_native_owned_species(foreign.data(), foreign.size(), selection) && selection.distinct,
              "each complete native species key component can require foreign owned construction");
    }
    owned[1].owner = 1;
    check(!sporemp::select_native_owned_species(owned.data(), owned.size(), selection),
          "duplicate canonical owner cannot steal an owned binding");
    owned[1].owner = 0;
    owned[2].kind = EntityKind::nest;
    check(!sporemp::select_native_owned_species(owned.data(), owned.size(), selection),
          "owned non-animal refused before native construction");
    check(!sporemp::select_native_owned_species(owned.data(), 1, selection),
          "missing second owner is not a foreign baseline");
    Packet p;
    p.kind = Kind::entity;
    p.sequence = 123;
    p.session = 42;
    p.scene = 4;
    p.baseline = 5;
    p.player = 2;
    p.identity = settings().identity;
    p.credential = digest(99);
    p.entity = entity(19, 2);
    p.entity.vx = 4;
    p.entity.vy = 5;
    p.entity.vz = 6;
    p.request = 991;
    Wire w = encode(p);
    Packet result;
    std::string error;
    check(decode(w.data(), w.size(), result, error), "roundtrip_decode");
    check(result.entity.id == 19 && result.entity.dna == 8.75f && result.entity.vz == 6 && result.request == 991 &&
              result.identity == p.identity,
          "roundtrip_fields");
    for (size_t n = 0; n < packet_bytes; ++n) {
        check(!decode(w.data(), n, result, error), "every_truncation_rejected");
    }
    check(!decode(w.data(), packet_bytes + 1, result, error), "oversize_rejected");
    for (size_t offset :
         {size_t(0), size_t(4), size_t(8), size_t(12), size_t(16), size_t(20), size_t(24), size_t(31), size_t(383)}) {
        Wire bad = w;
        bad[offset] = 255;
        check(!decode(bad.data(), bad.size(), result, error), "header_enum_or_reserved_rejected");
    }
    // Schema 28: the recharging abilities of a player's creature, as the original's 88 bits.
    p.kind = Kind::motion;
    p.entity.recharging = {0x20u, 0, 1u << 23};
    w = encode(p);
    check(decode(w.data(), w.size(), result, error) && result.entity.recharging == p.entity.recharging,
          "a player's creature carries its recharging abilities");
    p.entity.recharging = {0, 0, 1u << 24};
    w = encode(p);
    check(!decode(w.data(), w.size(), result, error), "a creature has 88 ability slots at most");
    p.entity = entity(19, 0);
    p.entity.recharging = {1, 0, 0};
    w = encode(p);
    check(!decode(w.data(), w.size(), result, error), "only a player's creature carries recharging abilities");
    // Schema 32: a fruit is a shared entity. Each game stocked and moved its own fruit (trials 378, 405, 409); the
    // authority's place, turn and state for every fruit now reach the clients, which no longer run that code.
    using sporemp::network::FruitState;
    using sporemp::network::pack_fruit_state;
    using sporemp::network::unpack_fruit_state;
    const FruitState held{0, true, true, true, true, false}, parked{6, false, false, true, false, false};
    FruitState back;
    check(unpack_fruit_state(pack_fruit_state(held), back) && back == held &&
              unpack_fruit_state(pack_fruit_state(parked), back) && back == parked &&
              pack_fruit_state(held) != pack_fruit_state(parked),
          "a fruit's state word and flags survive packing");
    check(!unpack_fruit_state(1u << 16, back) &&
              pack_fruit_state(FruitState{300, true, true, true, true, false}) == UINT32_MAX,
          "a state beyond a byte, or a word with unknown bits, is refused");
    Entity fruit;
    fruit.kind = EntityKind::fruit;
    fruit.id = 77;
    fruit.generation = 1;
    fruit.tick = 1;
    fruit.native_id = 2804;
    fruit.x = -419.7f;
    fruit.y = 147.6f;
    fruit.z = 245.9f;
    fruit.model_instance = 357547957u;
    fruit.fruit_state = pack_fruit_state(held);
    p.entity = fruit;
    w = encode(p);
    check(decode(w.data(), w.size(), result, error) && result.entity.kind == EntityKind::fruit &&
              result.entity.native_id == 2804 && result.entity.fruit_state == fruit.fruit_state,
          "a fruit crosses the wire with its state");
    auto moved = fruit;
    moved.fruit_state = pack_fruit_state(parked);
    check(unchanged_static_entity(fruit, fruit) && !unchanged_static_entity(fruit, moved),
          "a fruit is resent when its state changes and not otherwise");
    moved = fruit;
    moved.x += 1;
    check(!unchanged_static_entity(fruit, moved), "and when it moves");
    p.entity.owner = 1;
    w = encode(p);
    check(!decode(w.data(), w.size(), result, error), "no player owns a fruit");
    p.entity = fruit;
    p.entity.health = 1;
    w = encode(p);
    check(!decode(w.data(), w.size(), result, error), "a fruit has no creature values");
    p.entity = fruit;
    p.entity.fruit_state = 1u << 16;
    w = encode(p);
    check(!decode(w.data(), w.size(), result, error), "a fruit's state word has no unknown bits");
    p.entity = entity(19, 2);
    p.entity.fruit_state = 1;
    w = encode(p);
    check(!decode(w.data(), w.size(), result, error), "only a fruit carries a fruit's state");
    p.kind = Kind::entity;
    p.entity = entity(19, 2);
    p.entity.vx = 4;
    p.entity.vy = 5;
    p.entity.vz = 6;
    p.entity.x = std::numeric_limits<float>::quiet_NaN();
    w = encode(p);
    check(!decode(w.data(), w.size(), result, error), "nan_rejected");
    p.entity.x = 1;
    p.entity.owner = 3;
    w = encode(p);
    check(!decode(w.data(), w.size(), result, error), "invalid_entity_owner");
    p.entity = entity(19, 2);
    p.kind = Kind::action;
    p.direction = 3;
    w = encode(p);
    check(decode(w.data(), w.size(), result, error), "native_direction3");
    p.direction = 4;
    w = encode(p);
    check(!decode(w.data(), w.size(), result, error), "invalid_direction");
    p.direction = 0;
    p.verb = Verb(4);
    w = encode(p);
    check(!decode(w.data(), w.size(), result, error), "retire_not_exposed");
    Digest d;
    check(parse_hex(hex(digest(97)), d) && d == digest(97), "hex_roundtrip");
    check(!parse_hex(std::string(64, 'z'), d) && !parse_hex("abc", d), "invalid_hex");
}

struct Fixture {
    SessionConfig config = settings();
    Session session{config};
    uint64_t seq[16]{};

    std::vector<Dispatch> send(uint64_t c, Packet p) {
        p.sequence = ++seq[c];
        p.session = p.kind == Kind::hello ? 0 : config.epoch;
        return session.receive(c, p);
    }

    std::vector<Dispatch> join(uint64_t c, Role role, uint64_t player = 0) {
        check(session.connect(c), "session_connect");
        Packet p;
        p.kind = Kind::hello;
        p.role = role;
        p.identity = config.identity;
        p.credential = role == Role::authority ? config.authority : player == 1 ? config.player1 : config.player2;
        return send(c, p);
    }

    std::vector<Dispatch> baseline(uint64_t scene = 1, uint64_t baseline_id = 1) {
        Packet p;
        p.scene = scene;
        p.baseline = baseline_id;
        p.kind = Kind::scene_begin;
        p.count = 2;
        send(1, p);
        p.kind = Kind::entity;
        p.entity = entity(101, 1);
        send(1, p);
        p.entity = entity(102, 2);
        send(1, p);
        p.kind = Kind::scene_end;
        return send(1, p);
    }
};

uint64_t baseline_id(const std::vector<Dispatch>& v, uint64_t c) {
    for (const auto& d : v) {
        if (d.connection == c && d.packet.kind == Kind::scene_begin) {
            return d.packet.baseline;
        }
    }
    return 0;
}

bool rejected(const std::vector<Dispatch>& v, Error e) {
    return !v.empty() && v.back().packet.kind == Kind::reject && v.back().packet.error == e;
}

bool action_refused(const std::vector<Dispatch>& v, Error e) {
    return v.size() == 1 && v[0].packet.kind == Kind::action_result && v[0].packet.error == e && !v[0].close;
}

void hatch_tests() {
    Packet view;
    view.kind = Kind::hatch_view;
    view.role = Role::authority;
    view.player = 2;
    view.session = 42;
    view.scene = 1;
    view.baseline = 1;
    view.hatch = {102, 1, 3, 1, 627923451, 4, 0, UINT32_MAX};
    Packet decoded;
    std::string error;
    auto wire = encode(view);
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.hatch == view.hatch, "hatch view roundtrip");
    for (unsigned test = 0; test < 7; ++test) {
        auto bad = view;
        if (test == 0) {
            bad.hatch.entity = 0;
        }
        if (test == 1) {
            bad.hatch.serial = 0;
        }
        if (test == 2) {
            bad.hatch.active = 2;
        }
        if (test == 3) {
            bad.hatch.ability = 88;
        }
        if (test == 4) {
            bad.hatch.mode = 2;
        }
        if (test == 5) {
            bad.hatch.choice = 1;
        }
        if (test == 6) {
            bad.role = Role::player;
        }
        const auto bytes = encode(bad);
        check(!decode(bytes.data(), bytes.size(), decoded, error), "invalid hatch offer refused");
    }
    auto input = view;
    input.kind = Kind::hatch_input;
    input.role = Role::player;
    input.hatch.choice = 7;
    wire = encode(input);
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.hatch.choice == 7,
          "owner may choose a different original ability");
    input.hatch.choice = 88;
    wire = encode(input);
    check(!decode(wire.data(), wire.size(), decoded, error), "hatch input index bounded");
    input.hatch.choice = 7;
    wire = encode(view);
    wire.back() = 1;
    check(!decode(wire.data(), wire.size(), decoded, error), "hatch reserved bytes refused");
    Fixture fixture;
    fixture.join(1, Role::authority);
    fixture.join(2, Role::player, 1);
    fixture.join(3, Role::player, 2);
    const auto initial = fixture.baseline();
    const auto a = baseline_id(initial, 2), b = baseline_id(initial, 3);
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 1;
    ack.baseline = a;
    fixture.send(2, ack);
    ack.baseline = b;
    fixture.send(3, ack);
    auto delivered = fixture.send(1, view);
    check(delivered.size() == 1 && delivered[0].connection == 3 && delivered[0].packet.baseline == b,
          "native lesson reaches only its owner baseline");
    auto replaced = view;
    replaced.hatch.hint = 123;
    check(rejected(fixture.send(1, replaced), Error::malformed), "one hatch serial cannot silently change its cue");
    input.baseline = b;
    auto forwarded = fixture.send(3, input);
    check(forwarded.size() == 1 && forwarded[0].connection == 1 && forwarded[0].packet.hatch.choice == 7,
          "chosen ability reaches authority unchanged");
    auto stale = input;
    stale.hatch.serial = 2;
    check(fixture.send(3, stale).empty(), "previous lesson cannot act on current hatch");
    auto forged = input;
    forged.player = 1;
    check(rejected(fixture.send(3, forged), Error::ownership), "cross-owner hatch input rejected");
    forged = input;
    forged.hatch.generation = 2;
    const auto refusal = fixture.send(3, forged);
    check(rejected(refusal, Error::ownership), "old actor generation cannot drive hatch");
    // Trial233: a rejection that did not say what it refused could not be traced to its message.
    check(refusal.back().packet.target == uint64_t(forged.kind) &&
              refusal.back().packet.target_generation == forged.player,
          "a rejection names the kind and the player of the message it refuses");
    auto ended = view;
    ended.hatch = {102, 1, 4, 0, 0, UINT32_MAX, 0, UINT32_MAX};
    check(fixture.send(1, ended).size() == 1, "hatch completion reaches owner");
    check(fixture.send(3, input).empty(), "closed hatch refuses previous lesson input");
    check(fixture.send(1, view).empty(), "older view cannot reopen completed hatch");
}

void late_fossil_tests() {
    using sporemp::native_scene_fossil_unmirrored;
    check(native_scene_fossil_unmirrored(EntityKind::fossil, true),
          "a fossil this client's world does not hold is left unmirrored, in the baseline or after it");
    check(!native_scene_fossil_unmirrored(EntityKind::fossil, false),
          "a fossil that fails for any other reason is still fatal");
    check(!native_scene_fossil_unmirrored(EntityKind::animal, true) &&
              !native_scene_fossil_unmirrored(EntityKind::nest, true),
          "a creature or nest without a template is still fatal");
    check(native_scene_fossil_unmirrored(EntityKind::fruit, true) &&
              !native_scene_fossil_unmirrored(EntityKind::fruit, false),
          "a fruit this client's world does not hold is left unmirrored too");
    // Trials 411 to 413: a fruit the authority placed took its new state on a client and stayed where it was put
    // away. The client blended the place over 50 ms, applied the first blend and then skipped the sample as seen.
    using sporemp::native_scene_sample_blend;
    check(native_scene_sample_blend(EntityKind::fruit, 0) == 1.0f &&
              native_scene_sample_blend(EntityKind::fossil, 3) == 1.0f &&
              native_scene_sample_blend(EntityKind::nest, 10) == 1.0f,
          "a resource applied once per sample takes the sample's own place at once");
    check(native_scene_sample_blend(EntityKind::animal, 0) == 0.0f &&
              native_scene_sample_blend(EntityKind::animal, 25) == 0.5f &&
              native_scene_sample_blend(EntityKind::animal, 50) == 1.0f &&
              native_scene_sample_blend(EntityKind::animal, 4000) == 1.0f,
          "a creature is still blended over 50 ms and no further");
    // Trial422: a fruit the authority held still lay 3 units lower on one client, whose own game had moved it after
    // the authority's place was applied. An unchanged sample is applied again from time to time.
    using sporemp::native_scene_static_recheck_due;
    check(!native_scene_static_recheck_due(EntityKind::fruit, 7, 1000, 1400),
          "a fruit applied a moment ago is not looked at again");
    check(native_scene_static_recheck_due(EntityKind::fruit, 0, 1000, 1500) &&
              native_scene_static_recheck_due(EntityKind::fruit, 0, 1000, 9000),
          "after half a second it is");
    check(!native_scene_static_recheck_due(EntityKind::fruit, 15, 1000, 1500) &&
              native_scene_static_recheck_due(EntityKind::fruit, 15, 1000, 1740),
          "fruits take their turns over a quarter of a second, by their numbers");
    check(!native_scene_static_recheck_due(EntityKind::fossil, 0, 1000, 9000) &&
              !native_scene_static_recheck_due(EntityKind::nest, 0, 1000, 9000) &&
              !native_scene_static_recheck_due(EntityKind::fruit, 0, 2000, 1000),
          "a fossil and a nest are not; nor is anything by a clock that went back");
}

void fruit_eaten_tests() {
    using sporemp::native_plant_food_left;
    // Trial383: after a restore the original re-placed the eaten fruit's noun 200 units away instead of removing it;
    // the creature had its whole meal and the clients kept the fruit.
    check(native_plant_food_left(false, false, false) && native_plant_food_left(false, false, true) &&
              native_plant_food_left(true, false, true) && !native_plant_food_left(true, true, true) &&
              !native_plant_food_left(true, true, false),
          "an ordered fruit is gone once its noun is, or once its orderer was fed and no fruit of that number lies at "
          "that place");
    // Trial391: a fruit on its plant shifted to another slot while a carnivore that eats none stood by it, and the
    // clients removed it.
    check(!native_plant_food_left(true, false, false),
          "a fruit that only moved, its orderer unfed, is still in the world");
}

void recharge_tests() {
    using sporemp::native_recharge_edges;
    using Bits = std::array<uint32_t, 3>;
    // Trial303's frames: the original draws a yellow wipe over an ability while it recharges; the client whose
    // creature it was drew none, its copy of the creature never having a recharging bit.
    const auto edges = native_recharge_edges(Bits{0x20u, 0, 0}, Bits{0x80u, 0, 1u});
    check(edges.started == Bits{0x80u, 0, 1u} && edges.finished == Bits{0x20u, 0, 0},
          "an ability that begins or ends its recharge on the authority does so on its owner's client");
    const auto same = native_recharge_edges(Bits{0xa0u, 0, 0}, Bits{0xa0u, 0, 0});
    check(same.started == Bits{} && same.finished == Bits{},
          "an ability still recharging is left to the original's own progress");
    using sporemp::native_recharge_tick_admitted;
    check(native_recharge_tick_admitted(true, true, true, false),
          "the original advances the recharge drawn on a client's own creature");
    check(
        !native_recharge_tick_admitted(false, true, true, false) &&
            !native_recharge_tick_admitted(true, false, true, false) &&
            !native_recharge_tick_admitted(true, true, false, false) &&
            !native_recharge_tick_admitted(true, true, true, true),
        "not on the authority, for another creature, with nothing recharging, or with other ability state to advance");
}

void screen_point_tests() {
    using sporemp::network::SteeringVector;
    using sporemp::network::screen_point;
    // A pinhole camera at (1,2,3) looking along +z; screen right is +x, screen down is -y; 500 pixels per unit of
    // slope.
    const SteeringVector camera{1, 2, 3};
    const float focal = 500, step = 100;
    auto ray = [&](float dx, float dy) {
        return SteeringVector{dx / focal, -dy / focal, 1};
    };
    auto at = [&](float dx, float dy, float depth) {
        auto r = ray(dx, dy);
        return SteeringVector{camera.x + r.x * depth, camera.y + r.y * depth, camera.z + r.z * depth};
    };
    float dx = 0, dy = 0;
    check(screen_point(camera, ray(0, 0), ray(step, 0), ray(0, step), step, at(120, -80, 7), dx, dy) &&
              std::abs(dx - 120) < 0.01f && std::abs(dy + 80) < 0.01f,
          "a point in view maps to its offset from the viewport centre");
    check(screen_point(camera, ray(0, 0), ray(step, 0), ray(0, step), step, at(120, -80, 300), dx, dy) &&
              std::abs(dx - 120) < 0.05f && std::abs(dy + 80) < 0.05f,
          "distance along the ray does not change the screen point");
    check(screen_point(camera, ray(0, 0), ray(step, 0), ray(0, step), step, at(0, 0, 2), dx, dy) &&
              std::abs(dx) < 0.001f && std::abs(dy) < 0.001f,
          "the point straight ahead is the viewport centre");
    check(!screen_point(camera, ray(0, 0), ray(step, 0), ray(0, step), step, at(120, -80, -7), dx, dy),
          "a point behind the camera has no screen point");
    check(!screen_point(camera, ray(0, 0), ray(0, 0), ray(0, step), step, at(1, 1, 7), dx, dy) &&
              !screen_point(camera, ray(0, 0), ray(step, 0), ray(0, step), 0, at(1, 1, 7), dx, dy),
          "degenerate rays or a zero step are refused");
}

void model_drift_tests() {
    using sporemp::native_scene_model_drifted;
    using sporemp::native_model_drift_limit;
    const float limit = native_model_drift_limit;
    check(native_scene_model_drifted(true, limit * 1.1f, 0),
          "a resting replica drawn beyond the limit from its position is relocated");
    check(!native_scene_model_drifted(true, limit * 0.9f, 0) && !native_scene_model_drifted(true, 0, 0),
          "a model within the limit is left alone");
    check(!native_scene_model_drifted(false, limit * 100, 0),
          "a moving replica is never relocated: its model legitimately trails or leads");
    check(!native_scene_model_drifted(true, std::numeric_limits<float>::quiet_NaN(), 0) &&
              !native_scene_model_drifted(true, std::numeric_limits<float>::infinity(), 0),
          "an unreadable gap relocates nothing");
    // Trial210: one wild creature's model stayed 1.93 units from its position through 81 relocations in three seconds.
    check(!native_scene_model_drifted(true, 1.93f, 1.93f) && !native_scene_model_drifted(true, 2.0f, 1.93f),
          "a gap that relocation itself leaves behind is not relocated again");
    check(native_scene_model_drifted(true, 2.5f, 1.93f), "drift beyond what relocation left is relocated");
}

void selection_guard_tests() {
    using sporemp::native_selection_allowed_by_awards;
    using sporemp::awards_busy_foreign;
    using sporemp::awards_busy_relationship_checkpoint;
    check(native_selection_allowed_by_awards(0), "with nothing busy a selection is allowed");
    // Trials 217 and 220: beside a foreign-species second player exactly these two parts refused, on every click.
    check(native_selection_allowed_by_awards(awards_busy_foreign | awards_busy_relationship_checkpoint) &&
              native_selection_allowed_by_awards(awards_busy_foreign),
          "a foreign second player and its checkpoint consistency do not refuse a selection");
    for (uint32_t bit = 1; bit <= 0x1000; bit <<= 1) {
        if (bit != awards_busy_foreign && bit != awards_busy_relationship_checkpoint) {
            check(!native_selection_allowed_by_awards(bit) &&
                      !native_selection_allowed_by_awards(bit | awards_busy_foreign),
                  "any reward activity or a missing context still refuses");
        }
    }
}

void unowned_movement_tests() {
    using sporemp::native_unowned_movement;
    using sporemp::UnownedMovement;
    using sporemp::native_unowned_stop_limit;
    check(native_unowned_movement(false, true, 0) == UnownedMovement::stop,
          "a player's creature moving before its owner's first command is stopped");
    check(native_unowned_movement(false, false, 0) == UnownedMovement::none, "a creature at rest is left alone");
    check(native_unowned_movement(true, true, 0) == UnownedMovement::none,
          "movement after the owner's first command is the owner's");
    check(native_unowned_movement(false, true, native_unowned_stop_limit - 1) == UnownedMovement::stop &&
              native_unowned_movement(false, true, native_unowned_stop_limit) == UnownedMovement::exhausted &&
              native_unowned_movement(false, true, native_unowned_stop_limit + 1) == UnownedMovement::exhausted,
          "movement that keeps restarting is reported instead of stopped forever");
    check(native_unowned_movement(true, true, native_unowned_stop_limit) == UnownedMovement::none,
          "the owner's first command ends the fence even after it gave up");
}

void client_request_tests() {
    using sporemp::native_client_avatar_request;
    using sporemp::ClientRequest;
    using sporemp::native_client_request_stop_limit;
    // Trial295: a second player's promoted creature kept a native walk on its own client, and the original camera
    // turned toward that walk's destination whenever the creature moved.
    check(native_client_avatar_request(0, 0) == ClientRequest::none,
          "a client's avatar without a native locomotion request is left alone");
    check(native_client_avatar_request(1, 0) == ClientRequest::clear &&
              native_client_avatar_request(20, native_client_request_stop_limit - 1) == ClientRequest::clear,
          "a native locomotion request on a client's own avatar is cleared");
    check(native_client_avatar_request(1, native_client_request_stop_limit) == ClientRequest::exhausted &&
              native_client_avatar_request(0, native_client_request_stop_limit) == ClientRequest::none,
          "a request that keeps coming back is reported instead of cleared forever");
}

void relationship_tests() {
    static_assert(uint32_t(Kind::history) == 27 && uint32_t(Kind::relationship) == 28,
                  "relationship preserves earlier packet kinds");
    RelationshipFrame frame;
    frame.entity = 102;
    frame.generation = 1;
    frame.revision = 3;
    frame.count = 2;
    frame.entries[0] = {{0x065d0fd2, 0x2b978c46, 0x408a0000}, {1, 1, 0, 1, 0xffffffff}};
    frame.entries[1] = {{0x065d0fd3, 0x2b978c46, 0x408a0000}, {2, 0, 3, 0, 7}};
    check(valid_relationship_frame(frame), "two ascending species entries form one complete snapshot");
    for (unsigned field = 0; field < 8; ++field) {
        auto bad = frame;
        if (field == 0) {
            bad.entity = 0;
        }
        if (field == 1) {
            bad.generation = 0;
        }
        if (field == 2) {
            bad.revision = 0;
        }
        if (field == 3) {
            bad.count = relationship_capacity + 1;
        }
        if (field == 4) {
            std::swap(bad.entries[0], bad.entries[1]);
        }
        if (field == 5) {
            bad.entries[1].key = bad.entries[0].key;
        }
        if (field == 6) {
            bad.entries[0].key = {};
        }
        if (field == 7) {
            bad.entries[2].words[3] = 1;
        }
        check(!valid_relationship_frame(bad),
              "missing identity, unordered, duplicate, empty-key or hidden entry refused");
    }
    RelationshipFrame none;
    none.entity = 102;
    none.generation = 1;
    none.revision = 1;
    check(valid_relationship_frame(none), "an owner with no relationship yet is an explicit complete snapshot");
    auto grown = frame;
    grown.entries[1].words[3] = 2;
    check(same_relationships(frame, frame) && !same_relationships(frame, grown) && !same_relationships(frame, none),
          "snapshots compare by every entry word");
    auto fewer = frame;
    fewer.count = 1;
    fewer.entries[1] = {};
    check(relationships_cover(frame, fewer) && relationships_cover(frame, none) && !relationships_cover(fewer, frame) &&
              !relationships_cover(none, fewer),
          "a snapshot applies only when it still names every species the client holds");
    Packet packet;
    packet.kind = Kind::relationship;
    packet.role = Role::authority;
    packet.player = 2;
    packet.sequence = 19;
    packet.session = 42;
    packet.scene = 3;
    packet.baseline = 7;
    packet.relationship = frame;
    packet.relationship.entity = 0xfedcba9876543210ull;
    packet.relationship.generation = 0xabcdef1234567890ull;
    packet.relationship.revision = 0x1020304050607080ull;
    packet.location = {1, 2, 3, 4};
    packet.worker_generation = 91;
    packet.native_load_slot = 1;
    const auto wire = encode(packet);
    Packet decoded;
    std::string error;
    check(decode(wire.data(), wire.size(), decoded, error) &&
              same_relationships(decoded.relationship, packet.relationship) &&
              decoded.relationship.entity == packet.relationship.entity &&
              decoded.relationship.generation == packet.relationship.generation &&
              decoded.relationship.revision == packet.relationship.revision && decoded.player == 2 &&
              decoded.sequence == 19 && decoded.session == 42 && decoded.scene == 3 && decoded.baseline == 7 &&
              decoded.location == packet.location && decoded.worker_generation == 91 && decoded.native_load_slot == 1,
          "relationship codec preserves every value and authority lifetime");
    for (size_t length = 0; length < wire.size(); ++length) {
        check(!decode(wire.data(), length, decoded, error), "truncated relationship snapshot cannot be applied");
    }
    {
        auto reserved = wire;
        reserved[wire.size() - 1] = 1;
        check(!decode(reserved.data(), reserved.size(), decoded, error), "relationship reserved tail refused");
    }
    auto old_schema = wire;
    old_schema[12] = 25;
    check(!decode(old_schema.data(), old_schema.size(), decoded, error),
          "older schema cannot decode added relationship kind");
    for (unsigned field = 0; field < 6; ++field) {
        auto bad = packet;
        if (field == 0) {
            bad.player = 1;
        }
        if (field == 1) {
            bad.role = Role::player;
        }
        if (field == 2) {
            bad.count = 1;
        }
        if (field == 3) {
            bad.error = Error::stale;
        }
        if (field == 4) {
            bad.native_load_slot = 2;
        }
        if (field == 5) {
            bad.relationship.entries[1].key = bad.relationship.entries[0].key;
        }
        const auto bytes = encode(bad);
        check(!decode(bytes.data(), bytes.size(), decoded, error),
              "unsupported relationship envelope or state refused");
    }

    Fixture fixture;
    fixture.join(1, Role::authority);
    fixture.join(2, Role::player, 1);
    fixture.join(3, Role::player, 2);
    const auto initial = fixture.baseline();
    const auto a = baseline_id(initial, 2), b = baseline_id(initial, 3);
    packet.scene = 1;
    packet.baseline = 1;
    packet.location = {};
    packet.worker_generation = 0;
    packet.native_load_slot = 0;
    packet.relationship = frame;
    check(fixture.send(1, packet).empty(), "relationship snapshot waits for owner baseline readiness");
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 1;
    ack.baseline = a;
    fixture.send(2, ack);
    ack.baseline = b;
    fixture.send(3, ack);
    auto delivered = fixture.send(1, packet);
    check(delivered.size() == 1 && delivered[0].connection == 3 && delivered[0].packet.baseline == b &&
              same_relationships(delivered[0].packet.relationship, frame) &&
              delivered[0].packet.relationship.revision == 3,
          "B's relationships reach only B, under its current baseline");
    const auto repeated = fixture.send(1, packet);
    check(repeated.size() == 1 && repeated[0].connection == 3 && repeated[0].packet.relationship.revision == 3,
          "periodic resync repeats the same revision instead of a new event");
    for (unsigned field = 0; field < 6; ++field) {
        auto bad = packet;
        if (field == 0) {
            bad.player = 1;
        }
        if (field == 1) {
            bad.relationship.entity = 101;
        }
        if (field == 2) {
            bad.relationship.generation = 2;
        }
        if (field == 3) {
            bad.relationship.entity = 999;
        }
        if (field == 4) {
            bad.role = Role::player;
        }
        if (field == 5) {
            bad.relationship.count = relationship_capacity + 1;
        }
        check(rejected(fixture.send(1, bad), Error::ownership),
              "relationship authority input requires the exact owner and a valid snapshot");
    }
    auto forged = packet;
    forged.role = Role::player;
    forged.baseline = b;
    check(rejected(fixture.send(3, forged), Error::ownership), "B cannot author its own relationships");
    forged.player = 1;
    forged.baseline = a;
    check(rejected(fixture.send(2, forged), Error::ownership), "A cannot author another owner's relationships");
    for (unsigned field = 0; field < 5; ++field) {
        auto stale = packet;
        if (field == 0) {
            stale.scene = 2;
        }
        if (field == 1) {
            stale.baseline = 2;
        }
        if (field == 2) {
            stale.location.area = 1;
        }
        if (field == 3) {
            stale.worker_generation = 1;
        }
        if (field == 4) {
            stale.native_load_slot = 1;
        }
        check(rejected(fixture.send(1, stale), Error::stale),
              "relationship scene, location, worker and source baseline are fenced");
    }
    fixture.session.disconnect(3);
    const auto joined = fixture.join(4, Role::player, 2);
    const auto fresh = baseline_id(joined, 4);
    check(fresh > b && fixture.send(1, packet).empty(),
          "reconnect waits for a fresh entity baseline before a relationship resync");
    ack.baseline = fresh;
    fixture.send(4, ack);
    delivered = fixture.send(1, packet);
    check(delivered.size() == 1 && delivered[0].connection == 4 && delivered[0].packet.baseline == fresh &&
              same_relationships(delivered[0].packet.relationship, frame),
          "fresh ready B receives the unchanged full snapshot");
}

void history_tests() {
    static_assert(uint32_t(Kind::inventory) == 24 && uint32_t(Kind::hatch_view) == 25 &&
                      uint32_t(Kind::hatch_input) == 26 && uint32_t(Kind::history) == 27,
                  "history preserves earlier packet kinds");
    HistoryState state;
    state.date = {0x11223344, 0x55667788};
    state.count = 1;
    state.entries[0].header = {0x99aabbcc, 0xddeeff00, 0};
    state.entries[0].data = {173, 0x12345678, 0x87654321, 0x10203040, 5, 0xfedcba98,
                             1,   1,          1,          0xabcdef12, 0, 0xc0600000};
    check(valid_history(state), "first-kill history preserves complete original scalar values");
    Packet packet;
    packet.kind = Kind::history;
    packet.role = Role::authority;
    packet.player = 2;
    packet.sequence = 19;
    packet.session = 42;
    packet.scene = 3;
    packet.baseline = 7;
    packet.history = {0xfedcba9876543210ull, 0xabcdef1234567890ull, 0x1020304050607080ull, state};
    packet.location = {1, 2, 3, 4};
    packet.worker_generation = 91;
    packet.native_load_slot = 1;
    const auto wire = encode(packet);
    Packet decoded;
    std::string error;
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.history.state == state &&
              decoded.history.entity == packet.history.entity &&
              decoded.history.generation == packet.history.generation &&
              decoded.history.revision == packet.history.revision && decoded.player == 2 && decoded.sequence == 19 &&
              decoded.session == 42 && decoded.scene == 3 && decoded.baseline == 7 &&
              decoded.location == packet.location && decoded.worker_generation == 91 && decoded.native_load_slot == 1,
          "history codec preserves every value and authority lifetime");
    for (size_t length = 0; length < wire.size(); ++length) {
        check(!decode(wire.data(), length, decoded, error), "truncated history cannot become an applied snapshot");
    }
    for (unsigned field = 0; field < 24; ++field) {
        auto bad = packet;
        if (field == 0) {
            bad.role = Role::player;
        }
        if (field == 1) {
            bad.player = 1;
        }
        if (field == 2) {
            bad.player = 3;
        }
        if (field == 3) {
            bad.session = 0;
        }
        if (field == 4) {
            bad.scene = 0;
        }
        if (field == 5) {
            bad.baseline = 0;
        }
        if (field == 6) {
            bad.history.entity = 0;
        }
        if (field == 7) {
            bad.history.generation = 0;
        }
        if (field == 8) {
            bad.history.revision = 0;
        }
        if (field == 9) {
            bad.history.state.count = 2;
        }
        if (field == 10) {
            bad.history.state.entries[0].header[2] = 1;
        }
        if (field >= 11 && field <= 13) {
            bad.history.state.entries[0].data[field - 10] = 0;
        }
        if (field == 14) {
            bad.history.state.entries[0].data[4] = 7;
        }
        if (field == 15) {
            bad.history.state.entries[0].data[6] = 0;
        }
        if (field == 16) {
            bad.history.state.entries[0].data[7] = 0;
        }
        if (field == 17) {
            bad.history.state.entries[0].data[7] = 4;
        }
        if (field == 18) {
            bad.history.state.entries[0].data[8] = 3;
        }
        if (field == 19) {
            bad.history.state.entries[0].data[10] = 2;
        }
        if (field == 20) {
            bad.history.state.entries[0].data[11] = 0x7fc00000;
        }
        if (field == 21) {
            bad.native_load_slot = 2;
        }
        if (field == 22) {
            bad.count = 1;
        }
        if (field == 23) {
            bad.error = Error::stale;
        }
        const auto bytes = encode(bad);
        check(!decode(bytes.data(), bytes.size(), decoded, error), "unsupported history state or envelope refused");
    }
    for (uint32_t classification : {2u, 3u}) {
        auto native = packet;
        native.history.state.entries[0].data[7] = classification;
        native.history.state.entries[0].data[10] = 1;
        const auto bytes = encode(native);
        check(decode(bytes.data(), bytes.size(), decoded, error) && decoded.history.state == native.history.state,
              "original native classification and mode remain scalar values");
    }
    for (uint32_t nonfinite : {0x7f800000u, 0xff800000u, 0xffc00001u}) {
        auto bad = state;
        bad.entries[0].data[11] = nonfinite;
        check(!valid_history(bad), "nonfinite native history float refused without changing finite bits");
    }
    for (size_t offset : {size_t(212), wire.size() - 1}) {
        auto reserved = wire;
        reserved[offset] = 1;
        check(!decode(reserved.data(), reserved.size(), decoded, error), "history reserved tail refused");
    }
    auto old_schema = wire;
    old_schema[12] = 24;
    check(!decode(old_schema.data(), old_schema.size(), decoded, error),
          "older schema cannot decode added history kind");
    HistoryState empty;
    empty.date = state.date;
    packet.history.state = empty;
    auto bytes = encode(packet);
    check(decode(bytes.data(), bytes.size(), decoded, error) && decoded.history.state == empty,
          "empty owner history is an explicit complete snapshot with original root date");
    for (size_t i = 0; i < empty.entries[0].header.size(); ++i) {
        auto bad = empty;
        bad.entries[0].header[i] = 1;
        check(!valid_history(bad), "empty history cannot hide an entry header");
    }
    for (size_t i = 0; i < empty.entries[0].data.size(); ++i) {
        auto bad = empty;
        bad.entries[0].data[i] = 1;
        check(!valid_history(bad), "empty history cannot hide event payload");
    }
    // Trial315: the second species player 2 hunted added a second entry, which the one-entry form refused.
    HistoryState two = state;
    two.count = 2;
    two.entries[1] = state.entries[0];
    two.entries[1].data[0] = 3555;
    two.entries[1].data[2] = 0x87654322;
    check(valid_history(two) && history_extends(empty, state) && history_extends(state, two) &&
              history_extends(empty, two) && history_extends(two, two),
          "a history grows by whole entries added after the ones it had");
    {
        auto changed = two;
        changed.entries[0].data[9] ^= 1;
        auto dated = two;
        dated.date[0] ^= 1;
        check(!history_extends(two, state) && !history_extends(state, changed) && !history_extends(state, dated),
              "a history never loses an entry, rewrites one or changes its date");
    }
    {
        auto hole = state;
        hole.count = 2;
        auto hidden = state;
        hidden.entries[1] = state.entries[0];
        check(!valid_history(hole) && !valid_history(hidden),
              "every counted entry is complete and nothing lies beyond the count");
    }
    {
        HistoryState full = state;
        full.count = history_capacity;
        for (uint32_t k = 1; k < history_capacity; ++k) {
            full.entries[k] = state.entries[0];
            full.entries[k].data[2] += k;
        }
        auto over = full;
        over.count = history_capacity + 1;
        check(valid_history(full) && !valid_history(over), "a history holds up to its capacity and no more");
        auto large = packet;
        large.history.state = full;
        const auto large_wire = encode(large);
        Packet large_decoded;
        check(decode(large_wire.data(), large_wire.size(), large_decoded, error) && large_decoded.history.state == full,
              "a full history fits one packet and survives the codec");
    }
    {
        auto second = packet;
        second.history.state = two;
        const auto second_wire = encode(second);
        Packet second_decoded;
        check(decode(second_wire.data(), second_wire.size(), second_decoded, error) &&
                  second_decoded.history.state == two,
              "a two-entry history survives the codec");
    }

    Fixture fixture;
    fixture.join(1, Role::authority);
    fixture.join(2, Role::player, 1);
    fixture.join(3, Role::player, 2);
    const auto initial = fixture.baseline();
    const auto a = baseline_id(initial, 2), b = baseline_id(initial, 3);
    packet.scene = 1;
    packet.baseline = 1;
    packet.location = {};
    packet.worker_generation = 0;
    packet.native_load_slot = 0;
    packet.history = {102, 1, 1, empty};
    check(fixture.send(1, packet).empty(), "empty history waits for owner baseline readiness");
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 1;
    ack.baseline = a;
    fixture.send(2, ack);
    ack.baseline = b;
    fixture.send(3, ack);
    auto delivered = fixture.send(1, packet);
    check(delivered.size() == 1 && delivered[0].connection == 3 && delivered[0].packet.baseline == b &&
              delivered[0].packet.history.state == empty,
          "empty history reaches B only under its current baseline");
    packet.history.revision = 2;
    packet.history.state = state;
    delivered = fixture.send(1, packet);
    check(delivered.size() == 1 && delivered[0].connection == 3 && delivered[0].packet.history.state == state,
          "complete original first-kill history reaches B without a living NPC target");
    const auto repeated = fixture.send(1, packet);
    check(repeated.size() == 1 && repeated[0].packet.history.revision == 2 && repeated[0].packet.history.state == state,
          "periodic resync preserves revision and values instead of emitting a new gameplay event");
    for (unsigned field = 0; field < 8; ++field) {
        auto bad = packet;
        if (field == 0) {
            bad.player = 1;
        }
        if (field == 1) {
            bad.history.entity = 101;
        }
        if (field == 2) {
            bad.history.generation = 2;
        }
        if (field == 3) {
            bad.history.entity = 999;
        }
        if (field == 4) {
            bad.role = Role::player;
        }
        if (field == 5) {
            bad.history.state.count = 2;
        }
        if (field == 6) {
            bad.count = 1;
        }
        if (field == 7) {
            bad.error = Error::stale;
        }
        check(rejected(fixture.send(1, bad), Error::ownership),
              "history authority input still requires exact owner and valid state");
    }
    auto forged = packet;
    forged.role = Role::player;
    forged.baseline = b;
    check(rejected(fixture.send(3, forged), Error::ownership), "B cannot author its own history reward");
    forged.player = 1;
    forged.baseline = a;
    check(rejected(fixture.send(2, forged), Error::ownership), "A cannot author another owner history");
    for (unsigned field = 0; field < 5; ++field) {
        auto stale = packet;
        if (field == 0) {
            stale.scene = 2;
        }
        if (field == 1) {
            stale.baseline = 2;
        }
        if (field == 2) {
            stale.location.area = 1;
        }
        if (field == 3) {
            stale.worker_generation = 1;
        }
        if (field == 4) {
            stale.native_load_slot = 1;
        }
        check(rejected(fixture.send(1, stale), Error::stale),
              "history scene location worker and source baseline are fenced");
    }
    auto wrong_session = packet;
    wrong_session.session = 43;
    wrong_session.sequence = ++fixture.seq[1];
    check(rejected(fixture.session.receive(1, wrong_session), Error::stale), "history cannot cross session epochs");
    fixture.session.disconnect(3);
    const auto joined = fixture.join(4, Role::player, 2);
    const auto fresh = baseline_id(joined, 4);
    bool replay = false;
    for (const auto& dispatch : joined) {
        replay |= dispatch.packet.kind == Kind::history;
    }
    check(fresh > b && !replay && fixture.send(1, packet).empty(),
          "reconnect waits for fresh entity baseline before history resync");
    ack.baseline = b;
    fixture.send(4, ack);
    check(fixture.send(1, packet).empty(), "stale reconnect ACK cannot admit a history snapshot");
    ack.baseline = fresh;
    fixture.send(4, ack);
    delivered = fixture.send(1, packet);
    check(delivered.size() == 1 && delivered[0].connection == 4 && delivered[0].packet.baseline == fresh &&
              delivered[0].packet.history.revision == 2 && delivered[0].packet.history.state == state,
          "fresh ready B receives unchanged full history without reward or event replay");
    Packet death;
    death.kind = Kind::motion;
    death.scene = 1;
    death.baseline = 1;
    death.entity = entity(102, 2);
    death.entity.tick = 2;
    death.entity.life_state = 1;
    death.entity.health = 0;
    fixture.send(1, death);
    check(fixture.send(1, packet).size() == 1,
          "owner history remains readable after owner death while its incarnation exists");
    check(fixture.session.begin_transfer_maintenance(91), "history test acquires existing maintenance fence");
    check(fixture.send(1, packet).empty(),
          "in-flight history drains during maintenance without restoring subscriber readiness");
    auto invalid = packet;
    invalid.history.state.count = 2;
    check(rejected(fixture.send(1, invalid), Error::not_ready),
          "maintenance does not silently drain malformed history");
}

void inventory_tests() {
    InventoryState state;
    state.group = 900;
    state.points = 7;
    for (uint32_t i = 0; i < 49; ++i) {
        state.items.push_back({100 + i, 900, i < 2 ? 3u : 0u, i < 2 ? i : UINT32_MAX});
    }
    check(valid_inventory(state), "observed inventory order is complete");
    auto chunk = [&](uint32_t offset) {
        InventoryFrame frame;
        frame.entity = 102;
        frame.generation = 1;
        frame.revision = 8;
        frame.total = 49;
        frame.offset = offset;
        frame.count = std::min(inventory_chunk_items, 49 - offset);
        frame.points = 7;
        frame.group = 900;
        std::copy_n(state.items.begin() + offset, frame.count, frame.items.begin());
        return frame;
    };
    Packet packet;
    packet.kind = Kind::inventory;
    packet.role = Role::authority;
    packet.player = 2;
    packet.session = 42;
    packet.scene = 1;
    packet.baseline = 1;
    packet.inventory = chunk(0);
    packet.location = {1, 2, 3, 4};
    packet.worker_generation = 51;
    const auto wire = encode(packet);
    Packet decoded;
    std::string error;
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.inventory.items == packet.inventory.items &&
              decoded.inventory.revision == 8 && decoded.location == packet.location && decoded.worker_generation == 51,
          "inventory wire preserves items, revision and lifetime");
    for (unsigned field = 0; field < 8; ++field) {
        auto bad = packet;
        if (field == 0) {
            bad.role = Role::player;
        }
        if (field == 1) {
            bad.player = 3;
        }
        if (field == 2) {
            bad.inventory.count = 25;
        }
        if (field == 3) {
            bad.inventory.total = 1025;
        }
        if (field == 4) {
            bad.inventory.offset = 1;
        }
        if (field == 5) {
            bad.inventory.revision = 0;
        }
        if (field == 6) {
            bad.inventory.items[0].status = 8;
        }
        if (field == 7) {
            bad.inventory.items[0].index = 49;
        }
        const auto bytes = encode(bad);
        check(!decode(bytes.data(), bytes.size(), decoded, error), "invalid inventory envelope or chunk refused");
    }
    auto reserved = wire;
    reserved.back() = 1;
    check(!decode(reserved.data(), reserved.size(), decoded, error), "inventory reserved tail refused");
    InventoryAssembly assembly;
    InventoryState result;
    check(assembly.receive(chunk(24), result) == InventoryReceipt::ignored && result.items.empty(),
          "mid-publication subscriber waits for start");
    check(assembly.receive(chunk(0), result) == InventoryReceipt::partial && result.items.empty(),
          "partial inventory cannot be applied");
    check(assembly.receive(chunk(24), result) == InventoryReceipt::partial && result.items.empty(),
          "second partial chunk remains unapplied");
    check(assembly.receive(chunk(48), result) == InventoryReceipt::complete && result == state,
          "complete inventory assembles exact observed order");
    for (uint32_t offset : {0u, 24u, 48u}) {
        check(assembly.receive(chunk(offset), result) == InventoryReceipt::ignored,
              "retransmission cannot replay notifications");
    }
    assembly.reset();
    assembly.receive(chunk(0), result);
    check(assembly.receive(chunk(48), result) == InventoryReceipt::invalid,
          "missing middle chunk cannot replace inventory");
    auto duplicate = state;
    duplicate.items[1].index = 0;
    check(!valid_inventory(duplicate), "duplicate unlocked index refused");
    duplicate = state;
    duplicate.items[1] = duplicate.items[0];
    check(!valid_inventory(duplicate), "duplicate part identity refused");
    Fixture fixture;
    fixture.join(1, Role::authority);
    fixture.join(2, Role::player, 1);
    fixture.join(3, Role::player, 2);
    const auto initial = fixture.baseline();
    const auto a = baseline_id(initial, 2), b = baseline_id(initial, 3);
    packet.location = {};
    packet.worker_generation = 0;
    check(fixture.send(1, packet).empty(), "inventory waits for owning client readiness");
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 1;
    ack.baseline = a;
    fixture.send(2, ack);
    ack.baseline = b;
    fixture.send(3, ack);
    const auto delivered = fixture.send(1, packet);
    check(delivered.size() == 1 && delivered[0].connection == 3 && delivered[0].packet.baseline == b,
          "B inventory only reaches B under its current baseline");
    auto bad = packet;
    bad.player = 1;
    check(rejected(fixture.send(1, bad), Error::ownership), "inventory owner mismatch refused");
    bad = packet;
    bad.inventory.generation = 2;
    check(rejected(fixture.send(1, bad), Error::ownership), "inventory stale actor generation refused");
    bad = packet;
    bad.role = Role::player;
    bad.baseline = b;
    check(rejected(fixture.send(3, bad), Error::ownership), "player cannot publish an inventory reward");
}

void death_tests(uint32_t reason) {
    Packet p;
    p.kind = Kind::death;
    p.role = Role::authority;
    p.sequence = 1;
    p.session = 42;
    p.scene = 3;
    p.baseline = 7;
    p.player = 1;
    p.death = {0xfedcba9876543210ull, 0xabcdef1234567890ull, 0x1020304050607080ull, reason};
    p.location = {1, 2, 3, 4};
    p.worker_generation = 91;
    p.native_load_slot = 1;
    const auto wire = encode(p);
    Packet decoded;
    std::string error;
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.death == p.death && decoded.player == 1 &&
              decoded.location == p.location && decoded.worker_generation == 91 && decoded.native_load_slot == 1,
          "death_wire_preserves_occurrence_and_all_lifetimes");
    for (size_t n = 0; n < wire.size(); ++n) {
        check(!decode(wire.data(), n, decoded, error), "truncated_death_refused");
    }
    for (unsigned field = 0; field < 13; ++field) {
        auto bad = p;
        if (field == 0) {
            bad.death.occurrence = 0;
        }
        if (field == 1) {
            bad.death.entity = 0;
        }
        if (field == 2) {
            bad.death.generation = 0;
        }
        if (field == 3) {
            bad.death.reason = 9;
        }
        if (field == 4) {
            bad.role = Role::player;
        }
        if (field == 5) {
            bad.player = 0;
        }
        if (field == 6) {
            bad.player = 3;
        }
        if (field == 7) {
            bad.session = 0;
        }
        if (field == 8) {
            bad.scene = 0;
        }
        if (field == 9) {
            bad.baseline = 0;
        }
        if (field == 10) {
            bad.count = 1;
        }
        if (field == 11) {
            bad.error = Error::stale;
        }
        if (field == 12) {
            bad.native_load_slot = 2;
        }
        const auto bytes = encode(bad);
        check(!decode(bytes.data(), bytes.size(), decoded, error), "invalid_death_payload_or_envelope_refused");
    }
    auto reserved = wire;
    reserved.back() = 1;
    check(!decode(reserved.data(), reserved.size(), decoded, error), "death_reserved_bytes_refused");
    p.kind = Kind::death_done;
    p.role = Role::player;
    const auto done_wire = encode(p);
    check(decode(done_wire.data(), done_wire.size(), decoded, error) && decoded.death == p.death,
          "death_confirmation_roundtrips_with_separate_role");
    Fixture f;
    f.join(1, Role::authority);
    f.join(2, Role::player, 1);
    f.join(3, Role::player, 2);
    const auto initial = f.baseline();
    const auto a = baseline_id(initial, 2), b = baseline_id(initial, 3);
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 1;
    ack.baseline = b;
    f.send(3, ack);
    Packet notice;
    notice.kind = Kind::death;
    notice.role = Role::authority;
    notice.scene = 1;
    notice.baseline = 1;
    notice.player = 1;
    notice.death = {1, 101, 1, reason};
    check(rejected(f.send(1, notice), Error::ownership), "live_actor_cannot_offer_death_confirmation");
    Packet state;
    state.kind = Kind::motion;
    state.scene = 1;
    state.baseline = 1;
    state.entity = entity(101, 1);
    state.entity.tick = 2;
    state.entity.health = 0;
    state.entity.life_state = 1;
    f.send(1, state);
    check(f.send(1, notice).empty(), "death_waits_for_owning_client_baseline_ack");
    ack.baseline = a;
    f.send(2, ack);
    auto output = f.send(1, notice);
    check(output.size() == 1 && output[0].connection == 2 && output[0].packet.baseline == a &&
              output[0].packet.death == notice.death,
          "death_only_reaches_ready_dead_actor_owner");
    check(f.send(1, notice).size() == 1, "same_pending_death_can_retry_without_new_occurrence");
    Packet done = notice;
    done.kind = Kind::death_done;
    done.role = Role::player;
    done.baseline = a;
    output = f.send(2, done);
    check(output.size() == 1 && output[0].connection == 1 && output[0].packet.baseline == 1 &&
              output[0].packet.player == 1 && output[0].packet.death == notice.death,
          "observed_restart_confirmation_returns_to_authority_without_mutating_actor");
    for (unsigned field = 0; field < 6; ++field) {
        auto bad = done;
        if (field == 0) {
            bad.player = 2;
        }
        if (field == 1) {
            bad.death.entity = 102;
        }
        if (field == 2) {
            bad.death.generation++;
        }
        if (field == 3) {
            bad.death.occurrence++;
        }
        if (field == 4) {
            bad.role = Role::authority;
        }
        if (field == 5) {
            bad.death.reason = reason == 2 ? 5 : 2;
        }
        check(rejected(f.send(2, bad), Error::ownership),
              "death_confirmation_cannot_forge_owner_occurrence_or_incarnation");
    }
    auto wrong = done;
    wrong.baseline = b;
    check(rejected(f.send(3, wrong), Error::ownership), "other_player_cannot_confirm_death");
    wrong = notice;
    wrong.baseline = a;
    check(rejected(f.send(2, wrong), Error::ownership), "client_cannot_offer_native_death");
    f.session.disconnect(2);
    const auto reconnect = f.join(4, Role::player, 1);
    const auto fresh = baseline_id(reconnect, 4);
    check(fresh > a && f.send(1, notice).empty(), "death_offer_waits_for_reconnected_owner_baseline");
    ack.baseline = fresh;
    f.send(4, ack);
    check(f.send(4, done).empty(), "old_baseline_death_confirmation_is_ignored");
    output = f.send(1, notice);
    check(output.size() == 1 && output[0].connection == 4 && output[0].packet.baseline == fresh,
          "pending_native_death_survives_owner_disconnect");
    done.baseline = fresh;
    check(f.send(4, done).size() == 1, "reconnected_owner_can_confirm_same_native_occurrence");
    state.entity.tick++;
    state.entity.life_state = 0;
    state.entity.health = 100;
    f.send(1, state);
    check(f.send(4, done).empty(), "duplicate_confirmation_after_native_revival_is_ignored");
    state.entity.tick++;
    state.entity.life_state = 1;
    state.entity.health = 0;
    f.send(1, state);
    notice.death.occurrence++;
    check(f.send(1, notice).size() == 1, "next_native_death_can_offer_new_occurrence_for_same_actor");
    check(rejected(f.send(4, done), Error::ownership), "previous_death_confirmation_cannot_revive_later_death");
    wrong = notice;
    wrong.death.occurrence--;
    check(rejected(f.send(1, wrong), Error::stale), "old_occurrence_cannot_replace_new_native_death");
    check(f.session.begin_transfer_maintenance(17), "death_fixture_enters_transfer_maintenance");
    check(f.send(1, notice).empty(), "in_flight_death_offer_is_drained_during_transfer");
    wrong = notice;
    wrong.worker_generation = 99;
    check(rejected(f.send(1, wrong), Error::stale), "wrong_worker_cannot_offer_death_during_maintenance");
    f.session.disconnect(1);
    check(f.send(1, notice).empty(), "disconnected_authority_cannot_offer_death");
}

void social_view_tests() {
    Packet p;
    p.kind = Kind::social_view;
    p.role = Role::authority;
    p.sequence = 1;
    p.session = 42;
    p.scene = 3;
    p.baseline = 7;
    p.player = 2;
    p.social = {0xfedcba9876543210ull,
                0xabcdef1234567890ull,
                0x1020304050607080ull,
                0x1122334455667788ull,
                1,
                7,
                2,
                {0.125f, 1.25f, -0.5f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f}};
    p.social.relation = 4;
    p.social.segment_count = {1, 1};
    p.social.segments[0][0] = {0.5f, 1, 0xff7bcc76, 1, 0.25f};
    p.social.segments[1][0] = {1.2f, 2, 0xff7662cf, 0, 0.5f};
    p.location = {1, 2, 3, 4};
    p.worker_generation = 91;
    p.native_load_slot = 1;
    const auto wire = encode(p);
    Packet decoded;
    std::string error;
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.social == p.social && decoded.player == 2 &&
              decoded.location == p.location && decoded.worker_generation == 91 && decoded.native_load_slot == 1,
          "social_meter_wire_preserves_native_values_and_lifetimes");
    for (size_t n = 0; n < wire.size(); ++n) {
        check(!decode(wire.data(), n, decoded, error), "truncated_social_meter_refused");
    }
    for (unsigned field = 0; field < 24; ++field) {
        auto bad = p;
        if (field == 0) {
            bad.social.entity = 0;
        }
        if (field == 1) {
            bad.social.generation = 0;
        }
        if (field == 2) {
            bad.social.target = 0;
        }
        if (field == 3) {
            bad.social.target_generation = 0;
        }
        if (field == 4) {
            bad.social.entity = bad.social.target;
        }
        if (field == 5) {
            bad.social.visible = 2;
        }
        if (field == 6) {
            bad.social.flags = 8;
        }
        if (field == 7) {
            bad.social.mode = 3;
        }
        if (field == 8) {
            bad.social.values[0] = std::numeric_limits<float>::quiet_NaN();
        }
        if (field == 9) {
            bad.social.values[7] = std::numeric_limits<float>::infinity();
        }
        if (field == 10) {
            bad.social.values[3] = -65;
        }
        if (field == 11) {
            bad.role = Role::player;
        }
        if (field == 12) {
            bad.player = 3;
        }
        if (field == 13) {
            bad.session = 0;
        }
        if (field == 14) {
            bad.scene = 0;
        }
        if (field == 15) {
            bad.baseline = 0;
        }
        if (field == 16) {
            bad.count = 1;
        }
        if (field == 17) {
            bad.error = Error::stale;
        }
        if (field == 18) {
            bad.native_load_slot = 2;
        }
        if (field == 19) {
            bad.social.relation = 0;
        }
        if (field == 20) {
            bad.social.relation = 7;
        }
        if (field == 21) {
            bad.social.segment_count[0] = 7;
        }
        if (field == 22) {
            bad.social.segments[1][0].duration = std::numeric_limits<float>::quiet_NaN();
        }
        if (field == 23) {
            bad.social.segments[0][5].speed = 0.25f;
        }
        const auto bytes = encode(bad);
        check(!decode(bytes.data(), bytes.size(), decoded, error), "invalid_social_meter_or_identity_refused");
    }
    auto reserved = wire;
    reserved.back() = 1;
    check(!decode(reserved.data(), reserved.size(), decoded, error), "social_meter_reserved_bytes_refused");
    Fixture f;
    f.join(1, Role::authority);
    f.join(2, Role::player, 1);
    f.join(3, Role::player, 2);
    const auto initial = f.baseline();
    const auto a = baseline_id(initial, 2), b = baseline_id(initial, 3);
    Packet npc;
    npc.kind = Kind::entity;
    npc.entity = entity(103, 0);
    npc.scene = 1;
    npc.baseline = 1;
    f.send(1, npc);
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 1;
    ack.baseline = a;
    f.send(2, ack);
    Packet view;
    view.kind = Kind::social_view;
    view.role = Role::authority;
    view.scene = 1;
    view.baseline = 1;
    view.player = 2;
    view.social = {102, 1, 103, 1, 1, 1, 0, {0, 0, 0, 0, 1, 0, 0, 1}};
    view.social.relation = 4;
    check(f.send(1, view).empty(), "social_meter_waits_for_owning_client_baseline_ack");
    ack.baseline = b;
    f.send(3, ack);
    auto output = f.send(1, view);
    check(output.size() == 1 && output[0].connection == 3 && output[0].packet.baseline == b &&
              output[0].packet.social == view.social,
          "social_meter_reaches_only_ready_owner_with_current_baseline");
    auto bad = view;
    bad.baseline = b;
    check(rejected(f.send(3, bad), Error::ownership), "player_cannot_publish_social_meter");
    for (unsigned field = 0; field < 5; ++field) {
        bad = view;
        if (field == 0) {
            bad.player = 1;
        }
        if (field == 1) {
            bad.social.generation++;
        }
        if (field == 2) {
            bad.social.target_generation++;
        }
        if (field == 3) {
            bad.social.target = 101;
        }
        if (field == 4) {
            bad.social.target = 999;
        }
        check(rejected(f.send(1, bad), Error::ownership),
              "social_meter_requires_live_owned_actor_and_exact_npc_incarnation");
    }
    f.session.disconnect(3);
    const auto reconnect = f.join(4, Role::player, 2);
    const auto fresh = baseline_id(reconnect, 4);
    bool replay = false;
    for (const auto& item : reconnect) {
        replay |= item.packet.kind == Kind::social_view;
    }
    check(!replay && fresh > b && f.send(1, view).empty(), "ephemeral_social_meter_is_not_replayed_on_reconnect");
    ack.baseline = fresh;
    f.send(4, ack);
    output = f.send(1, view);
    check(output.size() == 1 && output[0].connection == 4 && output[0].packet.baseline == fresh,
          "fresh_social_meter_can_resume_after_baseline_application");
    npc.kind = Kind::despawn;
    npc.entity.tick++;
    f.send(1, npc);
    check(rejected(f.send(1, view), Error::ownership), "despawned_target_cannot_receive_social_meter");
    npc.kind = Kind::entity;
    npc.entity.generation++;
    npc.entity.tick++;
    f.send(1, npc);
    check(rejected(f.send(1, view), Error::ownership), "reused_target_id_does_not_reuse_social_meter_lifetime");
    view.social.target_generation++;
    check(f.send(1, view).size() == 1, "new_native_target_incarnation_can_publish_fresh_view");
    check(f.session.begin_transfer_maintenance(17), "social_meter_fixture_enters_transfer_maintenance");
    check(f.send(1, view).empty(), "in_flight_meter_is_dropped_during_maintenance_without_rejecting_authority");
    bad = view;
    bad.worker_generation = 99;
    check(rejected(f.send(1, bad), Error::stale), "maintenance_still_rejects_wrong_worker_generation");
    f.session.disconnect(1);
    check(f.send(1, view).empty(), "disconnected_authority_cannot_publish_old_meter");
}

void tutorial_tests() {
    Packet p;
    p.kind = Kind::tutorial;
    p.role = Role::authority;
    p.sequence = 1;
    p.session = 42;
    p.scene = 3;
    p.baseline = 7;
    p.player = 2;
    p.tutorial = {0xfedcba9876543210ull, 0xabcdef1234567890ull, 0xea7666af, 0x061d8045};
    p.location = {1, 2, 3, 4};
    p.worker_generation = 91;
    p.native_load_slot = 1;
    const auto good = encode(p);
    Packet decoded;
    std::string error;
    check(decode(good.data(), good.size(), decoded, error) && decoded.tutorial == p.tutorial && decoded.player == 2 &&
              decoded.location == p.location && decoded.worker_generation == 91 && decoded.native_load_slot == 1,
          "owner_tutorial_wire_preserves_all_lifetimes");
    for (size_t length = 0; length < good.size(); ++length) {
        check(!decode(good.data(), length, decoded, error), "truncated_tutorial_refused");
    }
    for (unsigned field = 0; field < 10; ++field) {
        auto bad = p;
        if (field == 0) {
            bad.tutorial.entity = 0;
        }
        if (field == 1) {
            bad.tutorial.generation = 0;
        }
        if (field == 2) {
            bad.tutorial.token++;
        }
        if (field == 3) {
            bad.tutorial.caption++;
        }
        if (field == 4) {
            bad.player = 0;
        }
        if (field == 5) {
            bad.player = 3;
        }
        if (field == 6) {
            bad.role = Role::player;
        }
        if (field == 7) {
            bad.scene = 0;
        }
        if (field == 8) {
            bad.baseline = 0;
        }
        if (field == 9) {
            bad.count = 1;
        }
        const auto wire = encode(bad);
        check(!decode(wire.data(), wire.size(), decoded, error), "invalid_tutorial_identity_or_payload_refused");
    }
    auto reserved = good;
    reserved.back() = 1;
    check(!decode(reserved.data(), reserved.size(), decoded, error), "tutorial_reserved_bytes_refused");
    auto part = p;
    part.tutorial.token = 0xb721f66f;
    part.tutorial.caption = 0;
    auto part_wire = encode(part);
    check(decode(part_wire.data(), part_wire.size(), decoded, error) && decoded.tutorial == part.tutorial,
          "first_part_tutorial_preserves_owner_and_original_token");
    part.tutorial.caption = 0x061d8045;
    part_wire = encode(part);
    check(!decode(part_wire.data(), part_wire.size(), decoded, error), "first_part_rejects_a_first_dna_caption");
    p.kind = Kind::tutorial_done;
    p.role = Role::player;
    p.worker_generation = 0;
    auto wire = encode(p);
    check(decode(wire.data(), wire.size(), decoded, error), "native_completion_receipt_has_separate_role");
    Fixture f;
    f.join(1, Role::authority);
    f.join(2, Role::player, 1);
    f.join(3, Role::player, 2);
    const auto initial = f.baseline();
    const auto a = baseline_id(initial, 2), b = baseline_id(initial, 3);
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 1;
    ack.baseline = a;
    f.send(2, ack);
    Packet notice;
    notice.kind = Kind::tutorial;
    notice.role = Role::authority;
    notice.scene = 1;
    notice.baseline = 1;
    notice.player = 2;
    notice.tutorial = {102, 1, 0xea7666af, 0x061d8045};
    check(f.send(1, notice).empty(), "tutorial_is_retained_but_not_sent_before_owner_baseline_ack");
    ack.baseline = b;
    f.send(3, ack);
    auto delivered = f.send(1, notice);
    check(delivered.size() == 1 && delivered[0].connection == 3 && delivered[0].packet.baseline == b &&
              delivered[0].packet.kind == Kind::tutorial,
          "tutorial_only_reaches_its_ready_owner");
    auto duplicate = f.send(1, notice);
    check(duplicate.size() == 1 && duplicate[0].connection == 3, "pending_native_tutorial_can_retry");
    Packet done = notice;
    done.kind = Kind::tutorial_done;
    done.role = Role::player;
    done.baseline = b;
    auto receipt = f.send(3, done);
    check(receipt.size() == 1 && receipt[0].connection == 1 && receipt[0].packet.baseline == 1 &&
              receipt[0].packet.player == 2 && receipt[0].packet.tutorial == notice.tutorial,
          "observed_completion_returns_to_original_authority");
    auto part_notice = notice;
    part_notice.tutorial.token = 0xb721f66f;
    part_notice.tutorial.caption = 0;
    auto part_delivered = f.send(1, part_notice);
    check(part_delivered.size() == 1 && part_delivered[0].connection == 3 &&
              part_delivered[0].packet.tutorial == part_notice.tutorial,
          "a_second_native_tutorial_does_not_conflict_with_retained_first_dna");
    auto part_done = part_notice;
    part_done.kind = Kind::tutorial_done;
    part_done.role = Role::player;
    part_done.baseline = b;
    auto part_receipt = f.send(3, part_done);
    check(part_receipt.size() == 1 && part_receipt[0].connection == 1 &&
              part_receipt[0].packet.tutorial == part_notice.tutorial,
          "first_part_completion_is_correlated_to_its_own_request");
    check(f.send(3, done).size() == 1, "late_first_dna_receipt_remains_distinct_from_first_part");
    auto forged = done;
    forged.baseline = a;
    check(rejected(f.send(2, forged), Error::ownership), "other_player_cannot_acknowledge_owner_tutorial");
    forged = done;
    forged.tutorial.generation++;
    check(rejected(f.send(3, forged), Error::ownership), "new_incarnation_cannot_acknowledge_old_request");
    forged = done;
    forged.tutorial.caption = 0x061d8186;
    check(rejected(f.send(3, forged), Error::ownership), "receipt_cannot_change_native_caption");
    forged = notice;
    forged.tutorial.entity = 101;
    check(rejected(f.send(1, forged), Error::ownership), "authority_notification_requires_published_owner_mapping");
    f.session.disconnect(3);
    const auto reconnect = f.join(4, Role::player, 2);
    const auto fresh = baseline_id(reconnect, 4);
    check(fresh > b && f.send(1, notice).empty(), "reconnect_withholds_tutorial_until_new_baseline_applies");
    ack.baseline = fresh;
    f.send(4, ack);
    check(f.send(4, done).empty(), "old_baseline_completion_is_ignored_after_reconnect");
    delivered = f.send(1, notice);
    check(delivered.size() == 1 && delivered[0].connection == 4 && delivered[0].packet.baseline == fresh,
          "unacknowledged_request_survives_owner_disconnect");
    done.baseline = fresh;
    check(f.send(4, done).size() == 1, "same_native_request_can_complete_after_reconnect");
    const auto replacement = f.baseline(2, 2);
    ack.scene = 2;
    ack.baseline = baseline_id(replacement, 4);
    f.send(4, ack);
    done.scene = 2;
    done.baseline = ack.baseline;
    check(rejected(f.send(4, done), Error::ownership), "replacement_scene_does_not_inherit_unoffered_tutorial");
    Packet social;
    social.kind = Kind::action;
    social.entity = entity(102, 2);
    social.verb = Verb::social;
    social.target = 103;
    social.target_generation = 1;
    for (int ability : {-1, 0, 4, 87}) {
        social.direction = ability;
        const auto bytes = encode(social);
        check(targeted(Verb::social) && decode(bytes.data(), bytes.size(), decoded, error) &&
                  decoded.direction == ability,
              "native_social_start_and_response_indices_roundtrip");
    }
    for (int ability : {-2, 88, INT32_MAX}) {
        social.direction = ability;
        check(!valid_action(social), "out_of_range_social_input_refused");
    }
    social.direction = 4;
    social.target_generation = 0;
    check(!valid_action(social), "social_requires_current_target_incarnation");
    Fixture g;
    g.join(1, Role::authority);
    g.join(2, Role::player, 1);
    g.join(3, Role::player, 2);
    const auto shared = g.baseline();
    ack.scene = 1;
    ack.baseline = baseline_id(shared, 2);
    g.send(2, ack);
    ack.baseline = baseline_id(shared, 3);
    g.send(3, ack);
    Packet npc;
    npc.kind = Kind::entity;
    npc.scene = 1;
    npc.baseline = 1;
    npc.entity = entity(103, 0);
    g.send(1, npc);
    social.scene = 1;
    social.baseline = ack.baseline;
    social.target_generation = 1;
    social.request = 1;
    auto forwarded = g.send(3, social);
    check(forwarded.size() == 1 && forwarded[0].connection == 1 && forwarded[0].packet.player == 2 &&
              forwarded[0].packet.direction == 4 && forwarded[0].packet.entity.native_id == 102,
          "social_response_uses_authenticated_owner_and_authoritative_actor");
    social.request++;
    social.baseline = baseline_id(shared, 2);
    check(rejected(g.send(2, social), Error::ownership), "wrong_owner_social_input_cannot_reach_authority");
    social.baseline = ack.baseline;
    social.target_generation++;
    check(action_refused(g.send(3, social), Error::stale), "stale_social_target_cannot_reach_authority");
    Packet combat = social;
    combat.verb = Verb::combat;
    combat.target_generation = 1;
    for (int ability : {0, 5, 87}) {
        combat.direction = ability;
        auto bytes = encode(combat);
        check(targeted(Verb::combat) && decode(bytes.data(), bytes.size(), decoded, error) &&
                  decoded.direction == ability,
              "selected_native_combat_index_roundtrips_without_default_substitution");
        bytes[12] = 14;
        check(!decode(bytes.data(), bytes.size(), decoded, error), "schema14_peer_cannot_admit_new_combat_verb");
    }
    for (int ability : {-1, 88, INT32_MAX}) {
        combat.direction = ability;
        check(!valid_action(combat), "invalid_combat_index_refused");
    }
    combat.direction = 5;
    combat.target_generation = 0;
    check(!valid_action(combat), "combat_requires_current_target_incarnation");
    combat.target_generation = 1;
    combat.request = 20;
    forwarded = g.send(3, combat);
    check(forwarded.size() == 1 && forwarded[0].connection == 1 && forwarded[0].packet.player == 2 &&
              forwarded[0].packet.direction == 5 && forwarded[0].packet.verb == Verb::combat &&
              forwarded[0].packet.entity.native_id == 102,
          "normal_combat_selection_reaches_authority_with_authenticated_owner");
    combat.baseline = baseline_id(shared, 2);
    combat.request++;
    check(rejected(g.send(2, combat), Error::ownership), "combat_cannot_command_the_other_player");
    combat.baseline = ack.baseline;
    combat.target_generation++;
    check(action_refused(g.send(3, combat), Error::stale), "combat_cannot_reuse_a_stale_target");
}

void capture_cadence_tests() {
    // Native19's first capture began at 77931437 and sampled its final entities
    // at 77931500. The former start-based deadline was already expired, allowing
    // a second capture inside the same ~15 ms native clock quantum.
    constexpr uint64_t began = 77931437, completed = 77931500;
    check(completed >= began + 50, "slow_first_capture_exposes_expired_start_deadline");
    const auto next = sporemp::next_native_scene_capture(completed);
    check(completed < next && completed + 49 < next && completed + 50 == next,
          "capture_waits_full_interval_after_completion");
    check(sporemp::next_native_scene_capture(completed + 200) == completed + 250,
          "later_slow_capture_does_not_catch_up_in_a_burst");
    Fixture f;
    f.join(1, Role::authority);
    f.join(2, Role::player, 1);
    Packet p;
    p.scene = 1;
    p.baseline = 1;
    p.kind = Kind::scene_begin;
    p.count = 2;
    f.send(1, p);
    p.kind = Kind::entity;
    p.entity = entity(101, 1);
    p.entity.tick = completed;
    f.send(1, p);
    p.entity = entity(102, 2);
    p.entity.tick = completed;
    f.send(1, p);
    p.kind = Kind::scene_end;
    f.send(1, p);
    p.kind = Kind::motion;
    check(rejected(f.send(1, p), Error::stale), "same_native_sample_tick_remains_rejected");
    // A fixture reading at the first coarse clock tick after the new deadline.
    // Production never synthesizes or increments this native sample timestamp.
    p.entity.tick = 77931562;
    check(p.entity.tick >= next, "next_native_clock_sample_follows_completion_deadline");
    const auto update = f.send(1, p);
    check(update.size() == 1 && update[0].packet.kind == Kind::motion && update[0].packet.entity.tick == p.entity.tick,
          "scheduled_new_native_sample_is_accepted_without_tick_rewrite");
}

void fruit_model_tests() {
    // Trial410: the original gives a fruit it places another model, the coordinator refused the update as an
    // identity change and the authority's session ended.
    using sporemp::network::FruitState;
    using sporemp::network::pack_fruit_state;
    Fixture f;
    f.join(1, Role::authority);
    f.join(2, Role::player, 1);
    Entity fruit;
    fruit.kind = EntityKind::fruit;
    fruit.id = 103;
    fruit.generation = 1;
    fruit.tick = 1;
    fruit.native_id = 11447;
    fruit.model_instance = 357547957u;
    fruit.fruit_state = pack_fruit_state(FruitState{6, false, false, true, false, false});
    Packet p;
    p.scene = 1;
    p.baseline = 1;
    p.kind = Kind::scene_begin;
    p.count = 3;
    f.send(1, p);
    p.kind = Kind::entity;
    p.entity = entity(101, 1);
    f.send(1, p);
    p.entity = entity(102, 2);
    f.send(1, p);
    p.entity = fruit;
    f.send(1, p);
    p.kind = Kind::scene_end;
    f.send(1, p);
    Packet ready;
    ready.kind = Kind::baseline_ack;
    ready.scene = 1;
    ready.baseline = 1;
    f.send(2, ready);
    p.kind = Kind::motion;
    p.entity = fruit;
    p.entity.tick = 2;
    p.entity.model_instance = 1896824018u;
    p.entity.x = 4;
    p.entity.fruit_state = pack_fruit_state(FruitState{0, true, true, true, true, false});
    const auto placed = f.send(1, p);
    check(!placed.empty() && placed.back().packet.kind == Kind::motion &&
              placed.back().packet.entity.model_instance == 1896824018u &&
              placed.back().packet.entity.fruit_state == p.entity.fruit_state,
          "a fruit's model and state change with where the original puts it");
    p.entity.tick = 3;
    p.entity.native_id++;
    const auto renamed = f.send(1, p);
    check(!renamed.empty() && renamed.back().packet.kind == Kind::reject,
          "a fruit's native number is still its identity");
}

void nest_model_tests() {
    // Trial239: at a player's kill the original swapped the model of the hunted species' nest, the coordinator
    // refused the update as an identity change and the authority's session ended.
    Fixture f;
    f.join(1, Role::authority);
    f.join(2, Role::player, 1);
    Entity nest = entity(103, 0);
    nest.kind = EntityKind::nest;
    nest.health = nest.energy = nest.hunger = nest.dna = 0;
    nest.age = 1;
    nest.model_instance = 756164235;
    Packet p;
    p.scene = 1;
    p.baseline = 1;
    p.kind = Kind::scene_begin;
    p.count = 3;
    f.send(1, p);
    p.kind = Kind::entity;
    p.entity = entity(101, 1);
    f.send(1, p);
    p.entity = entity(102, 2);
    f.send(1, p);
    p.entity = nest;
    f.send(1, p);
    p.kind = Kind::scene_end;
    f.send(1, p);
    Packet ready;
    ready.kind = Kind::baseline_ack;
    ready.scene = 1;
    ready.baseline = 1;
    f.send(2, ready);
    p.kind = Kind::motion;
    p.entity = nest;
    p.entity.tick = 2;
    p.entity.model_instance = 4264143246u;
    const auto swapped = f.send(1, p);
    check(!swapped.empty() && swapped.back().packet.kind == Kind::motion &&
              swapped.back().packet.entity.model_instance == 4264143246u,
          "a nest's model change is accepted and passed on");
    p.entity.tick = 3;
    p.entity.species_instance++;
    check(rejected(f.send(1, p), Error::ownership), "a nest cannot change species");
    p.entity = nest;
    p.entity.model_instance = 4264143246u;
    p.entity.tick = 4;
    p.entity.herd_native_id++;
    check(rejected(f.send(1, p), Error::ownership), "a nest cannot change herd");
    p.entity = entity(101, 1);
    p.entity.tick = 5;
    p.entity.model_instance = 77;
    const auto creature = f.send(1, p);
    check(!creature.empty() && creature.back().packet.kind == Kind::reject &&
              creature.back().packet.target == uint64_t(Kind::motion),
          "a creature's model is still part of its identity");
}

void session_tests() {
    Fixture f;
    check(f.join(1, Role::authority).front().packet.player == 0, "authority_authenticated");
    check(f.join(2, Role::player, 1).front().packet.player == 1, "first_player_identity");
    check(f.join(3, Role::player, 2).front().packet.player == 2, "second_player_identity");
    auto b = f.baseline();
    uint64_t b1 = baseline_id(b, 2), b2 = baseline_id(b, 3);
    check(b.size() == 8 && b1 && b2 && b1 != b2 && f.session.entity_count() == 2, "full_independent_baselines");
    Packet action;
    action.kind = Kind::action;
    action.scene = 1;
    action.baseline = b1;
    action.entity = entity(101, 1);
    action.verb = Verb::jump;
    action.player = 999;
    check(action_refused(f.send(2, action), Error::not_ready), "action_waits_baseline_ack");
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 1;
    ack.baseline = b1;
    check(f.send(2, ack).empty(), "baseline_ack");
    auto routed = f.send(2, action);
    check(routed.size() == 1 && routed[0].connection == 1 && routed[0].packet.player == 1 &&
              routed[0].packet.baseline == 1 && routed[0].packet.request != 0,
          "authenticated_owner_routing");
    action.entity.id = 102;
    check(rejected(f.send(2, action), Error::ownership), "wrong_actor_denied");
    action.entity.id = 101;
    action.baseline = b1 + 99;
    check(action_refused(f.send(2, action), Error::stale), "wrong_baseline_denied");
    f.session.disconnect(2);
    auto reconnect = f.join(4, Role::player, 1);
    uint64_t fresh = baseline_id(reconnect, 4);
    check(reconnect.front().packet.player == 1 && fresh > b2 && f.session.entity_count() == 2,
          "reconnect_identity_fresh_baseline_no_duplicate");
    ack.baseline = b1;
    check(f.send(4, ack).empty(), "reconnect_old_ack_ignored");
    action.scene = 1;
    action.baseline = fresh;
    action.entity = entity(101, 1);
    check(action_refused(f.send(4, action), Error::not_ready), "obsolete_ack_does_not_restore_readiness");
    Packet update;
    update.kind = Kind::despawn;
    update.scene = 1;
    update.baseline = 1;
    update.entity = entity(101, 1);
    update.entity.tick = 2;
    check(f.send(1, update).size() == 2 && f.session.entity_count() == 1, "reliable_despawn");
    update.kind = Kind::motion;
    update.entity.tick = 3;
    check(rejected(f.send(1, update), Error::stale) && f.session.entity_count() == 1, "motion_cannot_resurrect");
    update.kind = Kind::entity;
    check(rejected(f.send(1, update), Error::stale), "old_generation_spawn_denied");
    update.entity.generation = 2;
    check(f.send(1, update).size() == 2 && f.session.entity_count() == 2, "new_generation_spawn");
    update.kind = Kind::motion;
    update.entity.owner = 2;
    update.entity.tick = 4;
    check(rejected(f.send(1, update), Error::stale), "motion_cannot_change_owner");
    update.entity.owner = 1;
    update.entity.tick = 2;
    check(rejected(f.send(1, update), Error::stale), "out_of_order_motion_denied");
    update.entity.tick = 4;
    update.entity.native_id = 999;
    check(rejected(f.send(1, update), Error::ownership), "motion_cannot_replace_fingerprint");
    auto second = f.baseline(2, 2);
    check(f.session.scene() == 2 && baseline_id(second, 4) > fresh, "new_scene_new_baseline");
    update.scene = 1;
    check(rejected(f.send(1, update), Error::stale), "old_scene_motion_denied");
    auto lost = f.session.disconnect(1);
    check(lost.size() == 2 && lost[0].packet.error == Error::authority_lost && f.session.scene() == 0 &&
              f.session.entity_count() == 0,
          "authority_disconnect_fences_clients_and_clears_obsolete_scene");
    auto replacement = f.join(5, Role::authority);
    check(replacement.size() == 1 && replacement[0].packet.kind == Kind::welcome,
          "selected_head_authority_reauthenticates");
    Packet restored;
    restored.kind = Kind::scene_begin;
    restored.scene = 2;
    restored.baseline = 2;
    restored.count = 2;
    check(f.send(5, restored).empty(), "replacement_may_restart_original_scene_counter");
    restored.kind = Kind::entity;
    restored.entity = entity(101, 1);
    f.send(5, restored);
    restored.entity = entity(102, 2);
    f.send(5, restored);
    restored.kind = Kind::scene_end;
    auto restored_baselines = f.send(5, restored);
    const auto replacement_baseline = baseline_id(restored_baselines, 4);
    check(replacement_baseline > baseline_id(second, 4) && f.session.entity_count() == 2,
          "replacement_publishes_complete_scene_and_fresh_client_baseline");
    action.scene = 2;
    action.baseline = replacement_baseline;
    check(action_refused(f.send(4, action), Error::not_ready), "replacement_requires_fresh_client_ack");
    check(f.session.receive(1, restored).empty(), "disconnected_authority_packets_cannot_mutate_replacement_scene");
    Fixture bad;
    check(bad.session.connect(1), "bad_connect");
    Packet hello;
    hello.kind = Kind::hello;
    hello.identity = bad.config.identity;
    hello.credential = digest(77);
    check(rejected(bad.send(1, hello), Error::authentication), "wrong_invite_rejected");
    hello.credential = bad.config.player1;
    hello.identity.content = digest(99);
    check(rejected(bad.send(1, hello), Error::incompatible), "content_mismatch_rejected");
    hello.identity = bad.config.identity;
    hello.identity.fixture = digest(98);
    check(rejected(bad.send(1, hello), Error::incompatible), "fixture_mismatch_rejected");
    hello.identity = bad.config.identity;
    auto accepted = bad.send(1, hello);
    check(accepted.front().packet.player == 1, "valid_after_bad_fixture_in_pure_policy");
    hello.sequence = 1;
    hello.session = 42;
    check(rejected(bad.session.receive(1, hello), Error::stale), "replay_rejected");
    for (size_t index = 0; index < world_file_count; ++index) {
        Fixture missing;
        check(missing.session.connect(1), "missing world fixture connects");
        Packet incomplete;
        incomplete.kind = Kind::hello;
        incomplete.identity = missing.config.identity;
        incomplete.identity.world[index] = {};
        incomplete.credential = missing.config.player1;
        const auto denied = missing.send(1, incomplete);
        check(denied.size() == 1 && denied[0].close && denied[0].packet.error == Error::world_mismatch &&
                  denied[0].packet.world_index == index,
              "missing world digest cannot welcome or publish a baseline");
    }
    Fixture empty;
    empty.join(1, Role::authority);
    empty.baseline();
    Packet exit;
    exit.kind = Kind::scene_begin;
    exit.scene = 2;
    exit.baseline = 2;
    empty.send(1, exit);
    exit.kind = Kind::scene_end;
    check(empty.send(1, exit).empty() && empty.session.entity_count() == 0, "empty_scene_exit");
    Fixture full;
    for (uint64_t c = 1; c <= 8; ++c) {
        check(full.session.connect(c), "bounded_connection_capacity");
    }
    check(!full.session.connect(9), "ninth_connection_rejected");
    detail::Channel queue;
    Packet queued;
    for (size_t i = 0; i < queue_capacity; ++i) {
        if (!queue.send(queued)) {
            throw std::runtime_error("queue_premature_limit");
        }
    }
    check(!queue.send(queued), "outgoing_queue_exhaustion_fails_closed");
    Fixture duplicate;
    duplicate.join(1, Role::authority);
    duplicate.join(2, Role::player, 1);
    auto db = duplicate.baseline();
    Packet da;
    da.kind = Kind::baseline_ack;
    da.scene = 1;
    da.baseline = baseline_id(db, 2);
    duplicate.send(2, da);
    da.kind = Kind::action;
    da.entity = entity(101, 1);
    da.request = 10;
    check(duplicate.send(2, da).front().connection == 1, "new_action_request");
    check(rejected(duplicate.send(2, da), Error::stale), "duplicate_action_request_not_executed");
    std::vector<Dispatch> last;
    for (size_t i = 0; i < 128; ++i) {
        da.request++;
        last = duplicate.send(2, da);
    }
    check(rejected(last, Error::capacity), "per_player_action_rate_bounded");
    Fixture rebase;
    rebase.join(1, Role::authority);
    rebase.join(2, Role::player, 1);
    auto rb = rebase.baseline();
    uint64_t rb_old = baseline_id(rb, 2);
    Packet old_ack;
    old_ack.kind = Kind::baseline_ack;
    old_ack.scene = 1;
    old_ack.baseline = rb_old;
    rebase.send(2, old_ack);
    Packet begin;
    begin.kind = Kind::scene_begin;
    begin.scene = 2;
    begin.baseline = 2;
    begin.count = 2;
    rebase.send(1, begin);
    Packet late;
    late.kind = Kind::action;
    late.scene = 1;
    late.baseline = rb_old;
    late.request = 900;
    late.entity = entity(101, 1);
    late.player = 999;
    auto refused = rebase.send(2, late);
    check(action_refused(refused, Error::not_ready) && refused[0].packet.request == 900 &&
              refused[0].packet.player == 1 && refused[0].packet.scene == 1 && refused[0].packet.baseline == rb_old,
          "in_flight_action_during_rebase_correlated_refusal");
    check(rebase.send(2, old_ack).empty(), "in_flight_ack_during_rebase_ignored");
    begin.kind = Kind::entity;
    begin.entity = entity(101, 1);
    rebase.send(1, begin);
    begin.entity = entity(102, 2);
    rebase.send(1, begin);
    begin.kind = Kind::scene_end;
    auto committed = rebase.send(1, begin);
    uint64_t rb_new = baseline_id(committed, 2);
    check(rb_new > rb_old, "rebase_fresh_baseline_after_in_flight_action");
    check(rebase.send(2, old_ack).empty(), "obsolete_ack_after_rebase_ignored");
    late.scene = 2;
    late.baseline = rb_new;
    late.request = 901;
    check(action_refused(rebase.send(2, late), Error::not_ready), "rebase_requires_fresh_ack");
    old_ack.scene = 2;
    old_ack.baseline = rb_new;
    rebase.send(2, old_ack);
    late.request = 902;
    auto good = rebase.send(2, late);
    check(good.size() == 1 && good[0].connection == 1 && good[0].packet.kind == Kind::action,
          "fresh_ack_restores_action_routing_after_rebase");
}

bool await_packet(Peer& peer, Kind kind, Packet& packet, std::string& error, unsigned milliseconds = 5000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    Event e;
    while (std::chrono::steady_clock::now() < end) {
        while (peer.poll(e)) {
            if (e.kind == EventKind::packet) {
                if (e.packet.kind == kind) {
                    packet = e.packet;
                    return true;
                }
                if (e.packet.kind == Kind::reject) {
                    error = error_name(e.packet.error);
                    return false;
                }
            }
            if (e.kind == EventKind::disconnected) {
                error = e.detail;
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    error = "await_packet_timeout";
    return false;
}

void require_packet(Peer& peer, Kind kind, Packet& p, const char* message) {
    std::string error;
    bool got = await_packet(peer, kind, p, error);
    if (!got) {
        std::cerr << message << ": " << error << '\n';
    }
    check(got, message);
}

void tls_tests() {
    CoordinatorConfig config;
    config.port = 0;
    config.session = settings();
    Coordinator server;
    std::string error;
    bool started = server.start(config, error);
    if (!started) {
        std::cerr << "TLS server: " << error << '\n';
    }
    check(started, "real_tls_listener");
    std::atomic<bool> stop{false};
    std::atomic<unsigned> routed_actions{0}, ownership_refusals{0};
    std::thread pump([&] {
        CoordinatorEvent e;
        while (!stop) {
            while (server.poll(e)) {
                if (e.outbound && e.event.detail == "dispatch_queued" && e.event.packet.request &&
                    e.event.packet.player == 1) {
                    if (e.event.packet.kind == Kind::action) {
                        ++routed_actions;
                    }
                    if (e.event.packet.kind == Kind::reject && e.event.packet.error == Error::ownership) {
                        ++ownership_refusals;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    try {
        auto aconfig = server.peer_config(Role::authority), pconfig = server.peer_config(Role::player, 1),
             qconfig = server.peer_config(Role::player, 2);
        check(pconfig.certificate_pin != Digest{} && pconfig.credential != qconfig.credential &&
                  pconfig.credential != aconfig.credential,
              "tls_fresh_pin_distinct_credentials");
        Peer authority, p1, p2;
        check(authority.start(aconfig, error), "authority_network_start");
        check(p1.start(pconfig, error), "player1_network_start");
        check(p2.start(qconfig, error), "player2_network_start");
        Packet ap, pp, qp;
        require_packet(authority, Kind::welcome, ap, "authority_tls_welcome");
        require_packet(p1, Kind::welcome, pp, "player1_tls_welcome");
        require_packet(p2, Kind::welcome, qp, "player2_tls_welcome");
        check(pp.player == 1 && qp.player == 2 && pp.session == qp.session && ap.session == pp.session,
              "real_tls_shared_session_distinct_players");
        Packet b;
        b.kind = Kind::scene_begin;
        b.scene = 1;
        b.baseline = 1;
        b.count = static_cast<uint32_t>(max_entities);
        check(authority.send(b), "send_baseline_begin");
        b.kind = Kind::entity;
        b.entity = entity(101, 1);
        authority.send(b);
        b.entity = entity(102, 2);
        authority.send(b);
        for (size_t i = 2; i < max_entities; ++i) {
            b.entity = entity(1000 + i, 0);
            check(authority.send(b), "maximum_scene_entity_enqueued");
        }
        b.kind = Kind::scene_end;
        authority.send(b);
        require_packet(p1, Kind::scene_end, pp, "p1_actual_tls_baseline");
        require_packet(p2, Kind::scene_end, qp, "p2_actual_tls_baseline");
        check(pp.baseline != qp.baseline && pp.count == max_entities && qp.count == max_entities,
              "real_maximum_scene_independent_baselines");
        Packet ack;
        ack.kind = Kind::baseline_ack;
        ack.scene = 1;
        ack.baseline = pp.baseline;
        p1.send(ack);
        Packet act = ack;
        act.kind = Kind::action;
        act.entity = entity(101, 1);
        act.verb = Verb::jump;
        act.player = 999;
        p1.send(act);
        require_packet(authority, Kind::action, ap, "actual_action_to_authority");
        check(ap.player == 1 && ap.entity.owner == 1 && ap.baseline == 1 && ap.request != 0,
              "actual_authenticated_action_owner");
        act.entity.id = 102;
        p1.send(act);
        require_packet(p1, Kind::reject, ap, "actual_wrong_owner_rejection");
        check(ap.error == Error::ownership, "actual_wrong_owner_reason");
        auto old_baseline = pp.baseline;
        p1.stop();
        Peer resumed;
        check(resumed.start(pconfig, error), "reconnect_start");
        require_packet(resumed, Kind::welcome, pp, "reconnect_welcome");
        check(pp.player == 1, "reconnect_same_player");
        require_packet(resumed, Kind::scene_end, pp, "reconnect_fresh_baseline");
        check(pp.baseline > old_baseline, "reconnect_fresh_baseline_id");
        Peer wrong_pin;
        auto bad = pconfig;
        bad.certificate_pin[0] ^= 1;
        check(wrong_pin.start(bad, error), "bad_pin_start");
        check(!await_packet(wrong_pin, Kind::welcome, ap, error) && error == "certificate_pin_or_validity_mismatch",
              "real_wrong_pin_rejected");
        wrong_pin.stop();
        Peer wrong_secret;
        bad = pconfig;
        bad.credential[0] ^= 1;
        check(wrong_secret.start(bad, error), "bad_secret_start");
        require_packet(wrong_secret, Kind::reject, ap, "real_wrong_secret_rejected");
        check(ap.error == Error::authentication, "real_wrong_secret_reason");
        wrong_secret.stop();
        Peer mismatch;
        bad = pconfig;
        bad.identity.build[0] ^= 1;
        check(mismatch.start(bad, error), "bad_build_start");
        require_packet(mismatch, Kind::reject, ap, "real_wrong_build_rejected");
        check(ap.error == Error::incompatible, "real_wrong_build_reason");
        mismatch.stop();
        for (size_t index = 0; index < world_file_count; ++index) {
            Peer changed_world;
            bad = pconfig;
            bad.identity.world[index][0] ^= 1;
            check(changed_world.start(bad, error), "changed world TLS peer start");
            require_packet(changed_world, Kind::reject, ap, "actual world mismatch rejected over TLS");
            check(ap.error == Error::world_mismatch && ap.world_index == index &&
                      error_detail(ap) == std::string("canonical_world_mismatch:") + world_files[index].path,
                  "actual TLS rejection identifies exact changed world file");
            changed_world.stop();
        }
        auto raw_socket = detail::connect_socket(pconfig.host, pconfig.port, stop, error);
        check(raw_socket != INVALID_SOCKET, "malformed_tls_socket");
        {
            detail::TlsStream raw(raw_socket);
            check(raw.handshake(false, nullptr, pconfig.certificate_pin, pconfig.host, stop, error),
                  "malformed_real_tls_handshake");
            Packet h;
            h.kind = Kind::hello;
            h.sequence = 1;
            h.identity = pconfig.identity;
            h.credential = pconfig.credential;
            Wire malformed = encode(h);
            malformed[4] = 255;
            malformed[5] = 255;
            check(raw.send(malformed.data(), malformed.size(), error), "oversized_message_sent_inside_tls");
            std::vector<uint8_t> bytes;
            bool open = true;
            auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (open && std::chrono::steady_clock::now() < end) {
                open = raw.receive(bytes, error);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            check(!open, "real_oversized_frame_connection_closed");
        }
        check(routed_actions == 1 && ownership_refusals == 1,
              "real_tls_action_dispatch_and_owner_refusal_have_request_correlated_evidence");
        check(authority.transport_active(), "welcomed authority transport remains active before explicit revocation");
        authority.request_stop();
        check(!authority.transport_active(),
              "transport stop fences engine input readiness before a disconnect event is consumed");
        resumed.stop();
        p2.stop();
        authority.stop();
    } catch (...) {
        stop = true;
        pump.join();
        server.stop();
        throw;
    }
    stop = true;
    pump.join();
    server.stop();
}
} // namespace

int main() {
    try {
        codec_tests();
        hatch_tests();
        history_tests();
        relationship_tests();
        late_fossil_tests();
        fruit_model_tests();
        recharge_tests();
        fruit_eaten_tests();
        nest_model_tests();
        model_drift_tests();
        selection_guard_tests();
        unowned_movement_tests();
        client_request_tests();
        screen_point_tests();
        inventory_tests();
        death_tests(2);
        death_tests(5);
        social_view_tests();
        tutorial_tests();
        capture_cadence_tests();
        session_tests();
        assertions += encounter_network_tests();
        assertions += world_identity_tests();
        tls_tests();
        std::cout << "PASS " << assertions
                  << " HOST protocol/policy/real Windows Schannel TCP assertions; no SPORE process started.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << assertions << " assertions: " << e.what() << '\n';
        return 1;
    }
}
