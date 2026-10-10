#include "coordinator.h"
#include "channel.h"
#include "durable_store.h"
#include "state_codec.h"
#include <array>
#include <map>
#include <optional>
#include <fstream>

namespace sporemp::network {
bool TransferPreparation::matches(const TransferPreparation& other) const noexcept {
    return id == other.id && player == other.player && expected_revision == other.expected_revision &&
           checkpoint_revision == other.checkpoint_revision && avatar_generation == other.avatar_generation &&
           source_worker == other.source_worker && source_worker_generation == other.source_worker_generation &&
           destination_worker == other.destination_worker &&
           destination_worker_generation == other.destination_worker_generation && source == other.source &&
           destination == other.destination && universe == other.universe && checkpoint_hash == other.checkpoint_hash;
}

#include "coordinator_global_checkpoint_format.inc"

namespace {
// The durable universe record begins with this magic and its schema. Schema 9 is written; 2 to 9 are read.
constexpr uint32_t universe_state_magic = 0x39554d53, universe_state_schema = 9;

// Bounds of a transfer's artifact and of its approval, where they are bound and where a record is read back.
constexpr size_t max_transfer_artifact_bytes = 1024 * 1024, max_transfer_approval_bytes = 20680;

// A connection that was sent a rejection stays open this long, so that the rejection is delivered before the close.
constexpr uint64_t rejection_flush_ms = 100;

constexpr std::array<GlobalSystem, 4> global_systems{GlobalSystem::economy, GlobalSystem::diplomacy,
                                                     GlobalSystem::galaxy_ai, GlobalSystem::disasters};

void write_preparation(state::Writer& out, const TransferPreparation& preparation) {
    for (auto value :
         {preparation.id, preparation.player, preparation.expected_revision, preparation.checkpoint_revision,
          preparation.avatar_generation, preparation.source_worker, preparation.source_worker_generation,
          preparation.destination_worker, preparation.destination_worker_generation, preparation.source.galaxy,
          preparation.source.star, preparation.source.planet, preparation.source.area, preparation.destination.galaxy,
          preparation.destination.star, preparation.destination.planet, preparation.destination.area}) {
        out.wide(value);
    }
    out.digest(preparation.universe);
    out.digest(preparation.checkpoint_hash);
}

TransferPreparation read_preparation(state::Reader& in) {
    TransferPreparation preparation;
    preparation.id = in.wide();
    preparation.player = in.wide();
    preparation.expected_revision = in.wide();
    preparation.checkpoint_revision = in.wide();
    preparation.avatar_generation = in.wide();
    preparation.source_worker = in.wide();
    preparation.source_worker_generation = in.wide();
    preparation.destination_worker = in.wide();
    preparation.destination_worker_generation = in.wide();
    preparation.source = {in.wide(), in.wide(), in.wide(), in.wide()};
    preparation.destination = {in.wide(), in.wide(), in.wide(), in.wide()};
    preparation.universe = in.digest();
    preparation.checkpoint_hash = in.digest();
    return preparation;
}

// The hash of a preparation's durable encoding, which a transfer's command and receipts carry.
bool preparation_digest(const TransferPreparation& preparation, Digest& digest) {
    state::Writer bytes;
    write_preparation(bytes, preparation);
    return content_digest(bytes.bytes.data(), bytes.bytes.size(), digest);
}

void write_transfer_frame(state::Writer& out, const TransferFrame& frame) {
    out.word(frame.id ? 1 : 0);
    if (!frame.id) {
        return;
    }
    TransferWire wire{};
    encode_transfer(frame, wire);
    out.data({wire.begin(), wire.end()});
}

TransferFrame read_transfer_frame(state::Reader& in) {
    TransferFrame frame;
    const auto present = in.word();
    if (present > 1) {
        in.valid = false;
        return frame;
    }
    if (!present) {
        return frame;
    }
    const auto bytes = in.data(transfer_wire_bytes);
    TransferWire wire{};
    if (bytes.size() != wire.size()) {
        in.valid = false;
        return frame;
    }
    std::copy(bytes.begin(), bytes.end(), wire.begin());
    if (!decode_transfer(wire, frame)) {
        in.valid = false;
    }
    return frame;
}

bool closed_transfer_artifact(const std::vector<uint8_t>& artifact, const std::vector<uint8_t>& approval,
                              const TransferFrame& receipt, Digest& approval_hash, std::string& error) {
    auto fail = [&]() {
        error = "transfer_artifact_or_approval_mismatch";
        return false;
    };
    if (artifact.empty() || artifact.size() > max_transfer_artifact_bytes ||
        artifact.size() != receipt.artifact_bytes || approval.size() < 200 ||
        approval.size() > max_transfer_approval_bytes) {
        return fail();
    }
    Digest actual{};
    if (!content_digest(artifact.data(), artifact.size(), actual) || actual != receipt.artifact ||
        !content_digest(approval.data(), approval.size(), approval_hash)) {
        return fail();
    }
    auto word = [&](size_t at) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            value |= uint32_t(approval[at + i]) << (8 * i);
        }
        return value;
    };
    if (word(0) != 0x4630314d || word(4) != 3 || word(72) != artifact.size() || word(76) != 0x00250003 ||
        word(80) != receipt.source_player || word(84) != receipt.source_avatar || word(88) != receipt.unique_game_id ||
        word(92) != receipt.canonical_home || word(108) > 1 || word(148) > 1024 || word(152) > 1024 ||
        approval.size() != 200 + size_t(word(148)) * 12 + size_t(word(152)) * 8) {
        return fail();
    }
    const std::string expected = hex(actual);
    if (!std::equal(expected.begin(), expected.end(), approval.begin() + 8)) {
        return fail();
    }
    error.clear();
    return true;
}

bool same_secret(const Digest& a, const Digest& b) {
    uint32_t difference = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        difference |= uint32_t(a[i] ^ b[i]);
    }
    return difference == 0;
}

uint64_t player_from_credential(const SessionConfig& config, const Digest& credential) {
    if (same_secret(credential, config.player1)) {
        return 1;
    }
    if (same_secret(credential, config.player2)) {
        return 2;
    }
    return 0;
}

// A flag is stored as a word that is 0 or 1. False for any other word.
bool read_flag(state::Reader& in, bool& flag) {
    const auto word = in.word();
    if (word > 1) {
        return false;
    }
    flag = word != 0;
    return true;
}

// One bit per player whose active avatar is at the location.
uint8_t resident_owner_mask(const LocationLedger& ledger, const LocationId& location) {
    uint8_t mask = 0;
    for (uint64_t player = 1; player <= 2; ++player) {
        const auto* avatar = ledger.avatar(player);
        if (avatar && avatar->active && avatar->location == location) {
            mask |= static_cast<uint8_t>(1u << player);
        }
    }
    return mask;
}

// The content authority is the worker that owns the economy.
bool owns_content(const LocationLedger& ledger, uint64_t worker, uint64_t generation) {
    const auto* owner = ledger.global_owner(GlobalSystem::economy);
    return owner && owner->worker == worker && owner->generation == generation;
}

// Every location session gets an authority credential and a nonzero session epoch of its own.
bool randomize_location_admission(SessionConfig& config) {
    return detail::random_bytes(config.authority.data(), config.authority.size()) &&
           detail::random_bytes(&config.epoch, sizeof(config.epoch)) && config.epoch != 0;
}

// Every global system the old worker owned passes to the new one.
bool rebind_global_owners(LocationLedger& ledger, uint64_t old_worker, uint64_t old_generation, uint64_t new_worker,
                          uint64_t new_generation, std::string& error) {
    for (const auto system : global_systems) {
        const auto* owner = ledger.global_owner(system);
        if (owner && owner->worker == old_worker && owner->generation == old_generation &&
            !ledger.rebind_global(system, *owner, {new_worker, new_generation}, error)) {
            return false;
        }
    }
    return true;
}

void erase_maintenance_starts(std::map<LocationId, uint64_t>& starts, uint64_t transfer) {
    for (auto start = starts.begin(); start != starts.end();) {
        if (start->second == transfer) {
            start = starts.erase(start);
        } else {
            ++start;
        }
    }
}

// Fills the credentials the configuration left empty, requires the three to differ, and draws the session epoch.
bool prepare_session_secrets(SessionConfig& session, std::string& error) {
    Digest zero{};
    for (auto secret : {&session.authority, &session.player1, &session.player2}) {
        if (*secret == zero && !detail::random_bytes(secret->data(), secret->size())) {
            error = "credential_random_failed";
            return false;
        }
    }
    if (session.authority == session.player1 || session.authority == session.player2 ||
        session.player1 == session.player2) {
        error = "credentials_must_be_distinct";
        return false;
    }
    if (!detail::random_bytes(&session.epoch, sizeof(session.epoch)) || !session.epoch) {
        error = "session_random_failed";
        return false;
    }
    return true;
}

uint64_t file_time_value(const FILETIME& value) {
    return (uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}

// The image path of a process, provided its creation time is `creation_time`.
bool process_image_path(HANDLE process, uint64_t creation_time, std::wstring& image) {
    FILETIME created{}, exited{}, kernel{}, user{};
    wchar_t path[32768]{};
    DWORD length = _countof(path);
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user) || file_time_value(created) != creation_time ||
        !QueryFullProcessImageNameW(process, 0, path, &length)) {
        return false;
    }
    image = path;
    return true;
}

// Whether the file's bytes hash to `expected`. An empty file, or one over 128 MiB, never does.
bool file_has_digest(const std::filesystem::path& path, const Digest& expected) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file ? file.tellg() : std::streampos(-1);
    if (size <= 0 || size > 128 * 1024 * 1024) {
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Digest digest{};
    return file && content_digest(bytes.data(), bytes.size(), digest) && digest == expected;
}

// True once the process is observed to have ended; `exit_code` then holds its exit code.
bool process_exited(HANDLE process, DWORD& exit_code) {
    return WaitForSingleObject(process, 0) == WAIT_OBJECT_0 && GetExitCodeProcess(process, &exit_code) != FALSE &&
           exit_code != STILL_ACTIVE;
}
} // namespace

struct Coordinator::Impl {
    struct LocationRuntime {
        SessionConfig config{};
        std::unique_ptr<Session> session;
    };

    struct ReplacementRequest {
        LocationId location{};
        uint64_t old_worker = 0, old_generation = 0, new_worker = 0, new_generation = 0, expected_revision = 0;

        bool matches(const ReplacementRequest& other) const noexcept {
            return location == other.location && old_worker == other.old_worker &&
                   old_generation == other.old_generation && new_worker == other.new_worker &&
                   new_generation == other.new_generation && expected_revision == other.expected_revision;
        }
    };

