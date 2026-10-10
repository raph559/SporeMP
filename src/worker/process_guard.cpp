#include "process_guard.h"
#include "pipe.h"
#include <tlhelp32.h>
#include <sddl.h>
#include <vector>
#include <stdexcept>

namespace sporemp::worker {
namespace {
// The running processes with this executable name, other than the one to ignore.
std::vector<DWORD> processes_named(const std::wstring& executable_name, DWORD ignored_pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Could not enumerate game processes before launch");
    }
    std::vector<DWORD> matches;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID != ignored_pid && _wcsicmp(entry.szExeFile, executable_name.c_str()) == 0) {
                matches.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return matches;
}

// Whether a process runs as the user with this SID. One that exited during enumeration does not; one whose
// identity cannot be read is an error, never a pass.
bool runs_as(DWORD pid, const std::wstring& user_sid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) {
            return false; // exited during enumeration
        }
        throw std::runtime_error("Existing game process identity could not be checked");
    }
    HANDLE token = nullptr;
    const bool opened = OpenProcessToken(process, TOKEN_QUERY, &token) != 0;
    CloseHandle(process);
    if (!opened) {
        throw std::runtime_error("Existing game profile could not be checked");
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<uint8_t> bytes(size);
    const bool read = GetTokenInformation(token, TokenUser, bytes.data(), size, &size) != 0;
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!read || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid)) {
        throw std::runtime_error("Existing game SID unavailable");
    }
    const bool same = user_sid == sid;
    LocalFree(sid);
    return same;
}
} // namespace

void reject_process_in_profile(const std::wstring& executable_name, DWORD ignored_pid) {
    const auto own = current_sid();
    for (auto pid : processes_named(executable_name, ignored_pid)) {
        if (runs_as(pid, own)) {
            throw std::runtime_error("A game is already using this Windows profile");
        }
    }
}
} // namespace sporemp::worker
