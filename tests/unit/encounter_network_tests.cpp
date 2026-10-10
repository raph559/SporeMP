#include "../../src/network/steering.h"
#include "session.h"
#include <limits>
#include <stdexcept>
#include <string>

using namespace sporemp::network;

namespace {
int assertions = 0;

void check(bool value, const char* reason) {
    ++assertions;
    if (!value) {
        throw std::runtime_error(reason);
    }
}

Digest digest(uint8_t value) {
    Digest d{};
    d.fill(value);
    return d;
}

Entity entity(uint64_t id, uint64_t owner) {
    Entity e;
    e.id = id;
    e.generation = 7;
    e.owner = owner;
    e.native_id = uint32_t(id);
    e.tick = 1;
    e.health = 10;
    e.dna = 8.75f;
    return e;
}

SessionConfig config() {
    SessionConfig c;
    c.identity = {digest(1), digest(2), digest(3), digest(4)};
    for (auto& value : c.identity.world) {
        value = digest(4);
    }
    c.authority = digest(5);
    c.player1 = digest(6);
    c.player2 = digest(7);
    return c;
}

struct Fixture {
    SessionConfig settings = config();
    Session session{settings};
    uint64_t sequence[8]{};

    std::vector<Dispatch> send(uint64_t id, Packet p) {
        p.sequence = ++sequence[id];
        p.session = p.kind == Kind::hello ? 0 : settings.epoch;
        return session.receive(id, p);
    }

    std::vector<Dispatch> join(uint64_t id, Role role, uint64_t owner = 1) {
        check(session.connect(id), "encounter connection");
        Packet p;
        p.kind = Kind::hello;
        p.identity = settings.identity;
        p.role = role;
        p.credential = role == Role::authority ? settings.authority : owner == 1 ? settings.player1 : settings.player2;
        return send(id, p);
    }

    std::vector<Dispatch> initial() {
        join(1, Role::authority);
        join(2, Role::player);
        Packet p;
        p.scene = 3;
        p.baseline = 1;
        p.count = 3;
        p.kind = Kind::scene_begin;
        send(1, p);
        p.kind = Kind::entity;
        for (auto e : {entity(101, 1), entity(102, 2), entity(103, 0)}) {
            p.entity = e;
            send(1, p);
        }
        p.kind = Kind::scene_end;
        return send(1, p);
    }
};

uint64_t baseline(const std::vector<Dispatch>& out) {
    for (const auto& d : out) {
        if (d.packet.kind == Kind::scene_begin) {
            return d.packet.baseline;
        }
    }
    return 0;
}

bool refused(const std::vector<Dispatch>& out, Error error) {
    return out.size() == 1 && out[0].packet.kind == Kind::action_result && out[0].packet.error == error &&
           !out[0].close;
}

const Entity* find(const std::vector<Dispatch>& out, uint64_t id) {
    for (const auto& d : out) {
        if (d.packet.kind == Kind::entity && d.packet.entity.id == id) {
            return &d.packet.entity;
        }
    }
    return nullptr;
}

void fossil_scene_tests() {
    Entity resource;
    resource.id = 104;
    resource.native_id = 9777;
    resource.generation = 9;
    resource.tick = 1;
    resource.kind = EntityKind::fossil;
    resource.model_instance = 1640036248;
    Packet p;
    p.kind = Kind::entity;
    p.entity = resource;
    Packet decoded;
    std::string error;
    auto wire = encode(p);
    check(wire.size() == 688 && wire[648] == 1 && decode(wire.data(), wire.size(), decoded, error) &&
              decoded.entity.kind == EntityKind::fossil && decoded.entity.model_instance == 1640036248,
          "fossil kind and model survive an explicit bounded wire representation");
    auto old = wire;
    old[12] = 19;
    check(!decode(old.data(), old.size(), decoded, error), "schema19 without fossil intentions is incompatible");
    for (auto bad : {0u, 1u, 2u, 3u, 4u, 5u}) {
        p.entity = resource;
        if (bad == 0) {
            p.entity.owner = 1;
        }
        if (bad == 1) {
            p.entity.health = 1;
        }
        if (bad == 2) {
            p.entity.food = 1;
        }
        if (bad == 3) {
            p.entity.species_instance = 1;
        }
        if (bad == 4) {
            p.entity.model_instance = 0;
        }
        if (bad == 5) {
            p.entity.kind = EntityKind(4);
        }
        wire = encode(p);
        check(!decode(wire.data(), wire.size(), decoded, error),
              "resource cannot carry player/animal state or an unknown kind/model");
    }
    p.entity = entity(103, 0);
    p.entity.model_instance = 1;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "animal cannot smuggle a fossil model identity");
    Fixture f;
    auto initial = f.initial();
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 3;
    ack.baseline = baseline(initial);
    f.send(2, ack);
    p = {};
    p.kind = Kind::entity;
    p.scene = 3;
    p.baseline = 1;
    p.entity = resource;
    auto out = f.send(1, p);
    check(out.size() == 1 && out[0].packet.kind == Kind::entity && f.session.entity_count() == 4,
          "authority publishes a fossil in the existing scene lifecycle");
    Packet action = ack;
    action.kind = Kind::action;
    action.entity = entity(101, 1);
    action.target = 104;
    action.target_generation = 9;
    action.request = 1;
    for (auto verb : {Verb::attack, Verb::pickup, Verb::social, Verb::combat}) {
        action.verb = verb;
        action.direction = 0;
        ++action.request;
        check(refused(f.send(2, action), Error::not_ready),
              "animal interactions do not reinterpret a fossil as prey or food");
    }
    action.verb = Verb::collect;
    ++action.request;
    wire = encode(action);
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.verb == Verb::collect &&
              decoded.target_generation == 9,
          "collect carries a resource incarnation through the wire");
    out = f.send(2, action);
    check(out.size() == 1 && out[0].connection == 1 && out[0].packet.verb == Verb::collect &&
              out[0].packet.target == 104,
          "authenticated collect reaches only the original authority with its exact fossil target");
    action.target_generation = 8;
    ++action.request;
    check(refused(f.send(2, action), Error::stale), "collect refuses a stale fossil incarnation");
    action.target = 103;
    action.target_generation = 7;
    ++action.request;
    check(refused(f.send(2, action), Error::not_ready), "collect cannot reinterpret an animal as a part resource");
    action.target = 104;
    action.target_generation = 9;
    p.kind = Kind::motion;
    p.entity.tick = 2;
    ++p.entity.model_instance;
    out = f.send(1, p);
    check(out.size() == 1 && out[0].packet.kind == Kind::reject && out[0].packet.error == Error::ownership,
          "model identity cannot change inside a resource incarnation");
    p.entity = entity(104, 0);
    p.entity.generation = 9;
    p.entity.tick = 3;
    out = f.send(1, p);
    check(out.size() == 1 && out[0].packet.error == Error::ownership,
          "fossil cannot become an animal inside an incarnation");
    p.kind = Kind::despawn;
    p.entity = resource;
    p.entity.tick = 4;
    out = f.send(1, p);
    check(out.size() == 1 && out[0].packet.kind == Kind::despawn && f.session.entity_count() == 3,
          "authority removes a consumed resource once");
    ++action.request;
    check(refused(f.send(2, action), Error::stale), "a late collect cannot act on a consumed resource");
    auto late = f.join(3, Role::player, 2);
    check(!find(late, 104), "late baseline cannot resurrect the collected fossil");
    p.kind = Kind::entity;
    out = f.send(1, p);
    check(out.size() == 1 && out[0].packet.error == Error::stale, "retired resource incarnation cannot be republished");
    ++p.entity.generation;
    ++p.entity.tick;
    out = f.send(1, p);
    check(f.session.entity_count() == 4, "only a new authority incarnation can reuse the resource identity");
}