    SOCKET listener = INVALID_SOCKET;
    CoordinatorConfig config{};
    detail::TlsIdentity identity;
    std::unique_ptr<Session> session;
    std::map<uint64_t, std::unique_ptr<detail::Channel>> channels;
    std::deque<CoordinatorEvent> events;
    uint64_t next_connection = 0;
    std::map<LocationId, LocationRuntime> location_sessions;
    std::map<uint64_t, LocationId> routes;
    bool location_mode = false;
    std::unique_ptr<DurableStore> store;
    Digest universe{};
    Identity base_identity{};
    std::vector<uint8_t> artifact;
    std::vector<uint8_t> checkpoint_approval, checkpoint_proof;
    uint64_t checkpoint_revision = 0;
    uint32_t checkpoint_schema = 0;
    Digest secondary_save{};
    std::optional<ReplacementRequest> replacement_request;

    struct CancelReceipt {
        TransferPreparation preparation;
        uint64_t expected_revision = 0;
    };

    std::map<uint64_t, TransferPreparation> preparations;
    std::optional<CancelReceipt> cancelled_preparation;
    Digest checkpoint_hash{};
    std::array<uint32_t, 2> original_games{};
    std::array<uint32_t, 2> original_homes{};

    struct ProcessIdentity {
        uint32_t pid = 0;
        uint64_t created = 0;
    };

    struct ProcessExit {
        ProcessIdentity process;
        uint64_t worker = 0, generation = 0;
        uint32_t code = 0;
    };

    struct NativeRecord {
        TransferPreparation preparation;
        NativeTransferState state;
        uint32_t destination_slot = 0;
        uint64_t control_expected = 0;
        uint32_t control_op = 0;
        ProcessIdentity source_process, destination_process;
        std::optional<ProcessExit> source_exit, destination_exit;
        std::vector<uint8_t> artifact, approval;
    };

    std::map<uint64_t, NativeRecord> native_transfers;
    std::map<LocationId, uint64_t> maintenance_starts;

    struct LocalProcess {
        HANDLE handle = nullptr;
        uint64_t worker = 0, generation = 0, created = 0;
        uint32_t pid = 0;

        ~LocalProcess() {
            if (handle) {
                CloseHandle(handle);
            }
        }
    };

    std::map<LocationId, std::unique_ptr<LocalProcess>> local_processes;

    struct PreloadConnection {
        Packet request{};
        PreloadFrame granted{};
        uint64_t sequence = 0, nonce = 0, revision = 0;
        Digest checkpoint{};
        std::unique_ptr<LocalProcess> process;
    };

    std::map<uint64_t, PreloadConnection> preloads;
    LocationLedger locations;

    // Connections that are being closed, each with the tick at which it is dropped.
    std::map<uint64_t, uint64_t> closing;

    // One side of a native transfer whose worker died: the lease that worker held, and the worker that takes the
    // location over.
    struct WorkerRecovery {
        uint64_t transfer = 0;
        bool source = false;
        LocationId location{};
        uint64_t old_worker = 0, old_generation = 0, replacement_worker = 0, replacement_generation = 0;
    };

    // What commit_location_checkpoint replaces, kept to be put back when the store refuses the new head.
    struct CheckpointHead {
        SessionConfig session;
        LocationLedger locations;
        std::map<uint64_t, NativeRecord> native_transfers;
        std::vector<uint8_t> artifact, approval, proof;
        uint64_t revision = 0;
        Digest hash{}, secondary_save{};
    };

    bool checkpoint_stale_secondary(const LocationId& location, uint64_t player = 0) const {
        GlobalCheckpointApproval approved;
        if (!decode_global_checkpoint_approval(checkpoint_approval, approved) ||
            approved.candidate != checkpoint_hash || !(approved.location == location) ||
            (player && approved.owners[1] != player)) {
            return false;
        }
        const auto* avatar = locations.avatar(approved.owners[1]);
        return avatar && (!avatar->active || !(avatar->location == location));
    }

    bool preparation_identity(const TransferPreparation& preparation, const LocationLedger& ledger) const noexcept {
        if (!preparation.id || preparation.player < 1 || preparation.player > 2 || !preparation.expected_revision ||
            preparation.expected_revision == UINT64_MAX || !preparation.avatar_generation ||
            !preparation.source.valid() || !preparation.destination.valid() ||
            preparation.source == preparation.destination || preparation.universe != universe ||
            preparation.checkpoint_revision != checkpoint_revision || preparation.checkpoint_hash == Digest{} ||
            preparation.checkpoint_hash != checkpoint_hash) {
            return false;
        }
        const auto source = ledger.location_leases().find(preparation.source),
                   destination = ledger.location_leases().find(preparation.destination);
        const auto* avatar = ledger.avatar(preparation.player);
        return source != ledger.location_leases().end() && destination != ledger.location_leases().end() &&
               source->second.active && destination->second.active &&
               source->second.worker == preparation.source_worker &&
               source->second.generation == preparation.source_worker_generation &&
               destination->second.worker == preparation.destination_worker &&
               destination->second.generation == preparation.destination_worker_generation && avatar &&
               avatar->active && avatar->location == preparation.source &&
               avatar->generation == preparation.avatar_generation;
    }

    bool preparation_record(const TransferPreparation& preparation) const noexcept {
        const auto* transfer = locations.transfer(preparation.id);
        return preparation_identity(preparation, locations) && transfer && transfer->phase == TransferPhase::prepared &&
               transfer->player == preparation.player && transfer->source == preparation.source &&
               transfer->destination == preparation.destination &&
               transfer->source_generation == preparation.avatar_generation;
    }

    // The durable universe record: written whole at every publication.
    std::vector<uint8_t> persisted(const std::vector<uint8_t>& content) const;
    void write_identity_and_credentials(state::Writer& out) const;
    void write_replacement_receipt(state::Writer& out) const;
    void write_preparations(state::Writer& out) const;
    static void write_native_record(state::Writer& out, uint64_t id, const NativeRecord& record);
    void write_native_transfers(state::Writer& out) const;
    void write_maintenance_starts(state::Writer& out) const;

    // The durable universe record: read back and checked at start.
    bool recover(const std::vector<uint8_t>& bytes, CoordinatorConfig& requested, std::vector<uint8_t>& content,
                 std::string& error);
    bool read_identities(state::Reader& in, CoordinatorConfig& requested, Identity& observed, Identity& active,
                         std::string& error);
    bool read_replacement_receipt(state::Reader& in, std::string& error);
    bool read_preparations(state::Reader& in, std::string& error);
    static bool read_process_identities(state::Reader& in, NativeRecord& record);
    static void adopt_older_record_schema(uint32_t schema, NativeRecord& record);
    static bool read_native_record(state::Reader& in, uint32_t schema, NativeRecord& record);
    bool read_native_transfers(state::Reader& in, uint32_t schema, std::string& error);
    bool read_maintenance_starts(state::Reader& in, std::string& error);
    bool read_later_sections(state::Reader& in, uint32_t schema, std::string& error);
    bool restore_checkpoint_identity(uint32_t schema, const Identity& observed, const Identity& active,
                                     std::string& error);
    bool checkpoint_receipt_valid(std::string& error) const;
    bool recovered_transfers_valid(std::string& error) const;

    // Start.
    bool open_universe(CoordinatorConfig& requested, std::vector<uint8_t>& content, bool& recovered,
                       std::string& error);
    bool open_root_session(const CoordinatorConfig& requested, bool recovered, const std::vector<uint8_t>& content,
                           std::string& error);
    void select_maintenance_transfer(const LocationId& location, SessionConfig& session_config) const;
    void open_location_session(const LocationId& location, LocationRuntime& runtime, ContentControl& content);
    void resume_transfer_maintenance(const LocationId& location, LocationRuntime& runtime) const;
    bool start_location(const LocationId& location, const LocationLease& lease, std::string& error);
    bool open_location_sessions(std::string& error);

    // Location replacement and offline checkpoint commit.
    bool location_free_of_native_recovery(const LocationId& location, std::string& error) const;
    bool replaced_location_ledger(const ReplacementRequest& request,
                                  std::map<uint64_t, TransferPreparation>& retained_preparations,
                                  LocationLedger& checked, std::string& error) const;
    bool publish_replacement(const ReplacementRequest& request, LocationLedger& checked,
                             std::map<uint64_t, TransferPreparation>& retained_preparations, std::string& error);
    std::unique_ptr<Session> session_for_world(const WorldIdentity& world, std::string& error) const;
    bool publish_offline_checkpoint(const std::vector<uint8_t>& bytes, const CheckpointArtifact& checkpoint,
                                    uint64_t expected_revision, const Session& candidate, std::string& error,
                                    const std::function<void(const char*)>& test_boundary);

    // The connection pump.
    void stop_stale_preloads();
    void accept_connections();
    void disconnect(uint64_t connection);
    void reject_and_close(uint64_t connection, detail::Channel& channel, Error reason, uint64_t sequence);
    bool preload_credential_valid(const Packet& packet, const LocationRuntime& runtime, const LocationLease& lease,
                                  bool first) const;
    bool preload_repeat_valid(const PreloadConnection& binding, const Packet& packet) const;
    bool bind_preload_process(uint64_t connection, const Packet& packet, const LocationLease& lease);
    bool grant_preload(uint64_t connection, const Packet& packet, const LocationLease& lease);
    bool preload(uint64_t connection, const Packet& packet);
    bool receive_preload_packet(uint64_t connection, detail::Channel& channel, const Packet& packet);
    Session* admit_to_location(uint64_t connection, detail::Channel& channel, const Packet& packet);
    void route_packet(uint64_t connection, detail::Channel& channel, const Packet& packet);
    void pump_channel(uint64_t connection, detail::Channel& channel, std::vector<uint64_t>& closed);

    // Native transfers (coordinator_transfer.inc): what a durable record must satisfy.
    bool native_record_valid(uint64_t id, const NativeRecord& record) const;
    bool native_record_identity_valid(uint64_t id, const NativeRecord& record) const;
    bool native_record_workers_valid(const NativeRecord& record, bool terminal) const;
    static bool native_record_processes_valid(const NativeRecord& record);
    bool source_receipt_valid(uint64_t id, const NativeRecord& record, const Digest& digest) const;
    bool native_record_ledger_valid(uint64_t id, const NativeRecord& record, const Digest& digest,
                                    const AvatarLease* avatar, const Transfer* transfer, bool terminal) const;
    bool native_record_artifact_valid(uint64_t id, const NativeRecord& record, bool terminal) const;
    bool destination_receipt_valid(uint64_t id, const NativeRecord& record, const Digest& digest) const;
    bool cleanup_receipt_valid(uint64_t id, const NativeRecord& record, const Digest& digest) const;
    bool native_record_receipts_valid(uint64_t id, const NativeRecord& record, const Digest& digest) const;

