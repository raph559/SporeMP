// HOST and real TLS only. Actual child-process identity is checked; no game runs.
void preload_flow() {
    WorkerProcess owner_process, reader_process;
    CoordinatorConfig config;
    config.port = 0;
    auto& identity = config.session.identity;
    identity.build = digest(31);
    identity.executable = owner_process.executable;
    identity.content = digest(33);
    identity.fixture = digest(34);
    identity.world.fill(digest(34));
    const auto directory =
        std::filesystem::current_path() / L"transfer-host" /
        (L"preload-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    check(std::filesystem::create_directories(directory / L"state"), "preload HOST store created");
    config.state_directory = directory / L"state";
    std::string error;
    Coordinator created;
    check(created.start(config, error), "preload universe created");
    created.stop();
    config.offline = true;
    Coordinator sealed;
    check(sealed.start(config, error) && sealed.commit_checkpoint(checkpoint(identity), 1, error),
          "preload selected head");
    check(sealed.commit_locations(
              2,
              [](LocationLedger& ledger, std::string& why) {
                  if (!ledger.add_location({source, 11, 5, 2, digest(33), true, 0}, why) ||
                      !ledger.place_avatar(1, source, why) || !ledger.place_avatar(2, source, why)) {
                      return false;
                  }
                  for (unsigned n = 1; n <= 4; ++n) {
                      if (!ledger.set_global_owner(static_cast<GlobalSystem>(n), {11, 5}, why)) {
                          return false;
                      }
                  }
                  return true;
              },
              error),
          "preload four global leases");
    sealed.stop();
    config.offline = false;
    Coordinator host;
    check(host.start(config, error), "preload online host");
    PeerConfig authority, player;
    check(host.peer_config_at(Role::authority, 0, source, authority, error) &&
              host.peer_config_at(Role::player, 1, source, player, error),
          "preload configs");
    Peer owner, reader;
    std::vector<Packet> owner_packets, reader_packets;
    auto pump = [&] {
        CoordinatorEvent outgoing;
        while (host.poll(outgoing)) {};
        Event event;
        while (owner.poll(event)) {
            if (event.kind == EventKind::packet) {
                owner_packets.push_back(event.packet);
            }
        }
        while (reader.poll(event)) {
            if (event.kind == EventKind::packet) {
                reader_packets.push_back(event.packet);
            }
        }
    };
    auto until = [&](auto predicate, const char* why) {
        const auto deadline = GetTickCount64() + 5000;
        do {
            pump();
            if (predicate()) {
                check(true, why);
                return;
            }
            Sleep(1);
        } while (GetTickCount64() < deadline);
        check(false, why);
    };
    check(owner.start_preload(authority, 1, owner_process.process.dwProcessId, owner_process.created, error) &&
              reader.start_preload(player, 1, reader_process.process.dwProcessId, reader_process.created, error),
          "preload peers start");
    until([&] { return !owner_packets.empty() && !reader_packets.empty(); },
          "authenticated grants arrive before HELLO");
    check(owner_packets[0].kind == Kind::preload_grant && owner_packets[0].preload.producer_mask == 15 &&
              reader_packets[0].kind == Kind::preload_grant && reader_packets[0].preload.producer_mask == 0 &&
              owner_packets[0].preload.worker == reader_packets[0].preload.worker &&
              owner_packets[0].preload.checkpoint_revision == 2 && host.durable_revision() == 3,
          "reader at owner location cannot produce or mutate ledger");
    check(owner.renew_preload(2) && reader.renew_preload(2) && !owner.renew_preload(2), "monotonic renewal nonce");
    until([&] { return owner_packets.size() == 2 && reader_packets.size() == 2; },
          "independent renewal works without scene");
    check(owner_packets.back().preload.nonce == 2 &&
              owner_packets.back().preload.checkpoint == owner_packets.front().preload.checkpoint,
          "renewal pins selected head");
    Peer normal;
    check(normal.start(authority, error), "preload never claims gameplay Session");
    bool welcomed = false;
    until(
        [&] {
            Event event;
            while (normal.poll(event)) {
                if (event.kind == EventKind::packet && event.packet.kind == Kind::welcome) {
                    welcomed = true;
                }
            }
            return welcomed;
        },
        "normal authority HELLO follows preload");
    normal.stop();
    reader.stop();
    pump();
    Peer forged;
    auto bad = authority;
    bad.credential[0] ^= 1;
    check(forged.start_preload(bad, 1, reader_process.process.dwProcessId, reader_process.created, error),
          "forged grant request sent");
    bool rejected = false;
    until(
        [&] {
            Event event;
            while (forged.poll(event)) {
                if (event.kind == EventKind::packet && event.packet.kind == Kind::reject) {
                    rejected = true;
                }
            }
            return rejected;
        },
        "wrong credential denied before native load");
    forged.stop();
    owner_process.close();
    check(owner.renew_preload(3), "closed-process renewal request queued");
    until([&] { return !owner_packets.empty() && owner_packets.back().kind == Kind::reject; },
          "closed process cannot renew live grant");
    owner.stop();
    host.stop();
}
