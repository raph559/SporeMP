#include "checkpoint_artifact.h"
#include "coordinator.h"
#include "content_registry.h"
#include "durable_store.h"
#include "state_codec.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace sporemp::network;

namespace {
int checks = 0;

void check(bool yes, const char* why) {
    ++checks;
    if (!yes) {
        throw std::runtime_error(why);
    }
}

Digest digest(uint8_t n) {
    Digest d{};
    d.fill(n);
    return d;
}

LocationId a{1, 10, 100, 1000}, b{1, 10, 101, 1001};

Entity actor(uint32_t id, uint64_t owner) {
    Entity e;
    e.id = id;
    e.generation = 1;
    e.owner = owner;
    e.tick = 1;
    e.native_id = id;
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

std::vector<uint8_t> checkpoint(const Identity& identity) {
    state::Writer w;
    w.word(0x39424d53);
    w.word(2);
    for (const auto* d : {&identity.build, &identity.executable, &identity.content}) {
        w.digest(*d);
    }
    const std::vector<uint8_t> member{4, 9, 2, 7};
    Digest hash{};
    content_digest(member.data(), member.size(), hash);
    for (size_t n = 0; n < world_file_count; ++n) {
        w.digest(hash);
    }
    w.digest(digest(8));
    w.wide(0);
    w.word(0);
    for (auto id : {684349579u, 685579644u, 0u, 0u}) {
        w.word(id);
    }
    w.word(static_cast<uint32_t>(world_file_count + 1));
    for (const auto& file : world_files) {
        std::string name = std::string("spore/") + file.path;
        w.data(std::vector<uint8_t>(name.begin(), name.end()));
        w.digest(hash);
        w.data(member);
    }
    const std::string secondary = "spore/Games/Game0/Haagos.spo";
    w.data(std::vector<uint8_t>(secondary.begin(), secondary.end()));
    w.digest(hash);
    w.data(member);
    return std::move(w.bytes);
}

void policy() {
    LocationLedger l;
    std::string error;
    check(!l.place_avatar(1, a, error), "cannot place without active location");
    check(l.add_location({a, 11, 5, 2, digest(7), true}, error), "first location admitted");
    check(!l.add_location({b, 12, 9, 1, digest(7), true}, error) && error == "location_global_checkpoint_mismatch",
          "location cannot reference a different global checkpoint");
    check(l.add_location({b, 12, 9, 2, digest(7), true}, error), "second distinct worker admitted");
    check(l.checkpoints_share(2) && !l.checkpoints_share(3),
          "both locations require the selected global checkpoint head");
    check(!l.add_location({{1, 10, 102, 1002}, 11, 6, 2, digest(7), true}, error),
          "worker cannot own two active locations");
    check(l.place_avatar(1, a, error) && l.place_avatar(2, b, error), "two independently placed players");
    check(l.set_global_owner(GlobalSystem::economy, {11, 5}, error), "one global economy owner");
    check(!l.set_global_owner(GlobalSystem::economy, {12, 9}, error), "second global economy owner refused");
    check(!l.rebind_global(GlobalSystem::economy, {11, 5}, {12, 9}, error), "live global producer cannot be rebound");
    check(l.accepts_global_outcome(GlobalSystem::economy, 11, 5) &&
              !l.accepts_global_outcome(GlobalSystem::economy, 12, 9),
          "global outcome fenced");
    check(l.subscribe_interest(1, error) && l.accepts_interest(1, a, 1, 11, 5),
          "player interest follows its active location and worker lease");
    check(!l.accepts_interest(1, b, 1, 11, 5), "interest cannot observe another location");
    check(l.claim_global_event(GlobalSystem::economy, 11, 5, 77, error),
          "active global owner claims a durable event once");
    check(!l.claim_global_event(GlobalSystem::economy, 11, 5, 77, error), "replayed global event is deduplicated");
    check(!l.claim_global_event(GlobalSystem::economy, 12, 9, 78, error), "non-owner cannot claim global event");
    check(l.begin_transfer(1, 1, b, error), "prepare transfer");
    check(!l.begin_transfer(2, 1, b, error), "same avatar cannot transfer twice");
    check(l.accepts_avatar_outcome(1, a, 1, 11, 5), "prepare leaves source active");
    check(!l.destination_restored(1, 12, 9, error), "destination cannot restore before fence");
    check(!l.fence_source(1, 2, error), "wrong source generation refused");
    check(l.fence_source(1, 1, error), "source fence succeeds");
    check(!l.accepts_avatar_outcome(1, a, 1, 11, 5) && !l.accepts_avatar_outcome(1, b, 2, 12, 9),
          "no avatar authority during fenced window");
    check(!l.accepts_interest(1, a, 1, 11, 5), "source interest is revoked with its avatar fence");
    check(!l.destination_restored(1, 12, 8, error), "stale destination worker generation refused");
    check(l.destination_restored(1, 12, 9, error), "destination lease matches");
    check(!l.commit_transfer(2, error), "unrelated transfer cannot commit");
    check(l.commit_transfer(1, error), "ownership committed once");
    check(l.accepts_avatar_outcome(1, b, 3, 12, 9) && !l.accepts_avatar_outcome(1, a, 1, 11, 5),
          "source stale outcome refused after commit");
    check(l.subscribe_interest(1, error) && l.accepts_interest(1, b, 3, 12, 9),
          "destination interest starts only after committed ownership");
    check(!l.abort_transfer(1, 4, error), "committed transfer cannot be undone");
    check(l.finish_transfer(1, error), "cleanup committed transfer");
    check(l.begin_transfer(3, 1, a, error), "return trip prepares");
    check(l.fence_source(3, 3, error), "return trip fences");
    check(!l.abort_transfer(3, 4, error), "abort needs a newer recovered source generation");
    check(l.abort_transfer(3, 5, error) && l.accepts_avatar_outcome(1, b, 5, 12, 9),
          "recovered source resumes with fresh generation");
    check(l.subscribe_interest(1, error) && l.accepts_interest(1, b, 5, 12, 9),
          "recovered avatar receives fresh location interest");
    const auto bytes = l.snapshot();
    LocationLedger restored;
    check(restored.restore(bytes, error) && restored.accepts_avatar_outcome(1, b, 5, 12, 9) &&
              restored.accepts_interest(1, b, 5, 12, 9),
          "ledger codec preserves committed placement and interest");
    check(restored.global_event_count() == 1 && !restored.claim_global_event(GlobalSystem::economy, 11, 5, 77, error),
          "ledger codec preserves global event idempotency");
    auto corrupt = bytes;
    corrupt.push_back(0);
    check(!restored.restore(corrupt, error), "trailing bytes rejected");
    corrupt = bytes;
    corrupt.resize(8);
    check(!restored.restore(corrupt, error), "truncated ledger rejected");
}

void crashes() {
    std::string error;
    auto base = []() {
        LocationLedger l;
        std::string e;
        if (!l.add_location({a, 11, 5, 2, digest(7), true}, e) || !l.add_location({b, 12, 9, 2, digest(7), true}, e) ||
            !l.place_avatar(1, a, e) || !l.place_avatar(2, b, e) ||
            !l.set_global_owner(GlobalSystem::economy, {11, 5}, e)) {
            throw std::runtime_error("crash base");
        }
        return l;
    };
    auto roundtrip = [](const LocationLedger& l) {
        LocationLedger r;
        std::string e;
        if (!r.restore(l.snapshot(), e)) {
            throw std::runtime_error("crash record invalid");
        }
        return r;
    };
    auto l = base();
    check(l.worker_lost(a, 11, 5, error), "source worker loss recorded");
    l = roundtrip(l);
    check(!l.accepts_avatar_outcome(1, a, 1, 11, 5) && !l.accepts_global_outcome(GlobalSystem::economy, 11, 5),
          "source and global outcomes fenced after crash");
    check(!l.replace_worker(a, 11, 6, 3, error) && error == "location_global_checkpoint_mismatch",
          "worker replacement cannot split the global checkpoint head");
    check(l.replace_worker(a, 11, 6, 2, error), "source worker restored with new generation");
    check(l.accepts_avatar_outcome(1, a, 2, 11, 6) && !l.accepts_avatar_outcome(1, a, 1, 11, 5),
          "old worker and avatar generations refused");
    check(!l.accepts_global_outcome(GlobalSystem::economy, 11, 5), "global system still fenced until explicit rebind");
    check(l.rebind_global(GlobalSystem::economy, {11, 5}, {11, 6}, error) &&
              l.accepts_global_outcome(GlobalSystem::economy, 11, 6),
          "global owner rebound once");
    l = base();
    check(l.begin_transfer(1, 1, b, error) && l.worker_lost(a, 11, 5, error), "source crash after prepare");
    l = roundtrip(l);
    check(!l.accepts_avatar_outcome(1, a, 1, 11, 5), "prepared crash rejects old source");
    check(l.replace_worker(a, 11, 6, 2, error) && !l.transfer(1) && l.accepts_avatar_outcome(1, a, 2, 11, 6),
          "unfenced prepare aborts on source restore");
    l = base();
    check(l.begin_transfer(1, 1, b, error) && l.worker_lost(b, 12, 9, error), "destination crash during prepare");
    check(!l.fence_source(1, 1, error), "source cannot fence toward inactive destination");
    check(l.replace_worker(b, 12, 10, 2, error) && l.fence_source(1, 1, error),
          "prepared destination can recover before source fence");
    l = base();
    check(l.begin_transfer(1, 1, b, error) && l.fence_source(1, 1, error) && l.worker_lost(a, 11, 5, error),
          "source crash after fence");
    l = roundtrip(l);
    check(l.replace_worker(a, 11, 6, 2, error), "source restored after fenced crash");
    check(!l.accepts_avatar_outcome(1, a, 2, 11, 6) && l.abort_transfer(1, 3, error) &&
              l.accepts_avatar_outcome(1, a, 3, 11, 6),
          "fenced transfer requires explicit recovery abort");
    l = base();
    check(l.begin_transfer(1, 1, b, error) && l.fence_source(1, 1, error) && l.destination_restored(1, 12, 9, error),
          "destination restore phase");
    check(l.worker_lost(b, 12, 9, error), "destination crash before commit");
    l = roundtrip(l);
    check(!l.commit_transfer(1, error), "inactive destination cannot commit even with matching old generation");
    check(!l.accepts_avatar_outcome(2, b, 1, 12, 9) && l.replace_worker(b, 12, 10, 2, error),
          "destination recovered under new worker generation");
    check(!l.commit_transfer(1, error), "stale restored generation cannot commit");
    check(l.destination_restored(1, 12, 10, error) && l.commit_transfer(1, error),
          "new destination restoration can commit");
    check(l.accepts_avatar_outcome(1, b, 3, 12, 10) && !l.accepts_avatar_outcome(1, b, 3, 12, 9),
          "old destination generation fenced");
    check(l.worker_lost(b, 12, 10, error), "committed destination later crashes");
    l = roundtrip(l);
    check(l.replace_worker(b, 12, 11, 2, error) && l.accepts_avatar_outcome(1, b, 4, 12, 11),
          "committed avatar remains recoverable after destination crash");
}

void generation_exhaustion() {
    LocationLedger l;
    std::string error;
    check(l.add_location({a, 11, 5, 2, digest(7), true}, error) &&
              l.add_location({b, 12, 9, 2, digest(7), true}, error) && l.place_avatar(1, a, error),
          "generation exhaustion world admitted");
    check(l.begin_transfer(1, 1, b, error) && l.fence_source(1, 1, error) && l.abort_transfer(1, UINT64_MAX - 1, error),
          "source recovers near generation exhaustion");
    const auto before = l.snapshot();
    check(!l.begin_transfer(2, 1, b, error) && error == "avatar_generation_exhausted",
          "transfer refuses a source without generation space for both fence and completion");
    check(l.snapshot() == before && l.accepts_avatar_outcome(1, a, UINT64_MAX - 1, 11, 5) &&
              l.accepts_interest(1, a, UINT64_MAX - 1, 11, 5),
          "exhaustion refusal preserves source authority, interest and transfer high-water mark");
    LocationLedger recovered;
    check(recovered.restore(before, error) && recovered.accepts_avatar_outcome(1, a, UINT64_MAX - 1, 11, 5),
          "near-exhausted source still recovers from its durable record");
}

void connected_session_fencing() {
    // The ledger is the coordinator-owned durable policy object. Publishing a
    // new lease must also invalidate peers authenticated before that change.
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        LocationLedger ledger;
        std::string error;
        check(ledger.add_location({a, 11, 5, 2, digest(7), true}, error) &&
                  ledger.add_location({b, 12, 9, 2, digest(7), true}, error) && ledger.place_avatar(1, a, error) &&
                  ledger.place_avatar(2, b, error) && ledger.set_global_owner(GlobalSystem::economy, {11, 5}, error),
              "session-fence world admitted");
        const auto config = [&](const LocationId& location, uint64_t owner) {
            SessionConfig c;
            c.identity.build = digest(1);
            c.identity.executable = digest(2);
            c.identity.content = digest(7);
            c.identity.fixture = digest(4);
            c.identity.world.fill(digest(4));
            c.authority = digest(10);
            c.player1 = digest(11);
            c.player2 = digest(12);
            c.epoch = 42;
            c.location = location;
            c.worker_generation = ledger.location_leases().at(location).generation;
            c.checkpoint_revision = 2;
            c.permitted_owners = static_cast<uint8_t>(1u << owner);
            c.content_authority = owner == 1;
            return c;
        };
        const auto source_config = config(a, 1), other_config = config(b, 2);
        Session source(source_config, nullptr, &ledger), other(other_config, nullptr, &ledger);
        std::array<uint64_t, 3> source_sequences{}, other_sequences{};
        const auto send = [](Session& session, const SessionConfig& c, std::array<uint64_t, 3>& sequences,
                             size_t connection, Packet packet) {
            packet.sequence = ++sequences[connection];
            packet.session = packet.kind == Kind::hello ? 0 : c.epoch;
            packet.location = c.location;
            packet.worker_generation = connection == 1 ? c.worker_generation : 0;
            packet.native_load_slot = c.native_load_slot;
            return session.receive(connection, packet);
        };
        const auto initialize = [&](Session& session, const SessionConfig& c, std::array<uint64_t, 3>& sequences,
                                    uint64_t owner) {
            for (size_t connection = 1; connection <= 2; ++connection) {
                check(session.connect(connection), "pre-revocation session connects");
                Packet hello;
                hello.kind = Kind::hello;
                hello.role = connection == 1 ? Role::authority : Role::player;
                hello.identity = c.identity;
                hello.credential = connection == 1 ? c.authority : owner == 1 ? c.player1 : c.player2;
                const auto accepted = send(session, c, sequences, connection, hello);
                check(accepted.size() == 1 && accepted[0].packet.kind == Kind::welcome,
                      "pre-revocation session authenticates");
            }
            Packet scene;
            scene.kind = Kind::scene_begin;
            scene.scene = 1;
            scene.baseline = 1;
            scene.count = 1;
            send(session, c, sequences, 1, scene);
            scene.kind = Kind::entity;
            scene.entity = actor(static_cast<uint32_t>(100 + owner), owner);
            send(session, c, sequences, 1, scene);
            scene.kind = Kind::scene_end;
            const auto baseline = send(session, c, sequences, 1, scene);
            check(baseline.size() == 3 && session.entity_count() == 1, "pre-revocation scene published");
            Packet ack;
            ack.kind = Kind::baseline_ack;
            ack.scene = 1;
            ack.baseline = baseline[0].packet.baseline;
            send(session, c, sequences, 2, ack);
            ack.kind = Kind::action;
            ack.entity = actor(static_cast<uint32_t>(100 + owner), owner);
            ack.verb = Verb::jump;
            ack.request = 1;
            const auto routed = send(session, c, sequences, 2, ack);
            check(routed.size() == 1 && routed[0].connection == 1 && routed[0].packet.kind == Kind::action,
                  "pre-revocation acknowledged player action reaches its authority");
        };
        initialize(source, source_config, source_sequences, 1);
        initialize(other, other_config, other_sequences, 2);
        Packet late;
        late.kind = Kind::action;
        late.scene = 1;
        late.baseline = 1;
        late.entity = actor(101, 1);
        late.verb = Verb::jump;
        late.request = 2;
        size_t sender = 2;
        if (scenario == 0 || scenario == 1 || scenario == 4) {
            check(ledger.begin_transfer(1, 1, b, error) && source.lease_current(),
                  "prepare retains existing source session authority");
            if (scenario == 4) {
                Packet partial;
                partial.kind = Kind::scene_begin;
                partial.scene = 2;
                partial.baseline = 2;
                partial.count = 1;
                send(source, source_config, source_sequences, 1, partial);
                partial.kind = Kind::entity;
                partial.entity = actor(101, 1);
                send(source, source_config, source_sequences, 1, partial);
                late = partial;
                late.kind = Kind::scene_end;
                sender = 1;
            }
            check(ledger.fence_source(1, 1, error), "source avatar lease fenced in policy fixture");
            if (scenario == 1) {
                check(ledger.abort_transfer(1, 3, error),
                      "source recovers at same location under new avatar generation");
                late.kind = Kind::motion;
                late.entity.tick = 2;
                sender = 1;
            }
        }
        if (scenario == 2 || scenario == 5) {
            check(ledger.worker_lost(a, 11, 5, error), "worker lease revoked in policy fixture");
            if (scenario == 5) {
                check(ledger.replace_worker(a, 11, 6, 2, error) &&
                          ledger.rebind_global(GlobalSystem::economy, {11, 5}, {11, 6}, error),
                      "replacement worker lease cannot inherit the superseded session");
            }
            late.kind = Kind::action_result;
            late.player = 1;
            sender = 1;
        }
        if (scenario == 3) {
            check(ledger.unsubscribe_interest(1, error), "existing player interest revoked in policy fixture");
            late.kind = Kind::content_request;
        }
        check(!source.lease_current() && other.lease_current(),
              "lease revocation invalidates only the affected location session");
        check(send(source, source_config, source_sequences, sender, late).empty() && source.entity_count() == 0 &&
                  source.scene() == 0 && !source.authority_connected(),
              "late authenticated traffic cannot use cached scene, actions or staged baseline after revocation");
        Packet ack;
        ack.kind = Kind::baseline_ack;
        ack.scene = 1;
        ack.baseline = 1;
        check(send(source, source_config, source_sequences, 2, ack).empty() && !source.connect(3) &&
                  !source.can_rotate_disconnected_authority(6, digest(20)),
              "fenced session cannot be rearmed by late ACK, new connection or credential rotation");
        Packet continuation;
        continuation.kind = Kind::motion;
        continuation.scene = 1;
        continuation.baseline = 1;
        continuation.entity = actor(102, 2);
        continuation.entity.tick = 2;
        const auto continued = send(other, other_config, other_sequences, 1, continuation);
        check(continued.size() == 1 && continued[0].connection == 2 && continued[0].packet.kind == Kind::motion,
              "other location continues its authenticated scene after source revocation");
    }
}

void durable() {
    CoordinatorConfig config;
    config.port = 0;
    auto& identity = config.session.identity;
    identity.build = digest(1);
    identity.executable = digest(2);
    identity.content = digest(3);
    identity.fixture = digest(4);
    identity.world.fill(digest(4));
    config.state_directory = std::filesystem::current_path() / L"location-host" /
                             (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(config.state_directory);
    std::string error;
    Coordinator initial;
    check(initial.start(config, error), "universe created");
    initial.stop();
    config.offline = true;
    Coordinator seal;
    check(seal.start(config, error), "exclusive checkpoint writer starts");
    check(seal.commit_checkpoint(checkpoint(identity), 1, error), "fixture checkpoint committed");
    seal.stop();
    Coordinator first;
    check(first.start(config, error), "sealed universe reopens");
    check(!first.commit_locations(
              1, [](LocationLedger&, std::string&) { return true; }, error),
          "stale revision refused");
    check(!first.commit_locations(
              2, [](LocationLedger& l, std::string& e) { return l.add_location({a, 11, 5, 2, digest(7), true}, e); },
              error),
          "wrong content identity cannot publish a location");
    check(first.commit_locations(
              2,
              [](LocationLedger& l, std::string& e) {
                  return l.add_location({a, 11, 5, 2, digest(3), true, 0}, e) &&
                         l.add_location({b, 12, 9, 2, digest(3), true, 1}, e) && l.place_avatar(1, a, e) &&
                         l.place_avatar(2, b, e) && l.set_global_owner(GlobalSystem::economy, {11, 5}, e) &&
                         l.subscribe_interest(1, e) && l.claim_global_event(GlobalSystem::economy, 11, 5, 1, e);
              },
              error),
          "location roots, native-load mapping, interest and one global outcome atomically published");
    const auto universe = first.universe();
    first.stop();
    {
        auto online_config = config;
        online_config.offline = false;
        online_config.port = 0;
        Coordinator online;
        check(online.start(online_config, error), "one live coordinator restores both active location sessions");
        const auto ids = online.location_ids();
        check(ids.size() == 2 && ids[0] == a && ids[1] == b, "two canonical locations have live network routes");
        PeerConfig aa, pa, ab, pb;
        check(online.peer_config_at(Role::authority, 0, a, aa, error) &&
                  online.peer_config_at(Role::player, 1, a, pa, error) &&
                  online.peer_config_at(Role::authority, 0, b, ab, error) &&
                  online.peer_config_at(Role::player, 2, b, pb, error) && aa.native_load_slot == 0 &&
                  ab.native_load_slot == 1 && pb.native_load_slot == 1 && aa.primary_owner == 1 &&
                  pa.primary_owner == 1 && ab.primary_owner == 2 && pb.primary_owner == 2,
              "one-avatar locations export their exact leased owner and reviewed original save slot");
        PeerConfig wrong = pb;
        wrong.location = a;
        Peer authority_a, player_a, authority_b, player_b, wrong_player;
        check(authority_a.start(aa, error) && player_a.start(pa, error) && authority_b.start(ab, error) &&
                  player_b.start(pb, error) && wrong_player.start(wrong, error),
              "all native-shaped peers connect through one host coordinator");
        Peer* peers[]{&authority_a, &player_a, &authority_b, &player_b, &wrong_player};
        bool welcomed[4]{}, wrong_rejected = false;
        auto welcome_deadline = GetTickCount64() + 10000;
        while (GetTickCount64() < welcome_deadline &&
               !(welcomed[0] && welcomed[1] && welcomed[2] && welcomed[3] && wrong_rejected)) {
            CoordinatorEvent host_event;
            while (online.poll(host_event)) {}
            for (size_t p = 0; p < 5; ++p) {
                Event incoming;
                while (peers[p]->poll(incoming)) {
                    if (incoming.kind == EventKind::packet) {
                        if (p < 4 && incoming.packet.kind == Kind::welcome) {
                            welcomed[p] = true;
                        }
                        if (p == 4 && incoming.packet.kind == Kind::reject &&
                            incoming.packet.error == Error::not_ready) {
                            wrong_rejected = true;
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(welcomed[0] && welcomed[1] && welcomed[2] && welcomed[3] && wrong_rejected,
              "players cannot subscribe to a location outside their durable avatar lease");
        check(online.peer_config_at(Role::player, 1, a, pa, error) &&
                  online.peer_config_at(Role::player, 2, b, pb, error) &&
                  !online.peer_config_at(Role::player, 2, a, wrong, error),
              "location-specific invite generation enforces avatar interest");
        Packet begin_a;
        begin_a.kind = Kind::scene_begin;
        begin_a.scene = 101;
        begin_a.baseline = 1;
        begin_a.count = 1;
        Packet end_a = begin_a;
        end_a.kind = Kind::scene_end;
        Packet begin_b;
        begin_b.kind = Kind::scene_begin;
        begin_b.scene = 202;
        begin_b.baseline = 1;
        begin_b.count = 1;
        Packet end_b = begin_b;
        end_b.kind = Kind::scene_end;
        Packet owned_a = begin_a;
        owned_a.kind = Kind::entity;
        owned_a.entity = actor(101, 1);
        Packet owned_b = begin_b;
        owned_b.kind = Kind::entity;
        owned_b.entity = actor(202, 2);
        check(authority_a.send(begin_a) && authority_a.send(owned_a) && authority_a.send(end_a) &&
                  authority_b.send(begin_b) && authority_b.send(owned_b) && authority_b.send(end_b),
              "each location publishes only its leased native actor");
        bool player_a_scene = false, player_b_scene = false, leaked = false;
        auto scene_deadline = GetTickCount64() + 5000;
        while (GetTickCount64() < scene_deadline && !(player_a_scene && player_b_scene)) {
            CoordinatorEvent host_event;
            while (online.poll(host_event)) {}
            for (size_t p = 1; p <= 3; p += 2) {
                Event incoming;
                while (peers[p]->poll(incoming)) {
                    if (incoming.kind == EventKind::packet && incoming.packet.kind == Kind::scene_begin) {
                        if (p == 1 && incoming.packet.location == a && incoming.packet.scene == 101) {
                            player_a_scene = true;
                        } else if (p == 3 && incoming.packet.location == b && incoming.packet.scene == 202) {
                            player_b_scene = true;
                        } else {
                            leaked = true;
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(player_a_scene && player_b_scene && !leaked,
              "one coordinator keeps two scene baselines and location interest isolated");
        ContentFrame draft;
        draft.op = ContentOp::begin;
        draft.request = 1;
        Packet request;
        request.kind = Kind::content_request;
        bool cross_location_content_event = false;
        ContentFrame opened;
        check(encode_content(draft, request.content) && player_a.send(request),
              "location player starts a universe-scoped content transaction");
        auto content_deadline = GetTickCount64() + 5000;
        while (GetTickCount64() < content_deadline && !opened.transaction) {
            CoordinatorEvent host_event;
            while (online.poll(host_event)) {}
            Event incoming;
            while (player_a.poll(incoming)) {
                if (incoming.kind == EventKind::packet && incoming.packet.kind == Kind::content_response) {
                    ContentFrame response;
                    if (decode_content(incoming.packet.content, response) && response.op == ContentOp::begin &&
                        response.failure == ContentFailure::none) {
                        opened = response;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(opened.transaction != 0, "content transaction is admitted to the shared universe registry");
        ContentFrame cancel;
        cancel.op = ContentOp::cancel;
        cancel.request = 2;
        cancel.transaction = opened.transaction;
        request = {};
        request.kind = Kind::content_request;
        check(encode_content(cancel, request.content) && player_a.send(request),
              "location player cancels its content transaction");
        content_deadline = GetTickCount64() + 5000;
        while (GetTickCount64() < content_deadline && !cross_location_content_event) {
            CoordinatorEvent host_event;
            while (online.poll(host_event)) {}
            Event incoming;
            while (player_b.poll(incoming)) {
                if (incoming.kind == EventKind::packet && incoming.packet.kind == Kind::content_response) {
                    ContentFrame response;
                    if (decode_content(incoming.packet.content, response) &&
                        response.op == ContentOp::cancelled_event && response.transaction == opened.transaction) {
                        cross_location_content_event = true;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(cross_location_content_event,
              "shared content cancellation reaches a player in another location with that recipient session");
        Packet ghost_begin = begin_a;
        ghost_begin.scene = 102;
        ghost_begin.baseline = 2;
        ghost_begin.count = 2;
        Packet ghost = owned_b;
        ghost.scene = 102;
        ghost.baseline = 2;
        check(authority_a.send(ghost_begin) && authority_a.send(ghost),
              "ghost avatar challenge sent to the Satiria-shaped location");
        bool ghost_rejected = false, ghost_leaked = false;
        auto ghost_deadline = GetTickCount64() + 5000;
        while (GetTickCount64() < ghost_deadline && !ghost_rejected) {
            CoordinatorEvent host_event;
            while (online.poll(host_event)) {}
            Event incoming;
            while (authority_a.poll(incoming)) {
                if (incoming.kind == EventKind::packet && incoming.packet.kind == Kind::reject &&
                    incoming.packet.error == Error::ownership) {
                    ghost_rejected = true;
                }
            }
            while (player_a.poll(incoming)) {
                if (incoming.kind == EventKind::packet && incoming.packet.kind == Kind::scene_begin &&
                    incoming.packet.scene == 102) {
                    ghost_leaked = true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(ghost_rejected && !ghost_leaked,
              "coordinator refuses an authority scene containing an avatar leased to another location");
        online.stop();
    }
    Coordinator prepared;
    check(prepared.start(config, error) && prepared.universe() == universe, "same canonical universe reopens");
    check(prepared.locations().location_count() == 2 && prepared.locations().accepts_avatar_outcome(2, b, 1, 12, 9) &&
              prepared.locations().accepts_interest(1, a, 1, 11, 5) && prepared.locations().global_event_count() == 1,
          "locations, interest and global event head persist");
    check(prepared.commit_locations(
              prepared.durable_revision(),
              [](LocationLedger& l, std::string& e) { return l.begin_transfer(1, 1, b, e); }, error),
          "prepare record published");
    prepared.stop();
    Coordinator fenced;
    check(fenced.start(config, error), "prepared state recovers");
    check(fenced.locations().transfer(1)->phase == TransferPhase::prepared, "prepare phase durable");
    check(fenced.commit_locations(
              fenced.durable_revision(), [](LocationLedger& l, std::string& e) { return l.fence_source(1, 1, e); },
              error),
          "fence record published");
    fenced.stop();
    Coordinator resumed;
    check(resumed.start(config, error), "fenced state recovers");
    check(!resumed.locations().accepts_avatar_outcome(1, a, 1, 11, 5) &&
              !resumed.locations().accepts_avatar_outcome(1, b, 2, 12, 9),
          "no split authority after restart");
    check(resumed.commit_locations(
              resumed.durable_revision(),
              [](LocationLedger& l, std::string& e) { return l.destination_restored(1, 12, 9, e); }, error),
          "restore phase published");
    check(resumed.commit_locations(
              resumed.durable_revision(), [](LocationLedger& l, std::string& e) { return l.commit_transfer(1, e); },
              error),
          "commit phase published");
    resumed.stop();
    Coordinator final;
    check(final.start(config, error), "committed transfer recovers");
    check(final.locations().accepts_avatar_outcome(1, b, 3, 12, 9) &&
              !final.locations().accepts_avatar_outcome(1, a, 1, 11, 5),
          "one avatar owner after restart");
    check(!final.locations().accepts_interest(1, a, 1, 11, 5) && final.locations().accepts_interest(1, b, 3, 12, 9),
          "committed transfer installs only destination interest");
    auto replay = final.locations();
    check(!replay.claim_global_event(GlobalSystem::economy, 11, 5, 1, error),
          "coordinator restart does not replay a committed global event");
    check(final.locations().accepts_global_outcome(GlobalSystem::economy, 11, 5) &&
              !final.locations().accepts_global_outcome(GlobalSystem::economy, 12, 9),
          "global lease remains single after transfer");
    final.stop();
}

void live_replacement() {
    CoordinatorConfig config;
    config.port = 0;
    auto& identity = config.session.identity;
    identity.build = digest(21);
    identity.executable = digest(22);
    identity.content = digest(23);
    identity.fixture = digest(24);
    identity.world.fill(digest(24));
    config.state_directory =
        std::filesystem::current_path() / L"location-host" /
        (L"replace-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(config.state_directory);
    std::string error;
    Coordinator initial;
    check(initial.start(config, error), "replacement universe created");
    initial.stop();
    config.offline = true;
    Coordinator selected;
    check(selected.start(config, error) && selected.commit_checkpoint(checkpoint(identity), 1, error),
          "replacement selected world committed");
    check(selected.commit_locations(
              2,
              [](LocationLedger& ledger, std::string& reason) {
                  return ledger.add_location({a, 11, 5, 2, digest(23), true, 0}, reason) &&
                         ledger.add_location({b, 12, 9, 2, digest(23), true, 1}, reason) &&
                         ledger.place_avatar(1, a, reason) && ledger.place_avatar(2, b, reason) &&
                         ledger.set_global_owner(GlobalSystem::economy, {11, 5}, reason);
              },
              error),
          "replacement two-location head committed");
    selected.stop();
    config.offline = false;
    Coordinator online;
    check(online.start(config, error), "replacement online coordinator starts");
    PeerConfig old_a, new_a, b_auth, p1, p2;
    check(online.peer_config_at(Role::authority, 0, a, old_a, error) &&
              online.peer_config_at(Role::authority, 0, b, b_auth, error) &&
              online.peer_config_at(Role::player, 1, a, p1, error) &&
              online.peer_config_at(Role::player, 2, b, p2, error),
          "replacement routes exported");
    Peer authority_a, authority_b, player_a, player_b;
    check(authority_a.start(old_a, error) && authority_b.start(b_auth, error) && player_a.start(p1, error) &&
              player_b.start(p2, error),
          "replacement TLS peers start");
    bool welcomed[4]{};
    Peer* peers[]{&authority_a, &authority_b, &player_a, &player_b};
    auto deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline && !(welcomed[0] && welcomed[1] && welcomed[2] && welcomed[3])) {
        CoordinatorEvent host;
        while (online.poll(host)) {}
        for (size_t index = 0; index < 4; ++index) {
            Event e;
            while (peers[index]->poll(e)) {
                if (e.kind == EventKind::packet && e.packet.kind == Kind::welcome) {
                    welcomed[index] = true;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(welcomed[0] && welcomed[1] && welcomed[2] && welcomed[3], "replacement all four peers authenticate");
    Packet scene;
    scene.kind = Kind::scene_begin;
    scene.scene = 3;
    scene.baseline = 1;
    scene.count = 1;
    check(authority_a.send(scene), "original authority scene begin");
    scene.kind = Kind::entity;
    scene.entity = actor(101, 1);
    check(authority_a.send(scene), "original authority entity");
    scene.kind = Kind::scene_end;
    check(authority_a.send(scene), "original authority scene end");
    uint64_t first_baseline = 0;
    deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline && !first_baseline) {
        CoordinatorEvent host;
        while (online.poll(host)) {}
        Event e;
        while (player_a.poll(e)) {
            if (e.kind == EventKind::packet && e.packet.kind == Kind::scene_end) {
                first_baseline = e.packet.baseline;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(first_baseline != 0, "original player baseline received");
    check(!online.replace_disconnected_location(a, 11, 5, 11, 6, 3, error) &&
              error == "location_replacement_stale_or_authority_connected",
          "live old authority cannot be silently replaced");
    authority_a.stop();
    bool replaced = false;
    deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline && !replaced) {
        CoordinatorEvent host;
        while (online.poll(host)) {}
        Event e;
        while (player_a.poll(e)) {}
        replaced = online.replace_disconnected_location(a, 11, 5, 11, 6, 3, error);
        if (!replaced && error != "location_replacement_stale_or_authority_connected") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(replaced && online.durable_revision() == 4,
          "disconnected authority lease replaced durably while other location stays live");
    check(online.replace_disconnected_location(a, 11, 5, 11, 6, 3, error) && online.durable_revision() == 4,
          "uncertain replacement retry is idempotent");
    check(!online.replace_disconnected_location(a, 99, 5, 11, 6, 3, error) &&
              !online.replace_disconnected_location(a, 11, 4, 11, 6, 3, error) && online.durable_revision() == 4,
          "replacement retry rejects altered previous worker or generation without publishing");
    check(online.peer_config_at(Role::authority, 0, a, new_a, error) && new_a.worker_generation == 6 &&
              new_a.credential != old_a.credential && new_a.primary_owner == 1 && new_a.identity == old_a.identity,
          "replacement rotates credential and generation without changing selected native head");
    check(!online.locations().accepts_avatar_outcome(1, a, 1, 11, 5) &&
              online.locations().accepts_avatar_outcome(1, a, 2, 11, 6) &&
              !online.locations().accepts_global_outcome(GlobalSystem::economy, 11, 5) &&
              online.locations().accepts_global_outcome(GlobalSystem::economy, 11, 6),
          "old avatar and global producer generations fenced in the durable head");
    Peer stale, fresh;
    check(stale.start(old_a, error) && fresh.start(new_a, error),
          "old and new authority credentials challenged over TLS");
    bool stale_denied = false, fresh_welcomed = false;
    deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline && !(stale_denied && fresh_welcomed)) {
        CoordinatorEvent host;
        while (online.poll(host)) {}
        Event e;
        while (stale.poll(e)) {
            if (e.kind == EventKind::packet && e.packet.kind == Kind::reject && e.packet.error == Error::stale) {
                stale_denied = true;
            }
        }
        while (fresh.poll(e)) {
            if (e.kind == EventKind::packet && e.packet.kind == Kind::welcome) {
                fresh_welcomed = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(stale_denied && fresh_welcomed, "old generation refused and replacement authority welcomed");
    scene = {};
    scene.kind = Kind::scene_begin;
    scene.scene = 3;
    scene.baseline = 1;
    scene.count = 1;
    check(fresh.send(scene), "replacement native scene counter restarts");
    scene.kind = Kind::entity;
    scene.entity = actor(101, 1);
    check(fresh.send(scene), "replacement owner-1 entity published");
    scene.kind = Kind::scene_end;
    check(fresh.send(scene), "replacement scene complete");
    uint64_t next_baseline = 0;
    deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline && !next_baseline) {
        CoordinatorEvent host;
        while (online.poll(host)) {}
        Event e;
        while (player_a.poll(e)) {
            if (e.kind == EventKind::packet && e.packet.kind == Kind::scene_end) {
                next_baseline = e.packet.baseline;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(next_baseline > first_baseline, "retained player receives a fresh replacement baseline");
    Packet other;
    other.kind = Kind::scene_begin;
    other.scene = 7;
    other.baseline = 1;
    other.count = 1;
    check(authority_b.send(other), "other location authority remains live during replacement");
    other.kind = Kind::entity;
    other.entity = actor(202, 2);
    check(authority_b.send(other), "other location entity still admitted");
    other.kind = Kind::scene_end;
    check(authority_b.send(other), "other location scene completes");
    bool other_baseline = false;
    deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline && !other_baseline) {
        CoordinatorEvent host;
        while (online.poll(host)) {}
        Event e;
        while (player_b.poll(e)) {
            if (e.kind == EventKind::packet && e.packet.kind == Kind::scene_end && e.packet.scene == 7) {
                other_baseline = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(other_baseline, "other location player remains connected across durable replacement");
    stale.stop();
    fresh.stop();
    authority_b.stop();
    player_a.stop();
    player_b.stop();
    online.stop();
    config.offline = true;
    Coordinator recovered;
    check(recovered.start(config, error) && recovered.durable_revision() == 4 &&
              recovered.locations().accepts_avatar_outcome(1, a, 2, 11, 6) &&
              recovered.locations().accepts_global_outcome(GlobalSystem::economy, 11, 6),
          "new worker and global generation survive coordinator recovery");
    recovered.stop();
    config.offline = false;
    Coordinator reopened;
    check(reopened.start(config, error), "replacement retry coordinator reopens online");
    check(reopened.replace_disconnected_location(a, 11, 5, 11, 6, 3, error) && reopened.durable_revision() == 4,
          "exact uncertain-ack replacement retry survives durable reopen without another generation");
    check(!reopened.replace_disconnected_location(a, 99, 5, 11, 6, 3, error) &&
              !reopened.replace_disconnected_location(a, 11, 4, 11, 6, 3, error) && reopened.durable_revision() == 4,
          "recovered receipt refuses altered previous leases although resulting lease matches");
    reopened.stop();
    // Exercise the actual durable record across schema boundaries, including
    // checksummed but semantically invalid receipts. Only this test's store is changed.
    std::vector<uint8_t> receipt_record;
    {
        DurableStore store;
        check(store.open(config.state_directory, receipt_record, error), "replacement receipt record opened");
    }
    state::Reader envelope{receipt_record};
    check(envelope.word() == 0x39554d53 && envelope.word() == 9, "new coordinator records use schema 9");
    for (size_t index = 0; index < 1 + 4 + world_file_count * 2 + 3; ++index) {
        envelope.digest();
    }
    envelope.data(128 * 1024);
    envelope.data(ContentRegistry::max_cached_bytes + 256 * 1024);
    envelope.wide();
    envelope.data(CheckpointArtifact::max_bytes);
    const auto ledger_at = envelope.at;
    envelope.data(64 * 1024);
    const auto receipt_at = envelope.at;
    check(envelope.word() == 1 && receipt_record.size() == receipt_at + 4 + 9 * 8 + 8 + 32,
          "receipt has the bounded complete request tuple and empty transfer/publication extensions");
    config.offline = true;
    Coordinator unrelated;
    check(unrelated.start(config, error) && unrelated.commit_locations(
                                                4,
                                                [](LocationLedger& ledger, std::string& reason) {
                                                    return ledger.claim_global_event(GlobalSystem::economy, 11, 6, 17,
                                                                                     reason);
                                                },
                                                error),
          "unrelated durable publication follows the worker replacement");
    unrelated.stop();
    config.offline = false;
    Coordinator retired;
    check(retired.start(config, error) && retired.durable_revision() == 5 &&
              !retired.replace_disconnected_location(a, 11, 5, 11, 6, 3, error) &&
              !retired.replace_disconnected_location(a, 11, 5, 11, 6, 4, error),
          "unrelated publication retires the receipt and cannot manufacture a newer exact retry");
    retired.stop();
    const auto set_wide = [](std::vector<uint8_t>& bytes, size_t at, uint64_t value) {
        for (unsigned n = 0; n < 8; ++n) {
            bytes.at(at + n) = static_cast<uint8_t>(value >> (n * 8));
        }
    };
    const auto malformed = [&](unsigned challenge) {
        {
            DurableStore store;
            std::vector<uint8_t> previous;
            check(store.open(config.state_directory, previous, error), "malformed receipt fixture writer opens");
            auto bytes = receipt_record;
            set_wide(bytes, receipt_at + 4 + 8 * 8, store.revision());
            if (challenge == 0) {
                bytes[receipt_at] = 2; // Unknown optional-record flag.
            }
            if (challenge == 1) {
                set_wide(bytes, receipt_at + 4 + 4 * 8, 0); // Missing previous worker.
            }
            if (challenge == 2) {
                set_wide(bytes, receipt_at + 4 + 6 * 8, 99); // Result contradicts the ledger.
            }
            if (challenge == 3) {
                set_wide(bytes, receipt_at + 4 + 8 * 8, store.revision() + 1); // Future receipt.
            }
            if (challenge == 4) {
                bytes.resize(receipt_at + 4 + 9 * 8 - 1); // Truncated request.
            }
            check(store.commit(bytes, error), "checksummed malformed receipt fixture published");
        }
        Coordinator invalid;
        check(!invalid.start(config, error) && error == "location_replacement_receipt_invalid",
              "malformed receipt refuses recovery before an online listener starts");
    };
    for (unsigned challenge = 0; challenge < 5; ++challenge) {
        malformed(challenge);
    }
    for (uint8_t schema : {uint8_t(5), uint8_t(6)}) {
        {
            DurableStore store;
            std::vector<uint8_t> previous;
            check(store.open(config.state_directory, previous, error), "schema5/6 migration writer opens");
            auto legacy = receipt_record;
            legacy.resize(legacy.size() - 8);
            if (schema == 5) {
                legacy.resize(legacy.size() - 24);
            }
            legacy[4] = schema;
            set_wide(legacy, receipt_at + 4 + 8 * 8, store.revision());
            check(store.commit(legacy, error), "older coordinator envelope published with exact receipt");
        }
        Coordinator legacy;
        check(legacy.start(config, error) &&
                  legacy.replace_disconnected_location(a, 11, 5, 11, 6, legacy.durable_revision() - 1, error),
              "schema5 and schema6 preserve replacement identity and location state on migration");
        legacy.stop();
    }
    {
        DurableStore store;
        std::vector<uint8_t> previous;
        check(store.open(config.state_directory, previous, error), "schema-4 fixture writer opens");
        auto legacy = receipt_record;
        legacy.resize(receipt_at + 4 + 9 * 8);
        legacy[4] = 4;
        set_wide(legacy, receipt_at + 4 + 8 * 8, store.revision());
        check(store.commit(legacy, error), "schema-4 record with its exact replacement receipt published");
    }
    Coordinator legacy4;
    check(legacy4.start(config, error) &&
              legacy4.replace_disconnected_location(a, 11, 5, 11, 6, legacy4.durable_revision() - 1, error),
          "schema-4 replacement receipt recovers with its exact retry contract");
    legacy4.stop();
    {
        DurableStore store;
        std::vector<uint8_t> previous;
        check(store.open(config.state_directory, previous, error), "legacy schema fixture writer opens");
        auto legacy = receipt_record;
        legacy.resize(receipt_at);
        legacy[4] = 3;
        check(store.commit(legacy, error), "schema-3 record without receipt published");
    }
    Coordinator legacy3;
    check(legacy3.start(config, error) && legacy3.locations().accepts_avatar_outcome(1, a, 2, 11, 6),
          "schema-3 coordinator state recovers its existing location leases");
    check(!legacy3.replace_disconnected_location(a, 11, 5, 11, 6, legacy3.durable_revision() - 1, error),
          "legacy state cannot infer an exact replacement retry from the resulting lease alone");
    legacy3.stop();
    {
        DurableStore store;
        std::vector<uint8_t> previous;
        check(store.open(config.state_directory, previous, error), "schema-2 fixture writer opens");
        auto legacy = receipt_record;
        legacy.resize(ledger_at);
        legacy[4] = 2;
        check(store.commit(legacy, error), "schema-2 record without location extension published");
    }
    Coordinator legacy2;
    check(legacy2.start(config, error) && legacy2.checkpoint_revision() == 2 &&
              legacy2.locations().location_count() == 0,
          "schema-2 coordinator state still recovers its selected native checkpoint");
    legacy2.stop();
}
} // namespace

int main() {
    try {
        policy();
        crashes();
        generation_exhaustion();
        connected_session_fencing();
        durable();
        live_replacement();
        std::cout << "PASS " << checks << " HOST location-policy and durable transfer checks; SPORE NOT RUN.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
