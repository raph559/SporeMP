#include "coordinator.h"
#include "transfer_control.h"
#include <windows.h>
#include <sddl.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

using namespace sporemp::network;

namespace {
// Ends the serve loop. Set by the console control handler, which Windows runs on a thread of its own, and by the
// serve loop itself; read by the serve loop.
std::atomic<bool> quitting{false};

BOOL WINAPI ctrl(DWORD code) {
    if (code == CTRL_C_EVENT || code == CTRL_BREAK_EVENT || code == CTRL_CLOSE_EVENT) {
        quitting = true;
        return TRUE;
    }
    return FALSE;
}

std::string quote(const std::string& text) {
    std::string quoted = "\"";
    for (unsigned char character : text) {
        if (character == '"' || character == '\\') {
            quoted += '\\';
            quoted += char(character);
        } else if (character >= 32 && character < 127) {
            quoted += char(character);
        } else {
            quoted += '?';
        }
    }
    return quoted + '"';
}

int fail(const std::string& error) {
    std::cout << "{\"error\":" << quote(error) << "}\n";
    return 20;
}

std::string content_log(const Packet& packet) {
    if (packet.kind != Kind::content_request && packet.kind != Kind::content_response) {
        return {};
    }
    ContentFrame frame;
    if (!decode_content(packet.content, frame)) {
        return ",\"content_valid\":false";
    }
    return ",\"content_valid\":true,\"content_op\":" + std::to_string(uint32_t(frame.op)) +
           ",\"content_request\":" + std::to_string(frame.request) +
           ",\"transaction\":" + std::to_string(frame.transaction) +
           ",\"content_error\":" + quote(content_failure_name(frame.failure)) +
           ",\"content_identity\":" + quote(hex(frame.identity)) +
           ",\"content_offset\":" + std::to_string(frame.offset) + ",\"content_total\":" + std::to_string(frame.total) +
           ",\"missing_group\":" + std::to_string(frame.missing.group) +
           ",\"missing_instance\":" + std::to_string(frame.missing.instance) +
           ",\"missing_type\":" + std::to_string(frame.missing.type) +
           ",\"world_index\":" + std::to_string(frame.world_index);
}

// Creates a new directory that only the system, the administrators and the current user may open. An existing
// directory is an error.
bool private_directory(const std::wstring& path, std::string& error) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        error = "open_token_failed";
        return false;
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<uint8_t> storage(size);
    bool identified = GetTokenInformation(token, TokenUser, storage.data(), size, &size) != FALSE;
    CloseHandle(token);
    if (!identified) {
        error = "token_identity_failed";
        return false;
    }
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid, &sid)) {
        error = "sid_conversion_failed";
        return false;
    }
    std::wstring sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        error = "private_acl_failed";
        return false;
    }
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    bool created = CreateDirectoryW(path.c_str(), &attributes) != FALSE;
    DWORD last_error = GetLastError();
    LocalFree(descriptor);
    if (!created) {
        error = "output_directory_must_be_new:" + std::to_string(last_error);
        return false;
    }
    return true;
}

bool write_peer(const std::filesystem::path& path, const PeerConfig& config) {
    return write_peer_config(path.wstring(), config);
}

bool write_invite(const std::filesystem::path& path, const PeerConfig& config) {
    std::ofstream output(path, std::ios::binary);
    output << "sporemp://join?host=" << config.host << "&port=" << config.port
           << "&cert=" << hex(config.certificate_pin) << "&token=" << hex(config.credential);
    if (config.location.valid()) {
        output << "&galaxy=" << config.location.galaxy << "&star=" << config.location.star
               << "&planet=" << config.location.planet << "&area=" << config.location.area
               << "&native_load_slot=" << config.native_load_slot;
        if (config.primary_owner) {
            output << "&primary_owner=" << config.primary_owner << "&haagos_sha256=" << hex(config.secondary_save);
        }
    }
    output << '\n';
    output.close();
    return bool(output);
}

std::wstring location_suffix(const LocationId& id) {
    return L"-g" + std::to_wstring(id.galaxy) + L"-s" + std::to_wstring(id.star) + L"-p" + std::to_wstring(id.planet) +
           L"-a" + std::to_wstring(id.area);
}

