// Included inside sporemp::network, after Coordinator::Impl. Kept separate from
// socket pumping so native transfer publication order remains reviewable.
namespace {
bool committed_native_phase(NativeTransferPhase phase) {
    return phase == NativeTransferPhase::committed || phase == NativeTransferPhase::activate_requested ||
           phase == NativeTransferPhase::destination_active || phase == NativeTransferPhase::cleanup_requested ||
           phase == NativeTransferPhase::source_cleaned || phase == NativeTransferPhase::complete;
}

bool terminal_native_phase(NativeTransferPhase phase) {
    return phase == NativeTransferPhase::complete || phase == NativeTransferPhase::aborted ||
           phase == NativeTransferPhase::checkpoint_recovered;
}

bool stable_pause(const TransferFrame& frame) {
    return frame.pause_request && frame.input_epoch && frame.app_end > frame.app_begin &&
           frame.ai_begin == frame.ai_end && frame.zero_deltas && frame.nonzero_deltas == 0 &&
           frame.elapsed_ms >= 3000 && frame.pause_after == 1 && frame.pause_before <= 1 && frame.state != Digest{};
}

bool resumed_native(const TransferFrame& frame) {
    return frame.pause_request && frame.input_epoch && frame.pause_before == 1 && frame.pause_after == 0 &&
           frame.app_end > frame.app_begin && frame.ai_end > frame.ai_begin && frame.zero_deltas &&
           frame.elapsed_ms >= 3000;
}

bool same_frame(const TransferFrame& a, const TransferFrame& b) {
    TransferWire a_wire{}, b_wire{};
    return encode_transfer(a, a_wire) && encode_transfer(b, b_wire) && a_wire == b_wire;
}
} // namespace

// The record's own fields: this transfer and its player, a checkpoint that is the selected one unless the transfer
// has ended, the player's original game and home in its command, and a known control operation.
bool Coordinator::Impl::native_record_identity_valid(uint64_t id, const NativeRecord& record) const {
    const auto& preparation = record.preparation;
    const auto& state = record.state;
    const auto& command = state.command;
    if (!id || id != preparation.id || id != command.id || preparation.player < 1 || preparation.player > 2 || !store ||
        preparation.expected_revision >= store->revision() || state.phase < NativeTransferPhase::fence_requested ||
        state.phase > NativeTransferPhase::checkpoint_recovered || record.destination_slot < 1 ||
        record.destination_slot > 2 || !state.command_session || !state.command_scene || !state.source_worker ||
        !state.source_generation || !state.destination_worker || !state.destination_generation ||
        preparation.universe != universe || !preparation.checkpoint_revision ||
        preparation.checkpoint_revision > checkpoint_revision || preparation.checkpoint_hash == Digest{} ||
        (preparation.checkpoint_revision == checkpoint_revision && preparation.checkpoint_hash != checkpoint_hash) ||
        (preparation.checkpoint_revision < checkpoint_revision && !terminal_native_phase(state.phase)) ||
        command.player != preparation.player ||
        command.unique_game_id != original_games[static_cast<size_t>(preparation.player - 1)] ||
        command.canonical_home != original_homes[static_cast<size_t>(preparation.player - 1)] || !command.revision ||
        command.revision > store->revision()) {
        return false;
    }
    if (!state.avatar_generation || !record.control_expected || record.control_expected >= store->revision() ||
        (record.control_op != 1 && record.control_op != 2 && record.control_op != 3 && record.control_op != 4 &&
         (record.control_op < 17 || record.control_op > 23) && record.control_op != 100 && record.control_op != 101)) {
        return false;
    }
    return true;
}

// Each side's lease is still the record's worker, unless the transfer ended or its completed source retired. A
// recorded exit is of an earlier worker generation, or of the retired source itself.
bool Coordinator::Impl::native_record_workers_valid(const NativeRecord& record, bool terminal) const {
    const auto& preparation = record.preparation;
    const auto& state = record.state;
    for (bool source : {true, false}) {
        const auto location = locations.location_leases().find(source ? preparation.source : preparation.destination);
        const auto worker = source ? state.source_worker : state.destination_worker,
                   generation = source ? state.source_generation : state.destination_generation;
        if (location == locations.location_leases().end() ||
            (!terminal && !(source && state.completed_once && state.source_retired) &&
             (!location->second.active || location->second.worker != worker ||
              location->second.generation != generation))) {
            return false;
        }
        const auto& exited = source ? record.source_exit : record.destination_exit;
        if (exited &&
            ((exited->generation >= generation &&
              !(source && state.source_retired && exited->generation == generation && exited->worker == worker)) ||
             ((exited->generation ==
               (source ? preparation.source_worker_generation : preparation.destination_worker_generation)) &&
              exited->worker != (source ? preparation.source_worker : preparation.destination_worker)))) {
            return false;
        }
    }
    return true;
}

bool Coordinator::Impl::native_record_processes_valid(const NativeRecord& record) {
    for (const auto* process : {&record.source_process, &record.destination_process}) {
        if (bool(process->pid) != bool(process->created)) {
            return false;
        }
    }
    for (const auto* exited : {&record.source_exit, &record.destination_exit}) {
        if (*exited) {
            const auto& proof = **exited;
            if (!proof.process.pid || !proof.process.created || !proof.worker || !proof.generation ||
                proof.code == STILL_ACTIVE) {
                return false;
            }
        }
    }
    return true;
}

// The source's fence receipt: a stable pause of the source's own player, witnessed for this transfer.
bool Coordinator::Impl::source_receipt_valid(uint64_t id, const NativeRecord& record, const Digest& digest) const {
    const auto& preparation = record.preparation;
    const auto& state = record.state;
    const auto& command = state.command;
    if (!state.source_receipt.id || state.source_receipt.op != TransferOp::source_fence ||
        !stable_pause(state.source_receipt) || state.source_receipt.pause_request != id ||
        state.source_receipt.pause_before || state.source_receipt.approval != Digest{} ||
        state.source_receipt.approval_bytes || !state.source_receipt.source_player ||
        state.source_receipt.native_player != state.source_receipt.source_player ||
        state.source_receipt.native_avatar != state.source_receipt.source_avatar ||
        state.source_receipt.native_slot != state.source_receipt.source_slot || !state.source_session ||
        !state.source_scene || !state.source_baseline || state.source_receipt.artifact == Digest{} ||
        !state.source_receipt.artifact_bytes) {
        return false;
    }
    Digest witness{};
    if (state.source_receipt.id != id || state.source_receipt.player != preparation.player ||
        state.source_receipt.preparation != digest ||
        state.source_receipt.avatar_generation != preparation.avatar_generation ||
        state.source_receipt.revision <= preparation.expected_revision ||
        state.source_receipt.revision > store->revision() || !state.source_receipt.process_id ||
        !state.source_receipt.process_created || state.source_receipt.worker != preparation.source_worker ||
        state.source_receipt.worker_generation != preparation.source_worker_generation ||
        state.source_receipt.unique_game_id != command.unique_game_id ||
        state.source_receipt.canonical_home != command.canonical_home ||
        !transfer_witness_digest(state.source_receipt, state.source_session, state.source_scene, state.source_baseline,
                                 witness) ||
        witness != state.source_receipt.state) {
        return false;
    }
    return true;
}