void nest_scene_tests() {
    Entity nest;
    nest.id = 104;
    nest.native_id = 12561;
    nest.generation = 9;
    nest.tick = 1;
    nest.kind = EntityKind::nest;
    nest.herd_native_id = 12558;
    nest.species_instance = 9;
    nest.species_type = 10;
    nest.species_group = 11;
    nest.archetype = 12;
    nest.x = 10;
    nest.y = 20;
    nest.z = 30;
    auto unchanged = nest;
    unchanged.tick = 500;
    check(unchanged_static_entity(nest, unchanged), "static nest is not resent just because the capture tick changed");
    for (unsigned changed = 0; changed < 9; ++changed) {
        auto next = unchanged;
        if (changed == 0) {
            ++next.generation;
        }
        if (changed == 1) {
            ++next.herd_native_id;
        }
        if (changed == 2) {
            ++next.species_instance;
        }
        if (changed == 3) {
            ++next.model_instance;
        }
        if (changed == 4) {
            ++next.x;
        }
        if (changed == 5) {
            ++next.scale;
        }
        if (changed == 6) {
            next.health = 1;
        }
        if (changed == 7) {
            ++next.id;
        }
        if (changed == 8) {
            ++next.native_id;
        }
        check(!unchanged_static_entity(nest, next),
              "static suppression preserves changed state, identity and invalid-state admission");
    }
    auto animal = entity(101, 1);
    check(!unchanged_static_entity(animal, animal), "animal updates retain their ordinary publication path");
    Packet p;
    p.kind = Kind::entity;
    p.entity = nest;
    Packet decoded;
    std::string error;
    auto wire = encode(p);
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.entity.kind == EntityKind::nest &&
              decoded.entity.herd_native_id == 12558 && decoded.entity.species_instance == 9,
          "native nest herd and species survive the wire");
    for (unsigned changed = 0; changed < 6; ++changed) {
        p.entity = nest;
        if (changed == 0) {
            p.entity.owner = 2;
        }
        if (changed == 1) {
            p.entity.herd_native_id = 0;
        }
        if (changed == 2) {
            p.entity.species_instance = 0;
        }
        if (changed == 3) {
            p.entity.health = 1;
        }
        if (changed == 4) {
            p.entity.food = 1;
        }
        if (changed == 5) {
            p.entity.life_state = 1;
        }
        wire = encode(p);
        check(!decode(wire.data(), wire.size(), decoded, error),
              "nest cannot impersonate an avatar or carry incomplete affiliation");
    }
    Fixture f;
    auto initial = f.initial();
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 3;
    ack.baseline = baseline(initial);
    f.send(2, ack);
    p = {};
    p.kind = Kind::entity;
    p.scene = 3;
    p.baseline = 1;
    p.entity = nest;
    auto out = f.send(1, p);
    check(out.size() == 1 && out[0].packet.kind == Kind::entity && f.session.entity_count() == 4,
          "new nest uses authority scene publication");
    Packet action = ack;
    action.kind = Kind::action;
    action.entity = entity(101, 1);
    action.target = 104;
    action.target_generation = 9;
    action.request = 1;
    for (auto verb : {Verb::attack, Verb::pickup, Verb::social, Verb::combat, Verb::collect}) {
        action.verb = verb;
        ++action.request;
        check(refused(f.send(2, action), Error::not_ready), "nest is not prey, food or a fossil");
    }
    p.kind = Kind::motion;
    p.entity.tick = 2;
    ++p.entity.herd_native_id;
    out = f.send(1, p);
    check(out.size() == 1 && out[0].packet.error == Error::ownership,
          "nest cannot switch herds in the same incarnation");
    auto late = f.join(3, Role::player, 2);
    check(find(late, 104) && find(late, 104)->herd_native_id == 12558, "rejoin includes original nest affiliation");
    p.kind = Kind::despawn;
    p.entity = nest;
    p.entity.tick = 3;
    f.send(1, p);
    p.kind = Kind::entity;
    out = f.send(1, p);
    check(out.size() == 1 && out[0].packet.error == Error::stale, "retired nest incarnation cannot reappear");
}