// Reads `name=value` lines into `fields`. False for a line without '=' or for a repeated name. An empty name is
// refused too, except in the server configuration, which has always read one and then fails its field checks.
bool read_fields(std::istream& input, bool empty_name_allowed, std::map<std::string, std::string>& fields) {
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto equal = line.find('=');
        if (equal == std::string::npos || (!equal && !empty_name_allowed) ||
            !fields.emplace(line.substr(0, equal), line.substr(equal + 1)).second) {
            return false;
        }
    }
    return true;
}

// The server configuration's fields, once the file has exactly the names of schema 2.
bool read_server_fields(const std::wstring& path, std::map<std::string, std::string>& fields, std::string& error) {
    std::error_code ec;
    if (std::filesystem::file_size(path, ec) > 4096 || ec) {
        error = "server_config_missing_or_oversized";
        return false;
    }
    std::ifstream input{std::filesystem::path(path)};
    if (!read_fields(input, true, fields)) {
        error = "invalid_server_config";
        return false;
    }
    const char* required[]{"schema",         "host",          "port", "build_sha256", "executable_sha256",
                           "content_sha256", "fixture_sha256"};
    if (fields.size() != 7 + world_file_count) {
        error = "server_config_fields";
        return false;
    }
    for (auto name : required) {
        if (!fields.count(name)) {
            error = "server_config_fields";
            return false;
        }
    }
    if (fields["schema"] != "2") {
        error = "server_schema";
        return false;
    }
    return true;
}