// The ledger's avatar and transfer agree with how far the record says the transfer went.
bool Coordinator::Impl::native_record_ledger_valid(uint64_t id, const NativeRecord& record, const Digest& digest,
                                                   const AvatarLease* avatar, const Transfer* transfer,
                                                   bool terminal) const {
    const auto& preparation = record.preparation;
    const auto& state = record.state;
    if (state.phase == NativeTransferPhase::checkpoint_recovered) {
        if (transfer || (avatar->generation == state.avatar_generation && !(avatar->location == preparation.source)) ||
            avatar->generation < state.avatar_generation || state.avatar_generation <= preparation.avatar_generation ||
            state.source_receipt.id || !record.source_exit || !record.artifact.empty() || !record.approval.empty()) {
            return false;
        }
    } else if (state.phase == NativeTransferPhase::fence_requested) {
        if (!transfer || transfer->phase != TransferPhase::prepared || !avatar->active ||
            avatar->generation != preparation.avatar_generation || !(avatar->location == preparation.source) ||
            state.source_receipt.id || !record.artifact.empty() || !record.approval.empty()) {
            return false;
        }
    } else {
        if (!source_receipt_valid(id, record, digest)) {
            return false;
        }
        if (state.source_rollback_committed) {
            if (transfer || state.avatar_generation <= preparation.avatar_generation + 1 ||
                (terminal
                     ? (avatar->generation < state.avatar_generation ||
                        (avatar->generation == state.avatar_generation && !(avatar->location == preparation.source)))
                     : (!avatar->active || !(avatar->location == preparation.source) ||
                        avatar->generation != state.avatar_generation))) {
                return false;
            }
        } else if (state.ownership_committed) {
            if (state.avatar_generation < preparation.avatar_generation + 2 ||
                (terminal ? (avatar->generation < state.avatar_generation ||
                             (avatar->generation == state.avatar_generation &&
                              !(avatar->location == preparation.destination)))
                          : (!avatar->active || !(avatar->location == preparation.destination) ||
                             avatar->generation != state.avatar_generation)) ||
                (state.completed_once ? transfer != nullptr
                                      : !transfer || transfer->phase != TransferPhase::committed)) {
                return false;
            }
        } else if (!transfer || avatar->active || !(avatar->location == preparation.source) ||
                   avatar->generation != preparation.avatar_generation + 1 ||
                   (transfer->phase != TransferPhase::source_fenced &&
                    transfer->phase != TransferPhase::destination_restored)) {
            return false;
        }
    }
    return true;
}

// A record holds its artifact and approval from the binding until a newer checkpoint supersedes them.
bool Coordinator::Impl::native_record_artifact_valid(uint64_t id, const NativeRecord& record, bool terminal) const {
    const auto& preparation = record.preparation;
    const auto& state = record.state;
    const auto& command = state.command;
    if (!record.artifact.empty()) {
        if (state.artifact_checkpoint) {
            return false;
        }
        Digest approved{};
        std::string error;
        if (!closed_transfer_artifact(record.artifact, record.approval, state.source_receipt, approved, error)) {
            return false;
        }
        if (command.op != TransferOp::source_fence &&
            (command.approval != approved || command.approval_bytes != record.approval.size())) {
            return false;
        }
        if (state.destination_receipt.id && (state.destination_receipt.approval != approved ||
                                             state.destination_receipt.approval_bytes != record.approval.size())) {
            return false;
        }
    } else if (!record.approval.empty() || (state.phase != NativeTransferPhase::fence_requested &&
                                            state.phase != NativeTransferPhase::source_fenced &&
                                            state.phase != NativeTransferPhase::checkpoint_recovered &&
                                            !(terminal && state.artifact_checkpoint > preparation.checkpoint_revision &&
                                              state.artifact_checkpoint <= checkpoint_revision))) {
        return false;
    }
    if (state.artifact_checkpoint) {
        if (!terminal || !record.artifact.empty() || !record.approval.empty() ||
            state.artifact_checkpoint <= preparation.checkpoint_revision ||
            state.artifact_checkpoint > checkpoint_revision) {
            return false;
        }
        if (state.artifact_checkpoint == checkpoint_revision) {
            GlobalCheckpointApproval approval;
            if (!decode_global_checkpoint_approval(checkpoint_approval, approval)) {
                return false;
            }
            const auto& head = approval.heads[static_cast<size_t>(preparation.player - 1)];
            if (head.transfer != id || head.artifact != state.source_receipt.artifact ||
                head.approval != command.approval) {
                return false;
            }
        }
    }
    return true;
}

bool Coordinator::Impl::destination_receipt_valid(uint64_t id, const NativeRecord& record, const Digest& digest) const {
    const auto& preparation = record.preparation;
    const auto& state = record.state;
    const auto& command = state.command;
    Digest witness{};
    const auto& receipt = state.destination_receipt;
    if (receipt.id != id || receipt.player != preparation.player || receipt.preparation != digest ||
        receipt.op != TransferOp::destination_restore || receipt.worker != state.destination_worker ||
        receipt.worker_generation != state.destination_generation || receipt.pause_request != id ||
        receipt.revision <= preparation.expected_revision || receipt.revision > store->revision() ||
        !receipt.process_id || !receipt.process_created ||
        (record.destination_process.pid && (receipt.process_id != record.destination_process.pid ||
                                            receipt.process_created != record.destination_process.created)) ||
        !receipt.native_player || !receipt.native_avatar || !state.destination_session || !state.destination_scene ||
        receipt.avatar_generation < preparation.avatar_generation + 1 ||
        receipt.avatar_generation > state.avatar_generation || receipt.artifact != state.source_receipt.artifact ||
        receipt.unique_game_id != command.unique_game_id || receipt.canonical_home != command.canonical_home ||
        !stable_pause(receipt) ||
        !transfer_witness_digest(receipt, state.destination_session, state.destination_scene,
                                 state.destination_baseline, witness) ||
        witness != receipt.state) {
        return false;
    }
    return true;
}

bool Coordinator::Impl::cleanup_receipt_valid(uint64_t id, const NativeRecord& record, const Digest& digest) const {
    const auto& preparation = record.preparation;
    const auto& state = record.state;
    const auto& command = state.command;
    const auto& receipt = state.cleanup_receipt;
    Digest witness{};
    const bool original = receipt.worker == preparation.source_worker &&
                          receipt.worker_generation == preparation.source_worker_generation && state.cleanup_baseline;
    const bool recovered = record.source_exit && receipt.worker == state.source_worker &&
                           receipt.worker_generation == state.source_generation &&
                           record.source_exit->generation < receipt.worker_generation &&
                           record.source_process.pid == receipt.process_id &&
                           record.source_process.created == receipt.process_created;
    const bool exited = record.source_exit && receipt.worker == record.source_exit->worker &&
                        receipt.worker_generation == record.source_exit->generation &&
                        receipt.process_id == record.source_exit->process.pid &&
                        receipt.process_created == record.source_exit->process.created;
    if (receipt.id != id || receipt.player != preparation.player || receipt.preparation != digest ||
        receipt.op != TransferOp::source_cleanup || receipt.artifact != state.source_receipt.artifact ||
        !state.cleanup_session || !state.cleanup_scene || (!original && !recovered && !exited) ||
        receipt.artifact_bytes != state.source_receipt.artifact_bytes ||
        receipt.unique_game_id != command.unique_game_id || receipt.canonical_home != command.canonical_home ||
        receipt.source_player != state.source_receipt.source_player ||
        receipt.source_avatar != state.source_receipt.source_avatar || receipt.source_slot != 2 ||
        receipt.native_slot != 2 || receipt.approval != command.approval ||
        receipt.approval_bytes != command.approval_bytes || receipt.native_player || receipt.native_avatar ||
        receipt.remaining_imports || !resumed_native(receipt) ||
        !transfer_witness_digest(receipt, state.cleanup_session, state.cleanup_scene, state.cleanup_baseline,
                                 witness) ||
        witness != receipt.state) {
        return false;
    }
    return true;
}

bool Coordinator::Impl::native_record_receipts_valid(uint64_t id, const NativeRecord& record,
                                                     const Digest& digest) const {
    const auto& state = record.state;
    const auto& command = state.command;
    if (state.destination_receipt.id) {
        if (!destination_receipt_valid(id, record, digest)) {
            return false;
        }
    } else if (state.ownership_committed && state.phase != NativeTransferPhase::artifact_bound &&
               state.phase != NativeTransferPhase::restore_requested) {
        return false;
    }
    if (committed_native_phase(state.phase) && !state.ownership_committed) {
        return false;
    }
    if (state.last_receipt.id) {
        Digest witness{};
        if (!same_transfer_request(command, state.last_receipt) ||
            !transfer_witness_digest(state.last_receipt, state.command_session, state.command_scene,
                                     state.command_baseline, witness) ||
            witness != state.last_receipt.state) {
            return false;
        }
    }
    if (state.cleanup_receipt.id) {
        if (!cleanup_receipt_valid(id, record, digest)) {
            return false;
        }
    } else if (state.cleanup_session || state.cleanup_scene || state.cleanup_baseline) {
        return false;
    }
    return true;
}

