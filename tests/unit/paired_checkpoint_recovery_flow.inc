// HOST/FIXTURE: the selected native Save is synthetic. Actual TLS/process
// handles prove coordinator ordering; original three-noun retirement is separate.
void paired_saved_secondary_recovery(Coordinator& host, Peer& resident, TransferPreparation home) {
    std::string error;
    Peer traveler;
    std::array<Peer*, 2> links{&resident, &traveler};
    std::array<std::vector<Packet>, 2> received;
    const auto pump = [&]() {
        CoordinatorEvent event;
        while (host.poll(event)) {}
        for (size_t index = 0; index < links.size(); ++index) {
            Event next;
            while (links[index]->poll(next)) {
                if (next.kind == EventKind::packet) {
                    received[index].push_back(next.packet);
                }
            }
        }
    };
    const auto until = [&](const std::function<bool()>& ready, const char* message) {
        const auto end = GetTickCount64() + 5000;
        while (GetTickCount64() < end) {
            pump();
            if (ready()) {
                return;
            }
            Sleep(1);
        }
        check(false, message);
    };
    const auto command = [&](size_t index, TransferOp op) {
        Packet value;
        bool found = false;
        until(
            [&] {
                auto& packets = received[index];
                for (auto it = packets.begin(); it != packets.end(); ++it) {
                    if (it->kind == Kind::transfer_command && it->transfer.op == op) {
                        value = *it;
                        packets.erase(it);
                        found = true;
                        break;
                    }
                }
                return found;
            },
            "paired checkpoint recovery command delivered over authenticated TLS");
        return value;
    };
    const auto phase = [&](NativeTransferPhase expected) {
        until([&] { return host.native_transfer(home.id)->phase == expected; },
              "paired checkpoint recovery reaches the exact authenticated native phase");
    };
    const auto baseline = [&](size_t index, uint64_t scene, uint32_t owner, uint64_t sequence = 1) {
        Packet value;
        value.kind = Kind::scene_begin;
        value.scene = scene;
        value.baseline = sequence;
        value.count = 1;
        check(links[index]->send(value), "recovered single-owner baseline begins");
        value.kind = Kind::entity;
        value.entity = actor(owner == 1 ? 101 : 202, owner);
        check(links[index]->send(value), "recovered current resident enters baseline");
        value.kind = Kind::scene_end;
        check(links[index]->send(value), "recovered resident baseline ends");
    };
    WorkerProcess old_resident, home_process;
    check(host.register_local_worker_process(home.source, home.source_worker, home.source_worker_generation,
                                             old_resident.process.dwProcessId, old_resident.created, error),
          "fresh published paired worker registered while alive");
    PeerConfig config;
    check(host.peer_config_at(Role::authority, 0, home.destination, config, error) &&
              config.maintenance_transfer == home.id,
          "return primary starts only in explicit maintenance");
    config.maintenance_scene = 2;
    check(traveler.start(config, error), "return primary maintenance peer starts");
    until(
        [&] {
            return std::any_of(received[1].begin(), received[1].end(),
                               [](const Packet& p) { return p.kind == Kind::welcome; });
        },
        "return primary maintenance welcomed");
    check(host.register_local_worker_process(home.destination, home.destination_worker,
                                             home.destination_worker_generation, home_process.process.dwProcessId,
                                             home_process.created, error),
          "return primary original-image process registered");
    check(host.begin_native_transfer(home.id, host.durable_revision(), 2, 1, error),
          "published secondary begins return to original primary");
    auto exported = paused_receipt(command(0, TransferOp::source_fence), old_resident);
    const std::vector<uint8_t> bytes{8, 4, 0, 1};
    exported.transfer.artifact_bytes = static_cast<uint32_t>(bytes.size());
    check(content_digest(bytes.data(), bytes.size(), exported.transfer.artifact), "new return artifact fixture hashed");
    seal_receipt(exported);
    check(resident.send(exported), "published secondary measured export fixture sent");
    phase(NativeTransferPhase::source_fenced);
    check(host.bind_native_transfer_artifact(home.id, host.durable_revision(), bytes, approval_for(exported.transfer),
                                             error) &&
              host.request_native_transfer(home.id, host.durable_revision(), TransferOp::destination_restore, error),
          "return artifact bound before original-home restoration");
    auto restored = paused_receipt(command(1, TransferOp::destination_restore), home_process);
    seal_receipt(restored);
    check(traveler.send(restored), "return primary restored receipt sent");
    phase(NativeTransferPhase::destination_restored);
    check(host.commit_native_transfer(home.id, host.durable_revision(), error) &&
              host.request_native_transfer(home.id, host.durable_revision(), TransferOp::destination_activate, error),
          "return ownership commits before primary resumes");
    auto active = paused_receipt(command(1, TransferOp::destination_activate), home_process);
    active.transfer.pause_before = 1;
    active.transfer.pause_after = 0;
    ++active.transfer.ai_end;
    seal_receipt(active);
    check(traveler.send(active), "return primary resumed receipt sent");
    phase(NativeTransferPhase::destination_active);
    baseline(1, 2, 1);
    check(host.request_native_transfer(home.id, host.durable_revision(), TransferOp::source_cleanup, error),
          "initial secondary removal requested");
    const auto pending_cleanup = command(0, TransferOp::source_cleanup);
    home_process.close();
    traveler.stop();
    received[1].clear();
    pump();
    const auto pending_revision = host.durable_revision();
    check(!host.recover_native_transfer_worker(home.id, pending_revision, false, home.destination_worker,
                                               home.destination_worker_generation + 1, error) &&
              error == "native_transfer_source_cleanup_pending" && host.durable_revision() == pending_revision &&
              host.native_transfer(home.id)->phase == NativeTransferPhase::cleanup_requested &&
              host.native_transfer(home.id)->destination_generation == home.destination_worker_generation,
          "dead destination recovery preserves the live source pending removal and durable lease");
    auto original_cleanup = paused_receipt(pending_cleanup, old_resident);
    original_cleanup.transfer.native_player = original_cleanup.transfer.native_avatar =
        original_cleanup.transfer.remaining_imports = 0;
    original_cleanup.transfer.pause_before = 1;
    original_cleanup.transfer.pause_after = 0;
    ++original_cleanup.transfer.ai_end;
    seal_receipt(original_cleanup);
    check(resident.send(original_cleanup),
          "surviving original source completes its pending removal after destination death");
    phase(NativeTransferPhase::source_cleaned);
    baseline(0, 1, 2, 2);
    check(host.recover_native_transfer_worker(home.id, host.durable_revision(), false, home.destination_worker,
                                              home.destination_worker_generation + 1, error),
          "destination recovery proceeds once the source removal receipt is durable");
    WorkerProcess recovered_home;
    check(host.peer_config_at(Role::authority, 0, home.destination, config, error) &&
              config.maintenance_transfer == home.id,
          "recovered destination requires restoration maintenance");
    config.maintenance_scene = 4;
    check(traveler.start(config, error), "replacement destination maintenance starts");
    until(
        [&] {
            return std::any_of(received[1].begin(), received[1].end(),
                               [](const Packet& p) { return p.kind == Kind::welcome; });
        },
        "replacement destination welcomed");
    check(host.register_local_worker_process(home.destination, home.destination_worker,
                                             home.destination_worker_generation + 1, recovered_home.process.dwProcessId,
                                             recovered_home.created, error) &&
              host.request_native_transfer(home.id, host.durable_revision(), TransferOp::destination_restore, error),
          "replacement destination receives retained return artifact");
    restored = paused_receipt(command(1, TransferOp::destination_restore), recovered_home);
    seal_receipt(restored);
    check(traveler.send(restored), "replacement destination restores current player");
    phase(NativeTransferPhase::committed);
    check(host.request_native_transfer(home.id, host.durable_revision(), TransferOp::destination_activate, error),
          "replacement destination resumes committed player");
    active = paused_receipt(command(1, TransferOp::destination_activate), recovered_home);
    active.transfer.pause_before = 1;
    active.transfer.pause_after = 0;
    ++active.transfer.ai_end;
    seal_receipt(active);
    check(traveler.send(active), "replacement destination sends native activation witness");
    phase(NativeTransferPhase::destination_active);
    baseline(1, 4, 1);
    until([&] { return host.finish_native_transfer(home.id, host.durable_revision(), error); },
          "deferred destination recovery preserves the surviving resident baseline");
    const auto traveler_generation = host.locations().avatar(1)->generation;
    std::unique_ptr<WorkerProcess> replacement;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        WorkerProcess& prior = attempt ? *replacement : old_resident;
        const auto generation = host.native_transfer(home.id)->source_generation;
        prior.close();
        resident.stop();
        received[0].clear();
        pump();
        check(!host.replace_disconnected_location(home.source, home.source_worker, generation, home.source_worker,
                                                  generation + 1, host.durable_revision(), error) &&
                  error == "location_replacement_requires_saved_secondary_cleanup",
              "ordinary replacement cannot expose the saved but departed secondary");
        const auto revision = host.durable_revision();
        check(
            host.recover_native_transfer_worker(home.id, revision, true, home.source_worker, generation + 1, error) &&
                host.recover_native_transfer_worker(home.id, revision, true, home.source_worker, generation + 1, error),
            "saved-secondary source death grants exactly one maintenance replacement");
        check(host.native_transfer(home.id)->phase == NativeTransferPhase::destination_active &&
                  host.locations().avatar(1)->active && host.locations().avatar(1)->location == home.destination &&
                  host.locations().avatar(1)->generation == traveler_generation,
              "source repair preserves current traveler ownership and generation");
        check(!host.finish_native_transfer(home.id, host.durable_revision(), error),
              "source death alone cannot prove stale saved-secondary cleanup");
        replacement = std::make_unique<WorkerProcess>();
        check(host.peer_config_at(Role::authority, 0, home.source, config, error) &&
                  config.maintenance_transfer == home.id && config.resident_owners == 6 &&
                  config.worker_generation == generation + 1,
              "saved pair is admitted only through the authenticated maintenance channel");
        config.maintenance_scene = 3 + attempt;
        check(resident.start(config, error), "fresh stale-pair cleanup maintenance peer starts");
        until(
            [&] {
                return std::any_of(received[0].begin(), received[0].end(),
                                   [](const Packet& p) { return p.kind == Kind::welcome; });
            },
            "fresh source cleanup maintenance welcomed");
        check(host.register_local_worker_process(home.source, home.source_worker, generation + 1,
                                                 replacement->process.dwProcessId, replacement->created, error),
              "higher-generation cleanup process is registered under its actual identity");
        Packet forbidden;
        forbidden.kind = Kind::scene_begin;
        forbidden.scene = config.maintenance_scene;
        forbidden.baseline = 1;
        forbidden.count = 2;
        const auto before = received[0].size();
        check(resident.send(forbidden), "stale paired ordinary baseline challenged before native cleanup");
        until([&] { return received[0].size() > before && received[0].back().kind == Kind::reject; },
              "maintenance rejects ordinary publication of stale saved residents");
        check(host.request_native_transfer(home.id, host.durable_revision(), TransferOp::source_cleanup, error),
              "fresh source receives explicit original secondary-removal command");
        auto cleanup_command = command(0, TransferOp::source_cleanup);
        check(cleanup_command.baseline == 0, "fresh native cleanup is correlated to baseline-zero maintenance");
        auto cleaned = paused_receipt(cleanup_command, *replacement);
        cleaned.transfer.native_player = cleaned.transfer.native_avatar = cleaned.transfer.remaining_imports = 0;
        cleaned.transfer.pause_before = 1;
        cleaned.transfer.pause_after = 0;
        ++cleaned.transfer.ai_end;
        seal_receipt(cleaned);
        auto stale = cleaned;
        stale.transfer.worker_generation = generation;
        seal_receipt(stale);
        const auto rejected = received[0].size();
        check(resident.send(stale), "old-generation cleanup receipt challenged");
        until([&] { return received[0].size() > rejected && received[0].back().kind == Kind::reject; },
              "old source generation cannot acknowledge new native retirement");
        check(host.native_transfer(home.id)->phase == NativeTransferPhase::cleanup_requested,
              "invalid cleanup leaves current worker fenced");
        check(resident.send(cleaned), "actual replacement scoped native retirement fixture sent");
        phase(NativeTransferPhase::source_cleaned);
        check(!host.finish_native_transfer(home.id, host.durable_revision(), error) &&
                  error == "native_transfer_recovered_source_baseline_required",
              "actual cleanup receipt still requires the fresh resident baseline");
        baseline(0, config.maintenance_scene, 2);
        until([&] { return host.finish_native_transfer(home.id, host.durable_revision(), error); },
              "resident-only fresh baseline completes source recovery");
        check(host.native_transfer(home.id)->completed_once && host.native_transfer(home.id)->source_retired &&
                  host.native_transfer(home.id)->cleanup_receipt.worker_generation == generation + 1 &&
                  host.native_transfer(home.id)->cleanup_baseline == 0 &&
                  host.locations().avatar(1)->generation == traveler_generation,
              "terminal cleanup preserves historical retirement and the new actual maintenance witness");
    }
    traveler.stop();
    resident.stop();
}
