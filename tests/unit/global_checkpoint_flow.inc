// Synthetic archive/approval bytes exercise metadata publication only. The
// Python validator owns actual original Save and archive semantic evidence.
#include "paired_checkpoint_recovery_flow.inc"

void global_checkpoint_after_transfer(Coordinator& host, Peers& peers, WorkerProcess& owner_process,
                                      CoordinatorConfig config, const std::filesystem::path& directory) {
    std::string error;
    const auto expected = host.durable_revision();
    const auto selected = host.checkpoint_artifact();
    auto candidate = selected;
    state::Reader in{selected};
    in.word();
    in.word();
    for (size_t n = 0; n < 4 + world_file_count; ++n) {
        in.digest();
    }
    in.wide();
    in.word();
    for (unsigned n = 0; n < 4; ++n) {
        in.word();
    }
    const auto count = in.word();
    for (uint32_t n = 0; n < count; ++n) {
        const auto name = in.data(240);
        const auto hash_at = in.at;
        in.digest();
        const auto size = in.word(), data_at = in.at;
        std::vector<uint8_t> data(selected.begin() + data_at, selected.begin() + data_at + size);
        in.at += size;
        if (std::string(name.begin(), name.end()) == "spore/Games/Game0/Haagos.spo") {
            data[0] ^= 0x80;
            std::copy(data.begin(), data.end(), candidate.begin() + data_at);
            Digest hash{};
            check(content_digest(data.data(), data.size(), hash), "owner fixture member hashed");
            std::copy(hash.begin(), hash.end(), candidate.begin() + hash_at);
        }
    }
    CheckpointArtifact parsed;
    check(validate_checkpoint_artifact(candidate, parsed, error),
          "whole candidate fixture retains exact nonowner save");
    const std::vector<uint8_t> proof{'{', '"', 'h', 'o', 's', 't', '"', ':', 't', 'r', 'u', 'e', '}', '\n'};
    Digest proof_hash{}, selected_hash{};
    check(content_digest(proof.data(), proof.size(), proof_hash) &&
              content_digest(selected.data(), selected.size(), selected_hash),
          "publication fixture hashes pinned");
    std::vector<uint8_t> player_artifact, player_approval;
    check(host.native_transfer_artifact(1, player_artifact, player_approval),
          "latest player pair exists before native Save supersession");
    Digest player_hash{}, approval_hash{};
    check(content_digest(player_artifact.data(), player_artifact.size(), player_hash) &&
              content_digest(player_approval.data(), player_approval.size(), approval_hash),
          "current player head bytes independently pinned");
    state::Writer approval;
    approval.word(0x4730314d);
    approval.word(1);
    for (const auto& value : {host.universe(), selected_hash, parsed.digest, parsed.provenance, proof_hash}) {
        approval.digest(value);
    }
    for (auto number : {expected, host.checkpoint_revision(), uint64_t(12), uint64_t(9), destination.galaxy,
                        destination.star, destination.planet, destination.area, uint64_t(8301), uint64_t(1)}) {
        approval.wide(number);
    }
    approval.word(owner_process.process.dwProcessId);
    approval.word(6);
    approval.wide(owner_process.created);
    approval.wide(expected + 1);
    for (auto number : {111u, 202u, 211u, 301u, 684349579u, 685579644u, 2u, 1u}) {
        approval.word(number);
    }
    approval.wide(1);
    approval.digest(player_hash);
    approval.digest(approval_hash);
    approval.wide(0);
    approval.digest({});
    approval.digest({});
    for (auto number : {0x914013u, 1u, 2u, 3u}) {
        approval.word(number);
    }
    check(approval.bytes.size() == 464, "M10G fixture uses exact agreed wire-independent binary layout");
    check(!host.commit_location_checkpoint(candidate, approval.bytes, proof, expected, error) &&
              error == "global_checkpoint_stale_or_live",
          "live transports prevent native whole-head publication");
    WorkerProcess third;
    check(host.register_local_worker_process(unrelated, 13, 4, third.process.dwProcessId, third.created, error),
          "otherwise empty active worker also registers actual process identity");
    peers.stop();
    peers.until(
        [&] {
            host.commit_location_checkpoint(candidate, approval.bytes, proof, expected, error);
            return error == "global_checkpoint_worker_death_unproven";
        },
        "closed transports alone cannot prove the original workers stopped");
    owner_process.close(0);
    third.close(0);
    for (unsigned challenge = 0; challenge < 6; ++challenge) {
        auto wrong = approval.bytes;
        if (challenge == 0) {
            wrong[40] ^= 1; // prior immutable head
        }
        if (challenge == 1) {
            wrong[192] ^= 1; // global owner generation
        }
        if (challenge == 2) {
            wrong[256] ^= 1; // original process creation
        }
        if (challenge == 3) {
            wrong[312] ^= 1; // exact current player artifact
        }
        if (challenge == 4) {
            wrong[252] = 4; // paired Save boundary
        }
        if (challenge == 5) {
            for (unsigned n = 0; n < 8; ++n) {
                wrong[264 + n] = uint8_t(uint64_t(8301) >> (8 * n));
            }
        } // native request numbers may repeat across workers
        check(!host.commit_location_checkpoint(candidate, wrong, proof, expected, error) &&
                  host.durable_revision() == expected,
              "altered native owner/head/process/player approval cannot publish or retire recovery state");
    }
    const auto output = directory / L"global-control";
    check(std::filesystem::create_directory(output), "private global publication control output created");
    const auto write_bytes = [](const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.close();
        check(bool(file), "bounded global fixture input written");
    };
    write_bytes(output / L"global-checkpoint.smp", candidate);
    write_bytes(output / L"global-checkpoint.expected", approval.bytes);
    write_bytes(output / L"global-checkpoint-proof.json", proof);
    Digest envelope_hash{};
    check(content_digest(approval.bytes.data(), approval.bytes.size(), envelope_hash), "approval file pinned");
    const auto request = std::string("schema=1\nexpected_revision=") + std::to_string(expected) +
                         "\nartifact_sha256=" + hex(parsed.digest) + "\napproval_sha256=" + hex(envelope_hash) +
                         "\nproof_sha256=" + hex(proof_hash) + "\n";
    write(output / L"global-checkpoint.request", request);
    check(process_global_checkpoint_request(host, output, error) && error.empty() &&
              host.durable_revision() == expected + 1,
          "fixed administrator inputs atomically publish complete owner head with durable approval");
    check(host.checkpoint_revision() == expected + 1 && host.locations().checkpoints_share(expected + 1) &&
              host.locations().avatar(1)->generation == 4 && host.locations().avatar(2)->generation == 2 &&
              host.locations().location_leases().at(source).generation == 5 &&
              !host.locations().location_leases().at(source).active &&
              host.locations().location_leases().at(destination).generation == 10 &&
              host.locations().global_owner(GlobalSystem::economy)->generation == 10,
          "whole-head rotation fences active workers/avatars while preserving inactive home retirement");
    check(!host.recovery_artifact_transfer(1) && !host.native_transfer_artifact(1, player_artifact, player_approval) &&
              host.native_transfer(1)->artifact_checkpoint == expected + 1 && host.native_transfer(1)->source_retired,
          "verified paired Save supersedes player bytes while retaining the original source retirement proof");
    check(host.commit_location_checkpoint(candidate, approval.bytes, proof, expected, error),
          "uncertain whole-head acknowledgement retries exactly");
    auto changed = approval.bytes;
    changed[264] ^= 1;
    check(!host.commit_location_checkpoint(candidate, changed, proof, expected, error),
          "same candidate bytes cannot legitimize an altered publication request");
    PeerConfig fresh;
    check(host.peer_config_at(Role::authority, 0, destination, fresh, error) && fresh.resident_owners == 6 &&
              !fresh.maintenance_transfer && fresh.worker_generation == 10 && fresh.identity == parsed.identity,
          "fresh paired reader receives only the newly selected immutable head");
    host.stop();
    config.offline = false;
    Coordinator reopened;
    check(reopened.start(config, error) && reopened.checkpoint_revision() == expected + 1 &&
              reopened.native_transfer(1)->artifact_checkpoint == expected + 1 &&
              !reopened.recovery_artifact_transfer(1) &&
              reopened.commit_location_checkpoint(candidate, approval.bytes, proof, expected, error),
          "schema9 reopen preserves supersession provenance and exact publication retry");
    check(reopened.peer_config_at(Role::authority, 0, destination, fresh, error) && fresh.resident_owners == 6 &&
              !fresh.maintenance_transfer,
          "historical transfer receipts do not force restoration of superseded player state");
    Peer native;
    check(native.start(fresh, error), "fresh paired HOST authority connects after durable world publication");
    bool welcomed = false;
    const auto end = GetTickCount64() + 5000;
    while (GetTickCount64() < end && !welcomed) {
        CoordinatorEvent event;
        while (reopened.poll(event)) {}
        Event incoming;
        while (native.poll(incoming)) {
            if (incoming.kind == EventKind::packet && incoming.packet.kind == Kind::welcome) {
                welcomed = true;
            }
        }
        Sleep(1);
    }
    check(welcomed, "fresh selected-head authority authenticates");
    Packet baseline;
    baseline.kind = Kind::scene_begin;
    baseline.scene = 1;
    baseline.baseline = 1;
    baseline.count = 2;
    check(native.send(baseline), "fresh paired baseline starts");
    baseline.kind = Kind::entity;
    baseline.entity = actor(301, 1);
    check(native.send(baseline), "fresh paired traveler baseline");
    baseline.entity = actor(202, 2);
    check(native.send(baseline), "fresh paired resident baseline");
    baseline.kind = Kind::scene_end;
    check(native.send(baseline), "fresh paired baseline ends");
    auto home = *reopened.transfer_preparation(1);
    home.id = 2;
    home.expected_revision = reopened.durable_revision();
    home.checkpoint_revision = reopened.checkpoint_revision();
    home.checkpoint_hash = parsed.digest;
    home.avatar_generation = reopened.locations().avatar(1)->generation;
    home.source = destination;
    home.destination = source;
    home.source_worker = 12;
    home.source_worker_generation = 10;
    home.destination_worker = 11;
    home.destination_worker_generation = 6;
    bool reserved = false;
    const auto deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline && !reserved) {
        CoordinatorEvent event;
        while (reopened.poll(event)) {}
        reserved = reopened.prepare_transfer_maintenance(home, error);
        Sleep(1);
    }
    check(reserved, "return home still uses retained retirement evidence after Save superseded the player artifact");
    paired_saved_secondary_recovery(reopened, native, home);
    native.stop();
    reopened.stop();
    config.offline = true;
    Coordinator recovered_cleanup;
    check(recovered_cleanup.start(config, error) &&
              recovered_cleanup.native_transfer(2)->phase == NativeTransferPhase::complete &&
              recovered_cleanup.native_transfer(2)->cleanup_baseline == 0 &&
              recovered_cleanup.native_transfer(2)->cleanup_receipt.worker_generation == 12,
          "durable reopen validates repeated higher-generation original secondary cleanup and terminal retirement "
          "history");
    recovered_cleanup.stop();
    std::vector<uint8_t> durable;
    {
        DurableStore store;
        check(store.open(config.state_directory, durable, error), "published whole-head receipt snapshot retained");
    }
    const auto approval_at = durable.size() - proof.size() - 4 - approval.bytes.size();
    for (unsigned challenge = 0; challenge < 3; ++challenge) {
        {
            DurableStore store;
            std::vector<uint8_t> previous;
            check(store.open(config.state_directory, previous, error),
                  "checksummed publication corruption writer opens");
            auto wrong = durable;
            if (challenge == 0) {
                wrong[approval_at + 72] ^= 1;
            }
            if (challenge == 1) {
                wrong.back() ^= 1;
            }
            if (challenge == 2) {
                wrong[approval_at + 168] ^= 1;
            }
            check(store.commit(wrong, error),
                  "malformed current checkpoint approval/proof published with valid outer checksum");
        }
        config.offline = true;
        Coordinator malformed;
        check(!malformed.start(config, error) && error == "global_checkpoint_receipt_invalid",
              "durable recovery refuses altered candidate proof bytes or publication revision");
    }
}