bool Coordinator::Impl::native_record_valid(uint64_t id, const NativeRecord& record) const {
    const auto& preparation = record.preparation;
    const auto& state = record.state;
    const auto& command = state.command;
    if (!native_record_identity_valid(id, record)) {
        return false;
    }
    Digest digest{};
    if (!preparation_digest(preparation, digest) || digest != command.preparation) {
        return false;
    }
    const auto* avatar = locations.avatar(preparation.player);
    const auto* transfer = locations.transfer(id);
    if (!avatar) {
        return false;
    }
    const bool terminal = terminal_native_phase(state.phase);
    if (!native_record_workers_valid(record, terminal) || !native_record_processes_valid(record)) {
        return false;
    }
    if (state.ownership_committed && state.source_rollback_committed) {
        return false;
    }
    if (state.completed_once && (!state.ownership_committed || !state.source_retired)) {
        return false;
    }
    if (!native_record_ledger_valid(id, record, digest, avatar, transfer, terminal) ||
        !native_record_artifact_valid(id, record, terminal) || !native_record_receipts_valid(id, record, digest)) {
        return false;
    }
    if (state.phase == NativeTransferPhase::complete && !state.completed_once) {
        return false;
    }
    if (state.completed_once) {
        if (state.source_receipt.source_slot == 1) {
            if (state.source_exit_code == STILL_ACTIVE) {
                return false;
            }
        } else if (!record.source_exit && !state.cleanup_receipt.id) {
            return false;
        }
    }
    if (state.phase == NativeTransferPhase::aborted && !state.source_rollback_committed) {
        return false;
    }
    return true;
}

bool Coordinator::Impl::publish_native(const LocationLedger& candidate, const std::map<uint64_t, NativeRecord>& records,
                                       std::string& error) {
    auto old_locations = locations;
    auto old_records = native_transfers;
    auto old_preparations = preparations;
    auto old_starts = maintenance_starts;
    locations = candidate;
    native_transfers = records;
    for (const auto& entry : records) {
        if (terminal_native_phase(entry.second.state.phase)) {
            preparations.erase(entry.first);
            erase_maintenance_starts(maintenance_starts, entry.first);
        }
    }
    const auto next_preparations = preparations;
    const auto next_starts = maintenance_starts;
    auto bytes = persisted(session->content_snapshot());
    locations = std::move(old_locations);
    native_transfers = std::move(old_records);
    preparations = std::move(old_preparations);
    maintenance_starts = std::move(old_starts);
    if (!store->commit(bytes, error)) {
        return false;
    }
    locations = candidate;
    native_transfers = records;
    preparations = next_preparations;
    maintenance_starts = next_starts;
    error.clear();
    return true;
}

const NativeTransferState* Coordinator::native_transfer(uint64_t id) const noexcept {
    const auto found = impl_->native_transfers.find(id);
    return found == impl_->native_transfers.end() ? nullptr : &found->second.state;
}

bool Coordinator::native_transfer_artifact(uint64_t id, std::vector<uint8_t>& artifact,
                                           std::vector<uint8_t>& approval) const {
    const auto found = impl_->native_transfers.find(id);
    if (found == impl_->native_transfers.end() || found->second.artifact.empty() || found->second.approval.empty()) {
        return false;
    }
    artifact = found->second.artifact;
    approval = found->second.approval;
    return true;
}

uint64_t Coordinator::Impl::recovery_artifact_transfer(uint64_t player) const noexcept {
    const auto* avatar = locations.avatar(player);
    if (!avatar) {
        return 0;
    }
    uint64_t result = 0, revision = 0;
    for (const auto& entry : native_transfers) {
        const auto& record = entry.second;
        const auto& state = record.state;
        if (record.preparation.player != player || record.artifact.empty() || !terminal_native_phase(state.phase) ||
            !(avatar->location ==
              (state.ownership_committed ? record.preparation.destination : record.preparation.source))) {
            continue;
        }
        if (state.source_receipt.revision > revision) {
            result = entry.first;
            revision = state.source_receipt.revision;
        }
    }
    return result;
}

uint64_t Coordinator::recovery_artifact_transfer(uint64_t player) const noexcept {
    return impl_->recovery_artifact_transfer(player);
}

// Whether a native transfer has pinned another process to this worker of the location.
bool Coordinator::Impl::pinned_process_differs(const LocationId& location, uint64_t worker, uint64_t generation,
                                               uint32_t process_id, uint64_t creation_time) const {
    for (const auto& entry : native_transfers) {
        const auto& record = entry.second;
        const auto& state = record.state;
        const auto* pinned = location == record.preparation.source && worker == state.source_worker &&
                                     generation == state.source_generation
                                 ? &record.source_process
                             : location == record.preparation.destination && worker == state.destination_worker &&
                                     generation == state.destination_generation
                                 ? &record.destination_process
                                 : nullptr;
        if (pinned && pinned->pid && (pinned->pid != process_id || pinned->created != creation_time)) {
            return true;
        }
    }
    return false;
}

bool Coordinator::Impl::register_local_worker_process(const LocationId& location, uint64_t worker, uint64_t generation,
                                                      uint32_t process_id, uint64_t creation_time, std::string& error) {
    const auto found = locations.location_leases().find(location);
    if (!process_id || !creation_time || found == locations.location_leases().end() || !found->second.active ||
        found->second.worker != worker || found->second.generation != generation) {
        error = "worker_process_registration_lease";
        return false;
    }
    if (pinned_process_differs(location, worker, generation, process_id, creation_time)) {
        error = "worker_process_registration_durable_identity";
        return false;
    }
    auto process = std::make_unique<Impl::LocalProcess>();
    process->handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    std::wstring image;
    if (!process->handle || !process_image_path(process->handle, creation_time, image)) {
        error = "worker_process_registration_identity";
        return false;
    }
    if (!file_has_digest(image, config.session.identity.executable)) {
        error = "worker_process_registration_image";
        return false;
    }
    const auto previous = local_processes.find(location);
    if (previous != local_processes.end() && previous->second->worker == worker &&
        previous->second->generation == generation &&
        (previous->second->pid != process_id || previous->second->created != creation_time)) {
        error = "worker_process_registration_conflict";
        return false;
    }
    process->worker = worker;
    process->generation = generation;
    process->pid = process_id;
    process->created = creation_time;
    local_processes[location] = std::move(process);
    error.clear();
    return true;
}

bool Coordinator::register_local_worker_process(const LocationId& location, uint64_t worker, uint64_t generation,
                                                uint32_t process_id, uint64_t creation_time, std::string& error) {
    return impl_->register_local_worker_process(location, worker, generation, process_id, creation_time, error);
}

// Whether another pending preparation uses either location of this one.
bool Coordinator::Impl::overlapping_preparation(uint64_t id, const TransferPreparation& preparation) const {
    for (const auto& other : preparations) {
        if (other.first != id &&
            (other.second.source == preparation.source || other.second.destination == preparation.source ||
             other.second.source == preparation.destination || other.second.destination == preparation.destination)) {
            return true;
        }
    }
    return false;
}

// The record a transfer begins with: both workers and their processes pinned, and the fence command for the
// source, bound to the source's current session, scene and baseline.
bool Coordinator::Impl::new_native_record(uint64_t id, uint64_t expected_revision,
                                          const TransferPreparation& preparation, uint32_t source_slot,
                                          uint32_t destination_slot, const Session& source, const Entity& actor,
                                          NativeRecord& record, std::string& error) const {
    record.preparation = preparation;
    record.destination_slot = destination_slot;
    auto& state = record.state;
    auto& command = state.command;
    record.control_expected = expected_revision;
    record.control_op = 1;
    state.avatar_generation = preparation.avatar_generation;
    record.source_process = {local_processes.at(preparation.source)->pid,
                             local_processes.at(preparation.source)->created};
    record.destination_process = {local_processes.at(preparation.destination)->pid,
                                  local_processes.at(preparation.destination)->created};
    command.id = id;
    command.player = preparation.player;
    command.revision = expected_revision + 1;
    command.avatar_generation = preparation.avatar_generation;
    command.worker = preparation.source_worker;
    command.worker_generation = preparation.source_worker_generation;
    command.unique_game_id = original_games[static_cast<size_t>(preparation.player - 1)];
    command.canonical_home = original_homes[static_cast<size_t>(preparation.player - 1)];
    command.source_avatar = actor.native_id;
    command.source_slot = source_slot;
    command.native_slot = source_slot;
    command.pause_request = id;
    if (!preparation_digest(preparation, command.preparation) ||
        !detail::random_bytes(command.challenge.data(), command.challenge.size())) {
        error = "native_transfer_challenge_failed";
        return false;
    }
    state.command_session = source.epoch();
    state.command_scene = source.scene();
    state.command_baseline = source.baseline_id();
    state.source_worker = preparation.source_worker;
    state.source_generation = preparation.source_worker_generation;
    state.destination_worker = preparation.destination_worker;
    state.destination_generation = preparation.destination_worker_generation;
    return true;
}

