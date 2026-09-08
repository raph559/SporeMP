#pragma once
#include <windows.h>
#include <array>

namespace sporemp {
using Sha256 = std::array<char, 65>;
// Host OS operations only. Never call from DllMain.
bool sha256_file(const wchar_t* path, Sha256& digest) noexcept;
bool matches_candidate(const Sha256& digest) noexcept;
bool absolute_local_path(const wchar_t* path) noexcept;

struct RuntimeIdentity {
    int major;
    int minor;
    int build;
    int game_type;
};

class DiagnosticSession {
public:
    bool initialize(const wchar_t* executable, const wchar_t* directory,
                    RuntimeIdentity runtime) noexcept;
    void dispose() noexcept;
    bool native_path(unsigned int id, const wchar_t* value) noexcept;
private:
    HANDLE log_ = INVALID_HANDLE_VALUE;
    DWORD engine_thread_ = 0;
    Sha256 digest_{};
    RuntimeIdentity runtime_{};
    bool event(const char* name) noexcept;
};
}