void pickup_codec_tests() {
    Packet p;
    p.kind = Kind::entity;
    p.entity = entity(103, 0);
    p.entity.life_state = 1;
    p.entity.health = 0;
    p.entity.fed_on = 1;
    p.entity.food = -0.25f;
    p.entity.pickup_owner = 2;
    Packet decoded;
    std::string error;
    auto wire = encode(p);
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.entity.fed_on == 1 &&
              decoded.entity.food == -0.25f && decoded.entity.pickup_owner == 2,
          "first-feed beneficiary and signed native food roundtrip");
    check(wire.size() == 688 && wire[12] == schema_version && wire[388] == 1 && wire[389] == 0 && wire[390] == 0 &&
              wire[391] == 0 && wire[392] == 0 && wire[393] == 0 && wire[394] == 0x80 && wire[395] == 0xbe &&
              wire[396] == 2 && wire[397] == 0 && wire[398] == 0 && wire[399] == 0,
          "current schema preserves first-feed offsets388 through399");
    auto bad = wire;
    bad[383] = 1;
    check(!decode(bad.data(), bad.size(), decoded, error) && error == "nonzero_reserved",
          "context byte383 remains reserved in schema9");
    bad = wire;
    bad[399] = 1;
    check(!decode(bad.data(), bad.size(), decoded, error) && error == "invalid_entity",
          "byte399 is beneficiary high byte and rejects oversized owner, not reserved data");
    bad = wire;
    bad[389] = 1;
    check(!decode(bad.data(), bad.size(), decoded, error) && error == "invalid_entity",
          "first-feed flag cannot truncate a nonzero high byte");
    bad = wire;
    bad[12] = 3;
    check(!decode(bad.data(), bad.size(), decoded, error) && error == "unsupported_protocol_or_schema",
          "schema3 cannot silently discard first-feed state");
    for (uint32_t invalid : {2u, 256u, UINT32_MAX}) {
        p.entity.fed_on = invalid;
        wire = encode(p);
        check(!decode(wire.data(), wire.size(), decoded, error), "first-feed flag outside zero/one rejected");
    }
    p.entity.fed_on = 1;
    for (uint32_t invalid : {3u, 256u, UINT32_MAX}) {
        p.entity.pickup_owner = invalid;
        wire = encode(p);
        check(!decode(wire.data(), wire.size(), decoded, error), "unknown first-feed beneficiary rejected");
    }
    p.entity.pickup_owner = 1;
    p.entity.fed_on = 0;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "beneficiary without observed first-feed flag rejected");
    p.entity.pickup_owner = 0;
    p.entity.fed_on = 1;
    p.entity.food = 3.5f;
    wire = encode(p);
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.entity.pickup_owner == 0 &&
              decoded.entity.food == 3.5f,
          "already-fed corpse can retain nutrition with unknown historical beneficiary");
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(), 1.01e9f, -1.01e9f}) {
        p.entity.food = invalid;
        wire = encode(p);
        check(!decode(wire.data(), wire.size(), decoded, error), "nonfinite or unbounded native food rejected");
    }
    for (float accepted : {-1.0e9f, -0.001f, 0.f, 1.0e9f}) {
        p.entity.food = accepted;
        wire = encode(p);
        check(decode(wire.data(), wire.size(), decoded, error) && decoded.entity.food == accepted,
              "finite bounded food preserves signed native value without clamp");
    }
    p.kind = Kind::action;
    p.entity = entity(101, 1);
    p.verb = Verb::pickup;
    p.target = 103;
    p.target_generation = 7;
    wire = encode(p);
    check(uint32_t(Verb::pickup) == 7 && targeted(Verb::pickup) && decode(wire.data(), wire.size(), decoded, error) &&
              decoded.verb == Verb::pickup,
          "pickup7 is a targeted intention on the wire");
    p.target = 0;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "pickup requires a target ID");
    p.target = 103;
    p.target_generation = 0;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "pickup requires target incarnation");
    p.target_generation = 7;
    p.direction = 1;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "pickup cannot carry movement direction");
    // C02, trial364: a click on a fruit picks a noun that is no shared entity. The intention names it by the
    // original's own number for it, the same in every game that loaded the save.
    // Trial378: each game re-places part of its fruit on its own, so the number alone does not say which fruit
    // the player saw. The intention carries the fruit's place in the player's own world as well.
    using sporemp::network::pack_place;
    using sporemp::network::unpack_place;
    using sporemp::network::same_place;
    const float seen[3]{-419.7f, 147.6f, 245.9f};
    float back[3]{};
    const uint64_t place = pack_place(seen);
    check(place && unpack_place(place, back) && std::abs(back[0] - seen[0]) < 0.07f &&
              std::abs(back[1] - seen[1]) < 0.07f && std::abs(back[2] - seen[2]) < 0.07f,
          "a place survives packing to an eighth of a unit");
    const float near[3]{-419.2f, 147.9f, 246.3f}, far[3]{-410.5f, 122.6f, 275.2f}, out[3]{5000, 0, 0};
    check(same_place(place, near) && !same_place(place, far) && !pack_place(out) && !unpack_place(0, back) &&
              !same_place(0, near),
          "the authority's fruit of that number is the one seen only if it lies at that place; no place is no fruit");
    p.target = place;
    p.target_generation = 0;
    p.direction = 2804;
    p.verb = Verb::forage;
    wire = encode(p);
    check(uint32_t(Verb::forage) == 13 && !targeted(Verb::forage) && decode(wire.data(), wire.size(), decoded, error) &&
              decoded.verb == Verb::forage && decoded.direction == 2804 && decoded.target == place,
          "foraging names a fruit by its native number and its place, and carries no entity target");
    p.direction = 0;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "foraging requires a fruit");
    p.direction = -1;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "a fruit's number is positive");
    p.direction = 2804;
    p.target = 0;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "foraging requires the fruit's place");
    p.target = place;
    p.target_generation = 7;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "foraging carries no target incarnation");
    p.direction = 0;
    p.verb = Verb(14);
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "verb beyond foraging remains unsupported");
}