bool Coordinator::begin_native_transfer(uint64_t id, uint64_t expected_revision, uint32_t source_slot,
                                        uint32_t destination_slot, std::string& error) {
    auto& impl = *impl_;
    const auto prepared = impl.preparations.find(id);
    const auto prior = impl.native_transfers.find(id);
    if (impl.store && prior != impl.native_transfers.end() && prior->second.control_op == 1 &&
        prior->second.control_expected == expected_revision && expected_revision + 1 == impl.store->revision() &&
        prior->second.state.command.source_slot == source_slot && prior->second.destination_slot == destination_slot) {
        error.clear();
        return true;
    }
    if (impl.config.offline || !impl.store || !expected_revision || expected_revision != impl.store->revision() ||
        prepared == impl.preparations.end() || !impl.preparation_record(prepared->second) || source_slot < 1 ||
        source_slot > 2 || destination_slot < 1 || destination_slot > 2 || impl.native_transfers.count(id)) {
        error = "native_transfer_begin_stale";
        return false;
    }
    const auto& preparation = prepared->second;
    auto* source = impl.location_sessions.at(preparation.source).session.get();
    if (impl.overlapping_preparation(id, preparation)) {
        error = "native_transfer_overlapping_preparation";
        return false;
    }
    auto* destination = impl.location_sessions.at(preparation.destination).session.get();
    const auto* actor = source->owned_entity(preparation.player);
    if (!source->authority_ready() ||
        !(destination->authority_ready() ||
          (destination->maintenance_transfer() == id && destination->authority_control_ready())) ||
        !actor || !actor->native_id || !impl.local_processes.count(preparation.source) ||
        !impl.local_processes.count(preparation.destination) ||
        !impl.original_games[static_cast<size_t>(preparation.player - 1)] ||
        !impl.original_homes[static_cast<size_t>(preparation.player - 1)]) {
        error = "native_transfer_authority_or_process_not_ready";
        return false;
    }
    Impl::NativeRecord record;
    if (!impl.new_native_record(id, expected_revision, preparation, source_slot, destination_slot, *source, *actor,
                                record, error)) {
        return false;
    }
    auto records = impl.native_transfers;
    for (auto item = records.begin(); item != records.end();) {
        if (item->second.state.phase == NativeTransferPhase::checkpoint_recovered) {
            item = records.erase(item);
        } else {
            ++item;
        }
    }
    if (records.size() >= 4) {
        error = "native_transfer_capacity";
        return false;
    }
    records.emplace(id, record);
    if (!impl.publish_native(impl.locations, records, error)) {
        return false;
    }
    source->begin_transfer_maintenance(id);
    impl.dispatch(source->transfer_command(record.state.command));
    return true;
}

// A destination may be given a new worker for a transfer only once a completed transfer has durably retired the
// worker that held the destination's lease.
bool Coordinator::Impl::previous_worker_retired(const LocationId& destination, const LocationLease& lease) const {
    bool retired = false;
    for (const auto& entry : native_transfers) {
        const auto& earlier = entry.second;
        if (earlier.state.phase == NativeTransferPhase::complete && earlier.preparation.source == destination &&
            earlier.state.source_retired && earlier.state.source_worker == lease.worker &&
            earlier.state.source_generation == lease.generation) {
            retired = true;
        }
    }
    return retired;
}

bool Coordinator::Impl::publish_maintenance_start(const LocationLedger& candidate,
                                                  const TransferPreparation& preparation, std::string& error) {
    auto previous_locations = locations;
    locations = candidate;
    preparations.emplace(preparation.id, preparation);
    maintenance_starts.emplace(preparation.destination, preparation.id);
    auto bytes = persisted(session->content_snapshot());
    locations = std::move(previous_locations);
    preparations.erase(preparation.id);
    maintenance_starts.erase(preparation.destination);
    if (!store->commit(bytes, error)) {
        return false;
    }
    locations = candidate;
    preparations.emplace(preparation.id, preparation);
    maintenance_starts.emplace(preparation.destination, preparation.id);
    return true;
}

bool Coordinator::prepare_transfer_maintenance(const TransferPreparation& preparation, std::string& error) {
    auto& impl = *impl_;
    const auto location = impl.locations.location_leases().find(preparation.destination);
    const auto prior = impl.preparations.find(preparation.id);
    if (impl.store && prior != impl.preparations.end() && prior->second.matches(preparation) &&
        preparation.expected_revision + 1 == impl.store->revision() &&
        impl.maintenance_starts.count(preparation.destination) &&
        impl.maintenance_starts.at(preparation.destination) == preparation.id) {
        error.clear();
        return true;
    }
    if (impl.config.offline || !impl.store || preparation.expected_revision != impl.store->revision() ||
        !preparation.id || impl.preparations.count(preparation.id) || impl.preparations.size() >= 2 ||
        location == impl.locations.location_leases().end() || location->second.active ||
        !preparation.destination_worker || preparation.destination_worker_generation <= location->second.generation ||
        impl.maintenance_starts.count(preparation.destination)) {
        error = "transfer_maintenance_admission_stale";
        return false;
    }
    const auto source = impl.location_sessions.find(preparation.source);
    if (!impl.previous_worker_retired(preparation.destination, location->second) ||
        source == impl.location_sessions.end() || !source->second.session->authority_ready()) {
        error = "transfer_maintenance_previous_retirement_not_durable";
        return false;
    }
    LocationLedger candidate = impl.locations;
    if (!candidate.replace_worker(preparation.destination, preparation.destination_worker,
                                  preparation.destination_worker_generation, impl.checkpoint_revision, error) ||
        !impl.preparation_identity(preparation, candidate) ||
        !candidate.begin_transfer(preparation.id, preparation.player, preparation.destination, error)) {
        return false;
    }
    Impl::LocationRuntime runtime;
    runtime.config = impl.config.session;
    runtime.config.location = preparation.destination;
    runtime.config.worker_generation = preparation.destination_worker_generation;
    runtime.config.checkpoint_revision = impl.checkpoint_revision;
    runtime.config.native_load_slot = location->second.native_load_slot;
    runtime.config.permitted_owners = 0;
    runtime.config.content_authority = false;
    runtime.config.maintenance_transfer = preparation.id;
    if (!randomize_location_admission(runtime.config)) {
        error = "transfer_maintenance_random";
        return false;
    }
    if (!impl.publish_maintenance_start(candidate, preparation, error)) {
        return false;
    }
    impl.open_location_session(preparation.destination, runtime, impl.session->shared_content());
    impl.location_sessions[preparation.destination] = std::move(runtime);
    error.clear();
    return true;
}

bool Coordinator::Impl::accept_source_fence(const Packet& packet, NativeRecord& record, LocationLedger& candidate) {
    const auto& receipt = packet.transfer;
    auto& state = record.state;
    std::string error;
    if (state.phase != NativeTransferPhase::fence_requested || !stable_pause(receipt) || receipt.pause_before ||
        !receipt.source_player || receipt.native_player != receipt.source_player ||
        receipt.native_avatar != receipt.source_avatar || receipt.native_slot != receipt.source_slot ||
        receipt.artifact == Digest{} || !receipt.artifact_bytes || receipt.approval_bytes ||
        receipt.approval != Digest{} ||
        !candidate.fence_source(receipt.id, record.preparation.avatar_generation, error)) {
        return false;
    }
    state.source_receipt = receipt;
    state.source_session = packet.session;
    state.source_scene = packet.scene;
    state.source_baseline = packet.baseline;
    state.avatar_generation = record.preparation.avatar_generation + 1;
    state.phase = NativeTransferPhase::source_fenced;
    return true;
}

