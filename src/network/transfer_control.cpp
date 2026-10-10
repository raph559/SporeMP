#include "transfer_control.h"
#include <windows.h>
#include <map>
#include <sstream>

namespace sporemp::network {
namespace {
struct Request {
    TransferPreparation preparation;
    uint64_t expected_revision = 0;
    std::string op;
    uint64_t source_slot = 0, destination_slot = 0, process_id = 0, process_created = 0, replacement_worker = 0,
             replacement_generation = 0;
    Digest file_hash{};
};

struct CheckpointRequest {
    uint64_t expected_revision = 0;
    Digest file_hash{}, artifact_hash{}, approval_hash{}, proof_hash{};
};

using Fields = std::map<std::string, std::string>;

// The fields every transfer request carries, whatever its operation.
constexpr const char* common_fields[]{"schema",
                                      "op",
                                      "id",
                                      "player",
                                      "expected_revision",
                                      "prepare_expected_revision",
                                      "checkpoint_revision",
                                      "avatar_generation",
                                      "source_worker",
                                      "source_worker_generation",
                                      "destination_worker",
                                      "destination_worker_generation",
                                      "source",
                                      "destination",
                                      "universe",
                                      "checkpoint_sha256"};

bool parse_decimal(const std::string& text, uint64_t& value) {
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

bool parse_location(const std::string& text, LocationId& id) {
    if (text.empty() || text.back() == ',') {
        return false;
    }
    std::istringstream input(text);
    std::string part;
    uint64_t values[4]{};
    size_t count = 0;
    while (std::getline(input, part, ',')) {
        if (count == 4 || !parse_decimal(part, values[count])) {
            return false;
        }
        ++count;
    }
    if (count != 4) {
        return false;
    }
    id = {values[0], values[1], values[2], values[3]};
    return id.valid();
}

// One `name=value` per line, each name once and never empty.
bool parse_fields(const std::string& text, Fields& fields) {
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto equal = line.find('=');
        if (equal == std::string::npos || !equal ||
            !fields.emplace(line.substr(0, equal), line.substr(equal + 1)).second) {
            return false;
        }
    }
    return true;
}

HANDLE open_for_reading(const std::filesystem::path& path) {
    return CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}

// Reads the whole of an open file that is regular (no directory, reparse point or second hard link), not empty
// and at most `maximum` bytes.
bool read_regular_file(HANDLE file, size_t maximum, std::vector<uint8_t>& bytes) {
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    DWORD read = 0;
    bool ok = GetFileInformationByHandle(file, &info) &&
              !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
              info.nNumberOfLinks == 1 && GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
              uint64_t(size.QuadPart) <= maximum;
    if (ok) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
    }
    return ok;
}

bool read_artifact_member(const std::filesystem::path& path, size_t maximum, std::vector<uint8_t>& bytes) {
    const auto file = open_for_reading(path);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    const bool ok = read_regular_file(file, maximum, bytes);
    CloseHandle(file);
    return ok;
}

// Writes a new temporary file, flushes it and moves it into place; the temporary is removed when either fails.
// A temporary that already exists is left alone: another writer owns it.
bool publish_file(const std::filesystem::path& temporary, const std::filesystem::path& destination, const void* data,
                  size_t size, DWORD move_flags) {
    const auto file =
        CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool ok =
        WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) && written == size && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok && MoveFileExW(temporary.c_str(), destination.c_str(), move_flags)) {
        return true;
    }
    DeleteFileW(temporary.c_str());
    return false;
}

bool publish_artifact_member(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> existing;
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return read_artifact_member(path, bytes.size(), existing) && existing == bytes;
    }
    auto temporary = path;
    temporary += L".tmp";
    return publish_file(temporary, path, bytes.data(), bytes.size(), MOVEFILE_WRITE_THROUGH);
}