    // Native transfers: the control operations and the receipts that answer them.
    bool publish_native(const LocationLedger&, const std::map<uint64_t, NativeRecord>&, std::string&);
    uint64_t recovery_artifact_transfer(uint64_t player) const noexcept;
    bool pinned_process_differs(const LocationId& location, uint64_t worker, uint64_t generation, uint32_t process_id,
                                uint64_t creation_time) const;
    bool register_local_worker_process(const LocationId& location, uint64_t worker, uint64_t generation,
                                       uint32_t process_id, uint64_t creation_time, std::string& error);
    bool overlapping_preparation(uint64_t id, const TransferPreparation& preparation) const;
    bool new_native_record(uint64_t id, uint64_t expected_revision, const TransferPreparation& preparation,
                           uint32_t source_slot, uint32_t destination_slot, const Session& source, const Entity& actor,
                           NativeRecord& record, std::string& error) const;
    bool previous_worker_retired(const LocationId& destination, const LocationLease& lease) const;
    bool publish_maintenance_start(const LocationLedger& candidate, const TransferPreparation& preparation,
                                   std::string& error);
    static bool accept_source_fence(const Packet& packet, NativeRecord& record, LocationLedger& candidate);
    static bool accept_destination_restore(const Packet& packet, NativeRecord& record, LocationLedger& candidate);
    static bool accept_source_cleanup(const Packet& packet, NativeRecord& record);
    static bool apply_receipt(const Packet& packet, NativeRecord& record, LocationLedger& candidate);
    bool native_receipt(const LocationId&, const Packet&);
    static bool request_phase(TransferOp op, NativeTransferState& state, bool& destination);
    bool command_target_ready(const LocationId& location, uint64_t id, const Session& target,
                              ProcessIdentity& pinned) const;
    static bool request_command(TransferOp op, uint64_t revision, bool destination, const NativeRecord& record,
                                TransferFrame& command);
    bool observe_source_exit(NativeRecord& record, std::string& error) const;
    static bool vacate_empty_source(LocationLedger& candidate, const NativeRecord& record, std::string& error);

    // Native transfers: replacing a worker that died during one.
    bool worker_recovery_admitted(const WorkerRecovery& recovery, uint64_t expected_revision, const NativeRecord& old,
                                  const LocationLease* lease, bool terminal, bool saved_secondary, DWORD& exit_code,
                                  std::string& error) const;
    static bool cancel_later_reservations(const WorkerRecovery& recovery, uint64_t player, LocationLedger& candidate,
                                          std::map<uint64_t, TransferPreparation>& pending_preparations,
                                          std::map<LocationId, uint64_t>& pending_starts, std::string& error);
    static bool abandon_later_attempts(const WorkerRecovery& recovery, std::map<uint64_t, NativeRecord>& records,
                                       LocationLedger& candidate,
                                       std::map<uint64_t, TransferPreparation>& pending_preparations,
                                       std::map<LocationId, uint64_t>& pending_starts, std::string& error);
    bool replace_worker_in_ledger(const WorkerRecovery& recovery, bool checkpoint_only, bool lease_active,
                                  LocationLedger& candidate, std::string& error) const;
    static bool phase_after_recovery(NativeTransferState& state, bool source, bool checkpoint_only,
                                     bool saved_secondary, DWORD exit_code);
    bool install_worker_recovery(const WorkerRecovery& recovery, bool maintenance, uint32_t native_load_slot,
                                 const LocationLedger& candidate, const std::map<uint64_t, NativeRecord>& records,
                                 const std::map<uint64_t, TransferPreparation>& pending_preparations,
                                 const std::map<LocationId, uint64_t>& pending_starts, std::string& error);

    // The location checkpoint (coordinator_global_checkpoint.inc).
    bool decode_checkpoint_request(const std::vector<uint8_t>& candidate_artifact, const std::vector<uint8_t>& approval,
                                   const std::vector<uint8_t>& proof, uint64_t expected_revision,
                                   GlobalCheckpointApproval& approved, CheckpointArtifact& checkpoint,
                                   std::string& error) const;
    bool location_checkpoint_admitted(const GlobalCheckpointApproval& approved, const CheckpointArtifact& checkpoint,
                                      uint64_t expected_revision, std::string& error) const;
    bool supersede_player_heads(const GlobalCheckpointApproval& approved, uint64_t revision,
                                std::map<uint64_t, NativeRecord>& records, std::string& error) const;
    bool nonowner_members_kept(const std::vector<uint8_t>& candidate_artifact, std::string& error) const;
    bool rotated_ledger(const GlobalCheckpointApproval& approved, uint64_t revision, LocationLedger& ledger,
                        LocationLedger& checked, std::string& error) const;
    bool checkpoint_runtimes(const LocationLedger& ledger, const Identity& checkpoint_identity, uint64_t revision,
                             std::map<LocationId, LocationRuntime>& runtimes, std::string& error) const;

    CheckpointHead checkpoint_head() const {
        return {config.session,   locations,           native_transfers, artifact,      checkpoint_approval,
                checkpoint_proof, checkpoint_revision, checkpoint_hash,  secondary_save};
    }

    void restore_checkpoint_head(CheckpointHead&& head) {
        config.session = head.session;
        locations = std::move(head.locations);
        native_transfers = std::move(head.native_transfers);
        artifact = std::move(head.artifact);
        checkpoint_approval = std::move(head.approval);
        checkpoint_proof = std::move(head.proof);
        checkpoint_revision = head.revision;
        checkpoint_hash = head.hash;
        secondary_save = head.secondary_save;
    }

    bool process_matches(const LocationId& location, const TransferFrame& receipt) const {
        const auto found = local_processes.find(location);
        return found != local_processes.end() && found->second->pid == receipt.process_id &&
               found->second->created == receipt.process_created && found->second->worker == receipt.worker &&
               found->second->generation == receipt.worker_generation &&
               WaitForSingleObject(found->second->handle, 0) == WAIT_TIMEOUT;
    }

    Session* routed_session(uint64_t connection) {
        auto route = routes.find(connection);
        if (route == routes.end()) {
            return nullptr;
        }
        if (!route->second.valid()) {
            return location_mode ? nullptr : session.get();
        }
        auto location = location_sessions.find(route->second);
        return location == location_sessions.end() ? nullptr : location->second.session.get();
    }

    // Stop transports immediately instead of flushing a rejection
    // behind queued gameplay from the superseded lease. Packets that
    // already reached the native peer still require native fencing.
    void close_routes_to(const LocationId& location) {
        for (auto route = routes.begin(); route != routes.end();) {
            if (route->second == location) {
                const auto channel = channels.find(route->first);
                if (channel != channels.end()) {
                    channel->second->request_stop();
                }
                closing[route->first] = 0;
                route = routes.erase(route);
            } else {
                ++route;
            }
        }
    }

    void fence_stale_locations() {
        for (auto& entry : location_sessions) {
            if (entry.second.session->lease_current()) {
                continue;
            }
            entry.second.session->fence_lease();
            close_routes_to(entry.first);
        }
    }

    void dispatch(const std::vector<Dispatch>& messages) {
        for (const auto& message : messages) {
            auto channel = channels.find(message.connection);
            if (channel == channels.end()) {
                continue;
            }
            Packet packet = message.packet;
            if (message.recipient_scope) {
                auto* target = routed_session(message.connection);
                if (!target || !target->recipient_scope(message.connection, packet)) {
                    continue;
                }
            }
            const bool queued = channel->second->send(packet);
            // Record action/content decisions, not a second copy of every scene packet.
            // Queueing is not a native application or a delivery acknowledgment.
            if ((packet.kind == Kind::action || packet.kind == Kind::action_result || packet.kind == Kind::reject ||
                 packet.kind == Kind::content_response) &&
                events.size() < queue_capacity) {
                events.push_back({message.connection,
                                  {EventKind::packet, packet, queued ? "dispatch_queued" : "dispatch_queue_full"},
                                  true});
            }
            // Rejection is delivered before close; application deletes after a short flush below.
            if (message.close) {
                closing[message.connection] = GetTickCount64() + rejection_flush_ms;
            }
        }
    }
};

void Coordinator::Impl::write_identity_and_credentials(state::Writer& out) const {
    const auto& session_config = config.session;
    for (const auto* digest :
         {&base_identity.build, &base_identity.executable, &base_identity.content, &base_identity.fixture}) {
        out.digest(*digest);
    }
    for (const auto& digest : base_identity.world) {
        out.digest(digest);
    }
    for (const auto& digest : session_config.identity.world) {
        out.digest(digest);
    }
    for (const auto* digest : {&session_config.authority, &session_config.player1, &session_config.player2}) {
        out.digest(*digest);
    }
}

void Coordinator::Impl::write_replacement_receipt(state::Writer& out) const {
    // A receipt applies only to the publication performing that request.
    // Any later content or offline ledger publication retires it.
    const bool receipt = replacement_request && store && replacement_request->expected_revision == store->revision();
    out.word(receipt ? 1 : 0);
    if (receipt) {
        const auto& request = *replacement_request;
        for (auto value : {request.location.galaxy, request.location.star, request.location.planet,
                           request.location.area, request.old_worker, request.old_generation, request.new_worker,
                           request.new_generation, request.expected_revision}) {
            out.wide(value);
        }
    }
}

// Every pending preparation, then the receipt of a cancellation, written only by the publication that performs it.
void Coordinator::Impl::write_preparations(state::Writer& out) const {
    out.word(static_cast<uint32_t>(preparations.size()));
    for (const auto& entry : preparations) {
        write_preparation(out, entry.second);
    }
    const bool cancelled =
        cancelled_preparation && store && cancelled_preparation->expected_revision == store->revision();
    out.word(cancelled ? 1 : 0);
    if (cancelled) {
        write_preparation(out, cancelled_preparation->preparation);
        out.wide(cancelled_preparation->expected_revision);
    }
}

void Coordinator::Impl::write_native_record(state::Writer& out, uint64_t id, const NativeRecord& record) {
    const auto& state = record.state;
    out.wide(id);
    write_preparation(out, record.preparation);
    out.word(uint32_t(state.phase));
    out.word(record.destination_slot);
    for (auto value : {state.command_session, state.command_scene, state.command_baseline, state.source_worker,
                       state.source_generation, state.destination_worker, state.destination_generation,
                       state.source_session, state.source_scene, state.source_baseline, state.destination_session,
                       state.destination_scene, state.destination_baseline}) {
        out.wide(value);
    }
    out.word(state.source_retired ? 1 : 0);
    out.word(state.source_exit_code);
    out.wide(state.avatar_generation);
    out.word(state.ownership_committed ? 1 : 0);
    out.wide(record.control_expected);
    out.word(record.control_op);
    out.word(state.source_rollback_committed ? 1 : 0);
    for (const auto* process : {&record.source_process, &record.destination_process}) {
        out.word(process->pid);
        out.wide(process->created);
    }
    for (const auto* exited : {&record.source_exit, &record.destination_exit}) {
        out.word(*exited ? 1 : 0);
        if (*exited) {
            out.word((*exited)->process.pid);
            out.wide((*exited)->process.created);
            out.wide((*exited)->worker);
            out.wide((*exited)->generation);
            out.word((*exited)->code);
        }
    }
    out.word(state.completed_once ? 1 : 0);
    for (auto value : {state.cleanup_session, state.cleanup_scene, state.cleanup_baseline}) {
        out.wide(value);
    }
    write_transfer_frame(out, state.cleanup_receipt);
    out.wide(state.artifact_checkpoint);
    for (const auto* frame : {&state.command, &state.source_receipt, &state.destination_receipt, &state.last_receipt}) {
        write_transfer_frame(out, *frame);
    }
    out.data(record.artifact);
    out.data(record.approval);
}

