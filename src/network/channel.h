#pragma once
#include "tls.h"
#include "peer.h"
#include <deque>
#include <mutex>
#include <thread>

namespace sporemp::network::detail {
class Channel {
public:
    ~Channel();
    void start_client(const PeerConfig&);
    void start_server(SOCKET,TlsIdentity*);
    bool send(Packet);
    bool poll(Event&);
    void stop();
    bool finished() const noexcept {return finished_;}
    bool exited() {return finished_&&thread_.joinable()&&WaitForSingleObject(thread_.native_handle(),0)==WAIT_OBJECT_0;}
private:
    std::mutex mutex_;
    std::deque<Packet> outgoing_;
    std::deque<Event> incoming_;
    std::thread thread_;
    std::atomic<bool> stop_{false},finished_{false};
    std::atomic<uint64_t> session_{0};
    uint64_t sequence_=0;
    bool client_=false;
    bool push(Event);
    void run(SOCKET,TlsIdentity*,PeerConfig);
};
}
