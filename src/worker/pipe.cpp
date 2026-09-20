#include "pipe.h"
#include <sddl.h>
#include <vector>
#include <stdexcept>

namespace sporemp::worker {
Pipe::Pipe() {
    connect_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    read_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    write_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!connect_.hEvent || !read_.hEvent || !write_.hEvent) fail(GetLastError());
}
Pipe::~Pipe() {
    close();
    for (auto event : {connect_.hEvent, read_.hEvent, write_.hEvent}) if (event) CloseHandle(event);
}
bool Pipe::fail(DWORD error) { failed_ = true; error_ = error; return false; }
void Pipe::close() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipe_, nullptr);
        // After cancellation, keep OVERLAPPED storage alive until Windows completes it.
        // A synchronous ERROR_PIPE_CONNECTED can leave an unsignaled connect
        // event without any pending request. Wait only for operations actually
        // submitted as ERROR_IO_PENDING, or that unused event can hang teardown.
        DWORD ignored = 0;
        if (connecting_) GetOverlappedResult(pipe_, &connect_, &ignored, TRUE);
        if (reading_) GetOverlappedResult(pipe_, &read_, &ignored, TRUE);
        if (writing_) GetOverlappedResult(pipe_, &write_, &ignored, TRUE);
        CloseHandle(pipe_); pipe_ = INVALID_HANDLE_VALUE;
    }
    connecting_ = reading_ = writing_ = connected_ = false;
}
bool Pipe::server(const std::wstring& name, const std::wstring& sid, bool operator_access) {
    if (pipe_ != INVALID_HANDLE_VALUE || failed_) return false;
    PSID checked = nullptr;
    if (!ConvertStringSidToSidW(sid.c_str(), &checked)) return fail(ERROR_INVALID_SID);
    LocalFree(checked);
    // Client rights exclude FILE_CREATE_PIPE_INSTANCE (bit 4). Administrators are
    // admitted only at the operator endpoint; remote pipe clients are rejected.
    const auto sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + sid + L")" +
        (operator_access ? std::wstring(L"(A;;0x12019b;;;BA)") : std::wstring());
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return fail(GetLastError());
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    pipe_ = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, frame_size * 4, frame_size * 4, 0, &attributes);
    const DWORD error = GetLastError(); LocalFree(descriptor);
    if (pipe_ == INVALID_HANDLE_VALUE) return fail(error);
    server_ = true;
    if (ConnectNamedPipe(pipe_, &connect_)) connected_ = true;
    else {
        const auto status = GetLastError();
        if (status == ERROR_PIPE_CONNECTED) connected_ = true;
        else if (status == ERROR_IO_PENDING) connecting_ = true;
        else return fail(status);
    }
    return true;
}
bool Pipe::client(const std::wstring& name, DWORD server_pid) {
    if (pipe_ != INVALID_HANDLE_VALUE || failed_ || !server_pid) return false;
    pipe_ = CreateFileW(name.c_str(), FILE_READ_DATA | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
        0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (pipe_ == INVALID_HANDLE_VALUE) return fail(GetLastError());
    ULONG actual = 0;
    if (!GetNamedPipeServerProcessId(pipe_, &actual) || actual != server_pid) return fail(ERROR_ACCESS_DENIED);
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr)) return fail(GetLastError());
    connected_ = true; return true;
}
bool Pipe::complete(OVERLAPPED& operation, bool& pending, DWORD& transferred) {
    if (!pending) return false;
    if (!GetOverlappedResult(pipe_, &operation, &transferred, FALSE)) {
        auto error = GetLastError();
        if (error == ERROR_IO_INCOMPLETE) return false;
        pending = false; return fail(error);
    }
    pending = false; return true;
}
bool Pipe::connected(DWORD expected_pid) {
    if (failed_ || pipe_ == INVALID_HANDLE_VALUE) return false;
    if (connecting_) { DWORD ignored = 0; if (complete(connect_, connecting_, ignored)) connected_ = true; }
    if (!connected_) return false;
    if (server_ && expected_pid) {
        ULONG actual = 0;
        if (!GetNamedPipeClientProcessId(pipe_, &actual) || actual != expected_pid) return fail(ERROR_ACCESS_DENIED);
    }
    return true;
}
bool Pipe::receive(Message& message) {
    if (!connected_ || failed_) return false;
    DWORD size = 0;
    if (reading_) { if (!complete(read_, reading_, size)) return false; }
    else {
        ResetEvent(read_.hEvent);
        if (!ReadFile(pipe_, incoming_.data(), frame_size, &size, &read_)) {
            auto error = GetLastError();
            if (error == ERROR_IO_PENDING) { reading_ = true; return false; }
            return fail(error);
        }
    }
    if (!decode(incoming_.data(), size, message)) return fail(ERROR_INVALID_DATA);
    return true;
}
bool Pipe::writable() {
    if (!connected_ || failed_) return false;
    if (writing_) {
        DWORD size = 0;
        if (!complete(write_, writing_, size)) return false;
        if (size != frame_size) return fail(ERROR_WRITE_FAULT);
    }
    return true;
}
bool Pipe::send(const Message& message) {
    if (!writable()) return false;
    outgoing_ = encode(message); DWORD size = 0;
    ResetEvent(write_.hEvent);
    if (!WriteFile(pipe_, outgoing_.data(), frame_size, &size, &write_)) {
        auto error = GetLastError();
        if (error == ERROR_IO_PENDING) { writing_ = true; return true; }
        return fail(error);
    }
    return size == frame_size || fail(ERROR_WRITE_FAULT);
}
std::wstring pipe_name(const Generation& generation, bool control) {
    return L"\\\\.\\pipe\\SporeMP-M04-" + generation_hex(generation) + (control ? L"-control" : L"-engine");
}
std::wstring current_sid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) throw std::runtime_error("Worker token unavailable");
    DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<uint8_t> bytes(size);
    const bool ok = GetTokenInformation(token, TokenUser, bytes.data(), size, &size) != 0;
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!ok || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid)) throw std::runtime_error("Worker SID unavailable");
    std::wstring value(sid); LocalFree(sid); return value;
}
}