void Coordinator::Impl::write_native_transfers(state::Writer& out) const {
    for (auto value : original_games) {
        out.word(value);
    }
    for (auto value : original_homes) {
        out.word(value);
    }
    out.word(static_cast<uint32_t>(native_transfers.size()));
    for (const auto& entry : native_transfers) {
        write_native_record(out, entry.first, entry.second);
    }
}

void Coordinator::Impl::write_maintenance_starts(state::Writer& out) const {
    out.word(static_cast<uint32_t>(maintenance_starts.size()));
    for (const auto& entry : maintenance_starts) {
        for (auto value : {entry.first.galaxy, entry.first.star, entry.first.planet, entry.first.area, entry.second}) {
            out.wide(value);
        }
    }
}

std::vector<uint8_t> Coordinator::Impl::persisted(const std::vector<uint8_t>& content) const {
    state::Writer out;
    out.word(universe_state_magic);
    out.word(universe_state_schema);
    out.digest(universe);
    write_identity_and_credentials(out);
    out.data(identity.snapshot());
    out.data(content);
    out.wide(checkpoint_revision);
    out.data(artifact);
    out.data(locations.snapshot());
    write_replacement_receipt(out);
    write_preparations(out);
    write_native_transfers(out);
    write_maintenance_starts(out);
    out.data(checkpoint_approval);
    out.data(checkpoint_proof);
    return std::move(out.bytes);
}

// The universe, the identity it was created with and the one now active, and the credentials. The requested
// configuration must be one of those two identities and continues as the active one.
bool Coordinator::Impl::read_identities(state::Reader& in, CoordinatorConfig& requested, Identity& observed,
                                        Identity& active, std::string& error) {
    universe = in.digest();
    for (auto* digest : {&observed.build, &observed.executable, &observed.content, &observed.fixture}) {
        *digest = in.digest();
    }
    for (auto& digest : observed.world) {
        digest = in.digest();
    }
    base_identity = observed;
    active = observed;
    for (auto& digest : active.world) {
        digest = in.digest();
    }
    active.fixture = active.world[0];
    if (universe == Digest{} || !valid_world_identity(active.world) ||
        (!(observed == requested.session.identity) && !(active == requested.session.identity))) {
        error = "universe_compatibility_mismatch";
        return false;
    }
    requested.session.identity = active;
    for (auto* digest : {&requested.session.authority, &requested.session.player1, &requested.session.player2}) {
        *digest = in.digest();
    }
    return true;
}

bool Coordinator::Impl::read_replacement_receipt(state::Reader& in, std::string& error) {
    const auto receipt = in.word();
    if (receipt > 1) {
        error = "location_replacement_receipt_invalid";
        return false;
    }
    if (receipt) {
        ReplacementRequest request;
        request.location = {in.wide(), in.wide(), in.wide(), in.wide()};
        request.old_worker = in.wide();
        request.old_generation = in.wide();
        request.new_worker = in.wide();
        request.new_generation = in.wide();
        request.expected_revision = in.wide();
        const auto location = locations.location_leases().find(request.location);
        if (!request.location.valid() || !request.old_worker || !request.old_generation || !request.new_worker ||
            request.new_generation <= request.old_generation || !request.expected_revision || !store->revision() ||
            request.expected_revision != store->revision() - 1 || location == locations.location_leases().end() ||
            !location->second.active || location->second.worker != request.new_worker ||
            location->second.generation != request.new_generation) {
            error = "location_replacement_receipt_invalid";
            return false;
        }
        replacement_request = request;
    }
    return true;
}

bool Coordinator::Impl::read_preparations(state::Reader& in, std::string& error) {
    const auto count = in.word();
    if (count > 2) {
        error = "transfer_preparation_record_invalid";
        return false;
    }
    for (uint32_t n = 0; n < count; ++n) {
        auto preparation = read_preparation(in);
        if (!preparations.emplace(preparation.id, preparation).second) {
            error = "transfer_preparation_record_invalid";
            return false;
        }
    }
    const auto cancelled = in.word();
    if (cancelled > 1) {
        error = "transfer_preparation_record_invalid";
        return false;
    }
    if (cancelled) {
        auto preparation = read_preparation(in);
        cancelled_preparation = CancelReceipt{preparation, in.wide()};
    }
    return true;
}

// Schema 7 added the source rollback flag, the two pinned processes and the two observed process exits.
bool Coordinator::Impl::read_process_identities(state::Reader& in, NativeRecord& record) {
    if (!read_flag(in, record.state.source_rollback_committed)) {
        return false;
    }
    for (auto* process : {&record.source_process, &record.destination_process}) {
        process->pid = in.word();
        process->created = in.wide();
    }
    for (auto* exited : {&record.source_exit, &record.destination_exit}) {
        bool present = false;
        if (!read_flag(in, present)) {
            return false;
        }
        if (present) {
            ProcessExit value;
            value.process.pid = in.word();
            value.process.created = in.wide();
            value.worker = in.wide();
            value.generation = in.wide();
            value.code = in.word();
            *exited = value;
        }
    }
    return true;
}

// A record written before schema 8 or 7 gets the fields those schemas added from what it already states.
void Coordinator::Impl::adopt_older_record_schema(uint32_t schema, NativeRecord& record) {
    auto& state = record.state;
    if (schema < 8) {
        state.completed_once = state.phase == NativeTransferPhase::complete;
        if (state.last_receipt.op == TransferOp::source_cleanup && state.last_receipt.id) {
            state.cleanup_receipt = state.last_receipt;
            state.cleanup_session = state.command_session;
            state.cleanup_scene = state.command_scene;
            state.cleanup_baseline = state.command_baseline;
        }
    }
    if (schema == 6) {
        state.source_rollback_committed = state.phase == NativeTransferPhase::source_activate_requested ||
                                          state.phase == NativeTransferPhase::aborted;
        record.source_process = {state.source_receipt.process_id, state.source_receipt.process_created};
        record.destination_process = {state.destination_receipt.process_id, state.destination_receipt.process_created};
    }
}

bool Coordinator::Impl::read_native_record(state::Reader& in, uint32_t schema, NativeRecord& record) {
    auto& state = record.state;
    record.preparation = read_preparation(in);
    state.phase = NativeTransferPhase(in.word());
    record.destination_slot = in.word();
    for (auto* value : {&state.command_session, &state.command_scene, &state.command_baseline, &state.source_worker,
                        &state.source_generation, &state.destination_worker, &state.destination_generation,
                        &state.source_session, &state.source_scene, &state.source_baseline, &state.destination_session,
                        &state.destination_scene, &state.destination_baseline}) {
        *value = in.wide();
    }
    if (!read_flag(in, state.source_retired)) {
        return false;
    }
    state.source_exit_code = in.word();
    state.avatar_generation = in.wide();
    if (!read_flag(in, state.ownership_committed)) {
        return false;
    }
    record.control_expected = in.wide();
    record.control_op = in.word();
    if (schema >= 7 && !read_process_identities(in, record)) {
        return false;
    }
    if (schema >= 8) {
        if (!read_flag(in, state.completed_once)) {
            return false;
        }
        for (auto* scope : {&state.cleanup_session, &state.cleanup_scene, &state.cleanup_baseline}) {
            *scope = in.wide();
        }
        state.cleanup_receipt = read_transfer_frame(in);
    }
    if (schema >= 9) {
        state.artifact_checkpoint = in.wide();
    }
    for (auto* frame : {&state.command, &state.source_receipt, &state.destination_receipt, &state.last_receipt}) {
        *frame = read_transfer_frame(in);
    }
    adopt_older_record_schema(schema, record);
    record.artifact = in.data(max_transfer_artifact_bytes);
    record.approval = in.data(max_transfer_approval_bytes);
    return true;
}

bool Coordinator::Impl::read_native_transfers(state::Reader& in, uint32_t schema, std::string& error) {
    for (auto& value : original_games) {
        value = in.word();
    }
    for (auto& value : original_homes) {
        value = in.word();
    }
    const auto count = in.word();
    if (count > (schema >= 8 ? 4u : 2u)) {
        error = "native_transfer_record_invalid";
        return false;
    }
    for (uint32_t n = 0; n < count; ++n) {
        const auto id = in.wide();
        NativeRecord record;
        if (!read_native_record(in, schema, record) || !id || !native_transfers.emplace(id, std::move(record)).second) {
            error = "native_transfer_record_invalid";
            return false;
        }
    }
    return read_maintenance_starts(in, error);
}

bool Coordinator::Impl::read_maintenance_starts(state::Reader& in, std::string& error) {
    const auto starts = in.word();
    if (starts > 2) {
        error = "transfer_maintenance_record_invalid";
        return false;
    }
    for (uint32_t n = 0; n < starts; ++n) {
        const LocationId location{in.wide(), in.wide(), in.wide(), in.wide()};
        const auto id = in.wide();
        const auto preparation = preparations.find(id);
        if (!location.valid() || !id || preparation == preparations.end() ||
            (!(preparation->second.destination == location) && !(preparation->second.source == location)) ||
            !maintenance_starts.emplace(location, id).second) {
            error = "transfer_maintenance_record_invalid";
            return false;
        }
    }
    return true;
}

// The selected checkpoint artifact must carry the active identity. A universe without one has no checkpoint
// revision and still has the identity it was created with.
bool Coordinator::Impl::restore_checkpoint_identity(uint32_t schema, const Identity& observed, const Identity& active,
                                                    std::string& error) {
    if (artifact.empty()) {
        if (checkpoint_revision || !(observed == active)) {
            error = "universe_checkpoint_missing";
            return false;
        }
    } else {
        CheckpointArtifact checkpoint;
        if (!checkpoint_revision || checkpoint_revision > store->revision() ||
            !validate_checkpoint_artifact(artifact, checkpoint, error) || !(checkpoint.identity == active)) {
            if (error.empty()) {
                error = "universe_checkpoint_identity";
            }
            return false;
        }
        checkpoint_schema = checkpoint.schema;
        secondary_save = checkpoint.secondary_save;
        checkpoint_hash = checkpoint.digest;
        if (checkpoint.schema == 2) {
            if (schema >= 6 &&
                original_games != std::array<uint32_t, 2>{checkpoint.native_ids[0], checkpoint.native_ids[1]}) {
                error = "native_transfer_origin_invalid";
                return false;
            }
            original_games = {checkpoint.native_ids[0], checkpoint.native_ids[1]};
        }
    }
    return true;
}