void pickup_session_tests() {
    Fixture f;
    auto initial = f.initial();
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 3;
    ack.baseline = baseline(initial);
    f.send(2, ack);
    Packet update;
    update.kind = Kind::motion;
    update.scene = 3;
    update.baseline = 1;
    update.entity = entity(103, 0);
    update.entity.tick = 2;
    update.entity.food = 4;
    f.send(1, update);
    Packet action = ack;
    action.kind = Kind::action;
    action.entity = entity(101, 1);
    action.verb = Verb::pickup;
    action.target = 103;
    action.target_generation = 7;
    action.request = 10;
    check(refused(f.send(2, action), Error::not_ready), "positive food alone does not admit pickup of a living NPC");
    update.entity.life_state = 1;
    update.entity.health = 0;
    update.entity.tick = 3;
    f.send(1, update);
    action.request++;
    action.player = 2;
    action.entity.fed_on = 1;
    action.entity.pickup_owner = 2;
    action.entity.food = 999;
    auto out = f.send(2, action);
    check(out.size() == 1 && out[0].connection == 1 && out[0].packet.verb == Verb::pickup &&
              out[0].packet.player == 1 && out[0].packet.target == 103 && out[0].packet.target_generation == 7 &&
              out[0].packet.entity.fed_on == 0 && out[0].packet.entity.pickup_owner == 0 &&
              out[0].packet.entity.food == 0,
          "corpse pickup routes authenticated actor and discards forged actor pickup fields");
    action.request++;
    action.target_generation = 6;
    check(refused(f.send(2, action), Error::stale), "pickup rejects obsolete corpse incarnation");
    action.target_generation = 7;
    action.target = 102;
    check(refused(f.send(2, action), Error::ownership),
          "pickup does not admit another owned actor as a corpse resource");
    action.target = 103;
    action.entity.generation = 6;
    out = f.send(2, action);
    check(out.size() == 1 && out[0].packet.kind == Kind::reject && out[0].packet.error == Error::ownership,
          "pickup rejects obsolete controlled-actor incarnation");
    action.entity = entity(102, 2);
    out = f.send(2, action);
    check(out.size() == 1 && out[0].packet.kind == Kind::reject && out[0].packet.error == Error::ownership,
          "player1 cannot issue pickup as player2");
    action.entity = entity(101, 1);
    action.request++;
    update.entity.tick = 4;
    update.entity.fed_on = 1;
    update.entity.pickup_owner = 2;
    update.entity.food = 3.5f;
    out = f.send(1, update);
    check(out.size() == 1 && out[0].packet.entity.fed_on == 1 && out[0].packet.entity.pickup_owner == 2 &&
              out[0].packet.entity.food == 3.5f,
          "authoritative first-feed beneficiary publishes independently of remaining food");
    out = f.send(2, action);
    check(out.size() == 1 && out[0].connection == 1 && out[0].packet.kind == Kind::action,
          "first-feed bonus already claimed does not imply all corpse nutrition is consumed");
    auto joined = f.join(3, Role::player, 2);
    auto corpse = find(joined, 103);
    check(corpse && corpse->life_state == 1 && corpse->fed_on == 1 && corpse->pickup_owner == 2 && corpse->food == 3.5f,
          "late join baseline retains first-feed flag beneficiary and remaining nutrition");
    const Entity fed = update.entity;
    for (uint64_t stale_tick : {uint64_t(3), uint64_t(4)}) {
        update.entity = fed;
        update.entity.tick = stale_tick;
        update.entity.fed_on = 0;
        update.entity.pickup_owner = 0;
        update.entity.food = 4;
        out = f.send(1, update);
        check(out.size() == 1 && out[0].packet.error == Error::stale,
              "old or duplicate packet cannot erase first-feed state or refill food");
    }
    update.entity = fed;
    update.entity.generation = 6;
    update.entity.tick = 99;
    update.entity.pickup_owner = 1;
    out = f.send(1, update);
    check(out.size() == 1 && out[0].packet.error == Error::stale,
          "obsolete corpse generation cannot transfer beneficiary with a newer tick");
    joined = f.join(4, Role::player, 2);
    corpse = find(joined, 103);
    check(corpse && corpse->generation == 7 && corpse->tick == 4 && corpse->fed_on == 1 && corpse->pickup_owner == 2 &&
              corpse->food == 3.5f && f.session.entity_count() == 3,
          "fresh baseline proves rejected updates left the authoritative corpse unchanged");
    update.entity = fed;
    update.entity.tick = 5;
    update.entity.food = -0.25f;
    out = f.send(1, update);
    check(out.size() == 2 && out[0].packet.entity.food == -0.25f,
          "native final-bite negative food publishes without clamping");
    action.request++;
    check(refused(f.send(2, action), Error::not_ready), "depleted negative-food corpse cannot be picked up again");
    joined = f.join(5, Role::player, 2);
    corpse = find(joined, 103);
    check(corpse && corpse->food == -0.25f && corpse->fed_on == 1 && corpse->pickup_owner == 2,
          "late join preserves depletion and the original first-feed beneficiary");
    update.entity.tick = 6;
    update.entity.food = 0;
    f.send(1, update);
    action.request++;
    check(refused(f.send(2, action), Error::not_ready), "zero-food corpse cannot be picked up");
    update.entity.tick = 7;
    update.entity.food = 2;
    f.send(1, update);
    update.entity = entity(101, 1);
    update.entity.tick = 2;
    update.entity.life_state = 1;
    update.entity.health = 0;
    f.send(1, update);
    action.request++;
    check(refused(f.send(2, action), Error::not_ready), "dead controlled actor cannot pick up positive-food corpse");
}

