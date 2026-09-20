#pragma once
#include "pipe.h"
#include <filesystem>
#include <functional>
#include <memory>

namespace sporemp::worker {
using Log = std::function<void(const char*, const std::string&)>;
using Clock = std::function<uint64_t()>;
class Supervisor {
public:
    Supervisor(const Generation& generation, const std::filesystem::path& run, Log log,
        Clock clock = [] { return GetTickCount64(); });
    ~Supervisor();
    void attach(HANDLE process, DWORD pid);
    void tick();
    void exited(DWORD exit_code);
private:
    void new_control();
    void publish(const char* state);
    void stop(const char* reason);
    Generation generation_;
    std::wstring sid_;
    std::filesystem::path run_;
    Log log_;
    Clock clock_;
    Pipe engine_;
    std::unique_ptr<Pipe> control_;
    HANDLE job_ = nullptr, process_ = nullptr;
    DWORD pid_ = 0;
    Message latest_{}, reply_{};
    uint64_t outgoing_ = 0, incoming_ = 0, pending_ = 0;
    Op pending_op_ = Op::status;
    uint64_t control_serial_ = 0, pending_control_ = 0;
    uint64_t started_ = 0, last_heartbeat_ = 0, last_ai_ = 0, last_publish_ = 0;
    uint64_t control_since_ = 0, request_since_ = 0, stopping_ = 0;
    bool control_connected_ = false, response_ready_ = false, response_sent_ = false;
    bool requested_stop_ = false, failed_ = false;
};
}
