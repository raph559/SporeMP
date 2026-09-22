#pragma once
#include "content_wire.h"

namespace sporemp::network {
struct ContentDispatch {uint64_t connection=0;ContentFrame frame;};
// Coordinator-thread adapter for authenticated, bounded content RPC messages.
// All timestamps are coordinator monotonic time, never values sent by peers.
class ContentControl {
public:
    ContentControl(Digest installed,WorldIdentity world):registry_(installed,world) {}
    bool connect(uint64_t connection,uint64_t player,bool authority);
    void disconnect(uint64_t connection);
    void fence();
    std::vector<ContentDispatch> receive(uint64_t connection,const ContentFrame&,uint64_t now_ms);
    const ContentRegistry& registry()const noexcept {return registry_;}
private:
    struct Upload {uint64_t transaction=0;uint32_t total=0;Digest hash{};std::vector<uint8_t> bytes;};
    struct Peer {uint64_t player=0,last_request=0,rate_window=0;uint32_t requests=0;bool authority=false;Upload observation;};
    ContentRegistry registry_;
    std::map<uint64_t,Peer> peers_;
};
}
