#pragma once
#include "protocol.h"
#include <windows.h>

namespace sporemp::worker {
// One bounded overlapped operation per direction. poll/send never wait for a peer.
class Pipe {
public:
    Pipe();
    ~Pipe();
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    bool server(const std::wstring& name, const std::wstring& sid, bool operator_access);
    bool client(const std::wstring& name, DWORD server_pid);
    bool connected(DWORD expected_pid = 0);
    bool receive(Message& message);
    bool send(const Message& message);
    bool writable();
    void close();
    bool failed() const noexcept { return failed_; }
    DWORD error() const noexcept { return error_; }
    HANDLE handle() const noexcept { return pipe_; }
private:
    bool fail(DWORD error);
    bool complete(OVERLAPPED& operation, bool& pending, DWORD& transferred);
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    OVERLAPPED connect_{}, read_{}, write_{};
    Bytes incoming_{}, outgoing_{};
    bool connecting_ = false, reading_ = false, writing_ = false, connected_ = false;
    bool server_ = false, failed_ = false;
    DWORD error_ = 0;
};
std::wstring pipe_name(const Generation& generation, bool control);
std::wstring current_sid();
}
