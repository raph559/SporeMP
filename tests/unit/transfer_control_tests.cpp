#include "transfer_control.h"
#include "durable_store.h"
#include "state_codec.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace sporemp::network;

namespace {
int checks = 0;

void check(bool value, const char* why) {
    ++checks;
    if (!value) {
        throw std::runtime_error(why);
    }
}

Digest digest(uint8_t value) {
    Digest result{};
    result.fill(value);
    return result;
}

const LocationId source{1, 10, 100, 1000}, destination{1, 10, 101, 1001}, unrelated{1, 10, 102, 1002};

Entity actor(uint64_t id, uint64_t owner) {
    Entity e;
    e.id = id;
    e.generation = 1;
    e.owner = owner;
    e.tick = 1;
    e.native_id = static_cast<uint32_t>(id);
    e.health = 100;
    e.energy = 50;
    e.hunger = 30;
    return e;
}

std::vector<uint8_t> checkpoint(const Identity& identity) {
    state::Writer out;
    out.word(0x39424d53);
    out.word(2);
    for (const auto* value : {&identity.build, &identity.executable, &identity.content}) {
        out.digest(*value);
    }
    const std::vector<uint8_t> member{4, 9, 2, 7};
    Digest hash{};
    check(content_digest(member.data(), member.size(), hash), "fixture digest computed");
    for (size_t n = 0; n < world_file_count; ++n) {
        out.digest(hash);
    }
    out.digest(digest(8));
    out.wide(0);
    out.word(0);
    for (auto id : {684349579u, 685579644u, 0u, 0u}) {
        out.word(id);
    }
    out.word(static_cast<uint32_t>(world_file_count + 1));
    for (const auto& file : world_files) {
        const std::string name = std::string("spore/") + file.path;
        out.data({name.begin(), name.end()});
        out.digest(hash);
        out.data(member);
    }
    const std::string secondary = "spore/Games/Game0/Haagos.spo";
    out.data({secondary.begin(), secondary.end()});
    out.digest(hash);
    out.data(member);
    return std::move(out.bytes);
}

struct Peers {
    Coordinator& host;
    std::array<Peer, 5> peers;
    std::array<std::vector<Packet>, 5> packets;
    bool paired = false;

    explicit Peers(Coordinator& server, bool colocated = false) : host(server), paired(colocated) {}

    void poll() {
        CoordinatorEvent event;
        while (host.poll(event)) {}
        for (size_t n = 0; n < peers.size(); ++n) {
            Event incoming;
            while (peers[n].poll(incoming)) {
                if (incoming.kind == EventKind::packet) {
                    packets[n].push_back(incoming.packet);
                }
            }
        }
    }

