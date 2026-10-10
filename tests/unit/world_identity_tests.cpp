#include "protocol.h"
#include "peer.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace sporemp::network;

namespace {
int assertions = 0;

void check(bool pass, const char* message) {
    ++assertions;
    if (!pass) {
        throw std::runtime_error(message);
    }
}

struct Temporary {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (L"sporemp-world-host-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));

    Temporary() {
        if (!std::filesystem::create_directory(path)) {
            throw std::runtime_error("fresh world test directory required");
        }
    }

    ~Temporary() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void write(const std::filesystem::path& path, const std::string& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << value;
}
} // namespace

int world_identity_tests() {
    Temporary temporary;
    WorldIdentity world{}, before{};
    std::string error;
    check(!read_world_identity(temporary.path.wstring(), world, error) &&
              error == "canonical_world_missing_or_unreadable:Games/Game0/Satiria.spo",
          "missing exact first file denies world admission");
    for (const auto& entry : world_files) {
        write(temporary.path / entry.path, "abc");
    }
    check(read_world_identity(temporary.path.wstring(), world, error) && valid_world_identity(world),
          "six closed original-path fixture files inspected");
    for (const auto& value : world) {
        check(hex(value) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "actual sha256 known vector for each file");
    }
    before = world;
    for (size_t index = 0; index < world_file_count; ++index) {
        auto path = temporary.path / world_files[index].path;
        write(path, "abd");
        check(read_world_identity(temporary.path.wstring(), world, error) && world[index] != before[index],
              "same name and size changed bytes change world identity");
        for (size_t other = 0; other < world_file_count; ++other) {
            if (other != index) {
                check(world[other] == before[other], "unmodified file identity retained");
            }
        }
        std::filesystem::remove(path);
        check(!read_world_identity(temporary.path.wstring(), world, error) &&
                  error == std::string("canonical_world_missing_or_unreadable:") + world_files[index].path,
              "each missing file precisely identified");
        write(path, "abc");
    }
    auto path = temporary.path / world_files[1].path;
    HANDLE writer = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                0, nullptr);
    check(writer != INVALID_HANDLE_VALUE, "writer test handle opened");
    const bool accepted = read_world_identity(temporary.path.wstring(), world, error);
    CloseHandle(writer);
    check(!accepted && error == "canonical_world_not_closed_or_readable:Games/Game0/planetRecords.pkp",
          "open writer denies quiescent world hash");
    std::filesystem::resize_file(path, 128ull * 1024 * 1024 + 1);
    check(!read_world_identity(temporary.path.wstring(), world, error) &&
              error == "canonical_world_size_limit:Games/Game0/planetRecords.pkp",
          "oversized world fails before hashing");
    write(path, "abc");
    std::filesystem::remove(path);
    std::filesystem::create_directory(path);
    check(!read_world_identity(temporary.path.wstring(), world, error) &&
              error == "canonical_world_unsafe_path:Games/Game0/planetRecords.pkp",
          "directory cannot stand in for world file");
    std::filesystem::remove(path);
    write(path, "abc");
    Packet rejection;
    rejection.kind = Kind::reject;
    rejection.error = Error::world_mismatch;
    for (size_t index = 0; index < world_file_count; ++index) {
        rejection.world_index = static_cast<uint32_t>(index);
        const auto wire = encode(rejection);
        Packet decoded;
        check(decode(wire.data(), wire.size(), decoded, error) &&
                  error_detail(decoded) == std::string("canonical_world_mismatch:") + world_files[index].path,
              "wire rejection preserves allowlisted diagnostic without arbitrary path strings");
    }
    rejection.world_index = static_cast<uint32_t>(world_file_count);
    auto wire = encode(rejection);
    Packet decoded;
    check(!decode(wire.data(), wire.size(), decoded, error) && error == "invalid_world_mismatch_index",
          "out of range world path index refused");
    rejection.world_index = 1;
    rejection.error = Error::ownership;
    wire = encode(rejection);
    check(!decode(wire.data(), wire.size(), decoded, error), "world path index cannot accompany unrelated error");
    Packet scoped;
    scoped.kind = Kind::ping;
    scoped.location = {1, 2, 3, 4};
    scoped.worker_generation = 5;
    scoped.native_load_slot = 1;
    wire = encode(scoped);
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.location == scoped.location &&
              decoded.worker_generation == 5 && decoded.native_load_slot == 1,
          "wire packet preserves canonical location and native save selection");
    scoped.native_load_slot = 2;
    wire = encode(scoped);
    check(!decode(wire.data(), wire.size(), decoded, error) && error == "invalid_native_load_slot",
          "unknown original save selection rejected");
    auto peer_text = std::string("schema=3\nhost=127.0.0.1\nport=27060\ncertificate_sha256=") + std::string(64, '1') +
                     "\ncredential=" + std::string(64, '2') + "\nrole=authority\nbuild_sha256=" + std::string(64, '3') +
                     "\nexecutable_sha256=" + std::string(64, '4') + "\ncontent_sha256=" + std::string(64, '5') +
                     "\nfixture_sha256=" + hex(world[0]) + "\n";
    for (size_t index = 0; index < world_file_count; ++index) {
        peer_text += std::string(world_files[index].field) + "=" + hex(world[index]) + "\n";
    }
    peer_text += "location_galaxy=1\nlocation_star=2\nlocation_planet=3\nlocation_area=4\nworker_generation=5\nnative_"
                 "load_slot=1\n";
    const auto peer_path = temporary.path / L"peer.conf";
    write(peer_path, peer_text);
    PeerConfig peer_config;
    check(read_peer_config(peer_path.wstring(), peer_config, error) && peer_config.location == LocationId{1, 2, 3, 4} &&
              peer_config.worker_generation == 5 && peer_config.native_load_slot == 1,
          "location peer config pins worker generation and reviewed save slot");
    auto invalid_peer = peer_text;
    const auto slot_at = invalid_peer.find("native_load_slot=1");
    invalid_peer.replace(slot_at, std::string("native_load_slot=1").size(), "native_load_slot=2");
    write(peer_path, invalid_peer);
    check(!read_peer_config(peer_path.wstring(), peer_config, error) && error == "location_config_fields",
          "peer config refuses unreviewed native save slot");
    rejection = {};
    wire = encode(rejection);
    wire[640] = 1;
    check(!decode(wire.data(), wire.size(), decoded, error) && error == "invalid_maintenance_header",
          "schema9 maintenance ID cannot enter ordinary gameplay packets");
    Packet maintenance;
    maintenance.kind = Kind::hello;
    maintenance.role = Role::authority;
    maintenance.scene = 17;
    maintenance.maintenance_transfer = 0x0807060504030201ull;
    wire = encode(maintenance);
    check(wire[640] == 1 && wire[647] == 8 && decode(wire.data(), wire.size(), decoded, error) &&
              decoded.maintenance_transfer == maintenance.maintenance_transfer,
          "maintenance hello occupies exactly former reserved bytes640 through647");
    maintenance.kind = Kind::welcome;
    wire = encode(maintenance);
    check(decode(wire.data(), wire.size(), decoded, error) && decoded.scene == 17,
          "maintenance welcome retains its loaded scene");
    Packet transfer;
    transfer.kind = Kind::transfer_command;
    transfer.role = Role::authority;
    transfer.scene = 17;
    transfer.baseline = 0;
    auto& frame = transfer.transfer;
    frame.id = 9;
    frame.player = 1;
    frame.revision = 7;
    frame.avatar_generation = 2;
    frame.worker = 11;
    frame.worker_generation = 5;
    frame.preparation.fill(1);
    frame.challenge.fill(2);
    frame.unique_game_id = 88;
    frame.canonical_home = 99;
    frame.source_avatar = 101;
    frame.source_slot = 1;
    frame.native_slot = 1;
    frame.pause_request = 9;
    frame.process_created = 0x0807060504030201ull;
    wire = encode(transfer);
    check(wire.size() == 688 && wire[72] == 1 && wire[76] == 9 && wire[284] == 0 && wire[292] == 88 &&
              wire[304] == 101 && wire[308] == 1 && wire[412] == 1 && wire[419] == 8 &&
              decode(wire.data(), wire.size(), decoded, error) &&
              decoded.transfer.process_created == frame.process_created && decoded.baseline == 0,
          "transfer envelope pins header72 payload348 and supports explicit maintenance baseline0");
    for (size_t offset = 420; offset < wire.size(); ++offset) {
        auto malformed = wire;
        malformed[offset] = 1;
        check(!decode(malformed.data(), malformed.size(), decoded, error) && error == "invalid_transfer_frame",
              "each unused transfer byte rejects nonzero data");
    }
    for (unsigned challenge = 0; challenge < 4; ++challenge) {
        auto wrong = transfer;
        if (challenge == 0) {
            wrong.role = Role::player;
        }
        if (challenge == 1) {
            wrong.count = 1;
        }
        if (challenge == 2) {
            wrong.player = 1;
        }
        if (challenge == 3) {
            wrong.scene = 0;
        }
        auto malformed = encode(wrong);
        check(!decode(malformed.data(), malformed.size(), decoded, error) && error == "invalid_transfer_header",
              "transfer common header rejects player-role count player and absent-scene aliases");
    }
    auto receipt = frame;
    receipt.source_player = 111;
    check(same_transfer_request(frame, receipt) && !same_transfer_request(receipt, frame),
          "only source-fence command may bind an initially unknown native player");
    auto wrong = receipt;
    wrong.challenge[0] ^= 1;
    check(!same_transfer_request(frame, wrong), "challenge is never wildcarded");
    Digest witness{}, changed{};
    check(transfer_witness_digest(receipt, 42, 17, 1, witness), "canonical transfer witness hashes");
    receipt.state.fill(7);
    check(transfer_witness_digest(receipt, 42, 17, 1, changed) && changed == witness,
          "witness excludes its own digest without weakening scalar binding");
    ++receipt.worker_generation;
    check(transfer_witness_digest(receipt, 42, 17, 1, changed) && changed != witness,
          "worker-generation mutation changes the canonical witness digest");
    for (unsigned challenge = 0; challenge < 3; ++challenge) {
        auto wrong_frame = frame;
        if (challenge == 0) {
            wrong_frame.artifact_bytes = 1024 * 1024 + 1;
        }
        if (challenge == 1) {
            wrong_frame.approval_bytes = 20681;
        }
        if (challenge == 2) {
            wrong_frame.op = TransferOp::destination_restore;
        }
        TransferWire payload{};
        check(encode_transfer(wrong_frame, payload) && !decode_transfer(payload, receipt),
              "oversize artifact approval and unknown non-fence source identity are refused");
    }
    peer_config = {};
    peer_config.host = "127.0.0.1";
    peer_config.identity.world = world;
    peer_config.identity.fixture = world[0];
    peer_config.identity.build.fill(1);
    peer_config.identity.executable.fill(2);
    peer_config.identity.content.fill(3);
    peer_config.certificate_pin.fill(4);
    peer_config.credential.fill(5);
    peer_config.role = Role::authority;
    peer_config.location = {1, 2, 3, 4};
    peer_config.worker_generation = 8;
    peer_config.primary_owner = 1;
    peer_config.secondary_save.fill(6);
    peer_config.maintenance_transfer = 9;
    PeerConfig reread;
    check(write_peer_config(peer_path.wstring(), peer_config) && read_peer_config(peer_path.wstring(), reread, error) &&
              reread.maintenance_transfer == 9 && reread.worker_generation == 8 && reread.primary_owner == 1 &&
              reread.maintenance_scene == 0,
          "schema5 private config roundtrip binds maintenance transfer and defers scene until native load");
    check(reread.resident_owners == 2, "older one-primary config does not grant a paired resident admission");
    peer_config.resident_owners = 6;
    check(write_peer_config(peer_path.wstring(), peer_config) && read_peer_config(peer_path.wstring(), reread, error) &&
              reread.resident_owners == 6 && reread.maintenance_transfer == 9,
          "schema6 explicit paired residents remain provisional during maintenance startup");
    peer_config.role = Role::player;
    peer_config.worker_generation = 0;
    peer_config.maintenance_transfer = 0;
    peer_config.native_load_slot = 1;
    check(write_peer_config(peer_path.wstring(), peer_config) && read_peer_config(peer_path.wstring(), reread, error) &&
              reread.primary_owner == 1 && reread.native_load_slot == 1 && reread.resident_owners == 6 &&
              !reread.maintenance_transfer,
          "visiting player controls canonical owner1 while loading location2 with both intended residents");
    for (const auto bad : {"0", "1", "3", "7", "256", "-1", "6x"}) {
        std::ifstream input(peer_path);
        std::string valid{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        input.close();
        const auto at = valid.find("resident_owners=6");
        check(at != std::string::npos, "resident mask fixture found");
        auto malformed = valid;
        malformed.replace(at, std::string("resident_owners=6").size(), std::string("resident_owners=") + bad);
        write(peer_path, malformed);
        check(!read_peer_config(peer_path.wstring(), reread, error) && error == "resident_owners_invalid",
              "unknown empty or malformed native resident masks refused");
        write(peer_path, valid);
    }
    PeerConfig foreign;
    foreign.identity.world = before;
    foreign.identity.fixture = before[0];
    foreign.identity.build.fill(1);
    foreign.identity.executable.fill(2);
    foreign.identity.content.fill(3);
    foreign.certificate_pin.fill(4);
    foreign.credential.fill(5);
    foreign.role = Role::authority;
    check(!foreign.experimental_foreign_species, "existing peer configuration defaults preserve ordinary setup");
    auto read_text = [&]() {
        std::ifstream input(peer_path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    auto replace_field = [](std::string text, const std::string& from, const std::string& to) {
        const auto at = text.find(from);
        check(at != std::string::npos, "foreign config fixture field exists");
        text.replace(at, from.size(), to);
        return text;
    };
    for (bool located : {false, true}) {
        foreign.location = located ? LocationId{1, 2, 3, 4} : LocationId{};
        foreign.worker_generation = located ? 8 : 0;
        foreign.experimental_foreign_species = false;
        check(write_peer_config(peer_path.wstring(), foreign) && read_peer_config(peer_path.wstring(), reread, error) &&
                  !reread.experimental_foreign_species && read_text().find(located ? "schema=3\n" : "schema=2\n") == 0,
              "ordinary schema2 and schema3 remain unchanged and do not enable foreign species");
        foreign.experimental_foreign_species = true;
        check(valid_foreign_bootstrap(foreign) && write_peer_config(peer_path.wstring(), foreign) &&
                  read_peer_config(peer_path.wstring(), reread, error) && reread.experimental_foreign_species &&
                  reread.role == Role::authority && reread.location == foreign.location &&
                  reread.worker_generation == foreign.worker_generation && reread.native_load_slot == 0 &&
                  !reread.primary_owner && !reread.resident_owners && !reread.maintenance_transfer &&
                  !reread.maintenance_scene && reread.secondary_save == Digest{},
              "schema7 roundtrip admits only explicit authority bootstrap with optional complete location");
        const auto valid = read_text();
        check(valid.find("schema=7\n") == 0 && valid.find("experimental_foreign_species=1\n") != std::string::npos,
              "schema7 writer records its mandatory explicit foreign-mode opt-in");
        for (const auto bad : {"0", "2", "true", "01", "-1", "1 "}) {
            write(peer_path, replace_field(valid, "experimental_foreign_species=1\n",
                                           std::string("experimental_foreign_species=") + bad + "\n"));
            check(!read_peer_config(peer_path.wstring(), reread, error),
                  "schema7 rejects noncanonical and disabled mode values");
        }
        for (const auto extra :
             {"experimental_foreign_species=1\n", "unknown=1\n", "primary_owner=1\n", "resident_owners=6\n",
              "maintenance_transfer=1\n", "maintenance_scene=1\n", "world_haagos_sha256=0\n"}) {
            write(peer_path, valid + extra);
            check(!read_peer_config(peer_path.wstring(), reread, error),
                  "schema7 rejects duplicate unknown and other-mode fields");
        }
        write(peer_path, replace_field(valid, "experimental_foreign_species=1\n", ""));
        check(!read_peer_config(peer_path.wstring(), reread, error), "schema7 cannot silently omit its opt-in");
        write(peer_path, replace_field(valid, "role=authority\n", "role=player\n"));
        check(!read_peer_config(peer_path.wstring(), reread, error), "schema7 cannot bootstrap a player peer");
        write(peer_path, replace_field(valid, "schema=7\n", located ? "schema=3\n" : "schema=2\n"));
        check(!read_peer_config(peer_path.wstring(), reread, error),
              "legacy schemas cannot hide a foreign opt-in field");
        if (located) {
            write(peer_path, replace_field(valid, "native_load_slot=0\n", "native_load_slot=1\n"));
            check(!read_peer_config(peer_path.wstring(), reread, error),
                  "foreign bootstrap is restricted to original slot0");
            write(peer_path, replace_field(valid, "worker_generation=8\n", "worker_generation=0\n"));
            check(!read_peer_config(peer_path.wstring(), reread, error),
                  "located foreign authority needs its current worker generation");
            write(peer_path, replace_field(valid, "location_area=4\n", "unknown=4\n"));
            check(!read_peer_config(peer_path.wstring(), reread, error),
                  "matching field count cannot replace a required location coordinate");
        } else {
            write(peer_path, replace_field(valid, "experimental_foreign_species=1\n", "unknown=1\n"));
            check(!read_peer_config(peer_path.wstring(), reread, error),
                  "matching field count cannot replace explicit mode admission");
        }
    }
    for (unsigned mutation = 0; mutation < 11; ++mutation) {
        auto bad = foreign;
        switch (mutation) {
        case 0:
            bad.role = Role::player;
            break;
        case 1:
            bad.primary_owner = 1;
            break;
        case 2:
            bad.resident_owners = 6;
            break;
        case 3:
            bad.maintenance_transfer = 1;
            break;
        case 4:
            bad.maintenance_scene = 1;
            break;
        case 5:
            bad.secondary_save.fill(1);
            break;
        case 6:
            bad.native_load_slot = 1;
            break;
        case 7:
            bad.location = {1, 0, 0, 0};
            break;
        case 8:
            bad.worker_generation = 0;
            break;
        case 9:
            bad.location = {};
            break;
        case 10:
            bad.role = static_cast<Role>(255);
            break;
        }
        check(!valid_foreign_bootstrap(bad), "in-memory foreign bootstrap rejects every conflicting admission mode");
        write(peer_path, "preserve-existing-config");
        check(!write_peer_config(peer_path.wstring(), bad) && read_text() == "preserve-existing-config",
              "invalid foreign writer fails before truncating a retained config");
        Peer peer;
        check(!peer.start(bad, error) && error == "foreign_bootstrap_invalid" && !peer.transport_active(),
              "invalid in-memory foreign config cannot create a gameplay transport");
        check(!peer.start_preload(bad, 1, 1, 1, error) && error == "foreign_bootstrap_invalid" &&
                  !peer.transport_active(),
              "invalid in-memory foreign config cannot create a preload transport");
    }
    return assertions;
}
