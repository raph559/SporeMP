#include "peer.h"
#include "channel.h"
#include <filesystem>
#include <fstream>
#include <map>

namespace sporemp::network {
namespace {
// The fields of a configuration file, by name.
using ConfigFields = std::map<std::string, std::string>;

// The request a preload connection opens with and renews: the authenticated scope comes from the configuration, the
// nonce and the process identity from the caller.
Packet make_preload_request(const PeerConfig& config, uint64_t nonce, uint32_t process_id, uint64_t process_created) {
    Packet packet;
    packet.kind = Kind::preload_request;
    packet.role = config.role;
    packet.identity = config.identity;
    packet.credential = config.credential;
    auto& request = packet.preload;
    request.nonce = nonce;
    request.location = config.location;
    request.worker_generation = config.worker_generation;
    request.native_load_slot = config.native_load_slot;
    request.maintenance_transfer = config.maintenance_transfer;
    request.process_id = process_id;
    request.process_created = process_created;
    return packet;
}

// The schema a configuration is written as: the first of these features it uses.
int config_schema(const PeerConfig& config) {
    return config.experimental_foreign_species ? 7
           : config.resident_owners            ? 6
           : config.maintenance_transfer       ? 5
           : config.location.valid()           ? (config.primary_owner ? 4 : 3)
                                               : 2;
}

// One `name=value` per line. A line without a name, or a name given twice, refuses the file.
bool read_config_lines(std::ifstream& input, ConfigFields& values, std::string& error) {
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0 ||
            !values.emplace(line.substr(0, separator), line.substr(separator + 1)).second) {
            error = "invalid_or_duplicate_config_field";
            return false;
        }
    }
    return true;
}

// Each schema has an exact number of fields. Schema 7 has two: without a location and with one.
bool known_schema_with_its_field_count(const ConfigFields& values) {
    if (!values.count("schema")) {
        return false;
    }
    const std::string& schema = values.at("schema");
    const size_t count = values.size();
    if ((schema == "2" && count != 10 + world_file_count) || (schema == "3" && count != 16 + world_file_count) ||
        (schema == "4" && count != 18 + world_file_count) || (schema == "5" && count != 19 + world_file_count) ||
        (schema == "6" && count != 20 + world_file_count) ||
        (schema == "7" && count != 11 + world_file_count && count != 17 + world_file_count)) {
        return false;
    }
    return !(schema != "2" && schema != "3" && schema != "4" && schema != "5" && schema != "6" && schema != "7");
}

bool has_required_fields(const ConfigFields& values) {
    const char* required[]{"schema",         "host",          "port",         "certificate_sha256",
                           "credential",     "role",          "build_sha256", "executable_sha256",
                           "content_sha256", "fixture_sha256"};
    for (auto key : required) {
        if (!values.count(key)) {
            return false;
        }
    }
    return true;
}

bool read_host(const ConfigFields& values, PeerConfig& config, std::string& error) {
    config.host = values.at("host");
    if (config.host.empty() || config.host.size() > 253 ||
        config.host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:") !=
            std::string::npos) {
        error = "invalid_host";
        return false;
    }
    return true;
}

bool read_port(const ConfigFields& values, PeerConfig& config, std::string& error) {
    const auto& port = values.at("port");
    if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos) {
        error = "invalid_port";
        return false;
    }
    const unsigned number = static_cast<unsigned>(std::stoul(port));
    if (!number || number > 65535) {
        error = "invalid_port";
        return false;
    }
    config.port = static_cast<uint16_t>(number);
    return true;
}

// The certificate pin, the credential and the build, executable, content, fixture and world digests: every one
// present and not zero, and the fixture the first world file.
bool read_identity(const ConfigFields& values, PeerConfig& config, std::string& error) {
    if (!parse_hex(values.at("certificate_sha256"), config.certificate_pin) ||
        !parse_hex(values.at("credential"), config.credential) ||
        !parse_hex(values.at("build_sha256"), config.identity.build) ||
        !parse_hex(values.at("executable_sha256"), config.identity.executable) ||
        !parse_hex(values.at("content_sha256"), config.identity.content) ||
        !parse_hex(values.at("fixture_sha256"), config.identity.fixture)) {
        error = "invalid_digest_or_credential";
        return false;
    }
    const Digest zero{};
    if (config.certificate_pin == zero || config.credential == zero || config.identity.build == zero ||
        config.identity.executable == zero || config.identity.content == zero || config.identity.fixture == zero) {
        error = "empty_identity_or_credential";
        return false;
    }
    for (size_t index = 0; index < world_file_count; ++index) {
        const auto field = world_files[index].field;
        if (!values.count(field) || !parse_hex(values.at(field), config.identity.world[index]) ||
            config.identity.world[index] == Digest{}) {
            error = std::string("missing_or_invalid_world_identity:") + world_files[index].path;
            return false;
        }
    }
    if (config.identity.fixture != config.identity.world[0]) {
        error = "world_fixture_identity_disagrees";
        return false;
    }
    return true;
}