// A destination whose ownership is already committed is being restored again after its worker was replaced.
bool Coordinator::Impl::accept_destination_restore(const Packet& packet, NativeRecord& record,
                                                   LocationLedger& candidate) {
    const auto& receipt = packet.transfer;
    auto& state = record.state;
    std::string error;
    if (state.phase != NativeTransferPhase::restore_requested || !stable_pause(receipt) || !receipt.native_player ||
        !receipt.native_avatar ||
        (state.ownership_committed
             ? (!state.completed_once && !candidate.recover_committed_destination(receipt.id, state.destination_worker,
                                                                                  state.destination_generation, error))
             : !candidate.destination_restored(receipt.id, state.destination_worker, state.destination_generation,
                                               error))) {
        return false;
    }
    state.destination_receipt = receipt;
    state.destination_session = packet.session;
    state.destination_scene = packet.scene;
    state.destination_baseline = packet.baseline;
    state.phase =
        state.ownership_committed ? NativeTransferPhase::committed : NativeTransferPhase::destination_restored;
    return true;
}

bool Coordinator::Impl::accept_source_cleanup(const Packet& packet, NativeRecord& record) {
    const auto& receipt = packet.transfer;
    auto& state = record.state;
    if (state.phase != NativeTransferPhase::cleanup_requested || record.state.source_receipt.source_slot != 2 ||
        receipt.remaining_imports || receipt.native_player || receipt.native_avatar || !resumed_native(receipt)) {
        return false;
    }
    state.phase = NativeTransferPhase::source_cleaned;
    state.source_retired = true;
    state.cleanup_receipt = receipt;
    state.cleanup_session = packet.session;
    state.cleanup_scene = packet.scene;
    state.cleanup_baseline = packet.baseline;
    return true;
}

// Moves the record to the phase the receipt proves. False when the record did not ask for that receipt, or the
// receipt does not show what its operation requires.
bool Coordinator::Impl::apply_receipt(const Packet& packet, NativeRecord& record, LocationLedger& candidate) {
    const auto& receipt = packet.transfer;
    auto& state = record.state;
    switch (receipt.op) {
    case TransferOp::source_fence:
        return accept_source_fence(packet, record, candidate);
    case TransferOp::destination_restore:
        return accept_destination_restore(packet, record, candidate);
    case TransferOp::destination_discard:
        if (state.phase != NativeTransferPhase::discard_requested || receipt.remaining_imports ||
            receipt.native_player || receipt.native_avatar || !resumed_native(receipt)) {
            return false;
        }
        state.phase = NativeTransferPhase::destination_discarded;
        break;
    case TransferOp::source_restore:
        if (state.phase != NativeTransferPhase::rollback_requested || !stable_pause(receipt) ||
            !receipt.native_player || !receipt.native_avatar) {
            return false;
        }
        state.phase = NativeTransferPhase::source_restored;
        break;
    case TransferOp::source_activate:
        if (state.phase != NativeTransferPhase::source_activate_requested || !resumed_native(receipt)) {
            return false;
        }
        state.phase = NativeTransferPhase::aborted;
        break;
    case TransferOp::destination_activate:
        if (state.phase != NativeTransferPhase::activate_requested || !resumed_native(receipt)) {
            return false;
        }
        state.phase = NativeTransferPhase::destination_active;
        break;
    case TransferOp::source_cleanup:
        return accept_source_cleanup(packet, record);
    }
    return true;
}

bool Coordinator::Impl::native_receipt(const LocationId& location, const Packet& packet) {
    const auto found = native_transfers.find(packet.transfer.id);
    if (found == native_transfers.end()) {
        return false;
    }
    const auto& original = found->second;
    const auto& previous = original.state;
    const auto& receipt = packet.transfer;
    const auto runtime = location_sessions.find(location);
    if (runtime == location_sessions.end()) {
        return false;
    }
    if (packet.session != previous.command_session || packet.scene != previous.command_scene ||
        packet.baseline != previous.command_baseline || !same_transfer_request(previous.command, receipt) ||
        !process_matches(location, receipt)) {
        return false;
    }
    Digest witness{};
    if (!transfer_witness_digest(receipt, packet.session, packet.scene, packet.baseline, witness) ||
        witness != receipt.state) {
        return false;
    }
    if (previous.last_receipt.id && same_frame(previous.last_receipt, receipt)) {
        return true;
    }
    const bool destination = receipt.op == TransferOp::destination_restore ||
                             receipt.op == TransferOp::destination_discard ||
                             receipt.op == TransferOp::destination_activate;
    if (!(location == (destination ? original.preparation.destination : original.preparation.source))) {
        return false;
    }
    const auto& pinned = destination ? original.destination_process : original.source_process;
    if (!pinned.pid || pinned.pid != receipt.process_id || pinned.created != receipt.process_created) {
        return false;
    }
    auto records = native_transfers;
    auto& record = records.at(receipt.id);
    LocationLedger candidate = locations;
    std::string error;
    if (!apply_receipt(packet, record, candidate)) {
        return false;
    }
    record.state.last_receipt = receipt;
    if (!publish_native(candidate, records, error)) {
        return false;
    }
    const bool release = receipt.op == TransferOp::destination_activate || receipt.op == TransferOp::source_activate ||
                         receipt.op == TransferOp::destination_discard || receipt.op == TransferOp::source_cleanup;
    return runtime->second.session->rebind_transfer_maintenance(receipt.id, release);
}

bool Coordinator::bind_native_transfer_artifact(uint64_t id, uint64_t expected_revision,
                                                const std::vector<uint8_t>& artifact,
                                                const std::vector<uint8_t>& approval, std::string& error) {
    auto& impl = *impl_;
    const auto found = impl.native_transfers.find(id);
    if (impl.store && found != impl.native_transfers.end() && found->second.control_op == 2 &&
        found->second.control_expected == expected_revision && expected_revision + 1 == impl.store->revision() &&
        found->second.artifact == artifact && found->second.approval == approval) {
        error.clear();
        return true;
    }
    if (impl.config.offline || !impl.store || expected_revision != impl.store->revision() ||
        found == impl.native_transfers.end() || found->second.state.phase != NativeTransferPhase::source_fenced) {
        error = "transfer_artifact_stage";
        return false;
    }
    Digest approved{};
    if (!closed_transfer_artifact(artifact, approval, found->second.state.source_receipt, approved, error)) {
        return false;
    }
    auto records = impl.native_transfers;
    auto& record = records.at(id);
    record.artifact = artifact;
    record.approval = approval;
    for (auto previous = records.begin(); previous != records.end();) {
        if (previous->first != id && previous->second.preparation.player == record.preparation.player &&
            terminal_native_phase(previous->second.state.phase)) {
            previous = records.erase(previous);
        } else {
            ++previous;
        }
    }
    record.control_expected = expected_revision;
    record.control_op = 2;
    record.state.phase = NativeTransferPhase::artifact_bound;
    return impl.publish_native(impl.locations, records, error);
}

// The phase a request moves the transfer to, and whether the destination carries it out. False when the current
// phase does not admit the request.
bool Coordinator::Impl::request_phase(TransferOp op, NativeTransferState& state, bool& destination) {
    const auto phase = state.phase;
    switch (op) {
    case TransferOp::destination_restore:
        if (phase != NativeTransferPhase::artifact_bound) {
            return false;
        }
        destination = true;
        state.phase = NativeTransferPhase::restore_requested;
        break;
    case TransferOp::destination_activate:
        if (phase != NativeTransferPhase::committed) {
            return false;
        }
        destination = true;
        state.phase = NativeTransferPhase::activate_requested;
        break;
    case TransferOp::destination_discard:
        if (state.ownership_committed ||
            (phase != NativeTransferPhase::destination_restored && phase != NativeTransferPhase::restore_requested)) {
            return false;
        }
        destination = true;
        state.phase = NativeTransferPhase::discard_requested;
        break;
    case TransferOp::source_restore:
        if (state.ownership_committed ||
            (phase != NativeTransferPhase::artifact_bound && phase != NativeTransferPhase::destination_discarded)) {
            return false;
        }
        state.phase = NativeTransferPhase::rollback_requested;
        break;
    case TransferOp::source_activate:
        if (phase != NativeTransferPhase::source_restored) {
            return false;
        }
        state.phase = NativeTransferPhase::source_activate_requested;
        break;
    case TransferOp::source_cleanup:
        if (phase != NativeTransferPhase::destination_active || state.source_receipt.source_slot != 2) {
            return false;
        }
        state.phase = NativeTransferPhase::cleanup_requested;
        break;
    default:
        return false;
    }
    return true;
}

