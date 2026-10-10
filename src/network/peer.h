#pragma once
#include "protocol.h"
#include <memory>

namespace sporemp::network {
struct PeerConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 27060;
    Digest certificate_pin{}, credential{};
    Identity identity{};
    Role role = Role::player;
    LocationId location{};
    uint64_t worker_generation = 0;
    uint64_t maintenance_transfer = 0, maintenance_scene = 0; // Scene is set after native load, before start().
    uint32_t native_load_slot = 0;
    // Zero keeps the qualified two-avatar fixture. A location with exactly
    // one leased avatar binds that owner's original primary player instead.
    uint32_t primary_owner = 0;
    // Canonical owner bits intended in the selected location. Maintenance may
    // include the pending restored player before its avatar lease activates.
    uint32_t resident_owners = 0;
    Digest secondary_save{};
    // Explicit diagnostic bootstrap; authority-only and never a save/transfer mode.
    bool experimental_foreign_species = false;
};
enum class EventKind {
    connected,
    packet,
    disconnected
};

struct Event {
    EventKind kind = EventKind::disconnected;
    Packet packet{};
    std::string detail;
};

// Construct/start only after SDK initialization, stop before disposal, never DllMain.
// One network thread; send/poll are bounded and never call the engine or wait for I/O.
// send assigns a strictly increasing sequence; root must supply scene/baseline.
class Peer {
public:
    Peer();
    ~Peer();
    Peer(const Peer&) = delete;
    Peer& operator=(const Peer&) = delete;
    bool start(const PeerConfig&, std::string& error);
    // Dedicated TLS connection without HELLO/Session admission. Only nonce and
    // process identity are caller inputs; config supplies authenticated scope.
    // Keep this Peer alive and renew before expiry while the managed game lives.
    bool start_preload(const PeerConfig&, uint64_t nonce, uint32_t process_id, uint64_t process_created,
                       std::string& error);
    bool renew_preload(uint64_t nonce);
    // Restarts only once the OS reports the former network thread has exited.
    // Returns false with network_thread_still_exiting while cleanup is pending.
    bool try_restart(const PeerConfig&, std::string& error);
    bool send(Packet);
    bool poll(Event&);
    // A snapshot for engine-thread input fencing, not proof of remote lease
    // ownership or a native quiescence acknowledgment.
    bool transport_active() const noexcept;
    // Fence a failed native session without joining the network thread in an engine callback.
    void request_stop() noexcept;
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Strict line-based local config, UTF-8/ASCII; exact names in docs/m06-network.md.
bool valid_foreign_bootstrap(const PeerConfig&) noexcept;
bool read_peer_config(const std::wstring& path, PeerConfig&, std::string&);
bool write_peer_config(const std::wstring& path, const PeerConfig&);
} // namespace sporemp::network