// A field that is an unsigned decimal of at most twenty digits and fits 64 bits.
bool read_decimal(const ConfigFields& values, const char* name, uint64_t& number) {
    const auto found = values.find(name);
    if (found == values.end()) {
        return false;
    }
    const auto& text = found->second;
    if (text.empty() || text.size() > 20 || text.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    try {
        number = std::stoull(text);
    } catch (...) {
        return false;
    }
    return true;
}

bool read_location_and_worker(const ConfigFields& values, PeerConfig& config, std::string& error) {
    const std::pair<const char*, uint64_t*> numbers[]{{"location_galaxy", &config.location.galaxy},
                                                      {"location_star", &config.location.star},
                                                      {"location_planet", &config.location.planet},
                                                      {"location_area", &config.location.area},
                                                      {"worker_generation", &config.worker_generation}};
    for (const auto& number : numbers) {
        if (!read_decimal(values, number.first, *number.second)) {
            error = "location_config_fields";
            return false;
        }
    }
    const auto slot = values.find("native_load_slot");
    if (slot == values.end() || slot->second.empty() || slot->second.size() > 1 ||
        slot->second.find_first_not_of("01") != std::string::npos) {
        error = "location_config_fields";
        return false;
    }
    config.native_load_slot = static_cast<uint32_t>(slot->second[0] - '0');
    return true;
}

// The primary owner is the one resident until a schema 6 file names the residents itself.
bool read_primary_owner(const ConfigFields& values, PeerConfig& config, std::string& error) {
    const auto owner = values.find("primary_owner");
    if (owner == values.end() || (owner->second != "1" && owner->second != "2")) {
        error = "location_primary_owner_invalid";
        return false;
    }
    config.primary_owner = static_cast<uint32_t>(owner->second[0] - '0');
    config.resident_owners = 1u << config.primary_owner;
    const auto secondary = values.find("world_haagos_sha256");
    if (secondary == values.end() || !parse_hex(secondary->second, config.secondary_save) ||
        config.secondary_save == Digest{}) {
        error = "location_secondary_save_invalid";
        return false;
    }
    return true;
}

// A transfer is for an authority only, and a schema 5 file has to name one.
bool read_maintenance_transfer(const ConfigFields& values, const std::string& schema, PeerConfig& config,
                               std::string& error) {
    if (!read_decimal(values, "maintenance_transfer", config.maintenance_transfer)) {
        error = "maintenance_config_invalid";
        return false;
    }
    if ((config.maintenance_transfer && config.role != Role::authority) ||
        (schema == "5" && !config.maintenance_transfer)) {
        error = "maintenance_config_invalid";
        return false;
    }
    return true;
}

bool read_resident_owners(const ConfigFields& values, PeerConfig& config, std::string& error) {
    const auto owners = values.find("resident_owners");
    if (owners == values.end() || (owners->second != "2" && owners->second != "4" && owners->second != "6")) {
        error = "resident_owners_invalid";
        return false;
    }
    config.resident_owners = static_cast<uint32_t>(owners->second[0] - '0');
    return true;
}

// The fields a located configuration adds, each schema adding to the one before it. `config.role` is already read.
bool read_location(const ConfigFields& values, const std::string& schema, PeerConfig& config, std::string& error) {
    if (!read_location_and_worker(values, config, error)) {
        return false;
    }
    if ((schema == "4" || schema == "5" || schema == "6") && !read_primary_owner(values, config, error)) {
        return false;
    }
    if (!config.location.valid() ||
        (config.role == Role::authority ? !config.worker_generation : config.worker_generation != 0)) {
        error = "location_worker_generation_invalid";
        return false;
    }
    if ((schema == "5" || schema == "6") && !read_maintenance_transfer(values, schema, config, error)) {
        return false;
    }
    if (schema == "6" && !read_resident_owners(values, config, error)) {
        return false;
    }
    return true;
}
} // namespace