void pending_death_tests() {
    Fixture f;
    auto initial = f.initial();
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 3;
    ack.baseline = baseline(initial);
    f.send(2, ack);
    Packet update;
    update.kind = Kind::motion;
    update.scene = 3;
    update.baseline = 1;
    update.entity = entity(103, 0);
    update.entity.tick = 2;
    update.entity.health = 0;
    update.entity.food = 100;
    auto wire = encode(update);
    Packet decoded;
    std::string error;
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.entity.health == 0 &&
              decoded.entity.life_state == 0,
          "native zero-health before dead flag roundtrips without manufacturing death");
    auto out = f.send(1, update);
    check(out.size() == 1 && out[0].packet.entity.health == 0 && out[0].packet.entity.life_state == 0,
          "pending native death publishes the exact observed intermediate state");
    Packet action = ack;
    action.kind = Kind::action;
    action.entity = entity(101, 1);
    action.target = 103;
    action.target_generation = 7;
    for (auto verb : {Verb::attack, Verb::approach, Verb::engage, Verb::pickup, Verb::combat}) {
        action.verb = verb;
        action.request++;
        check(refused(f.send(2, action), Error::not_ready),
              "zero-health pending death is neither living combat target nor ready corpse");
    }
    auto joined = f.join(3, Role::player, 2);
    auto pending = find(joined, 103);
    check(pending && pending->health == 0 && pending->life_state == 0 && pending->food == 100 &&
              pending->generation == 7,
          "fresh baseline preserves pending native death without resurrection or invented dead flag");
    update.entity.life_state = 1;
    update.entity.tick = 3;
    f.send(1, update);
    action.verb = Verb::pickup;
    action.request++;
    out = f.send(2, action);
    check(out.size() == 1 && out[0].connection == 1 && out[0].packet.verb == Verb::pickup,
          "later original dead flag admits the same corpse incarnation for pickup");
    update.entity = entity(101, 1);
    update.entity.health = 0;
    update.entity.tick = 2;
    f.send(1, update);
    for (auto verb : {Verb::jump, Verb::move, Verb::stop, Verb::pickup, Verb::combat}) {
        action.verb = verb;
        action.target = targeted(verb) ? 103 : 0;
        action.target_generation = targeted(verb) ? 7 : 0;
        action.request++;
        check(refused(f.send(2, action), Error::not_ready),
              "zero-health controlled actor cannot act before native dead flag arrives");
    }
    update.entity.health = 5;
    update.entity.tick = 3;
    f.send(1, update);
    action.verb = Verb::jump;
    action.target = action.target_generation = 0;
    action.request++;
    out = f.send(2, action);
    check(out.size() == 1 && out[0].connection == 1, "subsequent positive native health permits an ordinary action");
}

