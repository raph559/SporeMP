// HOST/FIXTURE only: TLS peers and real disposable child process handles exercise
// coordinator gates. These scalar receipts do not claim original-game behavior.
struct WorkerProcess {
    PROCESS_INFORMATION process{};
    uint64_t created = 0;
    Digest executable{};

    WorkerProcess() {
        wchar_t module[32768]{};
        check(GetModuleFileNameW(nullptr, module, _countof(module)) != 0, "fixture executable located");
        std::ifstream image(std::filesystem::path(module), std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(image), std::istreambuf_iterator<char>()};
        check(!bytes.empty() && content_digest(bytes.data(), bytes.size(), executable),
              "fixture executable pinned by actual bytes");
        std::wstring command = L"\"" + std::wstring(module) + L"\" --worker-fixture";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        check(CreateProcessW(module, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &startup, &process) != FALSE,
              "disposable HOST worker process starts hidden");
        FILETIME start{}, exit{}, kernel{}, user{};
        check(GetProcessTimes(process.hProcess, &start, &exit, &kernel, &user) != FALSE,
              "process creation identity captured");
        created = (uint64_t(start.dwHighDateTime) << 32) | start.dwLowDateTime;
    }

    void close(uint32_t exit_code = 37) {
        if (process.hProcess && WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
            check(TerminateProcess(process.hProcess, exit_code) != FALSE &&
                      WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0,
                  "only the owned disposable HOST process is closed and observed dead");
        }
    }

    ~WorkerProcess() {
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
                TerminateProcess(process.hProcess, 38);
            }
            CloseHandle(process.hProcess);
            CloseHandle(process.hThread);
        }
    }
};

std::vector<uint8_t> approval_for(const TransferFrame& source_receipt) {
    std::vector<uint8_t> value(200);
    auto word = [&](size_t at, uint32_t number) {
        for (unsigned n = 0; n < 4; ++n) {
            value[at + n] = uint8_t(number >> (8 * n));
        }
    };
    word(0, 0x4630314d);
    word(4, 3);
    const auto hash = hex(source_receipt.artifact);
    std::copy(hash.begin(), hash.end(), value.begin() + 8);
    word(72, source_receipt.artifact_bytes);
    word(76, 0x00250003);
    word(80, source_receipt.source_player);
    word(84, source_receipt.source_avatar);
    word(88, source_receipt.unique_game_id);
    word(92, source_receipt.canonical_home);
    return value;
}

Packet native_command(Peers& peers, size_t index, TransferOp op) {
    peers.until(
        [&] {
            return std::any_of(peers.packets[index].begin(), peers.packets[index].end(), [&](const Packet& packet) {
                return packet.kind == Kind::transfer_command && packet.transfer.op == op;
            });
        },
        "authority receives request-correlated transfer command over TLS");
    for (auto it = peers.packets[index].rbegin(); it != peers.packets[index].rend(); ++it) {
        if (it->kind == Kind::transfer_command && it->transfer.op == op) {
            return *it;
        }
    }
    throw std::runtime_error("missing transfer command");
}

Packet paused_receipt(Packet command, const WorkerProcess& process) {
    command.kind = Kind::transfer_receipt;
    auto& frame = command.transfer;
    if (!frame.source_player) {
        frame.source_player = frame.source_slot == 1 ? 111 : 211;
    }
    frame.native_player = frame.native_slot == 1 ? 111 : 211;
    frame.native_avatar = frame.native_slot == 1 ? 101 : 301;
    frame.pause_request = frame.id;
    frame.input_epoch = 7;
    frame.app_begin = 100;
    frame.app_end = 300;
    frame.ai_begin = frame.ai_end = 55;
    frame.zero_deltas = 180;
    frame.nonzero_deltas = 0;
    frame.elapsed_ms = 3100;
    frame.pause_before = 0;
    frame.pause_after = 1;
    frame.remaining_imports = frame.native_slot == 2 ? 1 : 0;
    frame.process_id = process.process.dwProcessId;
    frame.process_created = process.created;
    return command;
}

void seal_receipt(Packet& packet) {
    check(
        transfer_witness_digest(packet.transfer, packet.session, packet.scene, packet.baseline, packet.transfer.state),
        "fixture receipt sealed using canonical serializer shared with native bridge");
}

void reconnect_maintenance(Peers& peers, size_t index, const LocationId& location, const WorkerProcess& process,
                           uint64_t worker, uint64_t generation, uint64_t id = 1) {
    peers.peers[index].stop();
    peers.packets[index].clear();
    PeerConfig config;
    std::string error;
    check(peers.host.peer_config_at(Role::authority, 0, location, config, error) && config.maintenance_transfer == id &&
              config.worker_generation == generation,
          "recovery issues an explicit maintenance-only higher-generation configuration");
    config.maintenance_scene = 2;
    check(peers.peers[index].start(config, error), "fresh loaded maintenance authority starts TLS");
    peers.until([&] { return peers.has(index, Kind::welcome); },
                "maintenance HELLO authenticates without an ordinary baseline");
    check(peers.host.register_local_worker_process(location, worker, generation, process.process.dwProcessId,
                                                   process.created, error),
          "replacement original-image process registered under higher worker generation");
    Packet ordinary;
    ordinary.kind = Kind::scene_begin;
    ordinary.scene = 2;
    ordinary.baseline = 1;
    ordinary.count = 0;
    check(peers.peers[index].send(ordinary), "ordinary baseline challenged before recovery restore");
    peers.until([&] { return peers.has(index, Kind::reject); },
                "maintenance startup refuses stale original baseline publication");
}