bool valid_foreign_bootstrap(const PeerConfig& config) noexcept {
    if (!config.experimental_foreign_species) {
        return true;
    }
    return config.role == Role::authority && !config.primary_owner && !config.resident_owners &&
           !config.maintenance_transfer && !config.maintenance_scene && config.secondary_save == Digest{} &&
           config.native_load_slot == 0 &&
           (config.location.valid() ? config.worker_generation != 0
                                    : config.location == LocationId{} && !config.worker_generation);
}

struct Peer::Impl {
    std::unique_ptr<detail::Channel> channel;
    Packet preload_request;
};

Peer::Peer() : impl_(std::make_unique<Impl>()) {}

Peer::~Peer() {
    stop();
}

bool Peer::start(const PeerConfig& config, std::string& error) {
    if (!valid_foreign_bootstrap(config)) {
        error = "foreign_bootstrap_invalid";
        return false;
    }
    if (config.maintenance_transfer ? (config.role != Role::authority || !config.maintenance_scene)
                                    : config.maintenance_scene != 0) {
        error = "maintenance_peer_identity_invalid";
        return false;
    }
    if (impl_->channel) {
        error = "peer_already_started";
        return false;
    }
    if (config.host.empty() || config.host.size() > 253 || !config.port) {
        error = "invalid_endpoint";
        return false;
    }
    if (!valid_world_identity(config.identity.world)) {
        error = "missing_world_identity";
        return false;
    }
    if (config.native_load_slot > 1 || config.primary_owner > 2 ||
        (config.primary_owner && (!config.location.valid() || config.secondary_save == Digest{})) ||
        (config.location.valid() && config.role == Role::authority && !config.worker_generation) ||
        (config.resident_owners & ~6u) ||
        (config.resident_owners && (!config.location.valid() || !config.primary_owner)) ||
        (config.location.valid() && config.role == Role::player && config.worker_generation) ||
        (!config.location.valid() && config.worker_generation)) {
        error = "location_worker_generation_invalid";
        return false;
    }
    if (!detail::initialize_sockets(error)) {
        return false;
    }
    impl_->channel = std::make_unique<detail::Channel>();
    impl_->channel->start_client(config);
    return true;
}

bool Peer::try_restart(const PeerConfig& config, std::string& error) {
    if (impl_->channel) {
        if (!impl_->channel->exited()) {
            error = "network_thread_still_exiting";
            return false;
        }
        impl_->channel.reset();
    }
    return start(config, error);
}

bool Peer::start_preload(const PeerConfig& config, uint64_t nonce, uint32_t process_id, uint64_t process_created,
                         std::string& error) {
    if (!valid_foreign_bootstrap(config)) {
        error = "foreign_bootstrap_invalid";
        return false;
    }
    if (impl_->channel) {
        error = "peer_already_started";
        return false;
    }
    if (config.host.empty() || config.host.size() > 253 || !config.port || !config.location.valid() ||
        config.maintenance_scene) {
        error = "preload_endpoint_or_scene_invalid";
        return false;
    }
    const Packet packet = make_preload_request(config, nonce, process_id, process_created);
    const auto wire = encode(packet);
    Packet checked;
    if (!decode(wire.data(), wire.size(), checked, error)) {
        return false;
    }
    if (!detail::initialize_sockets(error)) {
        return false;
    }
    impl_->preload_request = packet;
    impl_->channel = std::make_unique<detail::Channel>();
    impl_->channel->start_client(config, &packet);
    error.clear();
    return true;
}

bool Peer::renew_preload(uint64_t nonce) {
    if (!impl_->channel || impl_->preload_request.kind != Kind::preload_request || !nonce ||
        nonce <= impl_->preload_request.preload.nonce) {
        return false;
    }
    impl_->preload_request.preload.nonce = nonce;
    return impl_->channel->send(impl_->preload_request);
}

bool Peer::send(Packet packet) {
    return impl_->channel && impl_->channel->send(packet);
}

bool Peer::poll(Event& event) {
    return impl_->channel && impl_->channel->poll(event);
}