void reloaded_corpse_tests() {
    // Original .124 trial54: saved NPC 86 is mbDead with health6 after load.
    // The wire must preserve both observations, without reviving it or granting
    // attack/social eligibility merely because its health is positive.
    Packet state;
    state.kind = Kind::motion;
    state.scene = 3;
    state.baseline = 1;
    state.entity = entity(103, 0);
    state.entity.tick = 2;
    state.entity.life_state = 1;
    state.entity.health = 6;
    state.entity.fed_on = 1;
    state.entity.pickup_owner = 2;
    state.entity.food = 11.799727f;
    auto wire = encode(state);
    Packet decoded;
    std::string error;
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.entity.life_state == 1 &&
              decoded.entity.health == 6,
          "reloaded original NPC corpse preserves positive health and dead flag");
    wire[12] = 15;
    check(!decode(wire.data(), wire.size(), decoded, error), "schema15 cannot accept new corpse semantics");
    state.entity.health = -1;
    wire = encode(state);
    check(!decode(wire.data(), wire.size(), decoded, error), "negative corpse health remains rejected");
    state.entity.health = 6;
    state.entity.owner = 1;
    wire = encode(state);
    check(!decode(wire.data(), wire.size(), decoded, error),
          "unqualified positive health on a dead owned actor remains rejected");
    state.entity.owner = 0;
    Fixture f;
    auto initial = f.initial();
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 3;
    ack.baseline = baseline(initial);
    f.send(2, ack);
    auto out = f.send(1, state);
    check(out.size() == 1 && out[0].packet.entity.health == 6 && out[0].packet.entity.life_state == 1,
          "authority publishes original reloaded corpse scalars");
    Packet action = ack;
    action.kind = Kind::action;
    action.entity = entity(101, 1);
    action.target = 103;
    action.target_generation = 7;
    for (auto verb : {Verb::attack, Verb::approach, Verb::engage, Verb::social, Verb::combat}) {
        action.verb = verb;
        action.request++;
        check(refused(f.send(2, action), Error::not_ready),
              "positive health does not permit attacks or social interaction with corpse");
    }
    action.verb = Verb::pickup;
    action.request++;
    out = f.send(2, action);
    check(out.size() == 1 && out[0].connection == 1 && out[0].packet.kind == Kind::action,
          "original dead flag and food admit eating a reloaded corpse");
    auto joined = f.join(3, Role::player, 2);
    auto corpse = find(joined, 103);
    check(corpse && corpse->life_state == 1 && corpse->health == 6 && corpse->fed_on == 1 &&
              corpse->pickup_owner == 2 && corpse->food == state.entity.food,
          "fresh baseline preserves corpse life health nutrition and historical beneficiary");
}
} // namespace

