#pragma once
#include "peer.h"
#include "session.h"
#include "checkpoint_artifact.h"
#include "location_ledger.h"
#include <filesystem>
#include <memory>
#include <functional>

namespace sporemp::network {
struct CoordinatorConfig {
    std::string bind_host = "127.0.0.1";
    uint16_t port = 27060;
    SessionConfig session{};
    std::filesystem::path state_directory;
    bool offline = false;
};

struct CoordinatorEvent {
    uint64_t connection = 0;
    Event event{};
    bool outbound = false;
};

// Durable preparation identity. No native completion claim is represented here.
struct TransferPreparation {
    uint64_t id = 0, player = 0, expected_revision = 0, checkpoint_revision = 0;
    uint64_t avatar_generation = 0, source_worker = 0, source_worker_generation = 0;
    uint64_t destination_worker = 0, destination_worker_generation = 0;
    LocationId source{}, destination{};
    Digest universe{}, checkpoint_hash{};
    bool matches(const TransferPreparation& other) const noexcept;
};
enum class NativeTransferPhase : uint32_t {
    fence_requested = 1,
    source_fenced,
    artifact_bound,
    restore_requested,
    destination_restored,
    committed,
    activate_requested,
    destination_active,
    discard_requested,
    destination_discarded,
    rollback_requested,
    source_restored,
    source_activate_requested,
    aborted,
    complete,
    cleanup_requested,
    source_cleaned,
    checkpoint_recovered
};

struct NativeTransferState {
    NativeTransferPhase phase = NativeTransferPhase::fence_requested;
    TransferFrame command{}, source_receipt{}, destination_receipt{}, last_receipt{}, cleanup_receipt{};
    uint64_t command_session = 0, command_scene = 0, command_baseline = 0;
    uint64_t source_session = 0, source_scene = 0, source_baseline = 0, destination_session = 0, destination_scene = 0,
             destination_baseline = 0;
    uint64_t source_worker = 0, source_generation = 0, destination_worker = 0, destination_generation = 0;
    uint64_t avatar_generation = 0;
    uint64_t cleanup_session = 0, cleanup_scene = 0, cleanup_baseline = 0;
    uint64_t artifact_checkpoint = 0; // Newer native Save which superseded this player byte pair.
    bool ownership_committed = false;
    bool source_rollback_committed = false;
    bool completed_once = false;
    bool source_retired = false;
    uint32_t source_exit_code = 0;
};

class Coordinator {
public:
    Coordinator();
    ~Coordinator();
    bool start(CoordinatorConfig, std::string&);
    // Must be pumped by one coordinator thread. No game calls. Bounded per call.
    bool poll(CoordinatorEvent&);
    PeerConfig peer_config(Role role, uint64_t player = 0) const;
    uint64_t durable_revision() const noexcept;
    Digest universe() const noexcept;
    uint64_t checkpoint_revision() const noexcept;
    const std::vector<uint8_t>& checkpoint_artifact() const noexcept;
    const LocationLedger& locations() const noexcept;
    std::vector<LocationId> location_ids() const;
    bool peer_config_at(Role role, uint64_t player, const LocationId&, PeerConfig&, std::string&) const;
    // Online reservation; native ownership changes only after correlated
    // authority receipts and durable publication by the methods below.
    bool prepare_transfer(const TransferPreparation&, std::string&);
    bool cancel_transfer(const TransferPreparation&, uint64_t expected_revision, std::string&);
    const TransferPreparation* transfer_preparation(uint64_t id) const noexcept;
    const NativeTransferState* native_transfer(uint64_t id) const noexcept;
    // Fixed closed byte buffers: caller paths never reach the native worker.
    bool begin_native_transfer(uint64_t id, uint64_t expected_revision, uint32_t source_slot, uint32_t destination_slot,
                               std::string&);
    bool prepare_transfer_maintenance(const TransferPreparation&, std::string&);
    bool bind_native_transfer_artifact(uint64_t id, uint64_t expected_revision, const std::vector<uint8_t>& artifact,
                                       const std::vector<uint8_t>& approval, std::string&);
    bool request_native_transfer(uint64_t id, uint64_t expected_revision, TransferOp, std::string&);
    bool commit_native_transfer(uint64_t id, uint64_t expected_revision, std::string&);
    bool finish_native_transfer(uint64_t id, uint64_t expected_revision, std::string&);
    bool recover_native_transfer_worker(uint64_t id, uint64_t expected_revision, bool source,
                                        uint64_t replacement_worker, uint64_t replacement_generation, std::string&);
    bool native_transfer_artifact(uint64_t id, std::vector<uint8_t>& artifact, std::vector<uint8_t>& approval) const;
    uint64_t recovery_artifact_transfer(uint64_t player) const noexcept;
    bool commit_location_checkpoint(const std::vector<uint8_t>& artifact, const std::vector<uint8_t>& approval,
                                    const std::vector<uint8_t>& proof, uint64_t expected_revision, std::string&,
                                    const std::function<void(const char*)>& test_boundary = {});
    // Local qualified worker host only. Checks creation time and exact running
    // executable hash, then holds a process handle; never terminates a process.
    bool register_local_worker_process(const LocationId&, uint64_t worker, uint64_t generation, uint32_t process_id,
                                       uint64_t creation_time, std::string&);
    // Offline only: publish one validated location-policy step with the same
    // atomic universe record as content and checkpoint metadata. This is not
    // a native travel command or a native quiesce/restore witness.
    bool commit_locations(uint64_t expected_revision, const std::function<bool(LocationLedger&, std::string&)>& step,
                          std::string&);
    // Coordinator-thread control operation after the old authority transport
    // has closed and the worker's process exit has been verified by the host.
    // Atomically fences its lease, installs a higher generation and rotates
    // the authority credential while other location/player sessions continue.
    // An exact retry at the resulting revision is recognized from the durable
    // request identity, including after recovery. Altered old leases are stale.
    bool replace_disconnected_location(const LocationId&, uint64_t old_worker, uint64_t old_generation,
                                       uint64_t new_worker, uint64_t new_generation, uint64_t expected_revision,
                                       std::string&);
    // Requires an exclusive OFFLINE store. The expected revision prevents a
    // sealed world from being attached to a different content/ownership head.
    bool commit_checkpoint(const std::vector<uint8_t>&, uint64_t expected_revision, std::string&,
                           const std::function<void(const char*)>& test_boundary = {});
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sporemp::network