bool parse_port(const std::string& text, uint16_t& port) {
    if (text.empty() || text.size() > 5 || text.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    auto number = std::stoul(text);
    if (number > 65535) {
        return false;
    }
    port = static_cast<uint16_t>(number);
    return true;
}

// The build, executable, content and world hashes the server was configured for. None may be empty.
bool read_server_identity(std::map<std::string, std::string>& fields, Identity& identity, std::string& error) {
    if (!parse_hex(fields["build_sha256"], identity.build) ||
        !parse_hex(fields["executable_sha256"], identity.executable) ||
        !parse_hex(fields["content_sha256"], identity.content) ||
        !parse_hex(fields["fixture_sha256"], identity.fixture)) {
        error = "server_identity";
        return false;
    }
    Digest zero{};
    if (identity.build == zero || identity.executable == zero || identity.content == zero || identity.fixture == zero) {
        error = "server_empty_identity";
        return false;
    }
    for (size_t index = 0; index < world_file_count; ++index) {
        const auto field = world_files[index].field;
        if (!fields.count(field) || !parse_hex(fields[field], identity.world[index]) ||
            identity.world[index] == Digest{}) {
            error = std::string("missing_or_invalid_world_identity:") + world_files[index].path;
            return false;
        }
    }
    if (identity.fixture != identity.world[0]) {
        error = "world_fixture_identity_disagrees";
        return false;
    }
    return true;
}

bool read_server(const std::wstring& path, CoordinatorConfig& config, std::string& error) {
    std::map<std::string, std::string> fields;
    if (!read_server_fields(path, fields, error)) {
        return false;
    }
    if (!parse_port(fields["port"], config.port)) {
        error = "server_port";
        return false;
    }
    config.bind_host = fields["host"];
    return read_server_identity(fields, config.session.identity, error);
}

struct LocationPlan {
    std::vector<LocationLease> locations;
    std::array<uint32_t, 2> players{};
    std::map<GlobalSystem, uint32_t> globals;
};

bool decimal(const std::string& text, uint64_t& value) {
    if (text.empty() || text.size() > 20 || text.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    try {
        value = std::stoull(text);
        return true;
    } catch (...) {
        return false;
    }
}

bool numbers(const std::string& text, size_t count, std::vector<uint64_t>& values) {
    std::istringstream input(text);
    std::string part;
    values.clear();
    while (std::getline(input, part, ',')) {
        uint64_t number = 0;
        if (!decimal(part, number)) {
            return false;
        }
        values.push_back(number);
    }
    return values.size() == count;
}

struct ReplaceRequest {
    LocationId location{};
    uint64_t worker = 0, old_generation = 0, new_generation = 0, expected_revision = 0;
};

bool read_replace_request(const std::filesystem::path& path, ReplaceRequest& request, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 512) {
        error = "replacement_request_missing_or_oversized";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    std::map<std::string, std::string> fields;
    if (!read_fields(input, false, fields)) {
        error = "replacement_request_invalid";
        return false;
    }
    std::vector<uint64_t> location;
    if (fields.size() != 6 || fields["schema"] != "1" || !numbers(fields["location"], 4, location) ||
        !decimal(fields["worker"], request.worker) || !decimal(fields["old_generation"], request.old_generation) ||
        !decimal(fields["new_generation"], request.new_generation) ||
        !decimal(fields["expected_revision"], request.expected_revision)) {
        error = "replacement_request_fields";
        return false;
    }
    request.location = {location[0], location[1], location[2], location[3]};
    if (!request.location.valid() || !request.worker || !request.old_generation ||
        request.new_generation <= request.old_generation || !request.expected_revision) {
        error = "replacement_request_values";
        return false;
    }
    error.clear();
    return true;
}

// Writes the replaced location's new authority configuration into the directory. `configuration_ascii` receives the
// file's name for the receipt.
bool write_replacement_config(Coordinator& server, const std::filesystem::path& directory,
                              const ReplaceRequest& request, std::string& configuration_ascii, std::string& error) {
    PeerConfig config;
    if (!server.peer_config_at(Role::authority, 0, request.location, config, error)) {
        return false;
    }
    const std::wstring configuration_name = L"authority" + location_suffix(request.location) + L"-worker-" +
                                            std::to_wstring(request.new_generation) + L".conf";
    configuration_ascii = "authority-g" + std::to_string(request.location.galaxy) + "-s" +
                          std::to_string(request.location.star) + "-p" + std::to_string(request.location.planet) +
                          "-a" + std::to_string(request.location.area) + "-worker-" +
                          std::to_string(request.new_generation) + ".conf";
    const auto temporary = directory / L"replace-location-config.tmp";
    if (!write_peer(temporary, config) ||
        MoveFileExW(temporary.c_str(), (directory / configuration_name).c_str(), MOVEFILE_REPLACE_EXISTING) == FALSE) {
        error = "replacement_config_write_failed";
        return false;
    }
    return true;
}

void write_replacement_receipt(const Coordinator& server, const std::filesystem::path& directory, bool ok,
                               const ReplaceRequest& request, const std::string& configuration_ascii,
                               const std::string& error) {
    const auto receipt = directory / L"replace-location-receipt.json";
    const auto temporary = directory / L"replace-location-receipt.tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << "{\"schema\":1,\"ok\":" << (ok ? "true" : "false")
           << ",\"durable_revision\":" << server.durable_revision()
           << ",\"worker_generation\":" << (ok ? request.new_generation : 0)
           << ",\"authority_config\":" << quote(ok ? configuration_ascii : std::string{})
           << ",\"error\":" << quote(ok ? std::string{} : error) << "}\n";
    output.close();
    if (output) {
        MoveFileExW(temporary.c_str(), receipt.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
}

// Carries out a replace-location request file if the directory holds one. True when a request was claimed, whatever
// its outcome; the outcome is in the receipt file and in `error`.
bool replace_location_request(Coordinator& server, const std::filesystem::path& directory, std::string& error) {
    const auto request_path = directory / L"replace-location.request";
    const auto claimed = directory / L"replace-location.claimed";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(request_path, ec) || ec) {
        return false;
    }
    std::filesystem::rename(request_path, claimed, ec);
    if (ec) {
        error = "replacement_request_claim_failed";
        return true;
    }
    ReplaceRequest request;
    bool ok = read_replace_request(claimed, request, error);
    if (ok) {
        ok = server.replace_disconnected_location(request.location, request.worker, request.old_generation,
                                                  request.worker, request.new_generation, request.expected_revision,
                                                  error);
    }
    std::string configuration_ascii;
    if (ok) {
        ok = write_replacement_config(server, directory, request, configuration_ascii, error);
    }
    write_replacement_receipt(server, directory, ok, request, configuration_ascii, error);
    std::filesystem::remove(claimed, ec);
    return true;
}

// One `location.N` row per location: the four parts of its identity, its worker, its generation and its native
// load slot.
bool read_plan_locations(const std::map<std::string, std::string>& fields, uint64_t count,
                         std::vector<LocationLease>& locations, std::string& error) {
    locations.reserve(static_cast<size_t>(count));
    for (uint64_t index = 1; index <= count; ++index) {
        auto row = fields.find("location." + std::to_string(index));
        std::vector<uint64_t> values;
        if (row == fields.end() || !numbers(row->second, 7, values) || values[6] > 1) {
            error = "location_plan_location_row";
            return false;
        }
        const LocationId id{values[0], values[1], values[2], values[3]};
        locations.push_back({id, values[4], values[5], 1, {}, true, static_cast<uint32_t>(values[6])});
    }
    return true;
}

// A field that names one of the plan's `count` locations by its one-based row number. `location` receives the
// zero-based index.
bool read_plan_location_index(const std::map<std::string, std::string>& fields, const std::string& name, uint64_t count,
                              uint32_t& location) {
    auto value = fields.find(name);
    uint64_t index = 0;
    if (value == fields.end() || !decimal(value->second, index) || !index || index > count) {
        return false;
    }
    location = static_cast<uint32_t>(index - 1);
    return true;
}

bool read_plan_players(const std::map<std::string, std::string>& fields, uint64_t count,
                       std::array<uint32_t, 2>& players, std::string& error) {
    for (uint64_t player = 1; player <= 2; ++player) {
        if (!read_plan_location_index(fields, "player." + std::to_string(player), count,
                                      players[static_cast<size_t>(player - 1)])) {
            error = "location_plan_player";
            return false;
        }
    }
    return true;
}

bool read_plan_globals(const std::map<std::string, std::string>& fields, uint64_t count,
                       std::map<GlobalSystem, uint32_t>& globals, std::string& error) {
    const std::pair<const char*, GlobalSystem> names[] = {{"global.economy", GlobalSystem::economy},
                                                          {"global.diplomacy", GlobalSystem::diplomacy},
                                                          {"global.galaxy_ai", GlobalSystem::galaxy_ai},
                                                          {"global.disasters", GlobalSystem::disasters}};
    for (const auto& named : names) {
        uint32_t location = 0;
        if (!read_plan_location_index(fields, named.first, count, location)) {
            error = "location_plan_global_owner";
            return false;
        }
        globals.emplace(named.second, location);
    }
    return true;
}

bool read_location_plan(const std::wstring& path, LocationPlan& plan, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 16384) {
        error = "location_plan_missing_or_oversized";
        return false;
    }
    std::ifstream input{std::filesystem::path(path)};
    std::map<std::string, std::string> fields;
    if (!read_fields(input, false, fields)) {
        error = "location_plan_invalid";
        return false;
    }
    auto schema = fields.find("schema"), count_field = fields.find("location_count");
    uint64_t count = 0;
    if (schema == fields.end() || schema->second != "1" || count_field == fields.end() ||
        !decimal(count_field->second, count) || count < 2 || count > 64 || fields.size() != count + 8) {
        error = "location_plan_schema_or_count";
        return false;
    }
    std::vector<LocationLease> locations;
    std::array<uint32_t, 2> players{};
    std::map<GlobalSystem, uint32_t> globals;
    if (!read_plan_locations(fields, count, locations, error) || !read_plan_players(fields, count, players, error) ||
        !read_plan_globals(fields, count, globals, error)) {
        return false;
    }
    plan.locations = std::move(locations);
    plan.players = players;
    plan.globals = std::move(globals);
    error.clear();
    return true;
}

bool bootstrap_locations(Coordinator& server, uint64_t expected, const LocationPlan& plan, std::string& error) {
    const auto checkpoint = server.checkpoint_revision();
    const auto content = server.peer_config(Role::player, 1).identity.content;
    return server.commit_locations(
        expected,
        [&](LocationLedger& ledger, std::string& reason) {
            for (auto location : plan.locations) {
                location.checkpoint = checkpoint;
                location.content = content;
                if (!ledger.add_location(location, reason)) {
                    return false;
                }
            }
            for (size_t player = 0; player < plan.players.size(); ++player) {
                if (!ledger.place_avatar(player + 1, plan.locations[plan.players[player]].id, reason)) {
                    return false;
                }
            }
            for (const auto& owner : plan.globals) {
                const auto& location = plan.locations[owner.second];
                if (!ledger.set_global_owner(owner.first, {location.worker, location.generation}, reason)) {
                    return false;
                }
            }
            return true;
        },
        error);
}

// The command line: the mode, then its options. Every option takes one value and may appear once.
struct Arguments {
    std::wstring mode, config_path, output_path, state_path, artifact_path, plan_path, expected_revision,
        fault_boundary;
};

// The argument that the option `name` sets, or null for an unknown option.
std::wstring* option_value(Arguments& arguments, const std::wstring& name) {
    if (name == L"--config") {
        return &arguments.config_path;
    }
    if (name == L"--state") {
        return &arguments.state_path;
    }
    if (name == L"--artifact") {
        return &arguments.artifact_path;
    }
    if (name == L"--plan") {
        return &arguments.plan_path;
    }
    if (name == L"--expected-revision") {
        return &arguments.expected_revision;
    }
    if (name == L"--test-fault-boundary") {
        return &arguments.fault_boundary;
    }
    if (name == L"--output") {
        return &arguments.output_path;
    }
    return nullptr;
}

bool parse_arguments(int argc, wchar_t** argv, Arguments& arguments, std::string& error) {
    arguments.mode = argv[1];
    std::map<std::wstring, bool> seen;
    for (int index = 2; index < argc; ++index) {
        std::wstring name = argv[index];
        if (!seen.emplace(name, true).second) {
            error = "duplicate_argument";
            return false;
        }
        std::wstring* value = option_value(arguments, name);
        if (!value || index + 1 >= argc) {
            error = "unknown_or_missing_argument";
            return false;
        }
        *value = argv[++index];
    }
    return true;
}

// Mode --world-identity: prints the hash of each world file under a SPORE installation root.
int print_world_identity(const std::wstring& root) {
    WorldIdentity world;
    std::string error;
    if (!read_world_identity(root, world, error)) {
        return fail(error);
    }
    std::cout << "{\"schema\":1,\"evidence_class\":\"HOST\",\"native_validation\":\"NOT_RUN\",\"fields\":{";
    for (size_t index = 0; index < world_file_count; ++index) {
        if (index) {
            std::cout << ',';
        }
        std::cout << quote(world_files[index].field) << ':' << quote(hex(world[index]));
    }
    std::cout << "}}\n";
    return 0;
}

// Waits up to twelve seconds for the coordinator's answer to the player's hello.
int await_probe_welcome(Peer& peer) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    Event event;
    while (std::chrono::steady_clock::now() < deadline) {
        while (peer.poll(event)) {
            if (event.kind == EventKind::disconnected) {
                peer.stop();
                return fail(event.detail);
            }
            if (event.kind == EventKind::packet && event.packet.kind == Kind::reject) {
                peer.stop();
                return fail(error_detail(event.packet));
            }
            if (event.kind == EventKind::packet && event.packet.kind == Kind::welcome) {
                peer.stop();
                std::cout << "{\"authenticated\":true,\"player\":" << event.packet.player
                          << ",\"session\":" << event.packet.session
                          << ",\"protocol\":1,\"tls\":\"1.2\",\"native_baseline_applied\":false}\n";
                return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    peer.stop();
    return fail("authentication_timeout");
}

// Mode --client-probe: connects with a player's configuration and reports whether the coordinator admits it.
int run_client_probe(const Arguments& arguments) {
    if (!arguments.output_path.empty() || !arguments.state_path.empty() || !arguments.artifact_path.empty() ||
        !arguments.plan_path.empty() || !arguments.expected_revision.empty() || !arguments.fault_boundary.empty()) {
        return fail("probe_does_not_accept_output_or_state");
    }
    std::string error;
    PeerConfig config;
    if (!read_peer_config(arguments.config_path, config, error)) {
        return fail(error);
    }
    if (config.role != Role::player) {
        return fail("probe_requires_player_credential");
    }
    Peer peer;
    if (!peer.start(config, error)) {
        return fail(error);
    }
    return await_probe_welcome(peer);
}

// Which options each server mode requires and refuses.
bool server_arguments_valid(const Arguments& arguments, bool commit, bool exporting, bool bootstrapping,
                            std::string& error) {
    if ((arguments.mode != L"--serve" && !commit && !exporting && !bootstrapping) || arguments.output_path.empty() ||
        ((commit || exporting || bootstrapping) && arguments.state_path.empty()) ||
        (commit && (!arguments.plan_path.empty())) ||
        (bootstrapping &&
         (arguments.plan_path.empty() || !arguments.artifact_path.empty() || !arguments.fault_boundary.empty())) ||
        (!commit && !bootstrapping &&
         (!arguments.artifact_path.empty() || !arguments.expected_revision.empty() ||
          !arguments.fault_boundary.empty() || !arguments.plan_path.empty()))) {
        error = "invalid_server_arguments";
        return false;
    }
    if (!arguments.fault_boundary.empty() && arguments.fault_boundary != L"before_record_write" &&
        arguments.fault_boundary != L"after_record_flush" && arguments.fault_boundary != L"after_record_publish" &&
        arguments.fault_boundary != L"after_head_flush" && arguments.fault_boundary != L"after_head_publish") {
        error = "unknown_checkpoint_fault_boundary";
        return false;
    }
    return true;
}

// The durable revision a checkpoint commit or a location bootstrap must find. A commit also needs its artifact.
bool parse_expected_revision(const Arguments& arguments, bool commit, uint64_t& expected, std::string& error) {
    if ((commit && arguments.artifact_path.empty()) || arguments.expected_revision.empty() ||
        arguments.expected_revision.size() > 19 ||
        arguments.expected_revision.find_first_not_of(L"0123456789") != std::wstring::npos) {
        error = "expected_revision_required";
        return false;
    }
    try {
        expected = std::stoull(arguments.expected_revision);
    } catch (...) {
        error = "expected_revision_invalid";
        return false;
    }
    if (!expected) {
        error = "expected_revision_required";
        return false;
    }
    return true;
}

// The durable state lives in --state, or in a `state` directory under the output. An offline mode needs existing
// state; serving creates the directory when it is missing.
bool prepare_state_directory(const std::wstring& state_path, const std::filesystem::path& directory,
                             CoordinatorConfig& config, std::string& error) {
    config.state_directory =
        state_path.empty() ? std::filesystem::absolute(directory / L"state") : std::filesystem::path(state_path);
    std::error_code state_error;
    bool exists = std::filesystem::exists(config.state_directory, state_error);
    if (state_error) {
        error = "state_directory_unavailable";
        return false;
    }
    if (!exists && config.offline) {
        error = "checkpoint_state_missing";
        return false;
    }
    return exists || private_directory(config.state_directory.wstring(), error);
}

bool write_receipt(const std::filesystem::path& path, const std::string& receipt) {
    std::ofstream output(path, std::ios::binary);
    output << receipt << '\n';
    output.close();
    return bool(output);
}

// Mode --location-bootstrap: publishes the plan's locations, avatars and global owners, then writes the receipt.
int run_location_bootstrap(Coordinator& server, const std::wstring& plan_path, uint64_t expected,
                           const std::filesystem::path& directory) {
    std::string error;
    LocationPlan plan;
    if (!read_location_plan(plan_path, plan, error) || !bootstrap_locations(server, expected, plan, error)) {
        return fail(error);
    }
    std::string slots = "[";
    for (size_t index = 0; index < plan.locations.size(); ++index) {
        if (index) {
            slots += ",";
        }
        slots += std::to_string(plan.locations[index].native_load_slot);
    }
    slots += "]";
    const auto receipt = "{\"schema\":1,\"durable\":true,\"evidence_class\":\"HOST\",\"native_validation\":\"NOT_RUN\","
                         "\"universe\":" +
                         quote(hex(server.universe())) + ",\"revision\":" + std::to_string(server.durable_revision()) +
                         ",\"checkpoint_revision\":" + std::to_string(server.checkpoint_revision()) +
                         ",\"location_count\":" + std::to_string(plan.locations.size()) +
                         ",\"native_load_slots\":" + slots + "}";
    if (!write_receipt(directory / L"location-bootstrap-receipt.json", receipt)) {
        return fail("location_receipt_write_failed");
    }
    std::cout << receipt << '\n';
    server.stop();
    return 0;
}

// Mode --checkpoint-commit. The test fault boundary ends the process at the named point of the durable publication.
bool commit_checkpoint_artifact(Coordinator& server, const Arguments& arguments, uint64_t expected,
                                std::string& error) {
    std::vector<uint8_t> artifact;
    const auto boundary = [&](const char* point) {
        std::string name(point);
        if (std::wstring(name.begin(), name.end()) == arguments.fault_boundary) {
            TerminateProcess(GetCurrentProcess(), 91);
        }
    };
    return read_checkpoint_artifact(arguments.artifact_path, artifact, error) &&
           server.commit_checkpoint(artifact, expected, error, boundary);
}

// The configurations and invites of a universe that has no locations.
bool write_default_peer_files(const Coordinator& server, const std::filesystem::path& directory) {
    return write_peer(directory / L"authority.conf", server.peer_config(Role::authority)) &&
           write_peer(directory / L"player-1.conf", server.peer_config(Role::player, 1)) &&
           write_peer(directory / L"player-2.conf", server.peer_config(Role::player, 2)) &&
           write_invite(directory / L"player-1.invite", server.peer_config(Role::player, 1)) &&
           write_invite(directory / L"player-2.invite", server.peer_config(Role::player, 2));
}

// One authority configuration per location, and a configuration and an invite for each player it admits.
bool write_location_peer_files(const Coordinator& server, const std::filesystem::path& directory, std::string& error) {
    for (const auto& location : server.location_ids()) {
        const auto suffix = location_suffix(location);
        PeerConfig authority_config;
        std::string peer_error;
        if (!server.peer_config_at(Role::authority, 0, location, authority_config, peer_error) ||
            !write_peer(directory / (L"authority" + suffix + L".conf"), authority_config)) {
            error = "write_location_authority_config_failed";
            return false;
        }
        for (uint64_t player_id = 1; player_id <= 2; ++player_id) {
            PeerConfig player_config;
            if (server.peer_config_at(Role::player, player_id, location, player_config, peer_error)) {
                if (!write_peer(directory / (L"player-" + std::to_wstring(player_id) + suffix + L".conf"),
                                player_config) ||
                    !write_invite(directory / (L"player-" + std::to_wstring(player_id) + suffix + L".invite"),
                                  player_config)) {
                    error = "write_location_player_config_failed";
                    return false;
                }
            }
        }
    }
    return true;
}

// The offline modes end by reporting the selected checkpoint; --checkpoint-export also writes its artifact.
int write_checkpoint_receipt(Coordinator& server, const std::filesystem::path& directory, bool exporting) {
    std::string error;
    CheckpointArtifact checkpoint;
    if (!validate_checkpoint_artifact(server.checkpoint_artifact(), checkpoint, error)) {
        return fail(error);
    }
    if (exporting && !write_checkpoint_artifact(directory / L"checkpoint.smp", server.checkpoint_artifact(), error)) {
        return fail(error);
    }
    const auto receipt = "{\"schema\":1,\"durable\":true,\"universe\":" + quote(hex(server.universe())) +
                         ",\"revision\":" + std::to_string(server.durable_revision()) +
                         ",\"checkpoint_revision\":" + std::to_string(server.checkpoint_revision()) +
                         ",\"artifact_sha256\":" + quote(hex(checkpoint.digest)) +
                         ",\"rollback_ms\":" + std::to_string(checkpoint.rollback_ms) + "}";
    if (!write_receipt(directory / L"checkpoint-receipt.json", receipt)) {
        return fail("checkpoint_receipt_write_failed");
    }
    std::cout << receipt << '\n';
    server.stop();
    return 0;
}

void log_event(std::ostream& log, const CoordinatorEvent& event) {
    const Packet& packet = event.event.packet;
    log << "{\"time_ms\":" << GetTickCount64() << ",\"connection\":" << event.connection
        << ",\"event\":" << int(event.event.kind) << ",\"kind\":" << uint32_t(packet.kind)
        << ",\"sequence\":" << packet.sequence << ",\"scene\":" << packet.scene << ",\"baseline\":" << packet.baseline
        << ",\"player\":" << packet.player << ",\"entity\":" << packet.entity.id
        << ",\"generation\":" << packet.entity.generation << ",\"tick\":" << packet.entity.tick
        << ",\"outbound\":" << (event.outbound ? "true" : "false") << ",\"request\":" << packet.request
        << ",\"target\":" << packet.target << ",\"target_generation\":" << packet.target_generation
        << ",\"verb\":" << uint32_t(packet.verb) << ",\"life_state\":" << packet.entity.life_state << ",\"location\":["
        << packet.location.galaxy << "," << packet.location.star << "," << packet.location.planet << ","
        << packet.location.area << "],\"worker_generation\":" << packet.worker_generation
        << ",\"native_load_slot\":" << packet.native_load_slot << ",\"error\":" << quote(error_detail(packet))
        << ",\"detail\":" << quote(event.event.detail) << content_log(packet) << "}\n";
}

// Logs the events the coordinator has pending, 8192 at most. False once the log failed or reached its event limit.
bool log_pending_events(Coordinator& server, std::ostream& log, uint64_t& events) {
    CoordinatorEvent event;
    size_t count = 0;
    while (count++ < 8192 && server.poll(event)) {
        ++events;
        log_event(log, event);
        if (events % 128 == 0) {
            log.flush();
        }
        if (!log || events >= 10000000) {
            return false;
        }
    }
    return true;
}

void log_admin_operation(std::ostream& log, const char* operation, const std::string& error,
                         const Coordinator& server) {
    log << "{\"admin_operation\":\"" << operation << "\",\"error\":" << quote(error)
        << ",\"revision\":" << server.durable_revision() << "}\n";
}

// Carries out the request files that an operator or the worker host left in the private directory.
void process_admin_requests(Coordinator& server, const std::filesystem::path& directory, std::ostream& log) {
    std::string replacement_error;
    if (replace_location_request(server, directory, replacement_error)) {
        log_admin_operation(log, "replace_location", replacement_error, server);
    }
    std::string transfer_error;
    if (process_transfer_request(server, directory, transfer_error)) {
        log_admin_operation(log, "transfer", transfer_error, server);
    }
    std::string global_error;
    if (process_global_checkpoint_request(server, directory, global_error)) {
        log_admin_operation(log, "global_checkpoint", global_error, server);
    }
}

// Mode --serve: pumps the coordinator and logs its events until a stop is requested or the log is exhausted.
int serve(Coordinator& server, const std::filesystem::path& directory) {
    auto player = server.peer_config(Role::player, 1);
    std::ofstream log(directory / L"coordinator.jsonl", std::ios::binary);
    if (!log) {
        return fail("coordinator_log_failed");
    }
    SetConsoleCtrlHandler(ctrl, TRUE);
    std::cout << "{\"ready\":true,\"port\":" << player.port
              << ",\"certificate_sha256\":" << quote(hex(player.certificate_pin))
              << ",\"universe\":" << quote(hex(server.universe()))
              << ",\"durable_revision\":" << server.durable_revision() << "}\n"
              << std::flush;
    uint64_t events = 0;
    bool exhausted = false;
    ULONGLONG next_stop_check = 0, next_log_flush = 0;
    while (!quitting) {
        if (!log_pending_events(server, log, events)) {
            quitting = true;
            exhausted = true;
        }
        auto now = GetTickCount64();
        if (now >= next_log_flush) {
            log.flush();
            next_log_flush = now + 100;
        }
        if (now >= next_stop_check) {
            next_stop_check = now + 200;
            process_admin_requests(server, directory, log);
            std::error_code ec;
            if (std::filesystem::is_regular_file(directory / L"stop.request", ec)) {
                quitting = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    server.stop();
    log << "{\"stopped\":true,\"events\":" << events << ",\"log_limit_exhausted\":" << (exhausted ? "true" : "false")
        << "}\n";
    log.flush();
    return log && !exhausted ? 0 : 21;
}

// Every mode that opens the universe: --serve, --checkpoint-commit, --checkpoint-export and --location-bootstrap.
int run_server(const Arguments& arguments) {
    const bool commit = arguments.mode == L"--checkpoint-commit", exporting = arguments.mode == L"--checkpoint-export",
               bootstrapping = arguments.mode == L"--location-bootstrap";
    std::string error;
    if (!server_arguments_valid(arguments, commit, exporting, bootstrapping, error)) {
        return fail(error);
    }
    uint64_t expected = 0;
    if ((commit || bootstrapping) && !parse_expected_revision(arguments, commit, expected, error)) {
        return fail(error);
    }
    CoordinatorConfig config;
    if (!read_server(arguments.config_path, config, error)) {
        return fail(error);
    }
    config.offline = commit || exporting || bootstrapping;
    if (!private_directory(arguments.output_path, error)) {
        return fail(error);
    }
    std::filesystem::path directory(arguments.output_path);
    if (!prepare_state_directory(arguments.state_path, directory, config, error)) {
        return fail(error);
    }
    Coordinator server;
    if (!server.start(config, error)) {
        return fail(error);
    }
    if (bootstrapping) {
        return run_location_bootstrap(server, arguments.plan_path, expected, directory);
    }
    if (commit && !commit_checkpoint_artifact(server, arguments, expected, error)) {
        return fail(error);
    }
    if (!write_default_peer_files(server, directory)) {
        return fail("write_private_invite_failed");
    }
    if (!write_location_peer_files(server, directory, error)) {
        return fail(error);
    }
    if (config.offline) {
        return write_checkpoint_receipt(server, directory, exporting);
    }
    return serve(server, directory);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--world-identity" && std::wstring(argv[2]) == L"--root") {
        return print_world_identity(argv[3]);
    }
    if (argc < 4) {
        return fail("usage: --serve --config PATH --output NEW_DIRECTORY [--state ABSOLUTE_DIRECTORY] | "
                    "--location-bootstrap --config PATH --state DIRECTORY --plan PATH --expected-revision N --output "
                    "NEW_DIRECTORY | --client-probe --config PATH");
    }
    Arguments arguments;
    std::string error;
    if (!parse_arguments(argc, argv, arguments, error)) {
        return fail(error);
    }
    if (arguments.mode == L"--client-probe") {
        return run_client_probe(arguments);
    }
    return run_server(arguments);
}