namespace {
void camera_steering_tests() {
    SteeringVector front, right, diagonal, decoded;
    check(camera_steering({0, 0, 500}, {0, 1, -0.5f}, {0.5f, 1, -0.5f}, 0, 1, front) && front.y > 0.999f,
          "camera forward follows view instead of actor facing");
    check(camera_steering({0, 0, 500}, {0, 1, -0.5f}, {0.5f, 1, -0.5f}, 1, 0, right) && right.x > 0.999f,
          "camera right follows screen right");
    check(camera_steering({0, 0, 500}, {0, 1, -0.5f}, {0.5f, 1, -0.5f}, 1, 1, diagonal) &&
              std::abs(steering_dot(diagonal, diagonal) - 1) < 0.00001f,
          "diagonal input does not increase speed");
    check(!camera_steering({0, 0, 0}, {0, 1, 0}, {1, 1, 0}, 0, 1, front), "zero radial position is rejected");
    check(!camera_steering({0, 0, 500}, {0, 1, 0}, {0, 1, 0}, 0, 1, front), "degenerate camera basis is rejected");
    check(!camera_steering({0, 0, 500}, {0, 1, 0}, {1, 1, 0}, 0, 0, front), "opposite keys have no direction");
    check(camera_steering({0, 0, 500}, {0, 0, -1}, {1, 0, -1}, 0, 1, front) && front.y > 0.999f,
          "vertical camera uses its screen-right basis");
    for (int h = -1; h <= 1; ++h) {
        for (int f = -1; f <= 1; ++f) {
            if (h || f) {
                SteeringVector intended;
                int32_t packed = 0;
                check(camera_steering({-423, 50, 266}, {-0.3f, 0.8f, -0.5f}, {0.1f, 0.9f, -0.5f}, h, f, intended) &&
                          pack_steering(intended, packed) && unpack_steering(packed, decoded) &&
                          steering_dot(intended, decoded) > 0.99999f,
                      "spherical camera directions roundtrip within quantization bound");
                Packet action;
                action.kind = Kind::action;
                action.entity = entity(101, 1);
                action.verb = Verb::steer;
                action.direction = packed;
                auto wire = encode(action);
                Packet roundtrip;
                std::string error;
                check(decode(wire.data(), wire.size(), roundtrip, error) && roundtrip.direction == packed,
                      "steering action codec preserves bounded direction");
                action.target = 103;
                action.target_generation = 7;
                check(!valid_action(action), "steering cannot smuggle an interaction target");
            }
        }
    }
    for (int32_t bad : {-1, 0, int32_t(1u << 30), int32_t(1023u | (511u << 10) | (511u << 20)),
                        int32_t(511u | (511u << 10) | (511u << 20))}) {
        check(!unpack_steering(bad, decoded), "reserved and nonunit steering words rejected");
    }
    Packet release;
    release.kind = Kind::action;
    release.entity = entity(101, 1);
    release.verb = Verb::steer_stop;
    Packet release_roundtrip;
    std::string release_error;
    auto release_wire = encode(release);
    check(decode(release_wire.data(), release_wire.size(), release_roundtrip, release_error) &&
              release_roundtrip.verb == Verb::steer_stop,
          "keyboard release survives codec with actor incarnation");
    release.direction = 1;
    check(!valid_action(release), "release cannot smuggle a direction");
    release.direction = 0;
    release.target = 103;
    release.target_generation = 7;
    check(!valid_action(release), "release cannot smuggle a target");
    int32_t packed = 0;
    check(!pack_steering({std::numeric_limits<float>::quiet_NaN(), 0, 0}, packed), "nonfinite input rejected");
}
} // namespace

