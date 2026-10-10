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
    void start_client(const PeerConfig&, const Packet* initial = nullptr);
    void start_server(SOCKET, TlsIdentity*);
    bool send(Packet);
    bool poll(Event&);

    void request_stop() noexcept {
        stop_ = true;
    }

    void stop();

    bool finished() const noexcept {
        return finished_;
    }

    bool active() const noexcept {
        return !stop_ && !finished_;
    }

    bool exited() {
        return finished_ && thread_.joinable() && WaitForSingleObject(thread_.native_handle(), 0) == WAIT_OBJECT_0;
    }

private:
    std::mutex mutex_;
    std::deque<Packet> outgoing_;
    std::deque<Event> incoming_;
    std::thread thread_;
    std::atomic<bool> stop_{false}, finished_{false};
    std::atomic<uint64_t> session_{0};
    uint64_t sequence_ = 0;
    bool client_ = false;
    LocationId client_location_{};
    uint64_t client_worker_generation_ = 0;
    uint32_t client_native_load_slot_ = 0;
    struct StreamState;
    bool push(Event);
    void run(SOCKET socket, TlsIdentity* identity, const PeerConfig& config);
    void exchange(TlsStream& stream, std::string& error);
    std::vector<uint8_t> take_outgoing_batch();
    size_t deliver_packets(const std::vector<uint8_t>& plain, StreamState& state, std::string& error);
    bool deliver(const Packet& packet, StreamState& state, std::string& error);
    bool keep_alive(StreamState& state, std::string& error);
    void push_disconnected(const std::string& error);
};
} // namespace sporemp::network::detail