bool publish_receipt(const std::filesystem::path& directory, const std::string& text,
                     const std::wstring& stem = L"transfer-receipt") {
    return publish_file(directory / (stem + L".tmp"), directory / (stem + L".json"), text.data(), text.size(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

bool publish_peer_config(const std::filesystem::path& directory, const std::wstring& name, const PeerConfig& config) {
    const auto temporary = directory / (name + L".tmp");
    return write_peer_config(temporary.wstring(), config) &&
           MoveFileExW(temporary.c_str(), (directory / name).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

// The control directory is a real directory, not a link to one.
bool usable_directory(const std::filesystem::path& directory) {
    const auto attributes = GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return false;
    }
    return true;
}

enum class Claim {
    absent,
    failed,
    held
};

// A claim surviving process/receipt failure is replayed with its complete
// durable identity. Never replace a prior claim with a newer request.
Claim claim_request(const std::filesystem::path& request, const std::filesystem::path& claimed) {
    if (GetFileAttributesW(claimed.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return Claim::held;
    }
    if (GetFileAttributesW(request.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return Claim::absent;
    }
    if (!MoveFileExW(request.c_str(), claimed.c_str(), MOVEFILE_WRITE_THROUGH)) {
        return Claim::failed;
    }
    return Claim::held;
}

bool read_request_file(const std::filesystem::path& path, std::string& text, Digest& file_hash, std::string& error) {
    const auto handle = open_for_reading(path);
    if (handle == INVALID_HANDLE_VALUE) {
        error = "transfer_request_open_failed";
        return false;
    }
    std::vector<uint8_t> bytes;
    const bool regular = read_regular_file(handle, 2048, bytes);
    CloseHandle(handle);
    if (!regular) {
        error = "transfer_request_not_regular_or_bounded";
        return false;
    }
    if (!content_digest(bytes.data(), bytes.size(), file_hash)) {
        error = "transfer_request_hash_failed";
        return false;
    }
    text.assign(bytes.begin(), bytes.end());
    return true;
}

// Reads the fields an operation takes beyond the common ones and reports how many those are. An operation
// that is not listed here is refused.
bool read_operation_fields(const Fields& fields, Request& request, size_t& extra, std::string& error) {
    const auto& op = request.op;
    if (op == "fence") {
        extra = 2;
        if (!fields.count("source_slot") || !fields.count("destination_slot") ||
            !parse_decimal(fields.at("source_slot"), request.source_slot) ||
            !parse_decimal(fields.at("destination_slot"), request.destination_slot) || request.source_slot < 1 ||
            request.source_slot > 2 || request.destination_slot < 1 || request.destination_slot > 2) {
            error = "transfer_request_native_slots";
            return false;
        }
    } else if (op == "register_source" || op == "register_destination") {
        extra = 2;
        if (!fields.count("process_id") || !fields.count("process_created") ||
            !parse_decimal(fields.at("process_id"), request.process_id) || !request.process_id ||
            request.process_id > UINT32_MAX || !parse_decimal(fields.at("process_created"), request.process_created) ||
            !request.process_created) {
            error = "transfer_request_process_identity";
            return false;
        }
    } else if (op == "recover_source" || op == "recover_destination") {
        extra = 2;
        if (!fields.count("replacement_worker") || !fields.count("replacement_generation") ||
            !parse_decimal(fields.at("replacement_worker"), request.replacement_worker) ||
            !request.replacement_worker ||
            !parse_decimal(fields.at("replacement_generation"), request.replacement_generation) ||
            !request.replacement_generation) {
            error = "transfer_request_recovery_identity";
            return false;
        }
    } else if (op != "prepare" && op != "cancel" && op != "prepare_maintenance" && op != "bind" && op != "restore" &&
               op != "discard" && op != "rollback" && op != "activate" && op != "source_activate" &&
               op != "source_cleanup" && op != "commit" && op != "cleanup" && op != "status" && op != "artifact") {
        error = "transfer_native_phase_unavailable";
        return false;
    }
    return true;
}

// Reads the transfer a request names: its numbers, its two locations and the universe and checkpoint it was
// prepared against.
bool read_preparation(const Fields& fields, Request& request, std::string& error) {
    auto& preparation = request.preparation;
    const std::pair<const char*, uint64_t*> numbers[]{
        {"id", &preparation.id},
        {"player", &preparation.player},
        {"expected_revision", &request.expected_revision},
        {"prepare_expected_revision", &preparation.expected_revision},
        {"checkpoint_revision", &preparation.checkpoint_revision},
        {"avatar_generation", &preparation.avatar_generation},
        {"source_worker", &preparation.source_worker},
        {"source_worker_generation", &preparation.source_worker_generation},
        {"destination_worker", &preparation.destination_worker},
        {"destination_worker_generation", &preparation.destination_worker_generation}};
    for (const auto& field : numbers) {
        if (!parse_decimal(fields.at(field.first), *field.second)) {
            error = "transfer_request_number";
            return false;
        }
    }
    const auto& op = request.op;
    if (!parse_location(fields.at("source"), preparation.source) ||
        !parse_location(fields.at("destination"), preparation.destination) ||
        !parse_hex(fields.at("universe"), preparation.universe) ||
        !parse_hex(fields.at("checkpoint_sha256"), preparation.checkpoint_hash) ||
        ((op == "prepare" || op == "prepare_maintenance") &&
         request.expected_revision != preparation.expected_revision)) {
        error = "transfer_request_identity";
        return false;
    }
    return true;
}

bool read_request(const std::filesystem::path& path, Request& request, std::string& error) {
    std::string text;
    if (!read_request_file(path, text, request.file_hash, error)) {
        return false;
    }
    Fields fields;
    if (!parse_fields(text, fields)) {
        error = "transfer_request_invalid";
        return false;
    }
    for (const auto* name : common_fields) {
        if (!fields.count(name)) {
            error = "transfer_request_fields";
            return false;
        }
    }
    if (fields.at("schema") != "1" && fields.at("schema") != "2") {
        error = "transfer_request_schema";
        return false;
    }
    request.op = fields.at("op");
    if (fields.at("schema") == "1" && request.op != "prepare" && request.op != "cancel") {
        error = "transfer_native_phase_unavailable";
        return false;
    }
    size_t extra = 0;
    if (!read_operation_fields(fields, request, extra, error)) {
        return false;
    }
    if (fields.size() != std::size(common_fields) + extra) {
        error = "transfer_request_fields";
        return false;
    }
    if (!read_preparation(fields, request, error)) {
        return false;
    }
    error.clear();
    return true;
}

// Publishes the durable artifact of a transfer and its approval beside the request, or confirms that the
// files already there are those bytes.
bool export_artifact(Coordinator& server, const std::filesystem::path& directory, uint64_t id, std::string& error) {
    std::vector<uint8_t> artifact, approval;
    const auto stem = L"transfer-" + std::to_wstring(id);
    if (!server.native_transfer_artifact(id, artifact, approval)) {
        error = "transfer_durable_artifact_unavailable";
        return false;
    }
    if (!publish_artifact_member(directory / (stem + L".db"), artifact) ||
        !publish_artifact_member(directory / (stem + L".expected"), approval)) {
        error = "transfer_durable_artifact_write_conflict";
        return false;
    }
    error.clear();
    return true;
}

// Binds the closed artifact and approval an operator placed beside the request to the transfer.
bool bind_artifact(Coordinator& server, const std::filesystem::path& directory, uint64_t id, uint64_t revision,
                   std::string& error) {
    std::vector<uint8_t> artifact, approval;
    const auto stem = L"transfer-" + std::to_wstring(id);
    if (!read_artifact_member(directory / (stem + L".db"), 1024 * 1024, artifact) ||
        !read_artifact_member(directory / (stem + L".expected"), 20680, approval)) {
        error = "transfer_closed_artifact_unavailable";
        return false;
    }
    return server.bind_native_transfer_artifact(id, revision, artifact, approval, error);
}

// Registers the local process of the source or destination worker, under the worker identity the native
// transfer holds once it has begun and the prepared one before that.
bool register_worker_process(Coordinator& server, const Request& request, std::string& error) {
    const auto& preparation = request.preparation;
    if (server.durable_revision() != request.expected_revision) {
        error = "transfer_request_stale_revision";
        return false;
    }
    const bool source = request.op == "register_source";
    const auto* native = server.native_transfer(preparation.id);
    return server.register_local_worker_process(
        source ? preparation.source : preparation.destination,
        source ? (native ? native->source_worker : preparation.source_worker)
               : (native ? native->destination_worker : preparation.destination_worker),
        source ? (native ? native->source_generation : preparation.source_worker_generation)
               : (native ? native->destination_generation : preparation.destination_worker_generation),
        static_cast<uint32_t>(request.process_id), request.process_created, error);
}

// The native phase an operation asks a worker for; read_request has refused every name not handled before this.
TransferOp native_operation(const std::string& op) {
    return op == "restore"           ? TransferOp::destination_restore
           : op == "discard"         ? TransferOp::destination_discard
           : op == "rollback"        ? TransferOp::source_restore
           : op == "activate"        ? TransferOp::destination_activate
           : op == "source_activate" ? TransferOp::source_activate
                                     : TransferOp::source_cleanup;
}

bool execute_request(Coordinator& server, const std::filesystem::path& directory, const Request& request,
                     std::string& error) {
    const auto& preparation = request.preparation;
    const auto revision = request.expected_revision;
    const auto& op = request.op;
    if (op == "prepare") {
        return server.prepare_transfer(preparation, error);
    }
    if (op == "prepare_maintenance") {
        return server.prepare_transfer_maintenance(preparation, error);
    }
    if (op == "cancel") {
        return server.cancel_transfer(preparation, revision, error);
    }
    const auto* prepared = server.transfer_preparation(preparation.id);
    if (!prepared || !prepared->matches(preparation)) {
        error = "transfer_request_preparation_mismatch";
        return false;
    }
    if (op == "status") {
        error.clear();
        return true;
    }
    if (op == "artifact") {
        return export_artifact(server, directory, preparation.id, error);
    }
    if (op == "recover_source" || op == "recover_destination") {
        return server.recover_native_transfer_worker(preparation.id, revision, op == "recover_source",
                                                     request.replacement_worker, request.replacement_generation, error);
    }
    if (op == "register_source" || op == "register_destination") {
        return register_worker_process(server, request, error);
    }
    if (op == "fence") {
        return server.begin_native_transfer(preparation.id, revision, static_cast<uint32_t>(request.source_slot),
                                            static_cast<uint32_t>(request.destination_slot), error);
    }
    if (op == "bind") {
        return bind_artifact(server, directory, preparation.id, revision, error);
    }
    if (op == "commit") {
        return server.commit_native_transfer(preparation.id, revision, error);
    }
    if (op == "cleanup") {
        return server.finish_native_transfer(preparation.id, revision, error);
    }
    return server.request_native_transfer(preparation.id, revision, native_operation(op), error);
}

// Rewrites the peer configurations of the two ends of a transfer and of each placed player. A location or
// player the coordinator has no configuration for is skipped; a write that fails is an error.
bool publish_configurations(Coordinator& server, const std::filesystem::path& directory,
                            const TransferPreparation& preparation) {
    for (bool source : {true, false}) {
        PeerConfig config;
        std::string error;
        if (!server.peer_config_at(Role::authority, 0, source ? preparation.source : preparation.destination, config,
                                   error)) {
            continue;
        }
        if (!publish_peer_config(directory, source ? L"authority-source.conf" : L"authority-destination.conf",
                                 config)) {
            return false;
        }
    }
    for (uint64_t player = 1; player <= 2; ++player) {
        const auto* avatar = server.locations().avatar(player);
        if (!avatar || !avatar->active) {
            continue;
        }
        PeerConfig config;
        std::string error;
        if (!server.peer_config_at(Role::player, player, avatar->location, config, error)) {
            continue;
        }
        if (!publish_peer_config(directory, L"player-" + std::to_wstring(player) + L"-transfer.conf", config)) {
            return false;
        }
    }
    return true;
}

std::string witness_object(const TransferFrame& frame, uint64_t session, uint64_t scene, uint64_t baseline) {
    if (!frame.id) {
        return "null";
    }
    auto json = transfer_witness_json(frame, session, scene, baseline);
    if (!json.empty() && json.back() == '\n') {
        json.pop_back();
    }
    return json;
}

// The receipt of a transfer request: what was asked, whether it held, and the state of the native transfer
// it names. Error strings are internal fixed identifiers, never raw request bytes.
std::string transfer_receipt(const Coordinator& server, const Request& request, bool parsed, bool ok,
                             const std::string& error) {
    const auto& preparation = request.preparation;
    const auto* native = server.native_transfer(preparation.id);
    const bool ownership = native && native->ownership_committed;
    const bool completed = native && native->phase == NativeTransferPhase::complete;
    return std::string("{\"schema\":1,\"ok\":") + (ok ? "true" : "false") + ",\"op\":\"" +
           (parsed ? request.op : "invalid") + "\",\"id\":" + std::to_string(preparation.id) +
           ",\"player\":" + std::to_string(preparation.player) + ",\"request_sha256\":\"" + hex(request.file_hash) +
           "\",\"expected_revision\":" + std::to_string(request.expected_revision) +
           ",\"durable_revision\":" + std::to_string(server.durable_revision()) +
           ",\"native_completion\":" + (completed ? "true" : "false") +
           ",\"ownership_changed\":" + (ownership ? "true" : "false") +
           ",\"ownership_committed\":" + (ownership ? "true" : "false") +
           ",\"native_phase\":" + std::to_string(native ? uint32_t(native->phase) : 0) +
           ",\"avatar_generation\":" + std::to_string(native ? native->avatar_generation : 0) +
           ",\"recovery_artifact_transfer\":" + std::to_string(server.recovery_artifact_transfer(preparation.player)) +
           ",\"source_worker\":" + std::to_string(native ? native->source_worker : preparation.source_worker) +
           ",\"source_worker_generation\":" +
           std::to_string(native ? native->source_generation : preparation.source_worker_generation) +
           ",\"destination_worker\":" +
           std::to_string(native ? native->destination_worker : preparation.destination_worker) +
           ",\"destination_worker_generation\":" +
           std::to_string(native ? native->destination_generation : preparation.destination_worker_generation) +
           ",\"source_witness_sha256\":\"" + hex(native ? native->source_receipt.state : Digest{}) +
           "\",\"source_witness\":" +
           (native ? witness_object(native->source_receipt, native->source_session, native->source_scene,
                                    native->source_baseline)
                   : "null") +
           ",\"destination_witness_sha256\":\"" + hex(native ? native->destination_receipt.state : Digest{}) +
           "\",\"destination_witness\":" +
           (native ? witness_object(native->destination_receipt, native->destination_session, native->destination_scene,
                                    native->destination_baseline)
                   : "null") +
           ",\"command\":" +
           (native ? witness_object(native->command, native->command_session, native->command_scene,
                                    native->command_baseline)
                   : "null") +
           ",\"last_receipt_sha256\":\"" + hex(native ? native->last_receipt.state : Digest{}) +
           "\",\"last_receipt\":" +
           (native ? witness_object(native->last_receipt, native->command_session, native->command_scene,
                                    native->command_baseline)
                   : "null") +
           ",\"error\":\"" + (ok ? std::string{} : error) + "\"}\n";
}

// Reads a claimed global checkpoint request: its own digest, the revision it expects and the digests of the
// three files it approves. What was read before a refusal stays in `request` for the receipt.
bool read_checkpoint_request(const std::filesystem::path& claimed, CheckpointRequest& request) {
    std::vector<uint8_t> bytes;
    if (!read_artifact_member(claimed, 512, bytes) || !content_digest(bytes.data(), bytes.size(), request.file_hash)) {
        return false;
    }
    Fields fields;
    if (!parse_fields(std::string(bytes.begin(), bytes.end()), fields)) {
        return false;
    }
    const char* names[]{"schema", "expected_revision", "artifact_sha256", "approval_sha256", "proof_sha256"};
    if (fields.size() != std::size(names)) {
        return false;
    }
    for (const auto* name : names) {
        if (!fields.count(name)) {
            return false;
        }
    }
    return fields.at("schema") == "1" && parse_decimal(fields.at("expected_revision"), request.expected_revision) &&
           request.expected_revision && parse_hex(fields.at("artifact_sha256"), request.artifact_hash) &&
           parse_hex(fields.at("approval_sha256"), request.approval_hash) &&
           parse_hex(fields.at("proof_sha256"), request.proof_hash);
}

// Reads the checkpoint, its approval and its proof, each of which must still be the bytes the request approved.
bool read_checkpoint_inputs(const std::filesystem::path& directory, const CheckpointRequest& request,
                            std::vector<uint8_t>& artifact, std::vector<uint8_t>& approval,
                            std::vector<uint8_t>& proof) {
    Digest observed{};
    return read_artifact_member(directory / L"global-checkpoint.smp", CheckpointArtifact::max_bytes, artifact) &&
           content_digest(artifact.data(), artifact.size(), observed) && observed == request.artifact_hash &&
           read_artifact_member(directory / L"global-checkpoint.expected", 464, approval) &&
           content_digest(approval.data(), approval.size(), observed) && observed == request.approval_hash &&
           read_artifact_member(directory / L"global-checkpoint-proof.json", 64 * 1024, proof) &&
           content_digest(proof.data(), proof.size(), observed) && observed == request.proof_hash;
}

// Rewrites the peer configuration of every location's authority and of each placed player after a checkpoint.
// Unlike a transfer, a configuration the coordinator cannot produce is an error here.
bool publish_checkpoint_configurations(Coordinator& server, const std::filesystem::path& directory) {
    for (const auto& location : server.location_ids()) {
        PeerConfig config;
        std::string why;
        const auto name = L"global-authority-g" + std::to_wstring(location.galaxy) + L"-s" +
                          std::to_wstring(location.star) + L"-p" + std::to_wstring(location.planet) + L"-a" +
                          std::to_wstring(location.area) + L".conf";
        if (!server.peer_config_at(Role::authority, 0, location, config, why) ||
            !publish_peer_config(directory, name, config)) {
            return false;
        }
    }
    for (uint64_t player = 1; player <= 2; ++player) {
        const auto* avatar = server.locations().avatar(player);
        if (!avatar || !avatar->active) {
            continue;
        }
        PeerConfig config;
        std::string why;
        if (!server.peer_config_at(Role::player, player, avatar->location, config, why) ||
            !publish_peer_config(directory, L"global-player-" + std::to_wstring(player) + L".conf", config)) {
            return false;
        }
    }
    return true;
}

std::string checkpoint_receipt(const Coordinator& server, const CheckpointRequest& request, bool ok,
                               const std::string& error) {
    return std::string("{\"schema\":1,\"ok\":") + (ok ? "true" : "false") + ",\"request_sha256\":\"" +
           hex(request.file_hash) + "\",\"expected_revision\":" + std::to_string(request.expected_revision) +
           ",\"durable_revision\":" + std::to_string(server.durable_revision()) +
           ",\"checkpoint_revision\":" + std::to_string(server.checkpoint_revision()) + ",\"artifact_sha256\":\"" +
           hex(request.artifact_hash) + "\",\"native_reader_qualified\":false,\"error\":\"" +
           (ok ? std::string{} : error) + "\"}\n";
}
} // namespace

bool process_transfer_request(Coordinator& server, const std::filesystem::path& private_directory, std::string& error) {
    if (!usable_directory(private_directory)) {
        error = "transfer_control_directory_invalid";
        return false;
    }
    const auto claimed = private_directory / L"transfer.claimed";
    const auto claim = claim_request(private_directory / L"transfer.request", claimed);
    if (claim == Claim::absent) {
        return false;
    }
    if (claim == Claim::failed) {
        error = "transfer_request_claim_failed";
        return true;
    }
    Request request;
    const bool parsed = read_request(claimed, request, error);
    bool ok = parsed && execute_request(server, private_directory, request, error);
    if (ok && !publish_configurations(server, private_directory, request.preparation)) {
        ok = false;
        error = "transfer_configuration_write_failed";
    }
    if (!publish_receipt(private_directory, transfer_receipt(server, request, parsed, ok, error))) {
        error = "transfer_receipt_write_failed";
        return true;
    }
    if (!DeleteFileW(claimed.c_str())) {
        error = "transfer_claim_cleanup_failed";
    }
    return true;
}

bool process_global_checkpoint_request(Coordinator& server, const std::filesystem::path& private_directory,
                                       std::string& error) {
    if (!usable_directory(private_directory)) {
        error = "global_checkpoint_directory_invalid";
        return false;
    }
    const auto claimed = private_directory / L"global-checkpoint.claimed";
    const auto claim = claim_request(private_directory / L"global-checkpoint.request", claimed);
    if (claim == Claim::absent) {
        return false;
    }
    if (claim == Claim::failed) {
        error = "global_checkpoint_claim_failed";
        return true;
    }
    CheckpointRequest request;
    std::vector<uint8_t> artifact, approval, proof;
    bool ok = read_checkpoint_request(claimed, request);
    if (!ok) {
        error = "global_checkpoint_request_fields";
    }
    if (ok) {
        ok = read_checkpoint_inputs(private_directory, request, artifact, approval, proof);
        if (!ok) {
            error = "global_checkpoint_immutable_inputs_changed";
        }
    }
    if (ok) {
        ok = server.commit_location_checkpoint(artifact, approval, proof, request.expected_revision, error);
    }
    if (ok && !publish_checkpoint_configurations(server, private_directory)) {
        ok = false;
        error = "global_checkpoint_config_write_failed";
    }
    if (!publish_receipt(private_directory, checkpoint_receipt(server, request, ok, error),
                         L"global-checkpoint-receipt")) {
        error = "global_checkpoint_receipt_write_failed";
        return true;
    }
    if (!DeleteFileW(claimed.c_str())) {
        error = "global_checkpoint_claim_cleanup_failed";
    }
    return true;
}
} // namespace sporemp::network