bool Peer::transport_active() const noexcept {
    return impl_->channel && impl_->channel->active();
}

void Peer::request_stop() noexcept {
    if (impl_->channel) {
        impl_->channel->request_stop();
    }
}

void Peer::stop() {
    if (impl_->channel) {
        impl_->channel->stop();
        impl_->channel.reset();
    }
    impl_->preload_request = {};
}

bool write_peer_config(const std::wstring& path, const PeerConfig& config) {
    if (!valid_foreign_bootstrap(config)) {
        return false;
    }
    if (config.maintenance_transfer &&
        (!config.location.valid() || !config.primary_owner || config.role != Role::authority)) {
        return false;
    }
    if ((config.resident_owners & ~6u) ||
        (config.resident_owners && (!config.location.valid() || !config.primary_owner))) {
        return false;
    }
    std::ofstream output(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    output << "schema=" << config_schema(config) << "\nhost=" << config.host << "\nport=" << config.port
           << "\ncertificate_sha256=" << hex(config.certificate_pin) << "\ncredential=" << hex(config.credential)
           << "\nrole=" << (config.role == Role::authority ? "authority" : "player")
           << "\nbuild_sha256=" << hex(config.identity.build)
           << "\nexecutable_sha256=" << hex(config.identity.executable)
           << "\ncontent_sha256=" << hex(config.identity.content) << "\nfixture_sha256=" << hex(config.identity.fixture)
           << '\n';
    for (size_t index = 0; index < world_file_count; ++index) {
        output << world_files[index].field << '=' << hex(config.identity.world[index]) << '\n';
    }
    if (config.location.valid()) {
        output << "location_galaxy=" << config.location.galaxy << "\nlocation_star=" << config.location.star
               << "\nlocation_planet=" << config.location.planet << "\nlocation_area=" << config.location.area
               << "\nworker_generation=" << config.worker_generation << "\nnative_load_slot=" << config.native_load_slot
               << '\n';
        if (config.primary_owner) {
            output << "primary_owner=" << config.primary_owner << "\nworld_haagos_sha256=" << hex(config.secondary_save)
                   << '\n';
        }
        if (config.maintenance_transfer || config.resident_owners) {
            output << "maintenance_transfer=" << config.maintenance_transfer << '\n';
        }
        if (config.resident_owners) {
            output << "resident_owners=" << config.resident_owners << '\n';
        }
    }
    if (config.experimental_foreign_species) {
        output << "experimental_foreign_species=1\n";
    }
    output.close();
    return bool(output);
}

bool read_peer_config(const std::wstring& path, PeerConfig& result, std::string& error) {
    std::error_code size_error;
    const auto size = std::filesystem::file_size(path, size_error);
    if (size_error || size > 4096) {
        error = "config_missing_or_oversized";
        return false;
    }
    std::ifstream input{std::filesystem::path(path)};
    ConfigFields values;
    if (!read_config_lines(input, values, error)) {
        return false;
    }
    if (!known_schema_with_its_field_count(values)) {
        error = "unexpected_config_fields";
        return false;
    }
    if (!has_required_fields(values)) {
        error = "missing_config_field";
        return false;
    }
    const std::string& schema = values.at("schema");
    PeerConfig config;
    if (schema == "7") {
        const auto mode = values.find("experimental_foreign_species");
        if (mode == values.end() || mode->second != "1") {
            error = "foreign_bootstrap_invalid";
            return false;
        }
        config.experimental_foreign_species = true;
    }
    const std::string& role = values.at("role");
    if (role != "player" && role != "authority") {
        error = "unsupported_config_schema_or_role";
        return false;
    }
    if (!read_host(values, config, error) || !read_port(values, config, error)) {
        return false;
    }
    config.role = role == "authority" ? Role::authority : Role::player;
    if (!read_identity(values, config, error)) {
        return false;
    }
    const bool located = schema == "3" || schema == "4" || schema == "5" || schema == "6" ||
                         (config.experimental_foreign_species && values.size() == 17 + world_file_count);
    if (located && !read_location(values, schema, config, error)) {
        return false;
    }
    if (!valid_foreign_bootstrap(config)) {
        error = "foreign_bootstrap_invalid";
        return false;
    }
    result = config;
    error.clear();
    return true;
}
} // namespace sporemp::network