// The approval and proof published with a location checkpoint must still describe the selected artifact.
bool Coordinator::Impl::checkpoint_receipt_valid(std::string& error) const {
    GlobalCheckpointApproval approval;
    Digest proof_hash{};
    if (!decode_global_checkpoint_approval(checkpoint_approval, approval) || checkpoint_proof.empty() ||
        !content_digest(checkpoint_proof.data(), checkpoint_proof.size(), proof_hash) || proof_hash != approval.proof ||
        approval.universe != universe || approval.candidate != checkpoint_hash ||
        approval.expected + 1 != checkpoint_revision || approval.games != original_games) {
        error = "global_checkpoint_receipt_invalid";
        return false;
    }
    CheckpointArtifact selected;
    if (!validate_checkpoint_artifact(artifact, selected, error) || selected.provenance != approval.provenance) {
        error = "global_checkpoint_receipt_invalid";
        return false;
    }
    return true;
}

bool Coordinator::Impl::recovered_transfers_valid(std::string& error) const {
    for (const auto& entry : preparations) {
        const auto& preparation = entry.second;
        if (!store->revision() || preparation.expected_revision >= store->revision() ||
            (!native_transfers.count(entry.first) && !preparation_record(preparation))) {
            error = "transfer_preparation_record_invalid";
            return false;
        }
    }
    for (const auto& entry : native_transfers) {
        if (!native_record_valid(entry.first, entry.second)) {
            error = "native_transfer_record_invalid";
            return false;
        }
    }
    if (cancelled_preparation) {
        const auto& cancelled = *cancelled_preparation;
        const auto& preparation = cancelled.preparation;
        if (!store->revision() || !cancelled.expected_revision ||
            cancelled.expected_revision != store->revision() - 1 ||
            preparation.expected_revision >= cancelled.expected_revision || locations.transfer(preparation.id) ||
            !preparation_identity(preparation, locations)) {
            error = "transfer_preparation_record_invalid";
            return false;
        }
    }
    return true;
}

// The sections that schemas 4 to 9 added. A record written before a section's schema leaves that state empty.
bool Coordinator::Impl::read_later_sections(state::Reader& in, uint32_t schema, std::string& error) {
    replacement_request.reset();
    if (schema >= 4 && !read_replacement_receipt(in, error)) {
        return false;
    }
    preparations.clear();
    cancelled_preparation.reset();
    if (schema >= 5 && !read_preparations(in, error)) {
        return false;
    }
    native_transfers.clear();
    original_games = {};
    original_homes = {};
    if (schema >= 6 && !read_native_transfers(in, schema, error)) {
        return false;
    }
    checkpoint_approval.clear();
    checkpoint_proof.clear();
    if (schema >= 9) {
        checkpoint_approval = in.data(global_checkpoint_approval_bytes);
        checkpoint_proof = in.data(max_checkpoint_proof_bytes);
    }
    return true;
}

bool Coordinator::Impl::recover(const std::vector<uint8_t>& bytes, CoordinatorConfig& requested,
                                std::vector<uint8_t>& content, std::string& error) {
    state::Reader in{bytes};
    const auto magic = in.word(), schema = in.word();
    if (magic != universe_state_magic || (schema < 2 || schema > universe_state_schema)) {
        error = "universe_schema_migration_required";
        return false;
    }
    Identity observed, active;
    if (!read_identities(in, requested, observed, active, error)) {
        return false;
    }
    const auto tls = in.data(128 * 1024);
    content = in.data(ContentRegistry::max_cached_bytes + 256 * 1024);
    checkpoint_revision = in.wide();
    artifact = in.data(CheckpointArtifact::max_bytes);
    if (schema >= 3 && !locations.restore(in.data(64 * 1024), error)) {
        return false;
    }
    if (!read_later_sections(in, schema, error)) {
        return false;
    }
    if (!in.done() || requested.session.authority == Digest{} || requested.session.player1 == Digest{} ||
        requested.session.player2 == Digest{}) {
        error = "universe_state_invalid";
        return false;
    }
    if (!restore_checkpoint_identity(schema, observed, active, error)) {
        return false;
    }
    if (schema < 6) {
        for (size_t player = 1; player <= 2; ++player) {
            const auto* avatar = locations.avatar(player);
            if (avatar && avatar->location.planet <= UINT32_MAX) {
                original_homes[player - 1] = uint32_t(avatar->location.planet);
            }
        }
    }
    if ((!checkpoint_approval.empty() || !checkpoint_proof.empty()) && !checkpoint_receipt_valid(error)) {
        return false;
    }
    if (!recovered_transfers_valid(error)) {
        return false;
    }
    if (!locations.checkpoints_share(checkpoint_revision)) {
        error = "location_checkpoint_not_selected";
        return false;
    }
    if (!locations.content_matches(requested.session.identity.content)) {
        error = "location_content_mismatch";
        return false;
    }
    requested.session.checkpoint_revision = checkpoint_revision;
    return identity.restore(tls, error);
}

#include "coordinator_transfer.inc"
#include "coordinator_global_checkpoint.inc"

Coordinator::Coordinator() : impl_(std::make_unique<Impl>()) {}

Coordinator::~Coordinator() {
    stop();
}

// Opens the durable store when a state directory is configured and recovers the universe it holds. Without a
// stored universe an online coordinator creates one.
bool Coordinator::Impl::open_universe(CoordinatorConfig& requested, std::vector<uint8_t>& content, bool& recovered,
                                      std::string& error) {
    std::vector<uint8_t> restored;
    if (!requested.state_directory.empty()) {
        store = std::make_unique<DurableStore>();
        if (!store->open(requested.state_directory, restored, error)) {
            return false;
        }
    }
    recovered = store && store->revision() != 0;
    if (recovered) {
        if (!recover(restored, requested, content, error)) {
            return false;
        }
    } else {
        if (requested.offline) {
            error = "offline_checkpoint_requires_existing_universe";
            return false;
        }
        if (!identity.create(error) || !detail::random_bytes(universe.data(), universe.size())) {
            return false;
        }
        base_identity = requested.session.identity;
    }
    return true;
}

// The root session holds the content. A new universe is published before the session can change it.
bool Coordinator::Impl::open_root_session(const CoordinatorConfig& requested, bool recovered,
                                          const std::vector<uint8_t>& content, std::string& error) {
    config = requested;
    session = std::make_unique<Session>(requested.session);
    if (recovered && !session->restore_content(content, error)) {
        session.reset();
        return false;
    }
    if (store) {
        // Retain before attempting publication: an uncertain flush must never
        // delete the key of a possibly committed universe.
        identity.retained = true;
        if (!recovered && !store->commit(persisted(session->content_snapshot()), error)) {
            session.reset();
            return false;
        }
        session->content_persistence([this](const std::vector<uint8_t>& content_state, std::string& commit_error) {
            return store->commit(persisted(content_state), commit_error);
        });
    }
    return true;
}

// The transfer a location's authority starts in maintenance for: the location's durable maintenance start,
// otherwise the recovery artifact transfer of a player whose avatar is there.
void Coordinator::Impl::select_maintenance_transfer(const LocationId& location, SessionConfig& session_config) const {
    const auto maintenance = maintenance_starts.find(location);
    if (maintenance != maintenance_starts.end()) {
        session_config.maintenance_transfer = maintenance->second;
    }
    if (!session_config.maintenance_transfer) {
        for (uint64_t player = 1; player <= 2; ++player) {
            const auto* avatar = locations.avatar(player);
            const auto head = recovery_artifact_transfer(player);
            if (avatar && avatar->location == location && head) {
                session_config.maintenance_transfer = head;
            }
        }
    }
}

void Coordinator::Impl::open_location_session(const LocationId& location, LocationRuntime& runtime,
                                              ContentControl& content) {
    runtime.session = std::make_unique<Session>(runtime.config, &content, &locations);
    runtime.session->transfer_receipts(
        [this, location](const Packet& packet) { return native_receipt(location, packet); });
}

// A location that is the source or destination of an unfinished native transfer resumes in its maintenance.
void Coordinator::Impl::resume_transfer_maintenance(const LocationId& location, LocationRuntime& runtime) const {
    for (const auto& transfer : native_transfers) {
        const auto& record = transfer.second;
        const auto& state = record.state;
        if (terminal_native_phase(state.phase)) {
            continue;
        }
        if (location == record.preparation.source || location == record.preparation.destination) {
            auto owners = runtime.config.permitted_owners;
            if (location == record.preparation.source || state.destination_receipt.id) {
                owners |= static_cast<uint8_t>(1u << record.preparation.player);
            }
            runtime.session->begin_transfer_maintenance(transfer.first, owners);
        }
    }
}

bool Coordinator::Impl::start_location(const LocationId& location, const LocationLease& lease, std::string& error) {
    LocationRuntime runtime;
    runtime.config = config.session;
    runtime.config.location = location;
    select_maintenance_transfer(location, runtime.config);
    runtime.config.worker_generation = lease.generation;
    runtime.config.checkpoint_revision = checkpoint_revision;
    runtime.config.native_load_slot = lease.native_load_slot;
    runtime.config.content_authority = owns_content(locations, lease.worker, lease.generation);
    runtime.config.permitted_owners = resident_owner_mask(locations, location);
    if (!randomize_location_admission(runtime.config)) {
        error = "location_session_random_failed";
        return false;
    }
    open_location_session(location, runtime, session->shared_content());
    resume_transfer_maintenance(location, runtime);
    if (!runtime.session->lease_current()) {
        error = "location_session_lease_mismatch";
        return false;
    }
    location_sessions.emplace(location, std::move(runtime));
    return true;
}

// One session per active location. They need a content owner that the ledger still accepts.
bool Coordinator::Impl::open_location_sessions(std::string& error) {
    const auto* content_owner = locations.global_owner(GlobalSystem::economy);
    if (!content_owner ||
        !locations.accepts_global_outcome(GlobalSystem::economy, content_owner->worker, content_owner->generation)) {
        error = "location_content_owner_missing_or_inactive";
        return false;
    }
    for (const auto& entry : locations.location_leases()) {
        if (entry.second.active && !start_location(entry.first, entry.second, error)) {
            return false;
        }
    }
    return true;
}