// The location's authority can take a command for this transfer, and its process is running and is the one the
// record pinned, or becomes it.
bool Coordinator::Impl::command_target_ready(const LocationId& location, uint64_t id, const Session& target,
                                             ProcessIdentity& pinned) const {
    if (!target.authority_control_ready() || (target.maintenance_transfer() && target.maintenance_transfer() != id)) {
        return false;
    }
    const auto process = local_processes.find(location);
    if (process == local_processes.end() || WaitForSingleObject(process->second->handle, 0) != WAIT_TIMEOUT) {
        return false;
    }
    if (pinned.pid && (pinned.pid != process->second->pid || pinned.created != process->second->created)) {
        return false;
    }
    pinned = {process->second->pid, process->second->created};
    return true;
}

// The command repeats the source receipt's identity for the requested operation and its worker, with a fresh
// challenge and none of the receipt's measurements.
bool Coordinator::Impl::request_command(TransferOp op, uint64_t revision, bool destination, const NativeRecord& record,
                                        TransferFrame& command) {
    const auto& state = record.state;
    command = state.source_receipt;
    command.op = op;
    command.revision = revision;
    command.state = {};
    command.worker = destination ? state.destination_worker : state.source_worker;
    command.worker_generation = destination ? state.destination_generation : state.source_generation;
    command.native_slot = destination ? record.destination_slot : command.source_slot;
    command.native_player = command.native_avatar = 0;
    command.avatar_generation = op == TransferOp::source_activate && !state.source_rollback_committed
                                    ? record.preparation.avatar_generation + 2
                                    : state.avatar_generation;
    if (!content_digest(record.approval.data(), record.approval.size(), command.approval) ||
        !detail::random_bytes(command.challenge.data(), command.challenge.size())) {
        return false;
    }
    command.approval_bytes = static_cast<uint32_t>(record.approval.size());
    command.input_epoch = command.app_begin = command.app_end = command.ai_begin = command.ai_end =
        command.zero_deltas = command.nonzero_deltas = command.elapsed_ms = 0;
    command.pause_before = command.pause_after = command.remaining_imports = command.process_id = 0;
    command.process_created = 0;
    return true;
}

bool Coordinator::request_native_transfer(uint64_t id, uint64_t expected_revision, TransferOp op, std::string& error) {
    auto& impl = *impl_;
    const auto found = impl.native_transfers.find(id);
    if (impl.store && found != impl.native_transfers.end() && found->second.control_op == 16 + uint32_t(op) &&
        found->second.control_expected == expected_revision && expected_revision + 1 == impl.store->revision()) {
        error.clear();
        return true;
    }
    if (impl.config.offline || !impl.store || !expected_revision || expected_revision != impl.store->revision() ||
        expected_revision == UINT64_MAX || found == impl.native_transfers.end()) {
        error = "native_transfer_request_stale";
        return false;
    }
    auto records = impl.native_transfers;
    auto& record = records.at(id);
    auto& state = record.state;
    record.control_expected = expected_revision;
    record.control_op = 16 + uint32_t(op);
    bool destination = false;
    const auto invalid = [&error] {
        error = "native_transfer_phase_or_authority_not_ready";
        return false;
    };
    if (!Impl::request_phase(op, state, destination)) {
        return invalid();
    }
    const auto location = destination ? record.preparation.destination : record.preparation.source;
    auto* session = impl.location_sessions.at(location).session.get();
    TransferFrame command;
    if (!impl.command_target_ready(location, id, *session,
                                   destination ? record.destination_process : record.source_process) ||
        !Impl::request_command(op, expected_revision + 1, destination, record, command)) {
        return invalid();
    }
    state.command = command;
    state.last_receipt = {};
    state.command_session = session->epoch();
    state.command_scene = session->scene();
    state.command_baseline = session->baseline_id();
    LocationLedger candidate = impl.locations;
    if (op == TransferOp::source_activate && !state.source_rollback_committed) {
        if (!candidate.abort_transfer(id, record.preparation.avatar_generation + 2, error)) {
            return false;
        }
        state.avatar_generation = record.preparation.avatar_generation + 2;
        state.source_rollback_committed = true;
    }
    if (!impl.publish_native(candidate, records, error)) {
        return false;
    }
    if (op == TransferOp::source_activate) {
        session->rebind_transfer_maintenance(id);
    }
    session->begin_transfer_maintenance(id);
    impl.dispatch(session->transfer_command(command));
    return true;
}

bool Coordinator::commit_native_transfer(uint64_t id, uint64_t expected_revision, std::string& error) {
    auto& impl = *impl_;
    const auto found = impl.native_transfers.find(id);
    if (impl.store && found != impl.native_transfers.end() && found->second.control_op == 3 &&
        found->second.control_expected == expected_revision && expected_revision + 1 == impl.store->revision()) {
        error.clear();
        return true;
    }
    if (impl.config.offline || !impl.store || expected_revision != impl.store->revision() ||
        found == impl.native_transfers.end() ||
        found->second.state.phase != NativeTransferPhase::destination_restored) {
        error = "native_transfer_commit_stage";
        return false;
    }
    const auto& record = found->second;
    const auto preparation = record.preparation;
    const auto* source = impl.location_sessions.at(preparation.source).session.get();
    const auto* destination = impl.location_sessions.at(preparation.destination).session.get();
    if (!impl.process_matches(preparation.source, record.state.source_receipt) ||
        !impl.process_matches(preparation.destination, record.state.destination_receipt) ||
        !source->authority_control_ready() || !destination->authority_control_ready() ||
        source->maintenance_transfer() != id || destination->maintenance_transfer() != id ||
        source->epoch() != record.state.source_session || source->scene() != record.state.source_scene ||
        source->baseline_id() != record.state.source_baseline ||
        destination->epoch() != record.state.destination_session ||
        destination->scene() != record.state.destination_scene ||
        destination->baseline_id() != record.state.destination_baseline) {
        error = "native_transfer_commit_lost_witness_owner";
        return false;
    }
    LocationLedger candidate = impl.locations;
    if (!candidate.commit_transfer(id, error)) {
        return false;
    }
    auto records = impl.native_transfers;
    records.at(id).state.phase = NativeTransferPhase::committed;
    records.at(id).state.ownership_committed = true;
    records.at(id).state.avatar_generation = preparation.avatar_generation + 2;
    records.at(id).control_expected = expected_revision;
    records.at(id).control_op = 3;
    if (!impl.publish_native(candidate, records, error)) {
        return false;
    }
    impl.location_sessions.at(preparation.source).session->rebind_transfer_maintenance(id);
    impl.location_sessions.at(preparation.destination).session->rebind_transfer_maintenance(id);
    return true;
}

// Without a cleanup receipt the source is retired by the observed exit of its process, which only a transfer out
// of slot 1 allows.
bool Coordinator::Impl::observe_source_exit(NativeRecord& record, std::string& error) const {
    auto& state = record.state;
    if (state.source_receipt.source_slot != 1) {
        error = "native_transfer_cleanup_receipt_required";
        return false;
    }
    const auto process = local_processes.find(record.preparation.source);
    DWORD exit_code = STILL_ACTIVE;
    if (process == local_processes.end() || process->second->pid != state.source_receipt.process_id ||
        process->second->created != state.source_receipt.process_created ||
        !process_exited(process->second->handle, exit_code)) {
        error = "native_transfer_source_exit_not_observed";
        return false;
    }
    state.source_retired = true;
    state.source_exit_code = exit_code;
    record.source_exit =
        Impl::ProcessExit{record.source_process, state.source_worker, state.source_generation, exit_code};
    return true;
}

// The emptied source location gives up its lease. It must hold no avatar and own no global system.
bool Coordinator::Impl::vacate_empty_source(LocationLedger& candidate, const NativeRecord& record, std::string& error) {
    const auto& state = record.state;
    for (uint64_t player = 1; player <= 2; ++player) {
        const auto* avatar = candidate.avatar(player);
        if (avatar && avatar->active && avatar->location == record.preparation.source) {
            error = "native_transfer_source_still_occupied";
            return false;
        }
    }
    for (const auto system : global_systems) {
        const auto* owner = candidate.global_owner(system);
        if (owner && owner->worker == state.source_worker && owner->generation == state.source_generation) {
            error = "native_transfer_source_owns_global_producer";
            return false;
        }
    }
    if (!candidate.worker_lost(record.preparation.source, state.source_worker, state.source_generation, error)) {
        return false;
    }
    return true;
}