    template <class Predicate>
    void until(Predicate predicate, const char* why) {
        const auto deadline = GetTickCount64() + 5000;
        while (GetTickCount64() < deadline) {
            poll();
            if (predicate()) {
                check(true, why);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(false, why);
    }

    bool has(size_t index, Kind kind, uint64_t tick = 0) const {
        return std::any_of(packets[index].begin(), packets[index].end(), [&](const Packet& packet) {
            return packet.kind == kind && (!tick || packet.entity.tick == tick);
        });
    }

    void start() {
        std::string error;
        const LocationId locations[]{source, destination, unrelated, source, paired ? destination : unrelated};
        for (size_t n = 0; n < peers.size(); ++n) {
            PeerConfig config;
            check(host.peer_config_at(n < 3 ? Role::authority : Role::player, n < 3 ? 0 : n - 2, locations[n], config,
                                      error) &&
                      peers[n].start(config, error),
                  "three authorities and two players start TLS");
        }
        until(
            [&] {
                for (size_t n = 0; n < peers.size(); ++n) {
                    if (!has(n, Kind::welcome)) {
                        return false;
                    }
                }
                return true;
            },
            "all five independent TLS peers authenticate");
    }

    void baselines() {
        for (size_t n = 0; n < 3; ++n) {
            Packet packet;
            packet.kind = Kind::scene_begin;
            packet.scene = 1;
            packet.baseline = 1;
            packet.count = n == 0 || n == (paired ? 1u : 2u) ? 1 : 0;
            check(peers[n].send(packet), "authority scene starts");
            if (packet.count) {
                packet.kind = Kind::entity;
                packet.entity = actor(n == 0 ? 101 : 202, n == 0 ? 1 : 2);
                check(peers[n].send(packet), "authority scene actor sent");
            }
            packet.kind = Kind::scene_end;
            check(peers[n].send(packet), "authority scene ends");
        }
        until([&] { return has(3, Kind::scene_end) && has(4, Kind::scene_end); },
              "source and unrelated players receive complete baselines");
        for (size_t n = 3; n < 5; ++n) {
            Packet ack;
            ack.kind = Kind::baseline_ack;
            ack.scene = 1;
            for (const auto& packet : packets[n]) {
                if (packet.kind == Kind::scene_end) {
                    ack.baseline = packet.baseline;
                }
            }
            check(peers[n].send(ack), "player baseline acknowledgement sent");
        }
    }

    void motion_and_action(uint64_t tick) {
        Packet motion;
        motion.kind = Kind::motion;
        motion.scene = 1;
        motion.baseline = 1;
        motion.entity = actor(202, 2);
        motion.entity.tick = tick;
        motion.entity.x = static_cast<float>(tick);
        check(peers[2].send(motion), "unrelated authority sends continuing motion");
        until([&] { return has(4, Kind::motion, tick); }, "unrelated location retains its baseline and motion route");
        Packet action;
        action.kind = Kind::action;
        action.scene = 1;
        action.entity = actor(202, 2);
        action.verb = Verb::jump;
        action.request = tick;
        for (const auto& packet : packets[4]) {
            if (packet.kind == Kind::scene_end) {
                action.baseline = packet.baseline;
            }
        }
        check(peers[4].send(action), "unrelated player sends continuing action");
        until(
            [&] {
                return std::any_of(packets[2].begin(), packets[2].end(), [&](const Packet& packet) {
                    return packet.kind == Kind::action && packet.request == tick && packet.player == 2;
                });
            },
            "unrelated location retains acknowledged gameplay readiness");
    }

    void stop() {
        for (auto& peer : peers) {
            peer.stop();
        }
    }

    ~Peers() {
        stop();
    }
};

std::string request_text(const TransferPreparation& r, const std::string& op, uint64_t expected) {
    const auto location = [](const LocationId& id) {
        return std::to_string(id.galaxy) + "," + std::to_string(id.star) + "," + std::to_string(id.planet) + "," +
               std::to_string(id.area);
    };
    return "schema=1\nop=" + op + "\nid=" + std::to_string(r.id) + "\nplayer=" + std::to_string(r.player) +
           "\nexpected_revision=" + std::to_string(expected) +
           "\nprepare_expected_revision=" + std::to_string(r.expected_revision) +
           "\ncheckpoint_revision=" + std::to_string(r.checkpoint_revision) +
           "\navatar_generation=" + std::to_string(r.avatar_generation) +
           "\nsource_worker=" + std::to_string(r.source_worker) +
           "\nsource_worker_generation=" + std::to_string(r.source_worker_generation) +
           "\ndestination_worker=" + std::to_string(r.destination_worker) +
           "\ndestination_worker_generation=" + std::to_string(r.destination_worker_generation) +
           "\nsource=" + location(r.source) + "\ndestination=" + location(r.destination) +
           "\nuniverse=" + hex(r.universe) + "\ncheckpoint_sha256=" + hex(r.checkpoint_hash) + "\n";
}

void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    output.close();
    check(bool(output), "private fixture request written");
}

std::string control(Coordinator& host, const std::filesystem::path& directory, const std::string& text) {
    write(directory / L"transfer.request", text);
    std::string error;
    check(process_transfer_request(host, directory, error), "production local control claims a request");
    std::ifstream input(directory / L"transfer-receipt.json", std::ios::binary);
    std::string receipt{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    check(receipt.find("\"native_completion\":false,\"ownership_changed\":false") != std::string::npos,
          "control receipt explicitly withholds native completion and ownership changes");
    if (!text.empty() && text.size() <= 2048) {
        Digest hash{};
        check(content_digest(reinterpret_cast<const uint8_t*>(text.data()), text.size(), hash) &&
                  receipt.find("\"request_sha256\":\"" + hex(hash) + "\"") != std::string::npos,
              "receipt correlates the exact bounded request bytes including operation and revision");
    }
    check(!std::filesystem::exists(directory / L"transfer.claimed"), "acknowledged private claim cleaned up");
    return receipt;
}

void set_wide(std::vector<uint8_t>& bytes, size_t at, uint64_t value) {
    for (unsigned n = 0; n < 8; ++n) {
        bytes.at(at + n) = static_cast<uint8_t>(value >> (n * 8));
    }
}

size_t preparation_offset(const std::vector<uint8_t>& bytes) {
    state::Reader in{bytes};
    check(in.word() == 0x39554d53 && in.word() == 9, "live control persists coordinator schema 9");
    for (size_t n = 0; n < 1 + 4 + world_file_count * 2 + 3; ++n) {
        in.digest();
    }
    in.data(128 * 1024);
    in.data(ContentRegistry::max_cached_bytes + 256 * 1024);
    in.wide();
    in.data(CheckpointArtifact::max_bytes);
    in.data(64 * 1024);
    if (in.word()) {
        for (size_t n = 0; n < 9; ++n) {
            in.wide();
        }
    }
    return in.at;
}

void live_control() {
    CoordinatorConfig config;
    config.port = 0;
    auto& identity = config.session.identity;
    identity.build = digest(31);
    identity.executable = digest(32);
    identity.content = digest(33);
    identity.fixture = digest(34);
    identity.world.fill(digest(34));
    const auto directory = std::filesystem::current_path() / L"transfer-host" /
                           (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    check(std::filesystem::create_directories(directory), "fresh private host fixture directory");
    config.state_directory = directory / L"state";
    check(std::filesystem::create_directory(config.state_directory), "fresh private durable store directory");
    std::string error;
    Coordinator created;
    check(created.start(config, error), "universe created");
    created.stop();
    config.offline = true;
    Coordinator sealed;
    check(sealed.start(config, error) && sealed.commit_checkpoint(checkpoint(identity), 1, error),
          "HOST checkpoint selected");
    check(sealed.commit_locations(
              2,
              [](LocationLedger& ledger, std::string& why) {
                  return ledger.add_location({source, 11, 5, 2, digest(33), true, 0}, why) &&
                         ledger.add_location({destination, 12, 9, 2, digest(33), true, 1}, why) &&
                         ledger.add_location({unrelated, 13, 4, 2, digest(33), true, 0}, why) &&
                         ledger.place_avatar(1, source, why) && ledger.place_avatar(2, unrelated, why) &&
                         ledger.set_global_owner(GlobalSystem::economy, {11, 5}, why);
              },
              error),
          "three host location leases committed (no native projection claim)");
    sealed.stop();
    config.offline = false;
    Coordinator host;
    check(host.start(config, error), "online transfer coordinator starts");
    CheckpointArtifact selected;
    check(validate_checkpoint_artifact(host.checkpoint_artifact(), selected, error), "selected artifact revalidated");
    TransferPreparation request;
    request.id = 1;
    request.player = 1;
    request.expected_revision = 3;
    request.checkpoint_revision = 2;
    request.avatar_generation = 1;
    request.source_worker = 11;
    request.source_worker_generation = 5;
    request.destination_worker = 12;
    request.destination_worker_generation = 9;
    request.source = source;
    request.destination = destination;
    request.universe = host.universe();
    request.checkpoint_hash = selected.digest;
    check(!host.prepare_transfer(request, error) && error == "transfer_preparation_authority_not_ready" &&
              host.durable_revision() == 3,
          "unconnected native authorities cannot prepare a new transfer");
    Peers peers(host);
    peers.start();
    check(!host.prepare_transfer(request, error) && error == "transfer_preparation_authority_not_ready",
          "authentication without completed authority baselines cannot prepare");
    peers.baselines();
    peers.motion_and_action(2);
    const auto prepare = request_text(request, "prepare", 3);
    for (const auto& op : {"fence", "restore", "commit", "cleanup", "abort"}) {
        const auto receipt = control(host, directory, request_text(request, op, 3));
        check(receipt.find("transfer_native_phase_unavailable") != std::string::npos && host.durable_revision() == 3,
              "unimplemented native phases cannot mutate the online ledger");
    }
    const std::vector<std::string> malformed{prepare + "unknown=1\n", prepare + "id=1\n", std::string(2049, 'x'),
                                             "schema=1\nop=prepare\n", std::string("schema=1\nop=prepare\n") + prepare};
    for (const auto& text : malformed) {
        const auto receipt = control(host, directory, text);
        check(receipt.find("\"ok\":false") != std::string::npos && host.durable_revision() == 3,
              "malformed requests cannot publish");
    }
    const std::pair<std::string, std::string> altered_fields[]{{"\nid=1\n", "\nid=18446744073709551616\n"},
                                                               {"\nplayer=1\n", "\nplayer=-1\n"},
                                                               {"source=1,10,100,1000\n", "source=1,10,100,1000,\n"},
                                                               {"schema=1\n", "schema=3\n"}};
    for (const auto& field : altered_fields) {
        auto text = prepare;
        const auto at = text.find(field.first);
        check(at != std::string::npos, "malformed field fixture matches its request");
        text.replace(at, field.first.size(), field.second);
        check(control(host, directory, text).find("\"ok\":false") != std::string::npos && host.durable_revision() == 3,
              "numeric overflow, signed values, extra location components and unsupported schema refuse publication");
    }
    write(directory / L"retained-request.txt", prepare);
    check(CreateHardLinkW((directory / L"transfer.request").c_str(), (directory / L"retained-request.txt").c_str(),
                          nullptr) != FALSE,
          "hard-linked control input challenge created");
    check(process_transfer_request(host, directory, error) && error == "transfer_request_not_regular_or_bounded" &&
              host.durable_revision() == 3 && std::filesystem::exists(directory / L"retained-request.txt"),
          "control refuses multiply linked input and preserves the retained file");
    auto bad = request;
    bad.checkpoint_hash[0] ^= 1;
    check(!host.prepare_transfer(bad, error) && host.durable_revision() == 3, "wrong selected artifact digest refused");
    bad = request;
    ++bad.source_worker_generation;
    check(!host.prepare_transfer(bad, error), "wrong source generation refused");
    bad = request;
    ++bad.destination_worker;
    check(!host.prepare_transfer(bad, error), "wrong destination worker refused");
    bad = request;
    ++bad.avatar_generation;
    check(!host.prepare_transfer(bad, error), "wrong avatar incarnation refused");
    const auto receipt = control(host, directory, prepare);
    check(receipt.find("\"ok\":true") != std::string::npos && host.durable_revision() == 4 &&
              host.transfer_preparation(1) && host.locations().transfer(1)->phase == TransferPhase::prepared,
          "local prepare returns success only after durable reservation");
    check(host.locations().accepts_avatar_outcome(1, source, 1, 11, 5) &&
              host.locations().accepts_interest(1, source, 1, 11, 5) &&
              !host.locations().accepts_avatar_outcome(1, destination, 1, 12, 9),
          "prepare preserves source ownership and interest");
    peers.motion_and_action(3);
    check(host.prepare_transfer(request, error) && host.durable_revision() == 4,
          "exact preparation retry is idempotent");
    bad = request;
    bad.destination_worker_generation = 10;
    check(!host.prepare_transfer(bad, error) && host.durable_revision() == 4, "altered preparation retry refused");
    check(!host.commit_locations(
              4, [](LocationLedger& ledger, std::string& why) { return ledger.fence_source(1, 1, why); }, error),
          "generic offline policy callback cannot bypass the live native phase boundary");
    TransferPreparation second = request;
    second.id = 2;
    second.player = 2;
    second.expected_revision = 4;
    second.source = unrelated;
    second.source_worker = 13;
    second.source_worker_generation = 4;
    check(host.prepare_transfer(second, error) && host.durable_revision() == 5,
          "second independent player can reserve a bounded transfer");
    check(!host.begin_native_transfer(1, 5, 1, 2, error) && error == "native_transfer_overlapping_preparation" &&
              host.durable_revision() == 5,
          "overlapping location reservations cannot enter native maintenance and orphan one another");
    check(host.prepare_transfer(request, error) && host.durable_revision() == 5,
          "first exact retry survives another durable publication");
    peers.stop();
    host.stop();
    std::vector<uint8_t> prepared_record;
    {
        DurableStore store;
        check(store.open(config.state_directory, prepared_record, error), "prepared record inspected");
    }
    const auto preparations_at = preparation_offset(prepared_record);
    check(prepared_record.size() == preparations_at + 4 + 2 * 200 + 4 + 32,
          "two durable identities and empty native journal/approval are bounded");
    config.offline = true;
    Coordinator offline;
    check(offline.start(config, error), "offline inspection reads prepared identities");
    check(!offline.commit_locations(
              5, [](LocationLedger& ledger, std::string& why) { return ledger.fence_source(1, 1, why); }, error) &&
              error == "location_step_has_online_preparations",
          "offline callback cannot advance a reserved live transfer without native evidence");
    offline.stop();
    config.offline = false;
    Coordinator resumed;
    check(resumed.start(config, error) && resumed.prepare_transfer(request, error) &&
              resumed.prepare_transfer(second, error) && resumed.durable_revision() == 5,
          "both exact preparation identities survive reopen without connected authorities");
    bad = request;
    bad.source_worker = 99;
    check(!resumed.prepare_transfer(bad, error), "recovered reservation rejects a changed previous worker");
    check(!resumed.cancel_transfer(bad, 5, error) && resumed.durable_revision() == 5,
          "altered cancellation cannot release another identity");
    check(control(resumed, directory, request_text(request, "cancel", 5)).find("\"ok\":true") != std::string::npos &&
              resumed.durable_revision() == 6 && !resumed.locations().transfer(1) && resumed.transfer_preparation(2),
          "exact cancellation erases only its unfenced reservation");
    check(resumed.cancel_transfer(request, 5, error) && resumed.durable_revision() == 6,
          "uncertain cancellation acknowledgement retries exactly");
    check(resumed.cancel_transfer(second, 6, error) && resumed.durable_revision() == 7,
          "second reservation cancels independently");
    check(!resumed.cancel_transfer(request, 5, error) && !resumed.cancel_transfer(request, 6, error),
          "later publication retires older cancellation receipt");
    resumed.stop();
    std::vector<uint8_t> cancelled_record;
    {
        DurableStore store;
        check(store.open(config.state_directory, cancelled_record, error), "cancellation receipt record inspected");
    }
    const auto cancelled_at = preparation_offset(cancelled_record);
    check(cancelled_record.size() == cancelled_at + 4 + 4 + 208 + 32,
          "one cancellation receipt and empty native journal/approval have bounded storage");
    Coordinator retry;
    check(retry.start(config, error) && retry.cancel_transfer(second, 6, error) && retry.durable_revision() == 7,
          "latest exact cancellation retry survives durable reopen");
    bad = second;
    --bad.source_worker_generation;
    check(!retry.cancel_transfer(bad, 6, error), "recovered cancellation rejects altered original lease");
    check(!retry.prepare_transfer(request, error), "cancelled transfer identifier cannot be reused");
    // Receipt output failure retains the claimed request. The durable operation
    // is replayed without allocating a second transfer when delivery resumes.
    Peers active(retry);
    active.start();
    active.baselines();
    active.motion_and_action(2);
    auto third = request;
    third.id = 3;
    third.expected_revision = 7;
    write(directory / L"transfer-receipt.tmp", "held by test");
    write(directory / L"transfer.request", request_text(third, "prepare", 7));
    check(process_transfer_request(retry, directory, error) && error == "transfer_receipt_write_failed" &&
              retry.durable_revision() == 8 && std::filesystem::exists(directory / L"transfer.claimed"),
          "failed receipt publication retains exact durable claim for retry");
    check(std::filesystem::remove(directory / L"transfer-receipt.tmp"), "test releases receipt output obstruction");
    check(process_transfer_request(retry, directory, error) && error.empty() && retry.durable_revision() == 8 &&
              !std::filesystem::exists(directory / L"transfer.claimed"),
          "uncertain-ack claim replay succeeds without duplicate mutation");
    active.motion_and_action(3);
    active.peers[1].stop();
    bool replaced = false;
    active.until(
        [&] {
            replaced = retry.replace_disconnected_location(destination, 12, 9, 12, 10, 8, error);
            return replaced;
        },
        "destination worker replacement durably cancels affected unfenced reservations");
    check(retry.durable_revision() == 9 && !retry.transfer_preparation(3) && !retry.locations().transfer(3) &&
              retry.locations().accepts_avatar_outcome(1, source, 1, 11, 5),
          "destination replacement does not revoke source ownership");
    check(!retry.prepare_transfer(third, error) && !retry.cancel_transfer(third, 8, error),
          "replacement cannot resurrect or falsely acknowledge old reservations");
    active.motion_and_action(4);
    active.stop();
    retry.stop();
    Coordinator replaced_head;
    check(replaced_head.start(config, error) && replaced_head.durable_revision() == 9 &&
              !replaced_head.transfer_preparation(3),
          "implicit cancellation and new destination lease recover together");
    replaced_head.stop();
    // Semantically corrupt records are freshly checksummed by DurableStore;
    // coordinator recovery must still refuse them before creating a listener.
    for (unsigned challenge = 0; challenge < 7; ++challenge) {
        {
            DurableStore store;
            std::vector<uint8_t> previous;
            check(store.open(config.state_directory, previous, error), "semantic corruption fixture opens");
            auto bytes = prepared_record;
            if (challenge == 0) {
                bytes[preparations_at] = 3;
            }
            if (challenge == 1) {
                set_wide(bytes, preparations_at + 4 + 200, 1);
            }
            if (challenge == 2) {
                set_wide(bytes, preparations_at + 4 + 5 * 8, 99);
            }
            if (challenge == 3) {
                bytes[preparations_at + 4 + 17 * 8 + 32] ^= 1;
            }
            if (challenge == 4) {
                set_wide(bytes, preparations_at + 4 + 2 * 8, store.revision() + 1);
            }
            if (challenge == 5) {
                bytes[preparations_at + 4 + 2 * 200] = 2;
            }
            if (challenge == 6) {
                bytes.pop_back();
            }
            check(store.commit(bytes, error), "checksummed malformed preparation fixture published");
        }
        Coordinator invalid;
        check(!invalid.start(config, error) &&
                  (error == "transfer_preparation_record_invalid" || error == "universe_state_invalid"),
              "malformed identity, count, duplicates, revision, flags or truncation refuse startup");
    }
    for (unsigned challenge = 0; challenge < 5; ++challenge) {
        {
            DurableStore store;
            std::vector<uint8_t> previous;
            check(store.open(config.state_directory, previous, error), "cancellation corruption fixture opens");
            auto bytes = cancelled_record;
            set_wide(bytes, cancelled_at + 8 + 200, store.revision());
            if (challenge == 0) {
                bytes[cancelled_at + 4] = 2;
            }
            if (challenge == 1) {
                set_wide(bytes, cancelled_at + 8 + 6 * 8, 99);
            }
            if (challenge == 2) {
                set_wide(bytes, cancelled_at + 8 + 2 * 8, store.revision());
            }
            if (challenge == 3) {
                set_wide(bytes, cancelled_at + 8 + 200, store.revision() + 1);
            }
            if (challenge == 4) {
                bytes.pop_back();
            }
            check(store.commit(bytes, error), "checksummed malformed cancellation fixture published");
        }
        Coordinator invalid;
        check(!invalid.start(config, error) &&
                  (error == "transfer_preparation_record_invalid" || error == "universe_state_invalid"),
              "malformed cancellation flag, original lease, causal revision or truncation refuse startup");
    }
}

#include "native_transfer_flow.inc"
} // namespace

#include "preload_flow.inc"

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--worker-fixture") {
        Sleep(90000);
        return 0;
    }
    try {
        preload_flow();
        live_control();
        for (unsigned recovery = 0; recovery < 13; ++recovery) {
            native_flow(recovery);
        }
        std::cout << "PASS " << checks << " HOST live transfer/control checks; original-game transfer NOT RUN.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