bool Coordinator::start(CoordinatorConfig config, std::string& error) {
    auto& impl = *impl_;
    if (impl.session || impl.identity.acquired || impl.store) {
        error = "coordinator_already_started";
        return false;
    }
    if (!valid_world_identity(config.session.identity.world)) {
        error = "missing_world_identity";
        return false;
    }
    if (!detail::initialize_sockets(error)) {
        return false;
    }
    std::vector<uint8_t> content;
    bool recovered = false;
    if (!impl.open_universe(config, content, recovered, error) || !prepare_session_secrets(config.session, error) ||
        !impl.open_root_session(config, recovered, content, error)) {
        return false;
    }
    impl.location_mode = impl.locations.location_count() != 0;
    if (impl.location_mode && !config.offline && !impl.open_location_sessions(error)) {
        impl.location_sessions.clear();
        impl.session.reset();
        return false;
    }
    if (config.offline) {
        return true;
    }
    impl.listener = detail::listen_socket(config.bind_host, config.port, config.port, error);
    if (impl.listener == INVALID_SOCKET) {
        impl.session.reset();
        return false;
    }
    impl.config.port = config.port;
    return true;
}

uint64_t Coordinator::durable_revision() const noexcept {
    return impl_->store ? impl_->store->revision() : 0;
}

Digest Coordinator::universe() const noexcept {
    return impl_->universe;
}

uint64_t Coordinator::checkpoint_revision() const noexcept {
    return impl_->checkpoint_revision;
}

const std::vector<uint8_t>& Coordinator::checkpoint_artifact() const noexcept {
    return impl_->artifact;
}

const LocationLedger& Coordinator::locations() const noexcept {
    return impl_->locations;
}

std::vector<LocationId> Coordinator::location_ids() const {
    std::vector<LocationId> out;
    for (const auto& entry : impl_->location_sessions) {
        out.push_back(entry.first);
    }
    return out;
}

bool Coordinator::peer_config_at(Role role, uint64_t player, const LocationId& location, PeerConfig& result,
                                 std::string& error) const {
    const auto& impl = *impl_;
    auto runtime = impl.location_sessions.find(location);
    if (!impl.location_mode || runtime == impl.location_sessions.end() || !runtime->second.session->lease_current()) {
        error = "location_inactive";
        return false;
    }
    PeerConfig peer;
    peer.host = impl.config.bind_host;
    peer.port = impl.config.port;
    peer.certificate_pin = impl.identity.pin;
    peer.identity = impl.config.session.identity;
    peer.role = role;
    peer.location = location;
    const auto& lease = impl.locations.location_leases().at(location);
    peer.native_load_slot = lease.native_load_slot;
    const uint8_t mask = resident_owner_mask(impl.locations, location);
    peer.primary_owner = impl.checkpoint_schema == 2
                             ? (role == Role::authority ? lease.native_load_slot + 1 : static_cast<uint32_t>(player))
                             : 0;
    if (peer.primary_owner) {
        peer.resident_owners = mask;
        const auto maintenance = runtime->second.session->maintenance_transfer();
        const auto* preparation = maintenance ? transfer_preparation(maintenance) : nullptr;
        if (role == Role::authority && preparation) {
            peer.resident_owners |= 1u << preparation->player;
        }
    }
    peer.secondary_save = impl.secondary_save;
    if (role == Role::authority) {
        peer.credential = runtime->second.config.authority;
        peer.worker_generation = lease.generation;
        peer.maintenance_transfer = runtime->second.session->maintenance_start();
    } else if (role == Role::player) {
        if (player != 1 && player != 2) {
            error = "player_invalid";
            return false;
        }
        const auto* avatar = impl.locations.avatar(player);
        if (!avatar || !avatar->active || !(avatar->location == location) ||
            !impl.locations.accepts_interest(player, location, avatar->generation, lease.worker, lease.generation)) {
            error = "player_not_interested_in_location";
            return false;
        }
        peer.credential = player == 1 ? impl.config.session.player1 : impl.config.session.player2;
    } else {
        error = "role_invalid";
        return false;
    }
    result = std::move(peer);
    error.clear();
    return true;
}

const TransferPreparation* Coordinator::transfer_preparation(uint64_t id) const noexcept {
    const auto found = impl_->preparations.find(id);
    if (found != impl_->preparations.end()) {
        return &found->second;
    }
    const auto native = impl_->native_transfers.find(id);
    return native == impl_->native_transfers.end() ? nullptr : &native->second.preparation;
}

bool Coordinator::prepare_transfer(const TransferPreparation& request, std::string& error) {
    auto& impl = *impl_;
    if (impl.config.offline || !impl.store || !impl.session || !impl.location_mode || !impl.checkpoint_revision) {
        error = "transfer_requires_online_selected_head";
        return false;
    }
    const auto existing = impl.preparations.find(request.id);
    if (existing != impl.preparations.end()) {
        if (existing->second.matches(request) && request.expected_revision < impl.store->revision() &&
            impl.preparation_record(request)) {
            error.clear();
            return true;
        }
        error = "transfer_preparation_identity_mismatch";
        return false;
    }
    if (request.expected_revision != impl.store->revision() || !impl.preparation_identity(request, impl.locations)) {
        error = "transfer_preparation_stale_identity";
        return false;
    }
    if (impl.preparations.size() >= 2) {
        error = "transfer_preparation_limit";
        return false;
    }
    const auto source = impl.location_sessions.find(request.source),
               destination = impl.location_sessions.find(request.destination);
    if (source == impl.location_sessions.end() || destination == impl.location_sessions.end() ||
        !source->second.session->authority_ready() || !destination->second.session->authority_ready()) {
        error = "transfer_preparation_authority_not_ready";
        return false;
    }
    LocationLedger candidate = impl.locations;
    if (!candidate.begin_transfer(request.id, request.player, request.destination, error)) {
        return false;
    }
    LocationLedger checked;
    if (!checked.restore(candidate.snapshot(), error)) {
        return false;
    }
    auto previous = std::move(impl.locations);
    impl.locations = std::move(checked);
    impl.preparations.emplace(request.id, request);
    auto bytes = impl.persisted(impl.session->content_snapshot());
    std::swap(impl.locations, previous);
    impl.preparations.erase(request.id);
    if (!impl.store->commit(bytes, error)) {
        return false;
    }
    impl.locations = std::move(previous);
    impl.preparations.emplace(request.id, request);
    // Preparation reserves an identity only. Sessions retain their exact
    // leases, baselines, interest and native input permission.
    error.clear();
    return true;
}

bool Coordinator::cancel_transfer(const TransferPreparation& request, uint64_t expected_revision, std::string& error) {
    auto& impl = *impl_;
    if (impl.config.offline || !impl.store || !impl.session || !impl.location_mode || !impl.checkpoint_revision) {
        error = "transfer_requires_online_selected_head";
        return false;
    }
    if (expected_revision && expected_revision != UINT64_MAX && impl.store->revision() == expected_revision + 1 &&
        impl.cancelled_preparation && impl.cancelled_preparation->expected_revision == expected_revision &&
        impl.cancelled_preparation->preparation.matches(request) && !impl.locations.transfer(request.id) &&
        !impl.preparations.count(request.id) && impl.preparation_identity(request, impl.locations)) {
        error.clear();
        return true;
    }
    const auto found = impl.preparations.find(request.id);
    if (impl.native_transfers.count(request.id)) {
        error = "transfer_cancellation_requires_native_recovery";
        return false;
    }
    if (!expected_revision || expected_revision == UINT64_MAX || expected_revision != impl.store->revision() ||
        request.expected_revision >= expected_revision || found == impl.preparations.end() ||
        !found->second.matches(request) || !impl.preparation_record(request)) {
        error = "transfer_cancellation_stale_identity";
        return false;
    }
    LocationLedger candidate = impl.locations;
    if (!candidate.abort_transfer(request.id, 0, error)) {
        return false;
    }
    auto previous = std::move(impl.locations);
    impl.locations = std::move(candidate);
    auto previous_preparations = impl.preparations;
    impl.preparations.erase(request.id);
    const auto previous_receipt = impl.cancelled_preparation;
    impl.cancelled_preparation = Impl::CancelReceipt{request, expected_revision};
    auto bytes = impl.persisted(impl.session->content_snapshot());
    std::swap(impl.locations, previous);
    impl.preparations = std::move(previous_preparations);
    impl.cancelled_preparation = previous_receipt;
    if (!impl.store->commit(bytes, error)) {
        return false;
    }
    impl.locations = std::move(previous);
    impl.preparations.erase(request.id);
    impl.cancelled_preparation = Impl::CancelReceipt{request, expected_revision};
    error.clear();
    return true;
}

bool Coordinator::commit_locations(uint64_t expected_revision,
                                   const std::function<bool(LocationLedger&, std::string&)>& step, std::string& error) {
    auto& impl = *impl_;
    if (!impl.config.offline || !impl.store || !impl.session || !impl.channels.empty() || !impl.checkpoint_revision ||
        !expected_revision || expected_revision != impl.store->revision() || !step) {
        error = "location_step_requires_exclusive_matching_revision";
        return false;
    }
    if (!impl.preparations.empty()) {
        error = "location_step_has_online_preparations";
        return false;
    }
    LocationLedger candidate = impl.locations;
    if (!step(candidate, error)) {
        return false;
    }
    LocationLedger checked;
    if (!checked.restore(candidate.snapshot(), error)) {
        return false;
    }
    if (!checked.checkpoints_share(impl.checkpoint_revision)) {
        error = "location_checkpoint_not_selected";
        return false;
    }
    if (!checked.content_matches(impl.config.session.identity.content)) {
        error = "location_content_mismatch";
        return false;
    }
    if (candidate.snapshot() == impl.locations.snapshot()) {
        error = "location_step_no_change";
        return false;
    }
    auto previous = impl.locations;
    const auto old_homes = impl.original_homes;
    for (size_t player = 1; player <= 2; ++player) {
        const auto* avatar = candidate.avatar(player);
        if (!impl.original_homes[player - 1] && avatar && avatar->location.planet <= UINT32_MAX) {
            impl.original_homes[player - 1] = uint32_t(avatar->location.planet);
        }
    }
    const auto new_homes = impl.original_homes;
    impl.locations = candidate;
    auto bytes = impl.persisted(impl.session->content_snapshot());
    impl.locations = std::move(previous);
    impl.original_homes = old_homes;
    if (!impl.store->commit(bytes, error)) {
        return false;
    }
    impl.locations = std::move(checked);
    impl.original_homes = new_homes;
    impl.location_mode = impl.locations.location_count() != 0;
    return true;
}

bool Coordinator::Impl::location_free_of_native_recovery(const LocationId& location, std::string& error) const {
    for (const auto& transfer : native_transfers) {
        const auto phase = transfer.second.state.phase;
        if (phase != NativeTransferPhase::complete && phase != NativeTransferPhase::aborted &&
            phase != NativeTransferPhase::checkpoint_recovered &&
            (transfer.second.preparation.source == location || transfer.second.preparation.destination == location)) {
            error = "location_replacement_requires_native_recovery";
            return false;
        }
    }
    for (uint64_t player = 1; player <= 2; ++player) {
        const auto* avatar = locations.avatar(player);
        if (avatar && avatar->location == location && recovery_artifact_transfer(player)) {
            error = "location_replacement_requires_native_artifact_recovery";
            return false;
        }
    }
    return true;
}

