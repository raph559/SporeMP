#include "channel.h"
#include <chrono>

namespace sporemp::network::detail {
namespace {
// What a client says first unless it was given another opening packet.
Packet make_hello(const PeerConfig& config) {
    Packet hello;
    hello.kind = Kind::hello;
    hello.role = config.role;
    hello.identity = config.identity;
    hello.credential = config.credential;
    hello.location = config.location;
    hello.worker_generation = config.worker_generation;
    hello.native_load_slot = config.native_load_slot;
    hello.maintenance_transfer = config.maintenance_transfer;
    hello.scene = config.maintenance_scene;
    return hello;
}
} // namespace

// What the network thread carries from one pass of its loop to the next. Only that thread touches it.
struct Channel::StreamState {
    // When the current pass finished receiving.
    std::chrono::steady_clock::time_point now;
    std::chrono::steady_clock::time_point last_received, last_ping;
    // The packets received since `rate_start`, which moves forward once it is more than a second old.
    std::chrono::steady_clock::time_point rate_start;
    size_t rate_count = 0;
    uint64_t last_sequence = 0;
};

Channel::~Channel() {
    stop();
}

void Channel::start_client(const PeerConfig& config, const Packet* initial) {
    client_ = true;
    client_location_ = config.location;
    client_worker_generation_ = config.worker_generation;
    client_native_load_slot_ = config.native_load_slot;
    const Packet hello = make_hello(config);
    send(initial ? *initial : hello);
    thread_ = std::thread([this, config] { run(INVALID_SOCKET, nullptr, config); });
}

void Channel::start_server(SOCKET socket, TlsIdentity* identity) {
    thread_ = std::thread([this, socket, identity] { run(socket, identity, {}); });
}

bool Channel::send(Packet packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_ || finished_ || outgoing_.size() >= queue_capacity) {
        stop_ = true;
        return false;
    }
    if (client_) {
        packet.sequence = ++sequence_;
        packet.session = session_;
        packet.location = client_location_;
        packet.worker_generation = client_worker_generation_;
        packet.native_load_slot = client_native_load_slot_;
    }
    outgoing_.push_back(packet);
    return true;
}

bool Channel::push(Event event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (incoming_.size() >= queue_capacity) {
        stop_ = true;
        return false;
    }
    incoming_.push_back(std::move(event));
    return true;
}

bool Channel::poll(Event& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (incoming_.empty()) {
        return false;
    }
    event = std::move(incoming_.front());
    incoming_.pop_front();
    return true;
}

void Channel::stop() {
    stop_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Channel::run(SOCKET socket, TlsIdentity* identity, const PeerConfig& config) {
    std::string error;
    if (client_) {
        socket = connect_socket(config.host, config.port, stop_, error);
    }
    if (socket == INVALID_SOCKET) {
        push({EventKind::disconnected, {}, error});
        finished_ = true;
        return;
    }
    TlsStream stream(socket);
    if (!stream.handshake(!client_, identity, config.certificate_pin, config.host, stop_, error)) {
        push({EventKind::disconnected, {}, error});
        finished_ = true;
        return;
    }
    push({EventKind::connected, {}, "tls12_authenticated_server"});
    exchange(stream, error);
    if (error.empty()) {
        error = stop_ ? "stopped_or_queue_full" : "disconnected";
    }
    push_disconnected(error);
    finished_ = true;
}

// Sends what is queued and delivers what arrives until the channel is stopped or a step fails. `error` names the
// step that failed and is left empty when none did.
void Channel::exchange(TlsStream& stream, std::string& error) {
    std::vector<uint8_t> plain;
    plain.reserve(32768);
    StreamState state;
    state.last_received = state.last_ping = state.rate_start = std::chrono::steady_clock::now();
    while (!stop_) {
        const std::vector<uint8_t> batch = take_outgoing_batch();
        if (!batch.empty() && !stream.send(batch.data(), batch.size(), error)) {
            break;
        }
        if (!stream.receive(plain, error)) {
            break;
        }
        state.now = std::chrono::steady_clock::now();
        if (state.now - state.rate_start > std::chrono::seconds(1)) {
            state.rate_start = state.now;
            state.rate_count = 0;
        }
        const size_t used = deliver_packets(plain, state, error);
        if (used) {
            plain.erase(plain.begin(), plain.begin() + static_cast<ptrdiff_t>(used));
        }
        if (!keep_alive(state, error)) {
            break;
        }
    }
}

// The encoded packets at the head of the outgoing queue, at most 32 of them.
std::vector<uint8_t> Channel::take_outgoing_batch() {
    std::vector<uint8_t> batch;
    batch.reserve(packet_bytes * 32);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t n = 0; n < 32 && !outgoing_.empty(); ++n) {
            Wire wire = encode(outgoing_.front());
            outgoing_.pop_front();
            batch.insert(batch.end(), wire.begin(), wire.end());
        }
    }
    return batch;
}

// Decodes the whole packets in `plain` and delivers them in order, stopping at the first one refused. Returns the
// bytes consumed; a packet that does not decode is not counted.
size_t Channel::deliver_packets(const std::vector<uint8_t>& plain, StreamState& state, std::string& error) {
    size_t used = 0;
    while (plain.size() - used >= packet_bytes) {
        Packet packet;
        if (!decode(plain.data() + used, packet_bytes, packet, error)) {
            stop_ = true;
            break;
        }
        used += packet_bytes;
        if (!deliver(packet, state, error)) {
            break;
        }
    }
    return used;
}

// One decoded packet: refused when the peer sends too fast, replays a sequence or names another session than the
// one a client was welcomed into; otherwise queued for the application. False ends the pass over received packets.
bool Channel::deliver(const Packet& packet, StreamState& state, std::string& error) {
    if (++state.rate_count > max_entities * 25 + 256) {
        error = "packet_rate_exceeded";
        stop_ = true;
        return false;
    }
    if (!packet.sequence || packet.sequence <= state.last_sequence) {
        error = "replayed_stream_sequence";
        stop_ = true;
        return false;
    }
    state.last_sequence = packet.sequence;
    if (client_ && packet.kind == Kind::welcome) {
        session_ = packet.session;
    }
    if (client_ && session_ && packet.session != session_) {
        error = "wrong_session_epoch";
        stop_ = true;
        return false;
    }
    state.last_received = state.now;
    if (packet.kind != Kind::ping && !push({EventKind::packet, packet, {}})) {
        error = "incoming_queue_full";
        return false;
    }
    // Server application handles ping so it can allocate the stream sequence.
    if (!client_ && packet.kind == Kind::ping && !push({EventKind::packet, packet, {}})) {
        error = "incoming_queue_full";
        return false;
    }
    return true;
}

// A client that has a session pings when its last ping is more than two seconds old, and either side gives up on a
// peer it has not heard from for more than ten. False ends the exchange.
bool Channel::keep_alive(StreamState& state, std::string& error) {
    if (client_ && session_ && state.now - state.last_ping > std::chrono::seconds(2)) {
        Packet ping;
        ping.kind = Kind::ping;
        if (!send(ping)) {
            error = "outgoing_queue_full";
            return false;
        }
        state.last_ping = state.now;
    }
    if (state.now - state.last_received > std::chrono::seconds(10)) {
        error = "peer_timeout";
        return false;
    }
    return true;
}

// Ensure terminal state remains observable even after a queue overflow.
void Channel::push_disconnected(const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (incoming_.size() >= queue_capacity) {
        incoming_.pop_front();
    }
    incoming_.push_back({EventKind::disconnected, {}, error});
}
} // namespace sporemp::network::detail
