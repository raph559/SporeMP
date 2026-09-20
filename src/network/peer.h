#pragma once
#include "protocol.h"
#include <memory>

namespace sporemp::network {
struct PeerConfig {
    std::string host="127.0.0.1";
    uint16_t port=27060;
    Digest certificate_pin{},credential{};
    Identity identity{};
    Role role=Role::player;
};
enum class EventKind { connected, packet, disconnected };
struct Event { EventKind kind=EventKind::disconnected; Packet packet{}; std::string detail; };
// Construct/start only after SDK initialization, stop before disposal, never DllMain.
// One network thread; send/poll are bounded and never call the engine or wait for I/O.
// send assigns a strictly increasing sequence; root must supply scene/baseline.
class Peer {
public:
    Peer(); ~Peer();
    Peer(const Peer&)=delete; Peer& operator=(const Peer&)=delete;
    bool start(const PeerConfig&,std::string& error);
    // Restarts only once the OS reports the former network thread has exited.
    // Returns false with network_thread_still_exiting while cleanup is pending.
    bool try_restart(const PeerConfig&,std::string& error);
    bool send(Packet);
    bool poll(Event&);
    void stop();
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
// Strict line-based local config, UTF-8/ASCII; exact names in docs/m06-network.md.
bool read_peer_config(const std::wstring& path,PeerConfig&,std::string&);
}