// The ledger after the replacement: preparations that touch the location are aborted, the old worker is lost, the
// new one holds the lease and the old worker's global systems.
bool Coordinator::Impl::replaced_location_ledger(const ReplacementRequest& request,
                                                 std::map<uint64_t, TransferPreparation>& retained_preparations,
                                                 LocationLedger& checked, std::string& error) const {
    LocationLedger candidate = locations;
    for (auto preparation = retained_preparations.begin(); preparation != retained_preparations.end();) {
        if (preparation->second.source == request.location || preparation->second.destination == request.location) {
            if (!candidate.abort_transfer(preparation->first, 0, error)) {
                return false;
            }
            preparation = retained_preparations.erase(preparation);
        } else {
            ++preparation;
        }
    }
    if (!candidate.worker_lost(request.location, request.old_worker, request.old_generation, error) ||
        !candidate.replace_worker(request.location, request.new_worker, request.new_generation, checkpoint_revision,
                                  error)) {
        return false;
    }
    if (!rebind_global_owners(candidate, request.old_worker, request.old_generation, request.new_worker,
                              request.new_generation, error)) {
        return false;
    }
    if (!checked.restore(candidate.snapshot(), error) || !checked.checkpoints_share(checkpoint_revision) ||
        !checked.content_matches(config.session.identity.content)) {
        if (error.empty()) {
            error = "location_replacement_invalid_state";
        }
        return false;
    }
    return true;
}

bool Coordinator::Impl::publish_replacement(const ReplacementRequest& request, LocationLedger& checked,
                                            std::map<uint64_t, TransferPreparation>& retained_preparations,
                                            std::string& error) {
    auto previous = std::move(locations);
    locations = std::move(checked);
    const auto previous_request = replacement_request;
    replacement_request = request;
    auto previous_preparations = std::move(preparations);
    preparations = std::move(retained_preparations);
    auto bytes = persisted(session->content_snapshot());
    std::swap(locations, previous);
    replacement_request = previous_request;
    std::swap(preparations, previous_preparations);
    if (!store->commit(bytes, error)) {
        return false;
    }
    locations = std::move(previous);
    replacement_request = request;
    preparations = std::move(previous_preparations);
    return true;
}

bool Coordinator::replace_disconnected_location(const LocationId& location, uint64_t old_worker,
                                                uint64_t old_generation, uint64_t new_worker, uint64_t new_generation,
                                                uint64_t expected_revision, std::string& error) {
    auto& impl = *impl_;
    if (impl.config.offline || !impl.store || !impl.location_mode || !impl.checkpoint_revision || !location.valid() ||
        !expected_revision) {
        error = "location_replacement_requires_online_selected_head";
        return false;
    }
    if (impl.checkpoint_stale_secondary(location)) {
        error = "location_replacement_requires_saved_secondary_cleanup";
        return false;
    }
    const auto current = impl.locations.location_leases().find(location);
    const auto runtime = impl.location_sessions.find(location);
    if (!impl.location_free_of_native_recovery(location, error)) {
        return false;
    }
    if (current == impl.locations.location_leases().end() || runtime == impl.location_sessions.end() ||
        !runtime->second.session) {
        error = "location_replacement_missing";
        return false;
    }
    const Impl::ReplacementRequest request{location,   old_worker,     old_generation,
                                           new_worker, new_generation, expected_revision};
    if (expected_revision != UINT64_MAX && impl.store->revision() == expected_revision + 1 &&
        impl.replacement_request && impl.replacement_request->matches(request) && current->second.active &&
        current->second.worker == new_worker && current->second.generation == new_generation &&
        runtime->second.config.worker_generation == new_generation && runtime->second.session->lease_current()) {
        error.clear();
        return true;
    }
    if (impl.store->revision() != expected_revision || !current->second.active ||
        current->second.worker != old_worker || current->second.generation != old_generation ||
        new_generation <= old_generation || runtime->second.session->authority_connected() ||
        !runtime->second.session->lease_current()) {
        error = "location_replacement_stale_or_authority_connected";
        return false;
    }
    Digest new_credential{};
    if (!detail::random_bytes(new_credential.data(), new_credential.size()) ||
        !runtime->second.session->can_rotate_disconnected_authority(new_generation, new_credential)) {
        error = "location_replacement_credential_or_scene_not_fenced";
        return false;
    }
    LocationLedger checked;
    auto retained_preparations = impl.preparations;
    if (!impl.replaced_location_ledger(request, retained_preparations, checked, error) ||
        !impl.publish_replacement(request, checked, retained_preparations, error)) {
        return false;
    }
    runtime->second.config.worker_generation = new_generation;
    runtime->second.config.authority = new_credential;
    // The preflight above and single-threaded coordinator mutation make this
    // infallible after the durable publication succeeds.
    runtime->second.session->rotate_disconnected_authority(new_generation, new_credential);
    error.clear();
    return true;
}

// A session over the current content whose world is `world`. Null when the content cannot be restored into it or
// the world is refused.
std::unique_ptr<Session> Coordinator::Impl::session_for_world(const WorldIdentity& world, std::string& error) const {
    auto candidate = std::make_unique<Session>(config.session);
    if (!candidate->restore_content(session->content_snapshot(), error) || !candidate->checkpoint_world(world)) {
        return nullptr;
    }
    return candidate;
}

// Selects the checkpoint for the publication and puts the previous one back when the store refuses it.
bool Coordinator::Impl::publish_offline_checkpoint(const std::vector<uint8_t>& bytes,
                                                   const CheckpointArtifact& checkpoint, uint64_t expected_revision,
                                                   const Session& candidate, std::string& error,
                                                   const std::function<void(const char*)>& test_boundary) {
    auto old_config = config.session;
    auto old_artifact = std::move(artifact);
    auto old_revision = checkpoint_revision;
    auto old_schema = checkpoint_schema;
    auto old_secondary = secondary_save;
    auto old_hash = checkpoint_hash;
    const auto old_games = original_games;
    artifact = bytes;
    config.session.identity = checkpoint.identity;
    checkpoint_revision = expected_revision + 1;
    checkpoint_schema = checkpoint.schema;
    secondary_save = checkpoint.secondary_save;
    checkpoint_hash = checkpoint.digest;
    original_games = checkpoint.schema == 2
                         ? std::array<uint32_t, 2>{checkpoint.native_ids[0], checkpoint.native_ids[1]}
                         : std::array<uint32_t, 2>{};
    const bool committed = store->commit(persisted(candidate.content_snapshot()), error, test_boundary);
    if (!committed) {
        artifact = std::move(old_artifact);
        config.session = old_config;
        checkpoint_revision = old_revision;
        checkpoint_schema = old_schema;
        secondary_save = old_secondary;
        checkpoint_hash = old_hash;
        original_games = old_games;
        return false;
    }
    return true;
}

bool Coordinator::commit_checkpoint(const std::vector<uint8_t>& bytes, uint64_t expected_revision, std::string& error,
                                    const std::function<void(const char*)>& test_boundary) {
    auto& impl = *impl_;
    if (!impl.config.offline || !impl.store || !impl.session || !impl.channels.empty() || !expected_revision ||
        impl.locations.location_count()) {
        error = "checkpoint_requires_exclusive_matching_revision";
        return false;
    }
    CheckpointArtifact checkpoint;
    if (!validate_checkpoint_artifact(bytes, checkpoint, error)) {
        return false;
    }
    const auto& previous = impl.config.session.identity;
    if (checkpoint.identity.build != previous.build || checkpoint.identity.executable != previous.executable ||
        checkpoint.identity.content != previous.content) {
        error = "checkpoint_compatibility_mismatch";
        return false;
    }
    // An exact retry is idempotent, including an uncertain acknowledgment.
    if (bytes == impl.artifact &&
        (expected_revision == impl.store->revision() || expected_revision + 1 == impl.checkpoint_revision)) {
        return true;
    }
    if (expected_revision != impl.store->revision()) {
        error = "checkpoint_stale_revision";
        return false;
    }
    auto candidate = impl.session_for_world(checkpoint.identity.world, error);
    if (!candidate ||
        !impl.publish_offline_checkpoint(bytes, checkpoint, expected_revision, *candidate, error, test_boundary)) {
        return false;
    }
    // The offline object never accepts a connection. A new online instance
    // restores the committed revision and publishes fresh session/baseline IDs.
    impl.session = std::move(candidate);
    impl.config.session.checkpoint_revision = impl.checkpoint_revision;
    return true;
}

PeerConfig Coordinator::peer_config(Role role, uint64_t player) const {
    const auto& impl = *impl_;
    if (impl.location_mode) {
        LocationId location{};
        if (role == Role::player) {
            const auto* avatar = impl.locations.avatar(player);
            if (avatar && avatar->active) {
                location = avatar->location;
            }
        } else if (!impl.location_sessions.empty()) {
            location = impl.location_sessions.begin()->first;
        }
        PeerConfig peer;
        std::string error;
        if (location.valid() && peer_config_at(role, player, location, peer, error)) {
            return peer;
        }
        return peer;
    }
    PeerConfig peer;
    peer.host = impl.config.bind_host;
    peer.port = impl.config.port;
    peer.certificate_pin = impl.identity.pin;
    peer.identity = impl.config.session.identity;
    peer.role = role;
    peer.credential = role == Role::authority ? impl.config.session.authority
                      : player == 1           ? impl.config.session.player1
                                              : impl.config.session.player2;
    return peer;
}

// A preload grant holds only while its lease, checkpoint and global owners do; the connection is closed otherwise.
void Coordinator::Impl::stop_stale_preloads() {
    for (const auto& entry : preloads) {
        const auto& grant = entry.second.granted;
        const auto lease = locations.location_leases().find(grant.location);
        bool valid = lease != locations.location_leases().end() && lease->second.active &&
                     lease->second.worker == grant.worker && lease->second.generation == grant.worker_generation &&
                     grant.checkpoint_revision == checkpoint_revision && grant.checkpoint == checkpoint_hash;
        for (size_t i = 0; i < grant.owners.size(); ++i) {
            const auto* global = locations.global_owner(static_cast<GlobalSystem>(i + 1));
            valid = valid && global && global->worker == grant.owners[i].worker &&
                    (global->generation == grant.owners[i].generation ||
                     (!grant.producer_mask && global->generation > grant.owners[i].generation));
        }
        if (!valid) {
            channels.at(entry.first)->request_stop();
            closing[entry.first] = GetTickCount64();
        }
    }
}