bool Coordinator::finish_native_transfer(uint64_t id, uint64_t expected_revision, std::string& error) {
    auto& impl = *impl_;
    const auto found = impl.native_transfers.find(id);
    if (impl.store && found != impl.native_transfers.end() && found->second.control_op == 4 &&
        found->second.control_expected == expected_revision && expected_revision + 1 == impl.store->revision()) {
        error.clear();
        return true;
    }
    if (impl.config.offline || !impl.store || expected_revision != impl.store->revision() ||
        found == impl.native_transfers.end() ||
        (found->second.state.phase != NativeTransferPhase::destination_active &&
         found->second.state.phase != NativeTransferPhase::source_cleaned)) {
        error = "native_transfer_cleanup_stage";
        return false;
    }
    auto records = impl.native_transfers;
    auto& record = records.at(id);
    auto& state = record.state;
    const auto* destination = impl.location_sessions.at(record.preparation.destination).session.get();
    const auto* restored = destination->owned_entity(record.preparation.player);
    if (!destination->authority_ready() || !restored ||
        restored->native_id != state.destination_receipt.native_avatar) {
        error = "native_transfer_destination_baseline_required";
        return false;
    }
    const bool saved_secondary =
        impl.checkpoint_stale_secondary(record.preparation.source, record.preparation.player) &&
        state.source_receipt.source_slot == 2 && record.source_exit.has_value();
    if (saved_secondary &&
        (state.phase != NativeTransferPhase::source_cleaned || state.cleanup_receipt.worker != state.source_worker ||
         state.cleanup_receipt.worker_generation != state.source_generation)) {
        error = "native_transfer_saved_secondary_cleanup_required";
        return false;
    }
    if ((!state.completed_once || saved_secondary) && state.source_retired && record.source_exit &&
        state.source_receipt.source_slot == 2 &&
        !impl.location_sessions.at(record.preparation.source).session->authority_ready()) {
        error = "native_transfer_recovered_source_baseline_required";
        return false;
    }
    record.control_expected = expected_revision;
    record.control_op = 4;
    if (!state.source_retired && !impl.observe_source_exit(record, error)) {
        return false;
    }
    LocationLedger candidate = impl.locations;
    if (!state.completed_once && state.source_receipt.source_slot == 1 &&
        !Impl::vacate_empty_source(candidate, record, error)) {
        return false;
    }
    if (!state.completed_once && !candidate.finish_transfer(id, error)) {
        return false;
    }
    state.phase = NativeTransferPhase::complete;
    state.completed_once = true;
    return impl.publish_native(candidate, records, error);
}

// Every reason to refuse a recovery, in the order they are reported. The old worker's process must be observed
// dead; `exit_code` receives its exit code.
bool Coordinator::Impl::worker_recovery_admitted(const WorkerRecovery& recovery, uint64_t expected_revision,
                                                 const NativeRecord& old, const LocationLease* lease, bool terminal,
                                                 bool saved_secondary, DWORD& exit_code, std::string& error) const {
    const auto& previous = old.state;
    if (store->revision() != expected_revision || recovery.replacement_generation <= recovery.old_generation ||
        previous.phase == NativeTransferPhase::checkpoint_recovered ||
        (terminal && ((recovery.source == previous.ownership_committed && !saved_secondary) ||
                      recovery_artifact_transfer(old.preparation.player) != recovery.transfer))) {
        error = "native_transfer_recovery_stale";
        return false;
    }
    if (saved_secondary && (old.artifact.empty() || old.approval.empty())) {
        error = "native_transfer_saved_secondary_artifact_required";
        return false;
    }
    const auto artifact_head = recovery_artifact_transfer(old.preparation.player);
    if (recovery.source && previous.phase == NativeTransferPhase::fence_requested && artifact_head &&
        artifact_head != recovery.transfer) {
        error = "native_transfer_recovery_requires_latest_artifact";
        return false;
    }
    if (recovery.source && previous.ownership_committed && previous.source_receipt.source_slot == 1) {
        error = "native_transfer_empty_source_requires_cleanup";
        return false;
    }
    const auto process = local_processes.find(recovery.location);
    const auto& pinned = recovery.source ? old.source_process : old.destination_process;
    if (!pinned.pid || process == local_processes.end() || process->second->worker != recovery.old_worker ||
        process->second->generation != recovery.old_generation || process->second->pid != pinned.pid ||
        process->second->created != pinned.created || !process_exited(process->second->handle, exit_code)) {
        error = "native_transfer_recovery_old_process_not_proven_dead";
        return false;
    }
    if (!lease || lease->worker != recovery.old_worker || lease->generation != recovery.old_generation) {
        error = "native_transfer_recovery_lease_mismatch";
        return false;
    }
    // The live source still owns the pending removal command. Replacing the
    // destination now would overwrite its phase and reject the source receipt
    // and ensuing resident baseline. Finish that removal before recovery.
    if (!recovery.source && previous.phase == NativeTransferPhase::cleanup_requested) {
        error = "native_transfer_source_cleanup_pending";
        return false;
    }
    // A replacement may retain the same canonical global producer identity at
    // a higher generation after process death. Cross-worker global migration
    // remains a separate native ownership operation.
    for (const auto system : global_systems) {
        const auto* owner = locations.global_owner(system);
        if (owner && owner->worker == recovery.old_worker && owner->generation == recovery.old_generation &&
            recovery.replacement_worker != recovery.old_worker) {
            error = "native_transfer_recovery_global_migration_refused";
            return false;
        }
    }
    return true;
}

// Also cancel a reservation which has not issued its first command.
bool Coordinator::Impl::cancel_later_reservations(const WorkerRecovery& recovery, uint64_t player,
                                                  LocationLedger& candidate,
                                                  std::map<uint64_t, TransferPreparation>& pending_preparations,
                                                  std::map<LocationId, uint64_t>& pending_starts, std::string& error) {
    for (auto pending = pending_preparations.begin(); pending != pending_preparations.end();) {
        if (pending->first != recovery.transfer && pending->second.player == player) {
            if (!(pending->second.source == recovery.location) || !candidate.abort_transfer(pending->first, 0, error)) {
                return false;
            }
            const auto abandoned = pending->first;
            pending = pending_preparations.erase(pending);
            erase_maintenance_starts(pending_starts, abandoned);
        } else {
            ++pending;
        }
    }
    return true;
}

// A later unfenced/unbound attempt does not retire the last-good state.
// Abandon that attempt in the same publication as artifact recovery.
bool Coordinator::Impl::abandon_later_attempts(const WorkerRecovery& recovery,
                                               std::map<uint64_t, NativeRecord>& records, LocationLedger& candidate,
                                               std::map<uint64_t, TransferPreparation>& pending_preparations,
                                               std::map<LocationId, uint64_t>& pending_starts, std::string& error) {
    const auto& record = records.at(recovery.transfer);
    for (auto other = records.begin(); other != records.end();) {
        const auto& pending = other->second;
        if (other->first == recovery.transfer || pending.preparation.player != record.preparation.player ||
            terminal_native_phase(pending.state.phase)) {
            ++other;
            continue;
        }
        if (!pending.artifact.empty() ||
            (pending.state.phase != NativeTransferPhase::fence_requested &&
             pending.state.phase != NativeTransferPhase::source_fenced) ||
            !(pending.preparation.source == recovery.location)) {
            error = "native_transfer_recovery_newer_state_active";
            return false;
        }
        const auto* avatar = candidate.avatar(record.preparation.player);
        if (!candidate.abort_transfer(
                other->first, pending.state.phase == NativeTransferPhase::source_fenced ? avatar->generation + 1 : 0,
                error)) {
            return false;
        }
        pending_preparations.erase(other->first);
        erase_maintenance_starts(pending_starts, other->first);
        other = records.erase(other);
    }
    if (!cancel_later_reservations(recovery, record.preparation.player, candidate, pending_preparations, pending_starts,
                                   error)) {
        return false;
    }
    pending_preparations[recovery.transfer] = record.preparation;
    return true;
}

