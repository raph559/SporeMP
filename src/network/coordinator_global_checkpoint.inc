// Local qualified worker host publication. Whole original owner output only;
// native archive semantics are checked by the reviewed M10G producer validator.

// The approval must be for this revision and universe, and its proof must hash as approved. The artifact must be
// the candidate it approves, for this build, executable and content and for the universe's two games.
bool Coordinator::Impl::decode_checkpoint_request(const std::vector<uint8_t>& candidate_artifact,
                                                  const std::vector<uint8_t>& approval,
                                                  const std::vector<uint8_t>& proof, uint64_t expected_revision,
                                                  GlobalCheckpointApproval& approved, CheckpointArtifact& checkpoint,
                                                  std::string& error) const {
    Digest proof_hash{};
    if (config.offline || !store || !session || !location_mode || !expected_revision ||
        expected_revision == UINT64_MAX || !decode_global_checkpoint_approval(approval, approved) ||
        approved.expected != expected_revision || proof.empty() || proof.size() > max_checkpoint_proof_bytes ||
        !content_digest(proof.data(), proof.size(), proof_hash) || proof_hash != approved.proof) {
        error = "global_checkpoint_request_invalid";
        return false;
    }
    if (!validate_checkpoint_artifact(candidate_artifact, checkpoint, error)) {
        return false;
    }
    if (checkpoint.schema != 2 || checkpoint.digest != approved.candidate ||
        checkpoint.provenance != approved.provenance || approved.universe != universe ||
        checkpoint.identity.build != config.session.identity.build ||
        checkpoint.identity.executable != config.session.identity.executable ||
        checkpoint.identity.content != config.session.identity.content || approved.games != original_games ||
        checkpoint.native_ids != std::array<uint32_t, 4>{original_games[0], original_games[1], 0, 0}) {
        error = "global_checkpoint_identity_mismatch";
        return false;
    }
    return true;
}

// The checkpoint replaces the selected one only while nothing is live: no connection, no pending transfer, and
// every worker closed, with the approved owner of the location and of all global systems having exited cleanly.
bool Coordinator::Impl::location_checkpoint_admitted(const GlobalCheckpointApproval& approved,
                                                     const CheckpointArtifact& checkpoint, uint64_t expected_revision,
                                                     std::string& error) const {
    if (expected_revision != store->revision() || approved.selected != checkpoint_hash ||
        approved.checkpoint != checkpoint_revision || checkpoint.digest == checkpoint_hash || !preparations.empty() ||
        !maintenance_starts.empty() || !channels.empty()) {
        error = "global_checkpoint_stale_or_live";
        return false;
    }
    for (const auto& entry : native_transfers) {
        if (!terminal_native_phase(entry.second.state.phase)) {
            error = "global_checkpoint_transfer_active";
            return false;
        }
    }
    const auto owner = locations.location_leases().find(approved.location);
    if (owner == locations.location_leases().end() || !owner->second.active || owner->second.native_load_slot != 1 ||
        owner->second.worker != approved.worker || owner->second.generation != approved.generation) {
        error = "global_checkpoint_owner_lease";
        return false;
    }
    for (const auto system : global_systems) {
        const auto* global = locations.global_owner(system);
        if (!global || global->worker != approved.worker || global->generation != approved.generation) {
            error = "global_checkpoint_global_owner_mismatch";
            return false;
        }
    }
    // Every active authority must have been registered while alive. A socket
    // closure or externally supplied PID does not stand in for process death.
    for (const auto& entry : locations.location_leases()) {
        if (entry.second.active) {
            const auto process = local_processes.find(entry.first);
            DWORD code = STILL_ACTIVE;
            if (process == local_processes.end() || process->second->worker != entry.second.worker ||
                process->second->generation != entry.second.generation ||
                !process_exited(process->second->handle, code)) {
                error = "global_checkpoint_worker_death_unproven";
                return false;
            }
            if (entry.first == approved.location &&
                (process->second->pid != approved.pid || process->second->created != approved.created || code != 0)) {
                error = "global_checkpoint_owner_exit_or_identity";
                return false;
            }
        }
    }
    return true;
}

// Both players are saved at the approved location. The latest transfer artifact of each must be the one the
// approval names; the new checkpoint supersedes it, so the record gives up its bytes.
bool Coordinator::Impl::supersede_player_heads(const GlobalCheckpointApproval& approved, uint64_t revision,
                                               std::map<uint64_t, NativeRecord>& records, std::string& error) const {
    for (size_t index = 0; index < 2; ++index) {
        const auto* avatar = locations.avatar(index + 1);
        if (!avatar || !avatar->active || !(avatar->location == approved.location)) {
            error = "global_checkpoint_saved_residents_mismatch";
            return false;
        }
        const auto head = recovery_artifact_transfer(index + 1);
        const auto& pinned = approved.heads[index];
        if (head != pinned.transfer) {
            error = "global_checkpoint_latest_player_head_mismatch";
            return false;
        }
        if (head) {
            auto& record = records.at(head);
            Digest digest{};
            if (record.state.source_receipt.artifact != pinned.artifact ||
                !content_digest(record.approval.data(), record.approval.size(), digest) || digest != pinned.approval) {
                error = "global_checkpoint_latest_player_head_mismatch";
                return false;
            }
            record.artifact.clear();
            record.approval.clear();
            record.state.artifact_checkpoint = revision;
        }
    }
    return true;
}