// Takes up to eight pending connections. Outside location mode a connection joins the one session at once.
void Coordinator::Impl::accept_connections() {
    for (size_t n = 0; n < 8; ++n) {
        SOCKET socket = accept(listener, nullptr, nullptr);
        if (socket == INVALID_SOCKET) {
            break;
        }
        if (channels.size() >= 64) {
            closesocket(socket);
            continue;
        }
        u_long blocking = 0;
        ioctlsocket(socket, FIONBIO, &blocking);
        uint64_t connection = ++next_connection;
        if (!location_mode && !session->connect(connection)) {
            closesocket(socket);
            continue;
        }
        if (!location_mode) {
            routes.emplace(connection, LocationId{});
        }
        auto channel = std::make_unique<detail::Channel>();
        channel->start_server(socket, &identity);
        channels.emplace(connection, std::move(channel));
    }
}

void Coordinator::Impl::disconnect(uint64_t connection) {
    if (auto* routed = routed_session(connection)) {
        dispatch(routed->disconnect(connection));
    }
    routes.erase(connection);
    preloads.erase(connection);
}

void Coordinator::Impl::reject_and_close(uint64_t connection, detail::Channel& channel, Error reason,
                                         uint64_t sequence) {
    Packet rejection;
    rejection.kind = Kind::reject;
    rejection.error = reason;
    rejection.sequence = sequence;
    channel.send(rejection);
    closing[connection] = GetTickCount64() + rejection_flush_ms;
}

// An authority presents the location's credential and generation; a player, a credential whose avatar is there.
bool Coordinator::Impl::preload_credential_valid(const Packet& packet, const LocationRuntime& runtime,
                                                 const LocationLease& lease, bool first) const {
    const auto& request = packet.preload;
    if (packet.role == Role::authority) {
        if (!same_secret(packet.credential, runtime.config.authority) ||
            request.worker_generation != lease.generation ||
            (first && request.maintenance_transfer != runtime.session->maintenance_start())) {
            return false;
        }
    } else {
        const auto player = player_from_credential(config.session, packet.credential);
        const auto* avatar = locations.avatar(player);
        if (!player || !avatar || !avatar->active || !(avatar->location == request.location) ||
            !locations.accepts_interest(player, request.location, avatar->generation, lease.worker, lease.generation)) {
            return false;
        }
    }
    return true;
}

// A later request on a bound connection must repeat the first one, apart from a higher sequence and nonce, while
// the checkpoint is unchanged and the bound process still runs.
bool Coordinator::Impl::preload_repeat_valid(const PreloadConnection& binding, const Packet& packet) const {
    const auto& request = packet.preload;
    const auto& first_request = binding.request;
    if (packet.sequence <= binding.sequence || request.nonce <= binding.nonce || packet.role != first_request.role ||
        !same_secret(packet.credential, first_request.credential) ||
        !(request.location == first_request.preload.location) ||
        request.worker_generation != first_request.preload.worker_generation ||
        request.native_load_slot != first_request.preload.native_load_slot ||
        request.maintenance_transfer != first_request.preload.maintenance_transfer ||
        request.process_id != first_request.preload.process_id ||
        request.process_created != first_request.preload.process_created || binding.revision != checkpoint_revision ||
        binding.checkpoint != checkpoint_hash || !binding.process ||
        WaitForSingleObject(binding.process->handle, 0) != WAIT_TIMEOUT) {
        return false;
    }
    return true;
}

// Binds the connection to the requesting process, which must be running the configured executable. An authority's
// process is also registered as its location's worker.
bool Coordinator::Impl::bind_preload_process(uint64_t connection, const Packet& packet, const LocationLease& lease) {
    const auto& request = packet.preload;
    auto process = std::make_unique<Impl::LocalProcess>();
    process->handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, request.process_id);
    std::wstring image;
    if (!process->handle || WaitForSingleObject(process->handle, 0) != WAIT_TIMEOUT ||
        !process_image_path(process->handle, request.process_created, image) ||
        !file_has_digest(image, config.session.identity.executable)) {
        return false;
    }
    if (packet.role == Role::authority) {
        std::string error;
        if (!register_local_worker_process(request.location, lease.worker, lease.generation, request.process_id,
                                           request.process_created, error)) {
            return false;
        }
    }
    Impl::PreloadConnection binding;
    binding.request = packet;
    binding.process = std::move(process);
    binding.revision = checkpoint_revision;
    binding.checkpoint = checkpoint_hash;
    preloads.emplace(connection, std::move(binding));
    return true;
}

// The grant names the location's lease, the selected checkpoint and the global owners, which must all be one
// worker. Only the authority that is that worker gets the producer mask.
bool Coordinator::Impl::grant_preload(uint64_t connection, const Packet& packet, const LocationLease& lease) {
    const auto& request = packet.preload;
    Packet response;
    response.kind = Kind::preload_grant;
    response.role = packet.role;
    response.sequence = packet.sequence;
    response.preload = request;
    auto& grant = response.preload;
    grant.worker = lease.worker;
    grant.worker_generation = lease.generation;
    grant.checkpoint_revision = checkpoint_revision;
    grant.checkpoint = checkpoint_hash;
    grant.universe = universe;
    grant.ttl_ms = preload_ttl_ms;
    bool owner = packet.role == Role::authority;
    for (size_t i = 0; i < grant.owners.size(); ++i) {
        const auto* global = locations.global_owner(static_cast<GlobalSystem>(i + 1));
        if (!global || !global->worker || !global->generation) {
            return false;
        }
        grant.owners[i] = {global->worker, global->generation};
        owner = owner && global->worker == grant.worker && global->generation == grant.worker_generation;
        if (i && (grant.owners[i].worker != grant.owners[0].worker ||
                  grant.owners[i].generation != grant.owners[0].generation)) {
            return false;
        }
    }
    grant.producer_mask = owner ? 15u : 0u;
    auto& binding = preloads.at(connection);
    binding.sequence = packet.sequence;
    binding.nonce = request.nonce;
    binding.granted = grant;
    return channels.at(connection)->send(response);
}

// Answers one preload request. The first request of a connection binds it to the requesting process; every later
// one must repeat that binding.
bool Coordinator::Impl::preload(uint64_t connection, const Packet& packet) {
    if (!location_mode || routes.count(connection) || packet.kind != Kind::preload_request || !packet.sequence ||
        !checkpoint_revision || checkpoint_hash == Digest{}) {
        return false;
    }
    const auto& request = packet.preload;
    const auto runtime = location_sessions.find(request.location);
    const auto lease = locations.location_leases().find(request.location);
    if (runtime == location_sessions.end() || lease == locations.location_leases().end() || !lease->second.active ||
        !runtime->second.session->lease_current() || !(packet.identity == config.session.identity) ||
        request.native_load_slot != lease->second.native_load_slot) {
        return false;
    }
    const auto previous = preloads.find(connection);
    const bool first = previous == preloads.end();
    if (!preload_credential_valid(packet, runtime->second, lease->second, first)) {
        return false;
    }
    if (first ? !bind_preload_process(connection, packet, lease->second)
              : !preload_repeat_valid(previous->second, packet)) {
        return false;
    }
    return grant_preload(connection, packet, lease->second);
}

// True when the packet belongs to the preload exchange. It is answered here and is not reported as an event.
bool Coordinator::Impl::receive_preload_packet(uint64_t connection, detail::Channel& channel, const Packet& packet) {
    if (packet.kind != Kind::preload_request && !preloads.count(connection)) {
        return false;
    }
    if (!preload(connection, packet)) {
        reject_and_close(connection, channel, Error::authentication, packet.sequence);
    }
    return true;
}

// In location mode a connection is routed by its hello, to the location it names. A player must have its avatar
// there. Null, after a rejection, when the connection is not admitted.
Session* Coordinator::Impl::admit_to_location(uint64_t connection, detail::Channel& channel, const Packet& packet) {
    auto location = location_sessions.find(packet.location);
    bool admitted = packet.kind == Kind::hello && packet.location.valid() && location != location_sessions.end();
    if (admitted && packet.role == Role::player) {
        const auto player = player_from_credential(config.session, packet.credential);
        if (player) {
            const auto* avatar = locations.avatar(player);
            const auto& lease = locations.location_leases().at(packet.location);
            admitted =
                avatar && avatar->active && avatar->location == packet.location &&
                locations.accepts_interest(player, packet.location, avatar->generation, lease.worker, lease.generation);
        }
    }
    if (admitted && location->second.session->connect(connection)) {
        routes.emplace(connection, packet.location);
        return location->second.session.get();
    }
    reject_and_close(connection, channel, admitted ? Error::capacity : Error::not_ready, 1);
    return nullptr;
}

void Coordinator::Impl::route_packet(uint64_t connection, detail::Channel& channel, const Packet& packet) {
    auto* routed = routed_session(connection);
    if (!routed && !routes.count(connection) && location_mode) {
        routed = admit_to_location(connection, channel, packet);
    }
    if (routed) {
        dispatch(routed->receive(connection, packet));
    }
}

// Handles up to 4096 events of one connection and queues them for the caller. A connection that disconnected,
// that the event queue has no room for, or whose close time has come is added to `closed`.
void Coordinator::Impl::pump_channel(uint64_t connection, detail::Channel& channel, std::vector<uint64_t>& closed) {
    Event incoming;
    size_t n = 0;
    while (n++ < 4096 && channel.poll(incoming)) {
        if (incoming.kind == EventKind::packet && !closing.count(connection)) {
            if (receive_preload_packet(connection, channel, incoming.packet)) {
                continue;
            }
            route_packet(connection, channel, incoming.packet);
        }
        if (incoming.kind == EventKind::disconnected) {
            disconnect(connection);
            closed.push_back(connection);
        }
        if (events.size() < queue_capacity) {
            events.push_back({connection, std::move(incoming)});
        } else {
            disconnect(connection);
            closed.push_back(connection);
            break;
        }
    }
    auto close = closing.find(connection);
    if (close != closing.end() && GetTickCount64() >= close->second) {
        disconnect(connection);
        closed.push_back(connection);
    }
}

bool Coordinator::poll(CoordinatorEvent& event) {
    auto& impl = *impl_;
    if (!impl.session || impl.config.offline) {
        return false;
    }
    impl.fence_stale_locations();
    impl.stop_stale_preloads();
    impl.accept_connections();
    std::vector<uint64_t> closed;
    for (auto& entry : impl.channels) {
        impl.pump_channel(entry.first, *entry.second, closed);
    }
    for (auto connection : closed) {
        impl.channels.erase(connection);
        impl.closing.erase(connection);
    }
    if (impl.events.empty()) {
        return false;
    }
    event = std::move(impl.events.front());
    impl.events.pop_front();
    return true;
}

void Coordinator::stop() {
    auto& impl = *impl_;
    if (impl.listener != INVALID_SOCKET) {
        closesocket(impl.listener);
        impl.listener = INVALID_SOCKET;
    }
    impl.channels.clear();
    impl.preloads.clear();
    impl.routes.clear();
    impl.location_sessions.clear();
    impl.session.reset();
    impl.store.reset();
}
} // namespace sporemp::network