void terminal_restore(Coordinator& host, Peers& peers, WorkerProcess& previous, uint64_t id, bool source_side,
                      size_t index) {
    const auto preparation = *host.transfer_preparation(id);
    const auto before = *host.native_transfer(id);
    const auto location = source_side ? preparation.source : preparation.destination;
    const auto worker = source_side ? before.source_worker : before.destination_worker;
    const auto generation = (source_side ? before.source_generation : before.destination_generation) + 1;
    std::vector<uint8_t> artifact, approval;
    std::string error;
    check(host.native_transfer_artifact(id, artifact, approval) &&
              host.recovery_artifact_transfer(preparation.player) == id,
          "terminal record retains the latest immutable recovery artifact");
    const auto output =
        std::filesystem::current_path() / L"transfer-host" /
        (L"artifact-export-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    check(std::filesystem::create_directory(output), "durable recovery artifact export directory created");
    auto request = request_text(preparation, "artifact", host.durable_revision());
    request[7] = '2';
    const auto revision_before_export = host.durable_revision();
    write(output / L"transfer.request", request);
    check(process_transfer_request(host, output, error) && error.empty(),
          "admin exports the immutable stored pair without requiring a live original worker");
    const auto read_bytes = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::vector<uint8_t>{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    };
    const auto stem = L"transfer-" + std::to_wstring(id);
    check(read_bytes(output / (stem + L".db")) == artifact && read_bytes(output / (stem + L".expected")) == approval &&
              host.durable_revision() == revision_before_export,
          "fixed-path recovery export returns exact durable bytes without another ownership publication");
    write(output / L"transfer.request", request);
    check(process_transfer_request(host, output, error) && error.empty(),
          "exact recovery artifact export retries without overwriting files");
    write(output / (stem + L".db"), "altered");
    write(output / L"transfer.request", request);
    check(process_transfer_request(host, output, error) && error == "transfer_durable_artifact_write_conflict",
          "a conflicting output artifact is refused rather than silently replaced");
    check(!host.recover_native_transfer_worker(id, host.durable_revision(), source_side, worker, generation, error),
          "a live completed or aborted worker cannot be replaced");
    previous.close();
    const auto revision = host.durable_revision();
    check(host.recover_native_transfer_worker(id, revision, source_side, worker, generation, error) &&
              host.recover_native_transfer_worker(id, revision, source_side, worker, generation, error) &&
              !host.recover_native_transfer_worker(id, revision, source_side, worker + 100, generation, error),
          "terminal recovery publishes one exact higher-generation admission and rejects altered retry");
    WorkerProcess replacement;
    reconnect_maintenance(peers, index, location, replacement, worker, generation, id);
    const auto restore_op = source_side ? TransferOp::source_restore : TransferOp::destination_restore;
    check(host.request_native_transfer(id, host.durable_revision(), restore_op, error),
          "terminal artifact restoration receives a fresh authenticated challenge");
    auto restored = paused_receipt(native_command(peers, index, restore_op), replacement);
    seal_receipt(restored);
    check(peers.peers[index].send(restored), "terminal recovery native restoration fixture sent");
    peers.until(
        [&] {
            return host.native_transfer(id)->phase ==
                   (source_side ? NativeTransferPhase::source_restored : NativeTransferPhase::committed);
        },
        "latest artifact restoration preserves previously committed ownership");
    check(host.request_native_transfer(id, host.durable_revision(),
                                       source_side ? TransferOp::source_activate : TransferOp::destination_activate,
                                       error),
          "terminal recovery cannot release ordinary gameplay before original-state activation");
    auto activated = paused_receipt(
        native_command(peers, index, source_side ? TransferOp::source_activate : TransferOp::destination_activate),
        replacement);
    activated.transfer.pause_before = 1;
    activated.transfer.pause_after = 0;
    ++activated.transfer.ai_end;
    seal_receipt(activated);
    check(peers.peers[index].send(activated), "terminal recovered original-AI witness sent");
    peers.until(
        [&] {
            return host.native_transfer(id)->phase ==
                   (source_side ? NativeTransferPhase::aborted : NativeTransferPhase::destination_active);
        },
        "terminal recovered activation observed");
    Packet fresh;
    fresh.kind = Kind::scene_begin;
    fresh.scene = 2;
    fresh.baseline = 1;
    fresh.count = 1;
    check(peers.peers[index].send(fresh), "terminal recovered fresh baseline begins");
    fresh.kind = Kind::entity;
    fresh.entity = actor(restored.transfer.native_avatar, preparation.player);
    check(peers.peers[index].send(fresh), "terminal latest artifact avatar enters fresh baseline");
    fresh.kind = Kind::scene_end;
    check(peers.peers[index].send(fresh), "terminal recovered baseline ends");
    if (!source_side) {
        peers.until([&] { return host.finish_native_transfer(id, host.durable_revision(), error); },
                    "terminal recovery finishes without repeating already durable source retirement or cleanup");
    }
    std::vector<uint8_t> after_artifact, after_approval;
    check(host.native_transfer_artifact(id, after_artifact, after_approval) && after_artifact == artifact &&
              after_approval == approval && host.native_transfer(id)->avatar_generation > before.avatar_generation,
          "terminal recovery preserves exact latest bytes and increments the active avatar lease");
}

Digest return_home(Coordinator& host, Peers& peers, WorkerProcess& visiting_process, const TransferPreparation& outward,
                   bool cleanup_death = false, bool terminal_death = false) {
    std::string error;
    TransferPreparation home = outward;
    home.id = 2;
    home.expected_revision = host.durable_revision();
    home.avatar_generation = host.locations().avatar(1)->generation;
    home.source = destination;
    home.destination = source;
    home.source_worker = 12;
    home.source_worker_generation = 9;
    home.destination_worker = 11;
    home.destination_worker_generation = 6;
    check(host.prepare_transfer_maintenance(home, error) && host.prepare_transfer_maintenance(home, error),
          "retired home admits exactly one new maintenance worker and durable preparation retry");
    WorkerProcess home_process;
    peers.peers[0].stop();
    peers.packets[0].clear();
    PeerConfig config;
    check(host.peer_config_at(Role::authority, 0, source, config, error) && config.maintenance_transfer == 2 &&
              config.worker_generation == 6,
          "returning home worker cannot publish its stale primary before restoration");
    config.maintenance_scene = 2;
    check(peers.peers[0].start(config, error), "return-home maintenance HELLO starts");
    peers.until([&] { return peers.has(0, Kind::welcome); }, "returning home authenticates without baseline");
    check(host.register_local_worker_process(source, 11, 6, home_process.process.dwProcessId, home_process.created,
                                             error),
          "return-home process registered");
    check(host.begin_native_transfer(2, host.durable_revision(), 2, 1, error),
          "return exports selected native secondary to original primary home");
    auto source_proof = paused_receipt(native_command(peers, 1, TransferOp::source_fence), visiting_process);
    const std::vector<uint8_t> database{9, 9, 2, 7};
    source_proof.transfer.artifact_bytes = static_cast<uint32_t>(database.size());
    check(content_digest(database.data(), database.size(), source_proof.transfer.artifact),
          "return artifact changes its native serialized state");
    seal_receipt(source_proof);
    check(peers.peers[1].send(source_proof), "selected-secondary source receipt submitted");
    peers.until([&] { return host.native_transfer(2)->phase == NativeTransferPhase::source_fenced; },
                "selected secondary is durably fenced");
    check(host.bind_native_transfer_artifact(2, host.durable_revision(), database, approval_for(source_proof.transfer),
                                             error) &&
              host.request_native_transfer(2, host.durable_revision(), TransferOp::destination_restore, error),
          "return immutable pair bound before primary restore");
    auto restored = paused_receipt(native_command(peers, 0, TransferOp::destination_restore), home_process);
    seal_receipt(restored);
    check(restored.transfer.native_slot == 1 && restored.transfer.unique_game_id == 684349579u &&
              restored.transfer.canonical_home == source.planet,
          "original player and canonical home survive native slot remapping");
    check(peers.peers[0].send(restored), "restored original primary receipt submitted");
    peers.until([&] { return host.native_transfer(2)->phase == NativeTransferPhase::destination_restored; },
                "original-home restore remains provisional");
    check(host.commit_native_transfer(2, host.durable_revision(), error) &&
              host.locations().avatar(1)->location == source && host.locations().avatar(1)->generation == 5 &&
              host.request_native_transfer(2, host.durable_revision(), TransferOp::destination_activate, error),
          "return ownership commits at the next distinct avatar generation");
    auto active = paused_receipt(native_command(peers, 0, TransferOp::destination_activate), home_process);
    active.transfer.pause_before = 1;
    active.transfer.pause_after = 0;
    ++active.transfer.ai_end;
    seal_receipt(active);
    check(peers.peers[0].send(active), "original-home native activation submitted");
    peers.until([&] { return host.native_transfer(2)->phase == NativeTransferPhase::destination_active; },
                "original-home activation observed");
    Packet baseline;
    baseline.kind = Kind::scene_begin;
    baseline.scene = 2;
    baseline.baseline = 1;
    baseline.count = 1;
    check(peers.peers[0].send(baseline), "home fresh baseline starts");
    baseline.kind = Kind::entity;
    baseline.entity = actor(101, 1);
    check(peers.peers[0].send(baseline), "home restored primary appears in fresh baseline");
    baseline.kind = Kind::scene_end;
    check(peers.peers[0].send(baseline), "home fresh baseline ends");
    check(host.request_native_transfer(2, host.durable_revision(), TransferOp::source_cleanup, error),
          "secondary departure requires native source removal");
    if (cleanup_death) {
        visiting_process.close();
        check(host.recover_native_transfer_worker(2, host.durable_revision(), true, 12, 10, error) &&
                  host.native_transfer(2)->phase == NativeTransferPhase::source_cleaned,
              "observed process death during secondary cleanup publishes actual removal and permits progress");
        check(!host.finish_native_transfer(2, host.durable_revision(), error),
              "recovered source still requires a fresh resident baseline");
        peers.peers[1].stop();
        peers.packets[1].clear();
        PeerConfig resident;
        check(host.peer_config_at(Role::authority, 0, destination, resident, error) && !resident.maintenance_transfer &&
                  resident.worker_generation == 10 && peers.peers[1].start(resident, error),
              "resident reload admission excludes the departed secondary");
        peers.until([&] { return peers.has(1, Kind::welcome); }, "replacement resident authenticates");
        Packet fresh;
        fresh.kind = Kind::scene_begin;
        fresh.scene = 2;
        fresh.baseline = 1;
        fresh.count = 0;
        check(peers.peers[1].send(fresh), "empty resident fixture baseline begins");
        fresh.kind = Kind::scene_end;
        check(peers.peers[1].send(fresh), "empty resident fixture baseline completes");
        peers.until([&] { return host.finish_native_transfer(2, host.durable_revision(), error); },
                    "actual secondary process death plus fresh resident baseline completes cleanup without a "
                    "fabricated native receipt");
        return source_proof.transfer.state;
    }
    auto cleaned = paused_receipt(native_command(peers, 1, TransferOp::source_cleanup), visiting_process);
    cleaned.transfer.native_player = cleaned.transfer.native_avatar = cleaned.transfer.remaining_imports = 0;
    cleaned.transfer.pause_before = 1;
    cleaned.transfer.pause_after = 0;
    ++cleaned.transfer.ai_end;
    seal_receipt(cleaned);
    check(peers.peers[1].send(cleaned), "secondary removal and resident resume witness submitted");
    peers.until([&] { return host.native_transfer(2)->phase == NativeTransferPhase::source_cleaned; },
                "actual secondary removal receipt publishes source cleanup");
    check(host.finish_native_transfer(2, host.durable_revision(), error) &&
              host.native_transfer(2)->phase == NativeTransferPhase::complete &&
              host.locations().location_leases().at(destination).active &&
              WaitForSingleObject(visiting_process.process.hProcess, 0) == WAIT_TIMEOUT,
          "return completes without closing the still-active visiting location process");
    if (terminal_death) {
        terminal_restore(host, peers, home_process, 2, false, 0);
    }
    return source_proof.transfer.state;
}

#include "global_checkpoint_flow.inc"

void native_flow(unsigned recovery = 0) {
    const bool paired = recovery == 12;
    WorkerProcess source_process, destination_process;
    CoordinatorConfig config;
    config.port = 0;
    auto& identity = config.session.identity;
    identity.build = digest(31);
    identity.executable = source_process.executable;
    identity.content = digest(33);
    identity.fixture = digest(34);
    identity.world.fill(digest(34));
    const auto directory =
        std::filesystem::current_path() / L"transfer-host" /
        (L"native-phases-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    check(std::filesystem::create_directories(directory / L"state"), "native-phase HOST store created");
    config.state_directory = directory / L"state";
    std::string error;
    Coordinator created;
    check(created.start(config, error), "native-phase universe created");
    created.stop();
    config.offline = true;
    Coordinator sealed;
    check(sealed.start(config, error) && sealed.commit_checkpoint(checkpoint(identity), 1, error),
          "native-phase checkpoint fixture selected");
    check(sealed.commit_locations(
              2,
              [paired](LocationLedger& ledger, std::string& why) {
                  const bool initial = ledger.add_location({source, 11, 5, 2, digest(33), true, 0}, why) &&
                                       ledger.add_location({destination, 12, 9, 2, digest(33), true, 1}, why) &&
                                       ledger.add_location({unrelated, 13, 4, 2, digest(33), true, 0}, why) &&
                                       ledger.place_avatar(1, source, why) &&
                                       ledger.place_avatar(2, paired ? destination : unrelated, why) &&
                                       ledger.set_global_owner(GlobalSystem::economy,
                                                               paired ? GlobalLease{12, 9} : GlobalLease{13, 4}, why);
                  if (!initial || !paired) {
                      return initial;
                  }
                  return ledger.set_global_owner(GlobalSystem::diplomacy, {12, 9}, why) &&
                         ledger.set_global_owner(GlobalSystem::galaxy_ai, {12, 9}, why) &&
                         ledger.set_global_owner(GlobalSystem::disasters, {12, 9}, why);
              },
              error),
          "source owns no global producer lease");
    sealed.stop();
    config.offline = false;
    Coordinator host;
    check(host.start(config, error), "native-phase online host starts");
    Peers peers(host, paired);
    peers.start();
    peers.baselines();
    CheckpointArtifact selected;
    check(validate_checkpoint_artifact(host.checkpoint_artifact(), selected, error),
          "native-phase checkpoint identity decoded");
    TransferPreparation preparation;
    preparation.id = 1;
    preparation.player = 1;
    preparation.expected_revision = 3;
    preparation.checkpoint_revision = 2;
    preparation.avatar_generation = 1;
    preparation.source_worker = 11;
    preparation.source_worker_generation = 5;
    preparation.destination_worker = 12;
    preparation.destination_worker_generation = 9;
    preparation.source = source;
    preparation.destination = destination;
    preparation.universe = host.universe();
    preparation.checkpoint_hash = selected.digest;
    check(host.prepare_transfer(preparation, error), "native transfer reservation committed");
    check(!host.begin_native_transfer(1, 4, 1, 2, error), "unregistered native process cannot begin transfer");
    check(!host.register_local_worker_process(source, 11, 5, source_process.process.dwProcessId,
                                              source_process.created + 1, error),
          "creation-time mismatch is rejected");
    check(host.register_local_worker_process(source, 11, 5, source_process.process.dwProcessId, source_process.created,
                                             error) &&
              host.register_local_worker_process(destination, 12, 9, destination_process.process.dwProcessId,
                                                 destination_process.created, error),
          "exact original-image process handles registered");
    // Native capture and coordinator control run independently. Deliberately
    // queue a whole authority output frame without pumping the coordinator,
    // then begin maintenance before that frame reaches Session::receive.
    const auto source_packets_before = peers.packets[0].size();
    for (auto kind : {Kind::motion, Kind::entity, Kind::despawn, Kind::action_result}) {
        Packet late;
        late.kind = kind;
        late.scene = 1;
        late.baseline = 1;
        late.entity = actor(kind == Kind::entity ? 505 : 101, kind == Kind::entity ? 0 : 1);
        late.entity.tick = 901;
        late.request = 77;
        late.player = kind == Kind::action_result ? 1 : 0;
        check(peers.peers[0].send(late), "prior-baseline authority output queued before maintenance begins");
    }
    check(host.begin_native_transfer(1, 4, 1, 2, error) && host.durable_revision() == 5,
          "fence intent is durable before dispatch");
    check(host.begin_native_transfer(1, 4, 1, 2, error) && !host.begin_native_transfer(1, 4, 2, 2, error),
          "uncertain fence retry matches both native slots exactly");
    Packet future;
    future.kind = Kind::motion;
    future.scene = 1;
    future.baseline = 2;
    future.entity = actor(101, 1);
    future.entity.tick = 902;
    check(peers.peers[0].send(future), "unknown future baseline acts as ordered TLS barrier after old output");
    peers.until(
        [&] {
            return peers.packets[0].size() > source_packets_before && peers.packets[0].back().kind == Kind::reject &&
                   peers.packets[0].back().error == Error::stale;
        },
        "future authority baseline remains fenced after prior output drains");
    check(std::count_if(peers.packets[0].begin() + source_packets_before, peers.packets[0].end(),
                        [](const Packet& packet) { return packet.kind == Kind::reject; }) == 1,
          "only the future-baseline challenge is rejected; valid in-flight output cannot quarantine the pause");
    check(!peers.has(3, Kind::motion, 901) && !peers.has(3, Kind::despawn, 901) &&
              std::none_of(peers.packets[3].begin(), peers.packets[3].end(),
                           [](const Packet& packet) { return packet.entity.id == 505 || packet.request == 77; }),
          "drained authority motion spawn despawn and action result never reach subscribers");
    auto command = native_command(peers, 0, TransferOp::source_fence);
    auto receipt = paused_receipt(command, source_process);
    const std::vector<uint8_t> database{2, 7, 1, 8, 2, 8};
    receipt.transfer.artifact_bytes = static_cast<uint32_t>(database.size());
    check(content_digest(database.data(), database.size(), receipt.transfer.artifact), "closed fixture bytes hashed");
    seal_receipt(receipt);
    for (unsigned challenge = 0; challenge < 7; ++challenge) {
        auto wrong = receipt;
        if (challenge == 0) {
            ++wrong.transfer.ai_end;
        }
        if (challenge == 1) {
            wrong.transfer.elapsed_ms = 2999;
        }
        if (challenge == 2) {
            wrong.transfer.zero_deltas = 0;
        }
        if (challenge == 3) {
            ++wrong.transfer.process_created;
        }
        if (challenge == 4) {
            wrong.transfer.challenge[0] ^= 1;
        }
        if (challenge == 5) {
            ++wrong.transfer.worker_generation;
        }
        if (challenge == 6) {
            ++wrong.baseline;
        }
        seal_receipt(wrong);
        const auto before = peers.packets[0].size();
        check(peers.peers[0].send(wrong), "challenged receipt sent through real TLS");
        peers.until([&] { return peers.packets[0].size() > before && peers.packets[0].back().kind == Kind::reject; },
                    "invalid native witness rejected");
        check(host.durable_revision() == 5 && host.locations().avatar(1)->active,
              "invalid witness cannot revoke ownership or advance durable phase");
    }
    if (!paired) {
        peers.motion_and_action(51);
    }
    if (recovery == 1) {
        check(!host.recover_native_transfer_worker(1, 5, true, 11, 6, error) &&
                  error == "native_transfer_recovery_old_process_not_proven_dead",
              "a live original source cannot be replaced by a HOST boolean");
        source_process.close();
        check(host.recover_native_transfer_worker(1, 5, true, 11, 6, error) && host.durable_revision() == 6 &&
                  host.recover_native_transfer_worker(1, 5, true, 11, 6, error) &&
                  !host.recover_native_transfer_worker(1, 5, true, 11, 7, error),
              "pre-export death atomically retires the transfer and exact retry binds the replacement generation");
        check(host.native_transfer(1)->phase == NativeTransferPhase::checkpoint_recovered &&
                  host.locations().avatar(1)->active && host.locations().avatar(1)->generation == 2 &&
                  !host.locations().transfer(1),
              "checkpoint recovery never claims completed transfer or durable export");
        PeerConfig replacement;
        check(host.peer_config_at(Role::authority, 0, source, replacement, error) &&
                  replacement.worker_generation == 6 && !replacement.maintenance_transfer,
              "pre-export recovery requires normal selected-checkpoint baseline with fresh credential");
        peers.motion_and_action(52);
        peers.stop();
        host.stop();
        config.offline = true;
        Coordinator reopened;
        check(reopened.start(config, error) &&
                  reopened.native_transfer(1)->phase == NativeTransferPhase::checkpoint_recovered,
              "pre-export process-death and checkpoint fallback recover from schema7");
        reopened.stop();
        return;
    }
    check(peers.peers[0].send(receipt), "correlated paused-source receipt sent");
    peers.until([&] { return host.durable_revision() == 6; },
                "completed source receipt atomically fences durable avatar");
    check(!host.locations().avatar(1)->active && host.locations().avatar(1)->generation == 2 &&
              !host.cancel_transfer(preparation, 6, error),
          "native-fenced source requires native recovery rather than reservation cancellation");
    auto approval = approval_for(receipt.transfer), wrong_approval = approval;
    wrong_approval[88] ^= 1;
    if (recovery == 11) {
        source_process.close(); // Receipt/export completed before the source died; approval is still unbound.
    }
    check(!host.bind_native_transfer_artifact(1, 6, database, wrong_approval, error) && host.durable_revision() == 6,
          "wrong original player ID cannot bind artifact");
    check(host.bind_native_transfer_artifact(1, 6, database, approval, error) && host.durable_revision() == 7 &&
              host.bind_native_transfer_artifact(1, 6, database, approval, error) &&
              !host.bind_native_transfer_artifact(1, 6, database, wrong_approval, error),
          "closed byte pair is durable and exact-retry identity includes its full contents");
    if (recovery == 2 || recovery == 7 || recovery == 11) {
        source_process.close();
        check(host.recover_native_transfer_worker(1, 7, true, 11, 6, error) && host.durable_revision() == 8 &&
                  !host.locations().avatar(1)->active,
              "post-export recovery keeps the source avatar fenced under new worker lease");
        WorkerProcess replacement;
        reconnect_maintenance(peers, 0, source, replacement, 11, 6);
        check(host.request_native_transfer(1, 8, TransferOp::source_restore, error),
              "immutable artifact restored into fresh source maintenance authority");
        auto restore = paused_receipt(native_command(peers, 0, TransferOp::source_restore), replacement);
        seal_receipt(restore);
        check(peers.peers[0].send(restore), "fresh source measured restore receipt sent");
        peers.until([&] { return host.native_transfer(1)->phase == NativeTransferPhase::source_restored; },
                    "source restoration is provisional before abort commit");
        const auto revision = host.durable_revision();
        check(host.request_native_transfer(1, revision, TransferOp::source_activate, error) &&
                  host.locations().avatar(1)->active && host.locations().avatar(1)->generation == 3,
              "abort grants a higher source avatar generation before native resume");
        auto activate = paused_receipt(native_command(peers, 0, TransferOp::source_activate), replacement);
        activate.transfer.pause_before = 1;
        activate.transfer.pause_after = 0;
        ++activate.transfer.ai_end;
        seal_receipt(activate);
        check(peers.peers[0].send(activate), "recovered source original-AI advancement reported");
        peers.until([&] { return host.native_transfer(1)->phase == NativeTransferPhase::aborted; },
                    "native abort activation completes without destination ownership");
        if (recovery == 7) {
            terminal_restore(host, peers, replacement, 1, true, 0);
        }
        peers.motion_and_action(52);
        peers.stop();
        host.stop();
        config.offline = true;
        Coordinator reopened;
        check(reopened.start(config, error) && reopened.native_transfer(1)->phase == NativeTransferPhase::aborted &&
                  reopened.locations().avatar(1)->generation == (recovery == 7 ? 4u : 3u),
              "durable recovered source and native witness reopen coherently");
        reopened.stop();
        return;
    }
    check(host.request_native_transfer(1, 7, TransferOp::destination_restore, error) && host.durable_revision() == 8,
          "destination restore intent durable before delivery");
    command = native_command(peers, 1, TransferOp::destination_restore);
    auto restored = paused_receipt(command, destination_process);
    seal_receipt(restored);
    check(peers.peers[1].send(restored), "destination measured native-read fixture receipt sent");
    peers.until([&] { return host.durable_revision() == 9; }, "destination provisional receipt published");
    check(!host.locations().avatar(1)->active &&
              host.locations().transfer(1)->phase == TransferPhase::destination_restored,
          "provisional restoration never grants ownership before commit");
    std::unique_ptr<WorkerProcess> destination_replacement;
    const auto recover_destination = [&]() {
        const auto revision = host.durable_revision();
        destination_process.close();
        check(host.recover_native_transfer_worker(1, revision, false, 12, 10, error) &&
                  host.durable_revision() == revision + 1 &&
                  host.native_transfer(1)->phase == NativeTransferPhase::artifact_bound,
              "dead provisional or committed destination is replaced without stale native receipt");
        destination_replacement = std::make_unique<WorkerProcess>();
        reconnect_maintenance(peers, 1, destination, *destination_replacement, 12, 10);
        check(host.request_native_transfer(1, host.durable_revision(), TransferOp::destination_restore, error),
              "replacement destination restore gets a new challenge and scope");
        auto response =
            paused_receipt(native_command(peers, 1, TransferOp::destination_restore), *destination_replacement);
        seal_receipt(response);
        check(peers.peers[1].send(response), "replacement destination measured native-read receipt sent");
        peers.until(
            [&] {
                const auto phase = host.native_transfer(1)->phase;
                return phase == NativeTransferPhase::destination_restored || phase == NativeTransferPhase::committed;
            },
            "replacement destination receipt preserves the durable side of ownership commit");
    };
    if (recovery == 3) {
        recover_destination();
    }
    const auto commit_revision = host.durable_revision();
    check(host.commit_native_transfer(1, commit_revision, error) && host.durable_revision() == commit_revision + 1 &&
              host.commit_native_transfer(1, commit_revision, error),
          "matching live receipt scopes commit exactly once");
    check(host.locations().avatar(1)->active && host.locations().avatar(1)->location == destination &&
              host.locations().avatar(1)->generation == 3,
          "single destination avatar owns higher generation after durable commit");
    PeerConfig visiting_config;
    check(host.peer_config_at(Role::player, 1, destination, visiting_config, error) &&
              visiting_config.primary_owner == 1 && visiting_config.native_load_slot == 1 &&
              visiting_config.resident_owners == (paired ? 6u : 2u),
          "traveler config controls its canonical owner independently from destination native load slot");
    if (recovery == 4) {
        recover_destination();
        check(host.locations().avatar(1)->generation == 4 && host.locations().transfer(1)->destination_generation == 10,
              "postcommit recovery advances avatar and restored worker generation without reviving source ownership");
    }
    check(!host.request_native_transfer(1, host.durable_revision(), TransferOp::source_restore, error),
          "committed ownership cannot roll back to source");
    check(host.request_native_transfer(1, host.durable_revision(), TransferOp::destination_activate, error),
          "native activation requested only after durable commit");
    command = native_command(peers, 1, TransferOp::destination_activate);
    auto activated = paused_receipt(command, destination_replacement ? *destination_replacement : destination_process);
    activated.transfer.pause_before = 1;
    activated.transfer.pause_after = 0;
    seal_receipt(activated);
    const auto before = peers.packets[1].size();
    check(peers.peers[1].send(activated), "nonadvancing activation challenge sent");
    peers.until([&] { return peers.packets[1].size() > before && peers.packets[1].back().kind == Kind::reject; },
                "activation requires original AI to resume");
    ++activated.transfer.ai_end;
    seal_receipt(activated);
    check(peers.peers[1].send(activated), "resumed original-AI witness sent");
    peers.until([&] { return host.native_transfer(1)->phase == NativeTransferPhase::destination_active; },
                "native activation receipt published");
    const auto cleanup_revision = host.durable_revision();
    check(!host.finish_native_transfer(1, cleanup_revision, error) &&
              error == "native_transfer_destination_baseline_required",
          "completion requires restored avatar in fresh baseline");
    Packet baseline;
    baseline.kind = Kind::scene_begin;
    baseline.scene = destination_replacement ? 2 : 1;
    baseline.baseline = 2;
    baseline.count = paired ? 2 : 1;
    check(peers.peers[1].send(baseline), "fresh destination baseline begins");
    baseline.kind = Kind::entity;
    baseline.entity = actor(301, 1);
    check(peers.peers[1].send(baseline), "restored native avatar published");
    if (paired) {
        baseline.entity = actor(202, 2);
        check(peers.peers[1].send(baseline), "original resident remains in paired baseline");
    }
    baseline.kind = Kind::scene_end;
    check(peers.peers[1].send(baseline), "fresh destination baseline ends");
    peers.until(
        [&] {
            host.finish_native_transfer(1, cleanup_revision, error);
            return error == "native_transfer_source_exit_not_observed";
        },
        "live source process prevents final cleanup");
    source_process.close();
    check(host.finish_native_transfer(1, cleanup_revision, error) && host.durable_revision() == cleanup_revision + 1 &&
              host.finish_native_transfer(1, cleanup_revision, error),
          "retained signaled process handle permits durable cleanup and exact retry");
    check(host.native_transfer(1)->phase == NativeTransferPhase::complete &&
              host.native_transfer(1)->source_exit_code == 37 &&
              !host.locations().location_leases().at(source).active && !host.locations().transfer(1) &&
              host.transfer_preparation(1)->matches(preparation),
          "completion preserves exit observation and immutable request identity after reservation removal");
    if (paired) {
        global_checkpoint_after_transfer(host, peers, destination_process, config, directory);
        return;
    }
    if (recovery == 6) {
        terminal_restore(host, peers, destination_process, 1, false, 1);
    }
    if (recovery == 9 || recovery == 10) {
        auto pending = preparation;
        pending.id = 2;
        pending.expected_revision = host.durable_revision();
        pending.avatar_generation = 3;
        pending.source = destination;
        pending.destination = unrelated;
        pending.source_worker = 12;
        pending.source_worker_generation = 9;
        pending.destination_worker = 13;
        pending.destination_worker_generation = 4;
        check(host.prepare_transfer(pending, error),
              "later travel reserves a new target without retiring the last-good artifact");
        WorkerProcess third_process;
        check(host.register_local_worker_process(unrelated, 13, 4, third_process.process.dwProcessId,
                                                 third_process.created, error) &&
                  host.begin_native_transfer(2, host.durable_revision(), 2, 2, error),
              "new unbound transfer keeps its previous terminal artifact head");
        if (recovery == 10) {
            auto unbound = paused_receipt(native_command(peers, 1, TransferOp::source_fence), destination_process);
            unbound.transfer.artifact = receipt.transfer.artifact;
            unbound.transfer.artifact_bytes = receipt.transfer.artifact_bytes;
            seal_receipt(unbound);
            check(peers.peers[1].send(unbound),
                  "later source export receipt exists without a validated immutable pair");
            peers.until([&] { return host.native_transfer(2)->phase == NativeTransferPhase::source_fenced; },
                        "later unbound source receipt published");
        }
        check(host.native_transfer(1) && host.recovery_artifact_transfer(1) == 1,
              "new preparation and source receipt cannot evict the prior validated artifact");
        check(!host.recover_native_transfer_worker(2, host.durable_revision(), true, 12, 10, error),
              "unproven death or prior latest artifact prevents checkpoint fallback from a new attempt");
        terminal_restore(host, peers, destination_process, 1, false, 1);
        check(!host.native_transfer(2) && !host.transfer_preparation(2) && !host.locations().transfer(2),
              "last-good recovery atomically abandons only the later unbound attempt");
    }
    const auto completed_witness =
        (recovery == 0 || recovery == 5 || recovery == 8)
            ? return_home(host, peers, destination_process, preparation, recovery == 5, recovery == 8)
            : receipt.transfer.state;
    const uint64_t completed_id = (recovery == 0 || recovery == 5 || recovery == 8) ? 2 : 1;
    peers.motion_and_action(52);
    peers.stop();
    host.stop();
    config.offline = true;
    Coordinator reopened;
    check(reopened.start(config, error) &&
              reopened.native_transfer(completed_id)->phase == NativeTransferPhase::complete &&
              reopened.native_transfer(completed_id)->source_receipt.state == completed_witness,
          "schema7 durable reopen preserves authenticated witness and terminal identity");
    reopened.stop();
    if (recovery == 0) {
        std::vector<uint8_t> durable;
        {
            DurableStore store;
            check(store.open(config.state_directory, durable, error), "native schema migration snapshot opened");
        }
        state::Reader in{durable};
        in.at = preparation_offset(durable);
        const auto preparations = in.word();
        for (uint32_t n = 0; n < preparations; ++n) {
            for (size_t b = 0; b < 200 / 8; ++b) {
                in.wide();
            }
        }
        if (in.word()) {
            for (size_t b = 0; b < 200 / 8; ++b) {
                in.wide();
            }
            in.wide();
        }
        for (unsigned n = 0; n < 4; ++n) {
            in.word();
        }
        check(in.word() == 1, "terminal return retains one bounded native record");
        in.wide();
        for (size_t b = 0; b < 200 / 8; ++b) {
            in.wide();
        }
        in.word();
        in.word();
        for (unsigned n = 0; n < 13; ++n) {
            in.wide();
        }
        in.word();
        in.word();
        in.wide();
        in.word();
        in.wide();
        in.word();
        const auto extension_at = in.at;
        in.word();
        for (unsigned n = 0; n < 2; ++n) {
            in.word();
            in.wide();
        }
        for (unsigned n = 0; n < 2; ++n) {
            if (in.word()) {
                in.word();
                in.wide();
                in.wide();
                in.wide();
                in.word();
            }
        }
        const auto schema7_end = in.at;
        in.word();
        for (unsigned n = 0; n < 3; ++n) {
            in.wide();
        }
        if (in.word()) {
            in.data(576);
        }
        const auto schema8_end = in.at;
        in.wide();
        const auto extension_end = in.at;
        for (uint8_t schema : {uint8_t(6), uint8_t(7), uint8_t(8)}) {
            {
                DurableStore store;
                std::vector<uint8_t> previous;
                check(store.open(config.state_directory, previous, error), "native schema6/7/8 migration writer opens");
                auto legacy = durable;
                legacy.resize(legacy.size() - 8);
                const auto remove_at = schema == 6 ? extension_at : schema == 7 ? schema7_end : schema8_end;
                legacy.erase(legacy.begin() + remove_at, legacy.begin() + extension_end);
                legacy[4] = schema;
                check(store.commit(legacy, error), "actual native record encoded in prior native journal layout");
            }
            Coordinator legacy;
            check(legacy.start(config, error) && legacy.native_transfer(2)->phase == NativeTransferPhase::complete &&
                      legacy.native_transfer(2)->source_receipt.state == completed_witness,
                  "schema6/7/8 full native receipt and artifact record migrates without reinterpretation");
            legacy.stop();
        }
        for (unsigned challenge = 0; challenge < 4; ++challenge) {
            {
                DurableStore store;
                std::vector<uint8_t> previous;
                check(store.open(config.state_directory, previous, error), "malformed recovery record writer opens");
                auto corrupted = durable;
                if (challenge == 0) {
                    corrupted[extension_at] = 2;
                }
                if (challenge == 1) {
                    set_wide(corrupted, extension_at + 8, 0); // Nonzero source PID with absent creation identity.
                }
                if (challenge == 2) {
                    corrupted[extension_at + 28] = 2; // Unknown source death-proof flag.
                }
                if (challenge == 3) {
                    corrupted.resize(extension_end - 1);
                }
                check(store.commit(corrupted, error), "checksummed malformed recovery extension published");
            }
            Coordinator malformed;
            check(!malformed.start(config, error) &&
                      (error == "native_transfer_record_invalid" || error == "universe_state_invalid"),
                  "malformed rollback flag process identity death flag and truncation refuse durable recovery");
        }
    }
}