// The candidate ledger loses the dead worker and gains its replacement, which takes over the dead worker's global
// systems. A transfer that had only asked for the source fence is aborted.
bool Coordinator::Impl::replace_worker_in_ledger(const WorkerRecovery& recovery, bool checkpoint_only,
                                                 bool lease_active, LocationLedger& candidate,
                                                 std::string& error) const {
    if (checkpoint_only && !candidate.abort_transfer(recovery.transfer, 0, error)) {
        return false;
    }
    if (lease_active &&
        !candidate.worker_lost(recovery.location, recovery.old_worker, recovery.old_generation, error)) {
        return false;
    }
    if (!candidate.replace_worker(recovery.location, recovery.replacement_worker, recovery.replacement_generation,
                                  checkpoint_revision, error)) {
        return false;
    }
    return rebind_global_owners(candidate, recovery.old_worker, recovery.old_generation, recovery.replacement_worker,
                                recovery.replacement_generation, error);
}

// The phase a transfer resumes from once the side's worker has been replaced. True when the replacement starts in
// the transfer's maintenance.
bool Coordinator::Impl::phase_after_recovery(NativeTransferState& state, bool source, bool checkpoint_only,
                                             bool saved_secondary, DWORD exit_code) {
    bool maintenance = true;
    if (checkpoint_only) {
        state.phase = NativeTransferPhase::checkpoint_recovered;
        maintenance = false;
    } else if (source && state.ownership_committed) {
        // An imported secondary is gone with the observed dead process. The
        // source resident reloads its selected checkpoint under the new lease.
        state.source_retired = true;
        state.source_exit_code = exit_code;
        maintenance = saved_secondary;
        if (saved_secondary) {
            // The selected paired Save still contains this departed secondary.
            // Only a fresh original removal receipt may release its resident.
            if (state.phase == NativeTransferPhase::complete || state.phase == NativeTransferPhase::cleanup_requested ||
                state.phase == NativeTransferPhase::source_cleaned) {
                state.phase = NativeTransferPhase::destination_active;
            }
        } else if (state.phase == NativeTransferPhase::cleanup_requested) {
            state.phase = NativeTransferPhase::source_cleaned;
        }
    } else if (source) {
        if (state.phase == NativeTransferPhase::rollback_requested ||
            state.phase == NativeTransferPhase::source_restored ||
            state.phase == NativeTransferPhase::source_activate_requested ||
            state.phase == NativeTransferPhase::aborted) {
            state.phase = state.destination_receipt.id ? NativeTransferPhase::destination_discarded
                                                       : NativeTransferPhase::artifact_bound;
        }
    } else {
        state.destination_receipt = {};
        state.destination_session = state.destination_scene = state.destination_baseline = 0;
        if (state.ownership_committed || state.phase == NativeTransferPhase::restore_requested ||
            state.phase == NativeTransferPhase::destination_restored) {
            state.phase = NativeTransferPhase::artifact_bound;
        } else if (state.phase == NativeTransferPhase::discard_requested ||
                   state.phase == NativeTransferPhase::destination_discarded) {
            state.phase = NativeTransferPhase::destination_discarded;
            maintenance = false;
        } else if (state.phase == NativeTransferPhase::rollback_requested ||
                   state.phase == NativeTransferPhase::source_restored ||
                   state.phase == NativeTransferPhase::source_activate_requested) {
            maintenance = false;
        }
    }
    return maintenance;
}

// Publishes the recovery, then admits the replacement worker under a fresh credential and session.
bool Coordinator::Impl::install_worker_recovery(const WorkerRecovery& recovery, bool maintenance,
                                                uint32_t native_load_slot, const LocationLedger& candidate,
                                                const std::map<uint64_t, NativeRecord>& records,
                                                const std::map<uint64_t, TransferPreparation>& pending_preparations,
                                                const std::map<LocationId, uint64_t>& pending_starts,
                                                std::string& error) {
    Impl::LocationRuntime runtime;
    runtime.config = config.session;
    runtime.config.location = recovery.location;
    runtime.config.worker_generation = recovery.replacement_generation;
    runtime.config.checkpoint_revision = checkpoint_revision;
    runtime.config.native_load_slot = native_load_slot;
    runtime.config.maintenance_transfer = maintenance ? recovery.transfer : 0;
    runtime.config.permitted_owners = resident_owner_mask(candidate, recovery.location);
    runtime.config.content_authority =
        owns_content(candidate, recovery.replacement_worker, recovery.replacement_generation);
    if (!randomize_location_admission(runtime.config)) {
        error = "native_transfer_recovery_random";
        return false;
    }
    LocationLedger checked;
    if (!checked.restore(candidate.snapshot(), error)) {
        return false;
    }
    const auto old_starts = maintenance_starts;
    const auto old_preparations = preparations;
    preparations = pending_preparations;
    maintenance_starts = pending_starts;
    if (maintenance) {
        maintenance_starts[recovery.location] = recovery.transfer;
    } else {
        maintenance_starts.erase(recovery.location);
    }
    if (!publish_native(checked, records, error)) {
        maintenance_starts = old_starts;
        preparations = old_preparations;
        return false;
    }
    // Publication precedes transport revocation and the fresh admission. The
    // prior process was already proved dead; queued old credentials cannot
    // reach the newly constructed Session.
    fence_stale_locations();
    local_processes.erase(recovery.location);
    open_location_session(recovery.location, runtime, session->shared_content());
    location_sessions[recovery.location] = std::move(runtime);
    error.clear();
    return true;
}

bool Coordinator::recover_native_transfer_worker(uint64_t id, uint64_t expected_revision, bool source,
                                                 uint64_t replacement_worker, uint64_t replacement_generation,
                                                 std::string& error) {
    auto& impl = *impl_;
    const auto found = impl.native_transfers.find(id);
    const auto operation = source ? 100u : 101u;
    if (impl.config.offline || !impl.store || !expected_revision || expected_revision == UINT64_MAX ||
        found == impl.native_transfers.end() || !replacement_worker) {
        error = "native_transfer_recovery_request_invalid";
        return false;
    }
    const auto& old = found->second;
    const auto& previous = old.state;
    const auto location = source ? old.preparation.source : old.preparation.destination;
    const auto old_worker = source ? previous.source_worker : previous.destination_worker;
    const auto old_generation = source ? previous.source_generation : previous.destination_generation;
    if (old.control_op == operation && old.control_expected == expected_revision &&
        impl.store->revision() == expected_revision + 1 && old_worker == replacement_worker &&
        old_generation == replacement_generation) {
        error.clear();
        return true;
    }
    const bool terminal = terminal_native_phase(previous.phase);
    const bool saved_secondary = source && previous.ownership_committed && previous.source_receipt.source_slot == 2 &&
                                 old.preparation.checkpoint_revision == impl.checkpoint_revision &&
                                 impl.checkpoint_stale_secondary(location, old.preparation.player);
    const Impl::WorkerRecovery recovery{
        id, source, location, old_worker, old_generation, replacement_worker, replacement_generation};
    const auto lease = impl.locations.location_leases().find(location);
    DWORD exit_code = STILL_ACTIVE;
    if (!impl.worker_recovery_admitted(recovery, expected_revision, old,
                                       lease == impl.locations.location_leases().end() ? nullptr : &lease->second,
                                       terminal, saved_secondary, exit_code, error)) {
        return false;
    }
    const auto& pinned = source ? old.source_process : old.destination_process;
    auto records = impl.native_transfers;
    auto& record = records.at(id);
    auto& state = record.state;
    LocationLedger candidate = impl.locations;
    auto pending_preparations = impl.preparations;
    auto pending_starts = impl.maintenance_starts;
    if (terminal &&
        !Impl::abandon_later_attempts(recovery, records, candidate, pending_preparations, pending_starts, error)) {
        return false;
    }
    const bool checkpoint_only = source && state.phase == NativeTransferPhase::fence_requested;
    if (!impl.replace_worker_in_ledger(recovery, checkpoint_only, lease->second.active, candidate, error)) {
        return false;
    }
    auto& death = source ? record.source_exit : record.destination_exit;
    death = Impl::ProcessExit{pinned, old_worker, old_generation, exit_code};
    (source ? record.source_process : record.destination_process) = {};
    (source ? state.source_worker : state.destination_worker) = replacement_worker;
    (source ? state.source_generation : state.destination_generation) = replacement_generation;
    state.avatar_generation = candidate.avatar(record.preparation.player)->generation;
    record.control_op = operation;
    record.control_expected = expected_revision;
    state.last_receipt = {};
    const bool maintenance = Impl::phase_after_recovery(state, source, checkpoint_only, saved_secondary, exit_code);
    return impl.install_worker_recovery(recovery, maintenance, lease->second.native_load_slot, candidate, records,
                                        pending_preparations, pending_starts, error);
}