bool Coordinator::Impl::nonowner_members_kept(const std::vector<uint8_t>& candidate_artifact,
                                              std::string& error) const {
    const auto previous_members = checkpoint_members(artifact), next_members = checkpoint_members(candidate_artifact);
    const auto home = "spore/Games/Game0/Satiria.spo";
    if (previous_members.at(home) != next_members.at(home)) {
        error = "global_checkpoint_nonowner_save_changed";
        return false;
    }
    for (const auto& member : previous_members) {
        if (!next_members.count(member.first)) {
            error = "global_checkpoint_selected_member_missing";
            return false;
        }
    }
    return true;
}

// The ledger after the checkpoint: the approved event is claimed for the economy's owner and the checkpoint
// revision rotates.
bool Coordinator::Impl::rotated_ledger(const GlobalCheckpointApproval& approved, uint64_t revision,
                                       LocationLedger& ledger, LocationLedger& checked, std::string& error) const {
    ledger = locations;
    if (!ledger.claim_global_event(GlobalSystem::economy, approved.worker, approved.generation, approved.event,
                                   error) ||
        !ledger.rotate_checkpoint(checkpoint_revision, revision, error)) {
        return false;
    }
    if (!checked.restore(ledger.snapshot(), error)) {
        return false;
    }
    return true;
}

// A fresh admission for every active location under the new checkpoint. Their sessions are opened by the caller.
bool Coordinator::Impl::checkpoint_runtimes(const LocationLedger& ledger, const Identity& checkpoint_identity,
                                            uint64_t revision, std::map<LocationId, LocationRuntime>& runtimes,
                                            std::string& error) const {
    for (const auto& entry : ledger.location_leases()) {
        if (entry.second.active) {
            Impl::LocationRuntime runtime;
            runtime.config = config.session;
            runtime.config.identity = checkpoint_identity;
            runtime.config.location = entry.first;
            runtime.config.worker_generation = entry.second.generation;
            runtime.config.checkpoint_revision = revision;
            runtime.config.native_load_slot = entry.second.native_load_slot;
            runtime.config.maintenance_transfer = 0;
            runtime.config.permitted_owners = resident_owner_mask(ledger, entry.first);
            runtime.config.content_authority = owns_content(ledger, entry.second.worker, entry.second.generation);
            if (!randomize_location_admission(runtime.config)) {
                error = "global_checkpoint_admission_random";
                return false;
            }
            runtimes.emplace(entry.first, std::move(runtime));
        }
    }
    return true;
}

bool Coordinator::commit_location_checkpoint(const std::vector<uint8_t>& artifact, const std::vector<uint8_t>& approval,
                                             const std::vector<uint8_t>& proof, uint64_t expected_revision,
                                             std::string& error,
                                             const std::function<void(const char*)>& test_boundary) {
    auto& impl = *impl_;
    GlobalCheckpointApproval approved;
    CheckpointArtifact checkpoint;
    if (!impl.decode_checkpoint_request(artifact, approval, proof, expected_revision, approved, checkpoint, error)) {
        return false;
    }
    if (impl.store->revision() == expected_revision + 1 && impl.checkpoint_revision == expected_revision + 1 &&
        artifact == impl.artifact && approval == impl.checkpoint_approval && proof == impl.checkpoint_proof) {
        error.clear();
        return true;
    }
    if (!impl.location_checkpoint_admitted(approved, checkpoint, expected_revision, error)) {
        return false;
    }
    auto records = impl.native_transfers;
    LocationLedger ledger, checked;
    if (!impl.supersede_player_heads(approved, expected_revision + 1, records, error) ||
        !impl.nonowner_members_kept(artifact, error) ||
        !impl.rotated_ledger(approved, expected_revision + 1, ledger, checked, error)) {
        return false;
    }
    auto root_session = impl.session_for_world(checkpoint.identity.world, error);
    if (!root_session) {
        return false;
    }
    std::map<LocationId, Impl::LocationRuntime> runtimes;
    if (!impl.checkpoint_runtimes(ledger, checkpoint.identity, expected_revision + 1, runtimes, error)) {
        return false;
    }
    auto previous = impl.checkpoint_head();
    impl.config.session.identity = checkpoint.identity;
    impl.config.session.checkpoint_revision = expected_revision + 1;
    impl.locations = std::move(checked);
    impl.native_transfers = records;
    impl.artifact = artifact;
    impl.checkpoint_approval = approval;
    impl.checkpoint_proof = proof;
    impl.checkpoint_revision = expected_revision + 1;
    impl.checkpoint_hash = checkpoint.digest;
    impl.secondary_save = checkpoint.secondary_save;
    for (auto& entry : runtimes) {
        impl.open_location_session(entry.first, entry.second, root_session->shared_content());
    }
    if (!impl.store->commit(impl.persisted(root_session->content_snapshot()), error, test_boundary)) {
        impl.restore_checkpoint_head(std::move(previous));
        return false;
    }
    impl.location_sessions = std::move(runtimes);
    impl.session = std::move(root_session);
    impl.local_processes.clear();
    impl.replacement_request.reset();
    impl.cancelled_preparation.reset();
    error.clear();
    return true;
}
