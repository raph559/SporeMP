#pragma once
#include "peer.h"
#include "session.h"
#include <memory>

namespace sporemp::network {
struct CoordinatorConfig {std::string bind_host="127.0.0.1";uint16_t port=27060;SessionConfig session{};};
struct CoordinatorEvent {uint64_t connection=0;Event event{};};
class Coordinator {
public:
    Coordinator();~Coordinator();
    bool start(CoordinatorConfig,std::string&);
    // Must be pumped by one coordinator thread. No game calls. Bounded per call.
    bool poll(CoordinatorEvent&);
    PeerConfig peer_config(Role role,uint64_t player=0) const;
    void stop();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