int encounter_network_tests() {
    Packet p;
    p.kind = Kind::action;
    p.entity = entity(101, 1);
    p.verb = Verb::engage;
    p.target = 103;
    p.target_generation = 7;
    p.request = 41;
    std::string error;
    Packet decoded;
    auto wire = encode(p);
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.target_generation == 7 && decoded.target == 103,
          "target incarnation roundtrip");
    wire[12] = 1;
    check(!decode(wire.data(), wire.size(), decoded, error) && error == "unsupported_protocol_or_schema",
          "schema1 cannot omit target incarnation/life state");
    p.target_generation = 0;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "target incarnation required");
    p.target_generation = 7;
    p.verb = Verb::jump;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "untargeted command cannot smuggle target");
    p.kind = Kind::motion;
    p.entity.life_state = 1;
    p.entity.health = 0;
    wire = encode(p);
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.entity.life_state == 1 &&
              decoded.entity.health == 0,
          "terminal native state roundtrip");
    p.entity.health = 1;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "dead owned actor with positive health rejected");
    p.entity.health = 0;
    p.entity.life_state = 2;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "unknown native life state rejected");
    p.entity.life_state = 0;
    p.entity.health = 5;
    p.entity.age = 0;
    p.entity.alpha = 1;
    p.entity.combatant_state = 2;
    wire = encode(p);
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.entity.age == 0 && decoded.entity.alpha == 1 &&
              decoded.entity.combatant_state == 2,
          "native age alpha and combatant state roundtrip");
    for (size_t offset : {size_t(380), size_t(381), size_t(382)}) {
        auto bad = wire;
        bad[offset] = 3;
        check(!decode(bad.data(), bad.size(), decoded, error), "unsupported native context byte rejected");
    }
    wire[12] = 2;
    check(!decode(wire.data(), wire.size(), decoded, error), "schema2 cannot omit native age context");
    p.entity.age = 256;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error),
          "oversized native context cannot truncate to a valid byte");
    p.entity.age = 0;
    p.entity.combatant_state = 1;
    wire = encode(p);
    check(!decode(wire.data(), wire.size(), decoded, error), "unqualified native combatant state rejected");
    p.entity.combatant_state = 0;
    p.entity.scale = 0.375f;
    wire = encode(p);
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.entity.scale == 0.375f,
          "native observed scale roundtrip");
    check(wire.size() == 688 && wire[384] == 0 && wire[385] == 0 && wire[386] == 0xc0 && wire[387] == 0x3e,
          "native scale retains fixed offset and little endian encoding");
    for (float invalid :
         {0.f, -1.f, 1001.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        p.entity.scale = invalid;
        wire = encode(p);
        check(!decode(wire.data(), wire.size(), decoded, error), "invalid native scale rejected");
    }
    p.entity.scale = 1;
    wire = encode(p);
    wire[383] = 1;
    check(!decode(wire.data(), wire.size(), decoded, error), "context reserve remains rejected");

    Fixture f;
    auto initial = f.initial();
    auto b = baseline(initial);
    check(b && f.session.entity_count() == 3, "encounter baseline count");
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = 3;
    ack.baseline = b;
    f.send(2, ack);
    Packet action = ack;
    action.kind = Kind::action;
    action.entity = entity(101, 1);
    action.entity.native_id = 999;
    action.entity.health = 999;
    action.player = 2;
    action.verb = Verb::engage;
    action.target = 103;
    action.target_generation = 7;
    action.request = 1;
    auto out = f.send(2, action);
    check(out.size() == 1 && out[0].connection == 1 && out[0].packet.player == 1 &&
              out[0].packet.entity.native_id == 101 && out[0].packet.entity.health == 10,
          "untrusted client native fields replaced by authoritative values");
    action.target_generation = 6;
    action.request = 2;
    check(refused(f.send(2, action), Error::stale), "old target incarnation refused");
    action.target = 102;
    action.target_generation = 7;
    check(refused(f.send(2, action), Error::ownership), "owned target not silently admitted as PvP");
    action.target = 103;
    Packet update;
    update.kind = Kind::motion;
    update.scene = 3;
    update.baseline = 1;
    update.entity = entity(103, 0);
    update.entity.tick = 2;
    update.entity.health = 0;
    update.entity.life_state = 1;
    out = f.send(1, update);
    check(out.size() == 1 && out[0].packet.entity.life_state == 1 && f.session.entity_count() == 3,
          "native NPC death retained without despawn");
    check(refused(f.send(2, action), Error::not_ready), "attack against retained corpse refused");
    auto reconnect = f.join(3, Role::player, 2);
    auto corpse = find(reconnect, 103);
    check(corpse && corpse->life_state == 1 && corpse->health == 0 && corpse->generation == 7,
          "late join receives corpse not living fixture");
    update.entity = entity(101, 1);
    update.entity.tick = 2;
    update.entity.health = 0;
    update.entity.life_state = 1;
    update.entity.dna = 17.5f;
    f.send(1, update);
    action.verb = Verb::jump;
    action.target = action.target_generation = 0;
    check(refused(f.send(2, action), Error::not_ready), "dead controlled actor cannot act");
    f.session.disconnect(2);
    reconnect = f.join(4, Role::player, 1);
    auto player = find(reconnect, 101);
    corpse = find(reconnect, 103);
    check(player && player->life_state == 1 && player->dna == 17.5f && corpse && corpse->life_state == 1 &&
              f.session.entity_count() == 3,
          "post-death reconnect preserves balances corpse and actor count");
    ack.baseline = baseline(reconnect);
    f.send(4, ack);
    update.entity.health = 5;
    update.entity.life_state = 0;
    update.entity.age = 0;
    update.entity.alpha = 0;
    update.entity.combatant_state = 0;
    update.entity.tick = 3;
    out = f.send(1, update);
    check(out.size() == 2 && f.session.entity_count() == 3 && out[0].packet.entity.age == 0 &&
              out[0].packet.entity.combatant_state == 0,
          "same noun native revive preserves global identity and juvenile context");
    action.baseline = ack.baseline;
    action.request = 3;
    out = f.send(4, action);
    check(out.size() == 1 && out[0].connection == 1 && out[0].packet.entity.generation == 7 &&
              out[0].packet.entity.dna == 17.5f,
          "native revive restores intentions without reward addition");
    update.entity.tick = 2;
    update.entity.life_state = 1;
    update.entity.health = 0;
    out = f.send(1, update);
    check(out.size() == 1 && out[0].packet.error == Error::stale, "old terminal sample cannot undo native revive");
    camera_steering_tests();
    fossil_scene_tests();
    nest_scene_tests();
    pickup_codec_tests();
    pickup_session_tests();
    pending_death_tests();
    reloaded_corpse_tests();
    return assertions;
}
