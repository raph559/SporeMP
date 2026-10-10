#pragma once
#include "pipe.h"
#include <filesystem>
#include <functional>
#include <memory>

namespace sporemp::worker {
using Log = std::function<void(const char*, const std::string&)>;
using Clock = std::function<uint64_t()>;
// Supplied only by NativeHost after its launch-mode/configuration identity gate.
// The default keeps original AI progress mandatory for ordinary workers.
enum class ProgressRole {
    native_simulation,
    network_replica
};

class Supervisor {
public:
    Supervisor(
        const Generation& generation, const std::filesystem::path& run, Log log,
        Clock clock = [] { return GetTickCount64(); }, ProgressRole role = ProgressRole::native_simulation);
    ~Supervisor();
    void attach(HANDLE process, DWORD pid);
    void tick();
    void exited(DWORD exit_code);

private:
    void new_control();
    void publish(const char* state);
    void write_status(const char* state);
    void replace_status();
    void stop(const char* reason);
    bool receive_engine(uint64_t now);
    bool accept_engine_message(const Message& message, uint64_t now);
    bool accept_checkpoint_lease(uint64_t deadline);
    void record_projection(const Message& status, uint64_t now);
    bool accept_reply(const Message& reply);
    bool engine_within_deadlines(uint64_t now);
    bool serve_control(uint64_t now);
    void answer_control(Message& request, uint64_t now);
    void forward_to_engine(Message& request, uint64_t now);
    void flush_control(uint64_t now);
    const char* state_name(uint64_t now) const;
    Generation generation_;
    std::wstring sid_;
    std::filesystem::path run_;
    Log log_;
    Clock clock_;
    const ProgressRole role_;
    Pipe engine_;
    std::unique_ptr<Pipe> control_;
    HANDLE job_ = nullptr, process_ = nullptr;
    DWORD pid_ = 0;
    Message latest_{}, reply_{};
    uint64_t outgoing_ = 0, incoming_ = 0, pending_ = 0;
    Op pending_op_ = Op::status;
    uint64_t control_serial_ = 0, pending_control_ = 0;
    uint64_t started_ = 0, last_heartbeat_ = 0, last_app_ = 0, last_ai_ = 0, last_publish_ = 0;
    uint64_t last_projection_ = 0, projection_state_ = 0, projection_baseline_ = 0;
    uint64_t control_since_ = 0, request_since_ = 0, stopping_ = 0;
    uint64_t checkpoint_deadline_ = 0;
    bool control_connected_ = false, response_ready_ = false, response_sent_ = false;
    bool requested_stop_ = false, failed_ = false;
    bool projection_valid_ = false;
};
} // namespace sporemp::worker
